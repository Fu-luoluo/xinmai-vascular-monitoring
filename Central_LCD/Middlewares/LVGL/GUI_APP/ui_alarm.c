#include "ui_alarm.h"
#include "alarm_mgr.h"
#include "ui_strings.h"
#include "ui_font.h"
#include "lvgl.h"
#include <string.h>

#define UI_ALARM_BORDER_MS      500U
#define UI_ALARM_PREFLIGHT_MS   2000U
#define UI_ALARM_WEAK_COLOR     0xFF9800U
#define UI_ALARM_BORDER_A       0xFFB74DU
#define UI_ALARM_BORDER_B       0xE65100U
#define UI_ALARM_PWV_NORMAL     0xB71C1CU
#define UI_ALARM_PWV_HIGH       0xE65100U
#define UI_ALARM_PWV_LOW        0x1565C0U

static ui_alarm_bind_t s_bind;
static lv_timer_t *    s_border_timer;
static lv_timer_t *      s_preflight_timer;
static uint8_t           s_border_phase;
static int8_t            s_pwv_trend;
static uint8_t           s_weak_active;

static void ui_alarm_border_timer_cb(lv_timer_t * timer);
static void ui_alarm_preflight_timer_cb(lv_timer_t * timer);

static void ui_alarm_cancel_border_timer(void)
{
    if(s_border_timer) {
        lv_timer_del(s_border_timer);
        s_border_timer = NULL;
    }
    s_border_phase = 0U;
    if(s_bind.signal_panel) {
        lv_obj_set_style_border_color(s_bind.signal_panel,
                                      lv_color_hex(UI_ALARM_BORDER_A), 0);
    }
}

static void ui_alarm_border_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    if(s_bind.signal_panel == NULL) {
        return;
    }
    s_border_phase ^= 1U;
    lv_obj_set_style_border_color(s_bind.signal_panel,
                                  lv_color_hex(s_border_phase ? UI_ALARM_BORDER_B
                                                              : UI_ALARM_BORDER_A),
                                  0);
}

static void ui_alarm_preflight_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    if(s_preflight_timer) {
        lv_timer_del(s_preflight_timer);
        s_preflight_timer = NULL;
    }
    if(s_bind.preflight) {
        lv_obj_set_style_text_color(s_bind.preflight, lv_color_hex(0x616161), 0);
    }
}

void UI_Alarm_Bind(const ui_alarm_bind_t * bind)
{
    if(bind == NULL) {
        memset(&s_bind, 0, sizeof(s_bind));
        return;
    }
    s_bind = *bind;
}

void UI_Alarm_StartBorderBlink(void)
{
    ui_alarm_cancel_border_timer();
    if(s_bind.signal_panel == NULL) {
        return;
    }
    s_border_timer = lv_timer_create(ui_alarm_border_timer_cb, UI_ALARM_BORDER_MS, NULL);
}

void UI_Alarm_ShowWeakSignal(void)
{
    s_weak_active = 1U;
    if(s_bind.vitals_card) {
        lv_obj_set_style_border_color(s_bind.vitals_card, lv_color_hex(UI_ALARM_WEAK_COLOR), 0);
        lv_obj_set_style_border_width(s_bind.vitals_card, 2, 0);
    }
    if(s_bind.preflight) {
        lv_label_set_text(s_bind.preflight, UI_STR_ALARM_WEAK_HINT);
        ui_obj_set_font_cn(s_bind.preflight);
        lv_obj_set_style_text_color(s_bind.preflight, lv_color_hex(UI_ALARM_WEAK_COLOR), 0);
    }
}

void UI_Alarm_ClearWeakSignal(void)
{
    s_weak_active = 0U;
    if(s_bind.vitals_card) {
        lv_obj_set_style_border_color(s_bind.vitals_card, lv_color_hex(0xA0C4E8), 0);
        lv_obj_set_style_border_width(s_bind.vitals_card, 1, 0);
    }
}

void UI_Alarm_FlashPreflightHint(void)
{
    if(s_bind.preflight == NULL) {
        return;
    }
    if(s_preflight_timer) {
        lv_timer_del(s_preflight_timer);
        s_preflight_timer = NULL;
    }
    lv_obj_set_style_text_color(s_bind.preflight, lv_color_hex(0xC62828), 0);
    s_preflight_timer = lv_timer_create(ui_alarm_preflight_timer_cb,
                                         UI_ALARM_PREFLIGHT_MS, NULL);
}

void UI_Alarm_SetPwvTrend(int8_t trend)
{
    s_pwv_trend = trend;
}

uint32_t UI_Alarm_GetPwvColor(void)
{
    if(s_pwv_trend > 0) {
        return UI_ALARM_PWV_HIGH;
    }
    if(s_pwv_trend < 0) {
        return UI_ALARM_PWV_LOW;
    }
    return UI_ALARM_PWV_NORMAL;
}

void UI_Alarm_OnMeasureStatusCleared(void)
{
    ui_alarm_cancel_border_timer();
    AlarmMgr_Stop();
}

uint8_t UI_Alarm_IsWeakActive(void)
{
    return s_weak_active;
}
