/**
 * @file    device_status.cpp
 * @brief   设备状态快照 / RAM 日志环 / device_poll_task / status_publish_task
 */
#include "device_status.h"
#include "app_config.h"
#include "protocol_client.h"
#include "wifi_manager.h"
#include "web_server.h"
#include "storage/esp32_config.h"

#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_timer.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

namespace dome {
namespace device_status {

static Snapshot s_snap;
static SemaphoreHandle_t s_snap_lock = nullptr;

/* ---- 日志环 ---- */
#define LOG_CAP 128
static LogEntry s_logs[LOG_CAP];
static int s_log_head = 0, s_log_count = 0;
static bool s_log_paused = false;
static SemaphoreHandle_t s_log_lock = nullptr;

/* 快照更新事件 */
static EventGroupHandle_t s_events = nullptr;
#define EV_UPDATED  BIT0

void log(const char *fmt, ...)
{
    if (s_log_lock == nullptr ||
        xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(50)) != pdTRUE) {
        return;
    }
    if (s_log_paused) {
        xSemaphoreGive(s_log_lock);
        return;
    }
    LogEntry *e = &s_logs[s_log_head];
    e->tick_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    va_list ap;
    va_start(ap, fmt);
    (void)vsnprintf(e->text, sizeof(e->text), fmt, ap);
    va_end(ap);
    s_log_head = (s_log_head + 1) % LOG_CAP;
    if (s_log_count < LOG_CAP) {
        s_log_count++;
    }
    xSemaphoreGive(s_log_lock);
}

void log_hex(char dir, const char *hexstr)
{
    log("%cX: %s", dir, hexstr);
}

int log_count()
{
    if (s_log_lock == nullptr || xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0;
    }
    int n = s_log_count;
    xSemaphoreGive(s_log_lock);
    return n;
}

bool log_get(int idx, LogEntry *out)
{
    if (out == nullptr || s_log_lock == nullptr ||
        xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    if (idx < 0 || idx >= s_log_count) {
        xSemaphoreGive(s_log_lock);
        return false;
    }
    int start = (s_log_head + LOG_CAP - s_log_count) % LOG_CAP;
    *out = s_logs[(start + idx) % LOG_CAP];
    xSemaphoreGive(s_log_lock);
    return true;
}

void log_clear()
{
    if (s_log_lock == nullptr || xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    s_log_head = 0;
    s_log_count = 0;
    xSemaphoreGive(s_log_lock);
}

void log_set_paused(bool p)
{
    if (s_log_lock == nullptr || xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    s_log_paused = p;
    xSemaphoreGive(s_log_lock);
}

bool log_is_paused()
{
    if (s_log_lock == nullptr || xSemaphoreTake(s_log_lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    bool paused = s_log_paused;
    xSemaphoreGive(s_log_lock);
    return paused;
}

/* ---------------- 快照辅助 ---------------- */

static void upd_begin() { xSemaphoreTake(s_snap_lock, portMAX_DELAY); }
static void upd_end()
{
    xSemaphoreGive(s_snap_lock);
    xEventGroupSetBits(s_events, EV_UPDATED);
}

/* 泛型响应数据读取 */
namespace {
uint16_t rd_u8(const uint8_t *d, uint16_t pos, uint8_t *v) { *v = d[pos]; return pos + 1; }
uint16_t rd_u16(const uint8_t *d, uint16_t pos, uint16_t *v)
{
    *v = (uint16_t)(d[pos] | (d[pos + 1] << 8));
    return pos + 2;
}
uint16_t rd_u32(const uint8_t *d, uint16_t pos, uint32_t *v)
{
    *v = (uint32_t)d[pos] | ((uint32_t)d[pos + 1] << 8) |
         ((uint32_t)d[pos + 2] << 16) | ((uint32_t)d[pos + 3] << 24);
    return pos + 4;
}
uint16_t rd_f32(const uint8_t *d, uint16_t pos, float *v)
{
    uint32_t bits;
    pos = rd_u32(d, pos, &bits);
    memcpy(v, &bits, 4);
    return pos;
}
uint16_t rd_str(const uint8_t *d, uint16_t pos, char *out, uint16_t n, uint16_t field)
{
    uint16_t i;
    for (i = 0; i < field && i < (n - 1); i++) {
        out[i] = (char)d[pos + i];
    }
    out[i] = '\0';
    return pos + field;
}

/* 解析 DEVICE_INFO 数据区 */
void parse_info(const uint8_t *d, uint16_t len)
{
    uint16_t pos = 0;
    /* product(16)+fw(8)+proto(1)+hw(8)+mcu(12)+build(20) */
    if (d == nullptr || len < 65u) return;
    upd_begin();
    pos = rd_str(d, pos, s_snap.product, sizeof(s_snap.product), 16);
    pos = rd_str(d, pos, s_snap.fw, sizeof(s_snap.fw), 8);
    pos = rd_u8(d, pos, &s_snap.proto_ver);
    pos = rd_str(d, pos, s_snap.hw, sizeof(s_snap.hw), 8);
    pos = rd_str(d, pos, s_snap.mcu, sizeof(s_snap.mcu), 12);
    pos = rd_str(d, pos, s_snap.build, sizeof(s_snap.build), 20);
    upd_end();
}

/* 解析 DEVICE_STATUS 数据区 */
void parse_dev_status(const uint8_t *d, uint16_t len)
{
    uint16_t pos = 0;
    if (len < 17u) return;
    upd_begin();
    pos = rd_u8(d, pos, &s_snap.dev_state);
    pos = rd_u8(d, pos, &s_snap.comm_link);
    pos = rd_u8(d, pos, &s_snap.last_err);
    pos = rd_u32(d, pos, &s_snap.stm_uptime_ms);
    pos = rd_u8(d, pos, &s_snap.flash_valid);
    pos = rd_u16(d, pos, &s_snap.stm_crc_err);
    pos = rd_u16(d, pos, &s_snap.stm_frame_err);
    pos = rd_u16(d, pos, &s_snap.stm_timeout_err);
    pos = rd_u16(d, pos, &s_snap.stm_range_err);
    upd_end();
}

/* 解析 PID_RUNTIME_READ 数据区: loop(1)+7*f32(28)+state/mode/fault(3)+run_ms(4)=36 */
void parse_runtime(uint8_t loop_id, const uint8_t *d, uint16_t len)
{
    uint16_t pos = 0;
    LoopInfo *r;
    if (d == nullptr || len < 36u || loop_id >= PID_LOOP_MAX || d[0] != loop_id) return;
    upd_begin();
    r = &s_snap.rt[loop_id];
    pos = 1; /* 跳过 loop_id */
    pos = rd_f32(d, pos, &r->target);
    pos = rd_f32(d, pos, &r->feedback);
    pos = rd_f32(d, pos, &r->output);
    pos = rd_f32(d, pos, &r->err);
    pos = rd_f32(d, pos, &r->kp);
    pos = rd_f32(d, pos, &r->ki);
    pos = rd_f32(d, pos, &r->kd);
    pos = rd_u8(d, pos, &r->state);
    pos = rd_u8(d, pos, &r->mode);
    pos = rd_u8(d, pos, &r->fault);
    pos = rd_u32(d, pos, &r->run_ms);
    upd_end();
}

/* 解析 PID_PARAM_READ 数据区 */
void parse_params(uint8_t loop_id, const uint8_t *d, uint16_t len)
{
    /* loop_id(1) + params(44) + feedback(4) + output(4) + state(1) = 54 */
    uint16_t pos = 0;
    ParamInfo *p;
    if (d == nullptr || len < 54u || loop_id >= PID_LOOP_MAX || d[0] != loop_id) return;
    upd_begin();
    p = &s_snap.params[loop_id];
    pos = 1; /* 跳过 loop_id */
    pos = rd_f32(d, pos, &p->kp);
    pos = rd_f32(d, pos, &p->ki);
    pos = rd_f32(d, pos, &p->kd);
    pos = rd_f32(d, pos, &p->target);
    pos = rd_f32(d, pos, &p->out_min);
    pos = rd_f32(d, pos, &p->out_max);
    pos = rd_f32(d, pos, &p->integ_lim);
    pos = rd_f32(d, pos, &p->sample_time);
    pos = rd_f32(d, pos, &p->deadband);
    pos = rd_f32(d, pos, &p->filter);
    pos = rd_u8(d, pos, &p->dir);
    pos = rd_u8(d, pos, &p->mode);
    upd_end();
}


} // anonymous namespace

/* ---------------- 初始化与任务 ---------------- */

bool init()
{
    s_snap_lock = xSemaphoreCreateMutex();
    s_log_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (s_snap_lock == nullptr || s_log_lock == nullptr || s_events == nullptr) {
        return false;
    }
    memset(&s_snap, 0, sizeof(s_snap));
    s_snap.online = false;
    strcpy(s_snap.product, "-");
    return true;
}

void get_snapshot(Snapshot *out)
{
    if (out == nullptr) return;
    xSemaphoreTake(s_snap_lock, portMAX_DELAY);
    *out = s_snap;
    xSemaphoreGive(s_snap_lock);
}

void update_param_cache(uint8_t loop, const ParamInfo *params)
{
    if (params == nullptr || loop >= PID_LOOP_MAX || s_snap_lock == nullptr) {
        return;
    }
    upd_begin();
    s_snap.params[loop] = *params;
    upd_end();
}

bool refresh_device_info(uint8_t *err)
{
    ProtoResponse resp;
    if (!ProtocolClient::request(CMD_DEVICE_INFO_READ, nullptr, 0, &resp,
                                 CFG_REQ_TIMEOUT_MS)) {
        if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err != nullptr) *err = resp.err;
        return false;
    }
    if (resp.len < 65u) {
        if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    parse_info(resp.data, resp.len);
    if (err != nullptr) *err = ERR_OK;
    return true;
}



void poll_task(void *)
{
    static bool params_valid[PID_LOOP_MAX] = {false, false, false};
    bool link_online = false;
    uint32_t cycle = 0;
    TickType_t last_status_tick = 0;
    TickType_t last_info_tick = 0;
    TickType_t last_heartbeat_tick = 0;

    device_status::log("poll_task started");
    ESP_LOGI("poll_task", "started");

    /* Give STM32 time to finish its own reset and peripheral initialization. */
    vTaskDelay(pdMS_TO_TICKS(500));

    for (;;) {
        ProtoResponse resp;
        TickType_t now_tick = xTaskGetTickCount();
        const bool status_due = !link_online || last_status_tick == 0 ||
            (now_tick - last_status_tick) >= pdMS_TO_TICKS(CFG_POLL_STATUS_MS);

        /* Probe status first. If this request fails, do not immediately issue
         * three more 500 ms runtime requests. This keeps recovery responsive
         * and avoids flooding a recovering STM32/UART. */
        if (status_due) {
            last_status_tick = now_tick;
            if (ProtocolClient::request(CMD_DEVICE_STATUS_READ, nullptr, 0, &resp,
                                        CFG_REQ_TIMEOUT_MS,
                                        ProtocolClient::RequestKind::Background, false) &&
                resp.status == STATUS_OK) {
                parse_dev_status(resp.data, resp.len);
                link_online = true;
            } else {
                link_online = false;
            }
            vTaskDelay(1); /* let queued HTTP commands run between UART transactions */
        }

        if (link_online) {
            /* Device information changes rarely; refresh it every 10 seconds. */
            if (last_info_tick == 0 ||
                (now_tick - last_info_tick) >= pdMS_TO_TICKS(10000)) {
                last_info_tick = now_tick;
                if (ProtocolClient::request(CMD_DEVICE_INFO_READ, nullptr, 0, &resp,
                                            CFG_REQ_TIMEOUT_MS,
                                            ProtocolClient::RequestKind::Background, false) &&
                    resp.status == STATUS_OK) {
                    parse_info(resp.data, resp.len);
                }
                vTaskDelay(1);
            }

            /* Runtime polling. Stop the current cycle after the first UART
             * timeout; the next cycle returns to the lightweight status probe. */
            for (uint8_t l = 0; l < PID_LOOP_MAX; l++) {
                uint8_t lid = l;
                if (!ProtocolClient::request(CMD_PID_RUNTIME_READ, &lid, 1, &resp,
                                             CFG_REQ_TIMEOUT_MS,
                                             ProtocolClient::RequestKind::Background, false)) {
                    link_online = false;
                    break;
                }
                if (resp.status == STATUS_OK) {
                    parse_runtime(l, resp.data, resp.len);
                }
                vTaskDelay(1);

                /* Parameter cache: once at connection and every ~10 seconds. */
                if ((cycle % 50u) == 0u || !params_valid[l]) {
                    if (!ProtocolClient::request(CMD_PID_PARAM_READ, &lid, 1, &resp,
                                                 CFG_REQ_TIMEOUT_MS,
                                                 ProtocolClient::RequestKind::Background, false)) {
                        link_online = false;
                        break;
                    }
                    if (resp.status == STATUS_OK) {
                        parse_params(l, resp.data, resp.len);
                        params_valid[l] = true;
                    }
                    vTaskDelay(1);
                }
            }
        }

        upd_begin();
        s_snap.online = link_online;
        s_snap.esp_uptime_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        s_snap.tx_frames = ProtocolClient::tx_frames();
        s_snap.rx_frames = ProtocolClient::rx_frames();
        s_snap.timeouts = ProtocolClient::timeouts();
        s_snap.crc_errs = ProtocolClient::crc_errors();
        wifi_manager::get_ip(s_snap.ip, sizeof(s_snap.ip));
        s_snap.wifi_sta = wifi_manager::is_sta();
        wifi_manager::get_ssid(s_snap.ssid, sizeof(s_snap.ssid));
        upd_end();

        now_tick = xTaskGetTickCount();
        if (last_heartbeat_tick == 0 ||
            (now_tick - last_heartbeat_tick) >= pdMS_TO_TICKS(10000)) {
            last_heartbeat_tick = now_tick;
            device_status::log("poll_task alive cycle=%lu online=%u stack_free=%u",
                               (unsigned long)cycle,
                               link_online ? 1u : 0u,
                               (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        }

        cycle++;
        vTaskDelay(pdMS_TO_TICKS(link_online ? CFG_POLL_RUNTIME_MS : 250));
    }
}

void publish_task(void *)
{
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_events, EV_UPDATED, pdTRUE, pdFALSE,
                                               pdMS_TO_TICKS(500));
        if (bits & EV_UPDATED) {
            vTaskDelay(pdMS_TO_TICKS(CFG_WS_PUSH_MS)); /* 推送节流 */
            web_server::broadcast_status();
        }
    }
}

} // namespace device_status
} // namespace dome
