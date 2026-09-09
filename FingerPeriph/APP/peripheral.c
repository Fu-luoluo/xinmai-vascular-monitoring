/********************************** (C) COPYRIGHT *******************************
 * File Name          : peripheral.C
 * Author             : WCH
 * Version            : V1.0
 * Date               : 2018/12/10
 * Description        : ??????????????????????????????????????????????????
 *                      ?????????????????????????????????
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "CONFIG.h"
#include "devinfoservice.h"
#include "gattprofile.h"
#include "peripheral.h"
#include "bsp_uart.h"
#if defined(ENABLE_BLE_OTA) && (ENABLE_BLE_OTA == TRUE)
#include "ota_iap.h"
#endif
#include "../Middlewares/Algorithm/spo2/max30102_measure.h"
#include <stdio.h>

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

// How often to perform periodic event
#define SBP_PERIODIC_EVT_PERIOD              8000   // ?1600

// How often to perform read rssi event
#define SBP_READ_RSSI_EVT_PERIOD             3200

// Parameter update delay
#define SBP_PARAM_UPDATE_DELAY               6400

// PHY update delay
#define SBP_PHY_UPDATE_DELAY                 2400

// What is the advertising interval when device is discoverable (units of 625us, 80=50ms)
#define DEFAULT_ADVERTISING_INTERVAL         80

// Limited discoverable mode advertises for 30.72s, and then stops
// General discoverable mode advertises indefinitely
#define DEFAULT_DISCOVERABLE_MODE            GAP_ADTYPE_FLAGS_GENERAL

// Minimum connection interval (units of 1.25ms, 6=7.5ms)
/* 双连主机下 min=6(7.5ms) 过密，易导致第二条 ACL reason=0x3e；放宽到 30ms */
#define DEFAULT_DESIRED_MIN_CONN_INTERVAL    24

// Maximum connection interval (units of 1.25ms, 100=125ms)
#define DEFAULT_DESIRED_MAX_CONN_INTERVAL    100

// Slave latency to use parameter update
#define DEFAULT_DESIRED_SLAVE_LATENCY        0

// Supervision timeout value (units of 10ms, 100=1s)
#define DEFAULT_DESIRED_CONN_TIMEOUT         100

// Company Identifier: WCH
#define WCH_COMPANY_ID                       0x07D7
#define PPG_SAMPLE_INTERVAL_MS               20

/*
 * ?????? ATT MTU????????
 *   - ??? ATT_MTU_SIZE = 23 ?? Notify ??????? 20 ????????? ~44 ???? JSON
 *   - ???? -D BLE_BUFF_MAX_LEN=80 ?? ??????? MTU = BLE_BUFF_MAX_LEN - 4 = 76
 *   - ???????? = min(????, ??? ESP32-C3 setMTU(64)) ?? 64
 *   - Notify ??? = ????MTU - 3 = ? 61????????? JSON
 */
#define DESIRED_ATT_MTU                      76

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */

/*
 * ?????????? WristPeriph ????
 *   g_session_id : ?? 0 = ????? Char1 ???????????? Notify
 *   g_pulse_id   : ?????????? Notify ??????JSON id ???
 *   g_sampling_on: ? 1 ???????? SBP_PPG_SAMPLE_EVT
 */
static uint8_t  g_session_id  = 0U;
static uint8_t  g_sampling_on = 0U;
static uint16_t g_pulse_id    = 0U;

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t Peripheral_TaskID = INVALID_TASK_ID; // Task ID for internal task/event processing

// GAP - SCAN RSP data (max size = 31 bytes)
static uint8_t scanRspData[] = {
    // complete name
    0x07, // length of this data : 1 + 6 
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'F',
    'i',
    'n',
    'g',
    'e',
    'r',
    // service UUID (Scan Response ????????????? Central ??????)
    0x03, // length of this data
    GAP_ADTYPE_16BIT_MORE,
    LO_UINT16(SIMPLEPROFILE_SERV_UUID),
    HI_UINT16(SIMPLEPROFILE_SERV_UUID),
    // connection interval range
    0x05, // length of this data
    GAP_ADTYPE_SLAVE_CONN_INTERVAL_RANGE,
    LO_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL), // 100ms
    HI_UINT16(DEFAULT_DESIRED_MIN_CONN_INTERVAL),
    LO_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL), // 1s
    HI_UINT16(DEFAULT_DESIRED_MAX_CONN_INTERVAL),

    // Tx power level
    0x02, // length of this data
    GAP_ADTYPE_POWER_LEVEL,
    0 // 0dBm
};

// GAP - Advertisement data (max size = 31 bytes, though this is
// best kept short to conserve power while advertising)
static uint8_t advertData[] = {
    0x02, // length of this data
    GAP_ADTYPE_FLAGS,
    DEFAULT_DISCOVERABLE_MODE | GAP_ADTYPE_FLAGS_BREDR_NOT_SUPPORTED,

    /* ??????? ADV ?????Central ?????? Scan Response ?????? */
    0x07, // length of this data : 1 + 6
    GAP_ADTYPE_LOCAL_NAME_COMPLETE,
    'F',
    'i',
    'n',
    'g',
    'e',
    'r',

    0x03,                  // length of this data
    GAP_ADTYPE_16BIT_MORE, // some of the UUID's, but not all
    LO_UINT16(SIMPLEPROFILE_SERV_UUID),
    HI_UINT16(SIMPLEPROFILE_SERV_UUID)
};

// GAP GATT Attributes
static uint8_t attDeviceName[GAP_DEVICE_NAME_LEN] = "Finger";

// Connection item list
static peripheralConnItem_t peripheralConnList;

static uint16_t peripheralMTU = ATT_MTU_SIZE;
/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent);
static void performPeriodicTask(void);
static void simpleProfileChangeCB(uint8_t paramID, uint8_t *pValue, uint16_t len);
static void peripheralParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
                                    uint16_t connSlaveLatency, uint16_t connTimeout);
static void peripheralInitConnItem(peripheralConnItem_t *peripheralConnList);
static void peripheralRssiCB(uint16_t connHandle, int8_t rssi);
static void peripheralChar4Notify(uint8_t *pValue, uint16_t len);

/*********************************************************************
 * PROFILE CALLBACKS
 */

// GAP Role Callbacks
static gapRolesCBs_t Peripheral_PeripheralCBs = {
    peripheralStateNotificationCB, // Profile State Change Callbacks
    peripheralRssiCB,              // When a valid RSSI is read from controller (not used by application)
    peripheralParamUpdateCB
};

// Broadcast Callbacks
static gapRolesBroadcasterCBs_t Broadcaster_BroadcasterCBs = {
    NULL, // Not used in peripheral role
    NULL  // Receive scan request callback
};

// GAP Bond Manager Callbacks
static gapBondCBs_t Peripheral_BondMgrCBs = {
    NULL, // Passcode callback (not used by application)
    NULL,  // Pairing / Bonding state Callback (not used by application)
    NULL  // oob callback
};

// Simple GATT Profile Callbacks
static simpleProfileCBs_t Peripheral_SimpleProfileCBs = {
    simpleProfileChangeCB // Characteristic value change callback
};
/*********************************************************************
 * PUBLIC FUNCTIONS
 */

/*********************************************************************
 * @fn      Peripheral_Init
 *
 * @brief   Initialization function for the Peripheral App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by TMOS.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void Peripheral_Init()
{
    Peripheral_TaskID = TMOS_ProcessEventRegister(Peripheral_ProcessEvent);

    // Setup the GAP Peripheral Role Profile
    {
        uint8_t  initial_advertising_enable = TRUE;
        uint16_t desired_min_interval = DEFAULT_DESIRED_MIN_CONN_INTERVAL;
        uint16_t desired_max_interval = DEFAULT_DESIRED_MAX_CONN_INTERVAL;

        // Set the GAP Role Parameters
        GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &initial_advertising_enable);
        GAPRole_SetParameter(GAPROLE_SCAN_RSP_DATA, sizeof(scanRspData), scanRspData);
        GAPRole_SetParameter(GAPROLE_ADVERT_DATA, sizeof(advertData), advertData);
        GAPRole_SetParameter(GAPROLE_MIN_CONN_INTERVAL, sizeof(uint16_t), &desired_min_interval);
        GAPRole_SetParameter(GAPROLE_MAX_CONN_INTERVAL, sizeof(uint16_t), &desired_max_interval);
    }

    {
        uint16_t advInt = DEFAULT_ADVERTISING_INTERVAL;

        // Set advertising interval
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MIN, advInt);
        GAP_SetParamValue(TGAP_DISC_ADV_INT_MAX, advInt);

        // Enable scan req notify
        GAP_SetParamValue(TGAP_ADV_SCAN_REQ_NOTIFY, ENABLE);
    }

    /*
     * ?????? CH585 Central??NO_INPUT_NO_OUTPUT + ??? Just Works?????????
     * ???? DISPLAY_ONLY + MITM ??????????????????????? GATT/CCCD ????
     */
    {
        uint32_t passkey = 0;
        uint8_t  pairMode = GAPBOND_PAIRING_MODE_WAIT_FOR_REQ;
        uint8_t  mitm = FALSE;
        uint8_t  bonding = FALSE;
        uint8_t  ioCap = GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT;
        GAPBondMgr_SetParameter(GAPBOND_PERI_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_PERI_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_PERI_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_PERI_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_PERI_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    // Initialize GATT attributes
    GGS_AddService(GATT_ALL_SERVICES);           // GAP
    GATTServApp_AddService(GATT_ALL_SERVICES);   // GATT attributes
    DevInfo_AddService();                        // Device Information Service
    SimpleProfile_AddService(GATT_ALL_SERVICES); // Simple GATT Profile
#if defined(ENABLE_BLE_OTA) && (ENABLE_BLE_OTA == TRUE)
    OtaIap_Init(Peripheral_TaskID);
    OtaIap_AddGattService();
    OtaIap_RegisterProfileCallbacks();
#endif

    // Set the GAP Characteristics
    GGS_SetParameter(GGS_DEVICE_NAME_ATT, GAP_DEVICE_NAME_LEN, attDeviceName);

    // Setup the SimpleProfile Characteristic Values
    {
        uint8_t charValue1[SIMPLEPROFILE_CHAR1_LEN] = {0};   /* ?? host ?? '1' ???? */
        uint8_t charValue2[SIMPLEPROFILE_CHAR2_LEN] = {0};
        uint8_t charValue3[SIMPLEPROFILE_CHAR3_LEN] = {0};
        uint8_t charValue4[SIMPLEPROFILE_CHAR4_LEN] = {0};   /* ????????????????? Peripheral_FingerNotifyJson */
        uint8_t charValue5[SIMPLEPROFILE_CHAR5_LEN] = {1, 2, 3, 4, 5};

        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR1, SIMPLEPROFILE_CHAR1_LEN, charValue1);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR2, SIMPLEPROFILE_CHAR2_LEN, charValue2);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR3, SIMPLEPROFILE_CHAR3_LEN, charValue3);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR4, SIMPLEPROFILE_CHAR4_LEN, charValue4);
        SimpleProfile_SetParameter(SIMPLEPROFILE_CHAR5, SIMPLEPROFILE_CHAR5_LEN, charValue5);
    }

    // Init Connection Item
    peripheralInitConnItem(&peripheralConnList);

    // Register callback with SimpleGATTprofile
    SimpleProfile_RegisterAppCBs(&Peripheral_SimpleProfileCBs);

    // Register receive scan request callback
    GAPRole_BroadcasterSetCB(&Broadcaster_BroadcasterCBs);

    // Setup a delayed profile startup
    tmos_set_event(Peripheral_TaskID, SBP_START_DEVICE_EVT);

    /*
     * ???MAX30102 ????? init??max30102_measure_init?????
     * simpleProfileChangeCB ??? Char1 = '1' ??????????????????
     * ??????????MCU ?????? FIFO ???????????????? ?? BLE ????
     * ?????????? FIFO ???? 32 ??????????????????
     */
}

/*********************************************************************
 * @fn      peripheralInitConnItem
 *
 * @brief   Init Connection Item
 *
 * @param   peripheralConnList -
 *
 * @return  NULL
 */
static void peripheralInitConnItem(peripheralConnItem_t *peripheralConnList)
{
    peripheralConnList->connHandle = GAP_CONNHANDLE_INIT;
    peripheralConnList->connInterval = 0;
    peripheralConnList->connSlaveLatency = 0;
    peripheralConnList->connTimeout = 0;
}

/*********************************************************************
 * @fn      Peripheral_ProcessEvent
 *
 * @brief   Peripheral Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id - The TMOS assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  events not processed
 */
uint16_t Peripheral_ProcessEvent(uint8_t task_id, uint16_t events)
{
    //  VOID task_id; // TMOS required parameter that isn't used in this function

    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;

        if((pMsg = tmos_msg_receive(Peripheral_TaskID)) != NULL)
        {
            Peripheral_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            // Release the TMOS message
            tmos_msg_deallocate(pMsg);
        }
        // return unprocessed events
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & SBP_START_DEVICE_EVT)
    {
        // Start the Device
        GAPRole_PeripheralStartDevice(Peripheral_TaskID, &Peripheral_BondMgrCBs, &Peripheral_PeripheralCBs);
        return (events ^ SBP_START_DEVICE_EVT);
    }

    if(events & SBP_PERIODIC_EVT)
    {
        // Restart timer
        if(SBP_PERIODIC_EVT_PERIOD)
        {
            tmos_start_task(Peripheral_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD);
        }
        // Perform periodic application task
        performPeriodicTask();
        return (events ^ SBP_PERIODIC_EVT);
    }

    if(events & SBP_PARAM_UPDATE_EVT)
    {
        // Send connect param update request
        // When the current connection parameters already meet the requirements for update, return 0x18(InvalidRange)
        GAPRole_PeripheralConnParamUpdateReq(peripheralConnList.connHandle,
                                             DEFAULT_DESIRED_MIN_CONN_INTERVAL,
                                             DEFAULT_DESIRED_MAX_CONN_INTERVAL,
                                             DEFAULT_DESIRED_SLAVE_LATENCY,
                                             DEFAULT_DESIRED_CONN_TIMEOUT,
                                             Peripheral_TaskID);

        return (events ^ SBP_PARAM_UPDATE_EVT);
    }

    if(events & SBP_PHY_UPDATE_EVT)
    {
        // start phy update
        PRINT("PHY Update %x...\n", GAPRole_UpdatePHY(peripheralConnList.connHandle, 0, 
                    GAP_PHY_BIT_LE_2M, GAP_PHY_BIT_LE_2M, 0));

        return (events ^ SBP_PHY_UPDATE_EVT);
    }

    if(events & SBP_READ_RSSI_EVT)
    {
        GAPRole_ReadRssiCmd(peripheralConnList.connHandle);
        tmos_start_task(Peripheral_TaskID, SBP_READ_RSSI_EVT, SBP_READ_RSSI_EVT_PERIOD);
        return (events ^ SBP_READ_RSSI_EVT);
    }

    if(events & SBP_PPG_SAMPLE_EVT)
    {
        max30102_measure_poll();
        /* ????????"???"????????????STOP/?????????? */
        if(g_sampling_on)
        {
            tmos_start_task(Peripheral_TaskID, SBP_PPG_SAMPLE_EVT, MS1_TO_SYSTEM_TIME(PPG_SAMPLE_INTERVAL_MS));
        }
        return (events ^ SBP_PPG_SAMPLE_EVT);
    }

#if defined(ENABLE_BLE_OTA) && (ENABLE_BLE_OTA == TRUE)
    if(events & OTA_FLASH_ERASE_EVT)
    {
        return OtaIap_ProcessEvent(events);
    }
#endif

    // Discard unknown events
    return 0;
}

/*********************************************************************
 * @fn      Peripheral_ProcessGAPMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void Peripheral_ProcessGAPMsg(gapRoleEvent_t *pEvent)
{
    switch(pEvent->gap.opcode)
    {
        case GAP_SCAN_REQUEST_EVENT:
        {
            PRINT("Receive scan req from %x %x %x %x %x %x  ..\n", pEvent->scanReqEvt.scannerAddr[0],
                  pEvent->scanReqEvt.scannerAddr[1], pEvent->scanReqEvt.scannerAddr[2], pEvent->scanReqEvt.scannerAddr[3],
                  pEvent->scanReqEvt.scannerAddr[4], pEvent->scanReqEvt.scannerAddr[5]);
            break;
        }

        case GAP_PHY_UPDATE_EVENT:
        {
            PRINT("Phy update Rx:%x Tx:%x ..\n", pEvent->linkPhyUpdate.connRxPHYS, pEvent->linkPhyUpdate.connTxPHYS);
            break;
        }

        default:
            break;
    }
}

/*********************************************************************
 * @fn      Peripheral_ProcessTMOSMsg
 *
 * @brief   Process an incoming task message.
 *
 * @param   pMsg - message to process
 *
 * @return  none
 */
static void Peripheral_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    switch(pMsg->event)
    {
        case GAP_MSG_EVENT:
        {
            Peripheral_ProcessGAPMsg((gapRoleEvent_t *)pMsg);
            break;
        }

        case GATT_MSG_EVENT:
        {
            gattMsgEvent_t *pMsgEvent;

            pMsgEvent = (gattMsgEvent_t *)pMsg;
            if(pMsgEvent->method == ATT_MTU_UPDATED_EVENT)
            {
                peripheralMTU = pMsgEvent->msg.exchangeMTUReq.clientRxMTU;
                PRINT("mtu exchange: %d\n", pMsgEvent->msg.exchangeMTUReq.clientRxMTU);
            }
            break;
        }

        default:
            break;
    }
}

/*********************************************************************
 * @fn      Peripheral_LinkEstablished
 *
 * @brief   Process link established.
 *
 * @param   pEvent - event to process
 *
 * @return  none
 */
static void Peripheral_LinkEstablished(gapRoleEvent_t *pEvent)
{
    gapEstLinkReqEvent_t *event = (gapEstLinkReqEvent_t *)pEvent;

    PRINT("Finger ACL in h=%02X peer %02x %02x %02x %02x %02x %02x role=%u\n",
          event->connectionHandle,
          event->devAddr[0], event->devAddr[1], event->devAddr[2],
          event->devAddr[3], event->devAddr[4], event->devAddr[5],
          (unsigned int)event->connRole);

    /* ?????????????????? Central???????????????????????????????? Finger?????????????? */
    if(peripheralConnList.connHandle != GAP_CONNHANDLE_INIT)
    {
        GAPRole_TerminateLink(event->connectionHandle);
        PRINT("Finger: reject 2nd link h=%02X (already h=%02X, phone off?)\n",
              event->connectionHandle, peripheralConnList.connHandle);
    }
    else
    {
        peripheralConnList.connHandle = event->connectionHandle;
        peripheralConnList.connInterval = event->connInterval;
        peripheralConnList.connSlaveLatency = event->connLatency;
        peripheralConnList.connTimeout = event->connTimeout;
        peripheralMTU = ATT_MTU_SIZE;
        // Set timer for periodic event
        tmos_start_task(Peripheral_TaskID, SBP_PERIODIC_EVT, SBP_PERIODIC_EVT_PERIOD);

        // Set timer for param update event
        tmos_start_task(Peripheral_TaskID, SBP_PARAM_UPDATE_EVT, SBP_PARAM_UPDATE_DELAY);

        // Start read rssi
        tmos_start_task(Peripheral_TaskID, SBP_READ_RSSI_EVT, SBP_READ_RSSI_EVT_PERIOD);

        PRINT("Conn %x - Int %x \n", event->connectionHandle, event->connInterval);

        /* MTU 由 Central 发起；外设仅响应 ATT_MTU_UPDATED_EVENT，避免双向同时 Exchange 导致 0x3e */
    }
}

/*********************************************************************
 * @fn      Peripheral_LinkTerminated
 *
 * @brief   Process link terminated.
 *
 * @param   pEvent - event to process
 *
 * @return  none
 */
static void Peripheral_LinkTerminated(gapRoleEvent_t *pEvent)
{
    gapTerminateLinkEvent_t *event = (gapTerminateLinkEvent_t *)pEvent;

    if(event->connectionHandle == peripheralConnList.connHandle)
    {
        peripheralConnList.connHandle = GAP_CONNHANDLE_INIT;
        peripheralConnList.connInterval = 0;
        peripheralConnList.connSlaveLatency = 0;
        peripheralConnList.connTimeout = 0;
        tmos_stop_task(Peripheral_TaskID, SBP_PERIODIC_EVT);
        tmos_stop_task(Peripheral_TaskID, SBP_READ_RSSI_EVT);

        /* ???????????? + ???????????????????????? */
        g_sampling_on = 0U;
        g_session_id  = 0U;
        g_pulse_id    = 0U;
        tmos_stop_task(Peripheral_TaskID, SBP_PPG_SAMPLE_EVT);

        // Restart advertising
        {
            uint8_t advertising_enable = TRUE;
            GAPRole_SetParameter(GAPROLE_ADVERT_ENABLED, sizeof(uint8_t), &advertising_enable);
        }
    }
    else
    {
        PRINT("ERR..\n");
    }
}

/*********************************************************************
 * @fn      peripheralRssiCB
 *
 * @brief   RSSI callback.
 *
 * @param   connHandle - connection handle
 * @param   rssi - RSSI
 *
 * @return  none
 */
static void peripheralRssiCB(uint16_t connHandle, int8_t rssi)
{
    PRINT("RSSI -%d dB Conn  %x \n", -rssi, connHandle);
}

/*********************************************************************
 * @fn      peripheralParamUpdateCB
 *
 * @brief   Parameter update complete callback
 *
 * @param   connHandle - connect handle
 *          connInterval - connect interval
 *          connSlaveLatency - connect slave latency
 *          connTimeout - connect timeout
 *
 * @return  none
 */
static void peripheralParamUpdateCB(uint16_t connHandle, uint16_t connInterval,
                                    uint16_t connSlaveLatency, uint16_t connTimeout)
{
    if(connHandle == peripheralConnList.connHandle)
    {
        peripheralConnList.connInterval = connInterval;
        peripheralConnList.connSlaveLatency = connSlaveLatency;
        peripheralConnList.connTimeout = connTimeout;

        PRINT("Update %x - Int %x \n", connHandle, connInterval);
    }
    else
    {
        PRINT("ERR..\n");
    }
}

/*********************************************************************
 * @fn      peripheralStateNotificationCB
 *
 * @brief   Notification from the profile of a state change.
 *
 * @param   newState - new state
 *
 * @return  none
 */
static void peripheralStateNotificationCB(gapRole_States_t newState, gapRoleEvent_t *pEvent)
{
    switch(newState & GAPROLE_STATE_ADV_MASK)
    {
        case GAPROLE_STARTED:
        {
            uint8_t bdAddr[B_ADDR_LEN];

            GAPRole_GetParameter(GAPROLE_BD_ADDR, bdAddr);
            PRINT("Finger init, BLE addr %02x:%02x:%02x:%02x:%02x:%02x\n",
                  bdAddr[0], bdAddr[1], bdAddr[2], bdAddr[3], bdAddr[4], bdAddr[5]);
            break;
        }

        case GAPROLE_ADVERTISING:
            if(pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT)
            {
                Peripheral_LinkTerminated(pEvent);
                PRINT("Disconnected.. Reason:%x\n", pEvent->linkTerminate.reason);
                PRINT("Advertising..\n");
            }
            else if(pEvent->gap.opcode == GAP_MAKE_DISCOVERABLE_DONE_EVENT)
            {
                PRINT("Advertising..\n");
            }
            break;

        case GAPROLE_CONNECTED:
            if(pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT)
            {
                Peripheral_LinkEstablished(pEvent);
                PRINT("Connected..\n");
            }
            break;

        case GAPROLE_CONNECTED_ADV:
            if(pEvent->gap.opcode == GAP_MAKE_DISCOVERABLE_DONE_EVENT)
            {
                PRINT("Connected Advertising..\n");
            }
            break;

        case GAPROLE_WAITING:
            if(pEvent->gap.opcode == GAP_END_DISCOVERABLE_DONE_EVENT)
            {
                PRINT("Waiting for advertising..\n");
            }
            else if(pEvent->gap.opcode == GAP_LINK_TERMINATED_EVENT)
            {
                Peripheral_LinkTerminated(pEvent);
                PRINT("Disconnected.. Reason:%x\n", pEvent->linkTerminate.reason);
            }
            else if(pEvent->gap.opcode == GAP_LINK_ESTABLISHED_EVENT)
            {
                if(pEvent->gap.hdr.status != SUCCESS)
                {
                    PRINT("Waiting for advertising..\n");
                }
                else
                {
                    PRINT("Error..\n");
                }
            }
            else
            {
                PRINT("Error..%x\n", pEvent->gap.opcode);
            }
            break;

        case GAPROLE_ERROR:
            PRINT("Error..\n");
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @fn      performPeriodicTask
 *
 * @brief   Perform a periodic application task. This function gets
 *          called every five seconds as a result of the SBP_PERIODIC_EVT
 *          TMOS event. In this example, the value of the third
 *          characteristic in the SimpleGATTProfile service is retrieved
 *          from the profile, and then copied into the value of the
 *          the fourth characteristic.
 *
 * @param   none
 *
 * @return  none
 */
static void performPeriodicTask(void)
{
    /* ???? HR/SpO2 ??? max30102_measure.c ???? Peripheral_FingerNotifyHrSpo2Pwv???? UART JSON ?????? */
}

/*********************************************************************
 * @fn      peripheralChar4Notify
 *
 * @brief   Prepare and send simpleProfileChar4 notification
 *
 * @param   pValue - data to notify
 *          len - length of data
 *
 * @return  none
 */
static void peripheralChar4Notify(uint8_t *pValue, uint16_t len)
{
    attHandleValueNoti_t noti;
    bStatus_t           st;

    if(peripheralConnList.connHandle == GAP_CONNHANDLE_INIT)
    {
        return;
    }
    if(len > (peripheralMTU - 3U))
    {
        PRINT("Finger: Char4 notify too large (len=%u mtu=%u)\n",
              (unsigned int)len, (unsigned)peripheralMTU);
        return;
    }
    noti.len = len;
    noti.pValue = GATT_bm_alloc(peripheralConnList.connHandle, ATT_HANDLE_VALUE_NOTI, noti.len, NULL, 0);
    if(noti.pValue)
    {
        tmos_memcpy(noti.pValue, pValue, noti.len);
        st = simpleProfile_Notify(peripheralConnList.connHandle, &noti);
        if(st != SUCCESS)
        {
            /* ???? 0x12 bleIncorrectMode????????? CCCD ?????????????????????? */
            PRINT("Finger: Char4 notify fail status=%02X\n", st);
            GATT_bm_free((gattMsg_t *)&noti, ATT_HANDLE_VALUE_NOTI);
        }
    }
}

/*********************************************************************
 * @fn      Peripheral_FingerNotifyJson
 *
 * @brief   ???? HR/SpO2 ??? JSON ??? Char4 Notify ????? host (ESP32-C3)
 *          JSON: {"id":N,"ts":T,"b_idx":I,"beat_ts":B,"hr":H,"spo2":S}
 *          beat_ts = b_idx * 20 ms??????????????????????????????????
 */
void Peripheral_FingerNotifyJson(uint8_t hr, uint8_t spo2, uint16_t beat_idx, uint32_t beat_ts,
                                 const ppg_wf_compact_t *wf)
{
    char     buf[SIMPLEPROFILE_CHAR4_LEN];
    int      n;
    int      wf_std = 0;
    int      wf_prom = 0;
    unsigned int wf_df = 0;
    unsigned int wf_pec = 0;

    (void)beat_ts;

    if(wf != NULL)
    {
        wf_std  = (int)wf->std_x10;
        wf_prom = (int)wf->prom_x100;
        wf_df   = (unsigned int)wf->df_x10;
        wf_pec  = (unsigned int)wf->pec_x100;
    }

    if(peripheralConnList.connHandle == GAP_CONNHANDLE_INIT)
    {
        return;
    }
    if(g_session_id == 0U)
    {
        /* ??????? host ?? '1' ??????????? */
        return;
    }

    /* 紧凑 JSON（~55B），去掉 ts/beat_ts；wf 用 "w":[std,prom,df,pec] 适配 MTU */
    if(wf != NULL)
    {
        n = snprintf(buf, sizeof(buf),
                     "{\"id\":%u,\"b_idx\":%u,\"hr\":%u,\"spo2\":%u,"
                     "\"pi\":%u,\"w\":[%d,%d,%u,%u]}",
                     (unsigned int)g_pulse_id,
                     (unsigned int)beat_idx,
                     (unsigned int)hr,
                     (unsigned int)spo2,
                     (unsigned int)wf->pi_x10,
                     wf_std, wf_prom, wf_df, wf_pec);
        if(n > 0 && n < (int)sizeof(buf) && n <= (int)(peripheralMTU - 3U))
        {
            peripheralChar4Notify((uint8_t *)buf, (uint16_t)n);
            return;
        }
    }

    n = snprintf(buf, sizeof(buf),
                 "{\"id\":%u,\"b_idx\":%u,\"hr\":%u,\"spo2\":%u}",
                 (unsigned int)g_pulse_id,
                 (unsigned int)beat_idx,
                 (unsigned int)hr,
                 (unsigned int)spo2);
    if(n <= 0 || n >= (int)sizeof(buf))
    {
        PRINT("Finger: JSON encode fail n=%d\n", n);
        return;
    }
    peripheralChar4Notify((uint8_t *)buf, (uint16_t)n);
}

/*********************************************************************
 * @fn      simpleProfileChangeCB
 *
 * @brief   Callback from SimpleBLEProfile indicating a value change
 *
 * @param   paramID - parameter ID of the value that was changed.
 *          pValue - pointer to data that was changed
 *          len - length of data
 *
 * @return  none
 */
static void simpleProfileChangeCB(uint8_t paramID, uint8_t *pValue, uint16_t len)
{
    switch(paramID)
    {
        case SIMPLEPROFILE_CHAR1:
        {
            /*
             * Char1 ???? pacing????? B????
             *   '1'      : START + init??pulse_id=1????? 'S' ???????
             *   '2'      : STOP
             *   'N'      : pulse_id++????? 'S'
             *   'S'      : T0 = now + PPG_SYNC_T0_DELAY_MS ??? arm 150 ??
             */
            uint8_t cmd;
            if(len < 1)
            {
                return;
            }
            cmd = pValue[0];
            PRINT("[CHAR1] recv cmd='%c' (0x%02X)\n", cmd, cmd);

            if(cmd == '2')
            {
                if(g_sampling_on)
                {
                    g_sampling_on = 0U;
                    g_pulse_id = 0U;
                    tmos_stop_task(Peripheral_TaskID, SBP_PPG_SAMPLE_EVT);
                    PRINT("[CHAR1] STOP sampling (session id=%u)\n", (unsigned int)g_session_id);
                }
            }
            else if(cmd >= '1' && cmd <= '9')
            {
                g_session_id = (uint8_t)(cmd - '0');
                g_pulse_id = 1U;
                max30102_measure_on_sync_pulse(g_pulse_id);
                if(!g_sampling_on)
                {
                    max30102_measure_init();
                    g_sampling_on = 1U;
                    tmos_start_task(Peripheral_TaskID, SBP_PPG_SAMPLE_EVT,
                                    MS1_TO_SYSTEM_TIME(PPG_SAMPLE_INTERVAL_MS));
                }
                PRINT("[CHAR1] START sampling session id=%u pulse_id=%u (await 'S')\n",
                      (unsigned int)g_session_id, (unsigned int)g_pulse_id);
            }
            else if(cmd == 'N')
            {
                if(g_sampling_on && (g_pulse_id < 65535U))
                {
                    g_pulse_id++;
                    PRINT("[CHAR1] NEXT pulse_id=%u (await 'S')\n",
                          (unsigned int)g_pulse_id);
                }
            }
            else if(cmd == 'S')
            {
                if(g_sampling_on)
                {
                    max30102_measure_on_sync_pulse(g_pulse_id);
                    max30102_measure_schedule_sync_window(PPG_SYNC_T0_DELAY_MS);
                    PRINT("[CHAR1] SYNC arm pulse_id=%u\n", (unsigned int)g_pulse_id);
                }
            }
            else
            {
                PRINT("[CHAR1] ignore unknown cmd\n");
            }
            break;
        }

        case SIMPLEPROFILE_CHAR3:
        {
            uint8_t newValue[SIMPLEPROFILE_CHAR3_LEN];
            tmos_memcpy(newValue, pValue, len);
            PRINT("profile ChangeCB CHAR3..\n");
            break;
        }

        default:
            // should not reach here!
            break;
    }
}

/*********************************************************************
*********************************************************************/
