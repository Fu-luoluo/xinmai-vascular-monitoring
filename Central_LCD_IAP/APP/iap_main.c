#include "CH58x_common.h"
#include "ota.h"
#include "ota_flag.h"
#include "ota_store.h"
#include "flash_layout.h"
#include "flash_store.h"
#include "w25qxx.h"

static void iap_uart0_init(void)
{
    GPIOPinRemap(DISABLE, RB_PIN_UART0);
    GPIOB_SetBits(GPIO_Pin_7);
    GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
    UART0_DefInit();
}

static void iap_uart0_puts(const char *s)
{
    while(s != NULL && *s != '\0') {
        while(R8_UART0_TFC == UART_FIFO_SIZE) {
        }
        R8_UART0_THR = (uint8_t)*s++;
    }
}

static void iap_jump_app(void)
{
    /*
     * Re-enter the C JumpIAP selector in reset context.  It reads the actual
     * DataFlash flag with EEPROM_READ and then cold-starts the App.
     */
    SYS_ResetExecute();
    for(;;) {
    }
}

int main(void)
{
    FLASH_ROM_PWR_UP();

#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
    PWR_DCDCCfg(ENABLE);
#endif
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(SYSCLK_FREQ);

    iap_uart0_init();
    iap_uart0_puts("IAP\r\n");

#if(defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
    GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
#endif

    OtaFlag_Read();

    if(CurrImageFlag == IMAGE_IAP_FLAG) {
        iap_uart0_puts("IAP APPLY\r\n");
        if(W25Q_Init() == 0 && FlashStore_Init() == 0 &&
           OtaStore_ApplyPendingImage() == 0) {
            iap_uart0_puts("IAP DONE\r\n");
        } else {
            uint8_t err = OtaStore_GetLastApplyError();

            if(err == OTA_APPLY_ERR_HEADER) {
                iap_uart0_puts("IAP FAIL HEADER\r\n");
            } else if(err == OTA_APPLY_ERR_CRC) {
                iap_uart0_puts("IAP FAIL CRC\r\n");
            } else if(err == OTA_APPLY_ERR_FLASH) {
                iap_uart0_puts("IAP FAIL FLASH\r\n");
            } else if(err == OTA_APPLY_ERR_COMMIT) {
                iap_uart0_puts("IAP FAIL COMMIT\r\n");
            } else {
                iap_uart0_puts("IAP FAIL INIT\r\n");
            }
            /*
             * The previous App was not erased when validation failed.
             * Clear the route flag so the next reset returns to that App.
             */
            (void)OtaFlag_Switch(IMAGE_A_FLAG);
            mDelaymS(20);
            SYS_ResetExecute();
            for(;;) {
            }
        }
    } else {
        iap_uart0_puts("IAP PASS\r\n");
    }

    iap_uart0_puts("IAP RESET\r\n");
    mDelaymS(20);
    iap_jump_app();
}
