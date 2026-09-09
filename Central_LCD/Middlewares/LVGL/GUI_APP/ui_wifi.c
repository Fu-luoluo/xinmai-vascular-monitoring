#include "ui_wifi.h"
#include "ui.h"
#include "ui_profile.h"
#include "ui_history.h"
#include "ui_strings.h"
#include "ui_font.h"
#include "host_link.h"
#include "bsp_uart.h"
#include "measure_ctrl.h"
#include "power_mgr.h"
#include "lv_port_disp.h"
#include "lvgl.h"
#include "CONFIG.h"
#include "ui_scr_del.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_WIFI_AP_MAX       8
#define UI_WIFI_SSID_MAX     32
#define UI_WIFI_PASS_MAX     64
#define UI_WIFI_TOP_H        40
#define UI_WIFI_PASS_PAD     8
#define UI_WIFI_PASS_TA_H    34
#define UI_WIFI_PASS_BTN_H   38
#define UI_WIFI_PASS_TITLE_H 22
#define UI_WIFI_PASS_GAP     4
#define UI_WIFI_PASS_PANEL_Y (UI_WIFI_TOP_H + 4)
#define UI_WIFI_PASS_PANEL_H (LV_DISP_VER_RES - UI_WIFI_PASS_PANEL_Y)
#define UI_WIFI_PASS_KB_Y    (UI_WIFI_PASS_PAD + UI_WIFI_PASS_TITLE_H + UI_WIFI_PASS_GAP + \
                              UI_WIFI_PASS_TA_H + UI_WIFI_PASS_GAP + UI_WIFI_PASS_TA_H + UI_WIFI_PASS_GAP)
#define UI_WIFI_PASS_KB_H    (UI_WIFI_PASS_PANEL_H - UI_WIFI_PASS_KB_Y - UI_WIFI_PASS_GAP - \
                              UI_WIFI_PASS_BTN_H - UI_WIFI_PASS_PAD)

typedef struct {
    char    ssid[UI_WIFI_SSID_MAX + 1];
    int16_t rssi;
    uint8_t ch;
} ui_wifi_ap_t;

typedef enum {
    UI_WIFI_VIEW_LIST = 0,
    UI_WIFI_VIEW_PASS
} ui_wifi_view_t;

typedef enum {
    UI_WIFI_CONN_IDLE = 0,
    UI_WIFI_CONN_PENDING,
    UI_WIFI_CONN_CONNECTED
} ui_wifi_conn_phase_t;

static lv_obj_t *     s_scr_wifi;
static lv_obj_t *     s_lbl_wifi_title;
static lv_obj_t *     s_lbl_wifi_hint;
static lv_obj_t *     s_lbl_wifi_status;
static lv_obj_t *     s_list_wifi;
static lv_obj_t *     s_btn_rescan;
static lv_obj_t *     s_btn_manual;
static lv_obj_t *     s_btn_forget;
static lv_obj_t *     s_ap_btns[UI_WIFI_AP_MAX];
static lv_obj_t *     s_ap_lbls[UI_WIFI_AP_MAX];
static lv_obj_t *     s_panel_pass;
static lv_obj_t *     s_lbl_pass_ssid;
static lv_obj_t *     s_ta_ssid;
static lv_obj_t *     s_ta_pass;
static lv_obj_t *     s_kb_matrix;
static lv_obj_t *     s_btn_pass_connect;
static lv_obj_t *     s_btn_pass_cancel;
static lv_obj_t *     s_kb_target;
static lv_obj_t *     s_lbl_wifi_block;
static lv_obj_t *     s_lbl_wifi_dbg;
static lv_obj_t *     s_lbl_wifi_dbg2;

static ui_wifi_ap_t   s_aps[UI_WIFI_AP_MAX];
static uint8_t        s_ap_count = 0;
static ui_wifi_view_t s_wifi_view = UI_WIFI_VIEW_LIST;
static char           s_sel_ssid[UI_WIFI_SSID_MAX + 1];
static uint8_t        s_wifi_active = 0;
static uint8_t        s_wifi_connected = 0;
static char           s_wifi_ssid[UI_WIFI_SSID_MAX + 1];
static lv_obj_t *     s_return_scr = NULL;

#define UI_WIFI_SCAN_MIN_MS      3000U
#define UI_WIFI_SCAN_RETRY_MS    5000U
#define UI_WIFI_SCAN_TIMEOUT_MS  20000U
/*
 * ESP32-C3 在部分 AP 上会经历多轮认证重试后才获取 IP。
 * 与网关端 120 秒连接窗口保持一致，避免 UI 过早显示超时。
 */
#define UI_WIFI_CONNECT_TIMEOUT_MS 120000U

static uint32_t       s_last_scan_tick = 0U;
static uint8_t        s_wifi_scan_active = 0U;
static uint8_t        s_list_apply_pending = 0U;
static uint32_t       s_dbg_last_b = 0U;
static uint32_t       s_dbg_last_l = 0U;
static uint32_t       s_dbg_last_w = 0U;
static uint32_t       s_dbg_last_s = 0U;
static uint8_t        s_dbg_last_ap = 0U;
static char           s_last_wifi_head[56];
static int16_t        s_last_wifi_count = -1;
static uint8_t        s_last_wifi_parsed = 0U;

static uint8_t        s_wifi_ui_ready = 0U;
static uint8_t        s_pass_panel_ready = 0U;

static ui_wifi_conn_phase_t s_wifi_conn_phase = UI_WIFI_CONN_IDLE;
static uint32_t             s_last_status_poll_tick = 0U;
static uint32_t             s_wifi_conn_start_tick = 0U;

static char           s_wifi_line_buf[BSP_UART1_RX_LINE_MAX];
static char           s_pass_ssid_pending[UI_WIFI_SSID_MAX + 1];

static const char * s_kb_map[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "\n",
    "q", "w", "e", "r", "t", "y", "u", "i", "o", "p", "\n",
    "a", "s", "d", "f", "g", "h", "j", "k", "l", "\n",
    "z", "x", "c", "v", "b", "n", "m", ".", "_", LV_SYMBOL_BACKSPACE, ""
};

static void ui_wifi_build_list_ui(void);
static void ui_wifi_build_pass_panel(void);
static void ui_wifi_destroy_pass_panel(void);
static void ui_wifi_destroy_ui(void);
static void ui_wifi_clear_widget_ptrs(void);
static uint8_t ui_wifi_ensure_ui(void);
static void ui_wifi_show_list_view(void);
static void ui_wifi_show_pass_view(const char *ssid);
static void ui_wifi_refresh_list(void);
static void ui_wifi_set_status_text(const char *text);
static void ui_wifi_set_idle_hint(void);
static const char * ui_wifi_json_value(const char *json, const char *key);
static void ui_wifi_parse_list(const char *json);
static void ui_wifi_parse_status(const char *json);
static void ui_wifi_on_list_click(lv_event_t * e);
static void ui_wifi_on_manual(lv_event_t * e);
static void ui_wifi_on_pass_connect(lv_event_t * e);
static void ui_wifi_on_pass_cancel(lv_event_t * e);
static void ui_wifi_on_ta_focus(lv_event_t * e);
static void ui_wifi_update_debug_label(void);
static void ui_wifi_go_home_async(void * user_data);
static void ui_wifi_pass_cancel_async(void * user_data);
static void ui_wifi_pass_open_async(void * user_data);
static void ui_wifi_destroy_ap_buttons(void);
static uint8_t ui_wifi_aps_changed(const ui_wifi_ap_t *new_aps, uint8_t new_count);

static uint8_t ui_wifi_aps_changed(const ui_wifi_ap_t *new_aps, uint8_t new_count)
{
    uint8_t i;

    if(new_count != s_ap_count) {
        return 1U;
    }
    for(i = 0U; i < new_count; i++) {
        if(strcmp(new_aps[i].ssid, s_aps[i].ssid) != 0 ||
           new_aps[i].rssi != s_aps[i].rssi) {
            return 1U;
        }
    }
    return 0U;
}

static void ui_wifi_update_debug_label(void)
{
    char     buf[64];
    uint32_t b;
    uint32_t l;
    uint32_t w;
    uint32_t s;
    uint32_t d;

    if(s_lbl_wifi_dbg == NULL || s_wifi_view != UI_WIFI_VIEW_LIST) {
        return;
    }
    b = BSP_UART1_GetRxByteCount();
    l = HostLink_GetRxLineCount();
    w = HostLink_GetWifiListCount();
    s = HostLink_GetWifiStatusCount();
    d = HostLink_GetRxDropCount();
    if(b == s_dbg_last_b && l == s_dbg_last_l && w == s_dbg_last_w &&
       s == s_dbg_last_s && s_ap_count == s_dbg_last_ap) {
        return;
    }
    s_dbg_last_b = b;
    s_dbg_last_l = l;
    s_dbg_last_w = w;
    s_dbg_last_s = s;
    s_dbg_last_ap = s_ap_count;
    snprintf(buf, sizeof(buf), "B:%lu L:%lu W:%lu S:%lu D:%lu AP:%u",
             (unsigned long)b, (unsigned long)l, (unsigned long)w,
             (unsigned long)s, (unsigned long)d, (unsigned)s_ap_count);
    lv_label_set_text(s_lbl_wifi_dbg, buf);
}

static void ui_wifi_request_scan(uint8_t force)
{
    uint32_t now = TMOS_GetSystemClock();

    if(!force && s_last_scan_tick != 0U &&
       (now - s_last_scan_tick) < MS1_TO_SYSTEM_TIME(UI_WIFI_SCAN_MIN_MS)) {
        return;
    }
    s_last_scan_tick = now;
    s_wifi_scan_active = 1U;
    HostLink_CmdWifiScan();
}

static void ui_wifi_set_status_text(const char *text)
{
    if(s_lbl_wifi_status) {
        lv_label_set_text(s_lbl_wifi_status, text ? text : "");
        ui_obj_set_font_cn(s_lbl_wifi_status);
    }
}

static void ui_wifi_set_idle_hint(void)
{
    if(s_wifi_conn_phase != UI_WIFI_CONN_IDLE) {
        return;
    }
    if(s_ap_count > 0U) {
        ui_wifi_set_status_text(UI_STR_WIFI_TAP_HINT);
    } else if(s_wifi_scan_active) {
        ui_wifi_set_status_text(UI_STR_WIFI_SCANNING);
    } else {
        ui_wifi_set_status_text(UI_STR_WIFI_NO_24G);
    }
}

static const char * ui_wifi_json_value(const char *json, const char *key)
{
    static char s_val[48];
    char        pat[24];
    const char *p;
    const char *q;
    size_t      n;

    snprintf(pat, sizeof(pat), "\"%s\":\"", key);
    p = strstr(json, pat);
    if(p == NULL) {
        return NULL;
    }
    p += strlen(pat);
    q = strchr(p, '\"');
    if(q == NULL) {
        return NULL;
    }
    n = (size_t)(q - p);
    if(n >= sizeof(s_val)) {
        n = sizeof(s_val) - 1U;
    }
    memcpy(s_val, p, n);
    s_val[n] = '\0';
    return s_val;
}

static void ui_wifi_parse_status(const char *json)
{
    const char *state = ui_wifi_json_value(json, "state");
    const char *ssid;
    const char *ip;
    char         buf[96];

    if(state == NULL) {
        return;
    }

    if(strcmp(state, "connected") == 0) {
        char ssid_buf[48];
        ssid = ui_wifi_json_value(json, "ssid");
        strncpy(ssid_buf, ssid ? ssid : "?", sizeof(ssid_buf) - 1U);
        ssid_buf[sizeof(ssid_buf) - 1U] = '\0';
        ip = ui_wifi_json_value(json, "ip");
        snprintf(buf, sizeof(buf), UI_STR_WIFI_CONNECTED_FMT,
                 ssid_buf, ip ? ip : "");
        if(s_wifi_active) {
            ui_wifi_set_status_text(buf);
        }
        s_wifi_conn_phase = UI_WIFI_CONN_CONNECTED;
        s_wifi_conn_start_tick = 0U;
        s_wifi_connected = 1U;
        strncpy(s_wifi_ssid, ssid_buf, UI_WIFI_SSID_MAX);
        s_wifi_ssid[UI_WIFI_SSID_MAX] = '\0';
        UI_RefreshHome();
    } else if(strcmp(state, "scanning") == 0) {
        (void)state;
    } else if(strcmp(state, "connecting") == 0) {
        const char *show_ssid;
        ssid = ui_wifi_json_value(json, "ssid");
        if(ssid != NULL && ssid[0] != '\0') {
            show_ssid = ssid;
        } else if(s_sel_ssid[0] != '\0') {
            show_ssid = s_sel_ssid;
        } else {
            show_ssid = "...";
        }
        if(s_wifi_active && s_wifi_conn_phase == UI_WIFI_CONN_PENDING) {
            snprintf(buf, sizeof(buf), UI_STR_WIFI_CONNECTING_FMT, show_ssid);
            ui_wifi_set_status_text(buf);
        }
        if(ssid != NULL && ssid[0] != '\0') {
            s_wifi_conn_phase = UI_WIFI_CONN_PENDING;
        }
    } else if(strcmp(state, "failed") == 0) {
        const char *reason = ui_wifi_json_value(json, "reason");
        if(s_wifi_active) {
            snprintf(buf, sizeof(buf), UI_STR_WIFI_FAILED_FMT, reason ? reason : "error");
            ui_wifi_set_status_text(buf);
            ui_wifi_show_list_view();
        }
        s_wifi_conn_phase = UI_WIFI_CONN_IDLE;
        s_wifi_conn_start_tick = 0U;
        s_wifi_connected = 0U;
        s_wifi_ssid[0] = '\0';
        UI_RefreshHome();
    } else if(strcmp(state, "no_config") == 0) {
        if(s_wifi_scan_active || s_wifi_conn_phase == UI_WIFI_CONN_PENDING) {
            return;
        }
        if(s_ap_count == 0U) {
            ui_wifi_set_status_text(UI_STR_WIFI_NO_CFG);
        }
        s_wifi_conn_phase = UI_WIFI_CONN_IDLE;
        s_wifi_connected = 0U;
        s_wifi_ssid[0] = '\0';
        if(!s_wifi_active) {
            UI_RefreshHome();
        }
    } else {
        ui_wifi_set_status_text(state);
    }
}

static void ui_wifi_parse_list(const char *json)
{
    const char *p;
    const char *ss;
    const char *rp;
    const char *q;
    ui_wifi_ap_t tmp[UI_WIFI_AP_MAX];
    uint8_t      i = 0U;
    long         expect_count = -1;
    size_t       n;
    uint8_t      j = 0U;

    if(json == NULL) {
        return;
    }
    for(j = 0U; j < (sizeof(s_last_wifi_head) - 1U) && json[j] != '\0'; j++) {
        char c = json[j];
        if(c < 32 || c > 126) {
            s_last_wifi_head[j] = '.';
        } else {
            s_last_wifi_head[j] = c;
        }
    }
    s_last_wifi_head[j] = '\0';

    p = strstr(json, "\"aps\":[");
    if(p == NULL) {
        return;
    }
    p = strchr(p, '[');
    if(p == NULL) {
        return;
    }
    p++;
    ss = strstr(json, "\"count\":");
    if(ss != NULL) {
        expect_count = strtol(ss + 8, NULL, 10);
    }
    s_last_wifi_count = (int16_t)expect_count;
    while(i < UI_WIFI_AP_MAX && (ss = strstr(p, "\"ssid\":\"")) != NULL) {
        ss += 8;
        q = strchr(ss, '\"');
        if(q == NULL) {
            break;
        }
        n = (size_t)(q - ss);
        if(n > UI_WIFI_SSID_MAX) {
            n = UI_WIFI_SSID_MAX;
        }
        memcpy(tmp[i].ssid, ss, n);
        tmp[i].ssid[n] = '\0';

        rp = strstr(q, "\"rssi\":");
        if(rp == NULL) {
            rp = strstr(q, "rssi\":");
        }
        if(rp != NULL) {
            rp = strchr(rp, ':');
        }
        if(rp != NULL) {
            tmp[i].rssi = (int16_t)strtol(rp + 1, NULL, 10);
        } else {
            tmp[i].rssi = -99;
        }
        tmp[i].ch = 0U;
        i++;
        p = q;
    }

    if(expect_count > 0 && i == 0U) {
        s_last_wifi_parsed = 0U;
        if(s_wifi_active && s_wifi_view == UI_WIFI_VIEW_LIST) {
            ui_wifi_set_status_text("WiFi列表解析失败,请重扫");
        }
        return;
    }

    s_wifi_scan_active = 0U;
    s_last_wifi_parsed = i;
    if(!ui_wifi_aps_changed(tmp, i)) {
        if(s_wifi_active && s_wifi_conn_phase == UI_WIFI_CONN_IDLE) {
            if(i == 0U) {
                ui_wifi_set_status_text(UI_STR_WIFI_NO_24G);
            } else if(s_ap_count > 0U) {
                ui_wifi_set_idle_hint();
            }
        }
        return;
    }

    memcpy(s_aps, tmp, (size_t)i * sizeof(ui_wifi_ap_t));
    s_ap_count = i;
    s_list_apply_pending = 1U;
}

static void ui_wifi_destroy_ap_buttons(void)
{
    uint8_t i;

    for(i = 0U; i < UI_WIFI_AP_MAX; i++) {
        if(s_ap_btns[i] != NULL) {
            lv_obj_del(s_ap_btns[i]);
            s_ap_btns[i] = NULL;
            s_ap_lbls[i] = NULL;
        }
    }
}

static void ui_wifi_pass_open_async(void * user_data)
{
    (void)user_data;
    ui_wifi_show_pass_view(s_pass_ssid_pending);
}

static void ui_wifi_queue_pass_view(const char * ssid)
{
    if(ssid != NULL) {
        strncpy(s_pass_ssid_pending, ssid, UI_WIFI_SSID_MAX);
    } else {
        s_pass_ssid_pending[0] = '\0';
    }
    s_pass_ssid_pending[UI_WIFI_SSID_MAX] = '\0';
    lv_async_call(ui_wifi_pass_open_async, NULL);
}

static void ui_wifi_refresh_list(void)
{
    char    buf[48];
    uint8_t i;
    lv_obj_t *btn;
    lv_obj_t *lbl;

    if(s_list_wifi == NULL) {
        return;
    }
    if(s_ap_count <= 5U) {
        lv_obj_clear_flag(s_list_wifi, LV_OBJ_FLAG_SCROLLABLE);
    } else {
        lv_obj_add_flag(s_list_wifi, LV_OBJ_FLAG_SCROLLABLE);
    }
    for(i = 0U; i < UI_WIFI_AP_MAX; i++) {
        if(i < s_ap_count) {
            if(s_ap_btns[i] == NULL) {
                btn = lv_btn_create(s_list_wifi);
                lv_obj_set_width(btn, LV_PCT(100));
                lv_obj_set_height(btn, 32);
                lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
                lv_obj_set_style_border_width(btn, 1, 0);
                lv_obj_set_style_border_color(btn, lv_color_hex(0x90CAF9), 0);
                lv_obj_set_user_data(btn, (void *)(uintptr_t)i);
                lv_obj_add_event_cb(btn, ui_wifi_on_list_click, LV_EVENT_CLICKED, NULL);

                lbl = lv_label_create(btn);
                lv_obj_set_style_text_color(lbl, lv_color_hex(0x0D47A1), 0);
                ui_obj_set_font_cn(lbl);
                lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
                lv_obj_center(lbl);

                s_ap_btns[i] = btn;
                s_ap_lbls[i] = lbl;
            }
            snprintf(buf, sizeof(buf), "%s  (%d dBm)", s_aps[i].ssid, (int)s_aps[i].rssi);
            if(s_ap_lbls[i]) {
                lv_label_set_text(s_ap_lbls[i], buf);
            }
            lv_obj_clear_flag(s_ap_btns[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_ap_btns[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void UI_WiFi_ApplyPending(void)
{
    if(!s_list_apply_pending) {
        return;
    }
    if(s_wifi_view == UI_WIFI_VIEW_PASS) {
        return;
    }
    s_list_apply_pending = 0U;
    ui_wifi_refresh_list();
    if(!s_wifi_active) {
        return;
    }
    if(s_ap_count == 0U) {
        if(s_wifi_conn_phase == UI_WIFI_CONN_IDLE) {
            ui_wifi_set_status_text(UI_STR_WIFI_NO_24G);
        }
    } else {
        ui_wifi_set_idle_hint();
    }
}

static void ui_wifi_hide_pass_panel(void)
{
    s_kb_target = NULL;
    if(s_panel_pass) {
        lv_obj_add_flag(s_panel_pass, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_wifi_show_list_view(void)
{
    s_wifi_view = UI_WIFI_VIEW_LIST;
    if(s_lbl_wifi_hint) {
        lv_obj_clear_flag(s_lbl_wifi_hint, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_lbl_wifi_status) {
        lv_obj_clear_flag(s_lbl_wifi_status, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_lbl_wifi_dbg) {
        lv_obj_clear_flag(s_lbl_wifi_dbg, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_lbl_wifi_dbg2) {
        lv_obj_add_flag(s_lbl_wifi_dbg2, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_btn_rescan) {
        lv_obj_clear_flag(s_btn_rescan, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_btn_manual) {
        lv_obj_clear_flag(s_btn_manual, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_btn_forget) {
        lv_obj_clear_flag(s_btn_forget, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_list_wifi) {
        lv_obj_clear_flag(s_list_wifi, LV_OBJ_FLAG_HIDDEN);
    }
    ui_wifi_destroy_pass_panel();
    if(s_list_apply_pending) {
        UI_WiFi_ApplyPending();
    } else if(s_ap_count > 0U) {
        ui_wifi_refresh_list();
    }
}

static void ui_wifi_show_pass_view(const char *ssid)
{
    char hint[48];

    ui_wifi_ensure_ui();
    if(s_scr_wifi == NULL) {
        return;
    }

    /* 先隐藏列表并释放 AP 按钮，给密码面板腾出 LVGL 堆 */
    if(s_list_wifi) {
        lv_obj_add_flag(s_list_wifi, LV_OBJ_FLAG_HIDDEN);
    }
    ui_wifi_destroy_ap_buttons();

    if(!s_pass_panel_ready || s_panel_pass == NULL) {
        ui_wifi_build_pass_panel();
    }
    if(s_panel_pass == NULL) {
        ui_wifi_show_list_view();
        ui_wifi_set_status_text(UI_STR_WIFI_ERR_OOM);
        return;
    }

    s_wifi_view = UI_WIFI_VIEW_PASS;
    s_wifi_scan_active = 0U;
    strncpy(s_sel_ssid, ssid ? ssid : "", UI_WIFI_SSID_MAX);
    s_sel_ssid[UI_WIFI_SSID_MAX] = '\0';
    if(s_lbl_pass_ssid) {
        if(s_sel_ssid[0] != '\0') {
            snprintf(hint, sizeof(hint), "%s", s_sel_ssid);
            lv_label_set_text(s_lbl_pass_ssid, hint);
        } else {
            lv_label_set_text(s_lbl_pass_ssid, UI_STR_WIFI_PASS_TITLE);
        }
        ui_obj_set_font_cn(s_lbl_pass_ssid);
        lv_obj_set_width(s_lbl_pass_ssid, LV_DISP_HOR_RES - 16);
        lv_label_set_long_mode(s_lbl_pass_ssid, LV_LABEL_LONG_DOT);
    }
    if(s_ta_ssid) {
        lv_textarea_set_text(s_ta_ssid, s_sel_ssid);
        if(s_sel_ssid[0] != '\0') {
            lv_obj_add_flag(s_ta_ssid, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_clear_flag(s_ta_ssid, LV_OBJ_FLAG_HIDDEN);
            s_kb_target = s_ta_ssid;
        }
    }
    if(s_ta_pass) {
        lv_textarea_set_text(s_ta_pass, "");
        lv_obj_clear_flag(s_ta_pass, LV_OBJ_FLAG_HIDDEN);
        if(s_sel_ssid[0] != '\0') {
            s_kb_target = s_ta_pass;
        }
    }
    if(s_lbl_wifi_hint) {
        lv_obj_add_flag(s_lbl_wifi_hint, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_lbl_wifi_status) {
        lv_obj_add_flag(s_lbl_wifi_status, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_lbl_wifi_dbg) {
        lv_obj_add_flag(s_lbl_wifi_dbg, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_lbl_wifi_dbg2) {
        lv_obj_add_flag(s_lbl_wifi_dbg2, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_btn_rescan) {
        lv_obj_add_flag(s_btn_rescan, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_btn_manual) {
        lv_obj_add_flag(s_btn_manual, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_btn_forget) {
        lv_obj_add_flag(s_btn_forget, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_list_wifi) {
        lv_obj_add_flag(s_list_wifi, LV_OBJ_FLAG_HIDDEN);
    }
    if(s_panel_pass) {
        lv_obj_clear_flag(s_panel_pass, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_panel_pass, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_move_foreground(s_panel_pass);
    }
}

static void ui_wifi_on_rescan(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    if(s_wifi_view != UI_WIFI_VIEW_LIST) {
        return;
    }
    if(s_wifi_conn_phase == UI_WIFI_CONN_PENDING) {
        s_wifi_conn_phase = UI_WIFI_CONN_IDLE;
        s_wifi_conn_start_tick = 0U;
    }
    ui_wifi_set_status_text(UI_STR_WIFI_SCANNING);
    ui_wifi_request_scan(1U);
}

static void ui_wifi_on_forget(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    HostLink_CmdWifiForget();
    s_ap_count = 0U;
    s_wifi_scan_active = 0U;
    s_wifi_connected = 0U;
    s_wifi_conn_phase = UI_WIFI_CONN_IDLE;
    s_wifi_ssid[0] = '\0';
    s_list_apply_pending = 1U;
    UI_RefreshHome();
}

static void ui_wifi_on_manual(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    ui_wifi_queue_pass_view("");
}

static void ui_wifi_on_pass_cancel(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    lv_async_call(ui_wifi_pass_cancel_async, NULL);
}

static void ui_wifi_pass_cancel_async(void * user_data)
{
    (void)user_data;
    ui_wifi_show_list_view();
    ui_wifi_set_idle_hint();
    if(s_ap_count > 0U) {
        ui_wifi_refresh_list();
    }
}

static void ui_wifi_on_pass_connect(lv_event_t * e)
{
    const char *pass;
    char         buf[64];

    (void)e;
    PowerMgr_OnUserActivity();
    pass = s_ta_pass ? lv_textarea_get_text(s_ta_pass) : "";
    if(s_ta_ssid && !lv_obj_has_flag(s_ta_ssid, LV_OBJ_FLAG_HIDDEN)) {
        const char *input_ssid = lv_textarea_get_text(s_ta_ssid);
        if(input_ssid && input_ssid[0] != '\0') {
            strncpy(s_sel_ssid, input_ssid, UI_WIFI_SSID_MAX);
            s_sel_ssid[UI_WIFI_SSID_MAX] = '\0';
        }
    }
    if(s_sel_ssid[0] == '\0') {
        if(s_lbl_pass_ssid) {
            lv_label_set_text(s_lbl_pass_ssid, UI_STR_WIFI_PASS_NEED_SSID);
            ui_obj_set_font_cn(s_lbl_pass_ssid);
        }
        return;
    }
    HostLink_CmdWifiConnect(s_sel_ssid, pass ? pass : "");
    s_wifi_conn_phase = UI_WIFI_CONN_PENDING;
    s_wifi_conn_start_tick = TMOS_GetSystemClock();
    s_last_status_poll_tick = s_wifi_conn_start_tick;
    HostLink_CmdWifiStatus();
    ui_wifi_show_list_view();
    snprintf(buf, sizeof(buf), UI_STR_WIFI_CONNECTING_FMT, s_sel_ssid);
    ui_wifi_set_status_text(buf);
}

static void ui_wifi_on_ta_focus(lv_event_t * e)
{
  lv_obj_t * ta = lv_event_get_target(e);
  (void)e;
  PowerMgr_OnUserActivity();
  s_kb_target = ta;
}

void UI_WiFi_Release(void)
{
    s_wifi_active = 0;
    s_wifi_scan_active = 0U;
    ui_wifi_destroy_ui();
}

void UI_WiFi_Leave(void)
{
    s_wifi_active = 0;
    s_wifi_scan_active = 0U;
    ui_wifi_hide_pass_panel();
    s_wifi_view = UI_WIFI_VIEW_LIST;
}

static void ui_wifi_on_back(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    s_return_scr = NULL;
    lv_async_call(ui_wifi_go_home_async, NULL);
}

static void ui_wifi_go_home_async(void * user_data)
{
    (void)user_data;
    UI_WiFi_Leave();
    UI_ReturnHomeFromOverlay();
}

void UI_WiFi_GetStatusSummary(char *buf, size_t len)
{
    char ssid_short[12];

    if(buf == NULL || len == 0U) {
        return;
    }
    if(!s_wifi_connected || s_wifi_ssid[0] == '\0') {
        snprintf(buf, len, "%s", UI_STR_WIFI_CHIP_OFF);
        return;
    }
    strncpy(ssid_short, s_wifi_ssid, sizeof(ssid_short) - 1U);
    ssid_short[sizeof(ssid_short) - 1U] = '\0';
    if(strlen(s_wifi_ssid) > sizeof(ssid_short) - 1U) {
        ssid_short[sizeof(ssid_short) - 2U] = '.';
        ssid_short[sizeof(ssid_short) - 1U] = '\0';
    }
    snprintf(buf, len, UI_STR_WIFI_CHIP_FMT, ssid_short);
}

static void ui_wifi_on_list_click(lv_event_t * e)
{
    lv_obj_t * btn = lv_event_get_target(e);
    uint8_t    idx;

    while(btn && lv_obj_get_parent(btn) != s_list_wifi) {
        btn = lv_obj_get_parent(btn);
    }
    if(btn == NULL) {
        return;
    }
    PowerMgr_OnUserActivity();
    idx = (uint8_t)(uintptr_t)lv_obj_get_user_data(btn);
    if(idx >= s_ap_count) {
        return;
    }
    ui_wifi_queue_pass_view(s_aps[idx].ssid);
}

static void ui_wifi_kb_event(lv_event_t * e)
{
    lv_obj_t * obj = lv_event_get_target(e);
    uint32_t   id = lv_btnmatrix_get_selected_btn(obj);
    const char *txt = lv_btnmatrix_get_btn_text(obj, id);
    lv_obj_t * target = s_kb_target;

    if(s_wifi_view != UI_WIFI_VIEW_PASS || target == NULL) {
        return;
    }
    if(target != s_ta_ssid && target != s_ta_pass) {
        return;
    }
    if(txt == NULL || txt[0] == '\0') {
        return;
    }
    PowerMgr_OnUserActivity();
    if(strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        lv_textarea_del_char(target);
        return;
    }
    if(txt[1] == '\0') {
        lv_textarea_add_text(target, txt);
    }
}

void UI_WiFi_OnHostLine(const char *json_line)
{
    if(json_line == NULL || !s_wifi_active) {
        return;
    }
    if(strstr(json_line, "\"event\":\"wifi_ack\"") != NULL) {
        return;
    }
    if(strstr(json_line, "\"event\":\"wifi_status\"") != NULL) {
        ui_wifi_parse_status(json_line);
    }
}

uint8_t UI_WiFi_IsActive(void)
{
    return s_wifi_active;
}

uint8_t UI_WiFi_IsPassView(void)
{
    return (s_wifi_active && s_wifi_view == UI_WIFI_VIEW_PASS) ? 1U : 0U;
}

void UI_WiFi_Open(void)
{
    /* 释放其它副屏；首页常驻。进密码页时仍会 destroy AP 按钮腾堆 */
    UI_ReleaseSecondaryScreens();
    if(!ui_wifi_ensure_ui() || s_scr_wifi == NULL) {
        UI_ShowHome();
        return;
    }
    if(Measure_IsActive()) {
        if(s_lbl_wifi_block) {
            lv_obj_clear_flag(s_lbl_wifi_block, LV_OBJ_FLAG_HIDDEN);
        }
        s_wifi_active = 1;
        s_return_scr = NULL;
        lv_scr_load(s_scr_wifi);
        return;
    }
    if(s_lbl_wifi_block) {
        lv_obj_add_flag(s_lbl_wifi_block, LV_OBJ_FLAG_HIDDEN);
    }
    PowerMgr_OnUserActivity();
    s_wifi_active = 1;
    ui_wifi_show_list_view();
    lv_scr_load(s_scr_wifi);
    if(s_ap_count > 0U) {
        ui_wifi_refresh_list();
        if(s_wifi_connected) {
            s_wifi_conn_phase = UI_WIFI_CONN_CONNECTED;
            HostLink_CmdWifiStatus();
        } else {
            ui_wifi_set_idle_hint();
        }
    } else {
        ui_wifi_set_status_text(UI_STR_WIFI_SCANNING);
        ui_wifi_request_scan(1U);
    }
    if(HostLink_TakeWifiList(s_wifi_line_buf, sizeof(s_wifi_line_buf))) {
        ui_wifi_parse_list(s_wifi_line_buf);
    }
    UI_WiFi_ApplyPending();
}

void UI_WiFi_UpdateDebug(void)
{
    uint32_t now;

    if(s_wifi_view == UI_WIFI_VIEW_PASS) {
        return;
    }

    if(HostLink_TakeWifiStatus(s_wifi_line_buf, sizeof(s_wifi_line_buf))) {
        ui_wifi_parse_status(s_wifi_line_buf);
    }
    if(HostLink_TakeWifiList(s_wifi_line_buf, sizeof(s_wifi_line_buf))) {
        ui_wifi_parse_list(s_wifi_line_buf);
    }
    if(s_wifi_conn_phase == UI_WIFI_CONN_PENDING) {
        now = TMOS_GetSystemClock();
        if(s_last_status_poll_tick == 0U ||
           (now - s_last_status_poll_tick) > MS1_TO_SYSTEM_TIME(2000U)) {
            s_last_status_poll_tick = now;
            HostLink_CmdWifiStatus();
        }
        if(s_wifi_conn_start_tick != 0U &&
           (now - s_wifi_conn_start_tick) > MS1_TO_SYSTEM_TIME(UI_WIFI_CONNECT_TIMEOUT_MS)) {
            s_wifi_conn_phase = UI_WIFI_CONN_IDLE;
            s_wifi_conn_start_tick = 0U;
            ui_wifi_set_status_text("连接超时，请检查密码");
            HostLink_CmdWifiStatus();
        }
    }
    if(s_wifi_scan_active && s_last_scan_tick != 0U) {
        now = TMOS_GetSystemClock();
        if((now - s_last_scan_tick) > MS1_TO_SYSTEM_TIME(UI_WIFI_SCAN_TIMEOUT_MS)) {
            s_wifi_scan_active = 0U;
            if(s_wifi_active && s_ap_count == 0U &&
               s_wifi_conn_phase == UI_WIFI_CONN_IDLE) {
                ui_wifi_set_status_text(UI_STR_WIFI_NO_24G);
            }
        }
    } else if(s_wifi_active && s_wifi_view == UI_WIFI_VIEW_LIST &&
              s_ap_count == 0U && s_last_scan_tick != 0U &&
              s_wifi_conn_phase == UI_WIFI_CONN_IDLE) {
        now = TMOS_GetSystemClock();
        if((now - s_last_scan_tick) > MS1_TO_SYSTEM_TIME(UI_WIFI_SCAN_RETRY_MS)) {
            ui_wifi_set_status_text(UI_STR_WIFI_SCANNING);
            ui_wifi_request_scan(1U);
        }
    }
    if(s_wifi_active && s_wifi_view == UI_WIFI_VIEW_LIST) {
        ui_wifi_update_debug_label();
    }
}


static void ui_wifi_clear_widget_ptrs(void)
{
    s_scr_wifi = NULL;
    s_lbl_wifi_title = NULL;
    s_lbl_wifi_hint = NULL;
    s_lbl_wifi_status = NULL;
    s_list_wifi = NULL;
    s_btn_rescan = NULL;
    s_btn_manual = NULL;
    s_btn_forget = NULL;
    s_lbl_wifi_block = NULL;
    s_lbl_wifi_dbg = NULL;
    s_lbl_wifi_dbg2 = NULL;
    memset(s_ap_btns, 0, sizeof(s_ap_btns));
    memset(s_ap_lbls, 0, sizeof(s_ap_lbls));
    s_wifi_ui_ready = 0U;
    s_pass_panel_ready = 0U;
    s_wifi_view = UI_WIFI_VIEW_LIST;
}

static void ui_wifi_destroy_ui(void)
{
    lv_obj_t * scr = s_scr_wifi;

    s_kb_target = NULL;
    s_panel_pass = NULL;
    s_lbl_pass_ssid = NULL;
    s_ta_ssid = NULL;
    s_ta_pass = NULL;
    s_kb_matrix = NULL;
    s_btn_pass_connect = NULL;
    s_btn_pass_cancel = NULL;
    s_pass_panel_ready = 0U;

    if(scr) {
        ui_wifi_clear_widget_ptrs();
        ui_scr_del(scr);
    } else {
        ui_wifi_clear_widget_ptrs();
    }
}

static uint8_t ui_wifi_ensure_ui(void)
{
    if(s_wifi_ui_ready && s_scr_wifi) {
        return 1U;
    }
    ui_wifi_build_list_ui();
    if(s_scr_wifi == NULL) {
        s_wifi_ui_ready = 0U;
        return 0U;
    }
    s_wifi_ui_ready = 1U;
    return 1U;
}

static void ui_wifi_destroy_pass_panel(void)
{
    lv_obj_t * panel = s_panel_pass;

    if(panel == NULL) {
        s_pass_panel_ready = 0U;
        return;
    }
    s_panel_pass = NULL;
    s_lbl_pass_ssid = NULL;
    s_ta_ssid = NULL;
    s_ta_pass = NULL;
    s_kb_matrix = NULL;
    s_btn_pass_connect = NULL;
    s_btn_pass_cancel = NULL;
    s_kb_target = NULL;
    s_pass_panel_ready = 0U;
    ui_scr_del(panel);
}

static void ui_wifi_style_pass_ta(lv_obj_t * ta)
{
    lv_obj_set_style_pad_hor(ta, 10, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(ta, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(ta, 4, LV_PART_MAIN);
    lv_obj_set_style_border_width(ta, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x90CAF9), LV_PART_MAIN);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
}

static void ui_wifi_style_pass_kb(lv_obj_t * kb)
{
    lv_obj_set_style_pad_row(kb, 3, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb, 2, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0xE3F2FD), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, lv_color_hex(0xBBDEFB), LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(kb, lv_color_hex(0x1565C0), LV_PART_ITEMS);
}

static void ui_wifi_build_pass_panel(void)
{
    lv_obj_t * lbl;
    lv_coord_t y;
    lv_coord_t w;

    if(s_pass_panel_ready || s_scr_wifi == NULL) {
        return;
    }

    w = LV_DISP_HOR_RES - UI_WIFI_PASS_PAD * 2;
    y = UI_WIFI_PASS_PAD;

    s_panel_pass = lv_obj_create(s_scr_wifi);
    lv_obj_set_pos(s_panel_pass, 0, UI_WIFI_PASS_PANEL_Y);
    lv_obj_set_size(s_panel_pass, LV_DISP_HOR_RES, UI_WIFI_PASS_PANEL_H);
    lv_obj_set_style_bg_color(s_panel_pass, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_bg_opa(s_panel_pass, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_panel_pass, 0, 0);
    lv_obj_set_style_pad_all(s_panel_pass, 0, 0);
    lv_obj_add_flag(s_panel_pass, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_panel_pass, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_panel_pass, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_pass_ssid = lv_label_create(s_panel_pass);
    lv_label_set_text(s_lbl_pass_ssid, UI_STR_WIFI_PASS_TITLE);
    ui_obj_set_font_cn(s_lbl_pass_ssid);
    lv_obj_set_style_text_color(s_lbl_pass_ssid, lv_color_hex(0x0D47A1), 0);
    lv_obj_set_width(s_lbl_pass_ssid, w);
    lv_label_set_long_mode(s_lbl_pass_ssid, LV_LABEL_LONG_DOT);
    lv_obj_align(s_lbl_pass_ssid, LV_ALIGN_TOP_LEFT, UI_WIFI_PASS_PAD, y);
    lv_obj_clear_flag(s_lbl_pass_ssid, LV_OBJ_FLAG_CLICKABLE);
    y += UI_WIFI_PASS_TITLE_H + UI_WIFI_PASS_GAP;

    s_ta_ssid = lv_textarea_create(s_panel_pass);
    lv_obj_set_size(s_ta_ssid, w, UI_WIFI_PASS_TA_H);
    lv_obj_align(s_ta_ssid, LV_ALIGN_TOP_LEFT, UI_WIFI_PASS_PAD, y);
    lv_textarea_set_one_line(s_ta_ssid, true);
    lv_textarea_set_max_length(s_ta_ssid, UI_WIFI_SSID_MAX);
    lv_textarea_set_placeholder_text(s_ta_ssid, "SSID");
    lv_obj_set_style_text_font(s_ta_ssid, &lv_font_montserrat_14, LV_PART_MAIN);
    ui_wifi_style_pass_ta(s_ta_ssid);
    lv_obj_add_event_cb(s_ta_ssid, ui_wifi_on_ta_focus, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ta_ssid, ui_wifi_on_ta_focus, LV_EVENT_FOCUSED, NULL);
    y += UI_WIFI_PASS_TA_H + UI_WIFI_PASS_GAP;

    s_ta_pass = lv_textarea_create(s_panel_pass);
    lv_obj_set_size(s_ta_pass, w, UI_WIFI_PASS_TA_H);
    lv_obj_align(s_ta_pass, LV_ALIGN_TOP_LEFT, UI_WIFI_PASS_PAD, y);
    lv_textarea_set_password_mode(s_ta_pass, true);
    lv_textarea_set_one_line(s_ta_pass, true);
    lv_textarea_set_max_length(s_ta_pass, UI_WIFI_PASS_MAX);
    lv_textarea_set_placeholder_text(s_ta_pass, UI_STR_WIFI_PASS_PH_PASS);
    ui_obj_set_font_cn(s_ta_pass);
    ui_wifi_style_pass_ta(s_ta_pass);
    lv_obj_add_event_cb(s_ta_pass, ui_wifi_on_ta_focus, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_ta_pass, ui_wifi_on_ta_focus, LV_EVENT_FOCUSED, NULL);

    s_btn_pass_connect = lv_btn_create(s_panel_pass);
    lv_obj_set_size(s_btn_pass_connect, (w - 8) / 2, UI_WIFI_PASS_BTN_H);
    lv_obj_align(s_btn_pass_connect, LV_ALIGN_BOTTOM_LEFT, UI_WIFI_PASS_PAD, -UI_WIFI_PASS_PAD);
    lv_obj_set_style_radius(s_btn_pass_connect, 0, 0);
    lv_obj_set_style_shadow_width(s_btn_pass_connect, 0, 0);
    lv_obj_set_style_bg_color(s_btn_pass_connect, lv_color_hex(0x1565C0), 0);
    lv_obj_add_event_cb(s_btn_pass_connect, ui_wifi_on_pass_connect, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_pass_connect);
    lv_label_set_text(lbl, UI_STR_WIFI_KB_CONNECT);
    ui_obj_set_font_cn(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(lbl);

    s_btn_pass_cancel = lv_btn_create(s_panel_pass);
    lv_obj_set_size(s_btn_pass_cancel, (w - 8) / 2, UI_WIFI_PASS_BTN_H);
    lv_obj_align(s_btn_pass_cancel, LV_ALIGN_BOTTOM_RIGHT, -UI_WIFI_PASS_PAD, -UI_WIFI_PASS_PAD);
    lv_obj_set_style_radius(s_btn_pass_cancel, 0, 0);
    lv_obj_set_style_shadow_width(s_btn_pass_cancel, 0, 0);
    lv_obj_set_style_bg_color(s_btn_pass_cancel, lv_color_hex(0x78909C), 0);
    lv_obj_add_event_cb(s_btn_pass_cancel, ui_wifi_on_pass_cancel, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_pass_cancel);
    lv_label_set_text(lbl, UI_STR_WIFI_KB_CANCEL);
    ui_obj_set_font_cn(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(lbl);

    s_kb_matrix = lv_btnmatrix_create(s_panel_pass);
    lv_btnmatrix_set_map(s_kb_matrix, s_kb_map);
    lv_obj_set_style_text_font(s_kb_matrix, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_set_size(s_kb_matrix, w, UI_WIFI_PASS_KB_H);
    lv_obj_align(s_kb_matrix, LV_ALIGN_TOP_LEFT, UI_WIFI_PASS_PAD, UI_WIFI_PASS_KB_Y);
    ui_wifi_style_pass_kb(s_kb_matrix);
    lv_obj_add_event_cb(s_kb_matrix, ui_wifi_kb_event, LV_EVENT_VALUE_CHANGED, NULL);

    s_kb_target = s_ta_pass;
    s_pass_panel_ready = 1U;
}

static void ui_wifi_build_list_ui(void)
{
    lv_obj_t * top;
    lv_obj_t * btn;
    lv_obj_t * lbl;

    s_scr_wifi = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_wifi, lv_color_hex(0xE3F2FD), 0);
    lv_obj_clear_flag(s_scr_wifi, LV_OBJ_FLAG_CLICKABLE);

    top = lv_obj_create(s_scr_wifi);
    lv_obj_set_size(top, LV_DISP_HOR_RES - 16, UI_WIFI_TOP_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 4);
    lv_obj_set_style_pad_hor(top, 8, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    s_lbl_wifi_title = lv_label_create(top);
    lv_label_set_text(s_lbl_wifi_title, UI_STR_WIFI_TITLE);
    ui_obj_set_font_cn(s_lbl_wifi_title);
    lv_obj_set_style_text_color(s_lbl_wifi_title, lv_color_hex(0x0D47A1), 0);

    btn = lv_btn_create(top);
    lv_obj_set_size(btn, 112, 32);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), 0);
    lv_obj_add_event_cb(btn, ui_wifi_on_back, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, UI_STR_WIFI_BACK);
    ui_obj_set_font_cn(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(lbl);

    s_lbl_wifi_hint = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_lbl_wifi_hint, UI_STR_WIFI_HINT);
    ui_obj_set_font_cn(s_lbl_wifi_hint);
    lv_obj_set_style_text_color(s_lbl_wifi_hint, lv_color_hex(0xE65100), 0);
    lv_obj_align(s_lbl_wifi_hint, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_clear_flag(s_lbl_wifi_hint, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_wifi_status = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_lbl_wifi_status, "...");
    ui_obj_set_font_cn(s_lbl_wifi_status);
    lv_obj_set_width(s_lbl_wifi_status, LV_DISP_HOR_RES - 16);
    lv_obj_align(s_lbl_wifi_status, LV_ALIGN_TOP_MID, 0, 58);
    lv_obj_clear_flag(s_lbl_wifi_status, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_wifi_dbg = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_lbl_wifi_dbg, "B:0 L:0 W:0 S:0 AP:0");
    lv_obj_set_style_text_color(s_lbl_wifi_dbg, lv_color_hex(0x546E7A), 0);
    lv_obj_align(s_lbl_wifi_dbg, LV_ALIGN_TOP_RIGHT, -4, 74);
    lv_obj_clear_flag(s_lbl_wifi_dbg, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_wifi_dbg2 = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_lbl_wifi_dbg2, "");
    lv_obj_add_flag(s_lbl_wifi_dbg2, LV_OBJ_FLAG_HIDDEN);

    s_btn_rescan = lv_btn_create(s_scr_wifi);
    lv_obj_set_size(s_btn_rescan, 88, 28);
    lv_obj_align(s_btn_rescan, LV_ALIGN_TOP_LEFT, 8, 90);
    lv_obj_add_event_cb(s_btn_rescan, ui_wifi_on_rescan, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_rescan);
    lv_label_set_text(lbl, UI_STR_WIFI_RESCAN);
    ui_obj_set_font_cn(lbl);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(lbl);

    s_btn_manual = lv_btn_create(s_scr_wifi);
    lv_obj_set_size(s_btn_manual, 88, 28);
    lv_obj_align(s_btn_manual, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_add_event_cb(s_btn_manual, ui_wifi_on_manual, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_manual);
    lv_label_set_text(lbl, UI_STR_WIFI_MANUAL);
    ui_obj_set_font_cn(lbl);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(lbl);

    s_btn_forget = lv_btn_create(s_scr_wifi);
    lv_obj_set_size(s_btn_forget, 88, 28);
    lv_obj_align(s_btn_forget, LV_ALIGN_TOP_RIGHT, -8, 90);
    lv_obj_add_event_cb(s_btn_forget, ui_wifi_on_forget, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_forget);
    lv_label_set_text(lbl, UI_STR_WIFI_FORGET);
    ui_obj_set_font_cn(lbl);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(lbl);

    s_list_wifi = lv_obj_create(s_scr_wifi);
    lv_obj_set_size(s_list_wifi, LV_DISP_HOR_RES - 16, LV_DISP_VER_RES - 132);
    lv_obj_align(s_list_wifi, LV_ALIGN_TOP_MID, 0, 124);
    lv_obj_set_flex_flow(s_list_wifi, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_list_wifi, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(s_list_wifi, 4, 0);
    lv_obj_set_style_pad_all(s_list_wifi, 4, 0);
    lv_obj_set_scroll_dir(s_list_wifi, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_list_wifi, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_clear_flag(s_list_wifi, LV_OBJ_FLAG_CLICKABLE);

    s_lbl_wifi_block = lv_label_create(s_scr_wifi);
    lv_label_set_text(s_lbl_wifi_block, UI_STR_WIFI_BLOCK);
    ui_obj_set_font_cn(s_lbl_wifi_block);
    lv_obj_set_style_text_color(s_lbl_wifi_block, lv_color_hex(0xC62828), 0);
    lv_obj_align(s_lbl_wifi_block, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(s_lbl_wifi_block, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_lbl_wifi_block, LV_OBJ_FLAG_CLICKABLE);
}

void UI_WiFi_Init(void)
{
    HostLink_SetEventFn(UI_WiFi_OnHostLine);
}
