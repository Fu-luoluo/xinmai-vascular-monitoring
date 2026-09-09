/********************************** (C) COPYRIGHT *******************************
 * File Name          : main.c
 * Author             : WCH
 * Version            : V1.1
 * Date               : 2020/08/06
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/******************************************************************************/
/* Dual-link Central: enlarge BLE heap (default 6K is tight for 2 ACL + GATT) */
#ifndef BLE_MEMHEAP_SIZE
#define BLE_MEMHEAP_SIZE                    (1024 * 12)
#endif
#ifndef BLE_BUFF_MAX_LEN
#define BLE_BUFF_MAX_LEN                    128
#endif
#include "CONFIG.h"
#include "hal.h"
#include "central.h"
#include "pwv.h"
#include "lcd.h"
#include "touch.h"
#include "lvgl_hal.h"
#include "power_mgr.h"
#include "measure_ctrl.h"
#include "alarm_mgr.h"
#include "voice_mgr.h"
#include "host_link.h"
#include "w25qxx.h"
#include "w25q_selftest.h"
#include "flash_store.h"
#include "history_store.h"
#include "user_profile.h"
#include "ota_store.h"

#define APP_FIRMWARE_VERSION  16U

/*********************************************************************
 * GLOBAL TYPEDEFS
 */
__attribute__((aligned(4))) uint32_t MEM_BUF[BLE_MEMHEAP_SIZE / 4];

/* Kept at 0x1004 by the OTA linker script for image-validity checks. */
const uint32_t Image_Flag __attribute__((section(".ImageFlag"))) = 0xFFFFFFFFu;

#if(defined(BLE_MAC)) && (BLE_MAC == TRUE)
const uint8_t MacAddr[6] = {0x84, 0xC2, 0xE4, 0x03, 0x02, 0x02};
#endif

/*********************************************************************
 * @fn      Main_Circulation
 *
 * @brief   ?????
 *
 * @return  none
 */
__HIGH_CODE
__attribute__((noinline))
void Main_Circulation()
{
    while(1)
    {
        TMOS_SystemProcess();
    }
}

/*********************************************************************
 * @fn      main
 *
 * @brief   ??????
 *
 * @return  none
 */
int main(void)
{
#if(defined(DCDC_ENABLE)) && (DCDC_ENABLE == TRUE)
    PWR_DCDCCfg(ENABLE);
#endif
    HSECFG_Capacitance(HSECap_18p);
    SetSysClock(SYSCLK_FREQ);
#if(defined(HAL_SLEEP)) && (HAL_SLEEP == TRUE)
    GPIOA_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_All, GPIO_ModeIN_PU);
#endif
#ifdef DEBUG
    /* UART0 debug on default pins: PB7=TX, PB4=RX (do NOT remap to PA14/15 — W25Q SPI0) */
    GPIOPinRemap(DISABLE, RB_PIN_UART0);
    GPIOB_SetBits(GPIO_Pin_7);
    GPIOB_ModeCfg(GPIO_Pin_4, GPIO_ModeIN_PU);
    GPIOB_ModeCfg(GPIO_Pin_7, GPIO_ModeOut_PP_5mA);
    UART0_DefInit();
#endif
    PRINT("%s\n", VER_LIB);
    if(WWDG_GetFlowFlag()) {
        PRINT("Reset cause: watchdog\r\n");
        WWDG_ClearFlag();
    }

    if(W25Q_Init() != 0) {
        uint8_t n;

        PRINT("W25Q64 probe retry\r\n");
        for(n = 0; n < 4U; n++) {
            mDelaymS(20);
            if(W25Q_Init() == 0) {
                break;
            }
        }
    }
    if(FlashStore_Ensure() != 0) {
        PRINT("W25Q64 storage not ready\r\n");
    } else {
#ifdef DEBUG
        if(W25Q_RunSelfTest() != 0) {
            PRINT("W25Q64 selftest fail\r\n");
        } else {
            PRINT("W25Q64 selftest pass\r\n");
        }
#endif
        OtaStore_Init();
        (void)UserProfile_Init();
        if(HistoryStore_Init() != 0) {
            PRINT("HistoryStore init fail\r\n");
        }
#if OTA_STAGE1_TEST
        if(OtaStore_Stage1SelfTest() != 0) {
            PRINT("OTA S1: aborted\r\n");
        }
#endif
    }

    CH58x_BLEInit();
    HAL_Init();

    /*
     * PC OTA uses the same UART1 as the future gateway.  57600 is selected
     * for the current USB-TTL wiring after 115200 showed byte corruption.
     */
    BSP_UART1_Init(57600);
    HostLink_Init();
    HostLink_TaskStart();
    PWV_Init();

    LCD_Init();
    /* UART3 默认 TX 使用 PA5，PB20/PB21 继续由 CTP 使用。 */
    Voice_Init();
    TP_Init();
    PowerMgr_Init();
    AlarmMgr_Init();
    lvgl_hal_init();
    lvgl_hal_task_start();
    Measure_Init();

    GAPRole_CentralInit();
    Central_Init();
    /* MP3 模块和 TF 卡稳定后播放 /01/001.mp3，作为单线串口联调验证。 */
    Voice_PlayStartupTest();
    {
        ota_header_t hdr;
        uint8_t ota_status = OTA_STATUS_EMPTY;

        if(FlashStore_IsReady() && OtaStore_ReadHeader(&hdr) == 0) {
            ota_status = hdr.status;
        }
        HostLink_ReportBoot(APP_FIRMWARE_VERSION, ota_status);
    }
    Main_Circulation();
}

/******************************** endfile @ main ******************************/