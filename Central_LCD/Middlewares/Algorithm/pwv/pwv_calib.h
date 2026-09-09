#ifndef PWV_CALIB_H
#define PWV_CALIB_H

#include <stdint.h>
#include "user_profile.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PWV_CALIB_PATH_FACTOR       1.12f
#define PWV_CALIB_REF_PATH_M        0.35f
#define PWV_CALIB_REF_HAND_CM       19U
#define PWV_CALIB_K                 1.30f
#define PWV_CALIB_B                 0.0f

#define PWV_CALIB_CHEN_A            5.592f
#define PWV_CALIB_CHEN_GENDER_M     0.717f
#define PWV_CALIB_CHEN_AGE_M        0.0293f
#define PWV_CALIB_REF_SD_MALE       0.75f
#define PWV_CALIB_REF_SD_FEMALE     0.65f

#define PWV_RAW_MIN_MPS             2.0f
#define PWV_RAW_MAX_MPS             12.0f

float pwv_calib_path_m(void);
float pwv_calib_raw_mps(float ptt_ms);
float pwv_calib_apply(float raw_mps);
void  pwv_calib_ref_band(float *mean_mps, float *lo_mps, float *hi_mps);
void  pwv_calib_ref_band_for(const user_profile_t *prof,
                             float *mean_mps, float *lo_mps, float *hi_mps);
const char *pwv_calib_trend_hint(float cal_mps, float mean_mps, float lo_mps, float hi_mps);

#ifdef __cplusplus
}
#endif

#endif /* PWV_CALIB_H */
