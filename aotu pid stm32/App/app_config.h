/**
 * @file    app_config.h
 * @brief   STM32 端应用集中配置 (关键参数不散落硬编码)
 */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

/* ---------------- 控制与调度 ---------------- */
#define CFG_TICK_MS             10u     /* 主调度节拍 */
#define CFG_DISPLAY_REFRESH_MS  200u    /* 屏幕刷新周期 */
#define CFG_DISPLAY_CYCLE_MS    5000u   /* 自动轮显切换周期 */

/* ---------------- 通信看门狗 ---------------- */
#define CFG_COMM_TIMEOUT_MS     10000u  /* 有效帧间隔超过此值进入安全状态 */

/* ---------------- 被控对象 ---------------- */
#define CFG_PLANT_SIMULATION    1   /* 1=一阶对象仿真, 0=真实硬件(需实现 plant_sim.c 钩子) */
#define CFG_PLANT_GAIN          1.0f    /* 对象增益 K */
#define CFG_PLANT_TAU           0.8f    /* 对象时间常数 (s) */
#define CFG_PLANT_NOISE         0.05f   /* 测量噪声幅度 */

/* ---------------- 安全范围 (控制运行保护) ---------------- */
#define CFG_FEEDBACK_SAFE_MIN   -150.0f
#define CFG_FEEDBACK_SAFE_MAX   150.0f
#define CFG_OUTPUT_HARD_LIMIT   100.0f

#endif /* APP_CONFIG_H */
