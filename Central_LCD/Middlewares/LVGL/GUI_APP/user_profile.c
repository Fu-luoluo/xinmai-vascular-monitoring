#include "user_profile.h"
#include "flash_layout.h"
#include "flash_store.h"
#include "CONFIG.h"
#include <string.h>

#define USER_PROFILE_MAGIC      0x55505246u  /* 'UPRF' */
#define USER_PROFILE_VERSION    1u
#define USER_PROFILE_REF_HAND_CM 19U

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    user_profile_t profile;
    uint16_t crc;
} user_profile_blob_t;

static user_profile_t s_profile;
static uint8_t        s_ready;

static uint16_t user_profile_crc(const user_profile_blob_t *blob)
{
    const uint8_t *p = (const uint8_t *)blob;
    uint32_t       i;
    uint16_t       c = 0U;

    for(i = 0; i < sizeof(*blob) - sizeof(blob->crc); i++) {
        c = (uint16_t)(c + p[i]);
    }
    return c;
}

static void user_profile_set_defaults(void)
{
    memset(&s_profile, 0, sizeof(s_profile));
    s_profile.age = 25U;
    s_profile.gender = USER_PROFILE_GENDER_MALE;
    s_profile.height_cm = 170U;
    s_profile.hand_cm = USER_PROFILE_HAND_AUTO;
    s_profile.valid = 0U;
}

static uint8_t user_profile_fields_ok(const user_profile_t *p)
{
    if(p == NULL || !p->valid) {
        return 0U;
    }
    if(p->age < USER_PROFILE_AGE_MIN || p->age > USER_PROFILE_AGE_MAX) {
        return 0U;
    }
    if(p->gender > USER_PROFILE_GENDER_MALE) {
        return 0U;
    }
    if(p->height_cm < USER_PROFILE_HEIGHT_MIN_CM ||
       p->height_cm > USER_PROFILE_HEIGHT_MAX_CM) {
        return 0U;
    }
    if(p->hand_cm != USER_PROFILE_HAND_AUTO &&
       (p->hand_cm < USER_PROFILE_HAND_MIN_CM ||
        p->hand_cm > USER_PROFILE_HAND_MAX_CM)) {
        return 0U;
    }
    return 1U;
}

uint8_t UserProfile_IsComplete(void)
{
    return user_profile_fields_ok(&s_profile);
}

uint16_t UserProfile_EffectiveHandCm(void)
{
    uint32_t est;

    if(s_profile.hand_cm >= USER_PROFILE_HAND_MIN_CM &&
       s_profile.hand_cm <= USER_PROFILE_HAND_MAX_CM) {
        return s_profile.hand_cm;
    }
    if(s_profile.height_cm >= USER_PROFILE_HEIGHT_MIN_CM &&
       s_profile.height_cm <= USER_PROFILE_HEIGHT_MAX_CM) {
        est = ((uint32_t)s_profile.height_cm * 108U) / 1000U;
        if(est < USER_PROFILE_HAND_MIN_CM) {
            est = USER_PROFILE_HAND_MIN_CM;
        }
        if(est > USER_PROFILE_HAND_MAX_CM) {
            est = USER_PROFILE_HAND_MAX_CM;
        }
        return (uint16_t)est;
    }
    return USER_PROFILE_REF_HAND_CM;
}

uint8_t UserProfile_Init(void)
{
    user_profile_blob_t blob;

    s_ready = 0U;
    user_profile_set_defaults();

    if(!FlashStore_IsReady()) {
        return 1U;
    }

    if(FlashStore_Read(FLASH_PART_RSVD_BASE, (uint8_t *)&blob, sizeof(blob)) != 0) {
        return 1U;
    }

    if(blob.magic == USER_PROFILE_MAGIC &&
       blob.version == USER_PROFILE_VERSION &&
       blob.crc == user_profile_crc(&blob)) {
        user_profile_t loaded = blob.profile;

        if(user_profile_fields_ok(&loaded)) {
            s_profile = loaded;
        }
    }

    s_ready = 1U;
    PRINT("UserProfile: age=%u gender=%u h=%u hand=%u valid=%u\r\n",
          (unsigned)s_profile.age,
          (unsigned)s_profile.gender,
          (unsigned)s_profile.height_cm,
          (unsigned)s_profile.hand_cm,
          (unsigned)s_profile.valid);
    return 0U;
}

uint8_t UserProfile_IsReady(void)
{
    return s_ready;
}

void UserProfile_Get(user_profile_t *out)
{
    if(out != NULL) {
        *out = s_profile;
    }
}

uint8_t UserProfile_Set(const user_profile_t *in)
{
    if(in == NULL) {
        return 0U;
    }
    s_profile = *in;
    s_profile.valid = 1U;
    return UserProfile_IsComplete();
}

uint8_t UserProfile_Save(void)
{
    user_profile_blob_t blob;

    if(!s_ready || !UserProfile_IsComplete()) {
        return 0U;
    }

    memset(&blob, 0, sizeof(blob));
    blob.magic = USER_PROFILE_MAGIC;
    blob.version = USER_PROFILE_VERSION;
    blob.profile = s_profile;
    blob.crc = user_profile_crc(&blob);

    if(FlashStore_EraseSector(FLASH_PART_RSVD_BASE) != 0) {
        return 0U;
    }
    if(FlashStore_Program(FLASH_PART_RSVD_BASE, (const uint8_t *)&blob, sizeof(blob)) != 0) {
        return 0U;
    }
    return 1U;
}
