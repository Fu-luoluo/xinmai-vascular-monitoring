#include "hr_fusion.h"
#include <stddef.h>

#define HR_OX_LO              64U
#define HR_OX_HI              72U
#define HR_ACTIVE_LO          76U
#define HR_ACTIVE_HI          98U
#define HR_FINGER_REJECT_LO   58U
#define HR_WRIST_FINGER_GAP   5U

typedef struct
{
    uint16_t         pulse_id;
    uint8_t          hr;
    uint8_t          spo2;
    pwv_wf_compact_t wf;
    uint8_t          valid;
    uint8_t          active;
} hr_fusion_side_t;

static hr_fusion_side_t s_last_finger;
static uint8_t          s_session_active = 0U;

static float hr_fusion_wf_score(const pwv_wf_compact_t *wf)
{
    float score = 0.0f;

    if(wf == NULL || !wf->valid)
    {
        return 0.0f;
    }
    if(wf->prom_x100 >= 12)
    {
        score += 0.35f;
    }
    if(wf->std_x10 >= 4 && wf->std_x10 <= 90)
    {
        score += 0.25f;
    }
    if(wf->pec_x100 >= 15)
    {
        score += 0.20f;
    }
    if(wf->df_x10 >= 8 && wf->df_x10 <= 35)
    {
        score += 0.20f;
    }
    if(score > 1.0f)
    {
        score = 1.0f;
    }
    return score;
}

static uint8_t hr_fusion_is_active_hr(uint8_t hr)
{
    return (hr >= HR_ACTIVE_LO && hr <= HR_ACTIVE_HI) ? 1U : 0U;
}

void hr_fusion_reset(void)
{
    s_last_finger.valid  = 0U;
    s_last_finger.active = 0U;
    s_session_active     = 0U;
}

void hr_fusion_note_finger(uint16_t pulse_id, uint8_t hr, uint8_t spo2,
                           const pwv_wf_compact_t *wf)
{
    s_last_finger.pulse_id = pulse_id;
    s_last_finger.hr       = hr;
    s_last_finger.spo2     = spo2;
    s_last_finger.valid    = 0U;
    s_last_finger.active   = 0U;
    if(wf != NULL)
    {
        s_last_finger.wf = *wf;
    }
    else
    {
        s_last_finger.wf.valid = 0U;
    }
    if(hr >= HR_OX_LO && hr <= HR_OX_HI)
    {
        s_last_finger.valid = 1U;
    }
    if(hr_fusion_is_active_hr(hr))
    {
        s_last_finger.valid  = 1U;
        s_last_finger.active = 1U;
        s_session_active     = 1U;
    }
    else if(s_session_active)
    {
        s_last_finger.valid  = 1U;
        s_last_finger.active = 1U;
    }
}

uint8_t hr_fusion_filter_finger(uint16_t pulse_id, uint8_t hr, uint8_t spo2,
                                const pwv_wf_compact_t *wf,
                                hr_fusion_result_t *out)
{
    float wf_score;

    if(out != NULL)
    {
        out->score     = 0.0f;
        out->finger_ok = 0U;
        out->wrist_ok  = 0U;
    }
    if(hr == 0U)
    {
        /* 暖机首拍指端常 HR=0：用占位心率进入配对，避免会话卡住 */
        if(pulse_id >= PWV_WARMUP_PULSE_ID && pulse_id <= PWV_WARMUP_PULSE_MAX)
        {
            hr = 70U;
            wf_score = hr_fusion_wf_score(wf);
            if(out != NULL)
            {
                out->score     = wf_score;
                out->finger_ok = 1U;
            }
            hr_fusion_note_finger(pulse_id, hr, spo2, wf);
            return hr;
        }
        return 0U;
    }
    /* 暖机拍：指端 HR 常未收敛（如 54），仍需进入 PWV 配对 */
    if(pulse_id >= PWV_WARMUP_PULSE_ID && pulse_id <= PWV_WARMUP_PULSE_MAX)
    {
        if(hr >= PWV_MIN_WRIST_HR && hr <= PWV_FINGER_HR_PUBLISH_MAX)
        {
            wf_score = hr_fusion_wf_score(wf);
            if(out != NULL)
            {
                out->score     = wf_score;
                out->finger_ok = 1U;
            }
            hr_fusion_note_finger(pulse_id, hr, spo2, wf);
            return hr;
        }
        return 0U;
    }
    if(hr < HR_FINGER_REJECT_LO)
    {
        return 0U;
    }
    /* P3-F：原 58–63 死区硬拒会导致会话卡死；抬到 64 软通过 */
    if(!hr_fusion_is_active_hr(hr) &&
       hr >= HR_FINGER_REJECT_LO && hr < HR_OX_LO)
    {
        hr = HR_OX_LO;
    }

    wf_score = hr_fusion_wf_score(wf);
    if(out != NULL)
    {
        out->score     = wf_score;
        out->finger_ok = 1U;
    }

    hr_fusion_note_finger(pulse_id, hr, spo2, wf);
    return hr;
}

uint8_t hr_fusion_filter_wrist(uint16_t pulse_id, uint8_t hr, uint8_t spo2,
                               const pwv_wf_compact_t *wf,
                               hr_fusion_result_t *out)
{
    uint16_t fused;
    int      gap;
    float    wf_score;
    uint8_t  wrist_active;

    if(out != NULL)
    {
        out->score     = 0.0f;
        out->finger_ok = 0U;
        out->wrist_ok  = 0U;
    }
    if(hr == 0U)
    {
        return 0U;
    }

    wf_score = hr_fusion_wf_score(wf);
    wrist_active = hr_fusion_is_active_hr(hr);
    if(s_session_active)
    {
        wrist_active = 1U;
    }
    if(out != NULL)
    {
        out->score    = wf_score;
        out->wrist_ok = 1U;
    }

    if(wrist_active || s_last_finger.active || s_session_active)
    {
        if(out != NULL && s_last_finger.valid && s_last_finger.pulse_id == pulse_id)
        {
            out->finger_ok = 1U;
        }
        if(s_last_finger.valid && s_last_finger.pulse_id == pulse_id &&
           (s_last_finger.active || s_session_active))
        {
            gap = (int)s_last_finger.hr - (int)hr;
            if(gap > (int)HR_WRIST_FINGER_GAP)
            {
                /* 活动：腕端常偏低，向同 pulse 指端抬升并保留少量腕端差异 */
                fused = (uint16_t)((s_last_finger.hr * 9U + hr + 5U) / 10U);
                if(fused < hr)
                {
                    fused = hr;
                }
                if(fused > HR_ACTIVE_HI)
                {
                    fused = HR_ACTIVE_HI;
                }
                if(out != NULL)
                {
                    out->score += 0.15f;
                }
                return (uint8_t)fused;
            }
            gap = (int)hr - (int)s_last_finger.hr;
            if(gap < 0)
            {
                gap = -gap;
            }
            if(gap > (int)HR_WRIST_FINGER_GAP && hr < s_last_finger.hr)
            {
                fused = (uint16_t)((hr * 2U + s_last_finger.hr * 3U + 2U) / 5U);
                if(fused < hr)
                {
                    fused = hr;
                }
                if(fused > HR_ACTIVE_HI)
                {
                    fused = HR_ACTIVE_HI;
                }
                if(out != NULL)
                {
                    out->score += 0.10f;
                }
                return (uint8_t)fused;
            }
        }
        if(hr > HR_ACTIVE_HI)
        {
            return HR_ACTIVE_HI;
        }
        return hr;
    }

    if(s_last_finger.valid && s_last_finger.pulse_id == pulse_id &&
       s_last_finger.hr >= HR_OX_LO && s_last_finger.hr <= HR_OX_HI)
    {
        if(out != NULL)
        {
            out->finger_ok = 1U;
        }
        gap = (int)hr - (int)s_last_finger.hr;
        if(gap < 0)
        {
            gap = -gap;
        }
        if(gap > (int)HR_WRIST_FINGER_GAP)
        {
            /* Exp81：腕端系统性偏高，向同 pulse 指端收敛 */
            fused = (uint16_t)((hr * 2U + s_last_finger.hr * 3U + 2U) / 5U);
            if(fused < HR_OX_LO)
            {
                fused = HR_OX_LO;
            }
            if(fused > 75U)
            {
                fused = 75U;
            }
            if(out != NULL)
            {
                out->score += 0.15f;
            }
            return (uint8_t)fused;
        }
    }

    if(hr > 75U)
    {
        return (uint8_t)((hr * 3U + 68U + 2U) / 4U);
    }
    return hr;
}
