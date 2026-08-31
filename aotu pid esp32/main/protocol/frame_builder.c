/**
 * @file    frame_builder.c
 * @brief   命令帧构建
 */
#include "frame_builder.h"
#include "protocol_crc.h"

#include <string.h>

uint16_t frame_builder_build(uint8_t cmd, uint8_t seq,
                             const uint8_t *payload, uint16_t plen,
                             uint8_t *out)
{
    uint16_t pos = 0u;
    uint16_t crc;

    if (out == 0 || plen > PROTO_MAX_PAYLOAD ||
        (plen > 0u && payload == NULL)) {
        return 0u;
    }

    out[pos++] = PROTO_HEAD1;
    out[pos++] = PROTO_HEAD2;
    out[pos++] = PROTO_VERSION;
    out[pos++] = cmd;
    out[pos++] = seq;
    out[pos++] = (uint8_t)(plen & 0xFFu);
    out[pos++] = (uint8_t)(plen >> 8);
    if (plen > 0u && payload != NULL) {
        (void)memcpy(&out[pos], payload, plen);
        pos = (uint16_t)(pos + plen);
    }
    crc = protocol_crc16(&out[2], (size_t)(pos - 2u));
    out[pos++] = (uint8_t)(crc & 0xFFu);
    out[pos++] = (uint8_t)(crc >> 8);
    out[pos++] = PROTO_TAIL1;
    out[pos++] = PROTO_TAIL2;
    return pos;
}
