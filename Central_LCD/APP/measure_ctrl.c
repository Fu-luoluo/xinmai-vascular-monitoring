#include "measure_ctrl.h"
#include "central.h"
#include "pwv.h"
#include "ui.h"
#include "alarm_mgr.h"
#include "power_mgr.h"
#include "cloud_report.h"
#include "history_store.h"
#include "CONFIG.h"

#define MEASURE_EVT_TIMEOUT      0x0001
#define MEASURE_EVT_TICK         0x0002

typedef enum
{
    MEASURE_IDLE = 0,
    MEASURE_RUNNING,
    MEASURE_RESULT_HOLD
} measureState_t;

static measureState_t s_state = MEASURE_IDLE;
static uint8_t        s_measureTaskId = TASK_NO_TASK;
static uint32_t       s_sessionStartTick = 0;
static uint32_t       s_lastValidTick[2] = {0, 0};
static uint8_t        s_seenNotify[2] = {0, 0};
static uint8_t        s_gotValidWrist = 0;
static uint8_t        s_gotValidFinger = 0;
static uint8_t        s_signalWeakRaised = 0;

static void measure_stop_sampling(void);
static void measure_stop_timers(void);
static void measure_cancel(measureEndReason_t reason);
static void measure_complete(measureEndReason_t reason);
static void measure_check_signal_lost(void);
static uint8_t measure_sample_valid(uint8_t side, int hr, int spo2);
static uint8_t measure_has_any_data(void);
static uint16_t Measure_ProcessEvent(uint8_t task_id, uint16_t events);

static void measure_stop_sampling(void)
{
    PWV_OnMeasurementStop();
    Central_StopMeasurement();
}

static void measure_stop_timers(void)
{
    if(s_measureTaskId != TASK_NO_TASK) {
        tmos_stop_task(s_measureTaskId, MEASURE_EVT_TIMEOUT);
        tmos_stop_task(s_measureTaskId, MEASURE_EVT_TICK);
    }
}

static uint8_t measure_sample_valid(uint8_t side, int hr, int spo2)
{
    if(hr <= 0 || spo2 > 100) {
        return 0U;
    }
    if(side == MEASURE_SIDE_WRIST) {
        if(hr < 42 || hr > 100) {
            return 0U;
        }
        if(spo2 < (int)PWV_WRIST_SPO2_MIN) {
            return 0U;
        }
    } else {
        if(hr < 42 || hr > 100) {
            return 0U;
        }
        if(spo2 < (int)PWV_FINGER_SPO2_MIN) {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t measure_has_any_data(void)
{
    pwv_session_t snap;

    if(s_gotValidWrist || s_gotValidFinger) {
        return 1U;
    }
    if(PWV_GetSessionSnapshot(&snap) && (snap.pwv_valid ||
       snap.w_hr || snap.f_hr)) {
        return 1U;
    }
    return 0U;
}

static void measure_cancel(measureEndReason_t reason)
{
    (void)reason;

    if(s_state == MEASURE_IDLE) {
        return;
    }
    measure_stop_timers();
    measure_stop_sampling();
    s_state = MEASURE_IDLE;
    AlarmMgr_OnSessionStop();
    UI_ClearVitals();
    if(reason == MEASURE_END_SIGNAL_LOST) {
        UI_SetMeasureStatus(MEASURE_UI_FAILED_NO_SIGNAL);
        AlarmMgr_Raise(ALARM_SIGNAL_LOST);
    } else {
        UI_ClearMeasureStatus();
    }
    UI_NotifyMeasureState();
}

static void measure_complete(measureEndReason_t reason)
{
    pwv_session_t snap;

    if(s_state != MEASURE_RUNNING) {
        return;
    }
    if(PWV_GetSessionSnapshot(&snap)) {
        UI_ApplySessionResult(&snap);
    }
    measure_stop_timers();
    measure_stop_sampling();
    s_state = MEASURE_RESULT_HOLD;
    PowerMgr_OnUserActivity();
    UI_ClearMeasureStatus();
    AlarmMgr_Raise(ALARM_MEAS_DONE);
    AlarmMgr_OnSessionStop();
    CloudReport_SessionEnd(reason, &snap);
    if(HistoryStore_IsReady()) {
        if(HistoryStore_AppendSession(reason, &snap) == 0) {
            UI_NotifyHistoryDirty();
        }
    }
    UI_NotifyMeasureState();
}

static uint8_t measure_side_signal_stale(uint8_t side, uint32_t now,
                                         uint32_t lostTicks, uint32_t bootTicks)
{
    if(s_seenNotify[side]) {
        return ((now - s_lastValidTick[side]) > lostTicks) ? 1U : 0U;
    }
    return ((now - s_sessionStartTick) > bootTicks) ? 1U : 0U;
}

static void measure_check_signal_lost(void)
{
    uint32_t now;
    uint32_t graceTicks;
    uint32_t lostTicks;
    uint32_t bootTicks;
    uint32_t weakTicks;
    pwv_session_t snap;

    if(s_state != MEASURE_RUNNING) {
        return;
    }

    now = TMOS_GetSystemClock();
    graceTicks = MS1_TO_SYSTEM_TIME(MEASURE_SIGNAL_GRACE_SEC * 1000U);
    lostTicks = MS1_TO_SYSTEM_TIME(MEASURE_SIGNAL_LOST_SEC * 1000U);
    bootTicks = MS1_TO_SYSTEM_TIME(MEASURE_SIGNAL_FIRST_NOTIFY_SEC * 1000U);
    weakTicks = MS1_TO_SYSTEM_TIME(MEASURE_SIGNAL_WEAK_SEC * 1000U);

    if((now - s_sessionStartTick) >= graceTicks &&
       (now - s_sessionStartTick) >= weakTicks &&
       !s_signalWeakRaised)
    {
        if(PWV_GetSessionSnapshot(&snap) && !snap.pwv_valid) {
            s_signalWeakRaised = 1U;
            AlarmMgr_Raise(ALARM_SIGNAL_WEAK);
        }
    }

    if((now - s_sessionStartTick) < graceTicks) {
        return;
    }
    if(!measure_side_signal_stale(MEASURE_SIDE_WRIST, now, lostTicks, bootTicks)) {
        return;
    }
    if(!measure_side_signal_stale(MEASURE_SIDE_FINGER, now, lostTicks, bootTicks)) {
        return;
    }

    if(measure_has_any_data()) {
        pwv_session_t snap;

        if(PWV_GetSessionSnapshot(&snap) && snap.pwv_valid) {
            measure_complete(MEASURE_END_SIGNAL_LOST);
        }
        /* 尚无有效 PWV 时不提前结束，继续等腕部追拍 */
    } else {
        measure_cancel(MEASURE_END_SIGNAL_LOST);
    }
}

void Measure_Init(void)
{
    s_state = MEASURE_IDLE;
    s_measureTaskId = TMOS_ProcessEventRegister(Measure_ProcessEvent);
}

void Measure_Start(void)
{
    uint32_t now;

    if(s_state == MEASURE_RUNNING) {
        return;
    }
    if(!Central_CanStartMeasurement()) {
        return;
    }
    /* 上轮残留 vitals（如 w_hr=54）不应阻塞再次测量 */
    if(s_state == MEASURE_RESULT_HOLD) {
        UI_ClearVitals();
        s_state = MEASURE_IDLE;
        UI_NotifyMeasureState(); /* 收起结果浮层 */
    }
    if(!Measure_PreflightOk()) {
        return;
    }
    UI_ClearMeasureStatus();
    PWV_OnMeasurementStart();
    AlarmMgr_OnSessionStart();
    Central_StartMeasurement();
    s_state = MEASURE_RUNNING;
    s_gotValidWrist = 0;
    s_gotValidFinger = 0;
    s_signalWeakRaised = 0;
    s_seenNotify[MEASURE_SIDE_WRIST] = 0U;
    s_seenNotify[MEASURE_SIDE_FINGER] = 0U;
    now = TMOS_GetSystemClock();
    s_sessionStartTick = now;
    s_lastValidTick[MEASURE_SIDE_WRIST] = now;
    s_lastValidTick[MEASURE_SIDE_FINGER] = now;
    PowerMgr_OnUserActivity();
    if(s_measureTaskId != TASK_NO_TASK) {
        tmos_start_task(s_measureTaskId, MEASURE_EVT_TIMEOUT,
                        MS1_TO_SYSTEM_TIME(MEASURE_SESSION_SEC * 1000U));
        tmos_start_task(s_measureTaskId, MEASURE_EVT_TICK,
                        MS1_TO_SYSTEM_TIME(1000U));
    }
    AlarmMgr_Raise(ALARM_MEAS_START);
    UI_NotifyMeasureState();
}

void Measure_Stop(void)
{
    pwv_session_t snap;

    if(s_state != MEASURE_RUNNING) {
        return;
    }
    /* 有有效会话中位则定稿冻结，避免「停止」清空结果 */
    if(PWV_GetSessionSnapshot(&snap) && snap.pwv_valid) {
        measure_complete(MEASURE_END_USER_STOP);
    } else {
        measure_cancel(MEASURE_END_USER_STOP);
    }
}

void Measure_OnPause(void)
{
    if(s_state == MEASURE_RUNNING) {
        measure_cancel(MEASURE_END_USER_STOP);
    }
}

void Measure_OnResume(void)
{
}

void Measure_OnSample(uint8_t side, int hr, int spo2)
{
    uint32_t now;

    if(s_state != MEASURE_RUNNING) {
        return;
    }
    if(side > MEASURE_SIDE_WRIST) {
        return;
    }
    now = TMOS_GetSystemClock();
    /* 任意 paced notify（含暖机 HR=0）视为链路存活，与 PWV 临床门控分离 */
    if(spo2 > 0 && spo2 <= 100) {
        s_lastValidTick[side] = now;
        s_seenNotify[side] = 1U;
    }

    if(!measure_sample_valid(side, hr, spo2)) {
        return;
    }

    s_lastValidTick[side] = now;
    s_seenNotify[side] = 1U;
    if(side == MEASURE_SIDE_WRIST) {
        s_gotValidWrist = 1U;
    } else {
        s_gotValidFinger = 1U;
    }
}

void Measure_NotifyLinkLost(void)
{
    if(s_state == MEASURE_RUNNING) {
        measure_stop_timers();
        PWV_OnMeasurementStop();
        s_state = MEASURE_IDLE;
        UI_ClearVitals();
        UI_SetMeasureStatus(MEASURE_UI_FAILED_BLE_LOST);
        AlarmMgr_Raise(ALARM_BLE_LOST);
        AlarmMgr_OnSessionStop();
        UI_NotifyMeasureState();
        Central_StopMeasurement();
    }
}

void Measure_OnPulseTargetReached(void)
{
    if(s_state != MEASURE_RUNNING) {
        return;
    }
    measure_complete(MEASURE_END_PULSE_TARGET);
}

uint8_t Measure_IsActive(void)
{
    return (s_state == MEASURE_RUNNING) ? 1U : 0U;
}

uint8_t Measure_IsResultHold(void)
{
    return (s_state == MEASURE_RESULT_HOLD) ? 1U : 0U;
}

uint8_t Measure_CanStart(void)
{
    return Central_CanStartMeasurement();
}

uint8_t Measure_PreflightOk(void)
{
    return UI_PreflightOk();
}

static uint16_t Measure_ProcessEvent(uint8_t task_id, uint16_t events)
{
    (void)task_id;

    if(events & MEASURE_EVT_TIMEOUT) {
        measure_complete(MEASURE_END_TIMEOUT);
        events ^= MEASURE_EVT_TIMEOUT;
    }

    if(events & MEASURE_EVT_TICK) {
        measure_check_signal_lost();
        if(s_state == MEASURE_RUNNING && s_measureTaskId != TASK_NO_TASK) {
            tmos_start_task(s_measureTaskId, MEASURE_EVT_TICK,
                            MS1_TO_SYSTEM_TIME(1000U));
        }
        events ^= MEASURE_EVT_TICK;
    }

    return events;
}
