/**
 * @file    bsp_uart.h
 * @brief   USART1 BSP: direct RXNE receive ring + DMA transmit lock
 *
 * RX: RXNE reads DR directly into a ring buffer. This avoids the re-arm gap
 *     caused by repeated one-byte HAL_UART_Receive_IT calls.
 * TX: DMA1_Channel4 sends without blocking. Busy sends return 0 for retry.
 */
#ifndef BSP_UART_H
#define BSP_UART_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BSP_UART_RX_RING_SIZE   512u
#define BSP_UART_TX_BUF_SIZE    256u  /* >= PROTO_MAX_FRAME(203) */

/** Initialize buffers and enable USART1 RXNE/error interrupts. */
void BSP_UART_Init(void);

/** Recover a DMA TX whose completion interrupt was lost. */
void BSP_UART_Service(uint32_t now_ms);

/** @return 1 when one byte was read, otherwise 0. */
uint8_t BSP_UART_ReadByte(uint8_t *byte);

/** @return Number of bytes copied into buf. */
uint16_t BSP_UART_Read(uint8_t *buf, uint16_t len);

/** @return 1 when DMA send started, otherwise 0. */
uint8_t BSP_UART_Send(const uint8_t *data, uint16_t len);

/** @return 1 when TX is idle, otherwise 0. */
uint8_t BSP_UART_SendIdle(void);

uint32_t BSP_UART_GetRxOverflow(void);
uint32_t BSP_UART_GetRxError(void);
uint32_t BSP_UART_GetTxError(void);

/** Called by USART1_IRQHandler in stm32f1xx_it.c. */
void BSP_UART_IRQHandler(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_UART_H */
