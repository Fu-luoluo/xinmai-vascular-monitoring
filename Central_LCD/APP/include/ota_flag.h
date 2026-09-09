#ifndef __OTA_FLAG_H
#define __OTA_FLAG_H

#include <stdint.h>

void    OtaFlag_Read(void);
uint8_t OtaFlag_Switch(uint8_t new_flag);
uint8_t OtaFlag_Get(void);

#endif /* __OTA_FLAG_H */
