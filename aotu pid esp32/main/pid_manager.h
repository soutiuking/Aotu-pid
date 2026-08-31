/**
 * @file    pid_manager.h
 * @brief   PID 参数/控制/Flash 命令封装 (供 Web 层调用的类型化接口)
 */
#pragma once

#include <cstdint>
#include <cstddef>
#include <stdbool.h>
#include "protocol_def.h"
#include "protocol_client.h"

namespace dome {
namespace pid_manager {

#define PID_PARAMS_WIRE_SIZE  44u  /* 参数集线格式大小, 与 STM32 pid_parameter.h 一致 */

/* 参数集合 (线格式 44 字节) */
struct Params {
    float kp, ki, kd, target, out_min, out_max, integ_lim, sample_time, deadband, filter;
    uint8_t dir, mode;
    float feedback, output;   /* 读时附带 */
    uint8_t state;
};

struct Ranges {
    float minv[PIDR_COUNT];
    float maxv[PIDR_COUNT];
    float step[PIDR_COUNT];
};

struct Runtime {
    float target, feedback, output, err, kp, ki, kd;
    uint8_t state, mode, fault;
    uint32_t run_ms;
};

/* ---- 参数读写 ---- */
bool read_params(uint8_t loop, Params *out, uint8_t *err);
bool write_params(uint8_t loop, const Params *in, uint8_t *err, uint8_t *bad_idx);
bool save_flash(uint8_t *err, uint8_t *detail);      /* 返回 false 时 err=ERR_* */
bool load_flash(uint8_t *err, uint8_t *detail);
bool default_params(uint8_t *err);
bool read_ranges(Ranges *out, uint8_t *err);
bool read_runtime(uint8_t loop, Runtime *out, uint8_t *err);

/* ---- 运行控制 ---- */
bool control(uint8_t cmd, uint8_t loop, uint8_t *err);   /* START/STOP/PAUSE/RESUME */
bool set_target(uint8_t loop, float v, uint8_t *err);
bool set_output(uint8_t loop, float v, uint8_t *err);
bool set_mode(uint8_t loop, uint8_t mode, uint8_t *err);

/* ---- Flash 附加 ---- */
bool flash_erase(uint8_t *err);
bool flash_verify(uint8_t *ok, uint32_t *seq, uint8_t *err);


/* ---- 系统 ---- */
bool device_reset(uint8_t *err);

} // namespace pid_manager
} // namespace dome
