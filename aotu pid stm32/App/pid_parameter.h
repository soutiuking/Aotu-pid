/**
 * @file    pid_parameter.h
 * @brief   PID 参数表管理: 校验/默认值/线格式打包 与范围表
 */
#ifndef PID_PARAMETER_H
#define PID_PARAMETER_H

#include <stdint.h>
#include "protocol_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* PID 参数集合 (与 pid_parameter_record_t 的参数部分一致, 44 字节线格式) */
typedef struct
{
    float kp;
    float ki;
    float kd;
    float target;
    float output_min;
    float output_max;
    float integral_limit;
    float sample_time;
    float deadband;
    float filter_coefficient;
    uint8_t control_direction;
    uint8_t pid_mode;
    uint8_t reserved[2];
} pid_params_t;

#define PID_PARAMS_WIRE_SIZE  44u

/** 上电初始化: 从 Storage 装载全部回路参数映像 */
void pid_parameter_init(void);

/** 校验参数集合法性
 * @param bad_idx 返回越界参数的 PIDR_* 索引, 0xFF=组合规则错误
 * @return ERR_OK 或 ERR_PARAM_OUT_OF_RANGE
 */
uint8_t pid_parameter_validate(const pid_params_t *p, uint8_t *bad_idx);

/** 取某回路 RAM 参数 */
uint8_t pid_parameter_get(uint8_t loop_id, pid_params_t *p);

/** 写某回路 RAM 参数 (校验通过才写入; 不写 Flash) */
uint8_t pid_parameter_set(uint8_t loop_id, const pid_params_t *p);

/** 全部回路恢复默认 (RAM, 不写 Flash) */
void pid_parameter_set_defaults(void);

/** 将 Storage 的记录同步到 RAM 参数 (LOAD 后调用) */
void pid_parameter_sync_from_storage(void);

/** 参数范围表 [PIDR_COUNT][3] = {min, max, step} */
const float *pid_parameter_range_table(void);

/** 线格式打包/解包 (44 字节, 显式小端) */
void pid_params_pack(uint8_t *buf, const pid_params_t *p);
void pid_params_unpack(const uint8_t *buf, pid_params_t *p);

#ifdef __cplusplus
}
#endif

#endif /* PID_PARAMETER_H */
