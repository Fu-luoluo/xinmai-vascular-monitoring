/**
 * @file lv_port_indev.c
 * CH585 FT6336 capacitive touch input for LVGL (landscape 480x320)
 */
#include "lv_port_indev.h"
#include "lv_port_disp.h"
#include "lvgl.h"
#include "touch.h"
#include "touch_port.h"
#include "lcd.h"
#include "power_mgr.h"

#ifndef LV_TP_X_ADJ
#define LV_TP_X_ADJ  0
#endif
#ifndef LV_TP_Y_ADJ
#define LV_TP_Y_ADJ  0
#endif

static void touchpad_init(void);
static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data);

void lv_port_indev_init(void)
{
    touchpad_init();

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    indev_drv.scroll_limit = 4;
    indev_drv.scroll_throw = 4;
    lv_indev_drv_register(&indev_drv);
}

static void touchpad_init(void)
{
    TP_Port_GPIO_Init();
    (void)TP_Get_Adjdata();
}

static void touchpad_read(lv_indev_drv_t * indev_drv, lv_indev_data_t * data)
{
    (void)indev_drv;

    if(!PowerMgr_IsDisplayAwake()) {
        if(PEN_READ()) {
            PowerMgr_OnUserActivity();
        }
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if(TP_Scan(0)) {
        PowerMgr_OnUserActivity();

        lv_coord_t x = (lv_coord_t)tp_dev.x + LV_TP_X_ADJ;
        lv_coord_t y = (lv_coord_t)tp_dev.y + LV_TP_Y_ADJ;

        if(x < 0) {
            x = 0;
        }
        if(y < 0) {
            y = 0;
        }
        if(x >= LV_DISP_HOR_RES) {
            x = (lv_coord_t)(LV_DISP_HOR_RES - 1);
        }
        if(y >= LV_DISP_VER_RES) {
            y = (lv_coord_t)(LV_DISP_VER_RES - 1);
        }

        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = x;
        data->point.y = y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
