/**
 * @file    uart_manager.h
 * @brief   UART 收发封装 (统一发送入口, 收取环形数据)
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <stdbool.h>
#include "driver/uart.h"

namespace dome {

namespace uart_manager {
bool init(int tx_pin, int rx_pin, int baud);
bool write(const uint8_t *data, size_t len);
int  read(uint8_t *buf, size_t len);   /* 非阻塞, 返回读取字节数 */
} // namespace uart_manager

} // namespace dome
