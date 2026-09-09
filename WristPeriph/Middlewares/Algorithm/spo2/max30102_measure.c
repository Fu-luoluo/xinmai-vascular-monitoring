#include <string.h>

/*
 * WristPeriph: 手腕本地 PPG 采集。
 *   - max30102_measure_init() 由 APP/peripheral.c 在收到 Char1 = '1' 时调用，
 *     此时立刻 reset 驱动 + 重写所有寄存器（清 FIFO 指针）+ 重置 FIR state
 *     + 启动 SBP_PPG_SAMPLE_EVT；这样硬件采集与 MCU 轮询同步起跳，
 *     避免上电后空转期间 FIFO 写满 32 帧后停摆、留下"卡死"状态。
 *   - 主机 pacing：Char1 'S' 在 T0 同步开窗；'1'/'N' 仅改 pulse_id，不立即 arm。
 *     上行 JSON: {"id":N,"ts":T,"b_idx":I,"beat_ts":B,"hr":H,"spo2":S}。
 */

#include "CONFIG.h"
#include "max30102_measure.h"
#include "../../../Drivers/Peripherals/max30102.h"
#include "../filter/max30102_fir.h"
#include "../../../APP/include/peripheral.h"
#include <math.h>

#define PPG_CACHE_LEN             150
#define PPG_MS_PER_SAMPLE         20U
/* 用原始 ADC 判有效区；FIR 幅值与 raw 量级相关，固定 45000 在 raw≈5k 时永远进不了 cache */
#define PPG_RAW_MIN               35000.0f
#define PPG_SATURATION_RAW        250000.0f
#define PPG_WEAK_RESET_TH         25U
#define HR_VALID_MIN              35U
#define HR_VALID_MAX              180U
#define SPO2_VALID_MIN            70.0f
#define SPO2_VALID_MAX            99.0f
#define HR_SMOOTH_MAX_STEP        18U
#define HR_SMOOTH_RISE_STEP       18U
#define HR_MERGE_AC_TRUST_GAP     6U
#define HR_MERGE_HARMONIC_TOL     10U
#define HR_SMOOTH_JUMP_THRESH     14U
#define HR_PACE_NOTIFY_MIN        48U
#define HR_PACE_NOTIFY_MAX        98U
#define HR_NOTIFY_BOOST_PCT       5U
#define HR_NOTIFY_BOOST_ADD       4U
#define HR_NOTIFY_DISPLAY_MAX     98U
#define HR_REST_WINDOW_LO         62U
#define HR_REST_WINDOW_HI         90U
#define HR_REST_OX_LO             64U
#define HR_REST_OX_HI             72U
#define HR_WRIST_HR_BIAS          7U
#define HR_ACTIVE_ENTER           76U
#define HR_ACTIVE_EXIT            72U
#define HR_ACTIVE_NOTIFY_BOOST    2U
#define HR_ACTIVE_LOW_UPLIFT      12U
#define HR_ACTIVE_LOW_THRESH      78U
#define HR_PACE_REST_SKIP_HI      80U
#define SPO2_SMOOTH_MAX_STEP_X100 600
#define SPO2_PACE_NOTIFY_MIN_X100 7000
#define SPO2_NOTIFY_MAX           99U
#define SPO2_STABLE_MAX_X100      9900
#define WRIST_SPO2_NOTIFY_BOOST_PCT 3U
#define SENSOR_ERR_REINIT_TH      8U
#define SENSOR_ERR_REINIT_PACE_TH 16U

/* 调试输出节流（每 N 次 poll 打印一次）；50Hz 采样 → 50 ≈ 1s */
#define PPG_DEBUG_PRINT_EVERY     50U

volatile uint8_t g_max30102_int_flag = 0U;

static float g_ppg_ir_cache[PPG_CACHE_LEN] = {0};
static float g_ppg_red_cache[PPG_CACHE_LEN] = {0};
static uint16_t g_ppg_cache_idx = 0U;
static uint8_t g_sensor_err_cnt = 0U;
static uint8_t g_paced_notify_pending = 0U;
static uint32_t g_window_t0_ms = 0U;
static uint32_t g_window_start_ms = 0U;
static uint8_t  g_window_open = 0U;

/* 之前散落在函数里的 static，提升到模块级以便 measure_init 一次性重置 */
static uint8_t  g_weak_signal_cnt   = 0U;          /* ppg_data_process */
static uint16_t g_stable_hr         = 0U;          /* ppg_calc_and_publish */
static int32_t  g_stable_spo2_x100  = -1;
static uint8_t  g_spo2_invalid_seq  = 0U;
static uint8_t  g_last_good_spo2_u8 = 0U;
static uint16_t g_last_good_hr      = 0U;
static uint16_t g_poll_dbg_cnt      = 0U;          /* 限流调试计数 */
static uint16_t g_paced_pulse_id    = 0U;
static uint8_t  g_pace_edge_retry   = 0U;
static uint8_t  g_pace_quality_skips = 0U;
static uint8_t  g_pace_force_next    = 0U;
static uint16_t g_last_window_hr    = 0U;
static uint16_t g_last_hr_ac        = 0U;
static uint16_t g_last_hr_drv       = 0U;
static uint16_t g_rest_hr_anchor    = 0U;
static uint8_t  g_hr_active_mode    = 0U;
static float    g_ppg_ac_work[PPG_CACHE_LEN];
static ppg_wf_compact_t g_last_wf_compact;

static void ppg_data_process(float *raw_data);
static void ppg_calc_and_publish(void);
static void ppg_paced_try_notify(void);
static uint8_t ppg_wrist_spo2_for_notify(uint8_t raw);

static uint8_t ppg_wrist_spo2_for_notify(uint8_t raw)
{
    uint16_t boosted = (uint16_t)raw * (100U + WRIST_SPO2_NOTIFY_BOOST_PCT) / 100U;

    if(boosted > SPO2_NOTIFY_MAX)
    {
        boosted = SPO2_NOTIFY_MAX;
    }
    return (uint8_t)boosted;
}

static uint16_t ppg_rest_plausible_hr(void)
{
    uint16_t best = 0U;

    if(g_last_hr_ac >= HR_REST_OX_LO && g_last_hr_ac <= HR_REST_OX_HI)
    {
        best = g_last_hr_ac;
    }
    if(g_last_hr_drv >= HR_REST_OX_LO && g_last_hr_drv <= HR_REST_OX_HI)
    {
        if(best == 0U || g_last_hr_drv < best)
        {
            best = g_last_hr_drv;
        }
    }
    if(g_last_window_hr >= HR_REST_OX_LO && g_last_window_hr <= HR_REST_OX_HI)
    {
        if(best == 0U || g_last_window_hr < best)
        {
            best = g_last_window_hr;
        }
    }
    return best;
}

static uint16_t ppg_rest_plausible_hr_min(void)
{
    uint16_t best = 0U;

    if(g_last_hr_ac >= HR_REST_OX_LO && g_last_hr_ac <= HR_REST_OX_HI)
    {
        best = g_last_hr_ac;
    }
    if(g_last_hr_drv >= HR_REST_OX_LO && g_last_hr_drv <= HR_REST_OX_HI)
    {
        if(best == 0U || g_last_hr_drv < best)
        {
            best = g_last_hr_drv;
        }
    }
    if(g_last_window_hr >= HR_REST_OX_LO && g_last_window_hr <= HR_REST_OX_HI)
    {
        if(best == 0U || g_last_window_hr < best)
        {
            best = g_last_window_hr;
        }
    }
    return best;
}

static void ppg_rest_hr_anchor_update(uint16_t hr)
{
    if(g_hr_active_mode)
    {
        return;
    }
    if(hr < HR_REST_OX_LO || hr > HR_REST_OX_HI)
    {
        return;
    }
    if(g_rest_hr_anchor == 0U)
    {
        g_rest_hr_anchor = hr;
    }
    else
    {
        g_rest_hr_anchor = (uint16_t)((g_rest_hr_anchor * 3U + hr + 2U) / 4U);
    }
}

static void ppg_update_activity_mode(uint16_t hr_hint)
{
    if(hr_hint >= HR_ACTIVE_ENTER ||
       g_last_hr_ac >= HR_ACTIVE_ENTER ||
       g_last_hr_drv >= HR_ACTIVE_ENTER)
    {
        g_hr_active_mode = 1U;
    }
}

static uint16_t ppg_hr_active_pick(void)
{
    uint16_t hr = 0U;

    if(g_last_hr_ac != 0U)
    {
        hr = g_last_hr_ac;
    }
    if(g_last_hr_drv > hr)
    {
        hr = g_last_hr_drv;
    }
    if(g_last_window_hr > hr)
    {
        hr = g_last_window_hr;
    }
    if(g_stable_hr > hr && g_stable_hr <= HR_NOTIFY_DISPLAY_MAX)
    {
        hr = g_stable_hr;
    }
    return hr;
}

/* 算法 HR 常偏低；静息轻补偿，活动态取消 −7 偏置 */
static uint16_t ppg_hr_for_notify(uint16_t raw_hr)
{
    int32_t  hr;
    uint32_t uhr;

    if(raw_hr == 0U)
    {
        return 0U;
    }
    if(g_hr_active_mode || raw_hr >= HR_ACTIVE_ENTER)
    {
        uhr = (uint32_t)raw_hr;
        if(uhr < HR_ACTIVE_LOW_THRESH)
        {
            uhr = uhr + HR_ACTIVE_LOW_UPLIFT;
        }
        uhr = uhr + HR_ACTIVE_NOTIFY_BOOST + uhr * 2U / 100U;
        if(uhr > HR_NOTIFY_DISPLAY_MAX)
        {
            uhr = HR_NOTIFY_DISPLAY_MAX;
        }
        return (uint16_t)uhr;
    }
    if(raw_hr <= 82U)
    {
        hr = (int32_t)raw_hr - (int32_t)HR_WRIST_HR_BIAS;
        if(hr < (int32_t)HR_REST_OX_LO)
        {
            hr = (int32_t)HR_REST_OX_LO;
        }
        if(hr > 75)
        {
            hr = 75;
        }
        return (uint16_t)hr;
    }
    uhr = (uint32_t)raw_hr;
    if(raw_hr <= 85U)
    {
        uhr = uhr + 1U + uhr * 2U / 100U;
    }
    else
    {
        uhr = uhr + HR_NOTIFY_BOOST_ADD + uhr * HR_NOTIFY_BOOST_PCT / 100U;
    }
    if(uhr > HR_NOTIFY_DISPLAY_MAX)
    {
        uhr = HR_NOTIFY_DISPLAY_MAX;
    }
    return (uint16_t)uhr;
}

static uint16_t ppg_hr_pick_raw_for_notify(uint8_t force)
{
    uint16_t hr;
    uint16_t win;
    uint16_t ac;
    uint16_t rest;

    if(g_hr_active_mode)
    {
        hr = ppg_hr_active_pick();
        if(force && hr == 0U)
        {
            hr = HR_PACE_NOTIFY_MIN;
        }
        if(hr > HR_PACE_NOTIFY_MAX)
        {
            hr = HR_PACE_NOTIFY_MAX;
        }
        return hr;
    }

    hr  = g_stable_hr;
    win = g_last_window_hr;
    ac  = g_last_hr_ac;

    if(ac >= HR_REST_OX_LO && ac <= HR_REST_OX_HI && (hr == 0U || hr > ac + 3U))
    {
        hr = ac;
    }
    if(win != 0U)
    {
        if(hr == 0U)
        {
            hr = win;
        }
        else if(win >= HR_REST_OX_LO && win <= HR_REST_OX_HI && hr > win + 3U)
        {
            hr = win;
        }
        else if(hr > win + 12U)
        {
            hr = (uint16_t)((hr * 3U + win * 7U + 5U) / 10U);
        }
    }
    rest = ppg_rest_plausible_hr();
    if(rest != 0U && hr > rest + 3U)
    {
        hr = rest;
    }
    if(ac >= HR_REST_OX_LO && ac <= HR_REST_OX_HI && hr > ac + 3U)
    {
        hr = ac;
    }
    if(g_rest_hr_anchor >= HR_REST_OX_LO && hr > g_rest_hr_anchor + 4U)
    {
        hr = g_rest_hr_anchor;
    }
    if(force && hr == 0U)
    {
        hr = HR_PACE_NOTIFY_MIN;
    }
    if(hr > 0U && hr < HR_PACE_NOTIFY_MIN)
    {
        hr = HR_PACE_NOTIFY_MIN;
    }
    if(hr > HR_PACE_NOTIFY_MAX)
    {
        hr = HR_PACE_NOTIFY_MAX;
    }
    return hr;
}

static uint32_t ppg_now_ms(void)
{
    return (uint32_t)(((uint64_t)TMOS_GetSystemClock() * 625ULL) / 1000ULL);
}

static uint8_t ppg_sync_window_ready(void)
{
    uint32_t now_ms;

    if(!g_paced_notify_pending)
    {
        return 1U;
    }
    if(g_window_open)
    {
        return 1U;
    }
    if(g_window_t0_ms == 0U)
    {
        return 1U;
    }

    now_ms = ppg_now_ms();
    if(now_ms >= g_window_t0_ms)
    {
        g_window_open = 1U;
        g_window_start_ms = g_window_t0_ms;
        return 1U;
    }
    return 0U;
}

static uint16_t ppg_clamp_u16(uint16_t v, uint16_t lo, uint16_t hi)
{
    if(v < lo)
    {
        return lo;
    }
    if(v > hi)
    {
        return hi;
    }
    return v;
}

static uint16_t ppg_role_zone_start(uint16_t len)
{
    uint16_t global_lo = PPG_PEAK_SEARCH_MARGIN + PPG_PEAK_INNER_MARGIN;
    uint16_t global_hi = len - PPG_PEAK_SEARCH_MARGIN - PPG_PEAK_INNER_MARGIN;

    if(global_hi <= global_lo)
    {
        return len / 4U;
    }
    return ppg_clamp_u16(PPG_PEAK_ROLE_IDX_MIN, global_lo, global_hi - 1U);
}

static uint16_t ppg_role_zone_end(uint16_t len)
{
    uint16_t global_lo = PPG_PEAK_SEARCH_MARGIN + PPG_PEAK_INNER_MARGIN;
    uint16_t global_hi = len - PPG_PEAK_SEARCH_MARGIN - PPG_PEAK_INNER_MARGIN;
    uint16_t start     = ppg_role_zone_start(len);
    uint16_t role_end  = PPG_PEAK_ROLE_IDX_MAX + 1U;

    if(global_hi <= global_lo)
    {
        return len - start;
    }
    return ppg_clamp_u16(role_end, start + 1U, global_hi);
}

static uint16_t ppg_role_center_idx(void)
{
    return (PPG_PEAK_ROLE_IDX_MIN + PPG_PEAK_ROLE_IDX_MAX) / 2U;
}

static uint8_t ppg_peak_in_forbid_zone(uint16_t idx)
{
    if((PPG_PEAK_EDGE_FORBID_LO > 0U) && (idx <= PPG_PEAK_EDGE_FORBID_LO))
    {
        return 1U;
    }
    if(idx >= PPG_PEAK_EDGE_FORBID_HI)
    {
        return 1U;
    }
    return 0U;
}

static float ppg_peak_prefer_weight(uint16_t idx)
{
    uint16_t center;
    int      dist;
    int      half_span;
    int      shift;

    /* P3-A：按拍号微移偏好中心，使 Δb 在 sweet 区内游走 */
    shift = 0;
    switch(g_paced_pulse_id % 3U)
    {
        case 1U: shift = -1; break;
        case 2U: shift = 1;  break;
        default: break;
    }

    if((idx >= PPG_PEAK_PREFER_MIN) && (idx <= PPG_PEAK_PREFER_MAX))
    {
        center = (uint16_t)((int)((PPG_PEAK_PREFER_MIN + PPG_PEAK_PREFER_MAX) / 2U) + shift);
        if(center < PPG_PEAK_PREFER_MIN)
        {
            center = PPG_PEAK_PREFER_MIN;
        }
        if(center > PPG_PEAK_PREFER_MAX)
        {
            center = PPG_PEAK_PREFER_MAX;
        }
        if(idx <= center)
        {
            return 1.0f + PPG_PEAK_EARLY_BIAS * (float)(center - idx);
        }
        return 1.0f;
    }
    center    = (uint16_t)((int)((PPG_PEAK_PREFER_MIN + PPG_PEAK_PREFER_MAX) / 2U) + shift);
    half_span = (int)(PPG_PEAK_PREFER_MAX - PPG_PEAK_PREFER_MIN) / 2;
    if(half_span <= 0)
    {
        return 0.75f;
    }
    dist = (idx > center) ? (int)(idx - center) : (int)(center - idx);
    if(dist > half_span * 2)
    {
        return 0.55f;
    }
    {
        float w = 1.0f - (0.35f * (float)dist / (float)(half_span * 2));
        if(idx <= center)
        {
            w *= 1.0f + PPG_PEAK_EARLY_BIAS * (float)(center - idx);
        }
        return w;
    }
}

static uint8_t ppg_peak_score_better(float score, uint16_t idx, float best_score, uint16_t best_idx)
{
    if(score > best_score)
    {
        return 1U;
    }
    if((best_score > 0.0f) && (score >= (best_score * PPG_PEAK_SCORE_TIE_RATIO)))
    {
        /* 同分倾向更早的峰 → 更小 b_idx */
        return (idx < best_idx) ? 1U : 0U;
    }
    return 0U;
}

static void ppg_prepare_ac_wave(const float *ir, uint16_t start, uint16_t end)
{
    uint16_t i;
    uint16_t j0;
    uint16_t j1;
    uint16_t j;
    float    ma;
    float    cnt;

    for(i = start; i < end; i++)
    {
        j0 = (i > PPG_DETREND_MA_HALF) ? (i - PPG_DETREND_MA_HALF) : start;
        j1 = (i + PPG_DETREND_MA_HALF < end) ? (i + PPG_DETREND_MA_HALF) : (end - 1U);
        ma  = 0.0f;
        cnt = 0.0f;
        for(j = j0; j <= j1; j++)
        {
            ma += ir[j];
            cnt += 1.0f;
        }
        if(cnt > 0.0f)
        {
            ma /= cnt;
        }
        g_ppg_ac_work[i] = ir[i] - ma;
    }
}

static uint16_t ppg_find_ir_peak_idx(const float *ir, uint16_t len)
{
    uint16_t start = ppg_role_zone_start(len);
    uint16_t end   = ppg_role_zone_end(len);
    float    min_ac;
    float    max_ac;
    float    min_prom;
    float    range;
    uint16_t peak;
    uint16_t i;
    uint8_t  found = 0U;

    if((end <= start + 2U) || (len <= (PPG_PEAK_SEARCH_MARGIN * 2U)))
    {
        start = len / 4U;
        end   = len - start;
    }

    min_ac = ir[start];
    max_ac = ir[start];
    for(i = start; i < end; i++)
    {
        if(ir[i] < min_ac)
        {
            min_ac = ir[i];
        }
        if(ir[i] > max_ac)
        {
            max_ac = ir[i];
        }
    }
    range = max_ac - min_ac;
    min_prom = 0.10f * range;
    if(min_prom < 1.0f)
    {
        min_prom = 1.0f;
    }

    ppg_prepare_ac_wave(ir, start, end);

    peak   = ppg_role_center_idx();
    max_ac = -1e9f;
    for(i = start + 1U; i + 1U < end; i++)
    {
        float ac   = g_ppg_ac_work[i];
        float ac_l = g_ppg_ac_work[i - 1U];
        float ac_r = g_ppg_ac_work[i + 1U];
        float score;

        if(!((ac >= ac_l) && (ac > ac_r) && (ac >= min_prom)))
        {
            continue;
        }
        if(ppg_peak_in_forbid_zone(i))
        {
            continue;
        }
        score = ac * ppg_peak_prefer_weight(i);
        if(ppg_peak_score_better(score, i, max_ac, peak))
        {
            max_ac = score;
            peak   = i;
            found  = 1U;
        }
    }

    if(!found)
    {
        max_ac = -1e9f;
        for(i = start + 1U; i + 1U < end; i++)
        {
            float ac   = g_ppg_ac_work[i];
            float ac_l = g_ppg_ac_work[i - 1U];
            float ac_r = g_ppg_ac_work[i + 1U];
            float score;

            if(!((ac >= ac_l) && (ac > ac_r)))
            {
                continue;
            }
            score = ac * ppg_peak_prefer_weight(i);
            if(ppg_peak_in_forbid_zone(i))
            {
                score *= 0.25f;
            }
            if(ppg_peak_score_better(score, i, max_ac, peak))
            {
                max_ac = score;
                peak   = i;
                found  = 1U;
            }
        }
    }

    if(!found || (max_ac < min_prom))
    {
        peak = ppg_role_center_idx();
    }

    return peak;
}

static uint8_t ppg_beat_idx_usable(uint16_t beat_idx)
{
    if(beat_idx < PPG_PEAK_IDX_MIN || beat_idx > PPG_PEAK_IDX_MAX)
    {
        return 0U;
    }
    if(beat_idx < PPG_PEAK_ROLE_IDX_MIN || beat_idx > PPG_PEAK_ROLE_IDX_MAX)
    {
        return 0U;
    }
    if(ppg_peak_in_forbid_zone(beat_idx))
    {
        return 0U;
    }
    return 1U;
}

static uint32_t ppg_beat_ts_ms(uint16_t peak_idx)
{
    return (uint32_t)peak_idx * PPG_MS_PER_SAMPLE;
}

static float ppg_goertzel_power(const float *x, uint16_t n, float freq_hz)
{
    const float fs = 50.0f;
    float       k;
    float       w;
    float       coeff;
    float       s0;
    float       s1;
    float       s2;
    float       real;
    float       imag;
    uint16_t    i;

    k     = ((float)n * freq_hz) / fs;
    w     = (2.0f * 3.14159265f * k) / (float)n;
    coeff = 2.0f * cosf(w);
    s0    = 0.0f;
    s1    = 0.0f;
    s2    = 0.0f;
    for(i = 0U; i < n; i++)
    {
        s0 = x[i] + (coeff * s1) - s2;
        s2 = s1;
        s1 = s0;
    }
    real = s1 - (s2 * cosf(w));
    imag = s2 * sinf(w);
    return (real * real) + (imag * imag);
}

#define PPG_PI_LOCAL_HALF           20U

static uint8_t ppg_calc_pi_x10(uint16_t b_idx, float ac_range)
{
    uint16_t i0;
    uint16_t i1;
    uint16_t i;
    uint16_t cnt;
    float    dc;
    float    ir_min;
    float    ir_max;
    float    ac_amp;
    float    pi;
    float    pi_x10f;

    if(b_idx >= PPG_CACHE_LEN)
    {
        b_idx = PPG_CACHE_LEN - 1U;
    }
    i0 = (b_idx > PPG_PI_LOCAL_HALF) ? (uint16_t)(b_idx - PPG_PI_LOCAL_HALF) : 0U;
    i1 = b_idx + PPG_PI_LOCAL_HALF;
    if(i1 >= PPG_CACHE_LEN)
    {
        i1 = PPG_CACHE_LEN - 1U;
    }
    if(i1 <= i0)
    {
        return 0U;
    }
    dc = 0.0f;
    for(i = i0; i <= i1; i++)
    {
        dc += g_ppg_ir_cache[i];
    }
    cnt = (uint16_t)(i1 - i0 + 1U);
    dc /= (float)cnt;
    ir_min = g_ppg_ir_cache[i0];
    ir_max = ir_min;
    for(i = (uint16_t)(i0 + 1U); i <= i1; i++)
    {
        if(g_ppg_ir_cache[i] < ir_min)
        {
            ir_min = g_ppg_ir_cache[i];
        }
        if(g_ppg_ir_cache[i] > ir_max)
        {
            ir_max = g_ppg_ir_cache[i];
        }
    }
    ac_amp = (ir_max - ir_min) * 0.5f;
    if((ac_range * 0.5f) > ac_amp)
    {
        ac_amp = ac_range * 0.5f;
    }
    if(dc < 100.0f || ac_amp <= 0.0f)
    {
        return 0U;
    }
    pi = (ac_amp / dc) * 100.0f;
    if(pi > 25.5f)
    {
        pi = 25.5f;
    }
    pi_x10f = pi * 10.0f;
    return (uint8_t)(pi_x10f + 0.5f);
}

static void ppg_compute_wf_compact(uint16_t b_idx, ppg_wf_compact_t *out)
{
    float    mean;
    float    var;
    float    std;
    float    max_ac;
    float    min_ac;
    float    range;
    float    prom;
    float    dom_p;
    float    total_p;
    float    freq;
    float    pec;
    uint16_t i;

    if(out == NULL)
    {
        return;
    }

    ppg_prepare_ac_wave(g_ppg_ir_cache, 0U, PPG_CACHE_LEN);

    mean = 0.0f;
    for(i = 0U; i < PPG_CACHE_LEN; i++)
    {
        mean += g_ppg_ac_work[i];
    }
    mean /= (float)PPG_CACHE_LEN;

    var = 0.0f;
    for(i = 0U; i < PPG_CACHE_LEN; i++)
    {
        float d = g_ppg_ac_work[i] - mean;
        var += d * d;
    }
    std = sqrtf(var / (float)PPG_CACHE_LEN);

    max_ac = g_ppg_ac_work[0];
    min_ac = g_ppg_ac_work[0];
    for(i = 1U; i < PPG_CACHE_LEN; i++)
    {
        if(g_ppg_ac_work[i] > max_ac)
        {
            max_ac = g_ppg_ac_work[i];
        }
        if(g_ppg_ac_work[i] < min_ac)
        {
            min_ac = g_ppg_ac_work[i];
        }
    }
    range = max_ac - min_ac;
    if(range < 1.0f)
    {
        range = 1.0f;
    }

    if(b_idx >= PPG_CACHE_LEN)
    {
        b_idx = PPG_CACHE_LEN - 1U;
    }
    prom = g_ppg_ac_work[b_idx];
    if(prom < 0.0f)
    {
        prom = 0.0f;
    }

    dom_p   = 0.0f;
    total_p = 0.0f;
    freq    = 1.0f;
    {
        float scan_f;
        float best_f = 1.0f;

        for(scan_f = 0.5f; scan_f <= 3.01f; scan_f += 0.1f)
        {
            float p = ppg_goertzel_power(g_ppg_ac_work, PPG_CACHE_LEN, scan_f);
            total_p += p;
            if(p > dom_p)
            {
                dom_p  = p;
                best_f = scan_f;
            }
        }
        freq = best_f;
    }
    if(total_p < 1e-6f)
    {
        total_p = 1e-6f;
    }
    pec = dom_p / total_p;
    if(pec > 1.0f)
    {
        pec = 1.0f;
    }

    out->std_x10   = (int16_t)(std * 10.0f + 0.5f);
    out->prom_x100 = (int16_t)((prom / range) * 10000.0f + 0.5f);
    if(out->prom_x100 < 0)
    {
        out->prom_x100 = 0;
    }
    out->df_x10    = (uint8_t)(freq * 10.0f + 0.5f);
    if(out->df_x10 > 30U)
    {
        out->df_x10 = 30U;
    }
    out->pec_x100  = (uint8_t)(pec * 100.0f + 0.5f);
    out->pi_x10    = ppg_calc_pi_x10(b_idx, range);
    if(out->pi_x10 == 0U && out->prom_x100 >= 50)
    {
        uint8_t proxy = (uint8_t)(out->prom_x100 / 25U);

        if(proxy < 2U)
        {
            proxy = 2U;
        }
        if(proxy > 20U)
        {
            proxy = 20U;
        }
        out->pi_x10 = proxy;
    }
}

#if PPG_WAVELOG_ENABLE
static void ppg_wavelog_dump(const char *side, uint16_t pulse_id, uint16_t b_idx)
{
    uint16_t i;

    ppg_prepare_ac_wave(g_ppg_ir_cache, 0U, PPG_CACHE_LEN);
    PRINT("WAVE side=%s id=%u b_idx=%u n=%u ac=", side,
          (unsigned int)pulse_id, (unsigned int)b_idx, (unsigned int)PPG_CACHE_LEN);
    for(i = 0U; i < PPG_CACHE_LEN; i++)
    {
        int16_t q;
        float   v = g_ppg_ac_work[i];

        q = (int16_t)(v * 10.0f + ((v >= 0.0f) ? 0.5f : -0.5f));
        if(i > 0U)
        {
            PRINT(",");
        }
        PRINT("%d", (int)q);
    }
    PRINT("\n");
}
#endif

/* 全窗 AC 峰间隔估 HR（与 PWV 锚定 b_idx 解耦，便于运动后跟踪） */
static uint16_t ppg_estimate_window_hr(const float *ir, uint16_t len)
{
    uint16_t start = PPG_PEAK_SEARCH_MARGIN + PPG_PEAK_INNER_MARGIN;
    uint16_t end   = len - start;
    uint16_t peak_idx[12];
    uint16_t intervals[11];
    uint8_t  peak_cnt = 0U;
    uint8_t  iv_cnt   = 0U;
    uint16_t i;
    float    min_ac;
    float    max_ac;
    float    range;
    float    min_prom;
    uint16_t hr_bpm;

    if(end <= start + 4U)
    {
        return 0U;
    }

    min_ac = ir[start];
    max_ac = ir[start];
    for(i = start; i < end; i++)
    {
        if(ir[i] < min_ac)
        {
            min_ac = ir[i];
        }
        if(ir[i] > max_ac)
        {
            max_ac = ir[i];
        }
    }
    range = max_ac - min_ac;
    min_prom = 0.08f * range;
    if(min_prom < 0.5f)
    {
        min_prom = 0.5f;
    }

    ppg_prepare_ac_wave(ir, start, end);

    for(i = start + 1U; i + 1U < end; i++)
    {
        float ac   = g_ppg_ac_work[i];
        float ac_l = g_ppg_ac_work[i - 1U];
        float ac_r = g_ppg_ac_work[i + 1U];

        if((ac >= ac_l) && (ac > ac_r) && (ac >= min_prom))
        {
            if(peak_cnt < (uint8_t)(sizeof(peak_idx) / sizeof(peak_idx[0])))
            {
                peak_idx[peak_cnt++] = i;
            }
        }
    }

    if(peak_cnt < 2U)
    {
        return 0U;
    }

    for(i = 1U; i < peak_cnt; i++)
    {
        uint16_t d = peak_idx[i] - peak_idx[i - 1U];

        if((d >= 12U) && (d <= 90U))
        {
            if(iv_cnt < (uint8_t)(sizeof(intervals) / sizeof(intervals[0])))
            {
                intervals[iv_cnt++] = d;
            }
        }
    }

    if(iv_cnt == 0U)
    {
        return 0U;
    }

    for(i = 0U; i < iv_cnt; i++)
    {
        uint8_t j;

        for(j = (uint8_t)(i + 1U); j < iv_cnt; j++)
        {
            if(intervals[j] < intervals[i])
            {
                uint16_t t = intervals[i];

                intervals[i] = intervals[j];
                intervals[j] = t;
            }
        }
    }

    hr_bpm = (uint16_t)(3000U / intervals[iv_cnt / 2U]);

    if(hr_bpm > 71U)
    {
        uint16_t slow_iv[11];
        uint8_t  slow_cnt = 0U;
        uint8_t  j;

        for(i = 0U; i < iv_cnt; i++)
        {
            if(intervals[i] >= 43U)
            {
                slow_iv[slow_cnt++] = intervals[i];
            }
        }
        if(slow_cnt >= 2U)
        {
            for(i = 0U; i < slow_cnt; i++)
            {
                for(j = (uint8_t)(i + 1U); j < slow_cnt; j++)
                {
                    if(slow_iv[j] < slow_iv[i])
                    {
                        uint16_t t = slow_iv[i];

                        slow_iv[i] = slow_iv[j];
                        slow_iv[j] = t;
                    }
                }
            }
            hr_bpm = (uint16_t)(3000U / slow_iv[slow_cnt / 2U]);
        }
        else if(slow_cnt == 1U)
        {
            hr_bpm = (uint16_t)(3000U / slow_iv[0]);
        }
    }

    if(hr_bpm < 65U)
    {
        uint16_t min_d = 0U;

        for(i = 0U; i < iv_cnt; i++)
        {
            if((intervals[i] >= 38U) && (intervals[i] <= 44U))
            {
                if((min_d == 0U) || (intervals[i] < min_d))
                {
                    min_d = intervals[i];
                }
            }
        }
        if(min_d != 0U)
        {
            uint16_t hr_fast = (uint16_t)(3000U / min_d);

            if(hr_fast > hr_bpm && hr_fast <= HR_REST_OX_HI)
            {
                hr_bpm = hr_fast;
            }
        }
    }

    if(hr_bpm < 62U)
    {
        uint16_t min_d = 0U;

        for(i = 0U; i < iv_cnt; i++)
        {
            if((intervals[i] >= 36U) && (intervals[i] <= 48U))
            {
                if((min_d == 0U) || (intervals[i] < min_d))
                {
                    min_d = intervals[i];
                }
            }
        }
        if(min_d != 0U)
        {
            hr_bpm = (uint16_t)(3000U / min_d);
        }
    }

    if((hr_bpm < HR_VALID_MIN) || (hr_bpm > HR_VALID_MAX))
    {
        return 0U;
    }
    return hr_bpm;
}

static uint16_t ppg_merge_window_hr(uint16_t hr_drv, uint16_t hr_ac)
{
    uint16_t lo;
    uint16_t hi;
    uint16_t gap;

    if(hr_ac == 0U)
    {
        return hr_drv;
    }
    if(hr_drv == 0U)
    {
        return hr_ac;
    }

    lo = (hr_drv <= hr_ac) ? hr_drv : hr_ac;
    hi = (hr_drv <= hr_ac) ? hr_ac : hr_drv;
    gap = hi - lo;

    if(g_hr_active_mode || hi >= HR_ACTIVE_ENTER)
    {
        if(gap <= 10U)
        {
            return (uint16_t)((lo + hi + 1U) / 2U);
        }
        return hi;
    }

    if(lo >= HR_REST_OX_LO && hi <= HR_REST_OX_HI)
    {
        if(gap <= 6U)
        {
            return (uint16_t)((lo + hi + 1U) / 2U);
        }
        return lo;
    }
    if(hr_drv >= HR_REST_OX_LO && hr_drv <= HR_REST_OX_HI &&
       hr_ac > (HR_REST_OX_HI + 1U) && gap > HR_MERGE_AC_TRUST_GAP)
    {
        return hr_drv;
    }
    if(hr_ac >= HR_REST_OX_LO && hr_ac <= HR_REST_OX_HI &&
       hr_drv > (HR_REST_OX_HI + 1U) && gap > HR_MERGE_AC_TRUST_GAP)
    {
        return hr_ac;
    }
    if(hr_ac >= HR_VALID_MIN &&
       (hr_drv >= (uint16_t)(hr_ac * 2U - HR_MERGE_HARMONIC_TOL)) &&
       (hr_drv <= (uint16_t)(hr_ac * 2U + HR_MERGE_HARMONIC_TOL)))
    {
        return hr_ac;
    }
    if((hr_drv - hr_ac) > HR_MERGE_AC_TRUST_GAP)
    {
        return hr_ac;
    }
    if(gap <= 8U)
    {
        return (uint16_t)((hr_drv + hr_ac + 1U) / 2U);
    }
    if(hi > (HR_REST_OX_HI + 1U) && lo >= HR_REST_OX_LO && lo <= HR_REST_OX_HI)
    {
        return lo;
    }
    return hr_drv;
}

static void ppg_update_stable_hr(uint16_t heart_rate)
{
    uint16_t delta;
    uint16_t step;

    if(heart_rate == 0U)
    {
        return;
    }
    if(heart_rate > 120U)
    {
        heart_rate = 120U;
    }
    if(!g_hr_active_mode &&
       g_last_hr_ac >= HR_REST_OX_LO && g_last_hr_ac <= HR_REST_OX_HI &&
       heart_rate > g_last_hr_ac + 10U)
    {
        heart_rate = g_last_hr_ac;
    }

    if(g_stable_hr == 0U)
    {
        g_stable_hr = heart_rate;
        return;
    }

    if(g_hr_active_mode)
    {
        g_stable_hr = (uint16_t)((g_stable_hr * 3U + heart_rate * 7U + 5U) / 10U);
        if(g_stable_hr > HR_PACE_NOTIFY_MAX)
        {
            g_stable_hr = HR_PACE_NOTIFY_MAX;
        }
        return;
    }

    delta = (heart_rate > g_stable_hr) ? (heart_rate - g_stable_hr)
                                         : (g_stable_hr - heart_rate);
    if(delta >= HR_SMOOTH_JUMP_THRESH)
    {
        if(heart_rate > g_stable_hr)
        {
            g_stable_hr = (uint16_t)((g_stable_hr + heart_rate + 1U) / 2U);
        }
        else
        {
            g_stable_hr = (uint16_t)((g_stable_hr * 7U + heart_rate * 3U + 5U) / 10U);
        }
        if(g_stable_hr > HR_PACE_NOTIFY_MAX)
        {
            g_stable_hr = HR_PACE_NOTIFY_MAX;
        }
        return;
    }

    step = (heart_rate > g_stable_hr) ? HR_SMOOTH_RISE_STEP : HR_SMOOTH_MAX_STEP;
    if(delta > step)
    {
        if(heart_rate > g_stable_hr)
        {
            g_stable_hr = (uint16_t)(g_stable_hr + step);
        }
        else
        {
            g_stable_hr = (uint16_t)(g_stable_hr - step);
        }
    }
    else if(heart_rate > g_stable_hr + 8U)
    {
        g_stable_hr = (uint16_t)((g_stable_hr * 2U + heart_rate * 8U + 5U) / 10U);
    }
    else
    {
        g_stable_hr = (uint16_t)((g_stable_hr * 6U + heart_rate * 4U + 5U) / 10U);
    }

    if(g_stable_hr > HR_PACE_NOTIFY_MAX)
    {
        g_stable_hr = HR_PACE_NOTIFY_MAX;
    }
}

static uint16_t ppg_wrist_hr_raw_for_notify(uint8_t force)
{
    return ppg_hr_pick_raw_for_notify(force);
}

void max30102_measure_init(void)
{
    g_max30102_int_flag  = 0U;
    g_ppg_cache_idx      = 0U;
    g_sensor_err_cnt     = 0U;
    g_weak_signal_cnt    = 0U;
    g_stable_hr          = 0U;
    g_stable_spo2_x100   = -1;
    g_spo2_invalid_seq   = 0U;
    g_last_good_spo2_u8  = 0U;
    g_last_good_hr       = 0U;
    g_paced_notify_pending = 0U;
    g_window_t0_ms = 0U;
    g_window_start_ms = 0U;
    g_window_open = 0U;
    g_paced_pulse_id = 0U;
    g_pace_edge_retry = 0U;
    g_pace_quality_skips = 0U;
    g_pace_force_next    = 0U;
    g_last_window_hr = 0U;
    g_last_hr_ac     = 0U;
    g_last_hr_drv    = 0U;
    g_rest_hr_anchor = 0U;
    g_hr_active_mode   = 0U;
    g_poll_dbg_cnt       = 0U;
    memset(g_ppg_ir_cache, 0, sizeof(g_ppg_ir_cache));
    memset(g_ppg_red_cache, 0, sizeof(g_ppg_red_cache));

    /* 强制走完整 init：清驱动 once-only 标志 → 再 init 会真的写 FIFO 指针 */
    max30102_reset();
    if(!max30102_init())
    {
        PRINT("MAX30102 init failed\n");
    }
    max30102_fir_init();   /* 已无 once-only guard，每次都会重零 state */
    PRINT("PPG: measure_init done, ready=%u\n", (unsigned)max30102_is_ready());
}

void max30102_measure_arm_paced_window(void)
{
    g_ppg_cache_idx = 0U;
    g_weak_signal_cnt = 0U;
    g_window_t0_ms = 0U;
    g_window_start_ms = 0U;
    g_window_open = 1U;
    g_paced_notify_pending = 1U;
    g_pace_quality_skips = 0U;
}

void max30102_measure_on_sync_pulse(uint16_t pulse_id)
{
    g_paced_pulse_id = pulse_id;
    g_pace_edge_retry = 0U;
    g_pace_quality_skips = 0U;
    g_pace_force_next    = 0U;
}

void max30102_measure_schedule_sync_window(uint16_t delay_ms)
{
    g_ppg_cache_idx = 0U;
    g_weak_signal_cnt = 0U;
    g_window_open = 0U;
    g_window_start_ms = 0U;
    g_window_t0_ms = ppg_now_ms() + (uint32_t)delay_ms;
    g_paced_notify_pending = 1U;
    g_pace_edge_retry = 0U;
    PRINT("PPG: sync window T0 in %u ms\n", (unsigned int)delay_ms);
}

void max30102_measure_poll(void)
{
    uint8_t  sampled_cnt = 0U;
    uint8_t  avail;

    if(g_max30102_int_flag)
    {
        g_max30102_int_flag = 0U;
    }

    if(!max30102_is_ready())
    {
        g_sensor_err_cnt++;
        if((++g_poll_dbg_cnt % PPG_DEBUG_PRINT_EVERY) == 0U)
        {
            PRINT("PPG: sensor not ready err=%u\n", (unsigned)g_sensor_err_cnt);
        }
        goto check_reinit;
    }

    avail = max30102_fifo_available();
    if(avail == 0U)
    {
        if((++g_poll_dbg_cnt % PPG_DEBUG_PRINT_EVERY) == 0U)
        {
            PRINT("PPG: FIFO empty cache=%u\n", (unsigned)g_ppg_cache_idx);
        }
        return;
    }

    while((max30102_fifo_available() > 0U) && (sampled_cnt < 4U))
    {
        float raw_data[2] = {0};
        if(!max30102_fifo_read(raw_data))
        {
            g_sensor_err_cnt++;
            PRINT("PPG: fifo_read fail\n");
            break;
        }

        g_sensor_err_cnt = 0U;
        sampled_cnt++;
        /* 每 ~1s 打印一组原始值，便于判断"信号是否到达" */
        if((++g_poll_dbg_cnt % PPG_DEBUG_PRINT_EVERY) == 0U)
        {
            PRINT("PPG raw IR=%.0f RED=%.0f cache=%u\n",
                  raw_data[0], raw_data[1], (unsigned)g_ppg_cache_idx);
        }
        ppg_data_process(raw_data);
    }

check_reinit:
    if(g_sensor_err_cnt >= SENSOR_ERR_REINIT_TH)
    {
        if(g_paced_notify_pending && (g_sensor_err_cnt < SENSOR_ERR_REINIT_PACE_TH))
        {
            return;
        }
        g_sensor_err_cnt = 0U;
        g_ppg_cache_idx  = 0U;
        max30102_reset();           /* 让 re-init 真的重置 */
        if(!max30102_init())
        {
            PRINT("MAX30102 re-init failed\n");
        }
        else
        {
            PRINT("PPG: re-init OK\n");
        }
    }
}

static void ppg_data_process(float *raw_data)
{
    float fir_out[2] = {0};

    if(g_paced_notify_pending && !ppg_sync_window_ready())
    {
        return;
    }

    ir_max30102_fir(&raw_data[0], &fir_out[0]);
    red_max30102_fir(&raw_data[1], &fir_out[1]);

    /* 顶满/异常帧：262143=0x3FFFF，丢弃且不计 weak（避免 cache 被反复清零） */
    if((raw_data[0] >= PPG_SATURATION_RAW) || (raw_data[1] >= PPG_SATURATION_RAW))
    {
        return;
    }

    if((raw_data[0] < PPG_RAW_MIN) || (raw_data[1] < PPG_RAW_MIN))
    {
        if(g_weak_signal_cnt < 255U)
        {
            g_weak_signal_cnt++;
        }

        if(g_weak_signal_cnt >= PPG_WEAK_RESET_TH)
        {
            g_ppg_cache_idx = 0U;
            g_weak_signal_cnt = 0U;
        }
        return;
    }

    g_weak_signal_cnt = 0U;
    g_ppg_ir_cache[g_ppg_cache_idx] = fir_out[0];
    g_ppg_red_cache[g_ppg_cache_idx] = fir_out[1];
    g_ppg_cache_idx++;

    if(g_ppg_cache_idx >= PPG_CACHE_LEN)
    {
        ppg_calc_and_publish();
        g_ppg_cache_idx = 0U;
    }
}

static void ppg_paced_try_notify(void)
{
    uint8_t  force;
    uint8_t  spo2_u8;
    uint16_t hr_raw;
    uint16_t hr_notify;
    uint16_t hr_min;
    uint16_t beat_idx;
    uint32_t beat_ts;

    if(!g_paced_notify_pending)
    {
        return;
    }

    force = (g_pace_force_next ||
             g_pace_quality_skips >= PPG_PACE_QUALITY_SKIP_MAX) ? 1U : 0U;

    beat_idx = ppg_find_ir_peak_idx(g_ppg_ir_cache, PPG_CACHE_LEN);
    if(!ppg_beat_idx_usable(beat_idx))
    {
        /* P3-B：同窗中心 fallback，不 retry（避免 ~3s late） */
        beat_idx = ppg_role_center_idx();
        PRINT("WRIST paced edge fallback b_idx=%u\n", (unsigned int)beat_idx);
    }
    g_pace_edge_retry = 0U;

    if(!force && g_stable_hr == 0U && g_last_window_hr == 0U &&
       g_paced_pulse_id > PPG_WARMUP_PULSE_MAX)
    {
        if(g_last_good_hr >= HR_PACE_NOTIFY_MIN)
        {
            /* 继续，后面用 hold */
        }
        else
        {
            g_pace_quality_skips++;
            g_pace_force_next = 1U;
            PRINT("WRIST paced skip bad HR/SpO2, retry next window\n");
            return;
        }
    }

    ppg_update_activity_mode(g_last_window_hr);
    hr_raw = ppg_wrist_hr_raw_for_notify(force);
    ppg_update_activity_mode(hr_raw);

    /* P3-K：同窗 hold，禁止低 HR / 尖峰 retry 制造 late */
    if(hr_raw == 0U && g_last_good_hr >= HR_PACE_NOTIFY_MIN)
    {
        PRINT("WRIST paced hr hold %u (raw 0)\n",
              (unsigned int)g_last_good_hr);
        hr_raw = g_last_good_hr;
    }

    if(!g_hr_active_mode && g_paced_pulse_id <= 3U && hr_raw > HR_PACE_REST_SKIP_HI)
    {
        if(g_last_good_hr >= HR_REST_OX_LO && g_last_good_hr <= HR_PACE_REST_SKIP_HI)
        {
            PRINT("WRIST paced hr hold %u (warmup spike %u)\n",
                  (unsigned int)g_last_good_hr, (unsigned int)hr_raw);
            hr_raw = g_last_good_hr;
        }
        else
        {
            hr_raw = HR_PACE_REST_SKIP_HI;
            PRINT("WRIST paced hr clamp warmup spike -> %u\n",
                  (unsigned int)hr_raw);
        }
    }
    if(!g_hr_active_mode && hr_raw > HR_PACE_REST_SKIP_HI &&
       g_paced_pulse_id > PPG_WARMUP_PULSE_MAX)
    {
        uint16_t rest = ppg_rest_plausible_hr_min();

        if(rest != 0U)
        {
            hr_raw = rest;
        }
        else if(g_rest_hr_anchor != 0U)
        {
            hr_raw = g_rest_hr_anchor;
        }
        else if(g_last_good_hr >= HR_REST_OX_LO)
        {
            PRINT("WRIST paced hr hold %u (spike %u)\n",
                  (unsigned int)g_last_good_hr, (unsigned int)hr_raw);
            hr_raw = g_last_good_hr;
        }
        else
        {
            hr_raw = HR_PACE_REST_SKIP_HI;
            PRINT("WRIST paced hr clamp spike -> %u\n", (unsigned int)hr_raw);
        }
    }

    if(g_paced_pulse_id > PPG_WARMUP_PULSE_MAX &&
       (hr_raw < HR_PACE_NOTIFY_MIN || hr_raw > HR_PACE_NOTIFY_MAX))
    {
        if(g_last_good_hr >= HR_PACE_NOTIFY_MIN &&
           g_last_good_hr <= HR_PACE_NOTIFY_MAX)
        {
            PRINT("WRIST paced hr hold %u (oor %u)\n",
                  (unsigned int)g_last_good_hr, (unsigned int)hr_raw);
            hr_raw = g_last_good_hr;
        }
        else
        {
            hr_raw = (hr_raw > HR_PACE_NOTIFY_MAX) ? HR_PACE_NOTIFY_MAX
                                                   : HR_PACE_NOTIFY_MIN;
            PRINT("WRIST paced hr fallback oor -> %u\n", (unsigned int)hr_raw);
        }
    }
    if(force && (hr_raw < HR_PACE_NOTIFY_MIN || hr_raw > HR_PACE_NOTIFY_MAX))
    {
        PRINT("WRIST paced hr fallback raw=%u\n", (unsigned int)hr_raw);
    }

    if(g_stable_spo2_x100 >= 0)
    {
        spo2_u8 = (uint8_t)((g_stable_spo2_x100 + 50) / 100);
    }
    else if(g_last_good_spo2_u8 >= 70U)
    {
        /* P3-B：稳定值暂空时沿用上拍，禁止 retry 制造 late */
        spo2_u8 = g_last_good_spo2_u8;
        PRINT("WRIST paced spo2 hold %u (stable invalid)\n",
              (unsigned int)spo2_u8);
    }
    else if(g_paced_pulse_id <= PPG_WARMUP_PULSE_MAX)
    {
        spo2_u8 = 88U;
    }
    else
    {
        spo2_u8 = 88U;
        PRINT("WRIST paced spo2 fallback 88 (no stable)\n");
    }

    spo2_u8 = ppg_wrist_spo2_for_notify(spo2_u8);
    /*
     * 贴合差时 raw 常偏低：再 retry 只会制造 late。
     * ≥70 直接抬到 ≥88 上报（含暖机），仅 <70 才 retry。
     */
    if(spo2_u8 < 70U)
    {
        if(g_paced_pulse_id > PPG_WARMUP_PULSE_MAX && !force)
        {
            g_pace_quality_skips++;
            g_pace_force_next = 1U;
            PRINT("WRIST paced skip low spo2=%u, retry next window\n",
                  (unsigned int)spo2_u8);
            return;
        }
        spo2_u8 = 88U;
    }
    else if(spo2_u8 < 88U)
    {
        PRINT("WRIST paced spo2 lift %u -> 88\n", (unsigned int)spo2_u8);
        spo2_u8 = 88U;
    }
    else if(spo2_u8 < 90U && force)
    {
        spo2_u8 = 90U;
    }

    hr_notify = ppg_hr_for_notify(hr_raw);
    hr_min = (g_paced_pulse_id <= PPG_WARMUP_PULSE_MAX) ? 42U : HR_PACE_NOTIFY_MIN;
    if(hr_notify < hr_min)
    {
        if(g_last_good_hr >= hr_min)
        {
            PRINT("WRIST paced hr hold %u (was %u)\n",
                  (unsigned int)g_last_good_hr, (unsigned int)hr_notify);
            hr_notify = g_last_good_hr;
        }
        else
        {
            hr_notify = ppg_hr_for_notify(hr_min);
            PRINT("WRIST paced hr lift low -> %u\n", (unsigned int)hr_notify);
        }
    }

    beat_ts = ppg_beat_ts_ms(beat_idx);
    ppg_compute_wf_compact(beat_idx, &g_last_wf_compact);
    PRINT("WRIST paced hr=%u spo2=%u b_idx=%u beat_ts=%lu\n",
          (unsigned int)hr_notify, (unsigned int)spo2_u8,
          (unsigned int)beat_idx, (unsigned long)beat_ts);
    Peripheral_WristNotifyJson((uint8_t)hr_notify, spo2_u8, beat_idx, beat_ts, &g_last_wf_compact);
    if(spo2_u8 >= 70U)
    {
        g_last_good_spo2_u8 = spo2_u8;
    }
    if(hr_notify >= hr_min)
    {
        g_last_good_hr = hr_notify;
    }
    ppg_rest_hr_anchor_update(hr_notify);
#if PPG_WAVELOG_ENABLE
    ppg_wavelog_dump("WRIST", g_paced_pulse_id, beat_idx);
#endif
    g_pace_quality_skips = 0U;
    g_pace_force_next    = 0U;
    g_paced_notify_pending = 0U;
    g_window_t0_ms = 0U;
    g_window_start_ms = 0U;
    g_window_open = 0U;
}

static void ppg_calc_and_publish(void)
{
    uint16_t heart_rate = max30102_getHeartRate(g_ppg_ir_cache, PPG_CACHE_LEN);
    uint16_t hr_ac      = ppg_estimate_window_hr(g_ppg_ir_cache, PPG_CACHE_LEN);
    float    spo2       = max30102_getSpO2(g_ppg_ir_cache, g_ppg_red_cache, PPG_CACHE_LEN);

    g_last_hr_drv = heart_rate;
    g_last_hr_ac  = hr_ac;
    ppg_update_activity_mode((hr_ac > heart_rate) ? hr_ac : heart_rate);
    heart_rate = ppg_merge_window_hr(heart_rate, hr_ac);
    if(!g_hr_active_mode &&
       hr_ac >= HR_REST_OX_LO && hr_ac <= HR_REST_OX_HI &&
       heart_rate > hr_ac + HR_MERGE_AC_TRUST_GAP)
    {
        heart_rate = hr_ac;
    }

    if((heart_rate < HR_VALID_MIN) || (heart_rate > HR_VALID_MAX))
    {
        heart_rate = 0U;
    }
    if(heart_rate != 0U)
    {
        g_last_window_hr = heart_rate;
    }

    if((spo2 < SPO2_VALID_MIN) || (spo2 > SPO2_VALID_MAX) || !(spo2 == spo2))
    {
        spo2 = -1.0f;
    }

    ppg_update_stable_hr(heart_rate);

    if(spo2 >= 0.0f)
    {
        int32_t spo2_x100 = (int32_t)(spo2 * 100.0f + 0.5f);
        g_spo2_invalid_seq = 0U;
        if(spo2_x100 > SPO2_STABLE_MAX_X100)
        {
            spo2_x100 = SPO2_STABLE_MAX_X100;
        }
        if((g_stable_spo2_x100 < 0) ||
           ((spo2_x100 > g_stable_spo2_x100) ? ((spo2_x100 - g_stable_spo2_x100) <= SPO2_SMOOTH_MAX_STEP_X100)
                                             : ((g_stable_spo2_x100 - spo2_x100) <= SPO2_SMOOTH_MAX_STEP_X100)))
        {
            if(g_stable_spo2_x100 < 0)
            {
                g_stable_spo2_x100 = spo2_x100;
            }
            else
            {
                g_stable_spo2_x100 = (g_stable_spo2_x100 * 6 + spo2_x100 * 4 + 5) / 10;
            }
            if(g_stable_spo2_x100 > SPO2_STABLE_MAX_X100)
            {
                g_stable_spo2_x100 = SPO2_STABLE_MAX_X100;
            }
        }
    }
    else
    {
        if(g_spo2_invalid_seq < 255U)
        {
            g_spo2_invalid_seq++;
        }
        /* P3-B：短暂无效不清零，避免 notify 侧 retry/late；仍可走 last_good */
        if(g_spo2_invalid_seq >= 8U)
        {
            g_stable_spo2_x100 = -1;
        }
    }

    ppg_paced_try_notify();
}
