/**
 * @file    bsp_timer.h
 * @brief   时基工具: 毫秒时基 + 软件定时器 (基于 SysTick/HAL tick)
 */
#ifndef BSP_TIMER_H
#define BSP_TIMER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 当前毫秒时基 */
uint32_t bsp_timer_now(void);

/** 软件周期定时器 */
typedef struct
{
    uint32_t period_ms;
    uint32_t last;
} bsp_soft_timer_t;

/** 周期到检查 (自动重装); start=1 立即触发首拍 */
uint8_t bsp_timer_expired(bsp_soft_timer_t *t, uint32_t now, uint8_t start);

#ifdef __cplusplus
}
#endif

#endif /* BSP_TIMER_H */
