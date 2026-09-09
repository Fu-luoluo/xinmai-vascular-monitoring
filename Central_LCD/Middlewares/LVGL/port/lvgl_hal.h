/**
 * @file lvgl_hal.h
 * LVGL init and TMOS periodic task for CH585 Central_LCD
 */
#ifndef LVGL_HAL_H
#define LVGL_HAL_H

#include "tft_port.h"

#ifdef __cplusplus
extern "C" {
#endif

void lvgl_hal_init(void);
void lvgl_hal_task_start(void);
void lvgl_hal_suspend(void);
void lvgl_hal_resume(void);
uint8_t lvgl_hal_is_running(void);

#ifdef __cplusplus
}
#endif

#endif /* LVGL_HAL_H */
