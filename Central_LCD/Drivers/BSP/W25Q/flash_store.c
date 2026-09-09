#include "flash_store.h"
#include "flash_layout.h"
#include "w25qxx.h"
#include "CH58x_common.h"
#include "stddef.h"

static uint8_t s_flash_ready;

uint8_t FlashStore_Init(void)
{
    s_flash_ready = 1;
    return 0;
}

uint8_t FlashStore_IsReady(void)
{
    return s_flash_ready;
}

static uint8_t flash_addr_valid(uint32_t abs_addr, uint32_t len)
{
    if(len == 0) {
        return 0;
    }
    if(abs_addr >= W25Q64_CAPACITY_BYTES) {
        return 0;
    }
    if((abs_addr + len) > W25Q64_CAPACITY_BYTES) {
        return 0;
    }
    return 1;
}

uint8_t FlashStore_Read(uint32_t abs_addr, uint8_t *buf, uint32_t len)
{
    if(!s_flash_ready || buf == NULL) {
        return 1;
    }
    if(!flash_addr_valid(abs_addr, len)) {
        return 1;
    }
    W25Q_Read(abs_addr, buf, len);
    return 0;
}

uint8_t FlashStore_Program(uint32_t abs_addr, const uint8_t *buf, uint32_t len)
{
    uint32_t offset = 0;

    if(!s_flash_ready || buf == NULL || len == 0) {
        return 1;
    }
    if(!flash_addr_valid(abs_addr, len)) {
        return 1;
    }

    while(offset < len) {
        uint16_t chunk = (uint16_t)((len - offset) > W25Q_PAGE_SIZE ?
                                    W25Q_PAGE_SIZE : (len - offset));
        uint32_t page_remain = W25Q_PAGE_SIZE - (abs_addr % W25Q_PAGE_SIZE);

        if(chunk > page_remain) {
            chunk = (uint16_t)page_remain;
        }
        W25Q_PageProgram(abs_addr, buf + offset, chunk);
        WWDG_SetCounter(0);
        abs_addr += chunk;
        offset += chunk;
    }
    return 0;
}

uint8_t FlashStore_EraseSector(uint32_t abs_addr)
{
    if(!s_flash_ready) {
        return 1;
    }
    if((abs_addr % W25Q_SECTOR_SIZE) != 0) {
        return 1;
    }
    if(!flash_addr_valid(abs_addr, 1)) {
        return 1;
    }
    WWDG_SetCounter(0);
    W25Q_SectorErase4K(abs_addr);
    WWDG_SetCounter(0);
    return 0;
}
