#ifndef __OTA_STORE_H
#define __OTA_STORE_H

#include <stdint.h>

/*
 * Set to 1 only for the offline stage-1 verification build.  The App then
 * copies its own image to W25Q and requests IAP on the next reset.
 */
#ifndef OTA_STAGE1_TEST
#define OTA_STAGE1_TEST  0
#endif

#define OTA_HDR_MAGIC   0x4F544131u  /* "OTA1" */

typedef enum
{
    OTA_STATUS_EMPTY = 0,
    OTA_STATUS_DOWNLOADING,
    OTA_STATUS_READY,
    OTA_STATUS_APPLIED,
    OTA_STATUS_INVALID
} ota_status_t;

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t image_size;
    uint32_t image_crc32;
    uint8_t  status;
    uint8_t  reserved[3];
} ota_header_t;

typedef enum
{
    OTA_APPLY_OK = 0,
    OTA_APPLY_ERR_HEADER,
    OTA_APPLY_ERR_CRC,
    OTA_APPLY_ERR_FLASH,
    OTA_APPLY_ERR_COMMIT
} ota_apply_result_t;

void     OtaStore_Init(void);
uint8_t  OtaStore_RecoverBoot(void);
uint8_t  OtaStore_IsActive(void);
uint32_t OtaStore_GetBytesWritten(void);
uint8_t  OtaStore_ReadHeader(ota_header_t *hdr);
uint8_t  OtaStore_Begin(uint32_t image_size, uint32_t image_crc32, uint32_t version);
uint8_t  OtaStore_WriteChunk(uint32_t offset, const uint8_t *data, uint32_t len);
uint8_t  OtaStore_ValidateFinish(void);
uint8_t  OtaStore_CommitAndReset(void);
uint8_t  OtaStore_Finish(void);
uint8_t  OtaStore_Abort(void);
uint8_t  OtaStore_MarkApplied(void);
uint8_t  OtaStore_ApplyPendingImage(void);
uint8_t  OtaStore_GetLastApplyError(void);
uint32_t OtaStore_Crc32(const uint8_t *data, uint32_t len);
uint32_t OtaStore_Crc32Accum(uint32_t crc, const uint8_t *data, uint32_t len);

#if OTA_STAGE1_TEST
uint8_t OtaStore_Stage1SelfTest(void);
#endif

#endif /* __OTA_STORE_H */
