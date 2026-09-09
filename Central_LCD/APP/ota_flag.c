#include "ota_flag.h"
#include "ota.h"
#include "CH58x_common.h"

#define IMAGE_FLAG_MMAP_ADD    (DATA_FLASH_ADDR + OTA_DATAFLASH_ADD)

unsigned char CurrImageFlag = 0xFF;

static __attribute__((aligned(8))) uint8_t s_block_buf[16];

void OtaFlag_Read(void)
{
    OTADataFlashInfo_t info;

    FLASH_ROM_PWR_UP();
    EEPROM_READ(OTA_DATAFLASH_ADD, &info, sizeof(info));
    CurrImageFlag = info.ImageFlag;

    if(CurrImageFlag != IMAGE_A_FLAG &&
       CurrImageFlag != IMAGE_B_FLAG &&
       CurrImageFlag != IMAGE_IAP_FLAG) {
        CurrImageFlag = IMAGE_A_FLAG;
    }
}

uint8_t OtaFlag_Switch(uint8_t new_flag)
{
    OTADataFlashInfo_t info;
    uint32_t ret;

    FLASH_ROM_PWR_UP();
    ret = EEPROM_ERASE(OTA_DATAFLASH_ADD, EEPROM_PAGE_SIZE);
    if(ret != 0) {
        PRINT("OTA flag erase fail %lu\r\n", (unsigned long)ret);
        return 1;
    }

    s_block_buf[0] = new_flag;
    s_block_buf[1] = 0;
    s_block_buf[2] = 0;
    s_block_buf[3] = 0;

    ret = EEPROM_WRITE(OTA_DATAFLASH_ADD, (uint32_t *)&s_block_buf[0], sizeof(info));
    if(ret != 0) {
        PRINT("OTA flag program fail %lu\r\n", (unsigned long)ret);
        return 2;
    }

    FLASH_ROM_PWR_UP();
    FLASH_ROM_SW_RESET();
    EEPROM_READ(OTA_DATAFLASH_ADD, &info, sizeof(info));
    if(info.ImageFlag != new_flag) {
        PRINT("OTA flag verify fail eeprom=0x%02X\r\n", (unsigned)info.ImageFlag);
        return 3;
    }

    if(*(volatile uint8_t *)(uintptr_t)IMAGE_FLAG_MMAP_ADD != new_flag) {
        PRINT("OTA flag mmap lag 0x%02X\r\n",
              (unsigned)*(volatile uint8_t *)(uintptr_t)IMAGE_FLAG_MMAP_ADD);
    }

    CurrImageFlag = new_flag;
    return 0;
}

uint8_t OtaFlag_Get(void)
{
    return CurrImageFlag;
}
