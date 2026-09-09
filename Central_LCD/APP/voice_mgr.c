#include "voice_mgr.h"
#include "bsp_voice.h"
#include "CONFIG.h"

#define VOICE_EVT_BUSY           0x0001
#define VOICE_EVT_WELCOME        0x0004

#define VOICE_DEDUP_MS           5000U
#define VOICE_BUSY_DEFAULT_MS    3500U
#define VOICE_CMD_GAP_MS         20U
#define VOICE_FRAME_LEN          8U

static uint8_t  s_voiceTaskId = TASK_NO_TASK;
static uint8_t  s_busy = 0U;
static uint8_t  s_tfReady = 0U;
static uint32_t s_lastPlayTick[VOICE_TRACK_MAX + 1U];
static uint8_t  s_welcomeScheduled = 0U;

static uint16_t Voice_ProcessEvent(uint8_t task_id, uint16_t events);

static void voice_send_frame(uint8_t cmd, uint8_t para1, uint8_t para2)
{
    uint8_t frame[VOICE_FRAME_LEN];

    /* MH3028M/Catalex 手册：8 字节帧，无校验和，以 0xEF 结尾 */
    frame[0] = 0x7EU;
    frame[1] = 0xFFU;
    frame[2] = 0x06U;
    frame[3] = cmd;
    frame[4] = 0x00U;
    frame[5] = para1;
    frame[6] = para2;
    frame[7] = 0xEFU;
    mDelaymS(VOICE_CMD_GAP_MS);
    BSP_VoiceUart_Send(frame, VOICE_FRAME_LEN);
}

static void voice_mark_busy(uint32_t ms)
{
    s_busy = 1U;
    if(s_voiceTaskId != TASK_NO_TASK) {
        tmos_start_task(s_voiceTaskId, VOICE_EVT_BUSY, MS1_TO_SYSTEM_TIME(ms));
    }
}

static uint8_t voice_dedup_ok(uint8_t track)
{
    uint32_t now;

    if(track == 0U || track > VOICE_TRACK_MAX) {
        return 0U;
    }
    now = TMOS_GetSystemClock();
    /* 0 表示从未播放；勿用 (now-0)<DEDUP 在上电数秒内误拦欢迎语 */
    if(s_lastPlayTick[track] != 0U) {
        if((now - s_lastPlayTick[track]) < MS1_TO_SYSTEM_TIME(VOICE_DEDUP_MS)) {
            return 0U;
        }
    }
    s_lastPlayTick[track] = (now == 0U) ? 1U : now;
    return 1U;
}

static void Voice_SelectTF(void)
{
    voice_send_frame(0x09U, 0x00U, 0x02U);
}

void Voice_SetVolume(uint8_t level)
{
    if(level > 30U) {
        level = 30U;
    }
    voice_send_frame(0x06U, 0x00U, level);
}

static void voice_boot_sequence(void)
{
    PRINT("[VOICE] select TF card\r\n");
    Voice_SelectTF();
    mDelaymS(150);
    PRINT("[VOICE] set volume=%u\r\n", (unsigned)VOICE_DEFAULT_VOLUME);
    Voice_SetVolume(VOICE_DEFAULT_VOLUME);
    mDelaymS(100);
    s_tfReady = 1U;
}

void Voice_Stop(void)
{
    voice_send_frame(0x16U, 0x00U, 0x00U);
    s_busy = 0U;
    if(s_voiceTaskId != TASK_NO_TASK) {
        tmos_stop_task(s_voiceTaskId, VOICE_EVT_BUSY);
    }
}

void Voice_PlayFolderFile(uint8_t folder, uint8_t file)
{
    if(folder == 0U || file == 0U) {
        PRINT("[VOICE] play rejected: folder=%u file=%u\r\n",
              (unsigned)folder, (unsigned)file);
        return;
    }
    if(!s_tfReady) {
        voice_boot_sequence();
    }
    PRINT("[VOICE] play /%02u/%03u.mp3\r\n",
          (unsigned)folder, (unsigned)file);
    voice_send_frame(0x0FU, folder, file);
    voice_mark_busy(VOICE_BUSY_DEFAULT_MS);
}

void Voice_PlayFolderTrack(uint8_t track)
{
    if(track == 0U || track > VOICE_TRACK_MAX) {
        return;
    }
    Voice_PlayFolderFile(VOICE_FOLDER, track);
}

void Voice_PlayDirect(uint8_t track)
{
    if(!voice_dedup_ok(track)) {
        return;
    }
    Voice_PlayFolderTrack(track);
}

void Voice_PlayAfterBuzzer(uint8_t track)
{
    if(track == 0U) {
        return;
    }
    Voice_PlayFolderTrack(track);
}

void Voice_ScheduleWelcome(uint16_t delay_ms)
{
    if(s_welcomeScheduled || s_voiceTaskId == TASK_NO_TASK) {
        return;
    }
    s_welcomeScheduled = 1U;
    tmos_start_task(s_voiceTaskId, VOICE_EVT_WELCOME, MS1_TO_SYSTEM_TIME(delay_ms));
}

uint8_t Voice_IsBusy(void)
{
    return s_busy;
}

void Voice_Init(void)
{
    uint8_t i;

    for(i = 0; i <= VOICE_TRACK_MAX; i++) {
        s_lastPlayTick[i] = 0U;
    }
    s_busy = 0U;
    s_tfReady = 0U;
    s_welcomeScheduled = 0U;

    BSP_VoiceUart_Init();
    s_voiceTaskId = TMOS_ProcessEventRegister(Voice_ProcessEvent);
}

void Voice_PlayStartupTest(void)
{
    /* 上电欢迎：/01/001.mp3「欢迎使用脉搏波…」；先等模块/TF 就绪再播 */
    PRINT("[VOICE] startup welcome: wait MP3/TF then play track %u\r\n",
          (unsigned)VOICE_TRACK_WELCOME);
    mDelaymS(1200);
    if(!s_tfReady) {
        voice_boot_sequence();
    }
    /* 启动欢迎跳过去重，避免上电时钟未满 DEDUP 窗口时被静默丢弃 */
    Voice_PlayFolderTrack(VOICE_TRACK_WELCOME);
    {
        uint32_t now = TMOS_GetSystemClock();
        s_lastPlayTick[VOICE_TRACK_WELCOME] = (now == 0U) ? 1U : now;
    }
}

void Voice_BootNow(void)
{
    if(s_tfReady) {
        return;
    }
    mDelaymS(500);
    voice_boot_sequence();
    Voice_PlayDirect(VOICE_TRACK_INIT);
}

static uint16_t Voice_ProcessEvent(uint8_t task_id, uint16_t events)
{
    (void)task_id;

    if(events & VOICE_EVT_BUSY) {
        s_busy = 0U;
        events ^= VOICE_EVT_BUSY;
    }

    if(events & VOICE_EVT_WELCOME) {
        Voice_PlayDirect(VOICE_TRACK_WELCOME);
        events ^= VOICE_EVT_WELCOME;
    }

    return events;
}
