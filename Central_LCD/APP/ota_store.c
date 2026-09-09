#include "ota_store.h"
#include "ota_flag.h"
#include "ota.h"
#include "flash_layout.h"
#include "flash_store.h"
#include "w25qxx.h"
#include "CH58x_common.h"
#include <string.h>

static ota_header_t s_hdr;
static uint32_t s_bytes_written;
static uint32_t s_running_crc;
static uint8_t s_active;
static uint8_t s_verify_buf[IAP_LEN - 4U];
static uint8_t s_apply_error;

static void ota_reset_to_iap(void)
{
    PFIC_DisableAllIRQ();
    mDelaymS(50);
    SYS_ResetExecute();
    for(;;) {
    }
}

static uint8_t ota_write_header(void)
{
    return FlashStore_Program(FLASH_PART_OTA_HDR_BASE,
                              (const uint8_t *)&s_hdr, sizeof(s_hdr));
}

static uint8_t ota_commit_header(void)
{
    if(FlashStore_EraseSector(FLASH_PART_OTA_HDR_BASE) != 0) {
        return 1;
    }
    return ota_write_header();
}

uint32_t OtaStore_Crc32Accum(uint32_t crc, const uint8_t *data, uint32_t len)
{
    uint32_t i;

    for(i = 0; i < len; i++) {
        uint8_t bit;
        crc ^= data[i];
        for(bit = 0; bit < 8; bit++) {
            crc = (crc & 1u) ? ((crc >> 1) ^ 0xEDB88320u) : (crc >> 1);
        }
    }
    return crc;
}

uint32_t OtaStore_Crc32(const uint8_t *data, uint32_t len)
{
    return OtaStore_Crc32Accum(0xFFFFFFFFu, data, len) ^ 0xFFFFFFFFu;
}

uint8_t OtaStore_ReadHeader(ota_header_t *hdr)
{
    if(hdr == NULL || !FlashStore_IsReady()) {
        return 1;
    }
    if(FlashStore_Read(FLASH_PART_OTA_HDR_BASE, (uint8_t *)hdr, sizeof(*hdr)) != 0) {
        return 1;
    }
    if(hdr->magic != OTA_HDR_MAGIC) {
        memset(hdr, 0, sizeof(*hdr));
        hdr->status = OTA_STATUS_EMPTY;
    }
    return 0;
}

static uint8_t ota_verify_w25q_image(const ota_header_t *hdr)
{
    uint8_t buf[256];
    uint32_t offset = 0;
    uint32_t crc = 0xFFFFFFFFu;

    while(offset < hdr->image_size) {
        uint32_t chunk = hdr->image_size - offset;
        if(chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        if(FlashStore_Read(FLASH_PART_OTA_IMAGE_BASE + offset, buf, chunk) != 0) {
            return 1;
        }
        crc = OtaStore_Crc32Accum(crc, buf, chunk);
        offset += chunk;
    }
    return ((crc ^ 0xFFFFFFFFu) == hdr->image_crc32) ? 0 : 1;
}

static uint8_t ota_copy_w25q_to_app(const ota_header_t *hdr)
{
    __attribute__((aligned(8))) uint8_t buf[1024];
    uint32_t offset = 0;

    PRINT("OTA apply: erase app %lu KB\r\n",
          (unsigned long)(IMAGE_A_SIZE / 1024u));
    if(FLASH_ROM_ERASE(IMAGE_A_START_ADD, IMAGE_A_SIZE) != 0) {
        return 1;
    }

    PRINT("OTA apply: write %lu bytes\r\n", (unsigned long)hdr->image_size);
    while(offset < hdr->image_size) {
        uint32_t chunk = hdr->image_size - offset;
        if(chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        if(FlashStore_Read(FLASH_PART_OTA_IMAGE_BASE + offset, buf, chunk) != 0 ||
           FLASH_ROM_WRITE(IMAGE_A_START_ADD + offset, buf, chunk) != 0) {
            return 1;
        }
        offset += chunk;
    }
    return 0;
}

uint8_t OtaStore_ApplyPendingImage(void)
{
    ota_header_t hdr;

    /* This function must execute in IAP at 0x6D000, never in the App slot. */
    s_apply_error = OTA_APPLY_OK;
    if(!FlashStore_IsReady() || OtaStore_ReadHeader(&hdr) != 0 ||
       hdr.magic != OTA_HDR_MAGIC || hdr.status != OTA_STATUS_READY ||
       hdr.image_size == 0u || hdr.image_size > IMAGE_A_SIZE) {
        s_apply_error = OTA_APPLY_ERR_HEADER;
        return 1;
    }
    if(ota_verify_w25q_image(&hdr) != 0) {
        PRINT("OTA apply: crc fail\r\n");
        s_apply_error = OTA_APPLY_ERR_CRC;
        return 1;
    }
    if(ota_copy_w25q_to_app(&hdr) != 0) {
        PRINT("OTA apply: flash fail\r\n");
        s_apply_error = OTA_APPLY_ERR_FLASH;
        return 1;
    }

    s_hdr = hdr;
    s_hdr.status = OTA_STATUS_APPLIED;
    if(ota_commit_header() != 0 || OtaFlag_Switch(IMAGE_A_FLAG) != 0) {
        s_apply_error = OTA_APPLY_ERR_COMMIT;
        return 1;
    }
    PRINT("OTA apply: OK\r\n");
    return 0;
}

void OtaStore_Init(void)
{
    s_active = 0;
    s_bytes_written = 0;
    s_running_crc = 0xFFFFFFFFu;
    (void)OtaStore_ReadHeader(&s_hdr);
    (void)OtaStore_RecoverBoot();
}

uint8_t OtaStore_RecoverBoot(void)
{
    ota_header_t hdr;

    if(!FlashStore_IsReady()) {
        return 1;
    }
    OtaFlag_Read();
    if(OtaFlag_Get() == IMAGE_IAP_FLAG) {
        /* App was reached despite a pending request; do not loop forever. */
        PRINT("OTA recover: clear IAP flag\r\n");
        if(OtaFlag_Switch(IMAGE_A_FLAG) != 0) {
            return 1;
        }
    }

    if(OtaStore_ReadHeader(&hdr) == 0 &&
       hdr.magic == OTA_HDR_MAGIC && hdr.status == OTA_STATUS_READY) {
        PRINT("OTA recover: invalidate stale READY\r\n");
        s_hdr = hdr;
        s_hdr.status = OTA_STATUS_INVALID;
        return ota_commit_header();
    }
    return 0;
}

uint8_t OtaStore_IsActive(void)
{
    return s_active;
}

uint32_t OtaStore_GetBytesWritten(void)
{
    return s_bytes_written;
}

uint8_t OtaStore_GetLastApplyError(void)
{
    return s_apply_error;
}

uint8_t OtaStore_Begin(uint32_t image_size, uint32_t image_crc32, uint32_t version)
{
    if(!FlashStore_IsReady() || image_size == 0u ||
       image_size > FLASH_PART_OTA_IMAGE_MAX) {
        return 1;
    }

    memset(&s_hdr, 0, sizeof(s_hdr));
    s_hdr.magic = OTA_HDR_MAGIC;
    s_hdr.version = version;
    s_hdr.image_size = image_size;
    s_hdr.image_crc32 = image_crc32;
    s_hdr.status = OTA_STATUS_DOWNLOADING;
    if(ota_commit_header() != 0) {
        return 1;
    }

    s_active = 1;
    s_bytes_written = 0;
    s_running_crc = 0xFFFFFFFFu;
    return 0;
}

uint8_t OtaStore_WriteChunk(uint32_t offset, const uint8_t *data, uint32_t len)
{
    if(!s_active || data == NULL || len == 0u ||
       offset + len > s_hdr.image_size ||
       offset + len > FLASH_PART_OTA_IMAGE_MAX) {
        return 1;
    }

    /*
     * UART ACK can be lost after a page was successfully programmed.  Accept
     * an exact retransmission of already committed bytes without changing the
     * running CRC or byte count.
     */
    if(offset < s_bytes_written) {
        if(offset + len > s_bytes_written || len > sizeof(s_verify_buf) ||
           FlashStore_Read(FLASH_PART_OTA_IMAGE_BASE + offset, s_verify_buf, len) != 0 ||
           memcmp(s_verify_buf, data, len) != 0) {
            return 1;
        }
        return 0;
    }
    if(offset != s_bytes_written) {
        return 1;
    }

    if(FlashStore_Program(FLASH_PART_OTA_IMAGE_BASE + offset, data, len) != 0 ||
       FlashStore_Read(FLASH_PART_OTA_IMAGE_BASE + offset, s_verify_buf, len) != 0 ||
       memcmp(s_verify_buf, data, len) != 0) {
        PRINT("OTA store verify reject off=%lu len=%lu\r\n",
              (unsigned long)offset, (unsigned long)len);
        return 1;
    }
    s_running_crc = OtaStore_Crc32Accum(s_running_crc, data, len);
    s_bytes_written += len;
    return 0;
}

uint8_t OtaStore_ValidateFinish(void)
{
    if(!s_active || s_bytes_written != s_hdr.image_size ||
       (s_running_crc ^ 0xFFFFFFFFu) != s_hdr.image_crc32) {
        s_hdr.status = OTA_STATUS_INVALID;
        (void)ota_commit_header();
        s_active = 0;
        return 1;
    }

    return 0;
}

uint8_t OtaStore_CommitAndReset(void)
{
    s_hdr.status = OTA_STATUS_READY;
    if(ota_commit_header() != 0 || OtaFlag_Switch(IMAGE_IAP_FLAG) != 0) {
        s_active = 0;
        PRINT("OTA finish: commit fail\r\n");
        return 1;
    }
    s_active = 0;
    PRINT("OTA finish: reset to IAP\r\n");
    ota_reset_to_iap();
    return 0;
}

uint8_t OtaStore_Finish(void)
{
    if(OtaStore_ValidateFinish() != 0) {
        return 1;
    }
    return OtaStore_CommitAndReset();
}

uint8_t OtaStore_Abort(void)
{
    if(!s_active) {
        return 0;
    }
    s_hdr.status = OTA_STATUS_INVALID;
    (void)ota_commit_header();
    s_active = 0;
    return 0;
}

uint8_t OtaStore_MarkApplied(void)
{
    if(OtaStore_ReadHeader(&s_hdr) != 0) {
        return 1;
    }
    s_hdr.status = OTA_STATUS_APPLIED;
    return ota_commit_header();
}

#if OTA_STAGE1_TEST

/*
 * The offline self-test performs several seconds of synchronous SPI Flash
 * erase/program work before TMOS starts.  Reload the watchdog in each bounded
 * chunk so an enabled watchdog cannot restart the App mid-transfer.
 */
static void ota_stage1_feed_watchdog(void)
{
    WWDG_SetCounter(0);
}

static uint32_t ota_stage1_image_size(void)
{
    extern uint8_t _data_lma[];
    extern uint8_t _data_vma[];
    extern uint8_t _edata[];
    uint32_t flash_len = (uint32_t)((uintptr_t)_data_lma - IMAGE_A_START_ADD);
    uint32_t data_len = (uint32_t)(uintptr_t)(_edata - _data_vma);

    return flash_len + data_len;
}

static uint8_t ota_stage1_erase_image(uint32_t image_size)
{
    uint32_t addr = FLASH_PART_OTA_IMAGE_BASE;
    uint32_t end = addr + ((image_size + W25Q_SECTOR_SIZE - 1u) &
                          ~(W25Q_SECTOR_SIZE - 1u));

    while(addr < end) {
        ota_stage1_feed_watchdog();
        if(FlashStore_EraseSector(addr) != 0) {
            return 1;
        }
        addr += W25Q_SECTOR_SIZE;
    }
    return 0;
}

uint8_t OtaStore_Stage1SelfTest(void)
{
    ota_header_t hdr;
    uint32_t image_size;
    uint32_t offset;
    uint32_t crc = 0xFFFFFFFFu;
    uint8_t buf[256];

    if(!FlashStore_IsReady()) {
        PRINT("OTA S1: W25Q not ready\r\n");
        return 1;
    }
    if(OtaStore_ReadHeader(&hdr) == 0 && hdr.status == OTA_STATUS_APPLIED) {
        PRINT("OTA S1: skip (applied)\r\n");
        return 0;
    }

    image_size = ota_stage1_image_size();
    if(image_size == 0u || image_size > FLASH_PART_OTA_IMAGE_MAX) {
        PRINT("OTA S1: bad size %lu\r\n", (unsigned long)image_size);
        return 1;
    }
    for(offset = 0; offset < image_size;) {
        uint32_t chunk = image_size - offset;
        if(chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        memcpy(buf, (const void *)(uintptr_t)(IMAGE_A_START_ADD + offset), chunk);
        crc = OtaStore_Crc32Accum(crc, buf, chunk);
        offset += chunk;
        ota_stage1_feed_watchdog();
    }
    crc ^= 0xFFFFFFFFu;

    PRINT("OTA S1: copy App->W25Q size=%lu crc=0x%08lX\r\n",
          (unsigned long)image_size, (unsigned long)crc);
    if(OtaStore_Begin(image_size, crc, 0x00010001u) != 0 ||
       ota_stage1_erase_image(image_size) != 0) {
        (void)OtaStore_Abort();
        return 1;
    }
    for(offset = 0; offset < image_size;) {
        uint32_t chunk = image_size - offset;
        if(chunk > sizeof(buf)) {
            chunk = sizeof(buf);
        }
        memcpy(buf, (const void *)(uintptr_t)(IMAGE_A_START_ADD + offset), chunk);
        if(OtaStore_WriteChunk(offset, buf, chunk) != 0) {
            (void)OtaStore_Abort();
            return 1;
        }
        offset += chunk;
        ota_stage1_feed_watchdog();
        if((offset & 0x7FFFu) == 0u || offset == image_size) {
            PRINT("OTA S1: write %lu/%lu\r\n",
                  (unsigned long)offset, (unsigned long)image_size);
        }
    }
    PRINT("OTA S1: finish -> IAP reset\r\n");
    return OtaStore_Finish();
}

#endif /* OTA_STAGE1_TEST */
