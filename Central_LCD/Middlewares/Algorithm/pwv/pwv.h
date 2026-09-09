#ifndef PWV_H
#define PWV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PWV_PPG_MS_PER_SAMPLE       20U
#define PWV_PPG_WINDOW_SAMPLES      150U
#define PWV_WARMUP_PULSE_ID         1U
#define PWV_WARMUP_PULSE_MAX        2U
#define PWV_BEAT_IDX_MIN            35U
#define PWV_BEAT_IDX_MAX            114U
#define PWV_WRIST_BEAT_IDX_MAX      68U
#define PWV_FINGER_BEAT_IDX_MIN     50U
#define PWV_FINGER_BEAT_IDX_MAX     74U
#define PWV_BEAT_TS_SELF_MAX_MS     20.0f
#define PWV_PAIR_MAX_RX_MS          600.0f
#define PWV_PAIR_PUBLISH_MAX_RX_MS  800.0f
/* late soft 上限：>2s 仍拒，避免 q=27 级垃圾发布 */
#define PWV_PAIR_LATE_SOFT_MAX_RX_MS 2000.0f
#define PWV_LATE_PUBLISH_MIN_Q      55U
/* 腕部 paced 常比指部晚 2–3 窗；Δb 在 sweet 区时放宽至 6s，仍拒 6500ms 级异常 */
#define PWV_PAIR_LATE_PUBLISH_MAX_RX_MS 6000.0f
#define PWV_PAIR_SOFT_RX_MS         800.0f
#define PWV_PAIR_GOLDEN_RX_MS       800.0f
#define PWV_PAIR_SOFT_BIDX_MAX      12U
#define PWV_PAIR_BIDX_PUBLISH_MAX   12U
#define PWV_PENDING_SOLO_TIMEOUT_MS 10000U
#define PWV_WARMUP_WRIST_SPO2_MIN   80U
#define PWV_MIN_WRIST_HR            42U
#define PWV_MAX_WRIST_HR            100U
#define PWV_WRIST_HR_PRODUCT_MIN    55U
#define PWV_WRIST_HR_PRODUCT_MAX    100U
#define PWV_WRIST_SPO2_MIN          82U
#define PWV_FINGER_HR_PUBLISH_MIN   55U
#define PWV_FINGER_HR_PUBLISH_MAX   100U
#define PWV_FINGER_SPO2_MIN         80U
#define PWV_WARMUP_FINGER_SPO2_MIN  78U
#define PWV_HR_GAP_MAX              35U
#define PWV_WRIST_FINGER_HR_MAX_GAP PWV_HR_GAP_MAX
#define PWV_PAIR_BIDX_SWEET_MIN     3U
#define PWV_PAIR_BIDX_SWEET_MAX     6U
#define PWV_BIDX_DELTA3_QUALITY_PENALTY  5U
#define PWV_BIDX_DELTA6_QUALITY_PENALTY  5U
#define PWV_MIN_PTT_MS              60.0f
#define PWV_MAX_PTT_MS              200.0f
#define PWV_PUBLISH_MIN_MPS         4.0f
#define PWV_PUBLISH_MAX_MPS         8.0f
#define PWV_BEAT_ORDER_MIN_DELTA    1U

#define PWV_ML_GATE_ENABLE          1U
/* 0=ML 低分硬拒收；1=仅观测。score∈[0.10,0.15) 时仍拒收，≥0.15 通过 */
#define PWV_ML_GATE_OBSERVE_ONLY    0U
#define PWV_ML_SOFT_REJECT_THRESH   0.10f

/* PI×10：硬拒腕<1/指<3；软阈腕<5/指<6（P2-0：f_pi≥6 不罚） */
#define PWV_PI_WRIST_HARD_X10       1U
#define PWV_PI_FINGER_HARD_X10      3U
#define PWV_PI_WRIST_MIN_X10        5U
#define PWV_PI_FINGER_MIN_X10       5U
#define PWV_PI_GOOD_X10             80U
#define PWV_PI_SOFT_QUALITY_PENALTY 8U
#define PWV_ML_ZERO_SOFT_PENALTY    8U
/* Δb + PI + ML 软罚分总和上限 */
#define PWV_SOFT_PENALTY_CAP        12U
#define PWV_SESSION_QUALITY_MIN     65U
#define PWV_SESSION_RX_MAX_MS       800.0f
/* P3-J：仅 Δb≈4–5（约 80–100ms）入会话，避免 60/120ms 绑架中位 */
#define PWV_SESSION_PTT_MIN_MS      70.0f
#define PWV_SESSION_PTT_MAX_MS      110.0f
#define PWV_SESSION_RING            16U
#define PWV_PTT_CV_MAX_PCT          25U
#define PWV_PTT_CV_RING             8U
/* 软罚后质量地板：低于此不发布（避免 UI 出现 q=49） */
#define PWV_SOFT_PUBLISH_MIN_Q      55U

#define PWV_QUALITY_IDLE            0U
#define PWV_QUALITY_CALIBRATING     0xFEU

typedef struct
{
    uint8_t w_hr;
    uint8_t w_spo2;
    uint8_t f_hr;
    uint8_t f_spo2;
    float   pwv;
    float   pwv_raw;
    uint8_t pwv_valid;
    uint8_t signal_quality;
} pwv_session_t;

typedef struct
{
    int16_t std_x10;
    int16_t prom_x100;
    uint8_t df_x10;
    uint8_t pec_x100;
    uint8_t pi_x10;
    uint8_t valid;
} pwv_wf_compact_t;

void PWV_Init(void);
void PWV_OnMeasurementStart(void);
void PWV_OnMeasurementStop(void);
void PWV_OnPulseNextDone(void);
uint16_t PWV_GetLastAdvancedPulseId(void);
/* P3-F：hr_fusion 拒收时推进脉冲，避免 pending 死等 */
uint8_t PWV_OnFusionReject(uint8_t isWrist, uint16_t pulseId);
uint8_t PWV_IsEnabled(void);
uint8_t PWV_GetSessionSnapshot(pwv_session_t *out);
uint8_t PWV_GetSignalQuality(void);
uint8_t PWV_FeedWrist(uint16_t pulseId, uint8_t hr, uint8_t spo2,
                      uint16_t beatIdx, uint32_t beatTs, uint8_t pi_x10,
                      const pwv_wf_compact_t *wf);
uint8_t PWV_FeedFinger(uint16_t pulseId, uint8_t hr, uint8_t spo2,
                       uint16_t beatIdx, uint32_t beatTs, uint8_t pi_x10,
                       const pwv_wf_compact_t *wf);

#ifdef __cplusplus
}
#endif

#endif /* PWV_H */
