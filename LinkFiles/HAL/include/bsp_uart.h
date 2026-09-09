#ifndef __BSP_UART_H
#define __BSP_UART_H

#include "CONFIG.h"

#define BSP_UART1_RX_LINE_MAX   768U

void BSP_UART1_Init(uint32_t baudrate);
void BSP_UART1_SendString(const char *str);
void BSP_UART1_SendBytes(const uint8_t *data, uint16_t len);
void BSP_UART1_SendHeartJSON(uint8_t hr, uint8_t spo2, float pwv);

void BSP_UART1_SetOtaBinaryMode(uint8_t enable);
uint8_t BSP_UART1_IsOtaBinaryMode(void);

/* Poll assembled RX lines from ESP32-C3. Returns 1 if one line copied to buf. */
uint8_t BSP_UART1_PollLine(char *buf, uint16_t buf_len);

/* Returned storage remains valid until the next line-poll call. */
const char *BSP_UART1_PollLinePtr(void);

/* Poll one raw byte while in OTA binary mode. Returns 1 if byte available. */
uint8_t BSP_UART1_PollByte(uint8_t *out);

void BSP_UART1_DrainHwFifo(void);
uint32_t BSP_UART1_GetRxByteCount(void);
uint32_t BSP_UART1_GetRxDropCount(void);

#endif /* __BSP_UART_H */
