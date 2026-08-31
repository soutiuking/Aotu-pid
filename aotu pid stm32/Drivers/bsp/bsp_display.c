/**
 * @file    bsp_display.c
 * @brief   OLED display manager for the STM32 control device.
 *
 * The STM32 provides only the main, parameter, and error pages. Autotune is
 * owned by the ESP32; DISP_PAGE_AUTOTUNE remains a protocol compatibility
 * value and is intentionally not rendered here.
 */
#include "bsp_display.h"
#include "oled.h"
#include "oledfont.h"
#include "app_config.h"
#include "device_state.h"
#include "control_task.h"
#include "parameter_storage.h"
#include "protocol_def.h"

#include <stdio.h>
#include <string.h>

/* 128×32 屏幕布局: 6x8字体, 4行×21列, 每行起始像素Y坐标 */
#define DISP_FONT_SIZE  8u
#define DISP_LINE_Y(n)  ((uint8_t)((n) * 8u))
#define DISP_COL(n)     ((uint8_t)((n) * 6u))

/* OLED 引脚: PA0=SCL PA1=SDA PA4=RES (与 oled.h 一致, CubeMX 未配置, 此处初始化) */
static void display_gpio_init(void)
{
    GPIO_InitTypeDef gpio;

    __HAL_RCC_GPIOA_CLK_ENABLE();

    gpio.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOA, &gpio);

    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4, GPIO_PIN_SET);
}

/* ---- 浮点格式化 (避免 sprintf 浮点依赖, 支持负数) ---- */
static void fmtf(char *buf, float v, uint8_t dec)
{
    int32_t scaled;
    int32_t ipart;
    int32_t fpart;
    uint32_t div = 1u;
    uint8_t i;

    for (i = 0; i < dec; i++) {
        div *= 10u;
    }
    /* 四舍五入到 dec 位 */
    if (v < 0.0f) {
        scaled = (int32_t)(v * (float)div - 0.5f);
    } else {
        scaled = (int32_t)(v * (float)div + 0.5f);
    }
    ipart = scaled / (int32_t)div;
    fpart = scaled % (int32_t)div;
    if (fpart < 0) {
        fpart = -fpart;
    }
    if (dec == 0u) {
        (void)snprintf(buf, 16, "%d", (int)ipart);
    } else if (dec == 1u) {
        (void)snprintf(buf, 16, "%d.%d", (int)ipart, (int)fpart);
    } else {
        (void)snprintf(buf, 16, "%d.%02d", (int)ipart, (int)fpart);
    }
}

static const char *state_text(uint8_t s)
{
    switch (s) {
    case PID_STATE_RUNNING: return "RUN ";
    case PID_STATE_PAUSED:  return "PAUS";
    case PID_STATE_MANUAL:  return "MAN ";
    case PID_STATE_SAFE:    return "SAFE";
    case PID_STATE_FAULT:   return "FLT ";
    default:                return "IDLE";
    }
}

static const char *dev_state_text(uint8_t s)
{
    switch (s) {
    case DEV_STATE_READY:    return "RDY";
    case DEV_STATE_RUNNING:  return "RUN";
    case DEV_STATE_AUTOTUNE: return "AT ";
    case DEV_STATE_SAFE:     return "SAF";
    case DEV_STATE_FAULT:    return "FLT";
    default:                 return "INI";
    }
}

/* ---------------- 页面绘制 ---------------- */

static void page_main(void)
{
    char b[28];
    device_state_ctx_t *dev = device_state_ctx();
    pid_loop_t *L = control_task_loop(0u);
    uint8_t comm = (dev->comm_link_up != 0u) ? 1u : 0u;

    (void)snprintf(b, sizeof(b), "DOME-PID %s %s",
                   comm ? "ONL" : "OFF", dev_state_text(dev->state));
    OLED_ShowString(0, DISP_LINE_Y(0), (uint8_t *)b, DISP_FONT_SIZE, 1);

    if (L != 0) {
        (void)snprintf(b, sizeof(b), "L0 %s %s",
                       state_text(L->state),
                       (L->params.pid_mode == PID_MODE_AUTO) ? "AUTO" : "MANU");
        OLED_ShowString(0, DISP_LINE_Y(1), (uint8_t *)b, DISP_FONT_SIZE, 1);

        (void)snprintf(b, sizeof(b), "T:");
        OLED_ShowString(0, DISP_LINE_Y(2), (uint8_t *)b, DISP_FONT_SIZE, 1);
        fmtf(b, L->params.target, 1u);
        OLED_ShowString(DISP_COL(2), DISP_LINE_Y(2), (uint8_t *)b, DISP_FONT_SIZE, 1);
        (void)snprintf(b, sizeof(b), "F:");
        OLED_ShowString(DISP_COL(12), DISP_LINE_Y(2), (uint8_t *)b, DISP_FONT_SIZE, 1);
        fmtf(b, L->feedback, 1u);
        OLED_ShowString(DISP_COL(14), DISP_LINE_Y(2), (uint8_t *)b, DISP_FONT_SIZE, 1);

        (void)snprintf(b, sizeof(b), "O:");
        OLED_ShowString(0, DISP_LINE_Y(3), (uint8_t *)b, DISP_FONT_SIZE, 1);
        fmtf(b, L->output, 1u);
        OLED_ShowString(DISP_COL(2), DISP_LINE_Y(3), (uint8_t *)b, DISP_FONT_SIZE, 1);
        (void)snprintf(b, sizeof(b), "FLASH:%s",
                       parameter_storage_is_valid() ? "OK" : "DEF");
        OLED_ShowString(DISP_COL(12), DISP_LINE_Y(3), (uint8_t *)b, DISP_FONT_SIZE, 1);
    }
}

static void page_param(void)
{
    char b[28];
    pid_params_t p;
    pid_loop_t *L = control_task_loop(0u);

    if (L == 0) {
        return;
    }
    p = L->params;

    (void)snprintf(b, sizeof(b), "PARAM L0");
    OLED_ShowString(0, DISP_LINE_Y(0), (uint8_t *)b, DISP_FONT_SIZE, 1);

    (void)snprintf(b, sizeof(b), "Kp:");
    OLED_ShowString(0, DISP_LINE_Y(1), (uint8_t *)b, DISP_FONT_SIZE, 1);
    fmtf(b, p.kp, 2u);
    OLED_ShowString(DISP_COL(3), DISP_LINE_Y(1), (uint8_t *)b, DISP_FONT_SIZE, 1);
    (void)snprintf(b, sizeof(b), "Ki:");
    OLED_ShowString(DISP_COL(11), DISP_LINE_Y(1), (uint8_t *)b, DISP_FONT_SIZE, 1);
    fmtf(b, p.ki, 2u);
    OLED_ShowString(DISP_COL(14), DISP_LINE_Y(1), (uint8_t *)b, DISP_FONT_SIZE, 1);

    (void)snprintf(b, sizeof(b), "Kd:");
    OLED_ShowString(0, DISP_LINE_Y(2), (uint8_t *)b, DISP_FONT_SIZE, 1);
    fmtf(b, p.kd, 2u);
    OLED_ShowString(DISP_COL(3), DISP_LINE_Y(2), (uint8_t *)b, DISP_FONT_SIZE, 1);
    (void)snprintf(b, sizeof(b), "IL:%d", (int)p.integral_limit);
    OLED_ShowString(DISP_COL(11), DISP_LINE_Y(2), (uint8_t *)b, DISP_FONT_SIZE, 1);

    {
        char b1[16], b2[16];
        fmtf(b1, p.output_min, 0u);
        fmtf(b2, p.output_max, 0u);
        (void)snprintf(b, sizeof(b), "OUT:%s~%s TS:%dms",
                       b1, b2, (int)(p.sample_time * 1000.0f));
        OLED_ShowString(0, DISP_LINE_Y(3), (uint8_t *)b, DISP_FONT_SIZE, 1);
    }
}

static void page_error(void)
{
    char b[28];
    dev_error_entry_t e;
    uint8_t i;

    (void)snprintf(b, sizeof(b), "ERROR LOG");
    OLED_ShowString(0, DISP_LINE_Y(0), (uint8_t *)b, DISP_FONT_SIZE, 1);

    for (i = 0; i < 3u; i++) {
        if (device_state_get_error(i, &e) != 0u) {
            (void)snprintf(b, sizeof(b), "0x%02X L%d %ds",
                           (unsigned)e.code, (e.loop_id == 0xFFu) ? 9 : (int)e.loop_id,
                           (int)(e.tick / 1000u));
        } else {
            (void)snprintf(b, sizeof(b), "-");
        }
        OLED_ShowString(0, DISP_LINE_Y(1u + i), (uint8_t *)b, DISP_FONT_SIZE, 1);
    }
}

/* ---------------- 公开 API ---------------- */

static uint8_t cur_page = DISP_PAGE_MAIN;
static uint8_t auto_cycle = 1u;
static uint32_t last_refresh = 0u;
static uint32_t last_cycle = 0u;

void bsp_display_init(void)
{
    display_gpio_init();
    OLED_Init();
    cur_page = DISP_PAGE_MAIN;
    auto_cycle = 1u;
    OLED_Clear();
}

void bsp_display_set_page(uint8_t page)
{
    if (page == DISP_PAGE_AUTO_CYCLE) {
        auto_cycle = 1u;
        return;
    }
    /* Autotune is owned by the ESP32 and has no STM32 display page. */
    if (page == DISP_PAGE_AUTOTUNE) {
        return;
    }
    if (page == DISP_PAGE_MAIN || page == DISP_PAGE_PARAM || page == DISP_PAGE_ERROR) {
        auto_cycle = 0u;
        cur_page = page;
        OLED_Clear();
    }
}

void bsp_display_poll(uint32_t now_ms)
{
    if (auto_cycle != 0u && (now_ms - last_cycle) >= CFG_DISPLAY_CYCLE_MS) {
        last_cycle = now_ms;
        if (cur_page == DISP_PAGE_MAIN) {
            cur_page = DISP_PAGE_PARAM;
        } else if (cur_page == DISP_PAGE_PARAM) {
            cur_page = DISP_PAGE_ERROR;
        } else {
            cur_page = DISP_PAGE_MAIN;
        }
        OLED_Clear();
    }

    if ((now_ms - last_refresh) < CFG_DISPLAY_REFRESH_MS) {
        return;
    }
    last_refresh = now_ms;

    switch (cur_page) {
    case DISP_PAGE_PARAM: page_param(); break;
    case DISP_PAGE_ERROR: page_error(); break;
    default:                 page_main(); break;
    }
    OLED_Refresh();
}
