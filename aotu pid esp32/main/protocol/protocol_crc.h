/**
 * @file    protocol_crc.h
 * @brief   CRC16 (Modbus) 与 CRC32 (标准) —— 双端统一算法
 */
#ifndef PROTOCOL_CRC_H
#define PROTOCOL_CRC_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 计算 CRC16-Modbus (多项式0x8005反射0xA001, 初值0xFFFF, 无终值异或)
 * @param data 输入缓冲区
 * @param len  字节数
 * @return CRC16 值, 低字节先发送
 */
uint16_t protocol_crc16(const uint8_t *data, size_t len);

/**
 * @brief 增量 CRC16-Modbus: 在已有 crc 基础上继续计算后续字节
 * @note  首次调用时传入 protocol_crc16() 或对首字节单独计算的结果
 */
uint16_t protocol_crc16_update(uint16_t crc, const uint8_t *data, size_t len);

/**
 * @brief 计算 CRC32 (反射多项式0xEDB88320, 初值0xFFFFFFFF, 终值异或0xFFFFFFFF)
 *        与 zlib/gzip 的 crc32 结果一致, 用于 Flash 参数记录校验
 */
uint32_t protocol_crc32(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_CRC_H */
