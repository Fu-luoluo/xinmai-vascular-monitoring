#include "touch_port.h"
#include "tft_delay.h"

static void ctp_sda_out(void)
{
    GPIOB_ModeCfg(CTP_PIN_SDA, GPIO_ModeOut_PP_5mA);
}

static void ctp_sda_in(void)
{
    GPIOB_ModeCfg(CTP_PIN_SDA, GPIO_ModeIN_PU);
}

static void ctp_scl(uint8_t hi)
{
    if(hi) {
        GPIOB_SetBits(CTP_PIN_SCL);
    } else {
        GPIOB_ResetBits(CTP_PIN_SCL);
    }
}

static void ctp_sda(uint8_t hi)
{
    if(hi) {
        GPIOB_SetBits(CTP_PIN_SDA);
    } else {
        GPIOB_ResetBits(CTP_PIN_SDA);
    }
}

static uint8_t ctp_sda_read(void)
{
    return (GPIOB_ReadPortPin(CTP_PIN_SDA) != 0U) ? 1U : 0U;
}

static void ctp_delay(void)
{
    delay_us(2);
}

void CTP_RST_Low(void)
{
    GPIOB_ResetBits(CTP_PIN_RST);
}

void CTP_RST_High(void)
{
    GPIOB_SetBits(CTP_PIN_RST);
}

void TP_Port_GPIO_Init(void)
{
    /* PB20/PB21 与 UART3 remap / RF_ANT / I2C remap 互斥，触摸优先占用 */
    GPIOPinRemap(DISABLE, RB_PIN_UART3);
    GPIOPinRemap(DISABLE, RB_PIN_I2C);
    GPIOPinRemap(DISABLE, RB_RF_ANT_SW_EN);
    R16_PIN_CONFIG &= (uint16_t)(~RB_PBHx_IN_DIS);

    GPIOB_SetBits(CTP_PIN_RST | CTP_PIN_SCL | CTP_PIN_SDA);
    GPIOB_ModeCfg(CTP_PIN_RST | CTP_PIN_SCL | CTP_PIN_SDA, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(CTP_PIN_INT, GPIO_ModeIN_PU);
}

void TP_Port_TouchWakeRearm(void)
{
    GPIOB_ClearITFlagBit(CTP_PIN_INT);
    GPIOB_ITModeCfg(CTP_PIN_INT, GPIO_ITMode_FallEdge);
}

void CTP_IIC_Init(void)
{
    TP_Port_GPIO_Init();
    ctp_scl(1);
    ctp_sda(1);
}

void CTP_IIC_Start(void)
{
    ctp_sda_out();
    ctp_sda(1);
    ctp_scl(1);
    ctp_delay();
    ctp_sda(0);
    ctp_delay();
    ctp_scl(0);
}

void CTP_IIC_Stop(void)
{
    ctp_sda_out();
    ctp_scl(0);
    ctp_sda(0);
    ctp_delay();
    ctp_scl(1);
    ctp_delay();
    ctp_sda(1);
}

uint8_t CTP_IIC_Wait_Ack(void)
{
    uint8_t err = 0U;

    ctp_sda_in();
    ctp_sda(1);
    delay_us(1);
    ctp_scl(1);
    delay_us(1);
    while(ctp_sda_read()) {
        err++;
        if(err > 250U) {
            CTP_IIC_Stop();
            return 1U;
        }
    }
    ctp_scl(0);
    return 0U;
}

void CTP_IIC_Ack(void)
{
    ctp_scl(0);
    ctp_sda_out();
    ctp_sda(0);
    ctp_delay();
    ctp_scl(1);
    ctp_delay();
    ctp_scl(0);
}

void CTP_IIC_NAck(void)
{
    ctp_scl(0);
    ctp_sda_out();
    ctp_sda(1);
    ctp_delay();
    ctp_scl(1);
    ctp_delay();
    ctp_scl(0);
}

void CTP_IIC_Send_Byte(uint8_t txd)
{
    uint8_t t;

    ctp_sda_out();
    ctp_scl(0);
    for(t = 0U; t < 8U; t++) {
        ctp_sda((txd & 0x80U) ? 1U : 0U);
        txd <<= 1;
        ctp_scl(1);
        ctp_delay();
        ctp_scl(0);
        ctp_delay();
    }
}

uint8_t CTP_IIC_Read_Byte(uint8_t ack)
{
    uint8_t i;
    uint8_t receive = 0U;

    ctp_sda_in();
    for(i = 0U; i < 8U; i++) {
        ctp_scl(0);
        delay_us(3);
        ctp_scl(1);
        receive <<= 1;
        if(ctp_sda_read()) {
            receive++;
        }
    }
    if(ack) {
        CTP_IIC_Ack();
    } else {
        CTP_IIC_NAck();
    }
    return receive;
}
