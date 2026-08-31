/**
 * @file    bsp_display.h
 * @brief   OLED display manager for the STM32 control device.
 * @note    Autotune is owned by the ESP32; STM32 does not implement it.
 */
#ifndef BSP_DISPLAY_H
#define BSP_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化 GPIO 与 SSD1306, 默认主界面自动轮显 */
void bsp_display_init(void);

/** 主循环轮询: 按 CFG_DISPLAY_REFRESH_MS 周期重绘 */
void bsp_display_poll(uint32_t now_ms);

/** 设置显示页面 (DISP_PAGE_*), 0xFF 恢复自动轮显 */
void bsp_display_set_page(uint8_t page);

#ifdef __cplusplus
}
#endif

#endif /* BSP_DISPLAY_H */
