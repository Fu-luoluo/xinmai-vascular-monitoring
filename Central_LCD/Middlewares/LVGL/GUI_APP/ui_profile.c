#include "ui_profile.h"
#include "ui.h"
#include "ui_strings.h"
#include "ui_font.h"
#include "user_profile.h"
#include "power_mgr.h"
#include "lv_port_disp.h"
#include "lvgl.h"
#include "ui_scr_del.h"
#include <stdio.h>
#include <string.h>

#define UI_PROFILE_AGE_OPTS_LEN     512
#define UI_PROFILE_HEIGHT_OPTS_LEN  512
#define UI_PROFILE_HAND_OPTS_LEN    256

static lv_obj_t * s_scr_profile;
static lv_obj_t * s_btn_gender_m;
static lv_obj_t * s_btn_gender_f;
static lv_obj_t * s_roller_age;
static lv_obj_t * s_roller_height;
static lv_obj_t * s_roller_hand;
static char       s_age_opts[UI_PROFILE_AGE_OPTS_LEN];
static char       s_height_opts[UI_PROFILE_HEIGHT_OPTS_LEN];
static char       s_hand_opts[UI_PROFILE_HAND_OPTS_LEN];

static void ui_profile_build_opts(void)
{
    int   pos;
    int   v;

    pos = 0;
    for(v = USER_PROFILE_AGE_MIN; v <= USER_PROFILE_AGE_MAX; v++) {
        pos += snprintf(s_age_opts + pos, sizeof(s_age_opts) - (size_t)pos,
                        "%d\n", v);
        if(pos >= (int)sizeof(s_age_opts) - 4) {
            break;
        }
    }
    if(pos > 0) {
        s_age_opts[pos - 1] = '\0';
    }

    pos = 0;
    for(v = 140; v <= 200; v++) {
        pos += snprintf(s_height_opts + pos, sizeof(s_height_opts) - (size_t)pos,
                        "%d\n", v);
        if(pos >= (int)sizeof(s_height_opts) - 4) {
            break;
        }
    }
    if(pos > 0) {
        s_height_opts[pos - 1] = '\0';
    }

    pos = snprintf(s_hand_opts, sizeof(s_hand_opts), "%s\n", UI_STR_PROFILE_HAND_AUTO);
    for(v = USER_PROFILE_HAND_MIN_CM; v <= USER_PROFILE_HAND_MAX_CM; v++) {
        pos += snprintf(s_hand_opts + pos, sizeof(s_hand_opts) - (size_t)pos,
                        "%d\n", v);
        if(pos >= (int)sizeof(s_hand_opts) - 4) {
            break;
        }
    }
    if(pos > 0) {
        s_hand_opts[pos - 1] = '\0';
    }
}

static void ui_profile_style_gender_btn(lv_obj_t * btn, uint8_t active)
{
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x1565C0), 0);
    if(active) {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_PART_MAIN);
    } else {
        lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0x1565C0), LV_PART_MAIN);
    }
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), LV_STATE_CHECKED);
    lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), LV_STATE_CHECKED | LV_PART_MAIN);
}

static void ui_profile_on_gender(lv_event_t * e)
{
    lv_obj_t * target = lv_event_get_target(e);

    (void)e;
    PowerMgr_OnUserActivity();
    if(target == s_btn_gender_m) {
        lv_obj_add_state(s_btn_gender_m, LV_STATE_CHECKED);
        lv_obj_clear_state(s_btn_gender_f, LV_STATE_CHECKED);
        ui_profile_style_gender_btn(s_btn_gender_m, 1U);
        ui_profile_style_gender_btn(s_btn_gender_f, 0U);
    } else {
        lv_obj_add_state(s_btn_gender_f, LV_STATE_CHECKED);
        lv_obj_clear_state(s_btn_gender_m, LV_STATE_CHECKED);
        ui_profile_style_gender_btn(s_btn_gender_f, 1U);
        ui_profile_style_gender_btn(s_btn_gender_m, 0U);
    }
}

static void ui_profile_on_roller(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
}

static void ui_profile_load_fields(void)
{
    user_profile_t prof;
    uint16_t       age_sel;
    uint16_t       h_sel;
    uint16_t       hand_sel;

    UserProfile_Get(&prof);
    if(prof.age < USER_PROFILE_AGE_MIN) {
        prof.age = 25U;
    }
    if(prof.height_cm < 140U || prof.height_cm > 200U) {
        prof.height_cm = 170U;
    }

    age_sel = prof.age - USER_PROFILE_AGE_MIN;
    h_sel = prof.height_cm - 140U;
    if(prof.hand_cm == USER_PROFILE_HAND_AUTO || prof.hand_cm == 0U) {
        hand_sel = 0U;
    } else if(prof.hand_cm >= USER_PROFILE_HAND_MIN_CM &&
              prof.hand_cm <= USER_PROFILE_HAND_MAX_CM) {
        hand_sel = (uint16_t)(prof.hand_cm - USER_PROFILE_HAND_MIN_CM + 1U);
    } else {
        hand_sel = 0U;
    }

    lv_roller_set_selected(s_roller_age, age_sel, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_height, h_sel, LV_ANIM_OFF);
    lv_roller_set_selected(s_roller_hand, hand_sel, LV_ANIM_OFF);

    if(prof.gender == USER_PROFILE_GENDER_FEMALE) {
        lv_obj_add_state(s_btn_gender_f, LV_STATE_CHECKED);
        lv_obj_clear_state(s_btn_gender_m, LV_STATE_CHECKED);
        ui_profile_style_gender_btn(s_btn_gender_f, 1U);
        ui_profile_style_gender_btn(s_btn_gender_m, 0U);
    } else {
        lv_obj_add_state(s_btn_gender_m, LV_STATE_CHECKED);
        lv_obj_clear_state(s_btn_gender_f, LV_STATE_CHECKED);
        ui_profile_style_gender_btn(s_btn_gender_m, 1U);
        ui_profile_style_gender_btn(s_btn_gender_f, 0U);
    }
}

static void ui_profile_on_save(lv_event_t * e)
{
    user_profile_t prof;

    (void)e;
    PowerMgr_OnUserActivity();

    memset(&prof, 0, sizeof(prof));
    prof.age = (uint8_t)lv_roller_get_selected(s_roller_age) + USER_PROFILE_AGE_MIN;
    prof.gender = lv_obj_has_state(s_btn_gender_m, LV_STATE_CHECKED) ?
                  USER_PROFILE_GENDER_MALE : USER_PROFILE_GENDER_FEMALE;
    prof.height_cm = (uint16_t)(140U + lv_roller_get_selected(s_roller_height));
    if(lv_roller_get_selected(s_roller_hand) == 0U) {
        prof.hand_cm = USER_PROFILE_HAND_AUTO;
    } else {
        prof.hand_cm = (uint16_t)(USER_PROFILE_HAND_MIN_CM +
                                  lv_roller_get_selected(s_roller_hand) - 1U);
    }
    prof.valid = 1U;

    if(!UserProfile_Set(&prof) || !UserProfile_Save()) {
        return;
    }
    UI_RefreshHome();
    UI_ShowHome();
}

static void ui_profile_on_back(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    UI_ShowHome();
}

void UI_Profile_FormatSummary(char *buf, size_t len)
{
    user_profile_t prof;

    if(buf == NULL || len == 0U) {
        return;
    }
    UserProfile_Get(&prof);
    if(!UserProfile_IsComplete()) {
        snprintf(buf, len, "%s", UI_STR_PROFILE_INCOMPLETE);
        return;
    }
    snprintf(buf, len, UI_STR_PROFILE_SUMMARY_FMT,
             (prof.gender == USER_PROFILE_GENDER_MALE) ? UI_STR_PROFILE_MALE
                                                       : UI_STR_PROFILE_FEMALE,
             (unsigned)prof.age);
}

void UI_Profile_Init(void)
{
    ui_profile_build_opts();
}

static void ui_profile_build_screen(void)
{
    lv_obj_t * top;
    lv_obj_t * row;
    lv_obj_t * lbl;
    lv_obj_t * btn;
    lv_obj_t * col;

    if(s_scr_profile != NULL) {
        return;
    }

    s_scr_profile = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_profile, lv_color_hex(0xE3F2FD), 0);
    lv_obj_clear_flag(s_scr_profile, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(s_scr_profile, LV_OBJ_FLAG_CLICKABLE);

    top = lv_obj_create(s_scr_profile);
    lv_obj_set_size(top, LV_DISP_HOR_RES, 40);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_hor(top, 12, 0);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x1565C0), 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_CLICKABLE);

    lbl = lv_label_create(top);
    lv_label_set_text(lbl, UI_STR_PROFILE_TITLE);
    ui_obj_set_font_cn(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);

    btn = lv_btn_create(top);
    lv_obj_set_size(btn, 96, 32);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0D47A1), 0);
    lv_obj_add_event_cb(btn, ui_profile_on_back, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, UI_STR_PROFILE_BACK);
    ui_obj_set_font_cn(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(lbl);

    row = lv_obj_create(s_scr_profile);
    lv_obj_set_size(row, LV_DISP_HOR_RES - 16, 40);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 48);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    s_btn_gender_m = lv_btn_create(row);
    lv_obj_set_size(s_btn_gender_m, 140, 36);
    lv_obj_add_flag(s_btn_gender_m, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(s_btn_gender_m, ui_profile_on_gender, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_gender_m);
    lv_label_set_text(lbl, UI_STR_PROFILE_MALE);
    ui_obj_set_font_cn(lbl);
    lv_obj_center(lbl);

    s_btn_gender_f = lv_btn_create(row);
    lv_obj_set_size(s_btn_gender_f, 140, 36);
    lv_obj_add_flag(s_btn_gender_f, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_add_event_cb(s_btn_gender_f, ui_profile_on_gender, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_gender_f);
    lv_label_set_text(lbl, UI_STR_PROFILE_FEMALE);
    ui_obj_set_font_cn(lbl);
    lv_obj_center(lbl);

    ui_profile_style_gender_btn(s_btn_gender_m, 1U);
    ui_profile_style_gender_btn(s_btn_gender_f, 0U);

    row = lv_obj_create(s_scr_profile);
    lv_obj_set_size(row, LV_DISP_HOR_RES - 16, 150);
    lv_obj_align(row, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_CLICKABLE);

    col = lv_obj_create(row);
    lv_obj_set_size(col, 140, 150);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lbl = lv_label_create(col);
    lv_label_set_text(lbl, UI_STR_PROFILE_AGE);
    ui_obj_set_font_cn(lbl);
    s_roller_age = lv_roller_create(col);
    lv_roller_set_options(s_roller_age, s_age_opts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(s_roller_age, 132);
    lv_obj_set_height(s_roller_age, 96);
    ui_obj_set_font_cn(s_roller_age);
    lv_obj_add_event_cb(s_roller_age, ui_profile_on_roller, LV_EVENT_VALUE_CHANGED, NULL);

    col = lv_obj_create(row);
    lv_obj_set_size(col, 140, 150);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lbl = lv_label_create(col);
    lv_label_set_text(lbl, UI_STR_PROFILE_HEIGHT);
    ui_obj_set_font_cn(lbl);
    s_roller_height = lv_roller_create(col);
    lv_roller_set_options(s_roller_height, s_height_opts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(s_roller_height, 132);
    lv_obj_set_height(s_roller_height, 96);
    ui_obj_set_font_cn(s_roller_height);
    lv_obj_add_event_cb(s_roller_height, ui_profile_on_roller, LV_EVENT_VALUE_CHANGED, NULL);

    col = lv_obj_create(row);
    lv_obj_set_size(col, 140, 150);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(col, 0, 0);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_CLICKABLE);
    lbl = lv_label_create(col);
    lv_label_set_text(lbl, UI_STR_PROFILE_HAND);
    ui_obj_set_font_cn(lbl);
    s_roller_hand = lv_roller_create(col);
    lv_roller_set_options(s_roller_hand, s_hand_opts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_width(s_roller_hand, 132);
    lv_obj_set_height(s_roller_hand, 96);
    ui_obj_set_font_cn(s_roller_hand);
    lv_obj_add_event_cb(s_roller_hand, ui_profile_on_roller, LV_EVENT_VALUE_CHANGED, NULL);

    btn = lv_btn_create(s_scr_profile);
    lv_obj_set_size(btn, 220, 44);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -16);
    lv_obj_set_style_radius(btn, 0, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1565C0), 0);
    lv_obj_add_event_cb(btn, ui_profile_on_save, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(btn);
    lv_label_set_text(lbl, UI_STR_PROFILE_SAVE);
    ui_obj_set_font_cn(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_flag(lbl, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_center(lbl);
}

void UI_Profile_Destroy(void)
{
    lv_obj_t * scr = s_scr_profile;

    if(scr == NULL) {
        return;
    }
    s_scr_profile = NULL;
    s_btn_gender_m = NULL;
    s_btn_gender_f = NULL;
    s_roller_age = NULL;
    s_roller_height = NULL;
    s_roller_hand = NULL;
    ui_scr_del(scr);
}

void UI_Profile_Open(void)
{
    PowerMgr_OnUserActivity();
    UI_ReleaseSecondaryScreens();
    /* 首页常驻，档案页与首页切换只 load，加快响应 */
    if(s_scr_profile == NULL) {
        ui_profile_build_screen();
    }
    if(s_scr_profile == NULL) {
        UI_ShowHome();
        return;
    }
    ui_profile_load_fields();
    lv_scr_load(s_scr_profile);
}
