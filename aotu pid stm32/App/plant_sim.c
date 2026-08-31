/**
 * @file    plant_sim.c
 * @brief   一阶惯性对象仿真 (按回路分通道): y' = (K*u - y)/tau + noise
 *
 * CFG_PLANT_SIMULATION=0 时, 将本文件替换为真实硬件实现:
 *   plant_sim_step(): 写 PWM/DAC 执行输出, 读 ADC 返回反馈
 */
#include "plant_sim.h"
#include "app_config.h"

#if CFG_PLANT_SIMULATION

static float plant_y[PID_LOOP_MAX];
static uint32_t lcg[PID_LOOP_MAX] = { 0x1234ABCDu, 0x5678DEFEu, 0x9ABC1234u };

static float sim_noise(uint8_t ch)
{
    lcg[ch] = lcg[ch] * 1664525u + 1013904223u;
    /* 映射到 -1..1 */
    return ((float)((lcg[ch] >> 8) & 0xFFFFu) / 32768.0f) - 1.0f;
}

float plant_sim_step(uint8_t ch, float u, float dt)
{
    if (ch >= PID_LOOP_MAX || dt <= 0.0f) {
        return 0.0f;
    }
    plant_y[ch] += ((CFG_PLANT_GAIN * u) - plant_y[ch]) * (dt / CFG_PLANT_TAU);
    plant_y[ch] += CFG_PLANT_NOISE * sim_noise(ch);
    if (plant_y[ch] > 200.0f) {
        plant_y[ch] = 200.0f;
    }
    if (plant_y[ch] < -200.0f) {
        plant_y[ch] = -200.0f;
    }
    return plant_y[ch];
}

void plant_sim_reset(void)
{
    uint8_t ch;
    for (ch = 0; ch < PID_LOOP_MAX; ch++) {
        plant_y[ch] = 0.0f;
    }
}

#else /* 真实硬件桩: 替换为 ADC/PWM 实际读写 */

float plant_sim_step(uint8_t ch, float u, float dt)
{
    (void)ch;
    (void)dt;
    /* TODO: 写执行器 (PWM/DAC), 读传感器 (ADC), 返回反馈值 */
    (void)u;
    return 0.0f;
}

void plant_sim_reset(void)
{
}

#endif /* CFG_PLANT_SIMULATION */
