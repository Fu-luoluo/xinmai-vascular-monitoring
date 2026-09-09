#ifndef __BSP_BUZZER_H
#define __BSP_BUZZER_H

#include "CH58x_common.h"

/* Active-low buzzer module on PB5: LOW = on, HIGH = off */
#define BUZZER_GPIO_PIN    GPIO_Pin_5

void Buzzer_Init(void);
void Buzzer_On(void);
void Buzzer_Off(void);

#endif /* __BSP_BUZZER_H */
