/********************************** (C) COPYRIGHT *******************************
 * File Name          : peripheral.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/11
 * Description        :
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#ifndef PERIPHERAL_H
#define PERIPHERAL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * INCLUDES
 */
#include "../../Middlewares/Algorithm/spo2/max30102_measure.h"

/*********************************************************************
 * CONSTANTS
 */

// Peripheral Task Events
#define SBP_START_DEVICE_EVT    0x0001
#define SBP_PERIODIC_EVT        0x0002
#define SBP_READ_RSSI_EVT       0x0004
#define SBP_PARAM_UPDATE_EVT    0x0008
#define SBP_PHY_UPDATE_EVT      0x0010
#define SBP_PPG_SAMPLE_EVT      0x0020  /* PPG 周期采样（仅在 Char1 收到启动命令后才会被启用） */

/*********************************************************************
 * MACROS
 */
typedef struct
{
    uint16_t connHandle; // Connection handle of current connection
    uint16_t connInterval;
    uint16_t connSlaveLatency;
    uint16_t connTimeout;
} peripheralConnItem_t;

/*********************************************************************
 * FUNCTIONS
 */

/*
 * Task Initialization for the BLE Application
 */
extern void Peripheral_Init(void);

/*
 * Task Event Processor for the BLE Application
 */
extern uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events);

/*
 * Peripheral_WristNotifyJson - 把当前 HR/SpO2 拼装为 JSON 通过 Char4 Notify 发送
 *   JSON 格式: {"id":N,"ts":T,"b_idx":I,"beat_ts":B,"hr":H,"spo2":S}
 *     id      = pulse_id（主机 pacing）
 *     ts      = TMOS_GetSystemClock() 换算成 ms 的本机时间戳
 *     b_idx   = 当前窗口 IR 峰值样本索引 (0..149)
 *     beat_ts = 峰值时刻本机 ms 时间戳（估算）
 *     hr      = 心率 bpm
 *     spo2    = 血氧 %
 *
 *   未连接 / 未握手 (g_session_id == 0) 时直接 return，不发包
 */
extern void Peripheral_WristNotifyJson(uint8_t hr, uint8_t spo2, uint16_t beat_idx,
                                       uint32_t beat_ts, const ppg_wf_compact_t *wf);

/*********************************************************************
*********************************************************************/

#ifdef __cplusplus
}
#endif

#endif
