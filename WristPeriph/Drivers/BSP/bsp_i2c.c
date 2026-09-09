#include "bsp_i2c.h"
#include "../Peripherals/max30102.h"

#define I2C_SPEED        100000U
#define I2C_OWN_ADDRESS7 0x0A

static __IO uint32_t I2CTimeout = 0U;

static uint8_t I2C_TIMEOUT_UserCallback(void);
static uint8_t I2C_WaitEvent(uint32_t event);
static uint8_t I2C_WaitBusFree(uint32_t timeout);

static void I2C_GPIO_Config(void)
{
    GPIOPinRemap(ENABLE, RB_PIN_I2C);
    GPIOB_SetBits(I2C_SCL_PIN | I2C_SDA_PIN);
    GPIOB_ModeCfg(I2C_SCL_PIN | I2C_SDA_PIN, GPIO_ModeIN_PU);
}

static void I2C_Mode_Config(void)
{
    I2C_Init(I2C_Mode_I2C, I2C_SPEED, I2C_DutyCycle_2, I2C_Ack_Enable, I2C_AckAddr_7bit, I2C_OWN_ADDRESS7);
    I2C_Cmd(ENABLE);
}

void I2C_Bus_Init(void)
{
    I2C_GPIO_Config();
    I2C_Mode_Config();
}

static uint8_t I2C_WaitEvent(uint32_t event)
{
    I2CTimeout = I2CT_FLAG_TIMEOUT;
    while(!I2C_CheckEvent(event))
    {
        if(I2C_GetFlagStatus(I2C_FLAG_AF) == SET)
        {
            I2C_ClearFlag(I2C_FLAG_AF);
            I2C_GenerateSTOP(ENABLE);
            return I2C_TIMEOUT_UserCallback();
        }
        if((I2CTimeout--) == 0U)
        {
            I2C_GenerateSTOP(ENABLE);
            return I2C_TIMEOUT_UserCallback();
        }
    }
    return 1U;
}

static uint8_t I2C_WaitBusFree(uint32_t timeout)
{
    I2CTimeout = timeout;
    while(I2C_GetFlagStatus(I2C_FLAG_BUSY) == SET)
    {
        if((I2CTimeout--) == 0U)
        {
            I2C_GenerateSTOP(ENABLE);
            I2C_SoftwareResetCmd(ENABLE);
            I2C_SoftwareResetCmd(DISABLE);
            return I2C_TIMEOUT_UserCallback();
        }
    }
    return 1U;
}

uint8_t I2C_ByteWrite(uint8_t pBuffer, uint8_t WriteAddr)
{
    if(!I2C_WaitBusFree(I2CT_LONG_TIMEOUT))
    {
        return 0;
    }

    I2C_GenerateSTART(ENABLE);
    if(!I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
    {
        return 0;
    }

    I2C_Send7bitAddress(I2C_WRITE_ADDR, I2C_Direction_Transmitter);
    if(!I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        return 0;
    }

    I2C_SendData(WriteAddr);
    if(!I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        return 0;
    }

    I2C_SendData(pBuffer);
    if(!I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        return 0;
    }

    I2C_GenerateSTOP(ENABLE);
    return 1U;
}

uint8_t I2C_BufferRead(uint8_t *pBuffer, uint8_t ReadAddr, uint16_t NumByteToRead)
{
    if(!I2C_WaitBusFree(I2CT_LONG_TIMEOUT))
    {
        return 0;
    }

    I2C_GenerateSTART(ENABLE);
    if(!I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
    {
        return 0;
    }

    I2C_Send7bitAddress(I2C_WRITE_ADDR, I2C_Direction_Transmitter);
    if(!I2C_WaitEvent(I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED))
    {
        return 0;
    }

    I2C_SendData(ReadAddr);
    if(!I2C_WaitEvent(I2C_EVENT_MASTER_BYTE_TRANSMITTED))
    {
        return 0;
    }

    I2C_GenerateSTART(ENABLE);
    if(!I2C_WaitEvent(I2C_EVENT_MASTER_MODE_SELECT))
    {
        return 0;
    }

    I2C_Send7bitAddress(I2C_READ_ADDR, I2C_Direction_Receiver);
    if(!I2C_WaitEvent(I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED))
    {
        return 0;
    }

    while(NumByteToRead)
    {
        if(NumByteToRead == 1U)
        {
            I2C_AcknowledgeConfig(DISABLE);
            I2C_GenerateSTOP(ENABLE);
        }

        I2CTimeout = I2CT_FLAG_TIMEOUT;
        while(!I2C_CheckEvent(I2C_EVENT_MASTER_BYTE_RECEIVED))
        {
            if((I2CTimeout--) == 0U)
            {
                I2C_GenerateSTOP(ENABLE);
                I2C_AcknowledgeConfig(ENABLE);
                return I2C_TIMEOUT_UserCallback();
            }
        }

        *pBuffer = I2C_ReceiveData();
        pBuffer++;
        NumByteToRead--;
    }

    I2C_AcknowledgeConfig(ENABLE);
    return 1U;
}

static uint8_t I2C_TIMEOUT_UserCallback(void)
{
    MPU_ERROR("I2C Timeout error!");
    return 0U;
}
