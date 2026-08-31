/**
 * @file    parameter_storage.h
 * @brief   STM32F103C8T6 内部 Flash 参数掉电保存 (A/B 双页 + 磨损平均)
 *
 * 存储布局 (F103C8: 64KB Flash, 页大小 1KB):
 *   程序区:      0x08000000 .. 0x0800F7FF  (62KB, 必须保证不越界, 见文档)
 *   槽位 B:      0x0800F800 (page 62)
 *   槽位 A:      0x0800FC00 (page 63)
 *
 * 每个槽位存放 PID_LOOP_MAX 份 pid_parameter_record_t (每个回路一份),
 * 三份记录使用相同 sequence。上电/加载时选择 sequence 最大且 CRC 正确的槽位;
 * 保存时写入另一个槽位 (A/B 交替 = 磨损平均, 擦写次数减半)。
 * 内容无变化时跳过擦写, 进一步降低磨损。
 */
#ifndef PARAMETER_STORAGE_H
#define PARAMETER_STORAGE_H

#include <stdint.h>
#include "protocol_def.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================= 参数记录 (与协议文档规定完全一致) ================= */
typedef struct
{
    uint32_t magic;             /* 魔术字 PARAM_RECORD_MAGIC */
    uint16_t version;           /* 结构版本 PARAM_RECORD_VERSION */
    uint16_t length;            /* 本结构体长度, 固定 60 */
    uint32_t sequence;          /* 保存序号, 每次保存 +1 */
    uint32_t crc32;             /* 从 offset 16 (本字段结束) 到结构体末尾的 CRC32 */

    float kp;
    float ki;
    float kd;
    float target;
    float output_min;
    float output_max;
    float integral_limit;
    float sample_time;          /* 秒 */
    float deadband;
    float filter_coefficient;   /* 0..0.99, 一阶低通系数 */

    uint8_t control_direction;  /* PID_DIR_* */
    uint8_t pid_mode;           /* PID_MODE_* */
    uint8_t reserved[2];
} pid_parameter_record_t;       /* 60 字节, 4 字节对齐无填充 */

#define PARAM_RECORD_MAGIC      0xD00DE001u
#define PARAM_RECORD_VERSION    0x0001u
#define PARAM_RECORD_SIZE       ((uint16_t)sizeof(pid_parameter_record_t))

/* 保存结果原因 (用于屏幕/通信上报) */
#define PS_OK                   0x00u
#define PS_ERR_FLASH            0x01u  /* 擦写失败 */
#define PS_ERR_NO_VALID_SLOT    0x02u  /* 无有效槽位(已用默认值) */
#define PS_ERR_CRC              0x03u
#define PS_ERR_MAGIC            0x04u
#define PS_ERR_VERSION          0x05u
#define PS_ERR_LENGTH           0x06u
#define PS_ERR_RANGE            0x07u  /* 参数范围非法, 已用默认值 */
#define PS_ERR_UNCHANGED        0x08u  /* 内容无变化, 未擦写 (非错误) */

/* ================= API ================= */

/** 上电初始化: 读取 A/B 槽位, 校验并装载; 无有效数据时装载默认参数 */
void parameter_storage_init(void);

/** 读取 RAM 参数映像 (loop_id: 0..PID_LOOP_MAX-1), 失败返回 0 */
uint8_t parameter_storage_get(uint8_t loop_id, pid_parameter_record_t *rec);

/** 写入 RAM 参数映像 (不写 Flash), 范围检查由调用方/App层负责 */
void parameter_storage_set(uint8_t loop_id, const pid_parameter_record_t *rec);

/**
 * @brief 保存全部回路参数到 Flash (写入非当前槽位, sequence+1)
 * @return PS_OK / PS_ERR_FLASH / PS_ERR_UNCHANGED(未擦写)
 * @note  耗时约 20~40ms (页擦除), 调用期间控制任务暂停输出更新
 */
uint8_t parameter_storage_save(void);

/** 从 Flash 重新加载到 RAM 映像 (0x40 组 LOAD 命令) */
uint8_t parameter_storage_load(void);

/** 擦除两个槽位 (RAM 映像不变) */
uint8_t parameter_storage_erase(void);

/** 校验当前活动槽位 CRC; 1=有效 0=无效 */
uint8_t parameter_storage_verify(void);

/** 恢复 RAM 映像为默认参数 (不写 Flash, 由上层决定是否 SAVE) */
void parameter_storage_set_defaults(void);

/* 状态查询 */
uint8_t  parameter_storage_is_valid(void);   /* 当前是否有有效 Flash 参数 */
uint32_t parameter_storage_sequence(void);   /* 当前活动记录序号 */
uint8_t  parameter_storage_last_error(void); /* init/load 时的失败原因 */

#ifdef __cplusplus
}
#endif

#endif /* PARAMETER_STORAGE_H */
