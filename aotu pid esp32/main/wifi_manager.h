/**
 * @file    wifi_manager.h
 * @brief   Wi-Fi 管理: STA 优先, 失败回退 AP 配网, 自动重连
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <stdbool.h>

namespace dome {
namespace wifi_manager {

bool init();                     /* NVS 之后调用 */
bool is_sta();
bool is_ap();
void get_ip(char *out, size_t cap);
void get_ssid(char *out, size_t cap);
/* AP 配网: 保存凭据并尝试连接 */
bool set_credentials(const char *ssid, const char *pass);

} // namespace wifi_manager
} // namespace dome
