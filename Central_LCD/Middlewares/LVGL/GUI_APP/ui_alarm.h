#ifndef UI_ALARM_H
#define UI_ALARM_H

#include "lvgl.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lv_obj_t * signal_panel;
    lv_obj_t * signal_title;
    lv_obj_t * signal_hint;
    lv_obj_t * pwv_value;
    lv_obj_t * pwv_ref;
    lv_obj_t * preflight;
    lv_obj_t * vitals_card;
    lv_obj_t * ble_top;
} ui_alarm_bind_t;

void UI_Alarm_Bind(const ui_alarm_bind_t * bind);
void UI_Alarm_StartBorderBlink(void);
void UI_Alarm_ShowWeakSignal(void);
void UI_Alarm_ClearWeakSignal(void);
void UI_Alarm_FlashPreflightHint(void);
void UI_Alarm_SetPwvTrend(int8_t trend);
uint32_t UI_Alarm_GetPwvColor(void);
void UI_Alarm_OnMeasureStatusCleared(void);
uint8_t UI_Alarm_IsWeakActive(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_ALARM_H */
