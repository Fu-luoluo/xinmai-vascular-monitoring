#ifndef __BSP_I2C_H
#define __BSP_I2C_H

#include "CONFIG.h"

#define I2C_SCL_PIN GPIO_Pin_21
#define I2C_SDA_PIN GPIO_Pin_20

#define I2CT_FLAG_TIMEOUT ((uint32_t)0x1000)
#define I2CT_LONG_TIMEOUT ((uint32_t)(20U * I2CT_FLAG_TIMEOUT))

#define MPU_DEBUG_ON 1
#define MPU_INFO(fmt, arg...)  PRINT("<<-MPU-INFO->> " fmt "\n", ##arg)
#define MPU_ERROR(fmt, arg...) PRINT("<<-MPU-ERROR->> " fmt "\n", ##arg)
#define MPU_DEBUG(fmt, arg...)                                  \
    do                                                          \
    {                                                           \
        if(MPU_DEBUG_ON)                                        \
            PRINT("<<-MPU-DEBUG->> [%d] " fmt "\n", __LINE__, ##arg); \
    } while(0)

void    I2C_Bus_Init(void);
uint8_t I2C_ByteWrite(uint8_t pBuffer, uint8_t WriteAddr);
uint8_t I2C_BufferRead(uint8_t *pBuffer, uint8_t ReadAddr, uint16_t NumByteToRead);

#endif
