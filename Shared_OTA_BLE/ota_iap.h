#ifndef OTA_IAP_H
#define OTA_IAP_H

#include <stdint.h>

#define OTA_FLASH_ERASE_EVT   0x0040u

void OtaIap_Init(uint8_t task_id);
uint16_t OtaIap_ProcessEvent(uint16_t events);
void OtaIap_AddGattService(void);
void OtaIap_RegisterProfileCallbacks(void);
void OtaIap_FeedFrame(void);

#endif /* OTA_IAP_H */
