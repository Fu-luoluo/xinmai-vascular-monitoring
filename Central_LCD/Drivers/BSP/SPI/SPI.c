#include "SPI.h"
#include "CH58x_spi.h"
#include <stdint.h>

#define SPI1_DMA_CHUNK_MAX  4095U

u8 SPI_WriteByte(void *SPIx, u8 Byte)
{
    (void)SPIx;
    SPI1_MasterSendByte(Byte);
    return 0;
}

void SPI1_WriteBytes(const uint8_t * data, uint32_t len)
{
    uint32_t offset = 0;

    while(offset < len) {
        uint16_t chunk = (uint16_t)((len - offset) > SPI1_DMA_CHUNK_MAX ?
                                    SPI1_DMA_CHUNK_MAX : (len - offset));
        SPI1_MasterTrans((uint8_t *)(uintptr_t)(data + offset), chunk);
        offset += chunk;
    }
}

void SPI_SetSpeed(void *SPIx, u8 SpeedSet)
{
    (void)SPIx;
    if(SpeedSet == 1) {
        SPI1_CLKCfg(2);
    } else {
        SPI1_CLKCfg(8);
    }
}

void SPI2_Init(void)
{
    GPIOA_ModeCfg(GPIO_Pin_0 | GPIO_Pin_1, GPIO_ModeOut_PP_5mA);
    SPI1_MasterDefInit();
    SPI1_CLKCfg(2);
}
