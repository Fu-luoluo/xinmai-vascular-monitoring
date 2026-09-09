#ifndef __VOICE_MGR_H
#define __VOICE_MGR_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VOICE_FOLDER            0x01U
#define VOICE_DEFAULT_VOLUME    18U

#define VOICE_TRACK_WELCOME         1U
#define VOICE_TRACK_SCANNING        2U
#define VOICE_TRACK_WRIST_LINK      3U
#define VOICE_TRACK_FINGER_LINK     4U
#define VOICE_TRACK_BOTH_LINK       5U
#define VOICE_TRACK_WEAR_HINT       6U
#define VOICE_TRACK_MEAS_START      7U
#define VOICE_TRACK_MEAS_PROGRESS   8U
#define VOICE_TRACK_MEAS_NORMAL     9U
#define VOICE_TRACK_BLE_LOST        10U
#define VOICE_TRACK_SIGNAL_WEAK     11U
#define VOICE_TRACK_FINGER_NOT_RDY  12U
#define VOICE_TRACK_MEAS_DONE       13U
#define VOICE_TRACK_REWEAR          14U
#define VOICE_TRACK_INIT            15U
#define VOICE_TRACK_MAX             VOICE_TRACK_INIT

void Voice_Init(void);
void Voice_BootNow(void);
void Voice_SetVolume(uint8_t level);
void Voice_Stop(void);
void Voice_PlayFolderFile(uint8_t folder, uint8_t file);
void Voice_PlayFolderTrack(uint8_t track);
void Voice_PlayDirect(uint8_t track);
void Voice_PlayAfterBuzzer(uint8_t track);
void Voice_PlayStartupTest(void);
void Voice_ScheduleWelcome(uint16_t delay_ms);
uint8_t Voice_IsBusy(void);

#ifdef __cplusplus
}
#endif

#endif /* __VOICE_MGR_H */
