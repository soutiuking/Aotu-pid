/**
 * @file    pid_controller.h
 * @brief   位置式 PID 控制器 (微分先行/输入滤波/积分限幅/抗饱和/死区/无扰切换)
 */
#ifndef PID_CONTROLLER_H
#define PID_CONTROLLER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    /* ---- 可调参数 (与协议参数一一对应) ---- */
    float kp;
    float ki;
    float kd;
    float target;
    float output_min;
    float output_max;
    float integral_limit;
    float sample_time;       /* 秒, 计算周期 */
    float deadband;          /* 死区 (与反馈同单位) */
    float filter_coefficient;/* 测量值一阶低通系数 0~0.99, 0=不滤波 */
    uint8_t control_direction; /* PID_DIR_* */
    uint8_t pid_mode;          /* PID_MODE_* */

    /* ---- 内部状态 ---- */
    float integral;          /* 积分累计 */
    float filtered_meas;     /* 滤波后的测量值 */
    float prev_meas;         /* 上一拍原始测量值 (微分先行) */
    float output;            /* 当前输出 */
    uint8_t initialized;     /* 首拍标志 */
} pid_t;

/** 初始化/复位内部状态 (参数清零, 输出 0) */
void pid_init(pid_t *p);

/** 用参数集填充控制器 (不复位运行状态, 支持运行中安全更新) */
void pid_apply_params(pid_t *p, float kp, float ki, float kd,
                      float output_min, float output_max,
                      float integral_limit, float sample_time,
                      float deadband, float filter_coefficient,
                      uint8_t direction);

/**
 * @brief 执行一拍计算 (自动模式)
 * @param meas 原始测量值
 * @return 输出 (已限幅)
 * @note  meas 为 NaN/Inf 时进入保护: 保持当前输出并计饱和
 */
float pid_compute(pid_t *p, float meas);

/** 手动模式: 直接设置输出 (限幅), 用于手动模式与无扰切换 */
void pid_set_manual_output(pid_t *p, float out);

/** 暂停: 保持当前输出, 冻结积分 */
void pid_pause(pid_t *p);

/** 输出值读取 */
float pid_get_output(const pid_t *p);

/**
 * @brief 手动 -> 自动 无扰切换准备:
 *        反算积分项使 PID 输出从当前输出连续起步
 */
void pid_bumpless_transfer(pid_t *p);

#ifdef __cplusplus
}
#endif

#endif /* PID_CONTROLLER_H */
