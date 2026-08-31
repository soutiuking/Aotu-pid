/**
 * @file    device_state.c
 * @brief   设备全局状态、错误记录与通信统计
 */
#include "device_state.h"
#include "protocol_parser.h"

#include <string.h>

static device_state_ctx_t g_dev;
static dev_error_entry_t g_errs[DEV_ERR_MAX_ENTRIES];
static uint8_t g_err_head = 0u;   /* 下一个写入位置 (环形) */
static uint8_t g_err_count = 0u;

void device_state_init(void)
{
    (void)memset(&g_dev, 0, sizeof(g_dev));
    (void)memset(g_errs, 0, sizeof(g_errs));
    g_dev.state = DEV_STATE_INIT;
    g_dev.comm_last_rx_ms = 0xFFFFFFFFu;
    g_err_head = 0u;
    g_err_count = 0u;
}

void device_state_set(uint8_t state)
{
    g_dev.state = state;
}

uint8_t device_state_get(void)
{
    return g_dev.state;
}

device_state_ctx_t *device_state_ctx(void)
{
    return &g_dev;
}

void device_state_report_error(uint16_t code, uint8_t loop_id)
{
    dev_error_entry_t *e = &g_errs[g_err_head];

    e->code = code;
    e->loop_id = loop_id;
    e->tick = g_dev.uptime_ms;
    g_err_head = (uint8_t)((g_err_head + 1u) % DEV_ERR_MAX_ENTRIES);
    if (g_err_count < DEV_ERR_MAX_ENTRIES) {
        g_err_count++;
    }

    switch (code) {
    case ERR_CRC_ERROR:
    case PROTO_ERR_CRC:
        g_dev.crc_errors++;
        break;
    case ERR_FRAME_TIMEOUT:
    case PROTO_ERR_TIMEOUT:
        g_dev.timeout_errors++;
        break;
    case ERR_PARAM_OUT_OF_RANGE:
        g_dev.range_errors++;
        break;
    case ERR_FLASH_ERROR:
    case ERR_FLASH_DATA_INVALID:
        g_dev.flash_errors++;
        break;
    default:
        g_dev.frame_errors++;
        break;
    }
}

uint8_t device_state_get_error(uint8_t idx, dev_error_entry_t *e)
{
    uint8_t start;
    if (idx >= DEV_ERR_MAX_ENTRIES || e == 0) {
        return 0u;
    }
    if (idx >= g_err_count) {
        return 0u;
    }
    /* idx=0 is the newest entry, so the OLED always shows the fault
     * associated with the current operation instead of stale boot errors. */
    start = (uint8_t)((g_err_head + DEV_ERR_MAX_ENTRIES - 1u - idx) %
                      DEV_ERR_MAX_ENTRIES);
    *e = g_errs[start];
    return 1u;
}

void device_state_refresh(void)
{
    if (g_dev.safe_latched != 0u) {
        g_dev.state = DEV_STATE_SAFE;
    }
}
