#ifndef __SPI_H
#define __SPI_H

#include "tft_port.h"

/* 兼容商家 lcd.c 中的 SPI_WriteByte(SPI2, ...) 调用，CH585 固定用 SPI1 */
#define SPI2  ((void *)0)

u8 SPI_WriteByte(void *SPIx, u8 Byte);
void SPI1_WriteBytes(const uint8_t * data, uint32_t len);
void SPI2_Init(void);
void SPI_SetSpeed(void *SPIx, u8 SpeedSet);

#endif
