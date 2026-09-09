#include "bsp_exti.h"

extern volatile uint8_t g_max30102_int_flag;

void max30102_int_gpio_init(void)
{
    GPIOB_ModeCfg(MAX30102_INT_PIN, GPIO_ModeIN_PU);
    GPIOB_ITModeCfg(MAX30102_INT_PIN, GPIO_ITMode_FallEdge);
    GPIOB_ClearITFlagBit(MAX30102_INT_PIN);

    PFIC_EnableIRQ(GPIO_B_IRQn);
}

__INTERRUPT
__HIGH_CODE
void GPIOB_IRQHandler(void)
{
    if(GPIOB_ReadITFlagBit(MAX30102_INT_PIN))
    {
        GPIOB_ClearITFlagBit(MAX30102_INT_PIN);
        g_max30102_int_flag = 1;
    }
}
