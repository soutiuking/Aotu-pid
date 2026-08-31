/**
 * @file    pid_controller.c
 * @brief   位置式 PID 控制器
 *
 * 实现要点:
 *  - 位置式 PID, 微分先行 (对测量值微分, 目标值突变不冲击输出);
 *  - 测量值一阶低通滤波 (filter_coefficient 为新值权重);
 *  - 积分限幅 + 输出限幅联合抗饱和 (积分钳位到输出反向饱和所需范围);
 *  - 死区: |误差| < deadband 时误差按 0 处理;
 *  - 正/反作用: 反作用时内部误差取反;
 *  - NaN/Inf 保护: 异常输入时保持输出并进入故障路径 (由调用方处理返回标志)。
 */
#include "pid_controller.h"
#include "protocol_def.h"
#include "protocol_codec.h"

#include <string.h>

void pid_init(pid_t *p)
{
    if (p == 0) {
        return;
    }
    (void)memset(p, 0, sizeof(*p));
    p->output_min = -100.0f;
    p->output_max = 100.0f;
    p->sample_time = 0.1f;
}

void pid_apply_params(pid_t *p, float kp, float ki, float kd,
                      float output_min, float output_max,
                      float integral_limit, float sample_time,
                      float deadband, float filter_coefficient,
                      uint8_t direction)
{
    if (p == 0) {
        return;
    }
    p->kp = kp;
    p->ki = ki;
    p->kd = kd;
    p->output_min = output_min;
    p->output_max = output_max;
    p->integral_limit = integral_limit;
    p->sample_time = sample_time;
    p->deadband = deadband;
    p->filter_coefficient = filter_coefficient;
    p->control_direction = direction;
    p->integral = 0.0f;
    p->initialized = 0u;
}

static float clampf(float v, float lo, float hi)
{
    if (v < lo) {
        return lo;
    }
    if (v > hi) {
        return hi;
    }
    return v;
}

float pid_compute(pid_t *p, float meas)
{
    float error, meas_f, deriv, unsat, out;
    float dir = (p->control_direction == PID_DIR_REVERSE) ? -1.0f : 1.0f;

    if (p == 0) {
        return 0.0f;
    }

    /* 输入保护 */
    if (!codec_f32_is_finite(meas) || !codec_f32_is_finite(p->target)) {
        return p->output; /* 保持当前输出, 故障由调用方根据反馈判定 */
    }

    /* 测量值低通滤波: y = y + a*(x - y) */
    if (p->filter_coefficient > 0.0f && p->filter_coefficient < 1.0f) {
        if (p->initialized == 0u) {
            p->filtered_meas = meas;
        } else {
            p->filtered_meas += p->filter_coefficient * (meas - p->filtered_meas);
        }
    } else {
        p->filtered_meas = meas;
    }

    /* 误差 (含方向) */
    error = dir * (p->target - p->filtered_meas);

    /* 死区 */
    if (p->deadband > 0.0f && error < p->deadband && error > -p->deadband) {
        error = 0.0f;
    }

    /* 积分 (先钳位防饱和) */
    p->integral += p->ki * error * p->sample_time;
    if (p->integral > p->integral_limit) {
        p->integral = p->integral_limit;
    }
    if (p->integral < -p->integral_limit) {
        p->integral = -p->integral_limit;
    }

    /* 微分先行: 对滤波后测量值微分 */
    if (p->initialized == 0u) {
        p->prev_meas = p->filtered_meas;
        deriv = 0.0f;
        p->initialized = 1u;
    } else {
        deriv = -dir * (p->filtered_meas - p->prev_meas) / p->sample_time;
        p->prev_meas = p->filtered_meas;
    }

    unsat = p->kp * error + p->integral + p->kd * deriv;
    out = clampf(unsat, p->output_min, p->output_max);

    /* 抗饱和: 输出饱和且积分继续推向饱和方向时, 回退积分 */
    if (unsat != out) {
        if ((unsat > p->output_max && p->integral > 0.0f) ||
            (unsat < p->output_min && p->integral < 0.0f)) {
            p->integral -= p->ki * error * p->sample_time;
        }
    }

    p->output = out;
    return out;
}

void pid_set_manual_output(pid_t *p, float out)
{
    if (p == 0) {
        return;
    }
    p->output = clampf(out, p->output_min, p->output_max);
    /* 手动模式下让积分跟踪输出, 保证切回自动时无扰 */
    p->integral = p->output;
}

void pid_pause(pid_t *p)
{
    if (p != 0) {
        /* 冻结: 积分跟踪当前输出, 恢复时无跳变 */
        p->integral = p->output;
    }
}

float pid_get_output(const pid_t *p)
{
    return (p != 0) ? p->output : 0.0f;
}

void pid_bumpless_transfer(pid_t *p)
{
    if (p != 0) {
        p->integral = p->output; /* 忽略 Kp/Kd 项差异, 近似无扰 */
        p->initialized = 0u;     /* 微分项首拍清零 */
    }
}
