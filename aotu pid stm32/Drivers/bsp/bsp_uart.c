/**
 * @file    bsp_uart.c
 * @brief   USART1 BSP: direct RXNE receive ring + DMA transmit lock
 */
#include "bsp_uart.h"
#include "usart.h"

#include <string.h>

/* The ISR owns rx_head; the main loop owns rx_tail. */
static uint8_t rx_ring[BSP_UART_RX_RING_SIZE];
static volatile uint16_t rx_head = 0u;
static volatile uint16_t rx_tail = 0u;

/* 0 = idle, 1 = DMA transmit in progress. */
static volatile uint8_t tx_busy = 0u;
static uint32_t tx_started_ms = 0u;
static uint8_t tx_buf[BSP_UART_TX_BUF_SIZE];

/* Diagnostics counters. */
static volatile uint32_t rx_overflow = 0u;
static volatile uint32_t rx_error = 0u;
static volatile uint32_t tx_error = 0u;

static void rx_push_isr(uint8_t byte)
{
    uint16_t next = (uint16_t)((rx_head + 1u) % BSP_UART_RX_RING_SIZE);

    if (next != rx_tail) {
        rx_ring[rx_head] = byte;
        rx_head = next;
    } else {
        rx_overflow++;
    }
}

void BSP_UART_Init(void)
{
    volatile uint32_t dummy;

    rx_head = 0u;
    rx_tail = 0u;
    tx_busy = 0u;
    tx_started_ms = 0u;
    rx_overflow = 0u;
    rx_error = 0u;
    tx_error = 0u;

    /*
     * Do not use HAL_UART_Receive_IT(..., 1). Re-arming HAL after every byte
     * leaves a receive gap that can drop bytes in a continuous frame at
     * 115200 baud. RXNE now writes DR directly into the software ring.
     */
    __HAL_UART_DISABLE_IT(&huart1, UART_IT_RXNE);
    __HAL_UART_DISABLE_IT(&huart1, UART_IT_ERR);

    /* STM32F1 clears PE/FE/NE/ORE by reading SR and then DR. */
    dummy = huart1.Instance->SR;
    dummy = huart1.Instance->DR;
    (void)dummy;

    /* DMA1_Channel4 TX uses priority 0; USART1 RX uses priority 1. */
    HAL_NVIC_SetPriority(USART1_IRQn, 1u, 0u);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    __HAL_UART_ENABLE_IT(&huart1, UART_IT_ERR);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
}

void BSP_UART_Service(uint32_t now_ms)
{
    /*
     * A maximum-size protocol frame is below 30 ms at 115200 baud. If the
     * transmit lock remains set for 100 ms, recover from a lost completion
     * interrupt or peripheral error so later replies are not blocked forever.
     */
    if (tx_busy != 0u && (uint32_t)(now_ms - tx_started_ms) > 100u) {
        (void)HAL_UART_AbortTransmit(&huart1);
        tx_busy = 0u;
        tx_error++;
    }
}

uint8_t BSP_UART_ReadByte(uint8_t *byte)
{
    uint16_t next;

    if (byte == 0) {
        return 0u;
    }
    if (rx_head == rx_tail) {
        return 0u;
    }

    *byte = rx_ring[rx_tail];
    next = (uint16_t)((rx_tail + 1u) % BSP_UART_RX_RING_SIZE);
    rx_tail = next;
    return 1u;
}

uint16_t BSP_UART_Read(uint8_t *buf, uint16_t len)
{
    uint16_t n = 0u;

    while (n < len && BSP_UART_ReadByte(&buf[n]) != 0u) {
        n++;
    }
    return n;
}

uint8_t BSP_UART_Send(const uint8_t *data, uint16_t len)
{
    if (data == 0 || len == 0u || len > BSP_UART_TX_BUF_SIZE) {
        return 0u;
    }
    if (tx_busy != 0u) {
        return 0u;
    }

    tx_busy = 1u;
    tx_started_ms = HAL_GetTick();
    (void)memcpy(tx_buf, data, len);
    if (HAL_UART_Transmit_DMA(&huart1, tx_buf, len) != HAL_OK) {
        tx_busy = 0u;
        tx_error++;
        return 0u;
    }
    return 1u;
}

uint8_t BSP_UART_SendIdle(void)
{
    return (tx_busy == 0u) ? 1u : 0u;
}

uint32_t BSP_UART_GetRxOverflow(void) { return rx_overflow; }
uint32_t BSP_UART_GetRxError(void)    { return rx_error; }
uint32_t BSP_UART_GetTxError(void)    { return tx_error; }

/* ---------------- Interrupt handling ---------------- */

void BSP_UART_IRQHandler(void)
{
    USART_TypeDef *uart = huart1.Instance;
    uint32_t sr = uart->SR;
    uint32_t errors = sr & (USART_SR_ORE | USART_SR_NE |
                            USART_SR_FE | USART_SR_PE);

    /*
     * Reading DR clears RXNE and, after the SR read above, also clears the
     * STM32F1 ORE/NE/FE/PE flags. Preserve the byte already present in DR even
     * when an error is reported; CRC/timeout recovery will re-sync a bad frame.
     */
    if ((sr & USART_SR_RXNE) != 0u) {
        uint8_t byte = (uint8_t)uart->DR;
        rx_push_isr(byte);
    } else if (errors != 0u) {
        volatile uint32_t dummy = uart->DR;
        (void)dummy;
    }

    if (errors != 0u) {
        rx_error++;
    }

    /*
     * HAL enables the USART TC interrupt after DMA transfers the last byte.
     * RX is BSP-owned, so finish only the HAL DMA-TX state here. Calling the
     * full HAL UART IRQ handler with RxState=READY could leave RXNE uncleared.
     */
    if (((uart->SR & USART_SR_TC) != 0u) &&
        ((uart->CR1 & USART_CR1_TCIE) != 0u)) {
        __HAL_UART_DISABLE_IT(&huart1, UART_IT_TC);
        huart1.gState = HAL_UART_STATE_READY;
        tx_busy = 0u;
#if (USE_HAL_UART_REGISTER_CALLBACKS == 1)
        huart1.TxCpltCallback(&huart1);
#else
        HAL_UART_TxCpltCallback(&huart1);
#endif
    }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        tx_busy = 0u;
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) {
        return;
    }

    /* RX errors are cleared directly by BSP_UART_IRQHandler. */
    if ((huart->ErrorCode & HAL_UART_ERROR_DMA) != 0u) {
        (void)HAL_UART_AbortTransmit(huart);
        tx_busy = 0u;
        tx_error++;
    }
}
