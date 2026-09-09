/**
 * @file power_mgr.c
 * Phase-2: display idle + HAL_SLEEP; PB8 (T_IRQ) GPIO wake (no 50ms poll)
 */
#include "power_mgr.h"
#include "measure_ctrl.h"
#include "lvgl_hal.h"
#include "touch_port.h"
#include "lcd.h"
#include "tft_port.h"
#include "lvgl.h"
#include "ui.h"
#include "alarm_mgr.h"
#include "CONFIG.h"
#include "CH58x_common.h"

#define POWER_EVT_TICK_MS        1000

#define POWER_EVT_TICK           0x0001
#define POWER_EVT_WAKE           0x0002

static uint8_t  s_powerTaskId = TASK_NO_TASK;
static uint8_t  s_displayAwake = 1;
static uint8_t  s_idleSec = 0;

static void power_touch_wake_init(void);
static void power_touch_wake_arm(void);
static void power_enter_sleep(void);
static void power_wake_display(void);
static uint16_t PowerMgr_ProcessEvent(uint8_t task_id, uint16_t events);

static void power_touch_wake_init(void)
{
    TP_Port_GPIO_Init();
    power_touch_wake_arm();
    PFIC_EnableIRQ(GPIO_B_IRQn);
    PWR_PeriphWakeUpCfg(ENABLE, RB_SLP_GPIO_WAKE, Long_Delay);
}

static void power_touch_wake_arm(void)
{
    TP_Port_TouchWakeRearm();
}

void PowerMgr_TouchWakeRearm(void)
{
    power_touch_wake_arm();
}

static void power_enter_sleep(void)
{
    if(!s_displayAwake) {
        return;
    }
    if(Measure_IsActive()) {
        return;
    }
    if(AlarmMgr_IsActive()) {
        return;
    }
    Measure_OnPause();
    UI_Refresh();
    s_displayAwake = 0;
    LCD_DisplaySleep();
    LCD_LED_OFF();
    lvgl_hal_suspend();
    power_touch_wake_arm();
}

static void power_wake_display(void)
{
    if(s_displayAwake) {
        return;
    }
    s_displayAwake = 1;
    s_idleSec = 0;
    TP_Port_GPIO_Init();
    LCD_DisplayWake();
    LCD_LED_ON();
    lvgl_hal_resume();
    lv_obj_invalidate(lv_scr_act());
    UI_Refresh();
    tmos_start_task(s_powerTaskId, POWER_EVT_TICK, MS1_TO_SYSTEM_TIME(POWER_EVT_TICK_MS));
}

void PowerMgr_Init(void)
{
    s_displayAwake = 1;
    s_idleSec = 0;
    s_powerTaskId = TMOS_ProcessEventRegister(PowerMgr_ProcessEvent);
    power_touch_wake_init();
    tmos_start_task(s_powerTaskId, POWER_EVT_TICK, MS1_TO_SYSTEM_TIME(POWER_EVT_TICK_MS));
}

void PowerMgr_OnUserActivity(void)
{
    if(!s_displayAwake) {
        power_wake_display();
        return;
    }
    s_idleSec = 0;
}

uint8_t PowerMgr_IsDisplayAwake(void)
{
    return s_displayAwake;
}

__INTERRUPT
__HIGH_CODE
void GPIOB_IRQHandler(void)
{
    if(GPIOB_ReadITFlagBit(TP_PIN_IRQ)) {
        GPIOB_ClearITFlagBit(TP_PIN_IRQ);
        if(!s_displayAwake && s_powerTaskId != TASK_NO_TASK) {
            tmos_set_event(s_powerTaskId, POWER_EVT_WAKE);
        }
    }
}

static uint16_t PowerMgr_ProcessEvent(uint8_t task_id, uint16_t events)
{
    (void)task_id;

    if(events & POWER_EVT_WAKE) {
        power_wake_display();
        events ^= POWER_EVT_WAKE;
    }

    if(events & POWER_EVT_TICK) {
        if(s_displayAwake) {
    if(Measure_IsActive()) {
        s_idleSec = 0;
    } else if(AlarmMgr_IsActive()) {
        s_idleSec = 0;
    } else {
                s_idleSec++;
                if(s_idleSec >= POWER_IDLE_TIMEOUT_SEC) {
                    power_enter_sleep();
                }
            }
            if(s_displayAwake) {
                tmos_start_task(s_powerTaskId, POWER_EVT_TICK,
                                MS1_TO_SYSTEM_TIME(POWER_EVT_TICK_MS));
            }
        }
        events ^= POWER_EVT_TICK;
    }

    return events;
}
