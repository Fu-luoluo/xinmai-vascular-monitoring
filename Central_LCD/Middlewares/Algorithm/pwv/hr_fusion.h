#ifndef HR_FUSION_H
#define HR_FUSION_H

#include "pwv.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct
{
    float   score;
    uint8_t finger_ok;
    uint8_t wrist_ok;
} hr_fusion_result_t;

void hr_fusion_reset(void);

void hr_fusion_note_finger(uint16_t pulse_id, uint8_t hr, uint8_t spo2,
                           const pwv_wf_compact_t *wf);

uint8_t hr_fusion_filter_finger(uint16_t pulse_id, uint8_t hr, uint8_t spo2,
                                const pwv_wf_compact_t *wf,
                                hr_fusion_result_t *out);

uint8_t hr_fusion_filter_wrist(uint16_t pulse_id, uint8_t hr, uint8_t spo2,
                               const pwv_wf_compact_t *wf,
                               hr_fusion_result_t *out);

#ifdef __cplusplus
}
#endif

#endif /* HR_FUSION_H */
