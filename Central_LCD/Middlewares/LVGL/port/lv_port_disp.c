/**
 * @file lv_port_disp.c
 * CH585 ST7796S SPI flush via lcd.c + SPI1_MasterTrans block write
 */
#include "lv_port_disp.h"
#include "lvgl.h"
#include "lcd.h"
#include "SPI.h"

/* 480*20*2≈19.2KB：减少全屏刷新 SPI 次数，用剩余 RAM 换手感 */
#define LV_DISP_BUF_LINES   20

static void disp_init(void);
static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p);
static void rgb565_to_be_bytes(lv_color_t * color_p, uint32_t px_count);

void lv_port_disp_init(void)
{
    disp_init();

    static lv_disp_draw_buf_t draw_buf;
    static lv_color_t buf1[LV_DISP_HOR_RES * LV_DISP_BUF_LINES];
    lv_disp_draw_buf_init(&draw_buf, buf1, NULL, LV_DISP_HOR_RES * LV_DISP_BUF_LINES);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LV_DISP_HOR_RES;
    disp_drv.ver_res = LV_DISP_VER_RES;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}

static void disp_init(void)
{
    /* LCD_Init() is called from application before lvgl_hal_init() */
}

static void rgb565_to_be_bytes(lv_color_t * color_p, uint32_t px_count)
{
    uint32_t i;

    for(i = 0; i < px_count; i++) {
        uint16_t c = color_p[i].full;
        color_p[i].full = (uint16_t)((c << 8) | (c >> 8));
    }
}

static void disp_flush(lv_disp_drv_t * disp_drv, const lv_area_t * area, lv_color_t * color_p)
{
    uint32_t w = (uint32_t)(area->x2 - area->x1 + 1);
    uint32_t h = (uint32_t)(area->y2 - area->y1 + 1);
    uint32_t px = w * h;

    LCD_SetWindows((u16)area->x1, (u16)area->y1, (u16)area->x2, (u16)area->y2);
    LCD_CS_CLR();
    LCD_RS_SET();

    rgb565_to_be_bytes(color_p, px);
    SPI1_WriteBytes((const uint8_t *)color_p, px * 2U);

    LCD_CS_SET();
    lv_disp_flush_ready(disp_drv);
}
