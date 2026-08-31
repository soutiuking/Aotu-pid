/**
 * @file    control_task.c
 * @brief   控制任务: PID 回路运行时状态机、周期调度、通信看门狗与安全逻辑
 */
#include "control_task.h"
#include "app_config.h"
#include "parameter_storage.h"
#include "device_state.h"
#include "plant_sim.h"
#include "protocol_codec.h"

#include <string.h>

static pid_loop_t g_loops[PID_LOOP_MAX];
static uint8_t g_sched_inited = 0u;

/* ---------------- 内部 ---------------- */

static void loop_load_params(pid_loop_t *L)
{
    pid_apply_params(&L->ctrl,
                     L->params.kp, L->params.ki, L->params.kd,
                     L->params.output_min, L->params.output_max,
                     L->params.integral_limit, L->params.sample_time,
                     L->params.deadband, L->params.filter_coefficient,
                     L->params.control_direction);
    L->ctrl.target = L->params.target;
}

static void loop_force_safe(pid_loop_t *L, uint8_t fault_bit)
{
    if (L->state == PID_STATE_RUNNING || L->state == PID_STATE_MANUAL ||
        L->state == PID_STATE_PAUSED) {
        L->state = PID_STATE_SAFE;
    }
    L->fault |= fault_bit;
    L->output = 0.0f;
    pid_set_manual_output(&L->ctrl, 0.0f);
}

static uint8_t loop_feedback_abnormal(const pid_loop_t *L)
{
    if (!codec_f32_is_finite(L->feedback)) {
        return 1u;
    }
    if (L->feedback < CFG_FEEDBACK_SAFE_MIN || L->feedback > CFG_FEEDBACK_SAFE_MAX) {
        return 1u;
    }
    return 0u;
}

/* ---------------- 生命周期 ---------------- */

void control_task_init(void)
{
    uint8_t i;
    pid_loop_t *L;

    device_state_init();
    (void)memset(g_loops, 0, sizeof(g_loops));
    parameter_storage_init();
    pid_parameter_init();

    for (i = 0; i < PID_LOOP_MAX; i++) {
        L = &g_loops[i];
        (void)pid_parameter_get(i, &L->params);
        pid_init(&L->ctrl);
        loop_load_params(L);
        L->state = PID_STATE_IDLE;
        L->fault = PID_FAULT_NONE;
        L->feedback = plant_sim_step(i, 0.0f, 0.01f);
        L->output = 0.0f;
    }
    plant_sim_reset();
    g_sched_inited = 1u;
}

pid_loop_t *control_task_loop(uint8_t id)
{
    return (id < PID_LOOP_MAX) ? &g_loops[id] : (pid_loop_t *)0;
}

/* ---------------- 控制命令 ---------------- */

uint8_t control_task_start(uint8_t id)
{
    pid_loop_t *L = control_task_loop(id);

    if (L == 0) {
        return ERR_PARAM_OUT_OF_RANGE;
    }
    /* 显式启动解除安全锁 */
    device_state_ctx()->safe_latched = 0u;
    L->fault = PID_FAULT_NONE;
    L->run_ms = 0u;
    if (L->params.pid_mode == PID_MODE_AUTO) {
        L->state = PID_STATE_RUNNING;
        pid_bumpless_transfer(&L->ctrl);
    } else {
        L->state = PID_STATE_MANUAL;
        pid_set_manual_output(&L->ctrl, L->output);
    }
    return ERR_OK;
}

uint8_t control_task_stop(uint8_t id)
{
    pid_loop_t *L = control_task_loop(id);

    if (L == 0) {
        return ERR_PARAM_OUT_OF_RANGE;
    }
    L->state = PID_STATE_IDLE;
    L->output = 0.0f;
    pid_set_manual_output(&L->ctrl, 0.0f);
    return ERR_OK;
}

uint8_t control_task_pause(uint8_t id)
{
    pid_loop_t *L = control_task_loop(id);

    if (L == 0) {
        return ERR_PARAM_OUT_OF_RANGE;
    }
    if (L->state != PID_STATE_RUNNING) {
        return ERR_PID_NOT_RUNNING;
    }
    L->state = PID_STATE_PAUSED;
    pid_pause(&L->ctrl);
    return ERR_OK;
}

uint8_t control_task_resume(uint8_t id)
{
    pid_loop_t *L = control_task_loop(id);

    if (L == 0) {
        return ERR_PARAM_OUT_OF_RANGE;
    }
    if (L->state != PID_STATE_PAUSED) {
        return ERR_PID_NOT_RUNNING;
    }
    device_state_ctx()->safe_latched = 0u;
    if (L->params.pid_mode == PID_MODE_AUTO) {
        L->state = PID_STATE_RUNNING;
    } else {
        L->state = PID_STATE_MANUAL;
    }
    return ERR_OK;
}

uint8_t control_task_set_target(uint8_t id, float target)
{
    pid_loop_t *L = control_task_loop(id);

    if (L == 0 || !codec_f32_is_finite(target) ||
        target < -1000.0f || target > 1000.0f) {
        return ERR_PARAM_OUT_OF_RANGE;
    }
    L->params.target = target;
    L->ctrl.target = target;
    return ERR_OK;
}

uint8_t control_task_set_output(uint8_t id, float output)
{
    pid_loop_t *L = control_task_loop(id);

    if (L == 0 || !codec_f32_is_finite(output)) {
        return ERR_PARAM_OUT_OF_RANGE;
    }
    L->params.pid_mode = PID_MODE_MANUAL;
    L->state = PID_STATE_MANUAL;
    pid_set_manual_output(&L->ctrl, output);
    L->output = L->ctrl.output;
    return ERR_OK;
}

uint8_t control_task_set_mode(uint8_t id, uint8_t mode)
{
    pid_loop_t *L = control_task_loop(id);

    if (L == 0 || mode > 1u) {
        return ERR_PARAM_OUT_OF_RANGE;
    }
    L->params.pid_mode = mode;
    L->ctrl.pid_mode = mode;
    if (mode == PID_MODE_AUTO) {
        if (L->state == PID_STATE_IDLE || L->state == PID_STATE_MANUAL ||
            L->state == PID_STATE_PAUSED) {
            L->state = PID_STATE_RUNNING;
            pid_bumpless_transfer(&L->ctrl);
        }
    } else {
        if (L->state == PID_STATE_RUNNING || L->state == PID_STATE_PAUSED) {
            L->state = PID_STATE_MANUAL;
        }
    }
    return ERR_OK;
}

void control_task_sync_params(uint8_t id)
{
    pid_loop_t *L = control_task_loop(id);

    if (L == 0) {
        return;
    }
    (void)pid_parameter_get(id, &L->params);
    loop_load_params(L);
    if (L->state == PID_STATE_RUNNING) {
        pid_bumpless_transfer(&L->ctrl);
    }
}

void control_task_comm_alive(uint32_t now_ms)
{
    device_state_ctx()->comm_last_rx_ms = now_ms;
    device_state_ctx()->comm_link_up = 1u;
}

/* ---------------- 周期调度 ---------------- */

static void control_tick_10ms(void)
{
    uint8_t i;
    pid_loop_t *L;
    float dt;
    device_state_ctx_t *dev = device_state_ctx();

    for (i = 0; i < PID_LOOP_MAX; i++) {
        L = &g_loops[i];
        dt = L->params.sample_time;
        L->acc_ms += CFG_TICK_MS;

        /* 对象仿真每拍推进 (连续对象, 控制按采样周期离散) */
        L->feedback = plant_sim_step(i, L->output, (float)CFG_TICK_MS / 1000.0f);


        /* 反馈异常保护: 关闭输出并进入故障态 */
        if (loop_feedback_abnormal(L) != 0u &&
            (L->state == PID_STATE_RUNNING || L->state == PID_STATE_MANUAL)) {
            loop_force_safe(L, PID_FAULT_FEEDBACK_ABNORMAL);
            device_state_report_error(ERR_SAFETY_LIMIT, i);
        }

        if (L->state == PID_STATE_RUNNING) {
            L->run_ms += CFG_TICK_MS;
            if (L->acc_ms >= (uint32_t)(dt * 1000.0f)) {
                L->acc_ms = 0u;
                L->ctrl.pid_mode = PID_MODE_AUTO;
                L->output = pid_compute(&L->ctrl, L->feedback);
                /* 硬限幅兜底 */
                if (L->output > CFG_OUTPUT_HARD_LIMIT) {
                    L->output = CFG_OUTPUT_HARD_LIMIT;
                }
                if (L->output < -CFG_OUTPUT_HARD_LIMIT) {
                    L->output = -CFG_OUTPUT_HARD_LIMIT;
                }
                /* 长时间饱和提示 (不强制动作) */
                if (L->output >= L->params.output_max - 0.01f ||
                    L->output <= L->params.output_min + 0.01f) {
                    L->fault |= PID_FAULT_OUTPUT_LIMIT;
                }
            }
        } else if (L->state == PID_STATE_MANUAL) {
            L->run_ms += CFG_TICK_MS;
            if (L->acc_ms >= (uint32_t)(dt * 1000.0f)) {
                L->acc_ms = 0u;
                L->output = pid_get_output(&L->ctrl); /* 保持手动输出 */
            }
        } else if (L->state == PID_STATE_PAUSED || L->state == PID_STATE_SAFE ||
                   L->state == PID_STATE_FAULT || L->state == PID_STATE_IDLE) {
            if (L->state != PID_STATE_PAUSED) {
                L->output = 0.0f; /* 非运行态输出关闭 */
            }
            L->acc_ms = 0u;
        }
    }

    /* 设备状态派生 */
    if (dev->safe_latched != 0u) {
        dev->state = DEV_STATE_SAFE;
    } else {
        uint8_t any_run = 0u;
        for (i = 0; i < PID_LOOP_MAX; i++) {
            if (g_loops[i].state == PID_STATE_RUNNING ||
                g_loops[i].state == PID_STATE_MANUAL) {
                any_run = 1u;
            }
        }
        dev->state = any_run ? DEV_STATE_RUNNING : DEV_STATE_READY;
    }
}

void control_task_poll(uint32_t now_ms)
{
    static uint32_t last_ms = 0u;
    uint32_t elapsed;
    device_state_ctx_t *dev = device_state_ctx();

    if (g_sched_inited == 0u) {
        return;
    }

    dev->uptime_ms = now_ms;

    /* 10ms 节拍 (补偿主循环耗时抖动) */
    if (last_ms == 0u) {
        last_ms = now_ms;
    }
    elapsed = now_ms - last_ms;
    if (elapsed >= CFG_TICK_MS) {
        last_ms = now_ms;
        control_tick_10ms();
    }

    /* 通信看门狗: 收到过帧且超时 -> 全部回路进入安全状态 */
    if (dev->comm_link_up != 0u && dev->safe_latched == 0u &&
        (now_ms - dev->comm_last_rx_ms) > CFG_COMM_TIMEOUT_MS) {
        uint8_t i;
        for (i = 0; i < PID_LOOP_MAX; i++) {
            loop_force_safe(&g_loops[i], PID_FAULT_COMM_TIMEOUT);
        }
        dev->safe_latched = 1u;
        dev->state = DEV_STATE_SAFE;
        device_state_report_error(ERR_FRAME_TIMEOUT, 0xFFu);
    }
}
