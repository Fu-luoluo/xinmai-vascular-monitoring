#ifndef __BSP_MAX30102_H
#define __BSP_MAX30102_H

#include "CONFIG.h"

#define I2C_WRITE_ADDR 0xAE
#define I2C_READ_ADDR 0xAF

#define INTERRUPT_STATUS1 0X00
#define INTERRUPT_STATUS2 0X01
#define INTERRUPT_ENABLE1 0X02
#define INTERRUPT_ENABLE2 0X03

#define FIFO_WR_POINTER 0X04
#define FIFO_OV_COUNTER 0X05
#define FIFO_RD_POINTER 0X06
#define FIFO_DATA 0X07

#define FIFO_CONFIGURATION 0X08
#define MODE_CONFIGURATION 0X09
#define SPO2_CONFIGURATION 0X0A
#define LED1_PULSE_AMPLITUDE 0X0C
#define LED2_PULSE_AMPLITUDE 0X0D

#define MULTILED1_MODE 0X11
#define MULTILED2_MODE 0X12

#define TEMPERATURE_INTEGER 0X1F
#define TEMPERATURE_FRACTION 0X20
#define TEMPERATURE_CONFIG 0X21

#define VERSION_ID 0XFE
#define PART_ID 0XFF

uint8_t max30102_init(void);
/*
 * max30102_reset - 强制清掉 init 缓存的 once-only 标志，
 * 让下一次 max30102_init() 真的去重新写所有寄存器（包括 FIFO 指针清零）。
 * 不动硬件、不做任何 I2C 读写。
 */
void    max30102_reset(void);
uint8_t max30102_fifo_read(float *data);
uint8_t max30102_fifo_available(void);
uint8_t max30102_i2c_write(uint8_t reg_adder, uint8_t data);
uint8_t max30102_i2c_read(uint8_t reg_adder, uint8_t *pdata, uint8_t data_size);
uint8_t max30102_is_ready(void);
uint16_t max30102_getHeartRate(float *input_data,uint16_t cache_nums);
float max30102_getSpO2(float *ir_input_data,float *red_input_data,uint16_t cache_nums);

#endif /* __BSP_MAX30102_H */
