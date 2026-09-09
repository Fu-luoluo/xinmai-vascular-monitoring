#ifndef UI_HISTORY_H
#define UI_HISTORY_H

#include "lvgl.h"
#include "history_store.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_HISTORY_PAGE_SIZE  8u  /* 少对象、少 SPI，降低进页 OOM/卡顿 */

void     UI_History_Init(void);
void     UI_History_Ensure(void);
void     UI_History_Destroy(void);
lv_obj_t * UI_History_GetScreen(void);
void     UI_History_ResetPage(void);
void     UI_History_Refresh(void);
void     UI_History_NotifyDirty(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_HISTORY_H */
