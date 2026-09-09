#ifndef W25QXX_H
#define W25QXX_H

#include <stdint.h>

#define W25Q64_JEDEC_ID      0xEF4017u
#define W25Q64_CAPACITY_BYTES 0x00800000u
#define W25Q_PAGE_SIZE       256u
#define W25Q_SECTOR_SIZE     4096u

uint8_t  W25Q_Init(void);
uint8_t  W25Q_IsSupportedJEDECID(uint32_t id);
uint32_t W25Q_GetJEDECID(void);
uint32_t W25Q_ReadJEDECID(void);
void     W25Q_Read(uint32_t addr, uint8_t *buf, uint32_t len);
uint8_t  W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint16_t len);
uint8_t  W25Q_SectorErase4K(uint32_t addr);
uint8_t  W25Q_WaitBusy(void);

#endif /* W25QXX_H */
