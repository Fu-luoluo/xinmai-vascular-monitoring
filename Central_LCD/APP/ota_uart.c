#include "ota_uart.h"
#include "ota.h"
#include "ota_store.h"
#include "flash_layout.h"
#include "flash_store.h"
#include "w25qxx.h"

static OTA_IAP_CMD_t s_iap;
static uint16_t      s_rx_need;
static uint16_t      s_rx_len;
static uint8_t       s_error;
static uint8_t       s_end_received;
static uint8_t       s_abort_received;
static uint8_t       s_frame_done;
static uint8_t       s_frame_ok;

void OtaUart_Reset(void)
{
    s_rx_need = 0U;
    s_rx_len = 0U;
    s_error = 0U;
    s_end_received = 0U;
    s_abort_received = 0U;
    s_frame_done = 0U;
    s_frame_ok = 0U;
}

uint8_t OtaUart_HasError(void)
{
    return s_error;
}

uint8_t OtaUart_IsFrameIdle(void)
{
    return (s_rx_len == 0U) ? 1U : 0U;
}

uint8_t OtaUart_HasEnd(void)
{
    return s_end_received;
}

uint8_t OtaUart_HasAbort(void)
{
    return s_abort_received;
}

uint8_t OtaUart_TakeFrameResult(uint8_t *ok)
{
    if(!s_frame_done) {
        return 0U;
    }
    if(ok != 0) {
        *ok = s_frame_ok;
    }
    s_frame_done = 0U;
    return 1U;
}

static uint8_t ota_uart_erase(uint32_t offset, uint32_t sectors)
{
    uint32_t i;
    uint32_t erase_len;

    if((offset % W25Q_SECTOR_SIZE) != 0U || sectors == 0U) {
        return 1U;
    }
    erase_len = sectors * W25Q_SECTOR_SIZE;
    if(sectors > (FLASH_PART_OTA_IMAGE_MAX / W25Q_SECTOR_SIZE) ||
       offset > FLASH_PART_OTA_IMAGE_MAX ||
       erase_len > (FLASH_PART_OTA_IMAGE_MAX - offset)) {
        return 1U;
    }

    for(i = 0U; i < sectors; i++) {
        if(FlashStore_EraseSector(FLASH_PART_OTA_IMAGE_BASE + offset +
                                  (i * W25Q_SECTOR_SIZE)) != 0) {
            return 1U;
        }
    }
    return 0U;
}

static uint8_t ota_uart_dispatch(void)
{
    uint32_t block;
    uint32_t offset;

    if(s_iap.other.buf[0] == CMD_IAP_END) {
        if(s_rx_len != 2U || s_iap.other.buf[1] != 0U) {
            return 1U;
        }
        s_end_received = 1U;
        return 0U;
    }

    if(s_iap.other.buf[0] == CMD_IAP_ABORT) {
        if(s_rx_len != 2U || s_iap.other.buf[1] != 0U) {
            return 1U;
        }
        s_abort_received = 1U;
        return 0U;
    }

    if(s_iap.other.buf[0] == CMD_IAP_ERASE) {
        uint8_t ret;

        if(s_rx_len != 6U || s_iap.erase.len != 4U) {
            PRINT("OTA erase frame bad len=%u rx=%u\r\n",
                  (unsigned)s_iap.erase.len, (unsigned)s_rx_len);
            return 1U;
        }
        block = (uint32_t)s_iap.erase.addr[0] |
                ((uint32_t)s_iap.erase.addr[1] << 8);
        offset = block * 16U;
        ret = ota_uart_erase(offset,
                             (uint32_t)s_iap.erase.block_num[0] |
                             ((uint32_t)s_iap.erase.block_num[1] << 8));
        if(ret != 0U) {
            PRINT("OTA erase reject off=%lu\r\n", (unsigned long)offset);
        }
        return ret;
    }

    if(s_iap.other.buf[0] == CMD_IAP_PROM) {
        uint32_t data_len;

        if(s_iap.program.len < 2U || s_rx_len != (uint16_t)s_iap.program.len + 2U) {
            PRINT("OTA program frame bad len=%u rx=%u need=%u\r\n",
                  (unsigned)s_iap.program.len, (unsigned)s_rx_len,
                  (unsigned)s_rx_need);
            return 1U;
        }
        data_len = (uint32_t)s_iap.program.len - 2U;
        if(data_len == 0U || data_len > (IAP_LEN - 4U)) {
            PRINT("OTA program data bad len=%lu\r\n", (unsigned long)data_len);
            return 1U;
        }
        block = (uint32_t)s_iap.program.addr[0] |
                ((uint32_t)s_iap.program.addr[1] << 8);
        offset = block * 16U;
        if(OtaStore_WriteChunk(offset, s_iap.program.buf, data_len) != 0U) {
            PRINT("OTA program reject off=%lu len=%lu expect=%lu\r\n",
                  (unsigned long)offset, (unsigned long)data_len,
                  (unsigned long)OtaStore_GetBytesWritten());
            return 1U;
        }
        return 0U;
    }

    PRINT("OTA frame reject cmd=0x%02X\r\n", (unsigned)s_iap.other.buf[0]);
    return 1U;
}

void OtaUart_FeedByte(uint8_t b)
{
    if(s_rx_len == 0U && b != CMD_IAP_PROM && b != CMD_IAP_ERASE &&
       b != CMD_IAP_END && b != CMD_IAP_ABORT) {
        return;
    }
    if(s_rx_len >= IAP_LEN) {
        s_rx_len = 0U;
        s_rx_need = 0U;
        s_error = 1U;
        s_frame_done = 1U;
        s_frame_ok = 0U;
        PRINT("OTA frame overflow\r\n");
        return;
    }

    s_iap.other.buf[s_rx_len++] = b;
    if(s_rx_len == 2U) {
        s_rx_need = (uint16_t)s_iap.other.buf[1] + 2U;
        if(s_rx_need > IAP_LEN ||
           (s_rx_need < 4U &&
            s_iap.other.buf[0] != CMD_IAP_END &&
            s_iap.other.buf[0] != CMD_IAP_ABORT)) {
            PRINT("OTA frame header reject cmd=0x%02X len=%u\r\n",
                  (unsigned)s_iap.other.buf[0], (unsigned)s_iap.other.buf[1]);
            s_rx_len = 0U;
            s_rx_need = 0U;
            s_error = 1U;
            s_frame_done = 1U;
            s_frame_ok = 0U;
            return;
        }
    }

    if(s_rx_need != 0U && s_rx_len == s_rx_need) {
        s_frame_ok = (ota_uart_dispatch() == 0U) ? 1U : 0U;
        s_frame_done = 1U;
        if(!s_frame_ok) {
            s_error = 1U;
        }
        s_rx_len = 0U;
        s_rx_need = 0U;
    }
}
