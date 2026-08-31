/**
 * @file    plant_sim.h
 * @brief   被控对象接口 (一期: 一阶惯性对象仿真, 按 PID 回路分通道;
 *          真实硬件时替换此模块实现)
 */
#ifndef PLANT_SIM_H
#define PLANT_SIM_H

#include <stdint.h>
#include "protocol_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 周期调用: 通道 ch (loop_id) 输入执行器输出 u, 返回传感器反馈值 */
float plant_sim_step(uint8_t ch, float u, float dt);

/** 复位全部通道 */
void plant_sim_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* PLANT_SIM_H */
