/**
 * @file ui.h
 * Central_LCD LVGL UI — Home / Monitor / History + WiFi overlay
 */
#ifndef UI_H
#define UI_H

#include <stdint.h>
#include "lvgl.h"
#include "pwv.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MEASURE_UI_STATUS_NONE            0
#define MEASURE_UI_FAILED_NO_SIGNAL       1
#define MEASURE_UI_FAILED_BLE_LOST        2

typedef enum {
    UI_PAGE_HOME = 0,
    UI_PAGE_MONITOR,
    UI_PAGE_HISTORY,
    UI_PAGE_SIGNAL_LOST
} ui_page_t;

void UI_Init(void);
void UI_Refresh(void);
void UI_NotifyMeasureState(void);
void UI_ClearVitals(void);
void UI_SetMeasureStatus(uint8_t code);
void UI_ClearMeasureStatus(void);

uint8_t UI_PreflightOk(void);
const char * UI_PreflightHint(void);

void UI_SetWrist(uint8_t hr, uint8_t spo2);
void UI_SetFinger(uint8_t hr, uint8_t spo2);
void UI_SetPwv(float pwv_mps);
void UI_SetSignalQuality(uint8_t quality_pct);
void UI_SetGateHint(const char * hint);
void UI_ClearGateHint(void);
void UI_SetLink(uint8_t wrist_ok, uint8_t finger_ok,
                uint8_t wrist_connecting, uint8_t finger_connecting);
void UI_ApplySessionResult(const pwv_session_t *snap);

void UI_LoadHomeScreen(void);
void UI_UnloadHomeScreen(void);
void UI_ReturnHomeFromOverlay(void);
void UI_ShowHome(void);
void UI_ShowMonitor(void);
void UI_ShowHistory(void);
void UI_ReleaseSecondaryScreens(void);
void UI_NotifyHistoryDirty(void);
void UI_RefreshHome(void);

ui_page_t UI_GetActivePage(void);
void      UI_AddBottomNav(lv_obj_t * parent, ui_page_t active_page);

#ifdef __cplusplus
}
#endif

#endif /* UI_H */
