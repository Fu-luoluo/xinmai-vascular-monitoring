#ifndef __MAX30102_MEASURE_H
#define __MAX30102_MEASURE_H

#include <stdint.h>

extern volatile uint8_t g_max30102_int_flag;

#define PPG_SYNC_T0_DELAY_MS        250U
#define PPG_PEAK_SEARCH_MARGIN      20U
#define PPG_PEAK_INNER_MARGIN       8U
#define PPG_PEAK_EDGE_GUARD         7U
#define PPG_PEAK_IDX_MIN            (PPG_PEAK_SEARCH_MARGIN + PPG_PEAK_INNER_MARGIN + PPG_PEAK_EDGE_GUARD)
#define PPG_PEAK_IDX_MAX            (150U - PPG_PEAK_SEARCH_MARGIN - PPG_PEAK_INNER_MARGIN - PPG_PEAK_EDGE_GUARD - 1U)
/* 手指峰区 [50,74]：R2 收紧上沿，避免 f_b=69~75 导致 delta_b>12 */
#define PPG_PEAK_ROLE_IDX_MIN       50U
#define PPG_PEAK_ROLE_IDX_MAX       74U
/* P3-A/D：允许 60–64；去掉 65，减少与腕端组合 Δb=9 */
#define PPG_PEAK_EDGE_FORBID_LO     59U
#define PPG_PEAK_EDGE_FORBID_HI     65U
#define PPG_PEAK_PREFER_MIN         61U
#define PPG_PEAK_PREFER_MAX         63U
#define PPG_PEAK_LATE_BIAS          0.005f
#define PPG_PEAK_SCORE_TIE_RATIO    0.94f
#define PPG_DETREND_MA_HALF         12U
#define PPG_PACE_EDGE_RETRY_MAX     0U
#define PPG_PACE_QUALITY_SKIP_MAX   2U
#define PPG_WARMUP_PULSE_ID         1U
#define PPG_WARMUP_PULSE_MAX        2U

/* 第二阶段：UART 波形 dump（验证关 0，需采 wave 时再开 1） */
#define PPG_WAVELOG_ENABLE            0U

typedef struct
{
    int16_t std_x10;
    int16_t prom_x100;
    uint8_t df_x10;
    uint8_t pec_x100;
    uint8_t pi_x10;   /* PI×10，如 23 表示 2.3% */
} ppg_wf_compact_t;

void max30102_measure_init(void);
void max30102_measure_poll(void);
void max30102_measure_arm_paced_window(void);
void max30102_measure_schedule_sync_window(uint16_t delay_ms);
void max30102_measure_on_sync_pulse(uint16_t pulse_id);

#endif /* __MAX30102_MEASURE_H */
