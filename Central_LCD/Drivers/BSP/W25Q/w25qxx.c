#include "w25qxx.h"
#include "w25q_spi.h"
#include "CH58x_common.h"
#include "CONFIG.h"
#include <stddef.h>

#define W25Q_CMD_WRITE_ENABLE   0x06u
#define W25Q_CMD_READ_DATA      0x03u
#define W25Q_CMD_PAGE_PROGRAM   0x02u
#define W25Q_CMD_SECTOR_ERASE   0x20u
#define W25Q_CMD_READ_STATUS1   0x05u
#define W25Q_CMD_JEDEC_ID       0x9Fu

#define W25Q_STATUS_WIP         0x01u

static uint32_t s_w25q_jedec_id;

static void w25q_cmd_only(uint8_t cmd)
{
    W25Q_CS_Low();
    W25Q_SPI_Transmit(&cmd, 1);
    W25Q_CS_High();
}

static void w25q_cmd_read(uint8_t cmd, uint8_t *buf, uint16_t len)
{
    W25Q_CS_Low();
    W25Q_SPI_Transmit(&cmd, 1);
    if(len > 0 && buf != NULL) {
        W25Q_SPI_Receive(buf, len);
    }
    W25Q_CS_High();
}

static void w25q_write_enable(void)
{
    w25q_cmd_only(W25Q_CMD_WRITE_ENABLE);
}

static uint8_t w25q_read_status1(void)
{
    uint8_t status = 0;

    w25q_cmd_read(W25Q_CMD_READ_STATUS1, &status, 1);
    return status;
}

uint8_t W25Q_IsSupportedJEDECID(uint32_t id)
{
    if(id == 0u || id == 0xFFFFFFu) {
        return 0;
    }

    switch(id) {
        case 0xEF4017u: /* Winbond W25Q64 */
        case 0xEF7017u: /* Winbond W25Q64 (alt) */
        case 0xC84017u: /* GigaDevice GD25Q64 */
        case 0xC86017u: /* MXIC MX25L6433F */
        case 0x1C3017u: /* ESMT EN25Q64 / EN25QH64 and W25Q64-compatible modules */
            return 1;
        default:
            break;
    }

    /* JEDEC capacity byte 0x17 = 64Mbit (8MB), common on W25Q64-class clones */
    if((id & 0xFFu) == 0x17u) {
        return 1;
    }
    return 0;
}

uint32_t W25Q_GetJEDECID(void)
{
    return s_w25q_jedec_id;
}

void W25Q_WaitBusy(void)
{
    while(w25q_read_status1() & W25Q_STATUS_WIP) {
        WWDG_SetCounter(0);
    }
}

uint32_t W25Q_ReadJEDECID(void)
{
    uint8_t raw[3];
    uint8_t cmd = W25Q_CMD_JEDEC_ID;

    W25Q_CS_Low();
    W25Q_SPI_Transmit(&cmd, 1);
    W25Q_SPI_Receive(raw, sizeof(raw));
    W25Q_CS_High();

    s_w25q_jedec_id = ((uint32_t)raw[0] << 16) |
                      ((uint32_t)raw[1] << 8) |
                      (uint32_t)raw[2];
    return s_w25q_jedec_id;
}

uint8_t W25Q_Init(void)
{
    uint32_t id;

    W25Q_SPI_Init();
    id = W25Q_ReadJEDECID();
    if(!W25Q_IsSupportedJEDECID(id)) {
        PRINT("W25Q64 init fail, ID=0x%06lX\r\n", (unsigned long)id);
        return 1;
    }

    /*
     * ESMT 0x1C3017 parts on the current board intermittently fail page
     * verification at the faster divider.  Keep the conservative divider
     * used during probe so OTA writes remain reliable.
     */
    SPI0_CLKCfg(8);
    PRINT("W25Q64 init OK, ID=0x%06lX\r\n", (unsigned long)id);
    return 0;
}

void W25Q_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
    uint8_t cmd[4];

    if(buf == NULL || len == 0) {
        return;
    }

    cmd[0] = W25Q_CMD_READ_DATA;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)(addr);

    W25Q_CS_Low();
    W25Q_SPI_Transmit(cmd, sizeof(cmd));
    W25Q_SPI_Receive(buf, len);
    W25Q_CS_High();
}

void W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint16_t len)
{
    uint8_t cmd[4];

    if(buf == NULL || len == 0 || len > W25Q_PAGE_SIZE) {
        return;
    }

    cmd[0] = W25Q_CMD_PAGE_PROGRAM;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)(addr);

    w25q_write_enable();
    W25Q_CS_Low();
    W25Q_SPI_Transmit(cmd, sizeof(cmd));
    W25Q_SPI_Transmit(buf, len);
    W25Q_CS_High();
    W25Q_WaitBusy();
}

void W25Q_SectorErase4K(uint32_t addr)
{
    uint8_t cmd[4];

    cmd[0] = W25Q_CMD_SECTOR_ERASE;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)(addr);

    w25q_write_enable();
    W25Q_CS_Low();
    W25Q_SPI_Transmit(cmd, sizeof(cmd));
    W25Q_CS_High();
    W25Q_WaitBusy();
}
