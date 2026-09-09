#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H

#include "w25qxx.h"

/* W25Q64 8MB flat map (absolute addresses) */
#define FLASH_PART_OTA_BASE      0x000000u
#define FLASH_PART_OTA_SIZE      0x00200000u  /* 2MB: OTA staging */

#define FLASH_PART_OTA_HDR_BASE   FLASH_PART_OTA_BASE
#define FLASH_PART_OTA_HDR_SIZE   0x00001000u  /* OTA metadata sector */
#define FLASH_PART_OTA_IMAGE_BASE (FLASH_PART_OTA_BASE + FLASH_PART_OTA_HDR_SIZE)
#define FLASH_PART_OTA_IMAGE_MAX  0x0006C000u  /* 432KB: CodeFlash App slot */

#define FLASH_PART_FONT_BASE     0x00200000u
#define FLASH_PART_FONT_SIZE     0x00300000u  /* 3MB: LVGL fonts/assets */

#define FLASH_PART_HIST_BASE     0x00500000u
#define FLASH_PART_HIST_SIZE     0x00100000u  /* 1MB: measurement history ring */

#define FLASH_PART_RSVD_BASE     0x00600000u
#define FLASH_PART_RSVD_SIZE     0x00200000u  /* 2MB: reserved / debug selftest */

#define FLASH_SELFTEST_ADDR      (FLASH_PART_RSVD_BASE + FLASH_PART_RSVD_SIZE - W25Q_SECTOR_SIZE)

#endif /* FLASH_LAYOUT_H */
