#ifndef __BSP_VOICE_H
#define __BSP_VOICE_H

#include "CH58x_common.h"

#define VOICE_UART_BAUD         9600U

void BSP_VoiceUart_Init(void);
void BSP_VoiceUart_Send(const uint8_t *buf, uint16_t len);

#endif /* __BSP_VOICE_H */
