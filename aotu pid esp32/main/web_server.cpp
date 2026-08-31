/**
 * @file    web_server.cpp
 * @brief   HTTP + WebSocket 服务器 (esp_http_server)
 *
 * 路由:
 *   GET  /                     静态页面 (index.html, 内嵌 css/js)
 *   GET  /api/status           当前快照 JSON
 *   GET  /api/logs?from=0      日志列表 JSON
 *   POST /api/cmd              命令分发 (JSON body -> pid_manager/autotune_manager)
 *   GET  /ws                   WebSocket: 推送状态/日志, 接收 ping
 *
 * 所有 /api/cmd 通过 pid_manager 同步执行 (内部已串行化, 不阻塞 WS)。
 */
#include "web_server.h"
#include "app_config.h"
#include "device_status.h"
#include "pid_manager.h"
#include "autotune_manager.h"
#include "wifi_manager.h"

#include "esp_http_server.h"
#include "esp_log.h"
#include "cJSON.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

namespace dome {
namespace web_server {

static const char *TAG = "web";
static httpd_handle_t s_http = nullptr;

/* ---------------- JSON 组装 ---------------- */

static void snap_json(Snapshot &s, cJSON *root)
{
    char buf[32];
    cJSON_AddBoolToObject(root, "online", s.online);
    cJSON_AddStringToObject(root, "product", s.product);
    cJSON_AddStringToObject(root, "fw", s.fw);
    cJSON_AddStringToObject(root, "hw", s.hw);
    cJSON_AddStringToObject(root, "mcu", s.mcu);
    cJSON_AddStringToObject(root, "build", s.build);
    cJSON_AddNumberToObject(root, "proto_ver", s.proto_ver);
    cJSON_AddNumberToObject(root, "dev_state", s.dev_state);
    cJSON_AddNumberToObject(root, "flash_valid", s.flash_valid);
    cJSON_AddNumberToObject(root, "last_err", s.last_err);
    cJSON_AddNumberToObject(root, "stm_uptime_ms", s.stm_uptime_ms);
    cJSON_AddNumberToObject(root, "crc_errs", s.stm_crc_err + s.crc_errs);
    cJSON_AddNumberToObject(root, "frame_errs", s.stm_frame_err);
    cJSON_AddNumberToObject(root, "timeout_errs", s.stm_timeout_err + s.timeouts);
    cJSON_AddNumberToObject(root, "range_errs", s.stm_range_err);
    cJSON_AddNumberToObject(root, "tx_frames", s.tx_frames);
    cJSON_AddNumberToObject(root, "rx_frames", s.rx_frames);
    cJSON_AddNumberToObject(root, "esp_uptime_ms", s.esp_uptime_ms);
    cJSON_AddStringToObject(root, "ip", s.ip);
    cJSON_AddBoolToObject(root, "wifi_sta", s.wifi_sta);
    cJSON_AddStringToObject(root, "ssid", s.ssid);

    cJSON *loops = cJSON_AddArrayToObject(root, "loops");
    for (uint8_t l = 0; l < PID_LOOP_MAX; l++) {
        cJSON *o = cJSON_CreateObject();
        ParamInfo &p = s.params[l];
        LoopInfo &r = s.rt[l];
        snprintf(buf, sizeof(buf), "%u", l);
        cJSON_AddStringToObject(o, "id", buf);
        cJSON_AddNumberToObject(o, "kp", p.kp);
        cJSON_AddNumberToObject(o, "ki", p.ki);
        cJSON_AddNumberToObject(o, "kd", p.kd);
        cJSON_AddNumberToObject(o, "target", r.target);
        cJSON_AddNumberToObject(o, "feedback", r.feedback);
        cJSON_AddNumberToObject(o, "output", r.output);
        cJSON_AddNumberToObject(o, "err", r.err);
        cJSON_AddNumberToObject(o, "out_min", p.out_min);
        cJSON_AddNumberToObject(o, "out_max", p.out_max);
        cJSON_AddNumberToObject(o, "integ_lim", p.integ_lim);
        cJSON_AddNumberToObject(o, "sample_time", p.sample_time);
        cJSON_AddNumberToObject(o, "deadband", p.deadband);
        cJSON_AddNumberToObject(o, "filter", p.filter);
        cJSON_AddNumberToObject(o, "dir", p.dir);
        cJSON_AddNumberToObject(o, "mode", r.mode);
        cJSON_AddNumberToObject(o, "state", r.state);
        cJSON_AddNumberToObject(o, "fault", r.fault);
        cJSON_AddNumberToObject(o, "run_ms", r.run_ms);
        cJSON_AddItemToArray(loops, o);
    }


}

static esp_err_t send_json(httpd_req_t *req, cJSON *root)
{
    const char *s = cJSON_PrintUnformatted(root);
    httpd_resp_set_type(req, "application/json");
    esp_err_t e = httpd_resp_send(req, s, HTTPD_RESP_USE_STRLEN);
    cJSON_free((void *)s);
    return e;
}

/* ---------------- HTTP 处理 ---------------- */

extern "C" {
extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[]   asm("_binary_index_html_end");
}

static esp_err_t h_index(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html_start,
                           (int)(index_html_end - index_html_start));
}

static esp_err_t h_status(httpd_req_t *req)
{
    Snapshot s;
    device_status::get_snapshot(&s);
    cJSON *root = cJSON_CreateObject();
    snap_json(s, root);
    esp_err_t e = send_json(req, root);
    cJSON_Delete(root);
    return e;
}

static esp_err_t h_logs(httpd_req_t *req)
{
    char q[64] = "";
    char from_str[16] = "";
    int from = 0;
    if (httpd_req_get_url_query_str(req, q, sizeof(q)) == ESP_OK &&
        httpd_query_key_value(q, "from", from_str, sizeof(from_str)) == ESP_OK) {
        from = atoi(from_str);
    }
    cJSON *root = cJSON_CreateObject();
    cJSON *arr = cJSON_AddArrayToObject(root, "logs");
    int n = device_status::log_count();
    for (int i = (from > 0) ? from : 0; i < n; i++) {
        LogEntry e;
        if (device_status::log_get(i, &e)) {
            cJSON *o = cJSON_CreateObject();
            cJSON_AddNumberToObject(o, "t", e.tick_ms);
            cJSON_AddStringToObject(o, "text", e.text);
            cJSON_AddItemToArray(arr, o);
        }
    }
    cJSON_AddNumberToObject(root, "total", n);
    esp_err_t er = send_json(req, root);
    cJSON_Delete(root);
    return er;
}

/* 命令分发 */
static esp_err_t h_cmd(httpd_req_t *req)
{
    char body[768] = "";
    int len = req->content_len;
    cJSON *root = nullptr;
    cJSON *respj = cJSON_CreateObject();
    bool ok = false;
    uint8_t err = ERR_OK;
    uint8_t detail = 0;
    const char *cmd = "";

    if (len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        cJSON_AddStringToObject(respj, "error", "empty body");
    } else if ((size_t)len >= sizeof(body)) {
        httpd_resp_set_status(req, "413 Payload Too Large");
        cJSON_AddStringToObject(respj, "error", "request body too large");
    } else {
        int received = 0;
        while (received < len) {
            int rx = httpd_req_recv(req, body + received, len - received);
            if (rx <= 0) {
                httpd_resp_set_status(req, "408 Request Timeout");
                cJSON_AddStringToObject(respj, "error", "request body receive failed");
                break;
            }
            received += rx;
        }
        if (received == len) {
            body[len] = '\0';
            root = cJSON_Parse(body);
        }
    }
    if (root == nullptr) {
        if (cJSON_GetObjectItem(respj, "error") == nullptr) {
            httpd_resp_set_status(req, "400 Bad Request");
            cJSON_AddStringToObject(respj, "error", "bad json");
        }
        esp_err_t e = send_json(req, respj);
        cJSON_Delete(respj);
        return e;
    }

    cmd = cJSON_GetStringValue(cJSON_GetObjectItem(root, "cmd"));
    cJSON *loopj = cJSON_GetObjectItem(root, "loop");
    double loop_value = (loopj != nullptr && cJSON_IsNumber(loopj))
                        ? cJSON_GetNumberValue(loopj) : -1.0;
    bool loop_cmd = cmd != nullptr &&
                    (strcmp(cmd, "read_params") == 0 || strcmp(cmd, "write_params") == 0 ||
                     strcmp(cmd, "pid_start") == 0 || strcmp(cmd, "pid_stop") == 0 ||
                     strcmp(cmd, "pid_pause") == 0 || strcmp(cmd, "pid_resume") == 0 ||
                     strcmp(cmd, "set_target") == 0 || strcmp(cmd, "set_output") == 0 ||
                     strcmp(cmd, "set_mode") == 0 ||
                     strcmp(cmd, "at_start") == 0 ||
                     strcmp(cmd, "at_stop") == 0 ||
                     strcmp(cmd, "at_pause") == 0 ||
                     strcmp(cmd, "at_resume") == 0 ||
                     strcmp(cmd, "at_apply") == 0);
    bool loop_valid = loop_value >= 0.0 && loop_value < (double)PID_LOOP_MAX &&
                      loop_value == (double)(int)loop_value;
    uint8_t loop = loop_valid ? (uint8_t)loop_value : 0u;

    if (cmd == nullptr) {
        err = ERR_COMMUNICATION_ERROR;
    } else if (loop_cmd && !loop_valid) {
        err = ERR_PARAM_OUT_OF_RANGE;
        detail = 0xFFu;
    } else if (strcmp(cmd, "device_info") == 0) {
        ok = device_status::refresh_device_info(&err);
    } else if (strcmp(cmd, "read_params") == 0) {
        pid_manager::Params p;
        ok = pid_manager::read_params(loop, &p, &err);
        if (ok) {
            cJSON_AddNumberToObject(respj, "kp", p.kp);
            cJSON_AddNumberToObject(respj, "ki", p.ki);
            cJSON_AddNumberToObject(respj, "kd", p.kd);
            cJSON_AddNumberToObject(respj, "target", p.target);
            cJSON_AddNumberToObject(respj, "out_min", p.out_min);
            cJSON_AddNumberToObject(respj, "out_max", p.out_max);
            cJSON_AddNumberToObject(respj, "integ_lim", p.integ_lim);
            cJSON_AddNumberToObject(respj, "sample_time", p.sample_time);
            cJSON_AddNumberToObject(respj, "deadband", p.deadband);
            cJSON_AddNumberToObject(respj, "filter", p.filter);
            cJSON_AddNumberToObject(respj, "dir", p.dir);
            cJSON_AddNumberToObject(respj, "mode", p.mode);
        }
    } else if (strcmp(cmd, "write_params") == 0) {
        /* Use a fresh STM32 read as the baseline. This keeps partial API writes
         * safe even before the periodic parameter cache has been populated. */
        pid_manager::Params p = {};
        ok = pid_manager::read_params(loop, &p, &err);
        if (ok) {
            auto num = [&](const char *k, float *dst) {
                cJSON *it = cJSON_GetObjectItem(root, k);
                if (it != nullptr && cJSON_IsNumber(it)) {
                    *dst = (float)cJSON_GetNumberValue(it);
                }
            };
            num("kp", &p.kp); num("ki", &p.ki); num("kd", &p.kd);
            num("target", &p.target);
            num("out_min", &p.out_min); num("out_max", &p.out_max);
            num("integ_lim", &p.integ_lim);
            num("sample_time", &p.sample_time);
            num("deadband", &p.deadband);
            num("filter", &p.filter);
            cJSON *it;
            if ((it = cJSON_GetObjectItem(root, "dir")) != nullptr && cJSON_IsNumber(it)) {
                p.dir = (uint8_t)cJSON_GetNumberValue(it);
            }
            if ((it = cJSON_GetObjectItem(root, "mode")) != nullptr && cJSON_IsNumber(it)) {
                p.mode = (uint8_t)cJSON_GetNumberValue(it);
            }
            uint8_t bad_idx = 0xFFu;
            ok = pid_manager::write_params(loop, &p, &err, &bad_idx);
            if (!ok && err == ERR_PARAM_OUT_OF_RANGE) {
                detail = bad_idx;
            }
        }
    } else if (strcmp(cmd, "save_flash") == 0) {
        ok = pid_manager::save_flash(&err, &detail);
    } else if (strcmp(cmd, "load_flash") == 0) {
        ok = pid_manager::load_flash(&err, &detail);
    } else if (strcmp(cmd, "default_params") == 0) {
        ok = pid_manager::default_params(&err);
    } else if (strcmp(cmd, "flash_erase") == 0) {
        ok = pid_manager::flash_erase(&err);
    } else if (strcmp(cmd, "flash_verify") == 0) {
        uint8_t vok = 0;
        uint32_t seq = 0;
        ok = pid_manager::flash_verify(&vok, &seq, &err);
        if (ok) {
            cJSON_AddNumberToObject(respj, "valid", vok);
            cJSON_AddNumberToObject(respj, "seq", (double)seq);
        }
    } else if (strcmp(cmd, "pid_start") == 0)  { ok = pid_manager::control(CMD_PID_START, loop, &err); }
    else if (strcmp(cmd, "pid_stop") == 0)     { ok = pid_manager::control(CMD_PID_STOP, loop, &err); }
    else if (strcmp(cmd, "pid_pause") == 0)    { ok = pid_manager::control(CMD_PID_PAUSE, loop, &err); }
    else if (strcmp(cmd, "pid_resume") == 0)   { ok = pid_manager::control(CMD_PID_RESUME, loop, &err); }
    else if (strcmp(cmd, "set_target") == 0) {
        cJSON *v = cJSON_GetObjectItem(root, "value");
        ok = (v != nullptr && cJSON_IsNumber(v)) &&
             pid_manager::set_target(loop, (float)cJSON_GetNumberValue(v), &err);
    } else if (strcmp(cmd, "set_output") == 0) {
        cJSON *v = cJSON_GetObjectItem(root, "value");
        ok = (v != nullptr && cJSON_IsNumber(v)) &&
             pid_manager::set_output(loop, (float)cJSON_GetNumberValue(v), &err);
    } else if (strcmp(cmd, "set_mode") == 0) {
        cJSON *v = cJSON_GetObjectItem(root, "value");
        ok = (v != nullptr && cJSON_IsNumber(v)) &&
             pid_manager::set_mode(loop, (uint8_t)cJSON_GetNumberValue(v), &err);
    }
    /* ---- ESP32-local autotune placeholder API; never forwards CMD_AUTOTUNE_* ---- */
    else if (strcmp(cmd, "at_start") == 0) {
        autotune_manager::Config cfg = {};
        cfg.loop = loop;
        cfg.mode = AUTOTUNE_MODE_RELAY;
        cfg.max_time_s = 60u;
        cfg.max_osc = 6u;
        cfg.fb_min = -1000.0f;
        cfg.fb_max = 1000.0f;
        auto num = [&](const char *key, float *dst) {
            cJSON *v = cJSON_GetObjectItem(root, key);
            if (v != nullptr && cJSON_IsNumber(v)) {
                *dst = (float)cJSON_GetNumberValue(v);
            }
        };
        num("target", &cfg.target);
        num("out_max", &cfg.out_max);
        num("out_min", &cfg.out_min);
        num("allowed_err", &cfg.allowed_err);
        num("fb_min", &cfg.fb_min);
        num("fb_max", &cfg.fb_max);
        cJSON *v = cJSON_GetObjectItem(root, "mode");
        if (v != nullptr && cJSON_IsNumber(v)) {
            cfg.mode = (uint8_t)cJSON_GetNumberValue(v);
        }
        v = cJSON_GetObjectItem(root, "max_time_s");
        if (v != nullptr && cJSON_IsNumber(v)) {
            cfg.max_time_s = (uint32_t)cJSON_GetNumberValue(v);
        }
        v = cJSON_GetObjectItem(root, "max_osc");
        if (v != nullptr && cJSON_IsNumber(v)) {
            cfg.max_osc = (uint8_t)cJSON_GetNumberValue(v);
        }
        ok = autotune_manager::start(&cfg, &err);
    } else if (strcmp(cmd, "at_stop") == 0) {
        ok = autotune_manager::stop(&err);
    } else if (strcmp(cmd, "at_pause") == 0) {
        ok = autotune_manager::pause(&err);
    } else if (strcmp(cmd, "at_resume") == 0) {
        ok = autotune_manager::resume(&err);
    } else if (strcmp(cmd, "at_status") == 0) {
        autotune_manager::Status st = {};
        ok = autotune_manager::get_status(&st);
        if (ok) {
            cJSON_AddNumberToObject(respj, "loop", st.loop);
            cJSON_AddNumberToObject(respj, "state", st.state);
            cJSON_AddNumberToObject(respj, "progress", st.progress);
            cJSON_AddNumberToObject(respj, "osc", st.osc);
            cJSON_AddNumberToObject(respj, "elapsed_s", (double)st.elapsed_s);
            cJSON_AddNumberToObject(respj, "eta_s", (double)st.eta_s);
            cJSON_AddNumberToObject(respj, "target", st.target);
            cJSON_AddNumberToObject(respj, "feedback", st.feedback);
            cJSON_AddNumberToObject(respj, "output", st.output);
        }
    } else if (strcmp(cmd, "at_result") == 0) {
        autotune_manager::Result result = {};
        ok = autotune_manager::get_result(&result);
        if (ok) {
            cJSON_AddNumberToObject(respj, "loop", result.loop);
            cJSON_AddNumberToObject(respj, "valid", result.valid);
            cJSON_AddNumberToObject(respj, "kp", result.kp);
            cJSON_AddNumberToObject(respj, "ki", result.ki);
            cJSON_AddNumberToObject(respj, "kd", result.kd);
        }
    } else if (strcmp(cmd, "at_apply") == 0) {
        ok = autotune_manager::apply(loop, &err);
    } else if (strcmp(cmd, "device_reset") == 0) {
        ok = pid_manager::device_reset(&err);
    } else if (strcmp(cmd, "log_clear") == 0) {
        device_status::log_clear();
        ok = true;
    } else if (strcmp(cmd, "log_pause") == 0) {
        cJSON *paused = cJSON_GetObjectItem(root, "paused");
        if (paused != nullptr && cJSON_IsBool(paused)) {
            device_status::log_set_paused(cJSON_IsTrue(paused));
            ok = true;
            cJSON_AddBoolToObject(respj, "paused", cJSON_IsTrue(paused));
        } else {
            err = ERR_PARAM_OUT_OF_RANGE;
        }
    } else if (strcmp(cmd, "wifi_set") == 0) {
        const char *ssid = cJSON_GetStringValue(cJSON_GetObjectItem(root, "ssid"));
        const char *pass = cJSON_GetStringValue(cJSON_GetObjectItem(root, "pass"));
        ok = (ssid != nullptr) && wifi_manager::set_credentials(ssid, pass);
        if (!ok) {
            err = ERR_PARAM_OUT_OF_RANGE;
        }
    } else {
        err = ERR_UNKNOWN_COMMAND;
    }

    cJSON_AddBoolToObject(respj, "ok", ok);
    cJSON_AddNumberToObject(respj, "err", err);
    if (detail != 0u) {
        cJSON_AddNumberToObject(respj, "detail", detail);
    }
    /* at_status is a 1.5 s browser heartbeat handled entirely on ESP32.
     * Do not let successful local polling bury the UART diagnostic log. */
    if (cmd == nullptr || strcmp(cmd, "at_status") != 0 || !ok) {
        device_status::log("WEB cmd=%s -> %s (err=0x%02X)", cmd ? cmd : "?",
                           ok ? "OK" : "FAIL", err);
    }

    esp_err_t e = send_json(req, respj);
    cJSON_Delete(root);
    cJSON_Delete(respj);
    return e;
}

/* ---------------- WebSocket ---------------- */

static esp_err_t h_ws(httpd_req_t *req)
{
    if (req->method == HTTP_GET) {
        /* 握手由 httpd 完成; 新客户端连接 */
        return ESP_OK;
    }

    httpd_ws_frame_t pkt = {};
    uint8_t buf[128] = {0};
    pkt.payload = buf;
    pkt.type = HTTPD_WS_TYPE_TEXT;
    if (httpd_ws_recv_frame(req, &pkt, sizeof(buf)) != ESP_OK) {
        return ESP_FAIL;
    }
    if (pkt.type == HTTPD_WS_TYPE_CLOSE) {
        return ESP_OK;
    }
    if (pkt.type == HTTPD_WS_TYPE_PING) {
        pkt.type = HTTPD_WS_TYPE_PONG;
        return httpd_ws_send_frame(req, &pkt);
    }
    /* 客户端消息 (ping) 忽略, 状态由推送线程发送 */
    return ESP_OK;
}

/* 向所有已连接 WS 客户端广播.
 * 用同步版 httpd_ws_send_data: async 版只排队不拷贝 payload,
 * 而调用方在广播返回后立即释放 payload, 会导致发送读到已释放内存. */
static void ws_broadcast(httpd_ws_frame_t *pkt)
{
    if (s_http == nullptr) {
        return;
    }
    size_t max_fds = 8;
    int fds[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    if (httpd_get_client_list(s_http, &max_fds, fds) != ESP_OK) {
        return;
    }
    for (size_t i = 0; i < max_fds; i++) {
        if (fds[i] < 0) {
            continue;
        }
        /* 跳过非 WebSocket 连接, 避免把 WS 帧写入普通 HTTP 响应 */
        if (httpd_ws_get_fd_info(s_http, fds[i]) != HTTPD_WS_CLIENT_WEBSOCKET) {
            continue;
        }
        (void)httpd_ws_send_data(s_http, fds[i], pkt);
    }
}

void broadcast_status()
{
    if (s_http == nullptr) {
        return;
    }
    Snapshot s;
    device_status::get_snapshot(&s);
    cJSON *root = cJSON_CreateObject();
    snap_json(s, root);
    const char *str = cJSON_PrintUnformatted(root);
    size_t len = strlen(str);

    httpd_ws_frame_t pkt = {};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.payload = (uint8_t *)str;
    pkt.len = len;
    pkt.final = true;
    ws_broadcast(&pkt);

    cJSON_free((void *)str);
    cJSON_Delete(root);
}

void broadcast_log(const char *line)
{
    if (s_http == nullptr || line == nullptr) {
        return;
    }
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "log", line);
    const char *str = cJSON_PrintUnformatted(root);
    httpd_ws_frame_t pkt = {};
    pkt.type = HTTPD_WS_TYPE_TEXT;
    pkt.payload = (uint8_t *)str;
    pkt.len = strlen(str);
    pkt.final = true;
    ws_broadcast(&pkt);
    cJSON_free((void *)str);
    cJSON_Delete(root);
}

/* ---------------- 注册与启动 ---------------- */

static const httpd_uri_t uris[] = {
    { "/",           HTTP_GET,  h_index,  nullptr },
    { "/api/status", HTTP_GET,  h_status, nullptr },
    { "/api/logs",   HTTP_GET,  h_logs,   nullptr },
    { "/api/cmd",    HTTP_POST, h_cmd,    nullptr },
};
static httpd_uri_t uri_ws = {};
static bool uri_ws_ready = []() {
    uri_ws.uri = "/ws";
    uri_ws.method = HTTP_GET;
    uri_ws.handler = h_ws;
    uri_ws.user_ctx = nullptr;
    uri_ws.is_websocket = true;            /* WS 握手 */
    uri_ws.handle_ws_control_frames = true;
    return true;
}();

bool init(int port)
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = port;
    cfg.max_uri_handlers = 16;
    cfg.lru_purge_enable = true;

    if (httpd_start(&s_http, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return false;
    }
    for (auto &u : uris) {
        if (httpd_register_uri_handler(s_http, &u) != ESP_OK) {
            ESP_LOGE(TAG, "register %s failed", u.uri);
            return false;
        }
    }
    if (httpd_register_uri_handler(s_http, &uri_ws) != ESP_OK) {
        ESP_LOGE(TAG, "register /ws failed");
        return false;
    }
    ESP_LOGI(TAG, "web server on port %d", port);
    return true;
}

} // namespace web_server
} // namespace dome
