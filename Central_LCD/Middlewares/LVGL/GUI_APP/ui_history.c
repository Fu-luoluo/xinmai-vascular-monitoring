#include "ui_history.h"
#include "ui.h"
#include "ui_strings.h"
#include "ui_font.h"
#include "history_store.h"
#include "power_mgr.h"
#include "lv_port_disp.h"
#include "lvgl.h"
#include "ui_scr_del.h"
#include <stdio.h>

#define UI_HISTORY_TOP_H        26
#define UI_HISTORY_NAV_H        38
#define UI_HISTORY_LIST_H       (LV_DISP_VER_RES - UI_HISTORY_TOP_H - UI_HISTORY_NAV_H)
#define UI_HISTORY_ROW_H        (UI_HISTORY_LIST_H / UI_HISTORY_PAGE_SIZE)
#define UI_HISTORY_FILL_CHUNK   2u  /* 每轮最多读 2 条 Flash，避免一次阻塞 UI */

static lv_obj_t * s_scr_history;
static lv_obj_t * s_lbl_hist_total;
static lv_obj_t * s_lbl_hist_page;
static lv_obj_t * s_btn_hist_prev;
static lv_obj_t * s_btn_hist_next;
static lv_obj_t * s_row_labels[UI_HISTORY_PAGE_SIZE];
static lv_obj_t * s_lbl_hist_empty;
static uint32_t   s_hist_page;
static uint32_t   s_fill_record_count;
static uint32_t   s_fill_row;
static uint8_t    s_fill_active;
static uint8_t    s_refresh_queued;

static void ui_history_build_screen(void);
static void ui_history_ensure_screen(void);
static void ui_history_queue_refresh(void);
static void ui_history_refresh_async(void * user_data);
static void ui_history_fill_chunk_async(void * user_data);

static void ui_history_set_row_font(lv_obj_t * obj)
{
    lv_obj_set_style_text_font(obj, &lv_font_source_han_sans_bold_18, LV_PART_MAIN);
    lv_obj_set_style_text_line_space(obj, 0, LV_PART_MAIN);
}

static uint32_t ui_history_page_count(uint32_t record_count)
{
    if(record_count == 0U) {
        return 0U;
    }
    return (record_count + UI_HISTORY_PAGE_SIZE - 1U) / UI_HISTORY_PAGE_SIZE;
}

static void ui_history_format_time(uint32_t ts_sec, char *buf, size_t len)
{
    uint32_t hh;
    uint32_t mm;
    uint32_t ss;

    if(buf == NULL || len == 0U) {
        return;
    }
    hh = ts_sec / 3600U;
    mm = (ts_sec % 3600U) / 60U;
    ss = ts_sec % 60U;
    snprintf(buf, len, "%02lu:%02lu:%02lu",
             (unsigned long)hh,
             (unsigned long)mm,
             (unsigned long)ss);
}

static void ui_history_format_row(const history_record_t *rec, char *buf, size_t len)
{
    char tbuf[16];
    int  abs_x10;

    if(rec == NULL || buf == NULL || len == 0U) {
        return;
    }
    ui_history_format_time(rec->ts_sec, tbuf, sizeof(tbuf));
    if(rec->pwv_valid) {
        abs_x10 = rec->pwv_x10;
        if(abs_x10 < 0) {
            abs_x10 = -abs_x10;
        }
        snprintf(buf, len, "PWV %d.%01d  腕 %u %u  指 %u %u  %s",
                 (int)(rec->pwv_x10 / 10),
                 abs_x10 % 10,
                 (unsigned)rec->w_hr, (unsigned)rec->w_spo2,
                 (unsigned)rec->f_hr, (unsigned)rec->f_spo2,
                 tbuf);
    } else {
        snprintf(buf, len, "PWV --  腕 %u %u  指 %u %u  %s",
                 (unsigned)rec->w_hr, (unsigned)rec->w_spo2,
                 (unsigned)rec->f_hr, (unsigned)rec->f_spo2,
                 tbuf);
    }
}

static void ui_history_update_page_controls(uint32_t record_count)
{
    char page_buf[20];
    uint32_t page_total = ui_history_page_count(record_count);

    if(page_total == 0U) {
        s_hist_page = 0U;
    } else if(s_hist_page >= page_total) {
        s_hist_page = page_total - 1U;
    }

    if(s_lbl_hist_page != NULL) {
        if(page_total == 0U) {
            lv_label_set_text(s_lbl_hist_page, UI_STR_HIST_PAGE_EMPTY);
        } else {
            snprintf(page_buf, sizeof(page_buf), UI_STR_HIST_PAGE_FMT,
                     (unsigned long)(s_hist_page + 1U),
                     (unsigned long)page_total);
            lv_label_set_text(s_lbl_hist_page, page_buf);
        }
    }

    if(s_btn_hist_prev != NULL) {
        /* 左箭头：上一页（页码减一，最新记录页为第 1 页） */
        if(page_total == 0U || s_hist_page == 0U) {
            lv_obj_add_state(s_btn_hist_prev, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_btn_hist_prev, LV_STATE_DISABLED);
        }
    }
    if(s_btn_hist_next != NULL) {
        /* 右箭头：下一页（页码加一） */
        if(page_total == 0U || s_hist_page + 1U >= page_total) {
            lv_obj_add_state(s_btn_hist_next, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(s_btn_hist_next, LV_STATE_DISABLED);
        }
    }
}

static void ui_history_clear_rows(void)
{
    uint32_t i;

    for(i = 0; i < UI_HISTORY_PAGE_SIZE; i++) {
        if(s_row_labels[i] != NULL) {
            lv_label_set_text(s_row_labels[i], "");
            lv_obj_add_flag(s_row_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void ui_history_fill_chunk_async(void * user_data)
{
    history_record_t rec;
    char             line[72];
    uint32_t         base_index;
    uint32_t         done = 0U;
    uint32_t         gen;

    (void)user_data;
    if(!s_fill_active || s_scr_history == NULL) {
        s_fill_active = 0U;
        return;
    }

    gen = s_hist_page;
    base_index = s_hist_page * UI_HISTORY_PAGE_SIZE;

    while(s_fill_row < UI_HISTORY_PAGE_SIZE && done < UI_HISTORY_FILL_CHUNK) {
        if(s_row_labels[s_fill_row] == NULL) {
            s_fill_row++;
            continue;
        }
        if((base_index + s_fill_row) >= s_fill_record_count) {
            lv_label_set_text(s_row_labels[s_fill_row], "");
            lv_obj_add_flag(s_row_labels[s_fill_row], LV_OBJ_FLAG_HIDDEN);
            s_fill_row++;
            done++;
            continue;
        }
        if(HistoryStore_GetRecent(base_index + s_fill_row, &rec) != 0) {
            lv_label_set_text(s_row_labels[s_fill_row], "");
            lv_obj_add_flag(s_row_labels[s_fill_row], LV_OBJ_FLAG_HIDDEN);
        } else {
            ui_history_format_row(&rec, line, sizeof(line));
            lv_label_set_text(s_row_labels[s_fill_row], line);
            lv_obj_clear_flag(s_row_labels[s_fill_row], LV_OBJ_FLAG_HIDDEN);
        }
        s_fill_row++;
        done++;
    }

    /* 翻页期间若页码已变，放弃本轮续填 */
    if(gen != s_hist_page) {
        s_fill_active = 0U;
        return;
    }

    if(s_fill_row < UI_HISTORY_PAGE_SIZE) {
        lv_async_call(ui_history_fill_chunk_async, NULL);
    } else {
        s_fill_active = 0U;
    }
}

static void ui_history_start_fill(uint32_t record_count)
{
    s_fill_active = 0U;
    s_fill_record_count = record_count;
    s_fill_row = 0U;

    if(record_count == 0U) {
        ui_history_clear_rows();
        return;
    }

    ui_history_clear_rows();
    s_fill_active = 1U;
    lv_async_call(ui_history_fill_chunk_async, NULL);
}

static void ui_history_queue_refresh(void)
{
    if(s_refresh_queued) {
        return;
    }
    s_refresh_queued = 1U;
    lv_async_call(ui_history_refresh_async, NULL);
}

static void ui_history_refresh_async(void * user_data)
{
    (void)user_data;
    s_refresh_queued = 0U;
    UI_History_Refresh();
}

void UI_History_Refresh(void)
{
    char     total[24];
    uint32_t count;

    ui_history_ensure_screen();
    if(s_scr_history == NULL) {
        return;
    }

    (void)HistoryStore_Ensure();
    if(!HistoryStore_IsReady()) {
        if(s_lbl_hist_empty) {
            lv_obj_clear_flag(s_lbl_hist_empty, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_lbl_hist_empty, UI_STR_HIST_NORFLASH);
        }
        if(s_lbl_hist_total) {
            lv_label_set_text(s_lbl_hist_total, "共 0 条");
        }
        ui_history_update_page_controls(0U);
        ui_history_start_fill(0U);
        return;
    }

    count = HistoryStore_GetCount();
    if(s_lbl_hist_total) {
        snprintf(total, sizeof(total), UI_STR_HIST_TOTAL_FMT,
                 (unsigned long)HistoryStore_GetTotal());
        lv_label_set_text(s_lbl_hist_total, total);
    }

    if(count == 0U) {
        if(s_lbl_hist_empty) {
            lv_obj_clear_flag(s_lbl_hist_empty, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(s_lbl_hist_empty, UI_STR_HIST_EMPTY);
        }
        ui_history_update_page_controls(0U);
        ui_history_start_fill(0U);
        return;
    }

    if(s_lbl_hist_empty) {
        lv_obj_add_flag(s_lbl_hist_empty, LV_OBJ_FLAG_HIDDEN);
    }

    ui_history_update_page_controls(count);
    ui_history_start_fill(count);
}

void UI_History_NotifyDirty(void)
{
    if(UI_GetActivePage() == UI_PAGE_HISTORY) {
        ui_history_queue_refresh();
    }
}

void UI_History_ResetPage(void)
{
    s_hist_page = 0U;
}

lv_obj_t * UI_History_GetScreen(void)
{
    return s_scr_history;
}

static void ui_history_on_prev(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    s_hist_page++;
    s_fill_active = 0U;
    ui_history_queue_refresh();
}

static void ui_history_on_next(lv_event_t * e)
{
    (void)e;
    PowerMgr_OnUserActivity();
    if(s_hist_page > 0U) {
        s_hist_page--;
        s_fill_active = 0U;
        ui_history_queue_refresh();
    }
}

void UI_History_Init(void)
{
    s_hist_page = 0U;
    s_fill_active = 0U;
    s_refresh_queued = 0U;
}

static void ui_history_build_screen(void)
{
    lv_obj_t * top;
    lv_obj_t * title;
    lv_obj_t * nav_row;
    lv_obj_t * lbl;
    lv_coord_t row_y;
    uint32_t   i;

    if(s_scr_history != NULL) {
        return;
    }

    s_scr_history = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr_history, lv_color_hex(0xE3F2FD), 0);
    lv_obj_clear_flag(s_scr_history, LV_OBJ_FLAG_SCROLLABLE);

    top = lv_obj_create(s_scr_history);
    lv_obj_set_size(top, LV_DISP_HOR_RES, UI_HISTORY_TOP_H);
    lv_obj_align(top, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_pad_hor(top, 6, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_radius(top, 0, 0);
    lv_obj_set_style_border_width(top, 0, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(0x1565C0), 0);
    lv_obj_clear_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    title = lv_label_create(top);
    lv_label_set_text(title, UI_STR_HIST_TITLE);
    ui_obj_set_font_cn(title);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);

    nav_row = lv_obj_create(top);
    lv_obj_set_size(nav_row, 172, UI_HISTORY_TOP_H);
    lv_obj_set_flex_flow(nav_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav_row, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(nav_row, 4, 0);
    lv_obj_set_style_border_width(nav_row, 0, 0);
    lv_obj_set_style_bg_opa(nav_row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(nav_row, LV_OBJ_FLAG_SCROLLABLE);

    s_btn_hist_prev = lv_btn_create(nav_row);
    lv_obj_set_size(s_btn_hist_prev, 40, 24);
    lv_obj_set_style_radius(s_btn_hist_prev, 4, 0);
    lv_obj_set_style_bg_color(s_btn_hist_prev, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(s_btn_hist_prev, ui_history_on_next, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_hist_prev);
    lv_label_set_text(lbl, "<");
    ui_obj_set_font_cn_sm(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1565C0), 0);
    lv_obj_center(lbl);

    s_lbl_hist_page = lv_label_create(nav_row);
    lv_label_set_text(s_lbl_hist_page, UI_STR_HIST_PAGE_EMPTY);
    ui_obj_set_font_cn(s_lbl_hist_page);
    lv_obj_set_style_text_color(s_lbl_hist_page, lv_color_hex(0xE3F2FD), 0);

    s_btn_hist_next = lv_btn_create(nav_row);
    lv_obj_set_size(s_btn_hist_next, 40, 24);
    lv_obj_set_style_radius(s_btn_hist_next, 4, 0);
    lv_obj_set_style_bg_color(s_btn_hist_next, lv_color_hex(0xFFFFFF), 0);
    lv_obj_add_event_cb(s_btn_hist_next, ui_history_on_prev, LV_EVENT_CLICKED, NULL);
    lbl = lv_label_create(s_btn_hist_next);
    lv_label_set_text(lbl, ">");
    ui_obj_set_font_cn_sm(lbl);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x1565C0), 0);
    lv_obj_center(lbl);

    s_lbl_hist_total = lv_label_create(top);
    lv_label_set_text(s_lbl_hist_total, "共 0 条");
    ui_obj_set_font_cn(s_lbl_hist_total);
    lv_obj_set_style_text_color(s_lbl_hist_total, lv_color_hex(0xE3F2FD), 0);

    for(i = 0; i < UI_HISTORY_PAGE_SIZE; i++) {
        row_y = (lv_coord_t)(UI_HISTORY_TOP_H + (lv_coord_t)i * UI_HISTORY_ROW_H);
        lbl = lv_label_create(s_scr_history);
        lv_obj_set_pos(lbl, 4, row_y);
        lv_obj_set_size(lbl, LV_DISP_HOR_RES - 8, UI_HISTORY_ROW_H);
        lv_label_set_long_mode(lbl, LV_LABEL_LONG_DOT);
        ui_history_set_row_font(lbl);
        lv_obj_set_style_pad_all(lbl, 0, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(0x263238), 0);
        lv_obj_add_flag(lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(lbl, LV_OBJ_FLAG_SCROLLABLE);
        s_row_labels[i] = lbl;
    }

    s_lbl_hist_empty = lv_label_create(s_scr_history);
    lv_label_set_text(s_lbl_hist_empty, UI_STR_HIST_EMPTY);
    ui_obj_set_font_cn(s_lbl_hist_empty);
    lv_obj_set_style_text_color(s_lbl_hist_empty, lv_color_hex(0x616161), 0);
    lv_obj_align(s_lbl_hist_empty, LV_ALIGN_TOP_MID, 0, 100);

    UI_AddBottomNav(s_scr_history, UI_PAGE_HISTORY);
}

void UI_History_Ensure(void)
{
    ui_history_ensure_screen();
}

void UI_History_Destroy(void)
{
    uint32_t i;
    lv_obj_t * scr = s_scr_history;

    s_fill_active = 0U;
    s_refresh_queued = 0U;
    if(scr == NULL) {
        return;
    }
    s_scr_history = NULL;
    s_lbl_hist_total = NULL;
    s_lbl_hist_page = NULL;
    s_btn_hist_prev = NULL;
    s_btn_hist_next = NULL;
    s_lbl_hist_empty = NULL;
    for(i = 0U; i < UI_HISTORY_PAGE_SIZE; i++) {
        s_row_labels[i] = NULL;
    }
    ui_scr_del(scr);
}

static void ui_history_ensure_screen(void)
{
    if(s_scr_history == NULL) {
        ui_history_build_screen();
    }
}
