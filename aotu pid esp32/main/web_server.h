/**
 * @file    web_server.h
 * @brief   HTTP + WebSocket 服务器
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <stdbool.h>

namespace dome {
namespace web_server {

bool init(int port);
void broadcast_status();           /* 快照以 JSON 推送到所有 WS 客户端 */
void broadcast_log(const char *line); /* 日志行推送到所有 WS 客户端 */

} // namespace web_server
} // namespace dome
