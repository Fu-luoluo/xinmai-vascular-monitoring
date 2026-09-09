#include "w25q_selftest.h"
#include "w25qxx.h"
#include "flash_layout.h"
#include "CONFIG.h"

#define W25Q_SELFTEST_ADDR   FLASH_SELFTEST_ADDR
#define W25Q_SELFTEST_LEN    W25Q_PAGE_SIZE

uint8_t W25Q_RunSelfTest(void)
{
    static uint8_t tx[W25Q_SELFTEST_LEN];
    static uint8_t rx[W25Q_SELFTEST_LEN];
    uint32_t id;
    uint16_t i;

    id = W25Q_ReadJEDECID();
    if(!W25Q_IsSupportedJEDECID(id)) {
        PRINT("W25Q64 ID mismatch: 0x%06lX\r\n", (unsigned long)id);
        return 1;
    }

    for(i = 0; i < W25Q_SELFTEST_LEN; i++) {
        tx[i] = (uint8_t)(i & 0xFFu);
    }

    W25Q_SectorErase4K(W25Q_SELFTEST_ADDR);
    W25Q_PageProgram(W25Q_SELFTEST_ADDR, tx, W25Q_SELFTEST_LEN);
    W25Q_Read(W25Q_SELFTEST_ADDR, rx, W25Q_SELFTEST_LEN);

    for(i = 0; i < W25Q_SELFTEST_LEN; i++) {
        if(rx[i] != tx[i]) {
            PRINT("W25Q64 verify fail @0x%06lX idx=%u exp=%02X got=%02X\r\n",
                  (unsigned long)(W25Q_SELFTEST_ADDR + i),
                  (unsigned)i, (unsigned)tx[i], (unsigned)rx[i]);
            return 2;
        }
    }

    PRINT("W25Q64 OK: ID=0x%06lX\r\n", (unsigned long)id);
    return 0;
}
