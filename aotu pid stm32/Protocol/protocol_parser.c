/**
 * @file    protocol_parser.c
 * @brief   Buffered, self-resynchronizing protocol stream parser.
 */
#include "protocol_parser.h"
#include "protocol_crc.h"

#include <string.h>

#define PROTO_BYTE_GAP_MS 50u

static void parser_clear_stream(proto_parser_t *p)
{
    p->state = PS_WAIT_HEADER_1;
    p->index = 0u;
    p->pos = 0u;
    p->length = 0u;
    p->crc_calc = 0u;
    p->crc_rx = 0u;
    p->frame_ready = 0u;
}

static void parser_drop(proto_parser_t *p, uint16_t count)
{
    if (count >= p->pos) {
        p->pos = 0u;
        return;
    }
    (void)memmove(p->buffer, &p->buffer[count], (size_t)(p->pos - count));
    p->pos = (uint16_t)(p->pos - count);
}

/* Keep the first AA 55 candidate. If none exists, retain a trailing AA so a
 * header split across two UART reads is not lost. */
static void parser_seek_header(proto_parser_t *p)
{
    uint16_t i;

    for (i = 0u; (uint16_t)(i + 1u) < p->pos; i++) {
        if (p->buffer[i] == PROTO_HEAD1 && p->buffer[i + 1u] == PROTO_HEAD2) {
            if (i != 0u) {
                parser_drop(p, i);
            }
            p->state = PS_READ_VERSION;
            return;
        }
    }

    if (p->pos != 0u && p->buffer[p->pos - 1u] == PROTO_HEAD1) {
        p->buffer[0] = PROTO_HEAD1;
        p->pos = 1u;
        p->state = PS_WAIT_HEADER_2;
    } else {
        p->pos = 0u;
        p->state = PS_WAIT_HEADER_1;
    }
}

static uint8_t parser_reject(proto_parser_t *p, uint8_t err, uint16_t discard)
{
    p->last_err = err;
    parser_drop(p, discard);
    p->frame_ready = 0u;
    p->index = 0u;
    p->length = 0u;
    p->crc_calc = 0u;
    p->crc_rx = 0u;
    parser_seek_header(p);
    return PROTO_FEED_ERROR;
}

void proto_parser_init(proto_parser_t *p)
{
    if (p == 0) {
        return;
    }
    (void)memset(p, 0, sizeof(*p));
    p->state = PS_WAIT_HEADER_1;
}

void proto_parser_reset(proto_parser_t *p)
{
    if (p == 0) {
        return;
    }
    parser_clear_stream(p);
    p->last_err = PROTO_ERR_NONE;
}

uint8_t proto_parser_feed(proto_parser_t *p, uint8_t byte, uint32_t now_ms)
{
    uint16_t total;
    uint16_t crc_pos;

    if (p == 0) {
        return PROTO_FEED_ERROR;
    }

    /* The caller consumes a completed frame before feeding the next byte. */
    if (p->frame_ready != 0u) {
        parser_clear_stream(p);
    }

    p->last_byte_ms = now_ms;
    if (p->pos >= PROTO_MAX_FRAME) {
        /* This can only be a malformed stream: a legal maximum frame is
         * completed exactly when the buffer reaches PROTO_MAX_FRAME. */
        p->last_err = PROTO_ERR_OVERFLOW;
        parser_clear_stream(p);
        if (byte == PROTO_HEAD1) {
            p->buffer[p->pos++] = byte;
            p->state = PS_WAIT_HEADER_2;
        }
        return PROTO_FEED_ERROR;
    }
    p->buffer[p->pos++] = byte;

    parser_seek_header(p);
    if (p->pos < 2u) {
        return PROTO_FEED_NONE;
    }
    if (p->pos < PROTO_HEAD_LEN) {
        return PROTO_FEED_NONE;
    }

    p->version = p->buffer[2];
    p->cmd = p->buffer[3];
    p->seq = p->buffer[4];
    if (p->version != PROTO_VERSION) {
        return parser_reject(p, PROTO_ERR_VERSION, 1u);
    }

    p->length = (uint16_t)p->buffer[5] |
                (uint16_t)((uint16_t)p->buffer[6] << 8);
    if (p->length > PROTO_MAX_PAYLOAD) {
        return parser_reject(p, PROTO_ERR_LENGTH, 1u);
    }

    total = (uint16_t)(PROTO_OVERHEAD + p->length);
    p->index = total;
    p->state = PS_READ_PAYLOAD;
    if (p->pos < total) {
        return PROTO_FEED_NONE;
    }

    if (p->buffer[total - 2u] != PROTO_TAIL1 ||
        p->buffer[total - 1u] != PROTO_TAIL2) {
        /* Length may itself be corrupt, so move by one and search again rather
         * than discarding a possible next-frame header inside the candidate. */
        return parser_reject(p, PROTO_ERR_TAIL, 1u);
    }

    crc_pos = (uint16_t)(PROTO_HEAD_LEN + p->length);
    p->crc_rx = (uint16_t)p->buffer[crc_pos] |
                (uint16_t)((uint16_t)p->buffer[crc_pos + 1u] << 8);
    p->crc_calc = protocol_crc16(&p->buffer[2], (size_t)(p->length + 5u));
    if (p->crc_calc != p->crc_rx) {
        /* Tail and length are structurally valid. Discard this whole frame so
         * AA 55 bytes inside its payload cannot masquerade as a new frame. */
        return parser_reject(p, PROTO_ERR_CRC, total);
    }

    p->last_err = PROTO_ERR_NONE;
    p->state = PS_WAIT_HEADER_1;
    p->frame_ready = 1u;
    p->pos = total;
    return PROTO_FEED_COMPLETE;
}

uint8_t proto_parser_tick(proto_parser_t *p, uint32_t now_ms)
{
    if (p == 0 || p->frame_ready != 0u || p->pos == 0u) {
        return 0u;
    }
    if ((uint32_t)(now_ms - p->last_byte_ms) > PROTO_BYTE_GAP_MS) {
        p->last_err = PROTO_ERR_TIMEOUT;
        parser_clear_stream(p);
        return 1u;
    }
    return 0u;
}

void proto_queue_init(proto_queue_t *q)
{
    if (q != 0) {
        (void)memset(q, 0, sizeof(*q));
    }
}

uint8_t proto_queue_push(proto_queue_t *q, const uint8_t *data, uint16_t len)
{
    proto_frame_slot_t *s;
    if (q == 0 || data == 0 || len == 0u || len > PROTO_MAX_FRAME ||
        q->count >= PROTO_QUEUE_SLOTS) {
        return 0u;
    }
    s = &q->slot[q->head];
    (void)memcpy(s->data, data, len);
    s->len = len;
    q->head = (uint8_t)((q->head + 1u) % PROTO_QUEUE_SLOTS);
    q->count++;
    return 1u;
}

uint8_t proto_queue_pop(proto_queue_t *q, uint8_t *out, uint16_t *out_len)
{
    proto_frame_slot_t *s;
    if (q == 0 || out == 0 || out_len == 0 || q->count == 0u) {
        return 0u;
    }
    s = &q->slot[q->tail];
    (void)memcpy(out, s->data, s->len);
    *out_len = s->len;
    q->tail = (uint8_t)((q->tail + 1u) % PROTO_QUEUE_SLOTS);
    q->count--;
    return 1u;
}

uint8_t proto_queue_count(const proto_queue_t *q)
{
    return (q != 0) ? q->count : 0u;
}

