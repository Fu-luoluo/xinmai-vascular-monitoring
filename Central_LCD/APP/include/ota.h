#ifndef __OTA_H
#define __OTA_H

#include "CONFIG.h"

/* CodeFlash: Jump 4KB + Application 432KB + IAP 12KB. */
#define FLASH_BLOCK_SIZE       EEPROM_BLOCK_SIZE
#define IMAGE_A_FLAG           0x01u
#define IMAGE_B_FLAG           0x02u
#define IMAGE_IAP_FLAG         0x03u

#define IMAGE_A_START_ADD      0x1000u
#define IMAGE_A_SIZE           (432u * 1024u)
#define IMAGE_IAP_START_ADD    0x6D000u
#define IMAGE_IAP_SIZE         (12u * 1024u)

#define CMD_IAP_PROM           0x80u
#define CMD_IAP_ERASE          0x81u
#define CMD_IAP_VERIFY         0x82u
#define CMD_IAP_END            0x83u
#define CMD_IAP_INFO           0x84u
#define CMD_IAP_ABORT          0x85u

/* UART1 OTA data-plane frame limit: command + len + address + payload. */
#define IAP_LEN                247u

/*
 * DataFlash offset 0x7000 is reserved for the boot flag.
 * The application project must define BLE_SNV_ADDR=0x7100.
 */
#define OTA_DATAFLASH_ADD      (0x00077000u - FLASH_ROM_MAX_SIZE)

typedef struct
{
    unsigned char ImageFlag;
    unsigned char Revd[3];
} OTADataFlashInfo_t;

typedef union
{
    struct
    {
        uint8_t cmd;
        uint8_t len;
        uint8_t addr[2];
        uint8_t block_num[2];
    } erase;
    struct
    {
        uint8_t cmd;
        uint8_t len;
        uint8_t addr[2];
        uint8_t buf[IAP_LEN - 4u];
    } program;
    struct
    {
        uint8_t buf[IAP_LEN];
    } other;
} OTA_IAP_CMD_t;

extern unsigned char CurrImageFlag;

#endif /* __OTA_H */
