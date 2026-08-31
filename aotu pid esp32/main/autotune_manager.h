/**
 * @file    autotune_manager.h
 * @brief   ESP32-side autotune lifecycle interface (algorithm placeholder).
 *
 * Phase 1 intentionally contains no tuning algorithm. A future algorithm is
 * added behind this interface and uses pid_manager for all STM32 interaction.
 * STM32 remains a PID controller/feedback/output device only.
 */
#pragma once

#include <cstdint>
#include <stdbool.h>
#include "protocol_def.h"

namespace dome {
namespace autotune_manager {

struct Config {
    uint8_t loop;
    uint8_t mode;
    float target;
    float out_max;
    float out_min;
    float allowed_err;
    float fb_min;
    float fb_max;
    uint32_t max_time_s;
    uint8_t max_osc;
};

struct Status {
    uint8_t loop;
    uint8_t state;
    uint8_t progress;
    uint8_t osc;
    uint32_t elapsed_s;
    uint32_t eta_s;
    float target;
    float feedback;
    float output;
};

struct Result {
    uint8_t loop;
    uint8_t valid;
    float kp;
    float ki;
    float kd;
};

/* Initialize the ESP32-side state holder. No worker task is started. */
bool init();

/* Lifecycle API reserved for a future selected algorithm. */
bool start(const Config *cfg, uint8_t *err);
bool stop(uint8_t *err);
bool pause(uint8_t *err);
bool resume(uint8_t *err);
bool get_status(Status *out);
bool get_result(Result *out);
bool apply(uint8_t loop, uint8_t *err);
bool busy();

} // namespace autotune_manager
} // namespace dome
