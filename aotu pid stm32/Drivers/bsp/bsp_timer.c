/**
 * @file    bsp_timer.c
 * @brief   时基工具: 毫秒时基 + 软件定时器
 */
#include "bsp_timer.h"
#include "stm32f1xx_hal.h"

uint32_t bsp_timer_now(void)
{
    return HAL_GetTick();
}

uint8_t bsp_timer_expired(bsp_soft_timer_t *t, uint32_t now, uint8_t start)
{
    uint8_t fire;

    if (t == 0 || t->period_ms == 0u) {
        return 0u;
    }
    if (start != 0u) {
        t->last = now - t->period_ms;
        return 1u;
    }
    fire = (uint8_t)((now - t->last) >= t->period_ms);
    if (fire != 0u) {
        t->last = now;
    }
    return fire;
}
