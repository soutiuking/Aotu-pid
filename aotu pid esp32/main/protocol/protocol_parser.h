/**
 * @file    protocol_parser.h
 * @brief   Buffered protocol stream parser shared by STM32 and ESP32.
 *
 * The parser keeps a byte stream buffer, searches for AA 55, validates the
 * declared length, frame tail and CRC, and returns a complete frame including
 * CRC and tail.  Invalid frames are discarded without losing a possible AA at
 * the end of the buffered stream, so the next frame can be found immediately.
 */
#ifndef PROTOCOL_PARSER_H
#define PROTOCOL_PARSER_H

#include <stdint.h>
#include "protocol_def.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    PS_WAIT_HEADER_1 = 0,
    PS_WAIT_HEADER_2,
    PS_READ_VERSION,
    PS_READ_COMMAND,
    PS_READ_SEQUENCE,
    PS_READ_LENGTH_LOW,
    PS_READ_LENGTH_HIGH,
    PS_READ_PAYLOAD,
    PS_READ_CRC_LOW,
    PS_READ_CRC_HIGH,
    PS_WAIT_TAIL_1,
    PS_WAIT_TAIL_2
} proto_state_t;

#define PROTO_FEED_NONE      0u
#define PROTO_FEED_COMPLETE  1u
#define PROTO_FEED_ERROR     2u

typedef struct
{
    proto_state_t state;
    uint8_t  version;
    uint8_t  cmd;
    uint8_t  seq;
    uint16_t length;
    uint16_t index;
    uint16_t crc_calc;
    uint16_t crc_rx;
    uint32_t last_byte_ms;
    uint8_t  last_err;
    uint8_t  frame_ready;
    uint8_t  buffer[PROTO_MAX_FRAME];
    uint16_t pos;
} proto_parser_t;

#define PROTO_ERR_NONE           ERR_OK
#define PROTO_ERR_LENGTH         0xF1u
#define PROTO_ERR_CRC            0xF2u
#define PROTO_ERR_TAIL           0xF3u
#define PROTO_ERR_VERSION        0xF4u
#define PROTO_ERR_TIMEOUT        0xF5u
#define PROTO_ERR_OVERFLOW       0xF6u

typedef struct
{
    uint8_t  data[PROTO_MAX_FRAME];
    uint16_t len;
} proto_frame_slot_t;

#define PROTO_QUEUE_SLOTS 4u

typedef struct
{
    proto_frame_slot_t slot[PROTO_QUEUE_SLOTS];
    uint8_t head;
    uint8_t tail;
    uint8_t count;
} proto_queue_t;

void proto_parser_init(proto_parser_t *p);
void proto_parser_reset(proto_parser_t *p);

/** Feed one byte. On COMPLETE, buffer[0..pos-1] is the full wire frame. */
uint8_t proto_parser_feed(proto_parser_t *p, uint8_t byte, uint32_t now_ms);

/** Reset an incomplete frame after an excessive inter-byte gap. */
uint8_t proto_parser_tick(proto_parser_t *p, uint32_t now_ms);

void     proto_queue_init(proto_queue_t *q);
uint8_t  proto_queue_push(proto_queue_t *q, const uint8_t *data, uint16_t len);
uint8_t  proto_queue_pop(proto_queue_t *q, uint8_t *out, uint16_t *out_len);
uint8_t  proto_queue_count(const proto_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_PARSER_H */
