/**
 * @file ui.c
 * 4-page UI:
 *   Home    — Kiosk hub (Measure / Profile / History / WiFi)
 *   Monitor — scheme A: title+btn | PWV+status | vitals | nav
 *   History — Flash record list (ui_history module)
 *   Signal Lost — full-screen overlay
 */
#include "ui.h"
#include "ui_wifi.h"
#include "ui_history.h"
#include "ui_profile.h"
#include "ui_strings.h"
#include "ui_font.h"
#include "user_profile.h"
#include "lv_port_disp.h"
#include "power_mgr.h"
#include "measure_ctrl.h"
#include "alarm_mgr.h"
#include "ui_alarm.h"
#include "ui_scr_del.h"
#include "lvgl_hal.h"
#include "lvgl.h"
#include "pwv_calib.h"
#include <stdio.h>
#include <string.h>

#define UI_SIGNAL_LOST_MS       3000
#define UI_NAV_H                42
#define UI_MON_TOP_H            40
#define UI_MON_PWV_H            148
#define UI_MON_PREFLIGHT_H      24
#define UI_MON_BODY_GAP         6
#define UI_MON_CARD_H           56
#define UI_MON_BTN_H            32
#define UI_MON_BTN_W            120
#define UI_HOME_TOP_H           40
#define UI_HOME_TILE_W          224
#define UI_HOME_TILE_H          112
#define UI_HOME_TILE_GAP        12

typedef struct {
    uint8_t  w_hr;
    uint8_t  w_spo2;
    uint8_t  f_hr;
    uint8_t  f_spo2;
    uint8_t  w_valid;
    uint8_t  f_valid;
    float    pwv;
    int16_t  pwv_x10;
    uint8_t  pwv_valid;
    uint8_t  signal_quality;
    uint8_t  w_link;
    uint8_t  f_link;
    uint8_t  w_connecting;
    uint8_t  f_connecting;
    volatile uint8_t dirty;
} ui_state_t;

typedef struct {
    lv_obj_t * vitals;
    lv_obj_t * wrist_dot;
    lv_obj_t * finger_dot;
    lv_obj_t * card;
} ui_vitals_view_t;

typedef struct {
    lv_obj_t * home;
    lv_obj_t * monitor;
    lv_obj_t * history;
} ui_nav_btns_t;

static ui_state_t     s_ui;
static uint8_t        s_w_hr_disp = 0U;
static uint8_t        s_f_hr_disp = 0U;
static ui_page_t      s_page;
static uint8_t        s_measure_status = MEASURE_UI_STATUS_NONE;
static lv_obj_t *     s_scr_home;
static lv_obj_t *     s_scr_monitor;
static lv_obj_t *     s_scr_signal_lost;
static lv_obj_t *     s_sig_lost_panel;
static lv_obj_t *     s_sig_lost_title;
static lv_obj_t *     s_sig_lost_hint;
static lv_obj_t *     s_lbl_pwv_value;
static lv_obj_t *     s_lbl_pwv_unit;
static lv_obj_t *     s_lbl_pwv_done;
static lv_obj_t *     s_lbl_status_title;
static lv_obj_t *     s_pwv_panel;
static uint8_t        s_status_mode; /* 0 idle / 1 run / 2 done */
static lv_obj_t *     s_lbl_quality;
static lv_obj_t *     s_btn_measure;
static lv_obj_t *     s_lbl_measure;
static lv_obj_t *     s_lbl_preflight;
static lv_obj_t *     s_mon_body;
static lv_obj_t *     s_btn_home_measure;
static lv_obj_t *     s_lbl_home_profile_sub;
static ui_nav_btns_t  s_monitor_nav;
static ui_nav_btns_t  s_history_nav;
static ui_nav_btns_t *s_active_nav;
static ui_vitals_view_t s_vitals;
static lv_timer_t *   s_signal_lost_timer;
static lv_timer_t *   s_link_blink_timer;
static uint8_t        s_link_blink_phase;
#define UI_LINK_BLINK_MS        500U
#define UI_LINK_DOT_SLOT_W      28
static char           s_preflight_buf[56];
static char           s_gate_hint[56];
static char           s_quality_buf[32];
static char           s_result_pwv_buf[28];
static char           s_result_ref_buf[40];
static lv_obj_t *     s_result_overlay;

static void ui_hide_result_overlay(void);
static void ui_show_result_overlay(void);
static void ui_on_result_back(lv_event_t * e);
static void ui_create_home_screen(void);
static void ui_destroy_home_screen(void);
static void ui_ensure_home_screen(void);
static void ui_bind_alarm(void);
static void ui_destroy_monitor_screen(void);
static void ui_release_secondary_screens(void);
static void ui_release_secondary_async(void * user_data);
static void ui_nav_home_async(void * user_data);
static void ui_nav_monitor_async(void * user_data);
static void ui_nav_history_async(void * user_data);
static void ui_home_measure_async(void * user_data);
static void ui_home_profile_async(void * user_data);
static void ui_home_history_async(void * user_data);
static void ui_goto_wifi_async(void * user_data);
static void ui_measure_toggle_async(void * user_data);
static void ui_history_refresh_async(void * user_data);
static void ui_create_monitor_screen(void);
static void ui_create_signal_lost_screen(void);
static void ui_refresh_home(void);
static lv_obj_t * ui_create_home_tile(lv_obj_t * parent, const char * title,
                                      const char * subtitle, lv_event_cb_t cb,
                                      uint8_t primary);
static lv_obj_t * ui_create_vitals_card(lv_obj_t * parent, ui_vitals_view_t * view);
static lv_obj_t * ui_create_nav_btn(lv_obj_t * parent, const char * text, lv_event_cb_t cb);
static void ui_style_nav_btn(lv_obj_t * btn, uint8_t active);
static void ui_update_nav_highlight(void);
static void ui_show_page(ui_page_t page);
static void ui_update_measure_btn(void);
static void ui_update_quality_label(void);
static void ui_update_status_label(void);
static void ui_monitor_relayout(void);
static void ui_update_preflight_hint(void);
static void ui_refresh_vitals_merged(void);
static void ui_update_link_mark(lv_obj_t * dot, uint8_t linked, uint8_t connecting);
static void ui_update_all_link_marks(void);
static void ui_link_blink_apply(void);
static void ui_link_blink_timer_cb(lv_timer_t * timer);
static void ui_link_blink_sync(void);
static void ui_cancel_signal_lost_timer(void);
static void ui_signal_lost_timer_cb(lv_timer_t * timer);
static void ui_show_alarm_page(const char * title, const char * hint);
static void ui_show_signal_lost_page(void);
static void ui_on_nav_home(lv_event_t * e);
static void ui_on_nav_monitor(lv_event_t * e);
static void ui_on_nav_history(lv_event_t * e);
static void ui_on_measure_toggle(lv_event_t * e);
static void ui_on_goto_wifi(lv_event_t * e);
static void ui_on_home_measure(lv_event_t * e);
static void ui_on_home_profile(lv_event_t * e);
static void ui_on_home_history(lv_event_t * e);

static void ui_format_pwv_x10(int16_t pwv_x10, char *buf, size_t len)
{
    int abs_x10;

    if(buf == NULL || len == 0) {
        return;
    }
    abs_x10 = pwv_x10;
    if(abs_x10 < 0) {
        abs_x10 = -abs_x10;
    }
    snprintf(buf, len, "%d.%01d", (int)(pwv_x10 / 10), abs_x10 % 10);
}

static void ui_link_blink_apply(void)
{
    lv_opa_t blink_opa = s_link_blink_phase ? LV_OPA_COVER : LV_OPA_30;

    if(s_vitals.wrist_dot != NULL) {
        if(s_ui.w_connecting && !s_ui.w_link) {
            lv_obj_set_style_text_opa(s_vitals.wrist_dot, blink_opa, LV_PART_MAIN);
        } else if(s_ui.w_link) {
            lv_obj_set_style_text_opa(s_vitals.wrist_dot, LV_OPA_COVER, LV_PART_MAIN);
        }
    }
    if(s_vitals.finger_dot != NULL) {
        if(s_ui.f_connecting && !s_ui.f_link) {
            lv_obj_set_style_text_opa(s_vitals.finger_dot, blink_opa, LV_PART_MAIN);
        } else if(s_ui.f_link) {
            lv_obj_set_style_text_opa(s_vitals.finger_dot, LV_OPA_COVER, LV_PART_MAIN);
        }
    }
}

static void ui_link_blink_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    s_link_blink_phase ^= 1U;
    ui_link_blink_apply();
}

static void ui_link_blink_sync(void)
{
    uint8_t need_blink = 0U;

    if((s_ui.w_connecting && !s_ui.w_link) || (s_ui.f_connecting && !s_ui.f_link)) {
        need_blink = 1U;
    }

    if(need_blink) {
        if(s_link_blink_timer == NULL) {
            s_link_blink_phase = 0U;
            s_link_blink_timer = lv_timer_create(ui_link_blink_timer_cb, UI_LINK_BLINK_MS, NULL);
        }
        ui_link_blink_apply();
    } else if(s_link_blink_timer != NULL) {
        lv_timer_del(s_link_blink_timer);
        s_link_blink_timer = NULL;
        s_link_blink_phase = 0U;
        ui_link_blink_apply();
    }
}

static void ui_update_link_mark(lv_obj_t * dot, uint8_t linked, uint8_t connecting)
{
    if(dot == NULL) {
        return;
    }
    if(linked) {
        lv_label_set_text(dot, UI_STR_LINK_MARK_ON);
        lv_obj_set_style_text_font(dot, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(dot, lv_color_hex(0x2E7D32), 0);
        lv_obj_set_style_text_opa(dot, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    } else if(connecting) {
        lv_label_set_text(dot, UI_STR_LINK_MARK_ON);
        lv_obj_set_style_text_font(dot, &lv_font_montserrat_14, LV_PART_MAIN);
        lv_obj_set_style_text_color(dot, lv_color_hex(0xFF9800), 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_update_all_link_marks(void)
{
    ui_update_link_mark(s_vitals.wrist_dot,  s_ui.w_link, s_ui.w_connecting);
    ui_update_link_mark(s_vitals.finger_dot, s_ui.f_link, s_ui.f_connecting);
    ui_link_blink_sync();
}

static void ui_refresh_vitals_merged(void)
{
    char buf[40];
    uint8_t hr = 0U;
    uint8_t spo2 = 0U;
    uint8_t valid = 0U;
    uint8_t spo2_ok = 0U;
    lv_color_t color;

    if(s_vitals.vitals == NULL) {
        return;
    }

    if(s_ui.w_valid && s_ui.f_valid) {
        hr = (uint8_t)(((uint16_t)s_ui.w_hr + (uint16_t)s_ui.f_hr + 1U) / 2U);
        spo2 = (uint8_t)(((uint16_t)s_ui.w_spo2 + (uint16_t)s_ui.f_spo2 + 1U) / 2U);
        valid = 1U;
        spo2_ok = (s_ui.w_spo2 >= PWV_WRIST_SPO2_MIN &&
                   s_ui.f_spo2 >= PWV_FINGER_SPO2_MIN) ? 1U : 0U;
    } else if(s_ui.w_valid) {
        hr = s_ui.w_hr;
        spo2 = s_ui.w_spo2;
        valid = 1U;
        spo2_ok = (spo2 >= PWV_WRIST_SPO2_MIN) ? 1U : 0U;
    } else if(s_ui.f_valid) {
        hr = s_ui.f_hr;
        spo2 = s_ui.f_spo2;
        valid = 1U;
        spo2_ok = (spo2 >= PWV_FINGER_SPO2_MIN) ? 1U : 0U;
    }

    if(valid) {
        snprintf(buf, sizeof(buf), UI_STR_VITALS_LINE_FMT,
                 (unsigned)hr, (unsigned)spo2);
        lv_label_set_text(s_vitals.vitals, buf);
        color = spo2_ok ? lv_color_hex(0x2E7D32) : lv_color_hex(0xE65100);
        lv_obj_set_style_text_color(s_vitals.vitals, color, 0);
    } else {
        lv_label_set_text(s_vitals.vitals, UI_STR_VITALS_NONE);
        lv_obj_set_style_text_color(s_vitals.vitals, lv_color_hex(0x757575), 0);
    }
}

static lv_obj_t * ui_create_nav_btn(lv_obj_t * parent, const char * text, lv_event_cb_t cb)
{
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 140, 38);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x64B5F6), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_color(btn, lv_color_hex(0x1565C0), LV_PART_MAIN);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t * lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    ui_obj_set_font_cn(lbl);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(lbl);
    return btn;
}

static void ui_style_nav_btn(lv_obj_t * btn, uint8_t active)
{
    if(btn == NULL) {
        return;
    }
    if(active) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x1565C0), LV_PART_MAIN);
    }
}

static void ui_update_nav_highlight(void)
{
    if(s_active_nav == NULL) {
        return;
    }
    ui_style_nav_btn(s_active_nav->home,    s_page == UI_PAGE_HOME    ? 1U : 0U);
    ui_style_nav_btn(s_active_nav->monitor, s_page == UI_PAGE_MONITOR ? 1U : 0U);
    ui_style_nav_btn(s_active_nav->history, s_page == UI_PAGE_HISTORY ? 1U : 0U);
}

static void ui_cancel_signal_lost_timer(void)
{
    if(s_signal_lost_timer) {
        lv_timer_del(s_signal_lost_timer);
        s_signal_lost_timer = NULL;
    }
}

static void ui_signal_lost_timer_cb(lv_timer_t * timer)
{
    (void)timer;
    ui_cancel_signal_lost_timer();
    AlarmMgr_Stop();
    s_measure_status = MEASURE_UI_STATUS_NONE;
    ui_show_page(UI_PAGE_MONITOR);
}

static void ui_hide_result_overlay(void)
{
    if(s_result_overlay == NULL) {
        return;
    }
    lv_obj_del(s_result_overlay);
    s_result_overlay = NULL;
}

static void ui_on_result_back(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    ui_hide_result_overlay();
}

static void ui_show_result_overlay(void)
{
    float mean = 0.0f;
    float lo = 0.0f;
    float hi = 0.0f;
    const char * level;
    uint32_t color;
    lv_obj_t * panel;
    lv_obj_t * lbl;
    lv_obj_t * btn;
    int16_t abs_x10;

    if(s_scr_monitor == NULL) {
        return;
    }
    if(s_result_overlay != NULL) {
        return;
    }

    pwv_calib_ref_band(&mean, &lo, &hi);
    if(s_ui.pwv_valid) {
        level = pwv_calib_trend_hint(s_ui.pwv, mean, lo, hi);
        if(s_ui.pwv > hi) {
            color = 0xE65100U;
        } else if(s_ui.pwv < lo) {
            color = 0x1565C0U;
        } else {
            color = 0x2E7D32U;
        }
        abs_x10 = s_ui.pwv_x10;
        if(abs_x10 < 0) {
            abs_x10 = (int16_t)(-abs_x10);
        }
        snprintf(s_result_pwv_buf, sizeof(s_result_pwv_buf),
                 UI_STR_RESULT_PWV_FMT,
                 (int)(s_ui.pwv_x10 / 10), (int)(abs_x10 % 10));
        snprintf(s_result_ref_buf, sizeof(s_result_ref_buf),
                 UI_STR_RESULT_REF_FMT, (double)lo, (double)hi);
    } else {
        level = UI_STR_RESULT_NONE_HINT;
        color = 0x757575U;
        snprintf(s_result_pwv_buf, sizeof(s_result_pwv_buf), "PWV --");
        s_result_ref_buf[0] = '\0';
    }

    s_result_overlay = lv_obj_create(s_scr_monitor);
    lv_obj_set_size(s_result_overlay, LV_DISP_HOR_RES, LV_DISP_VER_RES);
    lv_obj_set_style_bg_color(s_result_overlay, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_result_overlay, LV_OPA_50, 0);
    lv_obj_set_style_border_width(s_result_overlay, 0, 0);
    lv_obj_set_style_pad_all(s_result_overlay, 0, 0);
    lv_obj_set_style_radius(s_result_overlay, 0, 0);
    lv_obj_clear_flag(s_result_overlay, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_result_overlay, LV_OBJ_FLAG_CLICKABLE);

    panel = lv_obj_create(s_result_overlay);
    lv_obj_set_size(panel, LV_DISP_HOR_RES - 48, 220);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 8, 0);
    lv_obj_set_style_pad_all(panel, 16, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(color), 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lbl = lv_label_create(panel);
    lv_label_set_text(lbl, UI_STR_RESULT_TITLE);
    ui_obj_set_font_cn(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1565C0), 0);

    lbl = lv_label_create(panel);
    lv_label_set_text(lbl, level);
    ui_obj_set_font_cn(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(color), 0);

    lbl = lv_label_create(panel);
    lv_label_set_text(lbl, s_result_pwv_buf);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x212121), 0);

    if(s_result_ref_buf[0] != '\0') {
        lbl = lv_label_create(panel);
        lv_label_set_text(lbl, s_result_ref_buf);
        ui_obj_set_font_cn_sm(lbl);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x616161), 0);
    }

    lbl = lv_label_create(panel);
    lv_label_set_text(lbl, UI_STR_MONITOR_DISCLAIMER);
    ui_obj_set_font_cn_sm(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x9E9E9E), 0);

    btn = lv_btn_create(panel);
    lv_obj_set_size(btn, 140, 36);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, ui_on_result_back, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, UI_STR_PROFILE_BACK);
    ui_obj_set_font_cn(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(lbl);
}

static void ui_show_alarm_page(const char * title, const char * hint)
{
    PowerMgr_OnUserActivity();
    ui_cancel_signal_lost_timer();
    s_page = UI_PAGE_SIGNAL_LOST;
    s_measure_status = MEASURE_UI_FAILED_NO_SIGNAL;
    if(s_scr_signal_lost == NULL) {
        ui_create_signal_lost_screen();
        ui_bind_alarm();
    }
    if(s_sig_lost_title && title) {
        lv_label_set_text(s_sig_lost_title, title);
    }
    if(s_sig_lost_hint && hint) {
        lv_label_set_text(s_sig_lost_hint, hint);
    }
    if(s_scr_signal_lost) {
        lv_scr_load(s_scr_signal_lost);
    }
    UI_Alarm_StartBorderBlink();
    s_signal_lost_timer = lv_timer_create(ui_signal_lost_timer_cb, UI_SIGNAL_LOST_MS, NULL);
}

static void ui_show_signal_lost_page(void)
{
    ui_show_alarm_page(UI_STR_SIG_TITLE, UI_STR_SIG_HINT);
}

static void ui_show_page(ui_page_t page)
{
    lv_obj_t * target = NULL;

    PowerMgr_OnUserActivity();
    s_page = page;

    switch(page) {
    case UI_PAGE_HOME:
        ui_cancel_signal_lost_timer();
        UI_Alarm_OnMeasureStatusCleared();
        lvgl_hal_resume();
        s_active_nav = NULL;
        ui_ensure_home_screen();
        target = s_scr_home;
        if(target != NULL && lv_scr_act() != target) {
            lv_scr_load(target);
            lv_obj_invalidate(target);
        }
        ui_release_secondary_screens();
        ui_refresh_home();
        return;
    case UI_PAGE_MONITOR:
        /* 首页常驻：只互斥释放其它副屏，避免每次重建导致切页顿挫 */
        UI_WiFi_Release();
        UI_Profile_Destroy();
        UI_History_Destroy();
        memset(&s_history_nav, 0, sizeof(s_history_nav));
        if(s_scr_monitor == NULL) {
            ui_create_monitor_screen();
            ui_bind_alarm();
        }
        target = s_scr_monitor;
        s_active_nav = &s_monitor_nav;
        s_ui.dirty = 1U;
        break;
    case UI_PAGE_HISTORY:
        UI_WiFi_Release();
        UI_Profile_Destroy();
        ui_destroy_monitor_screen();
        UI_History_Ensure();
        target = UI_History_GetScreen();
        if(target == NULL) {
            s_page = UI_PAGE_HOME;
            ui_ensure_home_screen();
            target = s_scr_home;
            s_active_nav = NULL;
            if(target != NULL) {
                lv_scr_load(target);
            }
            ui_refresh_home();
            return;
        }
        s_active_nav = &s_history_nav;
        break;
    default:
        return;
    }

    ui_update_nav_highlight();
    if(target != NULL && lv_scr_act() != target) {
        lv_scr_load(target);
        lv_obj_invalidate(target);
    }
    if(s_page == UI_PAGE_MONITOR) {
        UI_Refresh();
        if(Measure_IsResultHold()) {
            ui_show_result_overlay();
        }
    } else if(s_page == UI_PAGE_HISTORY) {
        lv_async_call(ui_history_refresh_async, NULL);
    }
}

static void ui_on_nav_home(lv_event_t * e)
{
    (void)e;
    lv_async_call(ui_nav_home_async, NULL);
}

static void ui_nav_home_async(void * user_data)
{
    (void)user_data;
    UI_ShowHome();
}

static void ui_on_nav_monitor(lv_event_t * e)
{
    (void)e;
    if(s_page == UI_PAGE_HISTORY) {
        return;
    }
    lv_async_call(ui_nav_monitor_async, NULL);
}

static void ui_on_nav_history(lv_event_t * e)
{
    (void)e;
    if(s_page == UI_PAGE_MONITOR) {
        return;
    }
    lv_async_call(ui_nav_history_async, NULL);
}

static void ui_on_goto_wifi(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    lv_async_call(ui_goto_wifi_async, NULL);
}

static void ui_update_quality_label(void)
{
    if(s_lbl_quality == NULL) {
        return;
    }
    if(s_ui.signal_quality == PWV_QUALITY_CALIBRATING) {
        lv_label_set_text(s_lbl_quality, UI_STR_QUALITY_CAL);
    } else if(s_ui.signal_quality >= 1U && s_ui.signal_quality <= 100U) {
        snprintf(s_quality_buf, sizeof(s_quality_buf),
                 UI_STR_QUALITY_FMT, (unsigned)s_ui.signal_quality);
        lv_label_set_text(s_lbl_quality, s_quality_buf);
        if(s_ui.signal_quality >= 70U) {
            lv_obj_set_style_text_color(s_lbl_quality, lv_color_hex(0x2E7D32), 0);
        } else if(s_ui.signal_quality >= 40U) {
            lv_obj_set_style_text_color(s_lbl_quality, lv_color_hex(0xE65100), 0);
        } else {
            lv_obj_set_style_text_color(s_lbl_quality, lv_color_hex(0xC62828), 0);
        }
        return;
    } else {
        lv_label_set_text(s_lbl_quality, UI_STR_QUALITY_IDLE);
    }
    lv_obj_set_style_text_color(s_lbl_quality, lv_color_hex(0x616161), 0);
}

static void ui_monitor_relayout(void)
{
    lv_coord_t y;

    if(s_scr_monitor == NULL) {
        return;
    }

    y = UI_MON_TOP_H + UI_MON_PWV_H + UI_MON_BODY_GAP;

    if(s_lbl_preflight != NULL &&
       !lv_obj_has_flag(s_lbl_preflight, LV_OBJ_FLAG_HIDDEN))
    {
        lv_obj_align(s_lbl_preflight, LV_ALIGN_TOP_MID, 0, y);
        y += UI_MON_PREFLIGHT_H + UI_MON_BODY_GAP;
    }

    if(s_mon_body != NULL) {
        lv_obj_align(s_mon_body, LV_ALIGN_TOP_MID, 0, y);
    }
}

static void ui_update_status_label(void)
{
    uint8_t mode;

    if(s_lbl_pwv_done == NULL) {
        return;
    }

    if(Measure_IsResultHold()) {
        mode = 2U;
    } else if(Measure_IsActive()) {
        mode = 1U;
    } else {
        mode = 0U;
    }
    /* 避免每次 Refresh 改 style/边框触发整卡重绘与图层分配 */
    if(mode == s_status_mode) {
        return;
    }
    s_status_mode = mode;

    if(mode == 2U) {
        lv_label_set_text(s_lbl_pwv_done, UI_STR_MEASURE_DONE);
        lv_obj_set_style_text_color(s_lbl_pwv_done, lv_color_hex(0x2E7D32), 0);
    } else if(mode == 1U) {
        lv_label_set_text(s_lbl_pwv_done, UI_STR_MEASURE_RUNNING);
        lv_obj_set_style_text_color(s_lbl_pwv_done, lv_color_hex(0x1565C0), 0);
    } else {
        lv_label_set_text(s_lbl_pwv_done, UI_STR_STATUS_IDLE);
        lv_obj_set_style_text_color(s_lbl_pwv_done, lv_color_hex(0x757575), 0);
    }
}

static void ui_update_preflight_hint(void)
{
    const char * hint;

    if(s_lbl_preflight == NULL) {
        return;
    }
    if(Measure_IsActive() || Measure_IsResultHold()) {
        if(Measure_IsActive() && s_gate_hint[0] != '\0') {
            lv_label_set_text(s_lbl_preflight, s_gate_hint);
            lv_obj_set_style_text_color(s_lbl_preflight, lv_color_hex(0xC62828), 0);
            lv_obj_clear_flag(s_lbl_preflight, LV_OBJ_FLAG_HIDDEN);
            ui_monitor_relayout();
            return;
        }
        if(Measure_IsActive() && UI_Alarm_IsWeakActive()) {
            lv_obj_clear_flag(s_lbl_preflight, LV_OBJ_FLAG_HIDDEN);
            ui_monitor_relayout();
            return;
        }
        lv_obj_add_flag(s_lbl_preflight, LV_OBJ_FLAG_HIDDEN);
        ui_monitor_relayout();
        return;
    }
    hint = UI_PreflightHint();
    if(hint[0] == '\0') {
        lv_obj_add_flag(s_lbl_preflight, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s_lbl_preflight, hint);
        lv_obj_clear_flag(s_lbl_preflight, LV_OBJ_FLAG_HIDDEN);
    }
    ui_monitor_relayout();
}

static void ui_update_measure_btn(void)
{
    uint8_t canStart;

    if(s_btn_measure == NULL || s_lbl_measure == NULL) {
        return;
    }

    if(Measure_IsActive()) {
        lv_label_set_text(s_lbl_measure, UI_STR_MEASURE_STOP);
        lv_obj_set_style_bg_color(s_btn_measure, lv_color_hex(0xC62828), 0);
        lv_obj_set_style_text_color(s_lbl_measure, lv_color_hex(0xFFFFFF), 0);
        lv_obj_clear_state(s_btn_measure, LV_STATE_DISABLED);
    } else {
        lv_label_set_text(s_lbl_measure, UI_STR_MEASURE_START);
        lv_obj_set_style_bg_color(s_btn_measure, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(s_lbl_measure, lv_color_hex(0x1565C0), 0);
        if(Measure_IsResultHold()) {
            canStart = Measure_CanStart();
        } else {
            canStart = Measure_CanStart() && Measure_PreflightOk();
        }
        if(canStart) {
            lv_obj_clear_state(s_btn_measure, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_btn_measure, LV_STATE_DISABLED);
        }
    }
    ui_update_status_label();
    ui_update_preflight_hint();
    ui_update_all_link_marks();
}

static void ui_on_measure_toggle(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    lv_async_call(ui_measure_toggle_async, NULL);
}

static void ui_measure_toggle_async(void * user_data)
{
    (void)user_data;
    if(Measure_IsActive()) {
        Measure_Stop();
    } else {
        if(!Measure_PreflightOk()) {
            AlarmMgr_Raise(ALARM_PREFLIGHT_FAIL);
            ui_update_preflight_hint();
            UI_Refresh();
            return;
        }
        Measure_Start();
    }
    UI_Refresh();
}

static lv_obj_t * ui_create_vitals_card(lv_obj_t * parent, ui_vitals_view_t * view)
{
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_t * hdr;
    lv_obj_t * title_lbl;
    lv_obj_t * link_row;
    lv_obj_t * w_lbl;
    lv_obj_t * f_lbl;
    lv_obj_t * w_slot;
    lv_obj_t * f_slot;

    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, UI_MON_CARD_H);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(card, 4, 0);
    lv_obj_set_style_pad_row(card, 2, 0);
    lv_obj_set_style_pad_hor(card, 8, 0);
    lv_obj_set_style_radius(card, 0, 0);
    lv_obj_set_style_shadow_width(card, 0, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0xA0C4E8), 0);
    lv_obj_set_style_bg_color(card, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_CLICKABLE);

    hdr = lv_obj_create(card);
    lv_obj_set_size(hdr, lv_pct(100), 24);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(hdr, 0, 0);
    lv_obj_set_style_radius(hdr, 0, 0);
    lv_obj_set_style_border_width(hdr, 0, 0);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    title_lbl = lv_label_create(hdr);
    lv_label_set_text(title_lbl, UI_STR_VITALS_TITLE);
    ui_obj_set_font_cn_sm(title_lbl);
    lv_obj_set_style_text_color(title_lbl, lv_color_hex(0x1565C0), 0);
    lv_obj_set_flex_grow(title_lbl, 1);

    link_row = lv_obj_create(hdr);
    lv_obj_set_size(link_row, 100, 24);
    lv_obj_set_flex_flow(link_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(link_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(link_row, 0, 0);
    lv_obj_set_style_pad_column(link_row, 2, 0);
    lv_obj_set_style_border_width(link_row, 0, 0);
    lv_obj_set_style_bg_opa(link_row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(link_row, LV_OBJ_FLAG_SCROLLABLE);

    w_lbl = lv_label_create(link_row);
    lv_label_set_text(w_lbl, "腕");
    ui_obj_set_font_cn_sm(w_lbl);
    lv_obj_set_style_text_color(w_lbl, lv_color_hex(0x757575), 0);

    w_slot = lv_obj_create(link_row);
    lv_obj_set_size(w_slot, UI_LINK_DOT_SLOT_W, 24);
    lv_obj_set_style_border_width(w_slot, 0, 0);
    lv_obj_set_style_bg_opa(w_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(w_slot, 0, 0);
    lv_obj_clear_flag(w_slot, LV_OBJ_FLAG_SCROLLABLE);

    view->wrist_dot = lv_label_create(w_slot);
    lv_label_set_text(view->wrist_dot, UI_STR_LINK_MARK_ON);
    lv_obj_set_style_text_font(view->wrist_dot, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->wrist_dot, lv_color_hex(0x2E7D32), 0);
    lv_obj_align(view->wrist_dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(view->wrist_dot, LV_OBJ_FLAG_HIDDEN);

    f_lbl = lv_label_create(link_row);
    lv_label_set_text(f_lbl, "指");
    ui_obj_set_font_cn_sm(f_lbl);
    lv_obj_set_style_text_color(f_lbl, lv_color_hex(0x757575), 0);

    f_slot = lv_obj_create(link_row);
    lv_obj_set_size(f_slot, UI_LINK_DOT_SLOT_W, 24);
    lv_obj_set_style_border_width(f_slot, 0, 0);
    lv_obj_set_style_bg_opa(f_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(f_slot, 0, 0);
    lv_obj_clear_flag(f_slot, LV_OBJ_FLAG_SCROLLABLE);

    view->finger_dot = lv_label_create(f_slot);
    lv_label_set_text(view->finger_dot, UI_STR_LINK_MARK_ON);
    lv_obj_set_style_text_font(view->finger_dot, &lv_font_montserrat_14, LV_PART_MAIN);
    lv_obj_set_style_text_color(view->finger_dot, lv_color_hex(0x2E7D32), 0);
    lv_obj_align(view->finger_dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(view->finger_dot, LV_OBJ_FLAG_HIDDEN);

    view->vitals = lv_label_create(card);
    lv_label_set_text(view->vitals, UI_STR_VITALS_NONE);
    ui_obj_set_font_cn_sm(view->vitals);
    lv_obj_set_style_text_color(view->vitals, lv_color_hex(0x757575), 0);
    lv_label_set_long_mode(view->vitals, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(view->vitals, lv_pct(100));
    lv_obj_set_height(view->vitals, 22);

    view->card = card;
    return card;
}

static void ui_add_bottom_nav_impl(lv_obj_t * parent, ui_page_t active, ui_nav_btns_t * out)
{
    lv_obj_t * nav;

    nav = lv_obj_create(parent);
    lv_obj_set_size(nav, LV_DISP_HOR_RES, UI_NAV_H);
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(nav, 4, 0);
    lv_obj_set_style_radius(nav, 0, 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0xFFFFFF), 0);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);

    out->home    = ui_create_nav_btn(nav, UI_STR_NAV_HOME,    ui_on_nav_home);
    out->monitor = ui_create_nav_btn(nav, UI_STR_NAV_MONITOR, ui_on_nav_monitor);
    out->history = ui_create_nav_btn(nav, UI_STR_NAV_HISTORY, ui_on_nav_history);

    ui_style_nav_btn(out->home,    active == UI_PAGE_HOME    ? 1U : 0U);
    ui_style_nav_btn(out->monitor, active == UI_PAGE_MONITOR ? 1U : 0U);
    ui_style_nav_btn(out->history, active == UI_PAGE_HISTORY ? 1U : 0U);

    if(active == UI_PAGE_HISTORY) {
        lv_obj_add_flag(out->monitor, LV_OBJ_FLAG_HIDDEN);
    } else if(active == UI_PAGE_MONITOR) {
        lv_obj_add_flag(out->history, LV_OBJ_FLAG_HIDDEN);
    }
}

void UI_AddBottomNav(lv_obj_t * parent, ui_page_t active_page)
{
    ui_add_bottom_nav_impl(parent, active_page, &s_history_nav);
}

static lv_obj_t * ui_create_home_tile(lv_obj_t * parent, const char * title,
                                      const char * subtitle, lv_event_cb_t cb,
                                      uint8_t primary)
{
    lv_obj_t * btn;
    lv_obj_t * lbl;
    lv_obj_t * sub;

    btn = lv_btn_create(parent);
    lv_obj_set_size(btn, UI_HOME_TILE_W, UI_HOME_TILE_H);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x64B5F6), 0);
    lv_obj_set_style_pad_all(btn, 8, 0);
    lv_obj_set_style_pad_row(btn, 4, 0);
    lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if(primary) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), 0);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
    }
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, title);
    ui_obj_set_font_cn(lbl);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    if(primary) {
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    } else {
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x1565C0), 0);
    }

    if(subtitle != NULL && subtitle[0] != '\0') {
        sub = lv_label_create(btn);
        lv_label_set_text(sub, subtitle);
        ui_obj_set_font_cn(sub);
        lv_obj_add_flag(sub, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_set_style_text_color(sub, lv_color_hex(0x546E7A), 0);
    }
    return btn;
}

static void ui_refresh_home(void)
{
    char profile_buf[24];
    lv_obj_t * measure_lbl;

    if(s_scr_home == NULL) {
        return;
    }

    UI_Profile_FormatSummary(profile_buf, sizeof(profile_buf));
    if(s_lbl_home_profile_sub) {
        lv_label_set_text(s_lbl_home_profile_sub, profile_buf);
    }

    measure_lbl = s_btn_home_measure ? lv_obj_get_child(s_btn_home_measure, 0) : NULL;
    if(s_btn_home_measure) {
        if(UserProfile_IsComplete()) {
            lv_obj_clear_state(s_btn_home_measure, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_btn_home_measure, lv_color_hex(0x1565C0), 0);
            if(measure_lbl) {
                lv_obj_set_style_text_color(measure_lbl, lv_color_hex(0xFFFFFF), 0);
            }
        } else {
            lv_obj_add_state(s_btn_home_measure, LV_STATE_DISABLED);
            lv_obj_set_style_bg_color(s_btn_home_measure, lv_color_hex(0xCFD8DC), 0);
            if(measure_lbl) {
                lv_obj_set_style_text_color(measure_lbl, lv_color_hex(0x455A64), 0);
            }
        }
    }
}

void UI_RefreshHome(void)
{
    ui_refresh_home();
}

static void ui_on_home_measure(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    lv_async_call(ui_home_measure_async, NULL);
}

static void ui_on_home_profile(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    lv_async_call(ui_home_profile_async, NULL);
}

static void ui_on_home_history(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    lv_async_call(ui_home_history_async, NULL);
}

static void ui_home_measure_async(void * user_data)
{
    (void)user_data;
    if(!UserProfile_IsComplete()) {
        UI_Profile_Open();
        return;
    }
    UI_ShowMonitor();
}

static void ui_home_profile_async(void * user_data)
{
    (void)user_data;
    UI_Profile_Open();
}

static void ui_home_history_async(void * user_data)
{
    (void)user_data;
    UI_ShowHistory();
}

static void ui_goto_wifi_async(void * user_data)
{
    (void)user_data;
    UI_WiFi_Open();
}

static void ui_nav_monitor_async(void * user_data)
{
    (void)user_data;
    ui_show_page(UI_PAGE_MONITOR);
}

static void ui_nav_history_async(void * user_data)
{
    (void)user_data;
    ui_show_page(UI_PAGE_HISTORY);
}

static void ui_history_refresh_async(void * user_data)
{
    (void)user_data;
    UI_History_ResetPage();
    UI_History_Refresh();
}

static void ui_create_home_screen(void)
{
    lv_obj_t * top;
    lv_obj_t * grid;
    lv_obj_t * row;
    lv_obj_t * title;
    lv_obj_t * tile;
    char       profile_buf[24];

    if(s_scr_home != NULL) {
        return;
    }

    s_scr_home = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_home, lv_color_hex(0xE3F2FD), 0);
    lv_obj_clear_flag(s_scr_home, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_scr_home, LV_OBJ_FLAG_CLICKABLE);

    top = lv_obj_create(s_scr_home);
    lv_obj_set_size(top, LV_DISP_HOR_RES, UI_HOME_TOP_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_hor(top, 12, 0);
    lv_obj_set_style_pad_ver(top, 4, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(top, 0, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x1565C0), 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_CLICKABLE);

    title = lv_label_create(top);
    lv_label_set_text(title, UI_STR_HOME_TITLE);
    ui_obj_set_font_cn(title);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    grid = lv_obj_create(s_scr_home);
    lv_obj_set_size(grid, LV_DISP_HOR_RES - 16,
                    UI_HOME_TILE_H * 2 + UI_HOME_TILE_GAP);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, UI_HOME_TOP_H + 16);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(grid, UI_HOME_TILE_GAP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(grid, LV_OBJ_FLAG_CLICKABLE);

    row = lv_obj_create(grid);
    lv_obj_set_size(row, LV_SIZE_CONTENT, UI_HOME_TILE_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UI_HOME_TILE_GAP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    s_btn_home_measure = ui_create_home_tile(row, UI_STR_HOME_MEASURE, NULL,
                                             ui_on_home_measure, 1U);

    UI_Profile_FormatSummary(profile_buf, sizeof(profile_buf));
    tile = ui_create_home_tile(row, UI_STR_HOME_PROFILE, profile_buf,
                               ui_on_home_profile, 0U);
    s_lbl_home_profile_sub = lv_obj_get_child(tile, 1);

    row = lv_obj_create(grid);
    lv_obj_set_size(row, LV_SIZE_CONTENT, UI_HOME_TILE_H);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, UI_HOME_TILE_GAP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    ui_create_home_tile(row, UI_STR_HOME_HISTORY, NULL, ui_on_home_history, 0U);
    ui_create_home_tile(row, UI_STR_HOME_WIFI, NULL, ui_on_goto_wifi, 0U);
}

static void ui_destroy_home_screen(void)
{
    lv_obj_t * scr = s_scr_home;

    if(scr == NULL) {
        return;
    }
    s_scr_home = NULL;
    s_btn_home_measure = NULL;
    s_lbl_home_profile_sub = NULL;
    ui_scr_del(scr);
}

static void ui_ensure_home_screen(void)
{
    if(s_scr_home == NULL) {
        ui_create_home_screen();
    }
}

static void ui_bind_alarm(void)
{
    ui_alarm_bind_t bind;

    memset(&bind, 0, sizeof(bind));
    bind.signal_panel = s_sig_lost_panel;
    bind.signal_title = s_sig_lost_title;
    bind.signal_hint = s_sig_lost_hint;
    if(s_scr_monitor) {
        bind.pwv_value = s_lbl_pwv_value;
        bind.preflight = s_lbl_preflight;
        bind.vitals_card = s_vitals.card;
    }
    UI_Alarm_Bind(&bind);
}

static void ui_destroy_monitor_screen(void)
{
    lv_obj_t * scr = s_scr_monitor;

    UI_Alarm_ClearWeakSignal();
    /* 父屏删除时一并释放；先置空避免二次 del */
    s_result_overlay = NULL;
    if(s_link_blink_timer != NULL) {
        lv_timer_del(s_link_blink_timer);
        s_link_blink_timer = NULL;
        s_link_blink_phase = 0U;
    }
    if(scr) {
        ui_scr_del(scr);
        s_scr_monitor = NULL;
    }
    s_btn_measure = NULL;
    s_lbl_measure = NULL;
    s_lbl_pwv_value = NULL;
    s_lbl_pwv_unit = NULL;
    s_lbl_pwv_done = NULL;
    s_lbl_status_title = NULL;
    s_pwv_panel = NULL;
    s_status_mode = 0xFFU;
    s_lbl_quality = NULL;
    s_lbl_preflight = NULL;
    s_mon_body = NULL;
    memset(&s_vitals, 0, sizeof(s_vitals));
    memset(&s_monitor_nav, 0, sizeof(s_monitor_nav));
    if(s_active_nav == &s_monitor_nav) {
        s_active_nav = NULL;
    }
    ui_bind_alarm();
}

static void ui_release_secondary_screens(void)
{
    UI_WiFi_Release();
    UI_Profile_Destroy();
    UI_History_Destroy();
    memset(&s_history_nav, 0, sizeof(s_history_nav));
    ui_destroy_monitor_screen();
    if(s_scr_signal_lost != NULL) {
        ui_scr_del(s_scr_signal_lost);
        s_scr_signal_lost = NULL;
        s_sig_lost_panel = NULL;
        s_sig_lost_title = NULL;
        s_sig_lost_hint = NULL;
        ui_bind_alarm();
    }
}

static void ui_release_secondary_async(void * user_data)
{
    (void)user_data;
    ui_release_secondary_screens();
    if(s_page == UI_PAGE_HOME) {
        ui_refresh_home();
        if(s_scr_home) {
            lv_obj_invalidate(s_scr_home);
        }
    }
}

static void ui_create_monitor_screen(void)
{
    lv_obj_t * top;
    lv_obj_t * title;
    lv_obj_t * pwv_title;
    lv_coord_t y;

    s_status_mode = 0xFFU; /* 强制首刷状态 */

    s_scr_monitor = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_monitor, lv_color_hex(0xE3F2FD), 0);
    lv_obj_clear_flag(s_scr_monitor, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_scr_monitor, LV_OBJ_FLAG_CLICKABLE);

    /* 顶栏：标题 + 开始/停止 */
    top = lv_obj_create(s_scr_monitor);
    lv_obj_set_size(top, LV_DISP_HOR_RES, UI_MON_TOP_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_hor(top, 12, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x1565C0), 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_CLICKABLE);

    title = lv_label_create(top);
    lv_label_set_text(title, UI_STR_MONITOR_TITLE);
    ui_obj_set_font_cn(title);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    s_btn_measure = lv_btn_create(top);
    lv_obj_set_size(s_btn_measure, UI_MON_BTN_W, UI_MON_BTN_H);
    lv_obj_set_style_radius(s_btn_measure, 0, 0);
    lv_obj_set_style_bg_color(s_btn_measure, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(s_btn_measure, 0, 0);
    lv_obj_add_event_cb(s_btn_measure, ui_on_measure_toggle, LV_EVENT_CLICKED, NULL);
    s_lbl_measure = lv_label_create(s_btn_measure);
    lv_label_set_text(s_lbl_measure, UI_STR_MEASURE_START);
    ui_obj_set_font_cn_sm(s_lbl_measure);
    lv_obj_set_style_text_color(s_lbl_measure, lv_color_hex(0x1565C0), 0);
    lv_obj_add_flag(s_lbl_measure, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(s_lbl_measure);

    /* PWV 面板：扁平绝对定位，去掉左/右嵌套容器（省 LVGL 堆 + 避免圆角图层） */
    y = UI_MON_TOP_H + UI_MON_BODY_GAP;
    s_pwv_panel = lv_obj_create(s_scr_monitor);
    lv_obj_set_size(s_pwv_panel, LV_DISP_HOR_RES - 16, UI_MON_PWV_H);
    lv_obj_align(s_pwv_panel, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_pad_all(s_pwv_panel, 0, 0);
    lv_obj_set_style_radius(s_pwv_panel, 0, 0);
    lv_obj_set_style_border_width(s_pwv_panel, 1, 0);
    lv_obj_set_style_border_color(s_pwv_panel, lv_color_hex(0xA0C4E8), 0);
    lv_obj_set_style_bg_color(s_pwv_panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_shadow_width(s_pwv_panel, 0, 0);
    lv_obj_clear_flag(s_pwv_panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_pwv_panel, LV_OBJ_FLAG_CLICKABLE);

    pwv_title = lv_label_create(s_pwv_panel);
    lv_label_set_text(pwv_title, "PWV");
    lv_obj_set_style_text_color(pwv_title, lv_color_hex(0x1565C0), 0);
    lv_obj_set_style_text_font(pwv_title, &lv_font_montserrat_28, 0);
    lv_obj_align(pwv_title, LV_ALIGN_LEFT_MID, 16, -10);

    s_lbl_pwv_value = lv_label_create(s_pwv_panel);
    lv_label_set_text(s_lbl_pwv_value, "--");
    lv_obj_set_style_text_color(s_lbl_pwv_value, lv_color_hex(0x757575), 0);
    lv_obj_set_style_text_font(s_lbl_pwv_value, &lv_font_montserrat_28, 0);
    lv_obj_align(s_lbl_pwv_value, LV_ALIGN_LEFT_MID, 100, -10);

    s_lbl_pwv_unit = lv_label_create(s_pwv_panel);
    lv_label_set_text(s_lbl_pwv_unit, "m/s");
    lv_obj_set_style_text_color(s_lbl_pwv_unit, lv_color_hex(0x424242), 0);
    lv_obj_set_style_text_font(s_lbl_pwv_unit, &lv_font_montserrat_14, 0);
    lv_obj_align(s_lbl_pwv_unit, LV_ALIGN_LEFT_MID, 210, -2);

    s_lbl_quality = lv_label_create(s_pwv_panel);
    lv_label_set_text(s_lbl_quality, UI_STR_QUALITY_IDLE);
    ui_obj_set_font_cn_sm(s_lbl_quality);
    lv_obj_set_style_text_color(s_lbl_quality, lv_color_hex(0x616161), 0);
    lv_obj_align(s_lbl_quality, LV_ALIGN_BOTTOM_LEFT, 18, -16);

    s_lbl_status_title = lv_label_create(s_pwv_panel);
    lv_label_set_text(s_lbl_status_title, UI_STR_STATUS_LABEL);
    ui_obj_set_font_cn_sm(s_lbl_status_title);
    lv_obj_set_style_text_color(s_lbl_status_title, lv_color_hex(0x757575), 0);
    lv_obj_align(s_lbl_status_title, LV_ALIGN_TOP_RIGHT, -18, 20);

    s_lbl_pwv_done = lv_label_create(s_pwv_panel);
    lv_label_set_text(s_lbl_pwv_done, UI_STR_STATUS_IDLE);
    ui_obj_set_font_cn_sm(s_lbl_pwv_done);
    lv_obj_set_style_text_color(s_lbl_pwv_done, lv_color_hex(0x757575), 0);
    lv_obj_set_style_text_align(s_lbl_pwv_done, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(s_lbl_pwv_done, LV_ALIGN_BOTTOM_RIGHT, -18, -18);

    y += UI_MON_PWV_H + UI_MON_BODY_GAP;
    s_lbl_preflight = lv_label_create(s_scr_monitor);
    lv_label_set_text(s_lbl_preflight, "");
    ui_obj_set_font_cn_sm(s_lbl_preflight);
    lv_obj_set_style_text_color(s_lbl_preflight, lv_color_hex(0xC62828), 0);
    lv_obj_set_size(s_lbl_preflight, LV_DISP_HOR_RES - 16, UI_MON_PREFLIGHT_H);
    lv_label_set_long_mode(s_lbl_preflight, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_lbl_preflight, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_lbl_preflight, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_add_flag(s_lbl_preflight, LV_OBJ_FLAG_HIDDEN);

    s_mon_body = lv_obj_create(s_scr_monitor);
    lv_obj_set_size(s_mon_body, LV_DISP_HOR_RES - 12, UI_MON_CARD_H + 4);
    lv_obj_set_flex_flow(s_mon_body, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_mon_body, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_all(s_mon_body, 0, 0);
    lv_obj_set_style_pad_column(s_mon_body, 0, 0);
    lv_obj_set_style_border_width(s_mon_body, 0, 0);
    lv_obj_set_style_bg_opa(s_mon_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(s_mon_body, 0, 0);
    lv_obj_clear_flag(s_mon_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_mon_body, LV_OBJ_FLAG_CLICKABLE);

    ui_create_vitals_card(s_mon_body, &s_vitals);

    ui_add_bottom_nav_impl(s_scr_monitor, UI_PAGE_MONITOR, &s_monitor_nav);
    ui_monitor_relayout();
    ui_update_status_label();
    ui_update_measure_btn();
}

static void ui_create_signal_lost_screen(void)
{
    lv_obj_t * panel;
    lv_obj_t * icon;
    lv_obj_t * title;
    lv_obj_t * hint;

    s_scr_signal_lost = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_signal_lost, lv_color_hex(0xFFF8E1), 0);
    lv_obj_clear_flag(s_scr_signal_lost, LV_OBJ_FLAG_SCROLLABLE);

    panel = lv_obj_create(s_scr_signal_lost);
    lv_obj_set_size(panel, LV_DISP_HOR_RES - 32, LV_DISP_VER_RES - 40);
    lv_obj_center(panel);
    lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(panel, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(panel, 12, 0);
    lv_obj_set_style_radius(panel, 0, 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(panel, 2, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0xFFB74D), 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    s_sig_lost_panel = panel;

    icon = lv_label_create(panel);
    lv_label_set_text(icon, LV_SYMBOL_WARNING);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xF57C00), 0);

    title = lv_label_create(panel);
    lv_label_set_text(title, UI_STR_SIG_TITLE);
    ui_obj_set_font_cn(title);
    lv_obj_set_style_text_color(title, lv_color_hex(0xE65100), 0);
    s_sig_lost_title = title;

    hint = lv_label_create(panel);
    lv_label_set_text(hint, UI_STR_SIG_HINT);
    ui_obj_set_font_cn(hint);
    lv_obj_set_style_text_align(hint, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hint, lv_color_hex(0x616161), 0);
    lv_label_set_long_mode(hint, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(hint, LV_DISP_HOR_RES - 64);
    s_sig_lost_hint = hint;
}

ui_page_t UI_GetActivePage(void)
{
    return s_page;
}

void UI_LoadHomeScreen(void)
{
    ui_ensure_home_screen();
    if(s_scr_home != NULL && lv_scr_act() != s_scr_home) {
        lv_scr_load(s_scr_home);
        lv_obj_invalidate(s_scr_home);
    }
}

void UI_UnloadHomeScreen(void)
{
    /* 仅在首页已非活动屏时删除；活动时由 ui_show_page 经 blank 过渡删除 */
    if(s_scr_home == NULL || lv_scr_act() == s_scr_home) {
        return;
    }
    ui_destroy_home_screen();
}

void UI_ReturnHomeFromOverlay(void)
{
    PowerMgr_OnUserActivity();
    UI_ShowHome();
}

void UI_ReleaseSecondaryScreens(void)
{
    ui_release_secondary_screens();
}

void UI_ShowHome(void)
{
    ui_show_page(UI_PAGE_HOME);
}

void UI_ShowMonitor(void)
{
    ui_show_page(UI_PAGE_MONITOR);
}

void UI_ShowHistory(void)
{
    ui_show_page(UI_PAGE_HISTORY);
}

void UI_NotifyHistoryDirty(void)
{
    UI_History_NotifyDirty();
}

void UI_NotifyMeasureState(void)
{
    s_ui.dirty = 1U;
    if(Measure_IsResultHold()) {
        if(s_page != UI_PAGE_MONITOR) {
            ui_show_page(UI_PAGE_MONITOR);
        } else if(s_scr_monitor == NULL) {
            ui_create_monitor_screen();
            ui_bind_alarm();
        }
        ui_show_result_overlay();
        UI_Refresh();
    } else {
        ui_hide_result_overlay();
        if(s_page == UI_PAGE_MONITOR) {
            UI_Refresh();
        }
    }
}

void UI_ClearVitals(void)
{
    s_ui.w_hr   = 0U; s_ui.f_hr   = 0U;
    s_ui.w_spo2 = 0U; s_ui.f_spo2 = 0U;
    s_ui.w_valid = 0U; s_ui.f_valid = 0U;
    s_ui.pwv = 0.0f;  s_ui.pwv_x10 = 0;
    s_ui.pwv_valid = 0U;
    s_ui.signal_quality = PWV_QUALITY_IDLE;
    s_gate_hint[0] = '\0';
    s_w_hr_disp = 0U;
    s_f_hr_disp = 0U;
    s_ui.dirty = 1U;
}

static uint8_t ui_hr_display_step(uint8_t *display, uint8_t sample)
{
    int delta;

    if(sample == 0U)
    {
        return *display;
    }
    if(*display == 0U)
    {
        *display = sample;
        return sample;
    }
    /* 活动 HR：加快 UI 跟随，保留窗间变化 */
    if(sample >= 76U)
    {
        *display = (uint8_t)((*display * 3U + sample * 7U + 5U) / 10U);
        return *display;
    }
    delta = (int)sample - (int)(*display);
    if(delta >= 10)
    {
        *display = (uint8_t)(*display + (delta * 3) / 4);
    }
    else if(delta <= -8)
    {
        *display = (uint8_t)(*display + (delta * 2) / 10);
    }
    else
    {
        *display = (uint8_t)((*display * 5U + sample * 5U + 5U) / 10U);
    }
    return *display;
}

void UI_ApplySessionResult(const pwv_session_t *snap)
{
    if(snap == NULL) {
        return;
    }
    if(snap->w_hr || snap->w_spo2) {
        s_ui.w_hr = snap->w_hr;
        s_ui.w_spo2 = snap->w_spo2;
        s_ui.w_valid = 1U;
    }
    if(snap->f_hr || snap->f_spo2) {
        s_ui.f_hr = snap->f_hr;
        s_ui.f_spo2 = snap->f_spo2;
        s_ui.f_valid = 1U;
    }
    if(snap->pwv_valid) {
        s_ui.pwv = snap->pwv;
        s_ui.pwv_x10 = (int16_t)(snap->pwv * 10.0f + 0.05f);
        s_ui.pwv_valid = 1U;
    }
    s_ui.signal_quality = snap->signal_quality;
    s_ui.dirty = 1U;
    if(s_page == UI_PAGE_MONITOR) {
        ui_update_measure_btn();
    }
}

void UI_SetMeasureStatus(uint8_t code)
{
    if(code == MEASURE_UI_FAILED_NO_SIGNAL) {
        ui_show_signal_lost_page();
        return;
    }
    if(code == MEASURE_UI_FAILED_BLE_LOST) {
        ui_show_alarm_page(UI_STR_BLE_TITLE, UI_STR_BLE_HINT);
        return;
    }
    s_measure_status = code;
    s_ui.dirty = 1U;
}

void UI_ClearMeasureStatus(void)
{
    ui_cancel_signal_lost_timer();
    UI_Alarm_OnMeasureStatusCleared();
    s_measure_status = MEASURE_UI_STATUS_NONE;
    s_ui.dirty = 1U;
}

uint8_t UI_PreflightOk(void)
{
    if(!UserProfile_IsComplete()) {
        return 0U;
    }
    if(!s_ui.w_link || !s_ui.f_link) {
        return 0U;
    }
    /* 测量前尚无 paced vitals；两路已连接即可 Start，逐拍门控在 PWV 层 */
    if(!s_ui.w_valid) {
        return 1U;
    }
    if(s_ui.w_spo2 < PWV_WRIST_SPO2_MIN) {
        return 0U;
    }
    if(s_ui.w_hr < PWV_WRIST_HR_PRODUCT_MIN || s_ui.w_hr > PWV_WRIST_HR_PRODUCT_MAX) {
        return 0U;
    }
    if(s_ui.f_valid && s_ui.f_spo2 < PWV_FINGER_SPO2_MIN) {
        return 0U;
    }
    return 1U;
}

const char * UI_PreflightHint(void)
{
    if(!Measure_CanStart()) {
        return "";
    }
    if(!UserProfile_IsComplete()) {
        return UI_STR_PREFLIGHT_PROFILE;
    }
    if(UI_PreflightOk()) {
        return "";
    }
    if(!s_ui.w_link) {
        return UI_STR_PREFLIGHT_WRIST;
    }
    if(!s_ui.f_link) {
        return UI_STR_PREFLIGHT_FINGER_LINK;
    }
    if(s_ui.w_valid && s_ui.w_spo2 < PWV_WRIST_SPO2_MIN) {
        snprintf(s_preflight_buf, sizeof(s_preflight_buf),
                 UI_STR_PREFLIGHT_SPO2_FMT,
                 (unsigned)s_ui.w_spo2, (unsigned)PWV_WRIST_SPO2_MIN);
        return s_preflight_buf;
    }
    if(s_ui.w_valid &&
       (s_ui.w_hr < PWV_WRIST_HR_PRODUCT_MIN || s_ui.w_hr > PWV_WRIST_HR_PRODUCT_MAX)) {
        snprintf(s_preflight_buf, sizeof(s_preflight_buf),
                 UI_STR_PREFLIGHT_HR_FMT,
                 (unsigned)s_ui.w_hr,
                 (unsigned)PWV_WRIST_HR_PRODUCT_MIN,
                 (unsigned)PWV_WRIST_HR_PRODUCT_MAX);
        return s_preflight_buf;
    }
    if(s_ui.f_valid && s_ui.f_spo2 < PWV_FINGER_SPO2_MIN) {
        snprintf(s_preflight_buf, sizeof(s_preflight_buf),
                 UI_STR_PREFLIGHT_FINGER_FMT,
                 (unsigned)s_ui.f_spo2, (unsigned)PWV_FINGER_SPO2_MIN);
        return s_preflight_buf;
    }
    return "";
}

void UI_Init(void)
{
    s_page = UI_PAGE_HOME;
    s_measure_status = MEASURE_UI_STATUS_NONE;
    s_signal_lost_timer = NULL;
    s_active_nav = NULL;
    memset(&s_ui, 0, sizeof(s_ui));
    s_gate_hint[0] = '\0';
    memset(&s_monitor_nav, 0, sizeof(s_monitor_nav));
    memset(&s_history_nav, 0, sizeof(s_history_nav));

    ui_create_home_screen();
    UI_Profile_Init();
    UI_History_Init();
    UI_WiFi_Init();
    ui_bind_alarm();

    lv_scr_load(s_scr_home);
    ui_refresh_home();
    lv_obj_invalidate(s_scr_home);
}

void UI_Refresh(void)
{
    char buf[16];

    if(s_page == UI_PAGE_HOME) {
        if(s_ui.dirty) {
            s_ui.dirty = 0U;
            ui_refresh_home();
        }
        return;
    }

    if(s_page != UI_PAGE_MONITOR || !s_ui.dirty || s_scr_monitor == NULL) {
        return;
    }
    s_ui.dirty = 0U;

    ui_refresh_vitals_merged();
    ui_update_all_link_marks();

    if(s_ui.pwv_valid) {
        ui_format_pwv_x10(s_ui.pwv_x10, buf, sizeof(buf));
        lv_label_set_text(s_lbl_pwv_value, buf);
        lv_obj_set_style_text_color(s_lbl_pwv_value,
                                    lv_color_hex(UI_Alarm_GetPwvColor()), 0);
    } else {
        lv_label_set_text(s_lbl_pwv_value, "--");
        lv_obj_set_style_text_color(s_lbl_pwv_value, lv_color_hex(0x757575), 0);
    }

    ui_update_status_label();
    ui_update_quality_label();
    ui_update_measure_btn();
}

void UI_SetWrist(uint8_t hr, uint8_t spo2)
{
    /* RESULT_HOLD：冻结 vitals，避免结束后晚包改写定稿 */
    if(Measure_IsResultHold()) {
        return;
    }
    if(hr > 0U) {
        s_ui.w_hr = ui_hr_display_step(&s_w_hr_disp, hr);
    }
    s_ui.w_spo2 = spo2;
    s_ui.w_valid = 1U; s_ui.dirty = 1U;
}

void UI_SetFinger(uint8_t hr, uint8_t spo2)
{
    if(Measure_IsResultHold()) {
        return;
    }
    if(hr > 0U) {
        s_ui.f_hr = ui_hr_display_step(&s_f_hr_disp, hr);
    }
    s_ui.f_spo2 = spo2;
    s_ui.f_valid = 1U; s_ui.dirty = 1U;
}

void UI_SetPwv(float pwv_mps)
{
    /* 测量中刷会话中位；结束后由 ApplySessionResult 定稿并冻结 */
    if(Measure_IsResultHold()) {
        return;
    }
    s_ui.pwv = pwv_mps;
    s_ui.pwv_x10 = (int16_t)(pwv_mps * 10.0f + 0.05f);
    s_ui.pwv_valid = 1U;
    s_ui.dirty = 1U;
}

void UI_SetSignalQuality(uint8_t quality_pct)
{
    if(Measure_IsResultHold()) {
        return;
    }
    s_ui.signal_quality = quality_pct;
    s_ui.dirty = 1U;
    if(s_page == UI_PAGE_MONITOR && s_scr_monitor != NULL) {
        ui_update_quality_label();
    }
}

void UI_SetGateHint(const char * hint)
{
    if(hint == NULL || hint[0] == '\0') {
        s_gate_hint[0] = '\0';
    } else {
        snprintf(s_gate_hint, sizeof(s_gate_hint), "%s", hint);
    }
    s_ui.dirty = 1U;
    if(s_page == UI_PAGE_MONITOR && s_scr_monitor != NULL) {
        ui_update_preflight_hint();
    }
}

void UI_ClearGateHint(void)
{
    s_gate_hint[0] = '\0';
    s_ui.dirty = 1U;
}

void UI_SetLink(uint8_t wrist_ok, uint8_t finger_ok,
                uint8_t wrist_connecting, uint8_t finger_connecting)
{
    s_ui.w_link = wrist_ok ? 1U : 0U;
    s_ui.f_link = finger_ok ? 1U : 0U;
    s_ui.w_connecting = wrist_connecting ? 1U : 0U;
    s_ui.f_connecting = finger_connecting ? 1U : 0U;
    s_ui.dirty = 1U;
    if(s_scr_monitor != NULL) {
        ui_update_all_link_marks();
    }
}
