/**
 * @file    bsp_flash.c
 * @brief   STM32F103 内部 Flash 页擦除/字编程/读取 BSP
 * @note    仅允许在主循环上下文调用 (擦写期间 CPU 取指停顿, 中断会延迟);
 *          禁止在中断中调用。
 */
#include "bsp_flash.h"
#include "stm32f1xx_hal.h"

#include <string.h>

uint8_t bsp_flash_erase_page(uint32_t page)
{
    FLASH_EraseInitTypeDef erase;
    uint32_t page_err = 0u;
    HAL_StatusTypeDef st;

    if (page >= BSP_FLASH_PAGE_COUNT) {
        return 1u;
    }

    (void)memset(&erase, 0, sizeof(erase));
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = BSP_FLASH_BASE_ADDR + page * BSP_FLASH_PAGE_SIZE;
    erase.NbPages = 1u;

    HAL_FLASH_Unlock();
    st = HAL_FLASHEx_Erase(&erase, &page_err);
    HAL_FLASH_Lock();

    return (st == HAL_OK) ? 0u : 1u;
}

uint8_t bsp_flash_program(uint32_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t words;
    uint16_t i;
    uint32_t word;
    uint8_t j;
    uint8_t tail;
    const uint8_t *p;

    if (data == 0 || len == 0u) {
        return 1u;
    }
    if ((addr & 3u) != 0u) {
        return 1u;
    }
    if (addr < BSP_FLASH_BASE_ADDR ||
        (addr + len) > (BSP_FLASH_BASE_ADDR + BSP_FLASH_PAGE_COUNT * BSP_FLASH_PAGE_SIZE)) {
        return 1u;
    }

    words = (uint16_t)((len + 3u) / 4u);
    p = data;

    HAL_FLASH_Unlock();
    for (i = 0; i < words; i++) {
        word = 0xFFFFFFFFu;
        tail = (uint8_t)(len - i * 4u);
        if (tail > 4u) {
            tail = 4u;
        }
        for (j = 0; j < tail; j++) {
            word &= ~((uint32_t)0xFFu << (8u * j));
            word |= (uint32_t)p[i * 4u + j] << (8u * j);
        }
        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr + (uint32_t)i * 4u, word) != HAL_OK) {
            HAL_FLASH_Lock();
            return 1u;
        }
    }
    HAL_FLASH_Lock();
    return 0u;
}

void bsp_flash_read(uint32_t addr, uint8_t *out, uint16_t len)
{
    if (out == 0 || len == 0u) {
        return;
    }
    (void)memcpy(out, (const void *)addr, len);
}
