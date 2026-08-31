/**
 * @file    app_main.cpp
 * @brief   DOME 一期 ESP32-S3 入口: 初始化各模块并启动任务
 *
 * 任务划分 (协议要求的任务划分映射):
 *   proto_task          - protocol_client 内部: UART 收发/解析/请求匹配
 *   poll_task           - device_poll_task: 轮询 STM32 状态并更新快照
 *   publish_task        - status_publish_task: WebSocket JSON 推送 (节流)
 *   (wifi/httpd 任务由 ESP-IDF 组件内部创建)
 */
#include "app_config.h"
#include "storage/esp32_config.h"
#include "wifi_manager.h"
#include "uart_manager.h"
#include "protocol_client.h"
#include "pid_manager.h"
#include "autotune_manager.h"
#include "device_status.h"
#include "web_server.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"

extern "C" void app_main(void);

static const char *TAG = "main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "DOME PID project boot");

    if (!dome::esp32_config::init()) {
        ESP_LOGE(TAG, "NVS init failed, restart");
        esp_restart();
    }
    if (!dome::device_status::init()) {
        ESP_LOGE(TAG, "status init failed");
    }

    /* UART (ESP32 <-> STM32) */
    if (!dome::uart_manager::init(CFG_UART_TX_PIN, CFG_UART_RX_PIN, CFG_UART_BAUD)) {
        ESP_LOGE(TAG, "uart init failed");
    }
    if (!dome::ProtocolClient::init()) {
        ESP_LOGE(TAG, "protocol client init failed");
    }

    /* Wi-Fi (STA 优先, 失败回退 AP 配网) */
    dome::wifi_manager::init();

    /* Initialize ESP32-local autotune state before HTTP can accept commands.
     * Phase 1 starts no tuning algorithm task. */
    if (!dome::autotune_manager::init()) {
        ESP_LOGE(TAG, "autotune manager init failed");
    }

    /* Web 服务器 */
    /* Start the UART polling task before HTTP allocates its worker stacks.
     * Keep task creation results separate: a publish-task failure must never
     * hide or prevent creation of the STM32 communication task. */
    TaskHandle_t poll_handle = nullptr;
    BaseType_t poll_rc = xTaskCreate(dome::device_status::poll_task, "poll_task",
                                     6144, nullptr, 4, &poll_handle);
    if (poll_rc != pdPASS) {
        ESP_LOGE(TAG, "poll_task create failed, free heap=%lu",
                 (unsigned long)esp_get_free_heap_size());
        dome::device_status::log("poll_task CREATE FAILED, heap=%lu",
                                 (unsigned long)esp_get_free_heap_size());
    } else {
        ESP_LOGI(TAG, "poll_task created, free heap=%lu",
                 (unsigned long)esp_get_free_heap_size());
    }

    /* Web server */
    if (!dome::web_server::init(CFG_WEB_PORT)) {
        ESP_LOGE(TAG, "web server init failed");
    }

    TaskHandle_t publish_handle = nullptr;
    BaseType_t publish_rc = xTaskCreate(dome::device_status::publish_task, "pub_task",
                                        4096, nullptr, 7, &publish_handle);
    if (publish_rc != pdPASS) {
        ESP_LOGE(TAG, "pub_task create failed, free heap=%lu",
                 (unsigned long)esp_get_free_heap_size());
        dome::device_status::log("pub_task CREATE FAILED, heap=%lu",
                                 (unsigned long)esp_get_free_heap_size());
    }

    dome::device_status::log("ESP32 booted, fw 1.0.0");
    ESP_LOGI(TAG, "all modules started");
}
