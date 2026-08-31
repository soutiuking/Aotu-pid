/**
 * @file    pid_manager.cpp
 * @brief   PID 鍙傛暟/鎺у埗/Flash 鍛戒护灏佽
 */
#include "pid_manager.h"
#include "app_config.h"
#include "device_status.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

namespace dome {
namespace pid_manager {

namespace {

/* 鏄惧紡灏忕鎵撳寘杈呭姪 */
void pk_f32(uint8_t *b, uint16_t &pos, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, 4);
    b[pos++] = (uint8_t)(bits & 0xFF);
    b[pos++] = (uint8_t)((bits >> 8) & 0xFF);
    b[pos++] = (uint8_t)((bits >> 16) & 0xFF);
    b[pos++] = (uint8_t)((bits >> 24) & 0xFF);
}

void pk_u8(uint8_t *b, uint16_t &pos, uint8_t v) { b[pos++] = v; }

void up_f32(const uint8_t *b, uint16_t &pos, float *v)
{
    uint32_t bits = (uint32_t)b[pos] | ((uint32_t)b[pos + 1] << 8) |
                    ((uint32_t)b[pos + 2] << 16) | ((uint32_t)b[pos + 3] << 24);
    pos += 4;
    memcpy(v, &bits, 4);
}

void up_u8(const uint8_t *b, uint16_t &pos, uint8_t *v) { *v = b[pos++]; }
void up_u32(const uint8_t *b, uint16_t &pos, uint32_t *v)
{
    *v = (uint32_t)b[pos] | ((uint32_t)b[pos + 1] << 8) |
         ((uint32_t)b[pos + 2] << 16) | ((uint32_t)b[pos + 3] << 24);
    pos += 4;
}

void update_cached_params(uint8_t loop, const Params *p)
{
    if (p == nullptr || loop >= PID_LOOP_MAX) {
        return;
    }
    ParamInfo cached = {};
    cached.kp = p->kp;
    cached.ki = p->ki;
    cached.kd = p->kd;
    cached.target = p->target;
    cached.out_min = p->out_min;
    cached.out_max = p->out_max;
    cached.integ_lim = p->integ_lim;
    cached.sample_time = p->sample_time;
    cached.deadband = p->deadband;
    cached.filter = p->filter;
    cached.dir = p->dir;
    cached.mode = p->mode;
    device_status::update_param_cache(loop, &cached);
}


uint16_t pack_param_write(uint8_t loop, const Params *in, uint8_t *payload)
{
    uint16_t pos = 0u;
    pk_u8(payload, pos, loop);
    pk_f32(payload, pos, in->kp);
    pk_f32(payload, pos, in->ki);
    pk_f32(payload, pos, in->kd);
    pk_f32(payload, pos, in->target);
    pk_f32(payload, pos, in->out_min);
    pk_f32(payload, pos, in->out_max);
    pk_f32(payload, pos, in->integ_lim);
    pk_f32(payload, pos, in->sample_time);
    pk_f32(payload, pos, in->deadband);
    pk_f32(payload, pos, in->filter);
    pk_u8(payload, pos, in->dir);
    pk_u8(payload, pos, in->mode);
    pk_u8(payload, pos, 0u);
    pk_u8(payload, pos, 0u);
    return pos;
}

bool request_with_retry(uint8_t cmd, const uint8_t *payload, uint16_t len,
                        ProtoResponse *resp, uint32_t timeout_ms,
                        uint8_t attempts)
{
    for (uint8_t attempt = 0u; attempt < attempts; attempt++) {
        *resp = {};
        if (ProtocolClient::request(cmd, payload, len, resp, timeout_ms)) {
            return true;
        }
        if ((uint8_t)(attempt + 1u) < attempts) {
            device_status::log("CMD 0x%02X retry %u/%u after timeout",
                               (unsigned)cmd, (unsigned)(attempt + 2u),
                               (unsigned)attempts);
            vTaskDelay(pdMS_TO_TICKS(CFG_PARAM_RETRY_DELAY_MS));
        }
    }
    return false;
}

bool refresh_all_param_caches(uint8_t *err)
{
    for (uint8_t loop = 0u; loop < PID_LOOP_MAX; loop++) {
        Params current = {};
        uint8_t read_err = ERR_OK;
        if (!read_params(loop, &current, &read_err)) {
            if (err != nullptr) *err = read_err;
            return false;
        }
    }
    if (err != nullptr) *err = ERR_OK;
    return true;
}

bool same_f32(float a, float b)
{
    uint32_t ua;
    uint32_t ub;
    memcpy(&ua, &a, sizeof(ua));
    memcpy(&ub, &b, sizeof(ub));
    return ua == ub;
}

bool params_equal(const Params *a, const Params *b)
{
    return same_f32(a->kp, b->kp) &&
           same_f32(a->ki, b->ki) &&
           same_f32(a->kd, b->kd) &&
           same_f32(a->target, b->target) &&
           same_f32(a->out_min, b->out_min) &&
           same_f32(a->out_max, b->out_max) &&
           same_f32(a->integ_lim, b->integ_lim) &&
           same_f32(a->sample_time, b->sample_time) &&
           same_f32(a->deadband, b->deadband) &&
           same_f32(a->filter, b->filter) &&
           a->dir == b->dir && a->mode == b->mode;
}

} // namespace

bool read_params(uint8_t loop, Params *out, uint8_t *err)
{
    ProtoResponse resp;
    if (out == nullptr) return false;
    if (!ProtocolClient::request(CMD_PID_PARAM_READ, &loop, 1, &resp, CFG_REQ_TIMEOUT_MS)) {
        if (err) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err) *err = resp.err;
        return false;
    }
    if (resp.len < 54u || resp.data[0] != loop) {
        if (err) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    uint16_t pos = 1; /* loop_id */
    up_f32(resp.data, pos, &out->kp);
    up_f32(resp.data, pos, &out->ki);
    up_f32(resp.data, pos, &out->kd);
    up_f32(resp.data, pos, &out->target);
    up_f32(resp.data, pos, &out->out_min);
    up_f32(resp.data, pos, &out->out_max);
    up_f32(resp.data, pos, &out->integ_lim);
    up_f32(resp.data, pos, &out->sample_time);
    up_f32(resp.data, pos, &out->deadband);
    up_f32(resp.data, pos, &out->filter);
    up_u8(resp.data, pos, &out->dir);
    up_u8(resp.data, pos, &out->mode);
    pos = (uint16_t)(pos + 2u); /* reserved[2] in the 44-byte parameter block */
    up_f32(resp.data, pos, &out->feedback);
    up_f32(resp.data, pos, &out->output);
    up_u8(resp.data, pos, &out->state);
    update_cached_params(loop, out);
    if (err) *err = ERR_OK;
    return true;
}

bool write_params(uint8_t loop, const Params *in, uint8_t *err, uint8_t *bad_idx)
{
    uint8_t payload[1u + PID_PARAMS_WIRE_SIZE];
    uint16_t payload_len;

    if (err != nullptr) *err = ERR_OK;
    if (bad_idx != nullptr) *bad_idx = 0xFFu;
    if (in == nullptr || loop >= PID_LOOP_MAX) {
        if (err != nullptr) *err = ERR_PARAM_OUT_OF_RANGE;
        return false;
    }

    payload_len = pack_param_write(loop, in, payload);
    if (payload_len != sizeof(payload)) {
        if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }

    /* One atomic 45-byte request replaces the fragile BEGIN/field/COMMIT
     * transaction. The STM32 validates the complete block before applying it,
     * so retries are idempotent and polling cannot interleave with a write. */
    for (uint8_t attempt = 0u; attempt < CFG_PARAM_WRITE_RETRIES; attempt++) {
        ProtoResponse resp = {};
        const bool got_response = ProtocolClient::request(
            CMD_PID_PARAM_WRITE, payload, payload_len, &resp, CFG_REQ_TIMEOUT_MS);

        if (got_response) {
            if (resp.status != STATUS_OK) {
                if (err != nullptr) *err = resp.err;
                if (bad_idx != nullptr) *bad_idx = resp.detail;
                return false;
            }

            Params verified = {};
            uint8_t read_err = ERR_OK;
            if (read_params(loop, &verified, &read_err) && params_equal(in, &verified)) {
                if (err != nullptr) *err = ERR_OK;
                if (bad_idx != nullptr) *bad_idx = 0xFFu;
                device_status::log("PID atomic write verified loop=%u",
                                   (unsigned)loop);
                return true;
            }
            if (err != nullptr) *err = read_err;
            device_status::log("PID atomic write ACK received but readback failed loop=%u",
                               (unsigned)loop);
            return false;
        }

        /* The write may have been applied even when its ACK was lost. Read back
         * before retrying to avoid reporting a successful write as 0x0E. */
        Params recovered = {};
        uint8_t read_err = ERR_OK;
        if (read_params(loop, &recovered, &read_err) && params_equal(in, &recovered)) {
            if (err != nullptr) *err = ERR_OK;
            if (bad_idx != nullptr) *bad_idx = 0xFFu;
            device_status::log("PID atomic write ACK lost, readback matched loop=%u",
                               (unsigned)loop);
            return true;
        }

        if ((uint8_t)(attempt + 1u) < CFG_PARAM_WRITE_RETRIES) {
            device_status::log("PID atomic write retry %u/%u loop=%u",
                               (unsigned)(attempt + 2u),
                               (unsigned)CFG_PARAM_WRITE_RETRIES,
                               (unsigned)loop);
            vTaskDelay(pdMS_TO_TICKS(CFG_PARAM_RETRY_DELAY_MS));
        }
    }

    if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
    return false;
}

bool save_flash(uint8_t *err, uint8_t *detail)
{
    ProtoResponse resp = {};
    uint8_t verify_ok = 0u;
    uint32_t verify_seq = 0u;
    uint32_t ack_seq = 0u;
    uint8_t verify_err = ERR_OK;
    bool have_ack_sequence = false;

    if (err != nullptr) *err = ERR_OK;
    if (detail != nullptr) *detail = 0u;

    if (request_with_retry(CMD_FLASH_PARAM_SAVE, nullptr, 0u, &resp,
                           CFG_FLASH_REQ_TIMEOUT_MS, 2u)) {
        if (resp.status != STATUS_OK) {
            if (err != nullptr) *err = resp.err;
            if (detail != nullptr) *detail = resp.detail;
            return false;
        }
        /* SAVE returns storage_rc, valid and sequence. Reject a malformed ACK. */
        if (resp.len < 6u || resp.data[1] == 0u) {
            if (err != nullptr) *err = ERR_FLASH_DATA_INVALID;
            return false;
        }
        uint16_t pos = 2u;
        up_u32(resp.data, pos, &ack_seq);
        have_ack_sequence = true;
        device_status::log("FLASH SAVE storage_rc=0x%02X seq=%lu",
                           (unsigned)resp.data[0], (unsigned long)ack_seq);
    } else {
        /* Both ACKs may have been lost. A valid post-operation slot is the
         * strongest non-destructive confirmation available over this protocol. */
        if (!flash_verify(&verify_ok, &verify_seq, &verify_err) || verify_ok == 0u) {
            if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
            return false;
        }
        device_status::log("FLASH SAVE ACK lost, verify valid seq=%lu",
                           (unsigned long)verify_seq);
    }

    if (!flash_verify(&verify_ok, &verify_seq, &verify_err) || verify_ok == 0u) {
        if (err != nullptr) *err = (verify_err == ERR_OK) ? ERR_FLASH_DATA_INVALID : verify_err;
        return false;
    }
    if (have_ack_sequence && verify_seq != ack_seq) {
        if (err != nullptr) *err = ERR_FLASH_DATA_INVALID;
        device_status::log("FLASH SAVE sequence mismatch ACK=%lu VERIFY=%lu",
                           (unsigned long)ack_seq, (unsigned long)verify_seq);
        return false;
    }
    if (err != nullptr) *err = ERR_OK;
    return true;
}

bool load_flash(uint8_t *err, uint8_t *detail)
{
    ProtoResponse resp = {};

    if (err != nullptr) *err = ERR_OK;
    if (detail != nullptr) *detail = 0u;
    if (!request_with_retry(CMD_FLASH_PARAM_LOAD, nullptr, 0u, &resp,
                            CFG_FLASH_REQ_TIMEOUT_MS, 2u)) {
        if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err != nullptr) *err = resp.err;
        if (detail != nullptr) *detail = resp.detail;
        return false;
    }

    /* Loading changes STM32 RAM; immediately read all loops back so the Web UI
     * and the controller show the same values. */
    if (!refresh_all_param_caches(err)) {
        return false;
    }
    if (err != nullptr) *err = ERR_OK;
    return true;
}

bool default_params(uint8_t *err)
{
    ProtoResponse resp;
    if (!ProtocolClient::request(CMD_PID_PARAM_DEFAULT, nullptr, 0, &resp,
                                 CFG_REQ_TIMEOUT_MS)) {
        if (err) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err) *err = resp.err;
        return false;
    }
    if (err) *err = ERR_OK;
    return true;
}

bool read_ranges(Ranges *out, uint8_t *err)
{
    ProtoResponse resp;
    if (out == nullptr) return false;
    if (!ProtocolClient::request(CMD_PID_PARAM_RANGE_READ, nullptr, 0, &resp,
                                 CFG_REQ_TIMEOUT_MS)) {
        if (err) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK || resp.len < (uint16_t)(1u + PIDR_COUNT * 12u)) {
        if (err) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    uint16_t pos = 1; /* count */
    for (uint8_t i = 0; i < PIDR_COUNT; i++) {
        up_f32(resp.data, pos, &out->minv[i]);
        up_f32(resp.data, pos, &out->maxv[i]);
        up_f32(resp.data, pos, &out->step[i]);
    }
    if (err) *err = ERR_OK;
    return true;
}

bool read_runtime(uint8_t loop, Runtime *out, uint8_t *err)
{
    ProtoResponse resp;
    uint16_t pos = 1u;

    if (out == nullptr || loop >= PID_LOOP_MAX) {
        if (err != nullptr) *err = ERR_PARAM_OUT_OF_RANGE;
        return false;
    }
    if (!ProtocolClient::request(CMD_PID_RUNTIME_READ, &loop, 1u, &resp,
                                 CFG_REQ_TIMEOUT_MS)) {
        if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    /* loop(1) + 7*f32(28) + state/mode/fault(3) + run_ms(4) = 36 bytes */
    if (resp.status != STATUS_OK) {
        if (err != nullptr) *err = resp.err;
        return false;
    }
    if (resp.len < 36u || resp.data[0] != loop) {
        if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }

    up_f32(resp.data, pos, &out->target);
    up_f32(resp.data, pos, &out->feedback);
    up_f32(resp.data, pos, &out->output);
    up_f32(resp.data, pos, &out->err);
    up_f32(resp.data, pos, &out->kp);
    up_f32(resp.data, pos, &out->ki);
    up_f32(resp.data, pos, &out->kd);
    up_u8(resp.data, pos, &out->state);
    up_u8(resp.data, pos, &out->mode);
    up_u8(resp.data, pos, &out->fault);
    up_u32(resp.data, pos, &out->run_ms);

    if (err != nullptr) *err = ERR_OK;
    return true;
}

bool control(uint8_t cmd, uint8_t loop, uint8_t *err)
{
    ProtoResponse resp;
    if (loop >= PID_LOOP_MAX) {
        if (err != nullptr) *err = ERR_PARAM_OUT_OF_RANGE;
        return false;
    }
    if (cmd != CMD_PID_START && cmd != CMD_PID_STOP &&
        cmd != CMD_PID_PAUSE && cmd != CMD_PID_RESUME) {
        /* Never forward reserved CMD_AUTOTUNE_* through this generic API. */
        if (err != nullptr) *err = ERR_UNKNOWN_COMMAND;
        return false;
    }
    if (!ProtocolClient::request(cmd, &loop, 1, &resp, CFG_REQ_TIMEOUT_MS)) {
        if (err) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err) *err = resp.err;
        return false;
    }
    if (err) *err = ERR_OK;
    return true;
}

bool set_target(uint8_t loop, float v, uint8_t *err)
{
    ProtoResponse resp;
    uint8_t buf[5];
    uint16_t pos = 0;
    pk_u8(buf, pos, loop);
    pk_f32(buf, pos, v);
    if (!ProtocolClient::request(CMD_PID_TARGET_SET, buf, sizeof(buf), &resp,
                                 CFG_REQ_TIMEOUT_MS)) {
        if (err) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err) *err = resp.err;
        return false;
    }
    if (err) *err = ERR_OK;
    return true;
}

bool set_output(uint8_t loop, float v, uint8_t *err)
{
    ProtoResponse resp;
    uint8_t buf[5];
    uint16_t pos = 0;
    pk_u8(buf, pos, loop);
    pk_f32(buf, pos, v);
    if (!ProtocolClient::request(CMD_PID_OUTPUT_SET, buf, sizeof(buf), &resp,
                                 CFG_REQ_TIMEOUT_MS)) {
        if (err) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err) *err = resp.err;
        return false;
    }
    if (err) *err = ERR_OK;
    return true;
}

bool set_mode(uint8_t loop, uint8_t mode, uint8_t *err)
{
    ProtoResponse resp;
    uint8_t buf[2] = { loop, mode };
    if (!ProtocolClient::request(CMD_PID_MODE_SET, buf, sizeof(buf), &resp,
                                 CFG_REQ_TIMEOUT_MS)) {
        if (err) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err) *err = resp.err;
        return false;
    }
    if (err) *err = ERR_OK;
    return true;
}

bool flash_erase(uint8_t *err)
{
    ProtoResponse resp = {};
    if (request_with_retry(CMD_FLASH_PARAM_ERASE, nullptr, 0u, &resp,
                           CFG_FLASH_REQ_TIMEOUT_MS, 2u)) {
        if (resp.status != STATUS_OK) {
            if (err != nullptr) *err = resp.err;
            return false;
        }
        if (err != nullptr) *err = ERR_OK;
        return true;
    }

    /* ERASE is idempotent. If ACKs were lost, VERIFY=invalid confirms the
     * requested final state. */
    uint8_t valid = 1u;
    uint32_t seq = 0u;
    uint8_t verify_err = ERR_OK;
    if (flash_verify(&valid, &seq, &verify_err) && valid == 0u) {
        if (err != nullptr) *err = ERR_OK;
        return true;
    }
    if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
    return false;
}

bool flash_verify(uint8_t *ok, uint32_t *seq, uint8_t *err)
{
    ProtoResponse resp = {};
    if (ok == nullptr || seq == nullptr) {
        if (err != nullptr) *err = ERR_PARAM_OUT_OF_RANGE;
        return false;
    }
    if (!request_with_retry(CMD_FLASH_PARAM_VERIFY, nullptr, 0u, &resp,
                            CFG_FLASH_REQ_TIMEOUT_MS, 2u)) {
        if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err != nullptr) *err = resp.err;
        return false;
    }
    if (resp.len < 5u) {
        if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    uint16_t pos = 0u;
    up_u8(resp.data, pos, ok);
    up_u32(resp.data, pos, seq);
    if (err != nullptr) *err = ERR_OK;
    return true;
}

bool device_reset(uint8_t *err)
{
    ProtoResponse resp;
    if (!ProtocolClient::request(CMD_RESET, nullptr, 0, &resp, CFG_REQ_TIMEOUT_MS)) {
        if (err != nullptr) *err = ERR_COMMUNICATION_ERROR;
        return false;
    }
    if (resp.status != STATUS_OK) {
        if (err != nullptr) *err = resp.err;
        return false;
    }
    if (err != nullptr) *err = ERR_OK;
    return true;
}

} // namespace pid_manager
} // namespace dome
