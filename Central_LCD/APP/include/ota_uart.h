#ifndef __OTA_UART_H
#define __OTA_UART_H

#include <stdint.h>

void    OtaUart_Reset(void);
void    OtaUart_FeedByte(uint8_t b);
uint8_t OtaUart_HasError(void);
uint8_t OtaUart_IsFrameIdle(void);
uint8_t OtaUart_HasEnd(void);
uint8_t OtaUart_HasAbort(void);
uint8_t OtaUart_TakeFrameResult(uint8_t *ok);

#endif /* __OTA_UART_H */
