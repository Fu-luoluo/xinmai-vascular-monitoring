#ifndef __MEASURE_CTRL_H
#define __MEASURE_CTRL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* exp18 R4-A 峰检已冻结；产品模式 90s 到时自动结束。PULSE_TARGET=0 表示不限拍数。 */
#define FIRMWARE_PEAK_BASELINE          "exp18-r4a"
#define MEASURE_SESSION_SEC             90
#define MEASURE_SESSION_PULSE_TARGET    0U
/* 脱落：grace 内不判；之后单端无 notify 持续 LOST_SEC 判 stale（已收过 notify）
 * 或自 session 起 FIRST_NOTIFY_SEC 仍无首包。WEAK 在 grace 后、无 PWV 时提前预警。 */
#define MEASURE_SIGNAL_LOST_SEC         18
#define MEASURE_SIGNAL_GRACE_SEC        6
#define MEASURE_SIGNAL_FIRST_NOTIFY_SEC 20
#define MEASURE_SIGNAL_WEAK_SEC         12

#define MEASURE_SIDE_FINGER        0
#define MEASURE_SIDE_WRIST         1

typedef enum
{
    MEASURE_END_TIMEOUT = 0,
    MEASURE_END_FIRST_PWV,
    MEASURE_END_SIGNAL_LOST,
    MEASURE_END_USER_STOP,
    MEASURE_END_PULSE_TARGET
} measureEndReason_t;

void Measure_Init(void);
void Measure_Start(void);
void Measure_Stop(void);
void Measure_OnPause(void);
void Measure_OnResume(void);
void Measure_NotifyLinkLost(void);
void Measure_OnSample(uint8_t side, int hr, int spo2);
uint8_t Measure_IsActive(void);
uint8_t Measure_IsResultHold(void);
uint8_t Measure_CanStart(void);
uint8_t Measure_PreflightOk(void);
void Measure_OnPulseTargetReached(void);

#ifdef __cplusplus
}
#endif

#endif /* __MEASURE_CTRL_H */
