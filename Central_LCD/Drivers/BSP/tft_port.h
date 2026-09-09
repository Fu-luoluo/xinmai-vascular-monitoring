#ifndef __TFT_PORT_H
#define __TFT_PORT_H

#include <stdint.h>
#include "CH58x_common.h"

typedef uint8_t   u8;
typedef uint16_t  u16;
typedef uint32_t  u32;
typedef int32_t   s32;

/* LCD SPI 控制脚（2.4"/3.5" 共用）
 * SCK=PA0 MOSI=PA1 RST=PA3 DC=PA4 LED=PA7 CS=PA12
 * PA10/PA11 = 外部 32K 晶振，不可作背光 */
#define LCD_BL_PIN             GPIO_Pin_7

#define LCD_CS_SET()   GPIOA_SetBits(GPIO_Pin_12)
#define LCD_CS_CLR()   GPIOA_ResetBits(GPIO_Pin_12)
#define LCD_RS_SET()   GPIOA_SetBits(GPIO_Pin_4)
#define LCD_RS_CLR()   GPIOA_ResetBits(GPIO_Pin_4)
#define LCD_RST_SET()  GPIOA_SetBits(GPIO_Pin_3)
#define LCD_RST_CLR()  GPIOA_ResetBits(GPIO_Pin_3)
#define LCD_LED_ON()   GPIOA_SetBits(LCD_BL_PIN)
#define LCD_LED_OFF()  GPIOA_ResetBits(LCD_BL_PIN)

#endif
