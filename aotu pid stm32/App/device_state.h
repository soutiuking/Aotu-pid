/**
 * @file    device_state.h
 * @brief   设备全局状态、错误记录与通信统计
 */
#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

#include <stdint.h>
#include "protocol_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 可报告的错误种类 (与协议错误码一致, 另含内部错误) */
#define DEV_ERR_MAX_ENTRIES  4u

typedef struct
{
    uint16_t code;       /* ERR_* 或 PROTO_ERR_* */
    uint8_t  loop_id;    /* 相关回路, 0xFF=无关 */
    uint32_t tick;       /* 发生时刻 (ms) */
} dev_error_entry_t;

typedef struct
{
    /* 设备状态 */
    uint8_t state;             /* device_state_t */
    uint32_t uptime_ms;

    /* 通信统计 */
    uint32_t comm_last_rx_ms;  /* 最近一次有效帧时间, 0xFFFFFFFF=从未收到 */
    uint8_t  comm_link_up;     /* 至少收到过一帧 */
    uint32_t crc_errors;
    uint32_t frame_errors;     /* 帧格式/长度/帧尾错误 */
    uint32_t timeout_errors;   /* 接收超时 */
    uint32_t range_errors;     /* 参数范围错误 */
    uint32_t flash_errors;

    /* 运行标志 */
    uint8_t  flash_busy;       /* Flash 擦写期间置 1, 控制任务暂停输出更新 */
    uint8_t  safe_latched;     /* 已进入安全状态 (通信超时) */
} device_state_ctx_t;

void device_state_init(void);
void device_state_set(uint8_t state);
uint8_t device_state_get(void);
device_state_ctx_t *device_state_ctx(void);

/** 记录一次错误 (写入错误环形记录并计数) */
void device_state_report_error(uint16_t code, uint8_t loop_id);

/** Read recent errors; idx=0 is the newest entry. */
uint8_t device_state_get_error(uint8_t idx, dev_error_entry_t *e);

/** 由状态计算派生: 任一回路 RUNNING/RUN 且未处于安全态 */
void device_state_refresh(void);

#ifdef __cplusplus
}
#endif

#endif /* DEVICE_STATE_H */
