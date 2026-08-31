/**
 * @file    esp32_config.h
 * @brief   ESP32 侧 NVS 配置存储 (Wi-Fi 凭据)
 */
#pragma once

#include <cstddef>

namespace dome {
namespace esp32_config {

bool init();
void get_wifi(char *ssid, size_t ssid_cap, char *pass, size_t pass_cap);
void set_wifi(const char *ssid, const char *pass);

} // namespace esp32_config
} // namespace dome
