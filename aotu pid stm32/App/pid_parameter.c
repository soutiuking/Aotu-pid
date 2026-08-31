/**
 * @file    pid_parameter.c
 * @brief   PID 参数表管理: 校验/默认值/线格式打包 与范围表
 */
#include "pid_parameter.h"
#include "parameter_storage.h"
#include "protocol_codec.h"

#include <string.h>

/* 范围表: {min, max, step}, 索引 = PIDR_* */
static const float g_ranges[PIDR_COUNT][3] =
{
    /* kp        */ { 0.0f,   500.0f, 0.01f },
    /* ki        */ { 0.0f,   500.0f, 0.01f },
    /* kd        */ { 0.0f,   500.0f, 0.01f },
    /* target    */ { -1000.0f, 1000.0f, 0.1f },
    /* out_min   */ { -100.0f, 0.0f,   0.1f },
    /* out_max   */ { 0.0f,   100.0f, 0.1f },
    /* integ_lim */ { 0.0f,   10000.0f, 1.0f },
    /* samp_time */ { 0.005f, 1.0f,   0.001f },
    /* deadband  */ { 0.0f,   100.0f, 0.01f },
    /* filter    */ { 0.0f,   0.99f,  0.01f }
};

static pid_params_t g_params[PID_LOOP_MAX];

void pid_parameter_init(void)
{
    pid_parameter_sync_from_storage();
}

void pid_parameter_sync_from_storage(void)
{
    uint8_t i;
    pid_parameter_record_t rec;

    for (i = 0; i < PID_LOOP_MAX; i++) {
        if (parameter_storage_get(i, &rec) != 0u) {
            g_params[i].kp = rec.kp;
            g_params[i].ki = rec.ki;
            g_params[i].kd = rec.kd;
            g_params[i].target = rec.target;
            g_params[i].output_min = rec.output_min;
            g_params[i].output_max = rec.output_max;
            g_params[i].integral_limit = rec.integral_limit;
            g_params[i].sample_time = rec.sample_time;
            g_params[i].deadband = rec.deadband;
            g_params[i].filter_coefficient = rec.filter_coefficient;
            g_params[i].control_direction = rec.control_direction;
            g_params[i].pid_mode = rec.pid_mode;
            g_params[i].reserved[0] = 0u;
            g_params[i].reserved[1] = 0u;
        }
    }
}

uint8_t pid_parameter_validate(const pid_params_t *p, uint8_t *bad_idx)
{
    float v[PIDR_COUNT];
    uint8_t i;

    if (p == 0) {
        return ERR_PARAM_OUT_OF_RANGE;
    }

    v[PIDR_KP] = p->kp;
    v[PIDR_KI] = p->ki;
    v[PIDR_KD] = p->kd;
    v[PIDR_TARGET] = p->target;
    v[PIDR_OUTPUT_MIN] = p->output_min;
    v[PIDR_OUTPUT_MAX] = p->output_max;
    v[PIDR_INTEGRAL_LIMIT] = p->integral_limit;
    v[PIDR_SAMPLE_TIME] = p->sample_time;
    v[PIDR_DEADBAND] = p->deadband;
    v[PIDR_FILTER] = p->filter_coefficient;

    for (i = 0; i < PIDR_COUNT; i++) {
        if (!codec_f32_is_finite(v[i]) ||
            v[i] < g_ranges[i][0] || v[i] > g_ranges[i][1]) {
            if (bad_idx != 0) {
                *bad_idx = i;
            }
            return ERR_PARAM_OUT_OF_RANGE;
        }
    }

    /* 组合规则 */
    if (p->output_min >= p->output_max) {
        if (bad_idx != 0) {
            *bad_idx = 0xFFu;
        }
        return ERR_PARAM_OUT_OF_RANGE;
    }
    if (p->control_direction > 1u || p->pid_mode > 1u) {
        if (bad_idx != 0) {
            *bad_idx = 0xFFu;
        }
        return ERR_PARAM_OUT_OF_RANGE;
    }
    return ERR_OK;
}

uint8_t pid_parameter_get(uint8_t loop_id, pid_params_t *p)
{
    if (loop_id >= PID_LOOP_MAX || p == 0) {
        return 0u;
    }
    *p = g_params[loop_id];
    return 1u;
}

uint8_t pid_parameter_set(uint8_t loop_id, const pid_params_t *p)
{
    if (loop_id >= PID_LOOP_MAX || p == 0) {
        return ERR_PARAM_OUT_OF_RANGE;
    }
    if (pid_parameter_validate(p, 0) != ERR_OK) {
        return ERR_PARAM_OUT_OF_RANGE;
    }
    g_params[loop_id] = *p;
    return ERR_OK;
}

void pid_parameter_set_defaults(void)
{
    uint8_t i;
    pid_params_t d;

    (void)memset(&d, 0, sizeof(d));
    d.kp = 2.0f;
    d.ki = 0.5f;
    d.kd = 0.1f;
    d.target = 0.0f;
    d.output_min = -100.0f;
    d.output_max = 100.0f;
    d.integral_limit = 50.0f;
    d.sample_time = 0.1f;
    d.deadband = 0.0f;
    d.filter_coefficient = 0.2f;
    d.control_direction = PID_DIR_DIRECT;
    d.pid_mode = PID_MODE_MANUAL;

    for (i = 0; i < PID_LOOP_MAX; i++) {
        g_params[i] = d;
    }
}

const float *pid_parameter_range_table(void)
{
    return &g_ranges[0][0];
}

void pid_params_pack(uint8_t *buf, const pid_params_t *p)
{
    uint16_t pos = 0u;
    pos = codec_put_f32(buf, pos, p->kp);
    pos = codec_put_f32(buf, pos, p->ki);
    pos = codec_put_f32(buf, pos, p->kd);
    pos = codec_put_f32(buf, pos, p->target);
    pos = codec_put_f32(buf, pos, p->output_min);
    pos = codec_put_f32(buf, pos, p->output_max);
    pos = codec_put_f32(buf, pos, p->integral_limit);
    pos = codec_put_f32(buf, pos, p->sample_time);
    pos = codec_put_f32(buf, pos, p->deadband);
    pos = codec_put_f32(buf, pos, p->filter_coefficient);
    pos = codec_put_u8(buf, pos, p->control_direction);
    pos = codec_put_u8(buf, pos, p->pid_mode);
    pos = codec_put_u8(buf, pos, 0u);
    pos = codec_put_u8(buf, pos, 0u);
    (void)pos; /* = PID_PARAMS_WIRE_SIZE */
}

void pid_params_unpack(const uint8_t *buf, pid_params_t *p)
{
    uint16_t pos = 0u;
    pos = codec_get_f32(buf, pos, &p->kp);
    pos = codec_get_f32(buf, pos, &p->ki);
    pos = codec_get_f32(buf, pos, &p->kd);
    pos = codec_get_f32(buf, pos, &p->target);
    pos = codec_get_f32(buf, pos, &p->output_min);
    pos = codec_get_f32(buf, pos, &p->output_max);
    pos = codec_get_f32(buf, pos, &p->integral_limit);
    pos = codec_get_f32(buf, pos, &p->sample_time);
    pos = codec_get_f32(buf, pos, &p->deadband);
    pos = codec_get_f32(buf, pos, &p->filter_coefficient);
    pos = codec_get_u8(buf, pos, &p->control_direction);
    pos = codec_get_u8(buf, pos, &p->pid_mode);
    pos = codec_get_u8(buf, pos, &p->reserved[0]);
    pos = codec_get_u8(buf, pos, &p->reserved[1]);
    (void)pos;
}
