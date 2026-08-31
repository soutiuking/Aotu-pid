/**
 * @file    protocol_handler.c
 * @brief   鍗忚鍛戒护鍒嗗彂澶勭悊 (鍗忚瑙ｆ瀽涓庡懡浠ゅ鐞嗗垎绂? 涓诲惊鐜笂涓嬫枃鎵ц)
 */
#include "protocol_handler.h"
#include "protocol_def.h"
#include "protocol_parser.h"
#include "protocol_crc.h"
#include "protocol_codec.h"
#include "bsp_uart.h"
#include "control_task.h"
#include "pid_parameter.h"
#include "device_state.h"
#include "parameter_storage.h"
#include "bsp_display.h"
#include "app_config.h"
#include "stm32f1xx_hal.h"

#include <string.h>
#include <stdio.h>

/* ================= 鍐呴儴鐘舵€?================= */

static proto_parser_t g_parser;
static proto_queue_t  g_rx_queue;

/* PID writes are applied atomically from one complete parameter block. */
static uint8_t  resp_buf[PROTO_MAX_FRAME];
static uint16_t resp_len;
/* 寰呭彂鍝嶅簲闃熷垪锛歎ART DMA 蹇欐椂鏆傚瓨锛岄伩鍏嶇矘鍖?杩炵画鍛戒护涓㈠搷搴?*/
#define RESP_QUEUE_SLOTS 4u
static uint8_t  pend_buf[RESP_QUEUE_SLOTS][PROTO_MAX_FRAME];
static uint16_t pend_len[RESP_QUEUE_SLOTS];
static uint8_t  pend_head = 0u;
static uint8_t  pend_tail = 0u;
static uint8_t  pend_count = 0u;
/* 寤惰繜杞欢澶嶄綅 */
static uint8_t  reset_pending = 0u;
static uint32_t reset_at_ms = 0u;

/* ================= 鍝嶅簲缁勮 ================= */

/**
 * @brief  寮€濮嬬粍瑁呭搷搴斿抚: 濉厖甯уご/鍛戒护/搴忓彿/闀垮害鍗犱綅/STATUS/ERROR
 * @return 鏁版嵁鍖哄啓鍏ヨ捣濮嬪亸绉? */
static void resp_send_current(void);
static uint16_t resp_begin(uint8_t orig_cmd, uint8_t seq,
                           uint8_t status, uint8_t err, uint8_t detail)
{
    uint16_t pos = 0u;
    resp_buf[pos++] = PROTO_HEAD1;
    resp_buf[pos++] = PROTO_HEAD2;
    resp_buf[pos++] = PROTO_VERSION;
    resp_buf[pos++] = (uint8_t)(orig_cmd | PROTO_RESP_BIT);
    resp_buf[pos++] = seq;
    resp_buf[pos++] = 0u; /* 闀垮害鍗犱綅 */
    resp_buf[pos++] = 0u;
    resp_buf[pos++] = status;
    resp_buf[pos++] = err;
    resp_buf[pos++] = detail; /* 鍝嶅簲澶村浐瀹?3 瀛楄妭: status/err/detail */
    resp_len = pos;
    return pos;
}

/** 鍝嶅簲瀹氶暱閿欒鍝嶅簲 (鏃犳暟鎹? */
/* 缁熶竴鍝嶅簲: err==ERR_OK 鏃?STATUS=OK, 鍚﹀垯 STATUS=ERROR */
static void resp_status(uint8_t orig_cmd, uint8_t seq, uint8_t err, uint8_t detail)
{
    (void)resp_begin(orig_cmd, seq,
                     (err == ERR_OK) ? STATUS_OK : STATUS_ERROR, err, detail);
    resp_send_current();
}

static void resp_flush_pending(void)
{
    while (pend_count != 0u) {
        if (BSP_UART_Send(pend_buf[pend_tail], pend_len[pend_tail]) == 0u) {
            return;
        }
        pend_len[pend_tail] = 0u;
        pend_tail = (uint8_t)((pend_tail + 1u) % RESP_QUEUE_SLOTS);
        pend_count--;
    }
}

static void resp_send_current(void)
{
    uint16_t payload = (uint16_t)(resp_len - PROTO_HEAD_LEN);
    uint16_t crc;
    uint16_t pos = resp_len;

    resp_buf[5] = (uint8_t)(payload & 0xFFu);
    resp_buf[6] = (uint8_t)(payload >> 8);
    crc = protocol_crc16(&resp_buf[2], (size_t)(payload + 5u));
    pos = codec_put_u16(resp_buf, pos, crc);
    resp_buf[pos++] = PROTO_TAIL1;
    resp_buf[pos++] = PROTO_TAIL2;
    resp_len = pos;

    if (pend_count == 0u && BSP_UART_Send(resp_buf, resp_len) != 0u) {
        return;
    }
    if (pend_count >= RESP_QUEUE_SLOTS) {
        device_state_report_error(ERR_DEVICE_BUSY, 0xFFu);
        return;
    }
    (void)memcpy(pend_buf[pend_head], resp_buf, resp_len);
    pend_len[pend_head] = resp_len;
    pend_head = (uint8_t)((pend_head + 1u) % RESP_QUEUE_SLOTS);
    pend_count++;
}

/* ================= 鍙傛暟 <-> Flash 璁板綍鍚屾 ================= */

static void push_params_to_storage(void)
{
    uint8_t i;
    pid_params_t p;
    pid_parameter_record_t rec;

    for (i = 0; i < PID_LOOP_MAX; i++) {
        (void)memset(&rec, 0, sizeof(rec));
        if (pid_parameter_get(i, &p) != 0u) {
            rec.kp = p.kp;
            rec.ki = p.ki;
            rec.kd = p.kd;
            rec.target = p.target;
            rec.output_min = p.output_min;
            rec.output_max = p.output_max;
            rec.integral_limit = p.integral_limit;
            rec.sample_time = p.sample_time;
            rec.deadband = p.deadband;
            rec.filter_coefficient = p.filter_coefficient;
            rec.control_direction = p.control_direction;
            rec.pid_mode = p.pid_mode;
            parameter_storage_set(i, &rec);
        }
    }
}

static void pull_params_from_storage(void)
{
    uint8_t i;
    pid_parameter_record_t rec;
    pid_params_t p;

    for (i = 0; i < PID_LOOP_MAX; i++) {
        if (parameter_storage_get(i, &rec) != 0u) {
            (void)memset(&p, 0, sizeof(p));
            p.kp = rec.kp;
            p.ki = rec.ki;
            p.kd = rec.kd;
            p.target = rec.target;
            p.output_min = rec.output_min;
            p.output_max = rec.output_max;
            p.integral_limit = rec.integral_limit;
            p.sample_time = rec.sample_time;
            p.deadband = rec.deadband;
            p.filter_coefficient = rec.filter_coefficient;
            p.control_direction = rec.control_direction;
            p.pid_mode = rec.pid_mode;
            (void)pid_parameter_set(i, &p);
        }
    }
    for (i = 0; i < PID_LOOP_MAX; i++) {
        control_task_sync_params(i);
    }
}

/* ================= 鍚勫懡浠ゅ鐞?================= */

static void h_device_info(uint8_t seq)
{
    uint16_t pos = resp_begin(CMD_DEVICE_INFO_READ, seq, STATUS_OK, ERR_OK, 0u);
    pos = codec_put_str(resp_buf, pos, "DOME PID CTRL", 16u);
    pos = codec_put_str(resp_buf, pos, "1.0.0", 8u);          /* 鍥轰欢鐗堟湰 */
    pos = codec_put_u8(resp_buf, pos, PROTO_VERSION);
    pos = codec_put_str(resp_buf, pos, "F103C8-RevA", 8u);    /* 纭欢鐗堟湰 */
    pos = codec_put_str(resp_buf, pos, "STM32F103C8", 12u);
    pos = codec_put_str(resp_buf, pos, __DATE__, 20u);        /* 缂栬瘧鏃ユ湡 */
    {
        uint32_t uid_low = 0u;
        uint32_t uid_mid = 0u;
        /* STM32F1 UID 璧峰浜?0x1FFFF7E8锛屾寜涓や釜鐙珛鐨?32-bit 瀛楄鍙栥€?*/
        (void)memcpy(&uid_low, (const void *)0x1FFFF7E8u, sizeof(uid_low));
        (void)memcpy(&uid_mid, (const void *)0x1FFFF7ECu, sizeof(uid_mid));
        pos = codec_put_u32(resp_buf, pos, uid_low);
        pos = codec_put_u32(resp_buf, pos, uid_mid);
    }
    pos = codec_put_u32(resp_buf, pos,
                        FEAT_BIT_PID | FEAT_BIT_FLASH_STORAGE | FEAT_BIT_DISPLAY);
    resp_len = pos;
    resp_send_current();
}

static void h_device_status(uint8_t seq)
{
    device_state_ctx_t *dev = device_state_ctx();
    uint16_t pos = resp_begin(CMD_DEVICE_STATUS_READ, seq, STATUS_OK, ERR_OK, 0u);
    pos = codec_put_u8(resp_buf, pos, dev->state);
    pos = codec_put_u8(resp_buf, pos, dev->comm_link_up);
    pos = codec_put_u8(resp_buf, pos, parameter_storage_last_error());
    pos = codec_put_u32(resp_buf, pos, dev->uptime_ms);
    pos = codec_put_u8(resp_buf, pos, parameter_storage_is_valid());
    pos = codec_put_u16(resp_buf, pos, (uint16_t)(dev->crc_errors & 0xFFFFu));
    pos = codec_put_u16(resp_buf, pos, (uint16_t)(dev->frame_errors & 0xFFFFu));
    pos = codec_put_u16(resp_buf, pos, (uint16_t)(dev->timeout_errors & 0xFFFFu));
    pos = codec_put_u16(resp_buf, pos, (uint16_t)(dev->range_errors & 0xFFFFu));
    pos = codec_put_u8(resp_buf, pos, PID_LOOP_MAX);
    resp_len = pos;
    resp_send_current();
}

static void h_heartbeat(uint8_t seq)
{
    uint16_t pos = resp_begin(CMD_HEARTBEAT, seq, STATUS_OK, ERR_OK, 0u);
    pos = codec_put_u32(resp_buf, pos, device_state_ctx()->uptime_ms);
    pos = codec_put_u8(resp_buf, pos, device_state_ctx()->state);
    resp_len = pos;
    resp_send_current();
}

static void h_param_read(uint8_t seq, const uint8_t *payload, uint16_t len)
{
    uint8_t loop_id;
    pid_params_t p;
    pid_loop_t *L;
    uint16_t pos;

    if (len != 1u) {
        resp_status(CMD_PID_PARAM_READ, seq, ERR_INVALID_LENGTH, 0u);
        return;
    }
    loop_id = payload[0];
    if (loop_id >= PID_LOOP_MAX || pid_parameter_get(loop_id, &p) == 0u) {
        resp_status(CMD_PID_PARAM_READ, seq, ERR_PARAM_OUT_OF_RANGE, loop_id);
        return;
    }
    L = control_task_loop(loop_id);
    pos = resp_begin(CMD_PID_PARAM_READ, seq, STATUS_OK, ERR_OK, 0u);
    pos = codec_put_u8(resp_buf, pos, loop_id);
    pid_params_pack(&resp_buf[pos], &p);
    pos = (uint16_t)(pos + PID_PARAMS_WIRE_SIZE);
    pos = codec_put_f32(resp_buf, pos, (L != 0) ? L->feedback : 0.0f);
    pos = codec_put_f32(resp_buf, pos, (L != 0) ? L->output : 0.0f);
    pos = codec_put_u8(resp_buf, pos, (L != 0) ? L->state : (uint8_t)PID_STATE_IDLE);
    resp_len = pos;
    resp_send_current();
}

static void h_param_write(uint8_t seq, const uint8_t *payload, uint16_t len)
{
    uint8_t loop_id;
    uint8_t bad_idx = 0xFFu;
    uint8_t rc;
    pid_params_t params;

    /* Atomic wire format: loop_id(1) + fixed PID parameter block(44). */
    if (len != (uint16_t)(1u + PID_PARAMS_WIRE_SIZE)) {
        resp_status(CMD_PID_PARAM_WRITE, seq, ERR_INVALID_LENGTH, 0u);
        return;
    }

    loop_id = payload[0];
    if (loop_id >= PID_LOOP_MAX) {
        resp_status(CMD_PID_PARAM_WRITE, seq, ERR_PARAM_OUT_OF_RANGE, loop_id);
        return;
    }

    pid_params_unpack(&payload[1], &params);
    rc = pid_parameter_validate(&params, &bad_idx);
    if (rc != ERR_OK) {
        device_state_report_error(ERR_PARAM_OUT_OF_RANGE, loop_id);
        resp_status(CMD_PID_PARAM_WRITE, seq, rc, bad_idx);
        return;
    }

    rc = pid_parameter_set(loop_id, &params);
    if (rc != ERR_OK) {
        resp_status(CMD_PID_PARAM_WRITE, seq, rc, bad_idx);
        return;
    }

    control_task_sync_params(loop_id);
    resp_status(CMD_PID_PARAM_WRITE, seq, ERR_OK, 0u);
}

static void h_param_save(uint8_t orig_cmd, uint8_t seq)
{
    uint8_t rc;
    uint16_t pos;

    push_params_to_storage();
    device_state_ctx()->flash_busy = 1u;
    rc = parameter_storage_save();
    device_state_ctx()->flash_busy = 0u;

    if (rc == PS_OK || rc == PS_ERR_UNCHANGED) {
        pos = resp_begin(orig_cmd, seq, STATUS_OK, ERR_OK, 0u);
        pos = codec_put_u8(resp_buf, pos, rc);
        pos = codec_put_u8(resp_buf, pos, parameter_storage_is_valid());
        pos = codec_put_u32(resp_buf, pos, parameter_storage_sequence());
        resp_len = pos;
        resp_send_current();
    } else {
        device_state_report_error(ERR_FLASH_ERROR, 0xFFu);
        resp_status(orig_cmd, seq, ERR_FLASH_ERROR, rc);
    }
}

static void h_param_load(uint8_t orig_cmd, uint8_t seq)
{
    uint8_t rc;

    device_state_ctx()->flash_busy = 1u;
    rc = parameter_storage_load();
    device_state_ctx()->flash_busy = 0u;
    if (rc == PS_OK) {
        pid_parameter_sync_from_storage();
        pull_params_from_storage();
        resp_status(orig_cmd, seq, ERR_OK, 0u);
    } else {
        resp_status(orig_cmd, seq, ERR_FLASH_DATA_INVALID, rc);
    }
}

static void h_param_default(uint8_t seq)
{
    uint8_t i;

    pid_parameter_set_defaults();
    for (i = 0; i < PID_LOOP_MAX; i++) {
        control_task_sync_params(i);
    }
    resp_status(CMD_PID_PARAM_DEFAULT, seq, ERR_OK, 0u);
}

static void h_param_list(uint8_t seq)
{
    uint8_t i;
    pid_params_t p;
    uint16_t pos = resp_begin(CMD_PID_PARAM_LIST_READ, seq, STATUS_OK, ERR_OK, 0u);

    pos = codec_put_u8(resp_buf, pos, PID_LOOP_MAX);
    for (i = 0; i < PID_LOOP_MAX; i++) {
        (void)pid_parameter_get(i, &p);
        pos = codec_put_u8(resp_buf, pos, i);
        pid_params_pack(&resp_buf[pos], &p);
        pos = (uint16_t)(pos + PID_PARAMS_WIRE_SIZE);
    }
    resp_len = pos;
    resp_send_current();
}

static void h_param_range(uint8_t seq)
{
    const float *tab = pid_parameter_range_table();
    uint16_t pos = resp_begin(CMD_PID_PARAM_RANGE_READ, seq, STATUS_OK, ERR_OK, 0u);
    uint8_t i, j;

    pos = codec_put_u8(resp_buf, pos, (uint8_t)PIDR_COUNT);
    for (i = 0; i < PIDR_COUNT; i++) {
        for (j = 0; j < 3u; j++) {
            pos = codec_put_f32(resp_buf, pos, tab[i * 3u + j]);
        }
    }
    resp_len = pos;
    resp_send_current();
}

static void h_runtime_read(uint8_t seq, const uint8_t *payload, uint16_t len)
{
    uint8_t loop_id;
    pid_loop_t *L;
    uint16_t pos;

    if (len != 1u) {
        resp_status(CMD_PID_RUNTIME_READ, seq, ERR_INVALID_LENGTH, 0u);
        return;
    }
    loop_id = payload[0];
    L = control_task_loop(loop_id);
    if (L == 0) {
        resp_status(CMD_PID_RUNTIME_READ, seq, ERR_PARAM_OUT_OF_RANGE, loop_id);
        return;
    }
    pos = resp_begin(CMD_PID_RUNTIME_READ, seq, STATUS_OK, ERR_OK, 0u);
    pos = codec_put_u8(resp_buf, pos, loop_id);
    pos = codec_put_f32(resp_buf, pos, L->params.target);
    pos = codec_put_f32(resp_buf, pos, L->feedback);
    pos = codec_put_f32(resp_buf, pos, L->output);
    pos = codec_put_f32(resp_buf, pos, L->params.target - L->feedback);
    pos = codec_put_f32(resp_buf, pos, L->params.kp);
    pos = codec_put_f32(resp_buf, pos, L->params.ki);
    pos = codec_put_f32(resp_buf, pos, L->params.kd);
    pos = codec_put_u8(resp_buf, pos, L->state);
    pos = codec_put_u8(resp_buf, pos, L->params.pid_mode);
    pos = codec_put_u8(resp_buf, pos, L->fault);
    pos = codec_put_u32(resp_buf, pos, L->run_ms);
    resp_len = pos;
    resp_send_current();
}

static void h_flash_verify(uint8_t seq)
{
    uint16_t pos = resp_begin(CMD_FLASH_PARAM_VERIFY, seq, STATUS_OK, ERR_OK, 0u);
    uint8_t ok = parameter_storage_verify();

    pos = codec_put_u8(resp_buf, pos, ok);
    pos = codec_put_u32(resp_buf, pos, parameter_storage_sequence());
    resp_len = pos;
    resp_send_current();
}

static void h_flash_version(uint8_t seq)
{
    uint16_t pos = resp_begin(CMD_FLASH_PARAM_VERSION, seq, STATUS_OK, ERR_OK, 0u);

    pos = codec_put_u16(resp_buf, pos, PARAM_RECORD_VERSION);
    pos = codec_put_u32(resp_buf, pos, parameter_storage_sequence());
    pos = codec_put_u8(resp_buf, pos, parameter_storage_is_valid());
    resp_len = pos;
    resp_send_current();
}

/* ================= 涓诲垎鍙?================= */

static void dispatch(const uint8_t *frame, uint16_t len)
{
    uint8_t cmd;
    uint8_t seq;
    uint16_t plen;
    const uint8_t *payload;
    uint8_t rc;

    /* Do not touch header fields until the complete fixed header is present. */
    if (frame == 0 || len < PROTO_OVERHEAD) {
        return;
    }

    cmd = frame[3];
    seq = frame[4];
    plen = (uint16_t)(frame[5] | ((uint16_t)frame[6] << 8));
    payload = &frame[7];

    /* The parser already checked CRC/tail. Dispatch only an exact full frame. */
    if (plen > PROTO_MAX_PAYLOAD ||
        len != (uint16_t)(PROTO_OVERHEAD + plen)) {
        return;
    }

    /* 璇锋眰-鍝嶅簲鍖归厤涓庨€氫俊鍠傜嫍 */
    control_task_comm_alive(device_state_ctx()->uptime_ms);

    switch (cmd) {
    case CMD_DEVICE_INFO_READ:
        h_device_info(seq);
        break;
    case CMD_DEVICE_STATUS_READ:
        h_device_status(seq);
        break;
    case CMD_HEARTBEAT:
        h_heartbeat(seq);
        break;
    case CMD_RESET:
        (void)resp_begin(CMD_RESET, seq, STATUS_OK, ERR_OK, 0u);
        resp_send_current();
        reset_pending = 1u;
        reset_at_ms = device_state_ctx()->uptime_ms + 100u; /* 绛?DMA 鍙戝畬 */
        break;
    case CMD_TIME_SYNC:
        if (plen == 4u) {
            uint32_t esp_ms;
            uint16_t pos;
            (void)codec_get_u32(payload, 0u, &esp_ms);
            pos = resp_begin(CMD_TIME_SYNC, seq, STATUS_OK, ERR_OK, 0u);
            pos = codec_put_u32(resp_buf, pos, esp_ms);
            pos = codec_put_u32(resp_buf, pos, device_state_ctx()->uptime_ms);
            resp_len = pos;
            resp_send_current();
        } else {
            resp_status(CMD_TIME_SYNC, seq, ERR_INVALID_LENGTH, 0u);
        }
        break;

    /* ---------------- PID 鍙傛暟 ---------------- */
    case CMD_PID_PARAM_READ:
        h_param_read(seq, payload, plen);
        break;
    case CMD_PID_PARAM_WRITE:
        h_param_write(seq, payload, plen);
        break;
    case CMD_PID_PARAM_SAVE:
        if (plen != 0u) resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        else h_param_save(cmd, seq);
        break;
    case CMD_PID_PARAM_LOAD:
        if (plen != 0u) resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        else h_param_load(cmd, seq);
        break;
    case CMD_PID_PARAM_DEFAULT:
        if (plen != 0u) resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        else h_param_default(seq);
        break;
    case CMD_PID_PARAM_LIST_READ:
        if (plen != 0u) resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        else h_param_list(seq);
        break;
    case CMD_PID_PARAM_RANGE_READ:
        if (plen != 0u) resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        else h_param_range(seq);
        break;

    /* ---------------- PID 杩愯鎺у埗 ---------------- */
    case CMD_PID_START:
    case CMD_PID_STOP:
    case CMD_PID_PAUSE:
    case CMD_PID_RESUME:
        if (plen != 1u) {
            resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
            break;
        }
        switch (cmd) {
        case CMD_PID_START:   rc = control_task_start(payload[0]); break;
        case CMD_PID_STOP:    rc = control_task_stop(payload[0]); break;
        case CMD_PID_PAUSE:   rc = control_task_pause(payload[0]); break;
        default:              rc = control_task_resume(payload[0]); break;
        }
        if (rc == ERR_OK) {
            resp_status(cmd, seq, ERR_OK, 0u);
        } else {
            resp_status(cmd, seq, rc, 0u);
        }
        break;

    case CMD_PID_MODE_SET:
        if (plen != 2u) {
            resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
            break;
        }
        rc = control_task_set_mode(payload[0], payload[1]);
        if (rc == ERR_OK) {
            resp_status(cmd, seq, ERR_OK, 0u);
        } else {
            resp_status(cmd, seq, rc, 0u);
        }
        break;

    case CMD_PID_TARGET_SET:
    case CMD_PID_OUTPUT_SET:
        if (plen != 5u) {
            resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
            break;
        }
        {
            float v;
            (void)codec_get_f32(payload, 1u, &v);
            rc = (cmd == CMD_PID_TARGET_SET)
                 ? control_task_set_target(payload[0], v)
                 : control_task_set_output(payload[0], v);
        }
        if (rc == ERR_OK) {
            resp_status(cmd, seq, ERR_OK, 0u);
        } else {
            resp_status(cmd, seq, rc, 0u);
        }
        break;

    case CMD_PID_RUNTIME_READ:
        h_runtime_read(seq, payload, plen);
        break;

    /* Autotune is ESP32-owned; wire commands are compatibility only. */
    case CMD_AUTOTUNE_START:
    case CMD_AUTOTUNE_STOP:
    case CMD_AUTOTUNE_PAUSE:
    case CMD_AUTOTUNE_RESUME:
    case CMD_AUTOTUNE_STATUS:
    case CMD_AUTOTUNE_RESULT:
    case CMD_AUTOTUNE_APPLY:
        resp_status(cmd, seq, ERR_UNSUPPORTED_VERSION, 0u);
        break;

    /* ---------------- Flash 鍙傛暟瀛樺偍 ---------------- */
    case CMD_FLASH_PARAM_SAVE:
        if (plen != 0u) resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        else h_param_save(cmd, seq);
        break;
    case CMD_FLASH_PARAM_LOAD:
        if (plen != 0u) resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        else h_param_load(cmd, seq);
        break;
    case CMD_FLASH_PARAM_ERASE:
        if (plen != 0u) {
            resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
            break;
        }
        device_state_ctx()->flash_busy = 1u;
        rc = parameter_storage_erase();
        device_state_ctx()->flash_busy = 0u;
        if (rc == PS_OK) {
            resp_status(cmd, seq, ERR_OK, 0u);
        } else {
            device_state_report_error(ERR_FLASH_ERROR, 0xFFu);
            resp_status(cmd, seq, ERR_FLASH_ERROR, rc);
        }
        break;
    case CMD_FLASH_PARAM_VERIFY:
        if (plen != 0u) resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        else h_flash_verify(seq);
        break;
    case CMD_FLASH_PARAM_VERSION:
        if (plen != 0u) resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        else h_flash_version(seq);
        break;

    /* ---------------- 鎵╁睍 ---------------- */
    case CMD_DISPLAY_PAGE_SET:
        if (plen == 1u) {
            bsp_display_set_page(payload[0]);
            resp_status(cmd, seq, ERR_OK, 0u);
        } else {
            resp_status(cmd, seq, ERR_INVALID_LENGTH, 0u);
        }
        break;

    default:
        resp_status(cmd, seq, ERR_UNKNOWN_COMMAND, 0u);
        break;
    }
}

/* ================= 杞鍏ュ彛 ================= */

void protocol_handler_init(void)
{
    proto_parser_init(&g_parser);
    proto_queue_init(&g_rx_queue);
    (void)memset(pend_len, 0, sizeof(pend_len));
    pend_head = 0u;
    pend_tail = 0u;
    pend_count = 0u;
    reset_pending = 0u;
}

void protocol_handler_poll(uint32_t now_ms)
{
    uint8_t byte;
    uint8_t frame[PROTO_MAX_FRAME];
    uint16_t flen;
    uint8_t r;

    BSP_UART_Service(now_ms);

    /* 1. UART -> 鐘舵€佹満 */
    while (BSP_UART_ReadByte(&byte) != 0u) {
        r = proto_parser_feed(&g_parser, byte, now_ms);
        if (r == PROTO_FEED_COMPLETE) {
            if (proto_queue_push(&g_rx_queue, g_parser.buffer, g_parser.pos) == 0u) {
                device_state_report_error(ERR_DEVICE_BUSY, 0xFFu); /* 闃熷垪婊?*/
            }
        } else if (r == PROTO_FEED_ERROR) {
            uint8_t parser_err =
                (g_parser.last_err == PROTO_ERR_CRC) ? ERR_CRC_ERROR
                : (g_parser.last_err == PROTO_ERR_TIMEOUT) ? ERR_FRAME_TIMEOUT
                : ERR_COMMUNICATION_ERROR;
            device_state_report_error(parser_err, 0xFFu);

            /* A CRC-bad frame is structurally complete, so cmd/seq are still
             * available even though its payload must never be dispatched.
             * Return an explicit error when possible instead of forcing the
             * ESP32 to wait for a timeout and report only 0x0E. If cmd/seq
             * themselves were corrupted the ESP32 will ignore this response. */
            if (g_parser.last_err == PROTO_ERR_CRC &&
                (g_parser.cmd & PROTO_RESP_BIT) == 0u) {
                resp_status(g_parser.cmd, g_parser.seq, ERR_CRC_ERROR, 0u);
            }
        }
    }

    /* 2. 甯ч棿瓒呮椂妫€娴?*/
    if (proto_parser_tick(&g_parser, now_ms) != 0u) {
        device_state_report_error(ERR_FRAME_TIMEOUT, 0xFFu);
    }

    /* 3. 甯ч槦鍒?-> 鍒嗗彂 (绮樺寘/澶氬抚鍦ㄦ閫愬抚澶勭悊) */
    while (proto_queue_pop(&g_rx_queue, frame, &flen) != 0u) {
        dispatch(frame, flen);
    }

    /* 4. 寰呭彂鍝嶅簲閲嶈瘯锛團IFO锛?*/
    resp_flush_pending();

    /* 5. 寤惰繜澶嶄綅 */
    if (reset_pending != 0u && now_ms >= reset_at_ms) {
        NVIC_SystemReset();
    }
}

