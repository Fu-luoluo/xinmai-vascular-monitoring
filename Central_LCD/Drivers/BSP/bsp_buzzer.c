#include "bsp_buzzer.h"

void Buzzer_Init(void)
{
    GPIOB_SetBits(BUZZER_GPIO_PIN);
    GPIOB_ModeCfg(BUZZER_GPIO_PIN, GPIO_ModeOut_PP_5mA);
}

void Buzzer_On(void)
{
    GPIOB_ResetBits(BUZZER_GPIO_PIN);
}

void Buzzer_Off(void)
{
    GPIOB_SetBits(BUZZER_GPIO_PIN);
}
