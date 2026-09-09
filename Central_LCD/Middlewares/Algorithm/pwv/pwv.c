#include "CONFIG.h"
#include "bsp_uart.h"
#include "pwv.h"
#include "pwv_calib.h"
#include "pwv_ml_gate.h"
#include "ui.h"
#include "ui_strings.h"
#include "ui_wifi.h"
#include "alarm_mgr.h"
#include "host_link.h"
#include <stdio.h>
#include <math.h>

#define PWV_MIN_HR                  35U
#define PWV_MAX_HR                  180U
#define PWV_MIN_SPO2                70U
#define PWV_MAX_SPO2                100U
#define PWV_MIN_MPS                 3.0f
#define PWV_MAX_MPS                 20.0f
#define PWV_UART1_PERIOD_MS         1000U
#define PWV_SYNC_MAX_AGE_MS         8000U

typedef struct
{
    uint8_t  valid;
    uint8_t  hr;
    uint8_t  spo2;
    uint8_t  pi_x10;
    uint16_t beatIdx;
    uint32_t beatTs;
    uint32_t tick;
    pwv_wf_compact_t wf;
} pwvSample_t;

typedef struct
{
    uint8_t  valid;
    uint16_t pulseId;
    uint8_t  hr;
    uint8_t  spo2;
    uint8_t  pi_x10;
    uint16_t beatIdx;
    uint32_t beatTs;
    uint32_t tick;
    pwv_wf_compact_t wf;
} pwvPending_t;

static pwvSample_t  s_wrist = {0};
static pwvSample_t  s_finger = {0};
static pwvPending_t s_wristPending = {0};
static pwvPending_t s_fingerPending = {0};
static uint16_t     s_targetPulseId = 0U;
static uint16_t     s_lastAdvancedPulseId = 0U;
static uint16_t     s_wristRejectPulseId = 0U;
static uint16_t     s_fingerRejectPulseId = 0U;
static uint8_t      s_awaitNext = 0U;
static float        s_lastPwv = 0.0f;
static float        s_lastPwvRaw = 0.0f;
static uint8_t      s_hasLastPwv = 0U;
static uint8_t      s_hasLastVitals = 0U;
static uint8_t      s_lastWristHr = 0U;
static uint8_t      s_lastWristSpo2 = 0U;
static uint8_t      s_lastFingerHr = 0U;
static uint8_t      s_lastFingerSpo2 = 0U;
static uint32_t     s_lastUart1Tick = 0U;
static uint8_t      s_measureEnabled = 0U;
static uint8_t      s_signalQuality = PWV_QUALITY_IDLE;
static float        s_pttRing[PWV_PTT_CV_RING];
static uint8_t      s_pttRingCnt = 0U;
static uint8_t      s_pttRingIdx = 0U;
static uint8_t      s_lastWristPi = 0U;
static uint8_t      s_lastFingerPi = 0U;
static float        s_sessionPwv[PWV_SESSION_RING];
static uint8_t      s_sessionCnt = 0U;
static uint8_t      s_sessionIdx = 0U;

static uint8_t pwv_begin_advance(void);

static uint8_t pwv_clamp_u8(int v, uint8_t lo, uint8_t hi)
{
    if(v < (int)lo) {
        return lo;
    }
    if(v > (int)hi) {
        return hi;
    }
    return (uint8_t)v;
}

static uint8_t pwv_pi_score(uint8_t pi_min_x10)
{
    int span = (int)PWV_PI_GOOD_X10 - (int)PWV_PI_WRIST_MIN_X10;

    if(span <= 0) {
        return pi_min_x10 >= PWV_PI_WRIST_MIN_X10 ? 100U : 0U;
    }
    if(pi_min_x10 <= PWV_PI_WRIST_MIN_X10) {
        return 0U;
    }
    if(pi_min_x10 >= PWV_PI_GOOD_X10) {
        return 100U;
    }
    return pwv_clamp_u8(((int)pi_min_x10 - (int)PWV_PI_WRIST_MIN_X10) * 100 / span, 0U, 100U);
}

static void pwv_ptt_ring_push(float ptt_ms)
{
    s_pttRing[s_pttRingIdx] = ptt_ms;
    s_pttRingIdx = (uint8_t)((s_pttRingIdx + 1U) % PWV_PTT_CV_RING);
    if(s_pttRingCnt < PWV_PTT_CV_RING) {
        s_pttRingCnt++;
    }
}

/* P3-D2：预览「若纳入候选 PTT」后的 CV，不改环（拒收拍不污染） */
static float pwv_ptt_cv_pct_with(float cand_ms)
{
    float tmp[PWV_PTT_CV_RING];
    float mean = 0.0f;
    float var = 0.0f;
    uint8_t n;
    uint8_t i;

    if(s_pttRingCnt < PWV_PTT_CV_RING) {
        n = (uint8_t)(s_pttRingCnt + 1U);
        for(i = 0U; i < s_pttRingCnt; i++) {
            tmp[i] = s_pttRing[i];
        }
        tmp[s_pttRingCnt] = cand_ms;
    } else {
        n = PWV_PTT_CV_RING;
        for(i = 0U; i < PWV_PTT_CV_RING; i++) {
            tmp[i] = s_pttRing[i];
        }
        tmp[s_pttRingIdx] = cand_ms;
    }

    if(n < 3U) {
        return 0.0f;
    }
    for(i = 0U; i < n; i++) {
        mean += tmp[i];
    }
    mean /= (float)n;
    if(mean < 1.0f) {
        return 0.0f;
    }
    for(i = 0U; i < n; i++) {
        float d = tmp[i] - mean;
        var += d * d;
    }
    return (sqrtf(var / (float)n) / mean) * 100.0f;
}

static float pwv_ptt_cv_pct(void)
{
    float mean = 0.0f;
    float var = 0.0f;
    uint8_t i;

    if(s_pttRingCnt < 3U) {
        return 0.0f;
    }
    for(i = 0U; i < s_pttRingCnt; i++) {
        mean += s_pttRing[i];
    }
    mean /= (float)s_pttRingCnt;
    if(mean < 1.0f) {
        return 0.0f;
    }
    for(i = 0U; i < s_pttRingCnt; i++) {
        float d = s_pttRing[i] - mean;
        var += d * d;
    }
    return (sqrtf(var / (float)s_pttRingCnt) / mean) * 100.0f;
}

static uint8_t pwv_ptt_cv_score(float cv_pct)
{
    if(cv_pct <= 0.0f) {
        return 100U;
    }
    if(cv_pct >= (float)PWV_PTT_CV_MAX_PCT) {
        return 0U;
    }
    return pwv_clamp_u8((int)(100.0f - (cv_pct / (float)PWV_PTT_CV_MAX_PCT) * 100.0f + 0.5f),
                        0U, 100U);
}

static uint8_t pwv_rx_dt_score(float dt_ms)
{
    if(dt_ms <= 400.0f) {
        return 100U;
    }
    if(dt_ms >= (float)PWV_PAIR_LATE_PUBLISH_MAX_RX_MS) {
        return 0U;
    }
    if(dt_ms <= (float)PWV_PAIR_PUBLISH_MAX_RX_MS) {
        return pwv_clamp_u8((int)(100.0f - ((dt_ms - 400.0f) /
                                            ((float)PWV_PAIR_PUBLISH_MAX_RX_MS - 400.0f)) * 40.0f + 0.5f),
                            60U, 100U);
    }
    return pwv_clamp_u8((int)(60.0f - ((dt_ms - (float)PWV_PAIR_PUBLISH_MAX_RX_MS) /
                                       ((float)PWV_PAIR_LATE_PUBLISH_MAX_RX_MS -
                                        (float)PWV_PAIR_PUBLISH_MAX_RX_MS)) * 60.0f + 0.5f),
                        0U, 60U);
}

static uint8_t pwv_soft_penalty_sum(uint8_t bidx_penalty, uint8_t pi_penalty,
                                    uint8_t ml_penalty)
{
    uint16_t sum = (uint16_t)bidx_penalty + (uint16_t)pi_penalty + (uint16_t)ml_penalty;

    if(sum > (uint16_t)PWV_SOFT_PENALTY_CAP) {
        return PWV_SOFT_PENALTY_CAP;
    }
    return (uint8_t)sum;
}

static uint8_t pwv_compute_quality(uint8_t w_pi_x10, uint8_t f_pi_x10, float ptt_cv_pct,
                                   float rx_dt_ms, uint8_t ml_ok, uint8_t bidx_penalty,
                                   uint8_t pi_penalty, uint8_t ml_penalty)
{
    uint8_t pi_min = w_pi_x10;
    uint8_t pi_score;
    uint8_t cv_score;
    uint8_t rx_score;
    uint8_t ml_score = ml_ok ? 100U : 55U;
    uint8_t soft_pen;
    uint8_t q;

    if(f_pi_x10 > 0U && f_pi_x10 < pi_min) {
        pi_min = f_pi_x10;
    }
    if(w_pi_x10 == 0U) {
        pi_min = f_pi_x10;
    }
    pi_score = pwv_pi_score(pi_min);
    cv_score = pwv_ptt_cv_score(ptt_cv_pct);
    rx_score = pwv_rx_dt_score(rx_dt_ms);
    q = (uint8_t)((pi_score + cv_score + rx_score + ml_score + 2U) / 4U);
    soft_pen = pwv_soft_penalty_sum(bidx_penalty, pi_penalty, ml_penalty);
    if(soft_pen > 0U && q > soft_pen) {
        q = (uint8_t)(q - soft_pen);
    }
    else if(soft_pen >= q) {
        q = 0U;
    }
    return q;
}

static void pwv_update_quality_ui(uint16_t pulseId, uint8_t w_pi_x10, uint8_t f_pi_x10,
                                  float rx_dt_ms, uint8_t bidx_penalty, uint8_t pi_penalty)
{
    float cv_pct;
    uint8_t q;

    if(pulseId >= PWV_WARMUP_PULSE_ID && pulseId <= PWV_WARMUP_PULSE_MAX) {
        s_signalQuality = PWV_QUALITY_CALIBRATING;
        UI_SetSignalQuality(PWV_QUALITY_CALIBRATING);
        return;
    }
    cv_pct = pwv_ptt_cv_pct();
    q = pwv_compute_quality(w_pi_x10, f_pi_x10, cv_pct, rx_dt_ms, 1U, bidx_penalty,
                              pi_penalty, 0U);
    s_signalQuality = q;
    UI_SetSignalQuality(q);
}

static uint8_t pwv_pi_gate_penalty(uint8_t w_pi_x10, uint8_t f_pi_x10,
                                   uint8_t w_spo2, uint8_t f_spo2)
{
    /* P3-G-3：指 SpO2≥88（含 lift 楼）不再因低 PI 扣分 */
    if(f_spo2 < 88U && f_pi_x10 > 0U && f_pi_x10 < PWV_PI_FINGER_MIN_X10) {
        return PWV_PI_SOFT_QUALITY_PENALTY;
    }
    if(w_spo2 < 88U && w_pi_x10 > 0U && w_pi_x10 < PWV_PI_WRIST_MIN_X10) {
        return PWV_PI_SOFT_QUALITY_PENALTY;
    }
    return 0U;
}

static uint8_t pwv_pi_gate_ok(uint8_t w_pi_x10, uint8_t f_pi_x10,
                              uint8_t w_spo2, uint8_t f_spo2, uint8_t *pi_penalty)
{
    uint8_t penalty = 0U;

    if(pi_penalty != NULL) {
        *pi_penalty = 0U;
    }
    /* 指 SpO2≥80 放行；≥88 不因硬阈 PI 扣分（P3-G-3） */
    if(f_pi_x10 > 0U && f_pi_x10 < PWV_PI_FINGER_HARD_X10) {
        if(f_spo2 >= 80U) {
            if(f_spo2 < 88U) {
                penalty = PWV_PI_SOFT_QUALITY_PENALTY;
            }
        } else {
            UI_SetGateHint(UI_STR_PREFLIGHT_PI_FINGER);
            return 0U;
        }
    }
    if(w_pi_x10 > 0U && w_pi_x10 < PWV_PI_WRIST_HARD_X10) {
        if(w_spo2 >= 88U) {
            /* 腕≥88 已过有效门，不再软扣 */
        } else if(w_spo2 >= 82U) {
            if(penalty < PWV_PI_SOFT_QUALITY_PENALTY) {
                penalty = PWV_PI_SOFT_QUALITY_PENALTY;
            }
        } else {
            UI_SetGateHint(UI_STR_PREFLIGHT_PI_WRIST);
            return 0U;
        }
    }
    if(penalty == 0U) {
        penalty = pwv_pi_gate_penalty(w_pi_x10, f_pi_x10, w_spo2, f_spo2);
    }
    if(pi_penalty != NULL) {
        *pi_penalty = penalty;
    }
    return 1U;
}

static void pwv_session_clear(void)
{
    s_sessionCnt = 0U;
    s_sessionIdx = 0U;
}

static void pwv_session_push(float pwv, uint8_t q, float rx_dt_ms, float ptt_ms)
{
    if(q < PWV_SESSION_QUALITY_MIN) {
        return;
    }
    /* P3-A：仅准时高质量拍进入会话中位 */
    if(rx_dt_ms > PWV_SESSION_RX_MAX_MS) {
        return;
    }
    /* P3-J：短/长 PTT（Δb=3/6）可发布但不入环，避免 sess 假高/假低 */
    if(ptt_ms < PWV_SESSION_PTT_MIN_MS || ptt_ms > PWV_SESSION_PTT_MAX_MS) {
        return;
    }
    s_sessionPwv[s_sessionIdx] = pwv;
    s_sessionIdx = (uint8_t)((s_sessionIdx + 1U) % PWV_SESSION_RING);
    if(s_sessionCnt < PWV_SESSION_RING) {
        s_sessionCnt++;
    }
}

static float pwv_session_median(void)
{
    float tmp[PWV_SESSION_RING];
    uint8_t n = s_sessionCnt;
    uint8_t i;
    uint8_t j;

    if(n == 0U) {
        return 0.0f;
    }
    for(i = 0U; i < n; i++) {
        tmp[i] = s_sessionPwv[i];
    }
    for(i = 0U; i < n; i++) {
        for(j = (uint8_t)(i + 1U); j < n; j++) {
            if(tmp[i] > tmp[j]) {
                float t = tmp[i];
                tmp[i] = tmp[j];
                tmp[j] = t;
            }
        }
    }
    if((n & 1U) != 0U) {
        return tmp[n / 2U];
    }
    return (tmp[n / 2U - 1U] + tmp[n / 2U]) * 0.5f;
}

static uint8_t pwv_sample_valid(uint8_t isWrist, uint8_t hr, uint8_t spo2)
{
    if(isWrist)
    {
        if(hr < PWV_MIN_WRIST_HR || hr > PWV_MAX_WRIST_HR)
        {
            return 0U;
        }
        if(spo2 < PWV_WRIST_SPO2_MIN || spo2 > PWV_MAX_SPO2)
        {
            return 0U;
        }
    }
    else
    {
        if(hr < PWV_FINGER_HR_PUBLISH_MIN || hr > PWV_FINGER_HR_PUBLISH_MAX)
        {
            return 0U;
        }
        if(spo2 < PWV_FINGER_SPO2_MIN || spo2 > PWV_MAX_SPO2)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t pwv_sample_valid_for_pulse(uint8_t isWrist, uint16_t pulseId,
                                          uint8_t hr, uint8_t spo2)
{
    if(pulseId >= PWV_WARMUP_PULSE_ID && pulseId <= PWV_WARMUP_PULSE_MAX)
    {
        if(spo2 > PWV_MAX_SPO2)
        {
            return 0U;
        }
        if(isWrist)
        {
            if(hr == 0U || spo2 < PWV_WARMUP_WRIST_SPO2_MIN || hr > PWV_MAX_WRIST_HR)
            {
                return 0U;
            }
            if(hr < PWV_MIN_WRIST_HR)
            {
                return 0U;
            }
            return 1U;
        }
        else
        {
            if(spo2 < PWV_WARMUP_FINGER_SPO2_MIN)
            {
                return 0U;
            }
            /* 暖机仅 warmup skip，指 HR 暂态（0 或超发布上限）仍允许配对 */
            return 1U;
        }
    }
    return pwv_sample_valid(isWrist, hr, spo2);
}

static uint8_t pwv_beat_idx_valid(uint8_t isWrist, uint16_t beatIdx)
{
    if(beatIdx < PWV_BEAT_IDX_MIN || beatIdx > PWV_BEAT_IDX_MAX)
    {
        return 0U;
    }
    if(isWrist)
    {
        if(beatIdx > PWV_WRIST_BEAT_IDX_MAX)
        {
            return 0U;
        }
    }
    else
    {
        if(beatIdx < PWV_FINGER_BEAT_IDX_MIN || beatIdx > PWV_FINGER_BEAT_IDX_MAX)
        {
            return 0U;
        }
    }
    return 1U;
}

static uint8_t pwv_rx_gap_ok(float dtMs, int bidxDelta)
{
    if(dtMs <= PWV_PAIR_MAX_RX_MS)
    {
        return 1U;
    }
    if(dtMs <= PWV_PAIR_PUBLISH_MAX_RX_MS)
    {
        return 1U;
    }
    if(dtMs <= PWV_PAIR_LATE_PUBLISH_MAX_RX_MS &&
       bidxDelta >= (int)PWV_PAIR_BIDX_SWEET_MIN &&
       bidxDelta <= (int)PWV_PAIR_BIDX_SWEET_MAX)
    {
        return 1U;
    }
    return 0U;
}

static uint8_t pwv_check_pending_timeout(void)
{
    uint32_t nowTick;
    uint32_t limit;

    if(s_awaitNext)
    {
        return 0U;
    }

    nowTick = TMOS_GetSystemClock();
    limit = MS1_TO_SYSTEM_TIME(PWV_PENDING_SOLO_TIMEOUT_MS);

    if(s_wristPending.valid && !s_fingerPending.valid)
    {
        if((nowTick - s_wristPending.tick) > limit)
        {
            PRINT("PWV: solo timeout wrist id=%u, advance\n",
                  (unsigned)s_wristPending.pulseId);
            return pwv_begin_advance();
        }
    }
    if(s_fingerPending.valid && !s_wristPending.valid)
    {
        if((nowTick - s_fingerPending.tick) > limit)
        {
            PRINT("PWV: solo timeout finger id=%u, advance\n",
                  (unsigned)s_fingerPending.pulseId);
            return pwv_begin_advance();
        }
    }
    return 0U;
}

static uint8_t pwv_force_advance_stale_pulse(uint16_t pulseId)
{
    if(s_awaitNext || pulseId == 0U)
    {
        return 0U;
    }
    if(s_targetPulseId != 0U && pulseId > s_targetPulseId)
    {
        if(s_wristPending.valid ^ s_fingerPending.valid)
        {
            PRINT("PWV: force advance stale id=%u new=%u\n",
                  (unsigned)s_targetPulseId, (unsigned)pulseId);
            return pwv_begin_advance();
        }
    }
    return 0U;
}

static uint8_t pwv_beat_ts_self_valid(uint16_t beatIdx, uint32_t beatTs)
{
    uint32_t expected = (uint32_t)beatIdx * PWV_PPG_MS_PER_SAMPLE;
    uint32_t delta = (beatTs > expected) ? (beatTs - expected) : (expected - beatTs);

    return (delta <= (uint32_t)PWV_BEAT_TS_SELF_MAX_MS) ? 1U : 0U;
}

static uint32_t pwv_abs_tick_diff(uint32_t a, uint32_t b)
{
    uint32_t d1 = a - b;
    uint32_t d2 = b - a;
    return (d1 < d2) ? d1 : d2;
}

static float pwv_clamp(float x, float lo, float hi)
{
    if(x < lo)
    {
        return lo;
    }
    if(x > hi)
    {
        return hi;
    }
    return x;
}

static void pwv_clear_pending(void)
{
    s_wristPending.valid = 0U;
    s_fingerPending.valid = 0U;
}

static void pwv_clear_reject_flags(void)
{
    s_wristRejectPulseId = 0U;
    s_fingerRejectPulseId = 0U;
}

static uint8_t pwv_other_side_rejected(uint8_t isWrist, uint16_t pulseId)
{
    uint16_t otherReject = isWrist ? s_fingerRejectPulseId : s_wristRejectPulseId;

    return (otherReject == pulseId) ? 1U : 0U;
}

static uint8_t pwv_begin_advance(void)
{
    if(s_targetPulseId != 0U)
    {
        s_lastAdvancedPulseId = s_targetPulseId;
    }
    else if(s_wristPending.valid)
    {
        s_lastAdvancedPulseId = s_wristPending.pulseId;
    }
    else if(s_fingerPending.valid)
    {
        s_lastAdvancedPulseId = s_fingerPending.pulseId;
    }

    s_targetPulseId = 0U;
    pwv_clear_pending();
    pwv_clear_reject_flags();
    s_wrist.valid = 0U;
    s_finger.valid = 0U;
    s_awaitNext = 1U;
    return 1U;
}

static uint8_t pwv_advance_on_side_reject(uint8_t isWrist, uint16_t pulseId)
{
    pwvPending_t *other = isWrist ? &s_fingerPending : &s_wristPending;

    if(s_awaitNext || pulseId == 0U)
    {
        return 0U;
    }
    if(s_targetPulseId == 0U)
    {
        s_targetPulseId = pulseId;
    }
    if(isWrist)
    {
        s_wristRejectPulseId = pulseId;
    }
    else
    {
        s_fingerRejectPulseId = pulseId;
    }
    if(pwv_other_side_rejected(isWrist, pulseId))
    {
        PRINT("PWV: skip pulse id=%u both sides invalid, advance\n", (unsigned)pulseId);
        return pwv_begin_advance();
    }
    if(other->valid && other->pulseId == pulseId)
    {
        PRINT("PWV: skip pulse id=%u side=%u invalid, advance\n",
              (unsigned)pulseId, (unsigned)isWrist);
        return pwv_begin_advance();
    }
    return 0U;
}

static uint8_t pwv_try_publish(uint16_t pulseId)
{
    uint32_t nowTick;
    uint32_t ageWristTicks;
    uint32_t ageFingerTicks;
    uint32_t dtTicks;
    float    dtMs;
    float    pttBidxMs;
    float    pttMs;
    float    pwvRaw;
    float    pwvRawSmooth;
    float    pwvCal;
    float    pwvOut;
    float    pwvSession;
    uint32_t tsMs;
    char     line[160];
    int      n;
    uint8_t  bidx_penalty = 0U;
    uint8_t  pi_penalty = 0U;
    uint8_t  ml_penalty = 0U;
#if PWV_ML_GATE_ENABLE
    pwv_ml_result_t ml_result = {0};
#endif

    if(!s_wrist.valid || !s_finger.valid)
    {
        return 0U;
    }

    nowTick = TMOS_GetSystemClock();
    ageWristTicks = nowTick - s_wrist.tick;
    ageFingerTicks = nowTick - s_finger.tick;
    if(ageWristTicks > MS1_TO_SYSTEM_TIME(PWV_SYNC_MAX_AGE_MS) ||
       ageFingerTicks > MS1_TO_SYSTEM_TIME(PWV_SYNC_MAX_AGE_MS))
    {
        PRINT("PWV: skip stale id=%u\n", (unsigned)pulseId);
        return 0U;
    }

    if((nowTick - s_lastUart1Tick) < MS1_TO_SYSTEM_TIME(PWV_UART1_PERIOD_MS))
    {
        return 0U;
    }

    dtTicks = pwv_abs_tick_diff(s_wrist.tick, s_finger.tick);
    dtMs = ((float)dtTicks * (float)SYSTEM_TIME_MICROSEN) / 1000.0f;

    if(!pwv_beat_ts_self_valid(s_wrist.beatIdx, s_wrist.beatTs))
    {
        PRINT("PWV: skip bad beat_ts wrist id=%u b=%u ts=%lu\n",
              (unsigned)pulseId,
              (unsigned)s_wrist.beatIdx,
              (unsigned long)s_wrist.beatTs);
        return 0U;
    }
    if(!pwv_beat_ts_self_valid(s_finger.beatIdx, s_finger.beatTs))
    {
        PRINT("PWV: skip bad beat_ts finger id=%u b=%u ts=%lu\n",
              (unsigned)pulseId,
              (unsigned)s_finger.beatIdx,
              (unsigned long)s_finger.beatTs);
        return 0U;
    }

    {
        int bidxDelta = (int)s_finger.beatIdx - (int)s_wrist.beatIdx;

        if(bidxDelta < (int)PWV_BEAT_ORDER_MIN_DELTA)
        {
            PRINT("PWV: skip beat order id=%u w_b=%u f_b=%u dt=%.0fms\n",
                  (unsigned)pulseId,
                  (unsigned)s_wrist.beatIdx, (unsigned)s_finger.beatIdx, dtMs);
            return 0U;
        }
        pttBidxMs = (float)bidxDelta * (float)PWV_PPG_MS_PER_SAMPLE;

        if(!pwv_rx_gap_ok(dtMs, bidxDelta))
        {
            PRINT("PWV: skip rx gap id=%u dt=%.0fms w_b=%u f_b=%u delta=%d\n",
                  (unsigned)pulseId, dtMs,
                  (unsigned)s_wrist.beatIdx, (unsigned)s_finger.beatIdx, bidxDelta);
            UI_SetGateHint(UI_STR_PREFLIGHT_KEEP_STILL);
            return 0U;
        }
        if(dtMs > PWV_PAIR_PUBLISH_MAX_RX_MS)
        {
            PRINT("PWV: late pair id=%u dt=%.0fms delta=%d (quality penalized)\n",
                  (unsigned)pulseId, dtMs, bidxDelta);
        }
        if(bidxDelta > (int)PWV_PAIR_BIDX_PUBLISH_MAX)
        {
            PRINT("PWV: skip bidx gap id=%u w_b=%u f_b=%u dt=%.0fms\n",
                  (unsigned)pulseId,
                  (unsigned)s_wrist.beatIdx, (unsigned)s_finger.beatIdx, dtMs);
            return 0U;
        }
        if(bidxDelta < (int)PWV_PAIR_BIDX_SWEET_MIN ||
           bidxDelta > (int)PWV_PAIR_BIDX_SWEET_MAX)
        {
            PRINT("PWV: skip bidx sweet id=%u w_b=%u f_b=%u delta=%d dt=%.0fms\n",
                  (unsigned)pulseId,
                  (unsigned)s_wrist.beatIdx, (unsigned)s_finger.beatIdx,
                  bidxDelta, dtMs);
            return 0U;
        }
        if(bidxDelta == (int)PWV_PAIR_BIDX_SWEET_MIN)
        {
            bidx_penalty = PWV_BIDX_DELTA3_QUALITY_PENALTY;
            PRINT("PWV: soft bidx delta=3 id=%u w_b=%u f_b=%u ptt=%.0fms (q-%u)\n",
                  (unsigned)pulseId,
                  (unsigned)s_wrist.beatIdx, (unsigned)s_finger.beatIdx,
                  pttBidxMs, (unsigned)bidx_penalty);
        }
        else if(bidxDelta == 6)
        {
            bidx_penalty = PWV_BIDX_DELTA6_QUALITY_PENALTY;
            PRINT("PWV: soft bidx delta=6 id=%u w_b=%u f_b=%u ptt=%.0fms (q-%u)\n",
                  (unsigned)pulseId,
                  (unsigned)s_wrist.beatIdx, (unsigned)s_finger.beatIdx,
                  pttBidxMs, (unsigned)bidx_penalty);
        }
    }

    {
        uint8_t hrGap = (s_wrist.hr > s_finger.hr) ?
                        (s_wrist.hr - s_finger.hr) :
                        (s_finger.hr - s_wrist.hr);

        if(hrGap > PWV_HR_GAP_MAX)
        {
            PRINT("PWV: skip hr gap id=%u w_hr=%u f_hr=%u dt=%.0fms\n",
                  (unsigned)pulseId,
                  (unsigned)s_wrist.hr, (unsigned)s_finger.hr, dtMs);
            return 0U;
        }
    }

    if(s_finger.hr < PWV_FINGER_HR_PUBLISH_MIN ||
       s_finger.hr > PWV_FINGER_HR_PUBLISH_MAX)
    {
        PRINT("PWV: skip finger hr id=%u f_hr=%u\n",
              (unsigned)pulseId, (unsigned)s_finger.hr);
        return 0U;
    }

    if(s_wrist.spo2 < PWV_WRIST_SPO2_MIN)
    {
        PRINT("PWV: skip wrist spo2 id=%u w_spo2=%u\n",
              (unsigned)pulseId, (unsigned)s_wrist.spo2);
        return 0U;
    }

    if(!pwv_pi_gate_ok(s_wrist.pi_x10, s_finger.pi_x10,
                       s_wrist.spo2, s_finger.spo2, &pi_penalty))
    {
        PRINT("PWV: skip pi hard id=%u w_pi=%u f_pi=%u w_spo2=%u f_spo2=%u\n",
              (unsigned)pulseId, (unsigned)s_wrist.pi_x10, (unsigned)s_finger.pi_x10,
              (unsigned)s_wrist.spo2, (unsigned)s_finger.spo2);
        return 0U;
    }
    if(pi_penalty > 0U)
    {
        PRINT("PWV: soft pi id=%u w_pi=%u f_pi=%u (q-%u)\n",
              (unsigned)pulseId, (unsigned)s_wrist.pi_x10, (unsigned)s_finger.pi_x10,
              (unsigned)pi_penalty);
    }

    /* P3-D2：仅用已发布样本算质量 CV；候选用 with() 做硬门，拒收不入环 */
    {
        float cv_pct = pwv_ptt_cv_pct_with(pttBidxMs);
        uint8_t n_probe = (s_pttRingCnt < PWV_PTT_CV_RING) ?
                          (uint8_t)(s_pttRingCnt + 1U) : PWV_PTT_CV_RING;

        if(n_probe >= 3U && cv_pct > (float)PWV_PTT_CV_MAX_PCT)
        {
            PRINT("PWV: skip ptt cv id=%u cv=%.1f%%\n",
                  (unsigned)pulseId, cv_pct);
            UI_SetGateHint(UI_STR_PREFLIGHT_PTT_CV);
            return 0U;
        }
    }

    if(pttBidxMs < PWV_MIN_PTT_MS || pttBidxMs > PWV_MAX_PTT_MS)
    {
        PRINT("PWV: skip bad ptt id=%u ptt=%.0fms w_b=%u f_b=%u dt=%.0fms\n",
              (unsigned)pulseId, pttBidxMs,
              (unsigned)s_wrist.beatIdx, (unsigned)s_finger.beatIdx, dtMs);
        return 0U;
    }

#if PWV_ML_GATE_ENABLE
    {
        int bidxDeltaMl = (int)s_finger.beatIdx - (int)s_wrist.beatIdx;

        ml_result = pwv_ml_eval(&s_wrist, &s_finger, dtMs, bidxDeltaMl);
        if(!ml_result.accept)
        {
#if PWV_ML_GATE_OBSERVE_ONLY
            PRINT("PWV: ml_observe id=%u score=%.2f (rules accept)\n",
                  (unsigned)pulseId, ml_result.score);
#else
            if(ml_result.score < PWV_ML_SOFT_REJECT_THRESH)
            {
                /* late∈(800,2000]ms 或 wf 有效低分：软放行；>2s 硬拒防 q=27 */
                if(dtMs > PWV_PAIR_LATE_SOFT_MAX_RX_MS)
                {
                    PRINT("PWV: skip ml_gate late id=%u score=%.2f rx=%.0fms\n",
                          (unsigned)pulseId, ml_result.score, dtMs);
                    UI_SetGateHint(UI_STR_PREFLIGHT_QUALITY);
                    return 0U;
                }
                if((dtMs > PWV_PAIR_PUBLISH_MAX_RX_MS) ||
                   (s_wrist.wf.valid && s_finger.wf.valid &&
                    ml_result.score < 0.05f))
                {
                    ml_penalty = PWV_ML_ZERO_SOFT_PENALTY;
                    PRINT("PWV: ml_zero_soft id=%u score=%.2f rx=%.0fms (rules accept q-%u)\n",
                          (unsigned)pulseId, ml_result.score, dtMs, (unsigned)ml_penalty);
                }
                else
                {
                    PRINT("PWV: skip ml_gate id=%u score=%.2f\n",
                          (unsigned)pulseId, ml_result.score);
                    UI_SetGateHint(UI_STR_PREFLIGHT_QUALITY);
                    return 0U;
                }
            }
            else
            {
                PRINT("PWV: ml_soft id=%u score=%.2f (rules accept)\n",
                      (unsigned)pulseId, ml_result.score);
            }
#endif
        }
    }
#endif

    pttMs = pttBidxMs;
    pwvRaw = pwv_calib_raw_mps(pttMs);
    if(pwvRaw < PWV_RAW_MIN_MPS || pwvRaw > PWV_RAW_MAX_MPS)
    {
        PRINT("PWV: skip raw band id=%u raw=%.2f ptt=%.0fms\n",
              (unsigned)pulseId, pwvRaw, pttMs);
        return 0U;
    }
    pwvRaw = pwv_clamp(pwvRaw, PWV_MIN_MPS, PWV_MAX_MPS);

    {
        /* P3-D3：用「入环后」CV 预检，避免先过门再入环导致 UI q=45 */
        float cv_q = pwv_ptt_cv_pct_with(pttBidxMs);
        uint8_t q_probe = pwv_compute_quality(s_wrist.pi_x10, s_finger.pi_x10,
                                              cv_q, dtMs,
#if PWV_ML_GATE_ENABLE
                                              ml_result.accept
#else
                                              1U
#endif
                                              , bidx_penalty, pi_penalty, ml_penalty);
        if(dtMs > PWV_PAIR_PUBLISH_MAX_RX_MS && q_probe < PWV_LATE_PUBLISH_MIN_Q)
        {
            PRINT("PWV: skip late low q id=%u q=%u rx=%.0fms\n",
                  (unsigned)pulseId, (unsigned)q_probe, dtMs);
            return 0U;
        }
        if(q_probe < PWV_SOFT_PUBLISH_MIN_Q)
        {
            PRINT("PWV: skip soft low q id=%u q=%u cv=%.1f%%\n",
                  (unsigned)pulseId, (unsigned)q_probe, cv_q);
            return 0U;
        }
    }

    if(pttMs >= PWV_SESSION_PTT_MIN_MS && pttMs <= PWV_SESSION_PTT_MAX_MS)
    {
        if(s_hasLastPwv)
        {
            pwvRawSmooth = s_lastPwvRaw * 0.70f + pwvRaw * 0.30f;
        }
        else
        {
            pwvRawSmooth = pwvRaw;
            s_hasLastPwv = 1U;
        }
        s_lastPwvRaw = pwvRawSmooth;
    }
    else
    {
        /* P3-J2：Δb=3/6 不参与 raw 平滑，避免下一拍 sess 假高（主机33） */
        pwvRawSmooth = pwvRaw;
        PRINT("PWV: ptt outlier no-smooth id=%u ptt=%.0fms raw=%.2f\n",
              (unsigned)pulseId, pttMs, pwvRaw);
    }

    pwvCal = pwv_calib_apply(pwvRawSmooth);
    if(pwvCal < PWV_PUBLISH_MIN_MPS || pwvCal > PWV_PUBLISH_MAX_MPS)
    {
        PRINT("PWV: skip publish band id=%u cal=%.2f raw=%.2f ptt=%.0fms\n",
              (unsigned)pulseId, pwvCal, pwvRawSmooth, pttMs);
        return 0U;
    }
    pwvOut = pwvCal;
    pwvSession = pwvOut;
    /* P3-D2/J2：仅黄金 PTT 入 CV 环 */
    if(pttMs >= PWV_SESSION_PTT_MIN_MS && pttMs <= PWV_SESSION_PTT_MAX_MS)
    {
        pwv_ptt_ring_push(pttMs);
    }
    s_lastWristHr = s_wrist.hr;
    s_lastWristSpo2 = s_wrist.spo2;
    s_lastFingerHr = s_finger.hr;
    s_lastFingerSpo2 = s_finger.spo2;
    s_hasLastVitals = 1U;
    s_lastUart1Tick = nowTick;

    tsMs = (uint32_t)(((uint64_t)nowTick * 625ULL) / 1000ULL);

#if PWV_ML_GATE_ENABLE
    n = snprintf(line, sizeof(line),
                 "{\"pulse_id\":%u,\"ts\":%lu,\"w_hr\":%u,\"w_spo2\":%u,"
                 "\"f_hr\":%u,\"f_spo2\":%u,\"pwv\":%.1f,\"pwv_raw\":%.1f,"
                 "\"pwv_cal\":%.1f,\"ml_score\":%.2f,\"ml_accept\":%u}\r\n",
                 (unsigned)pulseId,
                 (unsigned long)tsMs,
                 (unsigned)s_wrist.hr, (unsigned)s_wrist.spo2,
                 (unsigned)s_finger.hr, (unsigned)s_finger.spo2,
                 pwvOut, pwvRawSmooth, pwvOut,
                 ml_result.score, (unsigned)ml_result.accept);
#else
    n = snprintf(line, sizeof(line),
                 "{\"pulse_id\":%u,\"ts\":%lu,\"w_hr\":%u,\"w_spo2\":%u,"
                 "\"f_hr\":%u,\"f_spo2\":%u,\"pwv\":%.1f,\"pwv_raw\":%.1f,"
                 "\"pwv_cal\":%.1f}\r\n",
                 (unsigned)pulseId,
                 (unsigned long)tsMs,
                 (unsigned)s_wrist.hr, (unsigned)s_wrist.spo2,
                 (unsigned)s_finger.hr, (unsigned)s_finger.spo2,
                 pwvOut, pwvRawSmooth, pwvOut);
#endif
    if(n > 0 && n < (int)sizeof(line))
    {
        float mean;
        float lo;
        float hi;

        if(!UI_WiFi_IsActive() && !HostLink_IsOtaActive()) {
            BSP_UART1_SendString(line);
        }
        {
            uint8_t q = pwv_compute_quality(s_wrist.pi_x10, s_finger.pi_x10,
                                            pwv_ptt_cv_pct(), dtMs,
#if PWV_ML_GATE_ENABLE
                                            ml_result.accept
#else
                                            1U
#endif
                                            , bidx_penalty, pi_penalty, ml_penalty);
            s_signalQuality = q;
            UI_SetSignalQuality(q);
            pwv_session_push(pwvOut, q, dtMs, pttMs);
            pwvSession = pwv_session_median();
            if(pwvSession <= 0.0f) {
                /* 极值 PTT 且环空：沿用上一 sess，避免 UI 闪 7.x */
                if((pttMs < PWV_SESSION_PTT_MIN_MS ||
                    pttMs > PWV_SESSION_PTT_MAX_MS) &&
                   s_lastPwv > 0.0f) {
                    pwvSession = s_lastPwv;
                } else {
                    pwvSession = pwvOut;
                }
            }
            /* 仅黄金 PTT 更新持久 sess，极值不写入 s_lastPwv */
            if(pttMs >= PWV_SESSION_PTT_MIN_MS &&
               pttMs <= PWV_SESSION_PTT_MAX_MS) {
                s_lastPwv = pwvSession;
            }
        }
        UI_ClearGateHint();
        UI_SetPwv(pwvSession);
        AlarmMgr_Clear(ALARM_SIGNAL_WEAK);
        pwv_calib_ref_band(&mean, &lo, &hi);
        if(pwvOut > hi) {
            AlarmMgr_Raise(ALARM_PWV_TREND_HIGH);
        } else if(pwvOut < lo) {
            AlarmMgr_Raise(ALARM_PWV_TREND_LOW);
        } else {
            AlarmMgr_Raise(ALARM_FIRST_PWV);
        }
        (void)mean;
        PRINT("PWV=%.1f m/s raw=%.1f id=%u ptt=%.0fms q=%u w_pi=%u f_pi=%u cv=%.1f%% rx=%.0fms"
#if PWV_ML_GATE_ENABLE
              " ml=%.2f"
#endif
              " sess=%.1f (w_b=%u f_b=%u rx_dt=%.0fms)\n",
              pwvSession, pwvRawSmooth, (unsigned)pulseId, pttMs, (unsigned)s_signalQuality,
              (unsigned)s_wrist.pi_x10, (unsigned)s_finger.pi_x10,
              pwv_ptt_cv_pct(), dtMs
#if PWV_ML_GATE_ENABLE
              , ml_result.score
#endif
              , pwvSession
              , (unsigned)s_wrist.beatIdx, (unsigned)s_finger.beatIdx, dtMs);
    }

    return 1U;
}

static void pwv_clear_samples(void)
{
    s_wrist.valid = 0U;
    s_finger.valid = 0U;
    s_hasLastPwv = 0U;
    s_lastPwv = 0.0f;
    s_lastPwvRaw = 0.0f;
    s_hasLastVitals = 0U;
    s_lastUart1Tick = 0U;
    s_targetPulseId = 0U;
    s_awaitNext = 0U;
    s_signalQuality = PWV_QUALITY_IDLE;
    s_pttRingCnt = 0U;
    s_pttRingIdx = 0U;
    s_lastWristPi = 0U;
    s_lastFingerPi = 0U;
    pwv_session_clear();
    UI_SetSignalQuality(PWV_QUALITY_IDLE);
    UI_ClearGateHint();
    pwv_clear_pending();
    pwv_clear_reject_flags();
}

static uint8_t pwv_try_pair(uint8_t isWrist, uint16_t pulseId, uint8_t hr, uint8_t spo2,
                            uint16_t beatIdx, uint32_t beatTs, uint8_t pi_x10,
                            const pwv_wf_compact_t *wf)
{
    pwvPending_t *side;

    if(!s_measureEnabled)
    {
        return 0U;
    }
    if(s_awaitNext)
    {
        return 0U;
    }
    if(pulseId == 0U)
    {
        return 0U;
    }

    if(pwv_check_pending_timeout())
    {
        return 1U;
    }
    if(pwv_force_advance_stale_pulse(pulseId))
    {
        return 1U;
    }

    if(!pwv_sample_valid_for_pulse(isWrist, pulseId, hr, spo2))
    {
        if(isWrist && hr > 0U && hr < PWV_MIN_WRIST_HR)
        {
            PRINT("PWV: skip low wrist hr id=%u hr=%u\n", (unsigned)pulseId, (unsigned)hr);
        }
        if(pwv_advance_on_side_reject(isWrist, pulseId))
        {
            return 1U;
        }
        return 0U;
    }
    if(!pwv_beat_idx_valid(isWrist, beatIdx))
    {
        PRINT("PWV: skip edge b_idx id=%u side=%u b=%u\n",
              (unsigned)pulseId, (unsigned)isWrist, (unsigned)beatIdx);
        if(pwv_advance_on_side_reject(isWrist, pulseId))
        {
            return 1U;
        }
        return 0U;
    }
    if(!pwv_beat_ts_self_valid(beatIdx, beatTs))
    {
        PRINT("PWV: skip beat_ts id=%u side=%u b=%u ts=%lu\n",
              (unsigned)pulseId, (unsigned)isWrist, (unsigned)beatIdx,
              (unsigned long)beatTs);
        if(pwv_advance_on_side_reject(isWrist, pulseId))
        {
            return 1U;
        }
        return 0U;
    }

    side = isWrist ? &s_wristPending : &s_fingerPending;

    if(s_targetPulseId == 0U)
    {
        s_targetPulseId = pulseId;
    }
    else if(pulseId != s_targetPulseId)
    {
        PRINT("PWV: skip id=%u target=%u\n", (unsigned)pulseId, (unsigned)s_targetPulseId);
        return 0U;
    }
    if(pwv_other_side_rejected(isWrist, pulseId))
    {
        PRINT("PWV: skip pulse id=%u side=%u valid/other rejected, advance\n",
              (unsigned)pulseId, (unsigned)isWrist);
        return pwv_begin_advance();
    }

    side->valid = 1U;
    side->pulseId = pulseId;
    side->hr = hr;
    side->spo2 = spo2;
    side->pi_x10 = pi_x10;
    side->beatIdx = beatIdx;
    side->beatTs = beatTs;
    side->tick = TMOS_GetSystemClock();
    if(wf != NULL)
    {
        side->wf = *wf;
        side->wf.valid = 1U;
        if(side->wf.pi_x10 > 0U)
        {
            side->pi_x10 = side->wf.pi_x10;
        }
    }
    else
    {
        side->wf.valid = 0U;
    }

    if(isWrist)
    {
        s_lastWristPi = side->pi_x10;
    }
    else
    {
        s_lastFingerPi = side->pi_x10;
    }

    if(!s_wristPending.valid || !s_fingerPending.valid)
    {
        return 0U;
    }
    if(s_wristPending.pulseId != s_targetPulseId ||
       s_fingerPending.pulseId != s_targetPulseId)
    {
        return 0U;
    }

    s_wrist.valid = 1U;
    s_wrist.hr = s_wristPending.hr;
    s_wrist.spo2 = s_wristPending.spo2;
    s_wrist.pi_x10 = s_wristPending.pi_x10;
    s_wrist.beatIdx = s_wristPending.beatIdx;
    s_wrist.beatTs = s_wristPending.beatTs;
    s_wrist.tick = s_wristPending.tick;
    s_wrist.wf = s_wristPending.wf;

    s_finger.valid = 1U;
    s_finger.hr = s_fingerPending.hr;
    s_finger.spo2 = s_fingerPending.spo2;
    s_finger.pi_x10 = s_fingerPending.pi_x10;
    s_finger.beatIdx = s_fingerPending.beatIdx;
    s_finger.beatTs = s_fingerPending.beatTs;
    s_finger.tick = s_fingerPending.tick;
    s_finger.wf = s_fingerPending.wf;

    {
        uint32_t dtTicks = pwv_abs_tick_diff(s_wristPending.tick, s_fingerPending.tick);
        float    pairDtMs = ((float)dtTicks * (float)SYSTEM_TIME_MICROSEN) / 1000.0f;

        uint8_t pair_bidx_penalty = 0U;
        uint8_t pair_pi_penalty = 0U;
        int     pair_bidx_delta = (int)s_fingerPending.beatIdx - (int)s_wristPending.beatIdx;

        if(pair_bidx_delta == (int)PWV_PAIR_BIDX_SWEET_MIN) {
            pair_bidx_penalty = PWV_BIDX_DELTA3_QUALITY_PENALTY;
        }
        else if(pair_bidx_delta == 6) {
            pair_bidx_penalty = PWV_BIDX_DELTA6_QUALITY_PENALTY;
        }
        /* late pair 中间态不刷 UI，避免 38–47 误导（发布成功后再刷） */
        if(pairDtMs <= PWV_PAIR_PUBLISH_MAX_RX_MS) {
            pair_pi_penalty = pwv_pi_gate_penalty(s_wrist.pi_x10, s_finger.pi_x10,
                                                  s_wrist.spo2, s_finger.spo2);
            pwv_update_quality_ui(s_targetPulseId, s_wrist.pi_x10, s_finger.pi_x10,
                                  pairDtMs, pair_bidx_penalty, pair_pi_penalty);
        }
    }

    if(s_targetPulseId >= PWV_WARMUP_PULSE_ID &&
       s_targetPulseId <= PWV_WARMUP_PULSE_MAX)
    {
        PRINT("PWV: warmup skip id=%u\n", (unsigned)s_targetPulseId);
        (void)pwv_begin_advance();
        return 1U;
    }

    (void)pwv_try_publish(s_targetPulseId);

    (void)pwv_begin_advance();
    return 1U;
}

void PWV_Init(void)
{
    s_measureEnabled = 0U;
    pwv_clear_samples();
}

void PWV_OnMeasurementStart(void)
{
    s_measureEnabled = 1U;
    s_lastAdvancedPulseId = 0U;
    pwv_clear_samples();
}

void PWV_OnMeasurementStop(void)
{
    s_measureEnabled = 0U;
    pwv_clear_samples();
}

uint8_t PWV_IsEnabled(void)
{
    return s_measureEnabled;
}

uint8_t PWV_GetSessionSnapshot(pwv_session_t *out)
{
    if(out == NULL) {
        return 0U;
    }
    if(s_hasLastVitals) {
        out->w_hr = s_lastWristHr;
        out->w_spo2 = s_lastWristSpo2;
        out->f_hr = s_lastFingerHr;
        out->f_spo2 = s_lastFingerSpo2;
    } else {
        out->w_hr = s_wrist.valid ? s_wrist.hr : 0U;
        out->w_spo2 = s_wrist.valid ? s_wrist.spo2 : 0U;
        out->f_hr = s_finger.valid ? s_finger.hr : 0U;
        out->f_spo2 = s_finger.valid ? s_finger.spo2 : 0U;
    }
    out->pwv_valid = s_hasLastPwv;
    out->pwv = s_hasLastPwv ? s_lastPwv : 0.0f;
    out->pwv_raw = s_hasLastPwv ? s_lastPwvRaw : 0.0f;
    out->signal_quality = s_signalQuality;
    return 1U;
}

uint8_t PWV_GetSignalQuality(void)
{
    return s_signalQuality;
}

void PWV_OnPulseNextDone(void)
{
    s_awaitNext = 0U;
    s_targetPulseId = 0U;
    pwv_clear_pending();
    pwv_clear_reject_flags();
}

uint16_t PWV_GetLastAdvancedPulseId(void)
{
    return s_lastAdvancedPulseId;
}

uint8_t PWV_OnFusionReject(uint8_t isWrist, uint16_t pulseId)
{
    if(!s_measureEnabled || s_awaitNext || pulseId == 0U)
    {
        return 0U;
    }
    if(s_targetPulseId == 0U)
    {
        s_targetPulseId = pulseId;
    }
    else if(pulseId != s_targetPulseId &&
            !(s_wristPending.valid || s_fingerPending.valid))
    {
        /* 迟到的拒收通知，忽略 */
        return 0U;
    }

    if(isWrist)
    {
        s_wristRejectPulseId = pulseId;
    }
    else
    {
        s_fingerRejectPulseId = pulseId;
    }

    PRINT("PWV: fusion reject side=%u id=%u, advance\n",
          (unsigned)isWrist, (unsigned)pulseId);
    return pwv_begin_advance();
}

uint8_t PWV_FeedWrist(uint16_t pulseId, uint8_t hr, uint8_t spo2,
                      uint16_t beatIdx, uint32_t beatTs, uint8_t pi_x10,
                      const pwv_wf_compact_t *wf)
{
    return pwv_try_pair(1U, pulseId, hr, spo2, beatIdx, beatTs, pi_x10, wf);
}

uint8_t PWV_FeedFinger(uint16_t pulseId, uint8_t hr, uint8_t spo2,
                       uint16_t beatIdx, uint32_t beatTs, uint8_t pi_x10,
                       const pwv_wf_compact_t *wf)
{
    return pwv_try_pair(0U, pulseId, hr, spo2, beatIdx, beatTs, pi_x10, wf);
}
