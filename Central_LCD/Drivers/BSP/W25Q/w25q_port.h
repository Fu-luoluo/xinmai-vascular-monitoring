#ifndef W25Q_PORT_H
#define W25Q_PORT_H

#include "CH58x_common.h"

/* W25Q64 on SPI0 default pins (RB_PIN_SPI0=0):
 *   SCK0=PA13, MOSI0=PA14, MISO0=PA15
 * CS on PA2 (GPIO, independent from SPI0 hardware SCS on PA12). */
#define W25Q_CS_PIN          GPIO_Pin_2
#define W25Q_SPI0_SCK_PIN    GPIO_Pin_13
#define W25Q_SPI0_MOSI_PIN   GPIO_Pin_14
#define W25Q_SPI0_MISO_PIN   GPIO_Pin_15

#define W25Q_SPI0_PINS_OUT   (W25Q_SPI0_SCK_PIN | W25Q_SPI0_MOSI_PIN)

/* PA12 = SPI0 hardware SCS; flash CS is PA2 — keep PA12 high as GPIO */
#define W25Q_SPI0_SCS_UNUSED_PIN   GPIO_Pin_12

#define W25Q_CS_HIGH()       GPIOA_SetBits(W25Q_CS_PIN)
#define W25Q_CS_LOW()        GPIOA_ResetBits(W25Q_CS_PIN)

#endif /* W25Q_PORT_H */
