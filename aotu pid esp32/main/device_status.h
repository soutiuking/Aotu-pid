/**
 * @file    device_status.h
 * @brief   设备状态快照 (STM32+ESP32 聚合) / RAM 日志环 / 轮询与推送任务
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "protocol_def.h"

namespace dome {

struct ParamInfo {
    float kp, ki, kd, target, out_min, out_max, integ_lim, sample_time, deadband, filter;
    uint8_t dir, mode;
};

struct LoopInfo {
    float target, feedback, output, err, kp, ki, kd;
    uint8_t state, mode, fault;
    uint32_t run_ms;
};

struct Snapshot {
    bool online;

    /* 设备信息 (DEVICE_INFO) */
    char product[17];
    char fw[9];
    char hw[9];
    char mcu[13];
    char build[21];
    uint8_t proto_ver;

    /* 设备状态 (DEVICE_STATUS) */
    uint8_t dev_state, comm_link, flash_valid, last_err;
    uint32_t stm_uptime_ms;
    uint16_t stm_crc_err, stm_frame_err, stm_timeout_err, stm_range_err;

    ParamInfo params[PID_LOOP_MAX];
    LoopInfo  rt[PID_LOOP_MAX];
    /* ESP32 侧 */
    uint32_t esp_uptime_ms;
    uint32_t tx_frames, rx_frames, timeouts, crc_errs;
    char ip[16];
    bool wifi_sta;
    char ssid[33];
};

struct LogEntry {
    uint32_t tick_ms;
    char text[236];
};

namespace device_status {

bool init();
void poll_task(void *);          /* device_poll_task: 轮询 STM32 并更新快照 */
void publish_task(void *);       /* status_publish_task: WS JSON 推送 */

/* 快照访问 (内部加锁拷贝) */
void get_snapshot(Snapshot *out);
void update_param_cache(uint8_t loop, const ParamInfo *params);
bool refresh_device_info(uint8_t *err);

/* 日志 */
void log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_hex(char dir, const char *hexstr);
int  log_count();
bool log_get(int idx, LogEntry *out);   /* idx 从 0 (最早) 开始 */
void log_clear();
void log_set_paused(bool p);
bool log_is_paused();

} // namespace device_status
} // namespace dome
