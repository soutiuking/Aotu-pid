/**
 * @file    frame_builder.h
 * @brief   命令帧构建 (ESP32 -> STM32 方向)
 */
#ifndef FRAME_BUILDER_H
#define FRAME_BUILDER_H

#include <stdint.h>
#include <stddef.h>
#include "protocol_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  构建完整请求帧
 * @param  cmd 命令字, seq 序号, payload 数据区 (可为 NULL/0)
 * @param  out 输出缓冲 (>= PROTO_MAX_FRAME)
 * @return 帧总长度, 0=参数错
 */
uint16_t frame_builder_build(uint8_t cmd, uint8_t seq,
                             const uint8_t *payload, uint16_t plen,
                             uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_BUILDER_H */
