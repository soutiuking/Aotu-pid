/**
 * @file    control_task.h
 * @brief   控制任务: PID 回路运行时状态机、周期调度、通信看门狗与安全逻辑
 */
#ifndef CONTROL_TASK_H
#define CONTROL_TASK_H

#include <stdint.h>
#include "pid_controller.h"
#include "pid_parameter.h"
#include "protocol_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 单回路运行时 */
typedef struct
{
    pid_params_t params;   /* 可编辑参数映像 (与协议同步) */
    pid_t ctrl;            /* 控制器 */
    uint8_t state;         /* PID_STATE_* */
    uint8_t fault;         /* PID_FAULT_* 位掩码 */
    float feedback;        /* 最近反馈值 */
    float output;          /* 最近输出值 */
    uint32_t run_ms;       /* 累计运行时间 ms */
    uint32_t acc_ms;       /* 采样周期累加器 */
} pid_loop_t;

/** 上电初始化 (含参数装载) */
void control_task_init(void);

/** 主循环轮询: 节拍调度 PID/对象仿真/通信看门狗 */
void control_task_poll(uint32_t now_ms);

/** 回路运行时访问 (id: 0..PID_LOOP_MAX-1), 越界返回 0 */
pid_loop_t *control_task_loop(uint8_t id);

/* ---- 控制命令 (返回 ERR_*) ---- */
uint8_t control_task_start(uint8_t id);
uint8_t control_task_stop(uint8_t id);
uint8_t control_task_pause(uint8_t id);
uint8_t control_task_resume(uint8_t id);
uint8_t control_task_set_target(uint8_t id, float target);
uint8_t control_task_set_output(uint8_t id, float output);
uint8_t control_task_set_mode(uint8_t id, uint8_t mode);

/** 将 RAM 参数下发到运行中的控制器 (参数写/加载/恢复默认后调用) */
void control_task_sync_params(uint8_t id);

/** 通信看门狗喂狗 (每收到有效帧调用) */
void control_task_comm_alive(uint32_t now_ms);

#ifdef __cplusplus
}
#endif

#endif /* CONTROL_TASK_H */
