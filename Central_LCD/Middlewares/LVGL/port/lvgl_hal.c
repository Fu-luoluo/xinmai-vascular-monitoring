/**
 * @file lvgl_hal.c
 * LVGL bring-up and TMOS 5ms tick/handler task
 */
#include "lvgl_hal.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "ui.h"
#include "ui_wifi.h"
#include "host_link.h"
#include "power_mgr.h"
#include "touch.h"
#include "lvgl.h"
#include "voice_mgr.h"
#include "CONFIG.h"
#if LV_USE_DEMO_WIDGETS
#include "lv_demos.h"
#endif

void ui_lvgl_oom_reset(void)
{
    SYS_ResetExecute();
}

#define LVGL_TASK_EVT           0x0001
#define LVGL_TASK_PERIOD_MS     5

static uint16_t lvgl_task_period_ms(void)
{
    (void)0;
    return LVGL_TASK_PERIOD_MS;
}

static uint8_t lvglTaskId = TASK_NO_TASK;
static uint8_t s_lvgl_running = 0;
static uint8_t s_touch_rearm_done = 0;

#if 0 /* 触摸测试 UI，验收用 */
static void lvgl_create_minimal_ui(void)
{
    lv_obj_t * label = lv_label_create(lv_scr_act());
    lv_label_set_text(label, "CH585 LVGL OK");
    lv_obj_center(label);

    lv_obj_t * btn = lv_btn_create(lv_scr_act());
    lv_obj_set_size(btn, 100, 40);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -40);
    lv_obj_t * btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Touch");
    lv_obj_center(btn_label);
}
#endif

static uint16_t LVGL_ProcessEvent(uint8_t task_id, uint16_t events);

void lvgl_hal_init(void)
{
    lv_init();
    lv_port_disp_init();
    lv_port_indev_init();
    s_touch_rearm_done = 0;
#if LV_USE_DEMO_WIDGETS
    lv_demo_widgets();
#else
    UI_Init();
    /* 欢迎语会触发 UART3 remap 抢 CTP 脚；播报已改为发完归还，但仍延后到手动/另脚再开 */
    /* Voice_ScheduleWelcome(2000U); */
#endif
}

void lvgl_hal_task_start(void)
{
    lvglTaskId = TMOS_ProcessEventRegister(LVGL_ProcessEvent);
    s_lvgl_running = 1;
    tmos_start_task(lvglTaskId, LVGL_TASK_EVT, MS1_TO_SYSTEM_TIME(lvgl_task_period_ms()));
}

void lvgl_hal_suspend(void)
{
    s_lvgl_running = 0;
}

void lvgl_hal_resume(void)
{
    if(lvglTaskId == TASK_NO_TASK) {
        return;
    }
    s_lvgl_running = 1;
    tmos_start_task(lvglTaskId, LVGL_TASK_EVT, MS1_TO_SYSTEM_TIME(lvgl_task_period_ms()));
}

uint8_t lvgl_hal_is_running(void)
{
    return s_lvgl_running;
}

static uint16_t LVGL_ProcessEvent(uint8_t task_id, uint16_t events)
{
    (void)task_id;

    if(events & LVGL_TASK_EVT) {
        if(s_lvgl_running) {
            lv_tick_inc(lvgl_task_period_ms());
            if(!s_touch_rearm_done && TMOS_GetSystemClock() > MS1_TO_SYSTEM_TIME(500U)) {
                TP_Port_GPIO_Init();
                if(tp_dev.xfac < 0.01f || tp_dev.xfac > 2.0f) {
                    TP_Get_Adjdata();
                }
                PowerMgr_TouchWakeRearm();
                s_touch_rearm_done = 1U;
            }
            lv_timer_handler();
            HostLink_Poll();
            if(UI_WiFi_IsActive()) {
                if(!UI_WiFi_IsPassView()) {
                    HostLink_DispatchPending();
                    UI_WiFi_UpdateDebug();
                    UI_WiFi_ApplyPending();
                }
            }
            UI_Refresh();
            tmos_start_task(lvglTaskId, LVGL_TASK_EVT, MS1_TO_SYSTEM_TIME(lvgl_task_period_ms()));
        }
        return (events ^ LVGL_TASK_EVT);
    }

    return 0;
}
