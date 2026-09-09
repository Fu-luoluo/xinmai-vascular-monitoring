#include "pwv_calib.h"
#include <stddef.h>

float pwv_calib_path_m(void)
{
    float hand_cm;
    float L;
    float Lref;

    hand_cm = (float)UserProfile_EffectiveHandCm();
    L = (hand_cm / 100.0f) * PWV_CALIB_PATH_FACTOR;
    Lref = ((float)PWV_CALIB_REF_HAND_CM / 100.0f) * PWV_CALIB_PATH_FACTOR;
    if(Lref <= 0.0f) {
        return PWV_CALIB_REF_PATH_M;
    }
    return L * (PWV_CALIB_REF_PATH_M / Lref);
}

float pwv_calib_raw_mps(float ptt_ms)
{
    float path_m;

    if(ptt_ms <= 0.0f) {
        return 0.0f;
    }
    path_m = pwv_calib_path_m();
    return path_m / (ptt_ms / 1000.0f);
}

float pwv_calib_apply(float raw_mps)
{
    return (PWV_CALIB_K * raw_mps) + PWV_CALIB_B;
}

void pwv_calib_ref_band(float *mean_mps, float *lo_mps, float *hi_mps)
{
    user_profile_t prof;

    UserProfile_Get(&prof);
    pwv_calib_ref_band_for(&prof, mean_mps, lo_mps, hi_mps);
}

void pwv_calib_ref_band_for(const user_profile_t *prof,
                            float *mean_mps, float *lo_mps, float *hi_mps)
{
    float mean;
    float sd;
    uint8_t age;
    uint8_t gender;

    if(prof != NULL && prof->valid) {
        age = prof->age;
        gender = prof->gender;
    } else {
        age = 25U;
        gender = USER_PROFILE_GENDER_MALE;
    }

    mean = PWV_CALIB_CHEN_A;
    if(gender == USER_PROFILE_GENDER_MALE) {
        mean += PWV_CALIB_CHEN_GENDER_M;
        sd = PWV_CALIB_REF_SD_MALE;
    } else {
        sd = PWV_CALIB_REF_SD_FEMALE;
    }
    mean -= PWV_CALIB_CHEN_AGE_M * (float)age;

    if(mean_mps != NULL) {
        *mean_mps = mean;
    }
    if(lo_mps != NULL) {
        *lo_mps = mean - sd;
    }
    if(hi_mps != NULL) {
        *hi_mps = mean + sd;
    }
}

const char *pwv_calib_trend_hint(float cal_mps, float mean_mps, float lo_mps, float hi_mps)
{
    if(cal_mps <= 0.0f) {
        return "";
    }
    if(cal_mps > hi_mps) {
        return "同龄偏高";
    }
    if(cal_mps < lo_mps) {
        return "同龄偏低";
    }
    (void)mean_mps;
    return "同龄正常";
}
