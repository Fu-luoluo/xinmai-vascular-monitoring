#ifndef PWV_ML_GATE_H
#define PWV_ML_GATE_H

#include "pwv.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float   score;
    uint8_t accept;
} pwv_ml_result_t;

typedef struct
{
    uint8_t  w_hr;
    uint8_t  f_hr;
    uint8_t  w_spo2;
    uint8_t  f_spo2;
    uint16_t w_b;
    uint16_t f_b;
    int      delta_b;
    float    rx_dt_ms;
    pwv_wf_compact_t w_wf;
    pwv_wf_compact_t f_wf;
} pwv_ml_input_t;

pwv_ml_result_t pwv_ml_eval_input(const pwv_ml_input_t *in);

pwv_ml_result_t pwv_ml_eval(const void *wristSample, const void *fingerSample,
                            float rx_dt_ms, int delta_b);

#ifdef __cplusplus
}
#endif

#endif /* PWV_ML_GATE_H */
