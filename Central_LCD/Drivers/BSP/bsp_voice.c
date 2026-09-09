#include "bsp_voice.h"
/*
 * UART3 默认使用 PA4/PA5；本模块仅需 PA5(TXD3) -> MP3 RX。
 * PA4 已分配给 LCD DC，不能配置为 UART 接收脚。
 */

void BSP_VoiceUart_Init(void)
{
    GPIOPinRemap(DISABLE, RB_RF_ANT_SW_EN);
    GPIOPinRemap(DISABLE, RB_PIN_UART3);
    GPIOA_SetBits(bTXD3);
    GPIOA_ModeCfg(bTXD3, GPIO_ModeOut_PP_5mA);

    UART3_DefInit();
    UART3_BaudRateCfg(VOICE_UART_BAUD);
    PRINT("[VOICE] UART3 PA5(TX) ready, %lu bps\r\n",
          (unsigned long)VOICE_UART_BAUD);
}

void BSP_VoiceUart_Send(const uint8_t *buf, uint16_t len)
{
    uint16_t i;

    if(buf == NULL || len == 0U) {
        PRINT("[VOICE] TX ignored: empty frame\r\n");
        return;
    }
    PRINT("[VOICE] TX:");
    for(i = 0U; i < len; i++) {
        PRINT(" %02X", (unsigned)buf[i]);
    }
    PRINT("\r\n");

    UART3_SendString((uint8_t *)buf, len);
    while((R8_UART3_LSR & RB_LSR_TX_ALL_EMP) == 0U) {
    }
    PRINT("[VOICE] TX complete, UART3_LSR=%02X\r\n",
          (unsigned)R8_UART3_LSR);
}
