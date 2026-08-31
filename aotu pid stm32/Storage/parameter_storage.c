/**
 * @file    parameter_storage.c
 * @brief   STM32F103C8T6 内部 Flash 参数掉电保存 (A/B 双页 + 磨损平均)
 */
#include "parameter_storage.h"
#include "bsp_flash.h"
#include "protocol_crc.h"
#include "protocol_codec.h"

#include <string.h>

/* ---------------- Flash 布局 ---------------- */
#define PS_FLASH_BASE        0x08000000u
#define PS_PAGE_SIZE         1024u                  /* F103C8 中容量: 1KB/页 */
#define PS_SLOT_A_PAGE       63u                    /* 0x0800FC00 */
#define PS_SLOT_B_PAGE       62u                    /* 0x0800F800 */
#define PS_SLOT_A_ADDR       (PS_FLASH_BASE + (PS_SLOT_A_PAGE * PS_PAGE_SIZE))
#define PS_SLOT_B_ADDR       (PS_FLASH_BASE + (PS_SLOT_B_PAGE * PS_PAGE_SIZE))

/* RAM 参数映像 (仅收到保存命令才写 Flash) */
static pid_parameter_record_t ps_ram[PID_LOOP_MAX];
static uint8_t  ps_valid = 0u;      /* Flash 中存在有效参数 */
static uint32_t ps_sequence = 0u;
static uint8_t  ps_slot = 0u;       /* 0=A, 1=B, 0xFF=无 */
static uint8_t  ps_last_err = PS_OK;

/* ---------------- 内部函数 ---------------- */

/* 校验单条记录: 魔术字/版本/长度/CRC32/参数范围 */
static uint8_t ps_validate_record(const pid_parameter_record_t *r)
{
    if (r->magic != PARAM_RECORD_MAGIC) {
        return PS_ERR_MAGIC;
    }
    if (r->version != PARAM_RECORD_VERSION) {
        return PS_ERR_VERSION;
    }
    if (r->length != PARAM_RECORD_SIZE) {
        return PS_ERR_LENGTH;
    }
    if (protocol_crc32((const uint8_t *)r + 16u,
                       (size_t)PARAM_RECORD_SIZE - 16u) != r->crc32) {
        return PS_ERR_CRC;
    }
    return PS_OK;
}

/* 参数范围粗检 (详细范围检查在 App 层) */
static uint8_t ps_range_ok(const pid_parameter_record_t *r)
{
    if (!codec_f32_is_finite(r->kp) || !codec_f32_is_finite(r->ki) ||
        !codec_f32_is_finite(r->kd) || !codec_f32_is_finite(r->target) ||
        !codec_f32_is_finite(r->sample_time)) {
        return 0u;
    }
    if (r->output_min >= r->output_max) {
        return 0u;
    }
    if (r->sample_time <= 0.0f) {
        return 0u;
    }
    if (r->control_direction > 1u || r->pid_mode > 1u) {
        return 0u;
    }
    return 1u;
}

/* 读取一个槽位的全部记录, 全部有效返回 PS_OK, 否则返回原因 */
static uint8_t ps_read_slot(uint32_t addr, pid_parameter_record_t out[PID_LOOP_MAX],
                            uint32_t *seq)
{
    uint8_t i;
    uint8_t rc;

    for (i = 0; i < PID_LOOP_MAX; i++) {
        bsp_flash_read(addr + (uint32_t)i * PARAM_RECORD_SIZE,
                       (uint8_t *)&out[i], PARAM_RECORD_SIZE);
        rc = ps_validate_record(&out[i]);
        if (rc != PS_OK) {
            return rc;
        }
        if (!ps_range_ok(&out[i])) {
            return PS_ERR_RANGE;
        }
        if (i > 0u && out[i].sequence != out[0].sequence) {
            return PS_ERR_CRC; /* 三份记录序号不一致, 视为写坏 */
        }
    }
    *seq = out[0].sequence;
    return PS_OK;
}

/* 为默认参数填充一条记录 (不含 sequence/crc, 由调用方补) */
static void ps_fill_default(pid_parameter_record_t *r)
{
    (void)memset(r, 0, sizeof(*r));
    r->kp = 2.0f;
    r->ki = 0.5f;
    r->kd = 0.1f;
    r->target = 0.0f;
    r->output_min = -100.0f;
    r->output_max = 100.0f;
    r->integral_limit = 50.0f;
    r->sample_time = 0.1f;
    r->deadband = 0.0f;
    r->filter_coefficient = 0.2f;
    r->control_direction = PID_DIR_DIRECT;
    r->pid_mode = PID_MODE_MANUAL;
}

/* 重算记录头 + CRC */
static void ps_seal_record(pid_parameter_record_t *r, uint32_t seq)
{
    r->magic = PARAM_RECORD_MAGIC;
    r->version = PARAM_RECORD_VERSION;
    r->length = PARAM_RECORD_SIZE;
    r->sequence = seq;
    r->crc32 = protocol_crc32((const uint8_t *)r + 16u,
                              (size_t)PARAM_RECORD_SIZE - 16u);
}

static uint8_t ps_same_payload(const pid_parameter_record_t *a,
                               const pid_parameter_record_t *b)
{
    return (memcmp((const uint8_t *)a + 16u, (const uint8_t *)b + 16u,
                   (size_t)PARAM_RECORD_SIZE - 16u) == 0) ? 1u : 0u;
}

/* ---------------- 公开 API ---------------- */

void parameter_storage_init(void)
{
    pid_parameter_record_t a[PID_LOOP_MAX];
    pid_parameter_record_t b[PID_LOOP_MAX];
    uint32_t seq_a = 0u, seq_b = 0u;
    uint8_t ok_a, ok_b;
    uint8_t i;

    ps_valid = 0u;
    ps_slot = 0xFFu;
    ps_sequence = 0u;

    ok_a = (ps_read_slot(PS_SLOT_A_ADDR, a, &seq_a) == PS_OK) ? 1u : 0u;
    ok_b = (ps_read_slot(PS_SLOT_B_ADDR, b, &seq_b) == PS_OK) ? 1u : 0u;

    if (ok_a && (!ok_b || seq_a >= seq_b)) {
        (void)memcpy(ps_ram, a, sizeof(ps_ram));
        ps_valid = 1u;
        ps_slot = 0u;
        ps_sequence = seq_a;
        ps_last_err = PS_OK;
    } else if (ok_b) {
        (void)memcpy(ps_ram, b, sizeof(ps_ram));
        ps_valid = 1u;
        ps_slot = 1u;
        ps_sequence = seq_b;
        ps_last_err = PS_OK;
    } else {
        /* 无有效参数: 装载默认值并记录原因 (两槽位全空属正常, 不算错误) */
        parameter_storage_set_defaults();
        ps_valid = 0u;
        ps_slot = 0xFFu;
        ps_last_err = PS_ERR_NO_VALID_SLOT;
    }

    for (i = 0; i < PID_LOOP_MAX; i++) {
        if (!ps_range_ok(&ps_ram[i])) {
            ps_fill_default(&ps_ram[i]);
            ps_last_err = PS_ERR_RANGE;
        }
    }
}

uint8_t parameter_storage_get(uint8_t loop_id, pid_parameter_record_t *rec)
{
    if (loop_id >= PID_LOOP_MAX || rec == 0) {
        return 0u;
    }
    *rec = ps_ram[loop_id];
    return 1u;
}

void parameter_storage_set(uint8_t loop_id, const pid_parameter_record_t *rec)
{
    if (loop_id < PID_LOOP_MAX && rec != 0) {
        ps_ram[loop_id] = *rec;
    }
}

uint8_t parameter_storage_save(void)
{
    pid_parameter_record_t image[PID_LOOP_MAX];
    pid_parameter_record_t verify[PID_LOOP_MAX];
    uint32_t target_addr;
    uint32_t active_addr;
    uint32_t seq_read = 0u;
    uint32_t new_sequence;
    uint8_t target_slot;
    uint8_t same = 1u;
    uint8_t i;

    for (i = 0u; i < PID_LOOP_MAX; i++) {
        if (ps_range_ok(&ps_ram[i]) == 0u) {
            ps_last_err = PS_ERR_RANGE;
            return PS_ERR_RANGE;
        }
    }

    /* Do not erase/program when the active slot already contains the same
     * parameter payload. Header fields, sequence and CRC are intentionally
     * excluded from this comparison. */
    if (ps_valid != 0u && (ps_slot == 0u || ps_slot == 1u)) {
        active_addr = (ps_slot == 0u) ? PS_SLOT_A_ADDR : PS_SLOT_B_ADDR;
        if (ps_read_slot(active_addr, verify, &seq_read) == PS_OK) {
            for (i = 0u; i < PID_LOOP_MAX; i++) {
                if (ps_same_payload(&verify[i], &ps_ram[i]) == 0u) {
                    same = 0u;
                    break;
                }
            }
            if (same != 0u) {
                (void)memcpy(ps_ram, verify, sizeof(ps_ram));
                ps_sequence = seq_read;
                ps_last_err = PS_OK;
                return PS_ERR_UNCHANGED;
            }
        }
    }

    if (ps_slot == 0u) {
        target_addr = PS_SLOT_B_ADDR;
        target_slot = 1u;
    } else {
        target_addr = PS_SLOT_A_ADDR;
        target_slot = 0u;
    }

    new_sequence = ps_sequence + 1u;
    if (new_sequence == 0u) {
        new_sequence = 1u;
    }
    for (i = 0u; i < PID_LOOP_MAX; i++) {
        image[i] = ps_ram[i];
        ps_seal_record(&image[i], new_sequence);
    }

    /* Recover cleanly when a previous save reached Flash but reset occurred
     * before the RAM bookkeeping was updated. */
    bsp_flash_read(target_addr, (uint8_t *)verify, (uint16_t)sizeof(verify));
    if (memcmp(verify, image, sizeof(image)) != 0) {
        if (bsp_flash_erase_page((target_slot == 0u) ? PS_SLOT_A_PAGE
                                                     : PS_SLOT_B_PAGE) != 0u) {
            ps_last_err = PS_ERR_FLASH;
            return PS_ERR_FLASH;
        }
        if (bsp_flash_program(target_addr, (const uint8_t *)image,
                              (uint16_t)sizeof(image)) != 0u) {
            ps_last_err = PS_ERR_FLASH;
            return PS_ERR_FLASH;
        }
    }

    /* Full readback: record CRC/range, common sequence and exact bytes must all
     * match before the new slot becomes active. */
    if (ps_read_slot(target_addr, verify, &seq_read) != PS_OK ||
        seq_read != new_sequence || memcmp(verify, image, sizeof(image)) != 0) {
        ps_last_err = PS_ERR_FLASH;
        return PS_ERR_FLASH;
    }

    (void)memcpy(ps_ram, verify, sizeof(ps_ram));
    ps_valid = 1u;
    ps_slot = target_slot;
    ps_sequence = new_sequence;
    ps_last_err = PS_OK;
    return PS_OK;
}

uint8_t parameter_storage_load(void)
{
    pid_parameter_record_t a[PID_LOOP_MAX];
    pid_parameter_record_t b[PID_LOOP_MAX];
    uint32_t seq_a = 0u, seq_b = 0u;

    if (ps_read_slot(PS_SLOT_A_ADDR, a, &seq_a) == PS_OK &&
        (ps_read_slot(PS_SLOT_B_ADDR, b, &seq_b) != PS_OK || seq_a >= seq_b)) {
        (void)memcpy(ps_ram, a, sizeof(ps_ram));
        ps_valid = 1u;
        ps_slot = 0u;
        ps_sequence = seq_a;
        ps_last_err = PS_OK;
        return PS_OK;
    }
    if (ps_read_slot(PS_SLOT_B_ADDR, b, &seq_b) == PS_OK) {
        (void)memcpy(ps_ram, b, sizeof(ps_ram));
        ps_valid = 1u;
        ps_slot = 1u;
        ps_sequence = seq_b;
        ps_last_err = PS_OK;
        return PS_OK;
    }
    ps_valid = 0u;
    ps_slot = 0xFFu;
    ps_last_err = PS_ERR_NO_VALID_SLOT;
    return PS_ERR_NO_VALID_SLOT;
}

uint8_t parameter_storage_erase(void)
{
    uint32_t first_a;
    uint32_t first_b;

    if (bsp_flash_erase_page(PS_SLOT_A_PAGE) != 0u ||
        bsp_flash_erase_page(PS_SLOT_B_PAGE) != 0u) {
        ps_last_err = PS_ERR_FLASH;
        return PS_ERR_FLASH;
    }

    bsp_flash_read(PS_SLOT_A_ADDR, (uint8_t *)&first_a, (uint16_t)sizeof(first_a));
    bsp_flash_read(PS_SLOT_B_ADDR, (uint8_t *)&first_b, (uint16_t)sizeof(first_b));
    if (first_a != 0xFFFFFFFFu || first_b != 0xFFFFFFFFu) {
        ps_last_err = PS_ERR_FLASH;
        return PS_ERR_FLASH;
    }

    ps_valid = 0u;
    ps_slot = 0xFFu;
    ps_sequence = 0u;
    ps_last_err = PS_OK;
    return PS_OK;
}

uint8_t parameter_storage_verify(void)
{
    pid_parameter_record_t tmp[PID_LOOP_MAX];
    uint32_t seq = 0u;
    uint32_t addr;

    if (ps_valid == 0u) {
        return 0u;
    }
    if (ps_slot == 0u) {
        addr = PS_SLOT_A_ADDR;
    } else if (ps_slot == 1u) {
        addr = PS_SLOT_B_ADDR;
    } else {
        return 0u;
    }
    if (ps_read_slot(addr, tmp, &seq) != PS_OK || seq != ps_sequence) {
        return 0u;
    }
    return 1u;
}

void parameter_storage_set_defaults(void)
{
    uint8_t i;
    for (i = 0; i < PID_LOOP_MAX; i++) {
        ps_fill_default(&ps_ram[i]);
    }
}

uint8_t  parameter_storage_is_valid(void)   { return ps_valid; }
uint32_t parameter_storage_sequence(void)   { return ps_sequence; }
uint8_t  parameter_storage_last_error(void) { return ps_last_err; }
