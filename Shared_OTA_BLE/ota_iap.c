#include "CONFIG.h"
#include "ota_iap.h"
#include "OTAprofile.h"
#include "ota.h"
#include <string.h>

unsigned char CurrImageFlag = 0xFF;

static uint8_t         s_task_id;
static OTA_IAP_CMD_t   s_iap;
static uint32_t        s_op_add;
static uint32_t        s_op_len;
static uint32_t        s_erase_add;
static uint32_t        s_erase_block_num;
static uint32_t        s_erase_block_cnt;
static uint8_t         s_verify_status;
static __attribute__((aligned(8))) uint8_t s_block_buf[16];

static OTAProfileCBs_t s_profile_cbs = {
    NULL,
    NULL
};

static void ota_send_status(uint8_t status)
{
    uint8_t rsp[2] = {status, 0};
    OTAProfile_SendData(OTAPROFILE_CHAR, rsp, 2);
}

static void ota_switch_flag(uint8_t new_flag)
{
    EEPROM_READ(OTA_DATAFLASH_ADD, (uint32_t *)&s_block_buf[0], 4);
    EEPROM_ERASE(OTA_DATAFLASH_ADD, EEPROM_PAGE_SIZE);
    s_block_buf[0] = new_flag;
    EEPROM_WRITE(OTA_DATAFLASH_ADD, (uint32_t *)&s_block_buf[0], 4);
    CurrImageFlag = new_flag;
}

static void ota_read_complete(unsigned char index)
{
    (void)index;
}

static void ota_write_data(unsigned char index, unsigned char *p_data, unsigned char w_len)
{
    (void)index;
    if(w_len == 0 || w_len > IAP_LEN) {
        ota_send_status(0xFE);
        return;
    }
    memcpy(&s_iap.other.buf[0], p_data, w_len);
    OtaIap_FeedFrame();
}

void OtaIap_FeedFrame(void)
{
    switch(s_iap.other.buf[0]) {
        case CMD_IAP_PROM: {
            uint8_t status;
            s_op_len = s_iap.program.len;
            s_op_add = ((uint32_t)s_iap.program.addr[0]) |
                       ((uint32_t)s_iap.program.addr[1] << 8);
            s_op_add = (s_op_add * 16u) + IMAGE_A_SIZE;
            status = FLASH_ROM_WRITE(s_op_add, s_iap.program.buf, (uint16_t)s_op_len);
            ota_send_status(status);
            break;
        }
        case CMD_IAP_ERASE: {
            s_op_add = ((uint32_t)s_iap.erase.addr[0]) |
                       ((uint32_t)s_iap.erase.addr[1] << 8);
            s_op_add = (s_op_add * 16u) + IMAGE_A_SIZE;
            s_erase_block_num = ((uint32_t)s_iap.erase.block_num[0]) |
                                  ((uint32_t)s_iap.erase.block_num[1] << 8);
            s_erase_add = s_op_add;
            s_erase_block_cnt = 0;
            s_verify_status = 0;
            if(s_erase_add < IMAGE_B_START_ADD ||
               (s_erase_add + (s_erase_block_num - 1u) * FLASH_BLOCK_SIZE) > IMAGE_IAP_START_ADD) {
                ota_send_status(0xFF);
            } else {
                tmos_set_event(s_task_id, OTA_FLASH_ERASE_EVT);
            }
            break;
        }
        case CMD_IAP_VERIFY: {
            uint8_t status = 0;
            s_op_len = s_iap.verify.len;
            s_op_add = ((uint32_t)s_iap.verify.addr[0]) |
                       ((uint32_t)s_iap.verify.addr[1] << 8);
            s_op_add = (s_op_add * 16u) + IMAGE_A_SIZE;
            status = FLASH_ROM_VERIFY(s_op_add, s_iap.verify.buf, s_op_len);
            s_verify_status |= status;
            ota_send_status(s_verify_status);
            break;
        }
        case CMD_IAP_END:
            SYS_DisableAllIrq(NULL);
            ota_switch_flag(IMAGE_IAP_FLAG);
            mDelaymS(10);
            SYS_ResetExecute();
            break;
        case CMD_IAP_INFO: {
            uint8_t send_buf[20];
            send_buf[0] = IMAGE_B_FLAG;
            send_buf[1] = (uint8_t)(IMAGE_SIZE & 0xFF);
            send_buf[2] = (uint8_t)((IMAGE_SIZE >> 8) & 0xFF);
            send_buf[3] = (uint8_t)((IMAGE_SIZE >> 16) & 0xFF);
            send_buf[4] = (uint8_t)((IMAGE_SIZE >> 24) & 0xFF);
            send_buf[5] = (uint8_t)(FLASH_BLOCK_SIZE & 0xFF);
            send_buf[6] = (uint8_t)((FLASH_BLOCK_SIZE >> 8) & 0xFF);
            send_buf[7] = (uint8_t)(CHIP_ID & 0xFF);
            send_buf[8] = (uint8_t)((CHIP_ID >> 8) & 0xFF);
            memset(send_buf + 9, 0, sizeof(send_buf) - 9);
            OTAProfile_SendData(OTAPROFILE_CHAR, send_buf, 20);
            break;
        }
        default:
            ota_send_status(0xFE);
            break;
    }
}

void OtaIap_Init(uint8_t task_id)
{
    s_task_id = task_id;
    s_profile_cbs.pfnOTAProfileRead = ota_read_complete;
    s_profile_cbs.pfnOTAProfileWrite = ota_write_data;
}

void OtaIap_AddGattService(void)
{
    OTAProfile_AddService(GATT_ALL_SERVICES);
}

void OtaIap_RegisterProfileCallbacks(void)
{
    OTAProfile_RegisterAppCBs(&s_profile_cbs);
}

uint16_t OtaIap_ProcessEvent(uint16_t events)
{
    if(events & OTA_FLASH_ERASE_EVT) {
        uint8_t status = FLASH_ROM_ERASE(s_erase_add + s_erase_block_cnt * FLASH_BLOCK_SIZE,
                                         FLASH_BLOCK_SIZE);
        if(status != SUCCESS) {
            ota_send_status(status);
            return (uint16_t)(events ^ OTA_FLASH_ERASE_EVT);
        }
        s_erase_block_cnt++;
        if(s_erase_block_cnt >= s_erase_block_num) {
            ota_send_status(status);
            return (uint16_t)(events ^ OTA_FLASH_ERASE_EVT);
        }
        return events;
    }
    return 0;
}
