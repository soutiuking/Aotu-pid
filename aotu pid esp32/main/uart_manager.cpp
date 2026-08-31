/**
 * @file    uart_manager.cpp
 * @brief   UART 收发封装
 */
#include "uart_manager.h"
#include "app_config.h"

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

namespace dome {
namespace uart_manager {

static const char *TAG = "uart";

bool init(int tx_pin, int rx_pin, int baud)
{
    uart_config_t cfg = {};
    cfg.baud_rate = baud;
    cfg.data_bits = UART_DATA_8_BITS;
    cfg.parity = UART_PARITY_DISABLE;
    cfg.stop_bits = UART_STOP_BITS_1;
    cfg.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    cfg.source_clk = UART_SCLK_DEFAULT;

    if (uart_driver_install(CFG_UART_NUM, CFG_UART_RX_BUF, CFG_UART_TX_BUF,
                            0, nullptr, 0) != ESP_OK) {
        ESP_LOGE(TAG, "driver install failed");
        return false;
    }
    if (uart_param_config(CFG_UART_NUM, &cfg) != ESP_OK) {
        (void)uart_driver_delete(CFG_UART_NUM);
        return false;
    }
    if (uart_set_pin(CFG_UART_NUM, tx_pin, rx_pin,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE) != ESP_OK) {
        (void)uart_driver_delete(CFG_UART_NUM);
        return false;
    }
    return true;
}

bool write(const uint8_t *data, size_t len)
{
    return uart_write_bytes(CFG_UART_NUM, data, len) == (int)len;
}

int read(uint8_t *buf, size_t len)
{
    int n = uart_read_bytes(CFG_UART_NUM, buf, len, 0);
    return (n > 0) ? n : 0;
}

} // namespace uart_manager
} // namespace dome
