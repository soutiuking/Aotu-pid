/**
 * @file    esp32_config.cpp
 * @brief   ESP32 侧 NVS 配置存储 (Wi-Fi 凭据)
 */
#include "esp32_config.h"

#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"

#include <string.h>

namespace dome {
namespace esp32_config {

static const char *TAG = "cfg";
static const char *NS = "dome";

bool init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs init failed: %d", err);
        return false;
    }
    return true;
}

void get_wifi(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap)
{
    nvs_handle_t h;
    ssid[0] = '\0';
    pass[0] = '\0';
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t s = ssid_cap, p = pass_cap;
        nvs_get_str(h, "ssid", ssid, &s);
        nvs_get_str(h, "pass", pass, &p);
        nvs_close(h);
    }
}

void set_wifi(const char *ssid, const char *pass)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_set_str(h, "ssid", ssid);
        nvs_set_str(h, "pass", pass);
        nvs_commit(h);
        nvs_close(h);
    }
}

} // namespace esp32_config
} // namespace dome
