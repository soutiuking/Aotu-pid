/**
 * @file    app_config.h
 * @brief   ESP32-S3 绔泦涓厤缃?(寮曡剼/娉㈢壒鐜?瓒呮椂/杞鍛ㄦ湡/Wi-Fi 榛樿鍊?
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---------------- 涓插彛 (ESP32 <-> STM32) ---------------- */
#define CFG_UART_NUM            UART_NUM_1
#define CFG_UART_TX_PIN         17
#define CFG_UART_RX_PIN         18
#define CFG_UART_BAUD           115200
#define CFG_UART_RX_BUF         1024
#define CFG_UART_TX_BUF         512

/* ---------------- 璇锋眰瓒呮椂涓庤疆璇?---------------- */
#define CFG_REQ_TIMEOUT_MS      500     /* 鍗曞懡浠ゅ搷搴旇秴鏃?*/
#define CFG_FLASH_REQ_TIMEOUT_MS 1500   /* Flash 鎿﹀啓鍛戒护瓒呮椂 (椤垫摝闄ょ害40ms, 鐣欒閲? */
#define CFG_PARAM_WRITE_RETRIES  3       /* Max attempts for the retry-safe atomic PID write */
#define CFG_PARAM_RETRY_DELAY_MS 10      /* Line recovery delay before retrying an atomic PID write */
#define CFG_POLL_STATUS_MS      1000    /* 璁惧鐘舵€佽疆璇?*/
#define CFG_POLL_RUNTIME_MS     200     /* 瀹炴椂鏁版嵁杞 */
#define CFG_POLL_AUTOTUNE_MS    300     /* 鏁村畾鐘舵€佽疆璇?*/
#define CFG_WS_PUSH_MS          200     /* WebSocket 鎺ㄩ€佽妭娴?*/

/* ---------------- Wi-Fi ---------------- */
#define CFG_WIFI_AP_SSID_PREFIX "DOME-PID"
#define CFG_WIFI_AP_PASS        "12345678"
#define CFG_WIFI_STA_MAX_WAIT_S 20      /* STA 杩炴帴瓒呮椂鍚庡垏 AP 閰嶇綉 */

/* ---------------- Web ---------------- */
#define CFG_WEB_PORT            80

#endif /* APP_CONFIG_H */
