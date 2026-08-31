/**
 * @file    wifi_manager.cpp
 * @brief   Wi-Fi 管理: STA 优先 (NVS 凭据), 超时回退 AP "DOME-PID-xxxx" 配网
 */
#include "wifi_manager.h"
#include "app_config.h"
#include "storage/esp32_config.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"

#include <string.h>
#include <stdio.h>

namespace dome {
namespace wifi_manager {

static const char *TAG = "wifi";

static EventGroupHandle_t s_events = nullptr;
#define EV_STA_CONNECTED BIT0
#define EV_STA_FAIL      BIT1

static bool s_sta = false;
static bool s_ap = false;
static char s_ip[16] = "0.0.0.0";
static char s_ssid[33] = "";

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            esp_wifi_connect();
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            s_sta = false;
            xEventGroupSetBits(s_events, EV_STA_FAIL);
            ESP_LOGW(TAG, "STA disconnected, will retry");
            break;
        case WIFI_EVENT_AP_START:
            s_ap = true;
            break;
        default:
            break;
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *e = (ip_event_got_ip_t *)data;
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&e->ip_info.ip));
        s_sta = true;
        xEventGroupSetBits(s_events, EV_STA_CONNECTED);
        ESP_LOGI(TAG, "got ip %s", s_ip);
    }
}

static bool start_sta(const char *ssid, const char *pass)
{
    wifi_config_t cfg = {};

    s_sta = false;
    s_ap = false;
    strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
    xEventGroupClearBits(s_events, EV_STA_CONNECTED | EV_STA_FAIL);
    (void)esp_wifi_stop(); /* 从 AP 模式切换前先停止 (未启动时报错可忽略) */
    strlcpy((char *)cfg.sta.ssid, ssid, sizeof(cfg.sta.ssid));
    strlcpy((char *)cfg.sta.password, pass, sizeof(cfg.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    EventBits_t bits = xEventGroupWaitBits(s_events,
                                           EV_STA_CONNECTED | EV_STA_FAIL,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(CFG_WIFI_STA_MAX_WAIT_S * 1000));
    return (bits & EV_STA_CONNECTED) != 0;
}

static void start_ap()
{
    uint8_t mac[6] = {0};

    s_sta = false;
    s_ap = false;
    strlcpy(s_ip, "0.0.0.0", sizeof(s_ip));
    char ssid[32];
    wifi_config_t cfg = {};

    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(ssid, sizeof(ssid), "%s-%02X%02X", CFG_WIFI_AP_SSID_PREFIX, mac[4], mac[5]);

    (void)esp_wifi_stop(); /* 从 STA 模式切换前先停止 */
    strlcpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = (uint8_t)strlen(ssid);
    strlcpy((char *)cfg.ap.password, CFG_WIFI_AP_PASS, sizeof(cfg.ap.password));
    cfg.ap.max_connection = 4;
    cfg.ap.authmode = WIFI_AUTH_WPA_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGW(TAG, "AP mode: ssid=%s pass=%s", ssid, CFG_WIFI_AP_PASS);
}

bool init()
{
    s_events = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wcfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               event_handler, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               event_handler, nullptr));

    char ssid[33] = "", pass[65] = "";
    esp32_config::get_wifi(ssid, sizeof(ssid), pass, sizeof(pass));
    strlcpy(s_ssid, ssid, sizeof(s_ssid));

    if (ssid[0] != '\0' && start_sta(ssid, pass)) {
        ESP_LOGI(TAG, "STA connected to %s", ssid);
        return true;
    }

    ESP_LOGW(TAG, "STA not available, starting AP config");
    start_ap();
    return true;
}

bool is_sta() { return s_sta; }
bool is_ap()  { return s_ap; }

void get_ip(char *out, size_t cap)
{
    if (out != nullptr) {
        strlcpy(out, s_ip, cap);
    }
}

void get_ssid(char *out, size_t cap)
{
    if (out != nullptr) {
        strlcpy(out, s_ssid, cap);
    }
}

bool set_credentials(const char *ssid, const char *pass)
{
    if (ssid == nullptr || ssid[0] == '\0') {
        return false;
    }
    esp32_config::set_wifi(ssid, pass ? pass : "");
    strlcpy(s_ssid, ssid, sizeof(s_ssid));
    s_ap = false;
    if (start_sta(ssid, pass ? pass : "")) {
        ESP_LOGI(TAG, "switched to STA: %s", ssid);
        return true;
    }
    ESP_LOGW(TAG, "STA failed with new creds, back to AP");
    start_ap();
    return false;
}

} // namespace wifi_manager
} // namespace dome
