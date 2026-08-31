/**
 * @file    protocol_codec.h
 * @brief   显式小端序列化/反序列化辅助函数
 *
 * 协议规定所有多字节数据小端传输。为保证两端行为一致且不受结构体对齐/
 * 大小端影响, 一律通过本模块显式打包/解包, 不直接 memcpy 结构体。
 */
#ifndef PROTOCOL_CODEC_H
#define PROTOCOL_CODEC_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 打包 (写入缓冲区, 返回写入字节数) ---- */
static inline uint16_t codec_put_u8(uint8_t *buf, uint16_t pos, uint8_t v)
{
    buf[pos] = v;
    return (uint16_t)(pos + 1u);
}

static inline uint16_t codec_put_u16(uint8_t *buf, uint16_t pos, uint16_t v)
{
    buf[pos] = (uint8_t)(v & 0xFFu);
    buf[pos + 1u] = (uint8_t)(v >> 8);
    return (uint16_t)(pos + 2u);
}

static inline uint16_t codec_put_u32(uint8_t *buf, uint16_t pos, uint32_t v)
{
    buf[pos] = (uint8_t)(v & 0xFFu);
    buf[pos + 1u] = (uint8_t)((v >> 8) & 0xFFu);
    buf[pos + 2u] = (uint8_t)((v >> 16) & 0xFFu);
    buf[pos + 3u] = (uint8_t)((v >> 24) & 0xFFu);
    return (uint16_t)(pos + 4u);
}

/* IEEE754 float32: 先转 uint32 位模式再小端写入 */
static inline uint16_t codec_put_f32(uint8_t *buf, uint16_t pos, float v)
{
    uint32_t bits;
    (void)memcpy(&bits, &v, 4u);
    return codec_put_u32(buf, pos, bits);
}

/* 定长字符串: 不足补 0, 超长截断, 返回 pos + field_len */
static inline uint16_t codec_put_str(uint8_t *buf, uint16_t pos,
                                     const char *s, uint16_t field_len)
{
    uint16_t i;
    for (i = 0; i < field_len; i++) {
        buf[pos + i] = (uint8_t)((s != 0) ? (uint8_t)s[i] : 0u);
        if (s != 0 && s[i] == '\0') {
            s = 0; /* 之后全部填 0 */
        }
    }
    return (uint16_t)(pos + field_len);
}

/* ---- 解包 (读取缓冲区, 返回新位置) ---- */
static inline uint16_t codec_get_u8(const uint8_t *buf, uint16_t pos, uint8_t *v)
{
    *v = buf[pos];
    return (uint16_t)(pos + 1u);
}

static inline uint16_t codec_get_u16(const uint8_t *buf, uint16_t pos, uint16_t *v)
{
    *v = (uint16_t)(buf[pos] | ((uint16_t)buf[pos + 1u] << 8));
    return (uint16_t)(pos + 2u);
}

static inline uint16_t codec_get_u32(const uint8_t *buf, uint16_t pos, uint32_t *v)
{
    *v = (uint32_t)buf[pos]
       | ((uint32_t)buf[pos + 1u] << 8)
       | ((uint32_t)buf[pos + 2u] << 16)
       | ((uint32_t)buf[pos + 3u] << 24);
    return (uint16_t)(pos + 4u);
}

static inline uint16_t codec_get_f32(const uint8_t *buf, uint16_t pos, float *v)
{
    uint32_t bits;
    pos = codec_get_u32(buf, pos, &bits);
    (void)memcpy(v, &bits, 4u);
    return pos;
}

/* 位模式是否为合法 float (非 NaN / 非无穷) */
static inline uint8_t codec_f32_is_finite(float v)
{
    uint32_t bits;
    (void)memcpy(&bits, &v, 4u);
    return (uint8_t)(((bits & 0x7F800000u) != 0x7F800000u) ? 1u : 0u);
}

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_CODEC_H */
