/**
 * @file    bsp_flash.h
 * @brief   STM32F103 内部 Flash 页擦除/字编程/读取 BSP
 */
#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_FLASH_PAGE_SIZE   1024u  /* F103C8 中容量页大小 */
#define BSP_FLASH_PAGE_COUNT  64u    /* 64KB */
#define BSP_FLASH_BASE_ADDR   0x08000000u

/**
 * @brief  擦除一页
 * @param  page 页号 (0..63)
 * @return 0=成功, 1=失败 (参数非法或 HAL 错误)
 */
uint8_t bsp_flash_erase_page(uint32_t page);

/**
 * @brief  按字 (32bit) 编程写入, len 不满 4 字节时补 0xFF 对齐
 * @param  addr 目标地址 (必须 4 字节对齐, 且位于空白页内)
 * @return 0=成功, 1=失败
 */
uint8_t bsp_flash_program(uint32_t addr, const uint8_t *data, uint16_t len);

/** 读取任意长度 (直接内存映射读) */
void bsp_flash_read(uint32_t addr, uint8_t *out, uint16_t len);

#ifdef __cplusplus
}
#endif

#endif /* BSP_FLASH_H */
