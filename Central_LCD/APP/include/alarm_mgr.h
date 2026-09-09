#ifndef __ALARM_MGR_H
#define __ALARM_MGR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ALARM_SIGNAL_LOST = 0,
    ALARM_BLE_LOST,
    ALARM_SIGNAL_WEAK,
    ALARM_PREFLIGHT_FAIL,
    ALARM_PWV_TREND_HIGH,
    ALARM_PWV_TREND_LOW,
    ALARM_MEAS_START,
    ALARM_MEAS_DONE,
    ALARM_FIRST_PWV,
    ALARM_LINK_OK,
    ALARM_TYPE_COUNT
} alarm_type_t;

void AlarmMgr_Init(void);
void AlarmMgr_OnSessionStart(void);
void AlarmMgr_OnSessionStop(void);
void AlarmMgr_Raise(alarm_type_t type);
void AlarmMgr_Clear(alarm_type_t type);
void AlarmMgr_Stop(void);
uint8_t AlarmMgr_IsActive(void);

#ifdef __cplusplus
}
#endif

#endif /* __ALARM_MGR_H */
