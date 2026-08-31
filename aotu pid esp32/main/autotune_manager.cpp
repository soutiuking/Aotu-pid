/**
 * @file    autotune_manager.cpp
 * @brief   ESP32-side autotune lifecycle placeholder.
 *
 * No tuning calculation or excitation logic is implemented in phase 1.
 * Future algorithms shall communicate with STM32 only through the normal PID
 * commands exposed by pid_manager: parameter read/write, runtime read,
 * target/output/mode set, and PID start/stop/pause/resume. CMD_AUTOTUNE_* is
 * never sent to STM32.
 */
#include "autotune_manager.h"

#include <string.h>

namespace dome {
namespace autotune_manager {

namespace {
Status s_status{};
Result s_result{};
bool s_initialized = false;

void set_error(uint8_t *err, uint8_t value)
{
    if (err != nullptr) {
        *err = value;
    }
}
}

bool init()
{
    memset(&s_status, 0, sizeof(s_status));
    memset(&s_result, 0, sizeof(s_result));
    s_status.loop = 0xFFu;
    s_status.state = AT_STATE_IDLE;
    s_result.loop = 0xFFu;
    s_result.valid = AUTOTUNE_RESULT_NONE;
    s_initialized = true;
    return true;
}

bool start(const Config *cfg, uint8_t *err)
{
    if (!s_initialized) {
        (void)init();
    }
    if (cfg == nullptr || cfg->loop >= PID_LOOP_MAX) {
        set_error(err, ERR_PARAM_OUT_OF_RANGE);
        return false;
    }

    /* No algorithm has been selected for phase 1. */
    set_error(err, ERR_UNSUPPORTED_VERSION);
    return false;
}

bool stop(uint8_t *err)
{
    set_error(err, ERR_UNSUPPORTED_VERSION);
    return false;
}

bool pause(uint8_t *err)
{
    set_error(err, ERR_UNSUPPORTED_VERSION);
    return false;
}

bool resume(uint8_t *err)
{
    set_error(err, ERR_UNSUPPORTED_VERSION);
    return false;
}

bool get_status(Status *out)
{
    if (out == nullptr) {
        return false;
    }
    *out = s_status;
    return true;
}

bool get_result(Result *out)
{
    if (out == nullptr) {
        return false;
    }
    *out = s_result;
    return true;
}

bool apply(uint8_t loop, uint8_t *err)
{
    if (loop >= PID_LOOP_MAX) {
        set_error(err, ERR_PARAM_OUT_OF_RANGE);
        return false;
    }
    set_error(err, ERR_UNSUPPORTED_VERSION);
    return false;
}

bool busy()
{
    return false;
}

} // namespace autotune_manager
} // namespace dome
