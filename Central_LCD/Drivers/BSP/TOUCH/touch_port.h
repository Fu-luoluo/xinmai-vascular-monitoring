#ifndef __TOUCH_PORT_H
#define __TOUCH_PORT_H

#include "CH58x_common.h"

/*
 * 3.5" 电容触摸 FT6336（位带 I2C）
 * CTP_RST:PB1  CTP_SDA:PB20  CTP_SCL:PB21  CTP_INT:PB8
 * SD_CS 不接
 */
#define CTP_PIN_RST   GPIO_Pin_1
#define CTP_PIN_SDA   GPIO_Pin_20
#define CTP_PIN_SCL   GPIO_Pin_21
#define CTP_PIN_INT   GPIO_Pin_8

/* 兼容 power_mgr / LVGL：INT 低表示有触摸事件（轮询模式下也可读） */
#define TP_PIN_IRQ    CTP_PIN_INT
#define PEN_READ()    (GPIOB_ReadPortPin(CTP_PIN_INT) == 0)

void TP_Port_GPIO_Init(void);
void TP_Port_TouchWakeRearm(void);

void CTP_IIC_Init(void);
void CTP_IIC_Start(void);
void CTP_IIC_Stop(void);
void CTP_IIC_Send_Byte(uint8_t txd);
uint8_t CTP_IIC_Read_Byte(uint8_t ack);
uint8_t CTP_IIC_Wait_Ack(void);
void CTP_IIC_Ack(void);
void CTP_IIC_NAck(void);

void CTP_RST_Low(void);
void CTP_RST_High(void);

/* 电容屏无需四点校准 */
#define TP_LANDSCAPE_RECAL_TEST  0

#endif
