/**
 * @file ui_scr_del.h
 * Sync-delete LVGL screens/containers.
 *
 * `lv_obj_del_async()` may postpone heap reclamation, and with tight LVGL
 * memory this can lead to OOM/interaction freeze after several page switches.
 */
#ifndef UI_SCR_DEL_H
#define UI_SCR_DEL_H

#include "lvgl.h"

static inline void ui_scr_del(lv_obj_t * scr)
{
    if(scr == NULL) {
        return;
    }
    /* Do not delete the currently active screen. Caller should switch screen first. */
    if(lv_scr_act() == scr) {
        return;
    }
    lv_obj_del(scr);
}

#endif /* UI_SCR_DEL_H */

