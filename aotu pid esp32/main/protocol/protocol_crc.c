/**
 * @file    protocol_crc.c
 * @brief   CRC16 (Modbus) 与 CRC32 实现 —— 双端统一算法
 */
#include "protocol_crc.h"

uint16_t protocol_crc16_update(uint16_t crc, const uint8_t *data, size_t len)
{
    size_t i;
    uint8_t bit;

    if (data == 0) {
        return crc;
    }

    for (i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (bit = 0; bit < 8u; bit++) {
            if ((crc & 0x0001u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

uint16_t protocol_crc16(const uint8_t *data, size_t len)
{
    return protocol_crc16_update(0xFFFFu, data, len);
}

uint32_t protocol_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    size_t i;
    uint8_t bit;

    if (data == 0) {
        return 0u;
    }

    for (i = 0; i < len; i++) {
        crc ^= (uint32_t)data[i];
        for (bit = 0; bit < 8u; bit++) {
            if ((crc & 1u) != 0u) {
                crc = (crc >> 1) ^ 0xEDB88320u;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}
