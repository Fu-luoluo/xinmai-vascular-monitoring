/********************************** (C) COPYRIGHT *******************************
 * File Name          : central.h
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/11/12
 * Description        : BLE Central ??????????Finger + Wrist??????? MAC ??�F??
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

#ifndef CENTRAL_H
#define CENTRAL_H

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * CONSTANTS
 */

#define START_DEVICE_EVT              0x0001
#define START_SVC_DISCOVERY_EVT       0x0008
#define START_PARAM_UPDATE_EVT        0x0010
#define START_CHAR4_DISC_EVT          0x0100
#define ESTABLISH_LINK_TIMEOUT_EVT    0x0200
#define START_RECONNECT_EVT           0x0400
#define START_SECOND_LINK_EVT         0x0800
/* ?? ready ?? 1s ?????????? 4s ????????? */
#define CENTRAL_SECOND_LINK_DELAY     1600

/*********************************************************************
 * FUNCTIONS
 */

extern void Central_Init(void);
extern uint16_t Central_ProcessEvent(uint8_t task_id, uint16_t events);

void Central_StartMeasurement(void);
void Central_StopMeasurement(void);
uint8_t Central_CanStartMeasurement(void);
uint8_t Central_IsMeasurementWanted(void);

#ifdef __cplusplus
}
#endif

#endif
