#ifndef USER_PROFILE_H
#define USER_PROFILE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USER_PROFILE_GENDER_FEMALE    0U
#define USER_PROFILE_GENDER_MALE        1U
#define USER_PROFILE_HAND_AUTO          0U

#define USER_PROFILE_AGE_MIN            18U
#define USER_PROFILE_AGE_MAX            90U
#define USER_PROFILE_HEIGHT_MIN_CM      120U
#define USER_PROFILE_HEIGHT_MAX_CM      220U
#define USER_PROFILE_HAND_MIN_CM        14U
#define USER_PROFILE_HAND_MAX_CM        28U

typedef struct
{
    uint8_t  age;
    uint8_t  gender;
    uint16_t height_cm;
    uint16_t hand_cm;
    uint8_t  valid;
} user_profile_t;

uint8_t UserProfile_Init(void);
uint8_t UserProfile_IsReady(void);
void    UserProfile_Get(user_profile_t *out);
uint8_t UserProfile_Set(const user_profile_t *in);
uint8_t UserProfile_Save(void);
uint8_t UserProfile_IsComplete(void);
uint16_t UserProfile_EffectiveHandCm(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_PROFILE_H */
