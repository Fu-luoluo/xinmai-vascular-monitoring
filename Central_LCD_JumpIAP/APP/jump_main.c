#include <stdint.h>
#include "CH58x_common.h"

/*
 * Reset-context boot selector.  EEPROM_READ is required here because the
 * DataFlash memory mapping can retain stale data after an OTA flag update.
 */

#define IMAGE_IAP_FLAG         0x03u
#define IMAGE_A_START_ADD      0x1000u
#define IMAGE_IAP_START_ADD    0x6D000u
#define OTA_DATAFLASH_ADD      (0x00077000u - FLASH_ROM_MAX_SIZE)

typedef struct
{
    uint8_t ImageFlag;
    uint8_t Revd[3];
} OTADataFlashInfo_t;

/*
 * Called by the custom reset startup before it switches to user mode.  The
 * returned target is entered while still in reset/machine context, so each
 * application startup sees the same CPU state as a power-on reset.
 */
uint32_t JumpIAP_SelectTarget(void)
{
    OTADataFlashInfo_t info;

    FLASH_ROM_PWR_UP();
    EEPROM_READ(OTA_DATAFLASH_ADD, &info, 4);

    return (info.ImageFlag == IMAGE_IAP_FLAG) ?
           IMAGE_IAP_START_ADD : IMAGE_A_START_ADD;
}
