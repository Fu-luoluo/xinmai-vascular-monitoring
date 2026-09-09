#ifndef __POWER_MGR_H
#define __POWER_MGR_H

#include "tft_port.h"

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_IDLE_TIMEOUT_SEC   60

void PowerMgr_Init(void);
void PowerMgr_OnUserActivity(void);
void PowerMgr_TouchWakeRearm(void);
uint8_t PowerMgr_IsDisplayAwake(void);

#ifdef __cplusplus
}
#endif

#endif /* __POWER_MGR_H */
