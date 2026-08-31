/**
 * @file    protocol_client.h
 * @brief   ESP32 -> STM32 协议客户端: 请求队列/序号匹配/超时/统计
 *
 * 设计: Web 与轮询任务只调用 request(), 内部把请求投递到协议任务队列,
 * 由协议任务统一收发 UART 并按 (cmd|0x80, seq) 匹配响应 —— Web 层不直接
 * 访问串口, UART 发送天然单任务串行化。
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <stdbool.h>
#include "protocol_def.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace dome {

struct ProtoResponse {
    uint8_t  status;      /* STATUS_OK / STATUS_ERROR */
    uint8_t  err;         /* ERR_* */
    uint8_t  detail;      /* 部分错误的附加字节 */
    uint16_t len;         /* 数据区长度 (不含 status/err/detail, 调用方视角: 从 detail 之后算) */
    uint8_t  data[PROTO_MAX_PAYLOAD];
};

class ProtocolClient {
public:
    enum class RequestKind : uint8_t {
        Interactive = 0,  /* Web/user command: must take priority over polling */
        Background  = 1   /* Periodic status/runtime polling */
    };

    static bool init();
    /**
     * @brief 同步请求
     * @param timeout_ms 响应超时
     * @return true=收到响应 (status 字段区分命令执行结果), false=超时/未在线
     */
    static bool request(uint8_t cmd, const uint8_t *payload, uint16_t plen,
                        ProtoResponse *resp, uint32_t timeout_ms,
                        RequestKind kind = RequestKind::Interactive,
                        bool log_success = true);

    /* 在线判定: 最近 CFG_POLL_STATUS_MS*3 内有过成功响应 */
    static bool is_online();
    static uint32_t last_ok_ms();
    static uint32_t tx_frames();
    static uint32_t rx_frames();
    static uint32_t timeouts();
    static uint32_t crc_errors();

    static const char *last_hex_tx();  /* 最近一次发送帧 hex 文本 */
    static const char *last_hex_rx();

    /* 帧计数自增 (UART 层直接上报坏帧) */
    static void on_crc_error();
    static void on_rx_frame();

private:
    static void task(void *arg);
};

} // namespace dome
