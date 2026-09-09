#include "pwv_ml_gate.h"
#include "lgbm_model.h"
#include <stddef.h>

/* P0：score>=0.15 才发布；硬门控启用后低于阈值拒收 */
#define PWV_ML_ACCEPT_THRESH  0.15f
#define PWV_ML_N_FEATURES     16

/*
 * 特征顺序（与 tools/export_model_c.py 一致）:
 *  0 w_b_idx  1 f_b_idx  2 delta_b  3 rx_dt_ms
 *  4 w_hr  5 f_hr  6 hr_gap  7 w_spo2  8 f_spo2
 *  9 w_wf_std  10 w_wf_prom  11 w_wf_df  12 w_wf_pec
 *  13 f_wf_std  14 f_wf_prom  15 f_wf_df  (f_wf_pec 合并到 12 差分)
 */

/* 须与 pwv.c 中 pwvSample_t 布局一致（含 pi_x10），否则 wf 偏移错位 */
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
} pwv_ml_sample_view_t;

static void pwv_ml_fill_features(const pwv_ml_input_t *in, float *feat)
{
    int hr_gap;

    feat[0]  = (float)in->w_b;
    feat[1]  = (float)in->f_b;
    feat[2]  = (float)in->delta_b;
    feat[3]  = in->rx_dt_ms;
    feat[4]  = (float)in->w_hr;
    feat[5]  = (float)in->f_hr;
    hr_gap   = (int)in->w_hr - (int)in->f_hr;
    if(hr_gap < 0)
    {
        hr_gap = -hr_gap;
    }
    feat[6]  = (float)hr_gap;
    feat[7]  = (float)in->w_spo2;
    feat[8]  = (float)in->f_spo2;
    feat[9]  = in->w_wf.valid ? (float)in->w_wf.std_x10 : 0.0f;
    feat[10] = in->w_wf.valid ? (float)in->w_wf.prom_x100 : 0.0f;
    feat[11] = in->w_wf.valid ? (float)in->w_wf.df_x10 : 0.0f;
    feat[12] = in->w_wf.valid ? (float)in->w_wf.pec_x100 : 0.0f;
    feat[13] = in->f_wf.valid ? (float)in->f_wf.std_x10 : 0.0f;
    feat[14] = in->f_wf.valid ? (float)in->f_wf.prom_x100 : 0.0f;
    feat[15] = in->f_wf.valid ? (float)in->f_wf.df_x10 : 0.0f;
}

pwv_ml_result_t pwv_ml_eval_input(const pwv_ml_input_t *in)
{
    pwv_ml_result_t out = {0};
    float           feat[PWV_ML_N_FEATURES];

    if(in == NULL)
    {
        out.score  = 0.0f;
        out.accept = 0U;
        return out;
    }

    pwv_ml_fill_features(in, feat);
    out.score = lgbm_predict_proba(feat, PWV_ML_N_FEATURES);
    out.accept = (out.score >= PWV_ML_ACCEPT_THRESH) ? 1U : 0U;
    return out;
}

pwv_ml_result_t pwv_ml_eval(const void *wristSample, const void *fingerSample,
                            float rx_dt_ms, int delta_b)
{
    const pwv_ml_sample_view_t *w;
    const pwv_ml_sample_view_t *f;
    pwv_ml_input_t              in = {0};

    w = (const pwv_ml_sample_view_t *)wristSample;
    f = (const pwv_ml_sample_view_t *)fingerSample;
    if(w == NULL || f == NULL)
    {
        pwv_ml_result_t bad = {0};
        return bad;
    }

    in.w_hr     = w->hr;
    in.f_hr     = f->hr;
    in.w_spo2   = w->spo2;
    in.f_spo2   = f->spo2;
    in.w_b      = w->beatIdx;
    in.f_b      = f->beatIdx;
    in.delta_b  = delta_b;
    in.rx_dt_ms = rx_dt_ms;
    in.w_wf     = w->wf;
    in.f_wf     = f->wf;
    return pwv_ml_eval_input(&in);
}
