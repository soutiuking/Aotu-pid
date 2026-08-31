/**
 * @file    protocol_handler.h
 * @brief   协议命令分发处理: 从解析队列取帧 -> 执行命令 -> 组响应发送
 */
#ifndef PROTOCOL_HANDLER_H
#define PROTOCOL_HANDLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 (解析器/队列/待发缓冲) */
void protocol_handler_init(void);

/**
 * @brief  主循环轮询: UART 取字节 -> 状态机 -> 帧队列 -> 分发; 并处理待发帧/延迟复位
 * @param  now_ms 当前毫秒时基
 */
void protocol_handler_poll(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_HANDLER_H */
