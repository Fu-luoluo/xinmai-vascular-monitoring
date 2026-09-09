#ifndef FLASH_STORE_H
#define FLASH_STORE_H

#include <stdint.h>

uint8_t FlashStore_Init(void);
uint8_t FlashStore_Ensure(void);
uint8_t FlashStore_IsReady(void);
uint8_t FlashStore_Read(uint32_t abs_addr, uint8_t *buf, uint32_t len);
uint8_t FlashStore_Program(uint32_t abs_addr, const uint8_t *buf, uint32_t len);
uint8_t FlashStore_EraseSector(uint32_t abs_addr);

#endif /* FLASH_STORE_H */
