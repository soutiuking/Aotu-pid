/**
 * @file    protocol_client.cpp
 * @brief   ESP32 -> STM32 鍗忚瀹㈡埛绔疄鐜? *
 * 浜嬪姟妯″瀷: request() 鎸佹湁浜嬪姟浜掓枼閲忓畬鎴?鍙戦€?>绛夊搷搴?>鍖归厤(鎸?cmd|0x80,seq)
 * 鍏ㄨ繃绋? 淇濊瘉鍚屼竴鏃跺埢鍙湁涓€涓姹傚湪閫? 鍏朵粬浠诲姟鍦ㄤ簰鏂ラ噺涓婃帓闃熴€? * Web/杞浠诲姟浠庝笉鐩存帴璁块棶 UART銆? */
#include "protocol_client.h"
#include "app_config.h"
#include "uart_manager.h"
#include "frame_builder.h"
#include "protocol_parser.h"
#include "protocol_crc.h"
#include "device_status.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include <string.h>
#include <stdio.h>

namespace dome {

namespace {

SemaphoreHandle_t s_txn_lock = nullptr;
SemaphoreHandle_t s_stat_lock = nullptr;
proto_parser_t s_parser;

/* A high-priority poll task used to release and immediately retake the mutex,
 * starving the lower-priority HTTP task. Interactive waiters are announced
 * before taking the transaction mutex; background requests yield until those
 * waiters have completed. */
portMUX_TYPE s_waiter_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_interactive_waiters = 0;

uint8_t  s_seq = 0;
uint32_t s_tx_frames = 0, s_rx_frames = 0, s_timeouts = 0, s_crc_errors = 0;
uint32_t s_last_ok_ms = 0;

char s_hex_tx[224];
char s_hex_rx[224];

void hex_to_str(const uint8_t *d, uint16_t n, char *out, size_t outsz)
{
    size_t pos = 0;
    if (n > 72u) {
        n = 72u; /* 鏃ュ織鎴柇 */
    }
    out[0] = '\0';
    for (uint16_t i = 0; i < n && pos + 4 < outsz; i++) {
        pos += (size_t)snprintf(out + pos, outsz - pos, "%s%02X",
                                (i == 0) ? "" : " ", d[i]);
    }
}

inline uint32_t now_ms()
{
    return (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
}

void interactive_waiter_add()
{
    portENTER_CRITICAL(&s_waiter_mux);
    s_interactive_waiters++;
    portEXIT_CRITICAL(&s_waiter_mux);
}

void interactive_waiter_remove()
{
    portENTER_CRITICAL(&s_waiter_mux);
    if (s_interactive_waiters > 0u) {
        s_interactive_waiters--;
    }
    portEXIT_CRITICAL(&s_waiter_mux);
}

uint32_t interactive_waiter_count()
{
    portENTER_CRITICAL(&s_waiter_mux);
    uint32_t count = s_interactive_waiters;
    portEXIT_CRITICAL(&s_waiter_mux);
    return count;
}

/* Match one validated full wire response frame. The parser buffer includes
 * header, payload, CRC16 and the 0D 0A tail. */
bool frame_is_match(const uint8_t *frame, uint16_t len,
                    uint8_t cmd, uint8_t seq, ProtoResponse *resp)
{
    if (len < PROTO_OVERHEAD) {
        return false;
    }
    uint8_t fcmd = frame[3];
    uint8_t fseq = frame[4];
    uint16_t plen = (uint16_t)(frame[5] | (frame[6] << 8));
    if ((uint8_t)(cmd | PROTO_RESP_BIT) != fcmd || fseq != seq) {
        return false;
    }
    if (plen != (uint16_t)(len - PROTO_OVERHEAD)) {
        return false;
    }

    resp->status = (plen >= 1u) ? frame[7] : STATUS_ERROR;
    resp->err    = (plen >= 2u) ? frame[8] : ERR_COMMUNICATION_ERROR;
    resp->detail = (plen >= 3u) ? frame[9] : 0u;
    const uint16_t hdr = 3u;
    uint16_t dlen = (plen > hdr) ? (uint16_t)(plen - hdr) : 0u;
    if (dlen > PROTO_MAX_PAYLOAD) {
        dlen = PROTO_MAX_PAYLOAD;
    }
    if (dlen > 0u) {
        memcpy(resp->data, &frame[7 + hdr], dlen);
    }
    resp->len = dlen;
    return true;
}

} // namespace

void ProtocolClient::on_crc_error() { s_crc_errors++; }

bool ProtocolClient::init()
{
    s_txn_lock = xSemaphoreCreateMutex();
    s_stat_lock = xSemaphoreCreateMutex();
    proto_parser_init(&s_parser);
    return (s_txn_lock != nullptr && s_stat_lock != nullptr);
}

bool ProtocolClient::request(uint8_t cmd, const uint8_t *payload, uint16_t plen,
                             ProtoResponse *resp, uint32_t timeout_ms,
                             RequestKind kind, bool log_success)
{
    if (s_txn_lock == nullptr || resp == nullptr || plen > PROTO_MAX_PAYLOAD) {
        return false;
    }

    const bool interactive = (kind == RequestKind::Interactive);
    if (interactive) {
        interactive_waiter_add();
    } else {
        /* Do not start another periodic transaction while a Web command is
         * queued. A one-tick sleep is intentional: taskYIELD() alone would not
         * let a lower-priority HTTP task run. */
        while (interactive_waiter_count() > 0u) {
            vTaskDelay(1);
        }
    }

    /* Lock wait is separate from the UART response timeout. Give an
     * interactive command enough time to wait behind one in-flight request. */
    uint32_t lock_wait_ms = timeout_ms + 500u;
    if (lock_wait_ms < 1000u) {
        lock_wait_ms = 1000u;
    }
    if (xSemaphoreTake(s_txn_lock, pdMS_TO_TICKS(lock_wait_ms)) != pdTRUE) {
        s_timeouts++;
        device_status::log("CMD 0x%02X not sent: protocol client busy (%lu ms)",
                           cmd, (unsigned long)lock_wait_ms);
        if (interactive) {
            interactive_waiter_remove();
        }
        return false;
    }

    bool ok = false;
    uint8_t txbuf[PROTO_MAX_FRAME];
    uint8_t seq = ++s_seq;
    uint32_t t0 = now_ms();

    /* Reset parser and discard stale bytes left by a timed-out transaction. */
    proto_parser_reset(&s_parser);
    uint8_t rb[128];
    for (;;) {
        int nd = uart_manager::read(rb, sizeof(rb));
        if (nd <= 0) {
            break;
        }
    }

    uint16_t n = frame_builder_build(cmd, seq, payload, plen, txbuf);
    if (n == 0u) {
        xSemaphoreGive(s_txn_lock);
        if (interactive) {
            interactive_waiter_remove();
        }
        return false;
    }

    s_tx_frames++;
    hex_to_str(txbuf, n, s_hex_tx, sizeof(s_hex_tx));
    if (log_success) {
        device_status::log_hex('T', s_hex_tx);
    }
    if (!uart_manager::write(txbuf, n)) {
        s_timeouts++;
        if (!log_success) {
            device_status::log_hex('T', s_hex_tx);
        }
        device_status::log("CMD 0x%02X UART write failed", cmd);
        xSemaphoreGive(s_txn_lock);
        if (interactive) {
            interactive_waiter_remove();
        }
        return false;
    }

    for (;;) {
        int nl = uart_manager::read(rb, sizeof(rb));
        for (int i = 0; i < nl; i++) {
            uint8_t r = proto_parser_feed(&s_parser, rb[i], now_ms());
            if (r == PROTO_FEED_COMPLETE) {
                s_rx_frames++;
                hex_to_str(s_parser.buffer, s_parser.pos, s_hex_rx, sizeof(s_hex_rx));
                const bool matched = frame_is_match(s_parser.buffer, s_parser.pos,
                                                    cmd, seq, resp);
                if (log_success || !matched) {
                    device_status::log_hex('R', s_hex_rx);
                }
                if (matched) {
                    s_last_ok_ms = now_ms();
                    ok = true;
                } else {
                    device_status::log("RX unmatched frame for CMD 0x%02X seq=%u",
                                       cmd, (unsigned)seq);
                }
            } else if (r == PROTO_FEED_ERROR) {
                if (s_parser.last_err == PROTO_ERR_CRC) {
                    s_crc_errors++;
                    device_status::log("RX CRC error for CMD 0x%02X", cmd);
                } else if (s_parser.last_err == PROTO_ERR_TIMEOUT) {
                    device_status::log("RX frame timeout for CMD 0x%02X", cmd);
                } else {
                    device_status::log("RX frame error 0x%02X for CMD 0x%02X",
                                       s_parser.last_err, cmd);
                }
            }
        }

        if (ok || (now_ms() - t0) > timeout_ms) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }

    if (!ok) {
        s_timeouts++;
        if (!log_success) {
            device_status::log_hex('T', s_hex_tx);
        }
        device_status::log("CMD 0x%02X timeout: no valid response (seq=%u, %lu ms)",
                           cmd, (unsigned)seq, (unsigned long)timeout_ms);
    }
    xSemaphoreGive(s_txn_lock);
    if (interactive) {
        interactive_waiter_remove();
    }
    return ok;
}

bool ProtocolClient::is_online()
{
    uint32_t last = s_last_ok_ms;
    if (last == 0u) {
        return false;
    }
    return (now_ms() - last) < (CFG_POLL_STATUS_MS * 3u);
}

uint32_t ProtocolClient::last_ok_ms() { return s_last_ok_ms; }
uint32_t ProtocolClient::tx_frames()  { return s_tx_frames; }
uint32_t ProtocolClient::rx_frames()  { return s_rx_frames; }
uint32_t ProtocolClient::timeouts()   { return s_timeouts; }
uint32_t ProtocolClient::crc_errors() { return s_crc_errors; }
const char *ProtocolClient::last_hex_tx() { return s_hex_tx; }
const char *ProtocolClient::last_hex_rx() { return s_hex_rx; }

} // namespace dome

