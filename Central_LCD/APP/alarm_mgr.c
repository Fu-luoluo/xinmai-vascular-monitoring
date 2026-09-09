#include "alarm_mgr.h"
#include "bsp_buzzer.h"
#include "ui_alarm.h"
#include "voice_mgr.h"
#include "CONFIG.h"

#define ALARM_EVT_STEP           0x0001
#define ALARM_STEP_MS            50U
#define ALARM_DEDUP_MS           5000U

#define ALARM_LVL_INFO           1U
#define ALARM_LVL_WARN           2U
#define ALARM_LVL_CRITICAL       3U

typedef struct {
    uint8_t on_ticks;
    uint8_t off_ticks;
} alarm_step_t;

#define ALARM_LOOP_ONCE          0U
#define ALARM_LOOP_FOREVER       255U

typedef struct {
    const alarm_step_t *steps;
    uint8_t             step_count;
    uint8_t             level;
    uint8_t             loop_count;
} alarm_pattern_t;

/* 传感器脱落：嘀嘀嘀 + 停顿 + 嘀嘀嘀（100ms 响 / 100ms 停，组间约 400ms） */
static const alarm_step_t s_pat_signal_lost_6[] = {
    {2, 2}, {2, 2}, {2, 8},
    {2, 2}, {2, 2}, {2, 0}
};

static const alarm_step_t s_pat_warn_2[] = {
    {3, 4}, {3, 0}
};

/* 短促单「滴」：50ms */
static const alarm_step_t s_pat_tick_short[] = {
    {1, 0}
};

static const alarm_step_t s_pat_ok_2[] = {
    {2, 2}, {2, 0}
};

static const alarm_pattern_t s_patterns[] = {
    [ALARM_SIGNAL_LOST]     = { s_pat_signal_lost_6, 6, ALARM_LVL_CRITICAL, ALARM_LOOP_ONCE     },
    [ALARM_BLE_LOST]        = { s_pat_signal_lost_6, 6, ALARM_LVL_CRITICAL, ALARM_LOOP_ONCE     },
    [ALARM_SIGNAL_WEAK]     = { s_pat_warn_2,     2, ALARM_LVL_WARN,     ALARM_LOOP_ONCE     },
    [ALARM_PREFLIGHT_FAIL]  = { s_pat_tick_short, 1, ALARM_LVL_WARN,     ALARM_LOOP_ONCE     },
    [ALARM_PWV_TREND_HIGH]  = { s_pat_warn_2,     2, ALARM_LVL_WARN,     ALARM_LOOP_ONCE     },
    [ALARM_PWV_TREND_LOW]   = { s_pat_warn_2,     2, ALARM_LVL_WARN,     ALARM_LOOP_ONCE     },
    [ALARM_MEAS_START]      = { s_pat_tick_short, 1, ALARM_LVL_INFO,     ALARM_LOOP_ONCE     },
    [ALARM_MEAS_DONE]       = { s_pat_tick_short, 1, ALARM_LVL_INFO,     ALARM_LOOP_ONCE     },
    [ALARM_FIRST_PWV]       = { s_pat_ok_2,       2, ALARM_LVL_INFO,     ALARM_LOOP_ONCE     },
    [ALARM_LINK_OK]         = { s_pat_tick_short, 1, ALARM_LVL_INFO,     ALARM_LOOP_ONCE     },
};

static uint8_t  s_alarmTaskId = TASK_NO_TASK;
static uint8_t  s_playing = 0U;
static uint8_t  s_stepIdx = 0U;
static uint8_t  s_phaseOn = 0U;
static uint8_t  s_ticksLeft = 0U;
static uint8_t  s_loopsLeft = 0U;
static const alarm_pattern_t *s_activePat = NULL;
static uint32_t s_lastRaiseTick[ALARM_TYPE_COUNT];
static uint8_t  s_sessionRaised[ALARM_TYPE_COUNT];
static uint8_t  s_pendingVoiceTrack = 0U;

static uint16_t AlarmMgr_ProcessEvent(uint8_t task_id, uint16_t events);
static uint8_t  alarm_is_session_type(alarm_type_t type);
static void     alarm_start_pattern(const alarm_pattern_t *pat);
static void     alarm_stop_buzzer(void);
static void     alarm_finish_pattern(void);
static void     alarm_pattern_step(void);
static void     alarm_pattern_restart(void);
static uint8_t  alarm_voice_track_for_type(alarm_type_t type);
static void     alarm_play_pending_voice(void);

static uint8_t alarm_is_session_type(alarm_type_t type)
{
    switch(type) {
    case ALARM_SIGNAL_WEAK:
    case ALARM_FIRST_PWV:
    case ALARM_PWV_TREND_HIGH:
    case ALARM_PWV_TREND_LOW:
        return 1U;
    default:
        return 0U;
    }
}

static void alarm_start_pattern(const alarm_pattern_t *pat)
{
    if(pat == NULL || pat->step_count == 0U) {
        return;
    }

    s_activePat = pat;
    s_playing = 1U;
    s_loopsLeft = pat->loop_count;
    s_stepIdx = 0U;
    s_phaseOn = 1U;
    s_ticksLeft = pat->steps[0].on_ticks;
    if(s_ticksLeft == 0U) {
        alarm_finish_pattern();
        return;
    }
    Buzzer_On();
    if(s_alarmTaskId != TASK_NO_TASK) {
        tmos_start_task(s_alarmTaskId, ALARM_EVT_STEP,
                        MS1_TO_SYSTEM_TIME(ALARM_STEP_MS));
    }
}

static void alarm_stop_buzzer(void)
{
    s_playing = 0U;
    s_activePat = NULL;
    s_stepIdx = 0U;
    s_phaseOn = 0U;
    s_ticksLeft = 0U;
    s_loopsLeft = 0U;
    Buzzer_Off();
    if(s_alarmTaskId != TASK_NO_TASK) {
        tmos_stop_task(s_alarmTaskId, ALARM_EVT_STEP);
    }
}

static void alarm_play_pending_voice(void)
{
    uint8_t track = s_pendingVoiceTrack;

    s_pendingVoiceTrack = 0U;
    if(track != 0U) {
        Voice_PlayAfterBuzzer(track);
    }
}

static void alarm_finish_pattern(void)
{
    alarm_stop_buzzer();
    alarm_play_pending_voice();
}

static void alarm_pattern_restart(void)
{
    if(s_activePat == NULL) {
        return;
    }
    s_stepIdx = 0U;
    s_phaseOn = 1U;
    s_ticksLeft = s_activePat->steps[0].on_ticks;
    if(s_ticksLeft == 0U) {
        alarm_finish_pattern();
        return;
    }
    Buzzer_On();
}

static void alarm_pattern_step(void)
{
    const alarm_step_t *step;

    if(!s_playing || s_activePat == NULL) {
        alarm_stop_buzzer();
        return;
    }

    if(s_ticksLeft > 0U) {
        s_ticksLeft--;
        if(s_ticksLeft > 0U) {
            return;
        }
    }

    step = &s_activePat->steps[s_stepIdx];
    if(s_phaseOn) {
        Buzzer_Off();
        s_phaseOn = 0U;
        s_ticksLeft = step->off_ticks;
        if(s_ticksLeft == 0U) {
            s_stepIdx++;
            if(s_stepIdx >= s_activePat->step_count) {
                if(s_loopsLeft == ALARM_LOOP_FOREVER) {
                    alarm_pattern_restart();
                    return;
                }
                if(s_loopsLeft > 1U) {
                    s_loopsLeft--;
                    alarm_pattern_restart();
                    return;
                }
                alarm_finish_pattern();
                return;
            }
            step = &s_activePat->steps[s_stepIdx];
            s_phaseOn = 1U;
            s_ticksLeft = step->on_ticks;
            if(s_ticksLeft == 0U) {
                alarm_stop_buzzer();
                return;
            }
            Buzzer_On();
        }
    } else {
        s_stepIdx++;
        if(s_stepIdx >= s_activePat->step_count) {
            if(s_loopsLeft == ALARM_LOOP_FOREVER) {
                alarm_pattern_restart();
                return;
            }
            if(s_loopsLeft > 1U) {
                s_loopsLeft--;
                alarm_pattern_restart();
                return;
            }
            alarm_finish_pattern();
            return;
        }
        step = &s_activePat->steps[s_stepIdx];
        s_phaseOn = 1U;
        s_ticksLeft = step->on_ticks;
        if(s_ticksLeft == 0U) {
            alarm_stop_buzzer();
            return;
        }
        Buzzer_On();
    }
}

static uint8_t alarm_voice_track_for_type(alarm_type_t type)
{
    switch(type) {
    case ALARM_SIGNAL_LOST:
        return VOICE_TRACK_REWEAR;
    case ALARM_BLE_LOST:
        return VOICE_TRACK_BLE_LOST;
    case ALARM_SIGNAL_WEAK:
        return VOICE_TRACK_SIGNAL_WEAK;
    case ALARM_PREFLIGHT_FAIL:
        return VOICE_TRACK_WEAR_HINT;
    case ALARM_MEAS_START:
        return VOICE_TRACK_MEAS_START;
    case ALARM_MEAS_DONE:
        return VOICE_TRACK_MEAS_DONE;
    case ALARM_FIRST_PWV:
        return VOICE_TRACK_MEAS_NORMAL;
    case ALARM_LINK_OK:
        return 0U; /* 双端连上仅短蜂鸣，不播报语音 */
    default:
        return 0U;
    }
}

static void alarm_ui_for_type(alarm_type_t type)
{
    switch(type) {
    case ALARM_SIGNAL_WEAK:
        UI_Alarm_ShowWeakSignal();
        break;
    case ALARM_PREFLIGHT_FAIL:
        UI_Alarm_FlashPreflightHint();
        break;
    case ALARM_PWV_TREND_HIGH:
        UI_Alarm_SetPwvTrend(1);
        break;
    case ALARM_PWV_TREND_LOW:
        UI_Alarm_SetPwvTrend(-1);
        break;
    default:
        break;
    }
}

void AlarmMgr_Init(void)
{
    uint8_t i;

    Buzzer_Init();
    for(i = 0; i < ALARM_TYPE_COUNT; i++) {
        s_lastRaiseTick[i] = 0U;
        s_sessionRaised[i] = 0U;
    }
    s_alarmTaskId = TMOS_ProcessEventRegister(AlarmMgr_ProcessEvent);
}

void AlarmMgr_OnSessionStart(void)
{
    uint8_t i;

    for(i = 0; i < ALARM_TYPE_COUNT; i++) {
        if(alarm_is_session_type((alarm_type_t)i)) {
            s_sessionRaised[i] = 0U;
        }
    }
    UI_Alarm_ClearWeakSignal();
    UI_Alarm_SetPwvTrend(0);
}

void AlarmMgr_OnSessionStop(void)
{
    UI_Alarm_ClearWeakSignal();
}

void AlarmMgr_Raise(alarm_type_t type)
{
    const alarm_pattern_t *pat;
    uint32_t now;
    uint8_t level;

    if(type >= ALARM_TYPE_COUNT) {
        return;
    }

    now = TMOS_GetSystemClock();
    if((now - s_lastRaiseTick[type]) < MS1_TO_SYSTEM_TIME(ALARM_DEDUP_MS)) {
        return;
    }
    if(alarm_is_session_type(type) && s_sessionRaised[type]) {
        return;
    }

    pat = &s_patterns[type];
    level = pat->level;

    if(s_playing && s_activePat != NULL && level < s_activePat->level) {
        return;
    }
    if(s_playing && level >= s_activePat->level) {
        alarm_stop_buzzer();
    }

    s_lastRaiseTick[type] = now;
    if(alarm_is_session_type(type)) {
        s_sessionRaised[type] = 1U;
    }

    s_pendingVoiceTrack = alarm_voice_track_for_type(type);
    alarm_ui_for_type(type);
    alarm_start_pattern(pat);
}

void AlarmMgr_Clear(alarm_type_t type)
{
    switch(type) {
    case ALARM_SIGNAL_WEAK:
        UI_Alarm_ClearWeakSignal();
        break;
    default:
        break;
    }
    (void)type;
}

void AlarmMgr_Stop(void)
{
    alarm_stop_buzzer();
}

uint8_t AlarmMgr_IsActive(void)
{
    return (uint8_t)(s_playing || Voice_IsBusy());
}

static uint16_t AlarmMgr_ProcessEvent(uint8_t task_id, uint16_t events)
{
    (void)task_id;

    if(events & ALARM_EVT_STEP) {
        alarm_pattern_step();
        if(s_playing && s_alarmTaskId != TASK_NO_TASK) {
            tmos_start_task(s_alarmTaskId, ALARM_EVT_STEP,
                            MS1_TO_SYSTEM_TIME(ALARM_STEP_MS));
        }
        events ^= ALARM_EVT_STEP;
    }
    return events;
}
