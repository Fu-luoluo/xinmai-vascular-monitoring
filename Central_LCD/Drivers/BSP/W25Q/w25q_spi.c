#include "w25q_spi.h"
#include "w25q_port.h"
#include "CH58x_spi.h"

#define W25Q_SPI0_CHUNK_MAX  4095U

static void w25q_spi0_wait_free(void)
{
    while(!(R8_SPI0_INT_FLAG & RB_SPI_FREE)) {
    }
}

void W25Q_SPI_Init(void)
{
    GPIOPinRemap(DISABLE, RB_PIN_SPI0);

    /* Release hardware SPI0 SCS (PA12) and external flash CS (PA2) */
    GPIOA_SetBits(W25Q_SPI0_SCS_UNUSED_PIN | W25Q_CS_PIN);
    GPIOA_ModeCfg(W25Q_SPI0_SCS_UNUSED_PIN | W25Q_CS_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(W25Q_SPI0_PINS_OUT, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(W25Q_SPI0_MISO_PIN, GPIO_ModeIN_PU);

    SPI0_MasterDefInit();
    SPI0_CLKCfg(8);
    SPI0_DataMode(Mode0_HighBitINFront);

    mDelaymS(10);
}

void W25Q_CS_Low(void)
{
    W25Q_CS_LOW();
}

void W25Q_CS_High(void)
{
    W25Q_CS_HIGH();
}

void W25Q_SPI_SendByte(uint8_t tx)
{
    SPI0_MasterSendByte(tx);
}

uint8_t W25Q_SPI_RecvByte(void)
{
    return SPI0_MasterRecvByte();
}

uint8_t W25Q_SPI_TransferByte(uint8_t tx)
{
    W25Q_SPI_SendByte(tx);
    return W25Q_SPI_RecvByte();
}

void W25Q_SPI_Transmit(const uint8_t *tx, uint32_t len)
{
    uint32_t offset = 0;

    while(offset < len) {
        uint16_t chunk = (uint16_t)((len - offset) > W25Q_SPI0_CHUNK_MAX ?
                                    W25Q_SPI0_CHUNK_MAX : (len - offset));
        SPI0_MasterTrans((uint8_t *)(uintptr_t)(tx + offset), chunk);
        offset += chunk;
    }
    w25q_spi0_wait_free();
}

void W25Q_SPI_Receive(uint8_t *rx, uint32_t len)
{
    uint32_t offset = 0;

    while(offset < len) {
        uint16_t chunk = (uint16_t)((len - offset) > W25Q_SPI0_CHUNK_MAX ?
                                    W25Q_SPI0_CHUNK_MAX : (len - offset));
        SPI0_MasterRecv(rx + offset, chunk);
        offset += chunk;
    }
    w25q_spi0_wait_free();
}
