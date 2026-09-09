/**
 * LVGL OOM 软复位声明（无芯片头，可被所有 LVGL 源安全包含）
 */
#ifndef UI_LVGL_OOM_H
#define UI_LVGL_OOM_H

#ifdef __cplusplus
extern "C" {
#endif

void ui_lvgl_oom_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_LVGL_OOM_H */
