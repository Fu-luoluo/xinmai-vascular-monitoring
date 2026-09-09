#ifndef UI_FONT_H
#define UI_FONT_H

#include "lvgl.h"

/* UI 中文字库：仅保留 18 号，省约 26KB FLASH；界面统一用 18 */

LV_FONT_DECLARE(lv_font_source_han_sans_bold_18);

static inline void ui_obj_set_font_cn(lv_obj_t * obj)
{
    if(obj != NULL) {
        lv_obj_set_style_text_font(obj, &lv_font_source_han_sans_bold_18, LV_PART_MAIN);
    }
}

static inline void ui_obj_set_font_cn_sm(lv_obj_t * obj)
{
    ui_obj_set_font_cn(obj);
}

#endif /* UI_FONT_H */
