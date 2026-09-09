/**
 * @file lv_port_disp.h
 * CH585 ST7796S display port for LVGL (landscape 480x320)
 */
#ifndef LV_PORT_DISP_H
#define LV_PORT_DISP_H

#define LV_DISP_HOR_RES  480
#define LV_DISP_VER_RES  320

#ifdef __cplusplus
extern "C" {
#endif

void lv_port_disp_init(void);

#ifdef __cplusplus
}
#endif

#endif /* LV_PORT_DISP_H */
