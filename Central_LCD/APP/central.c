/********************************** (C) COPYRIGHT *******************************
* File Name          : central.c
* Author             : WCH
* Version            : V1.2
* Date               : 2020/08/06
 * Description        : BLE Central ????????????????????? MAC ?????? Finger/Wrist??
 *                      GATT ??????? Char1='1' ???????????? Char4 JSON?????? PWV ?? UART1 ???
 *********************************************************************************
 * Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
 * Attention: This software (modified or not) and binary are used for 
 * microcontroller manufactured by Nanjing Qinheng Microelectronics.
 *******************************************************************************/

/*********************************************************************
 * INCLUDES
 */
#include "CONFIG.h"
#include "gattprofile.h"
#include "pwv.h"
#include "hr_fusion.h"
#include "ui.h"
#include "central.h"
#include "measure_ctrl.h"
#include "alarm_mgr.h"
#include <stdio.h>

/*********************************************************************
 * CONSTANTS
 */
#define DEFAULT_MAX_SCAN_RES                16
#define DEFAULT_SCAN_DURATION               1600
#define DEFAULT_MIN_CONNECTION_INTERVAL     24
#define DEFAULT_MAX_CONNECTION_INTERVAL     120
#define DEFAULT_CONNECTION_TIMEOUT          800
#define DEFAULT_DISCOVERY_START_DELAY       32U
#define DEFAULT_DISCOVERY_MODE              DEVDISC_MODE_ALL
#define DEFAULT_DISCOVERY_ACTIVE_SCAN       TRUE
#define DEFAULT_DISCOVERY_WHITE_LIST        FALSE
#define DEFAULT_LINK_HIGH_DUTY_CYCLE        FALSE
#define DEFAULT_LINK_WHITE_LIST             FALSE
#define DEFAULT_UPDATE_MIN_CONN_INTERVAL    24
#define DEFAULT_UPDATE_MAX_CONN_INTERVAL    120
#define DEFAULT_UPDATE_SLAVE_LATENCY        0
#define DEFAULT_UPDATE_CONN_TIMEOUT         800
#define DEFAULT_PASSCODE                    0
#define DEFAULT_PAIRING_MODE                GAPBOND_PAIRING_MODE_WAIT_FOR_REQ
#define DEFAULT_MITM_MODE                   FALSE
#define DEFAULT_BONDING_MODE                FALSE
#define DEFAULT_IO_CAPABILITIES             GAPBOND_IO_CAP_NO_INPUT_NO_OUTPUT
#define DEFAULT_PARAM_UPDATE_DELAY          2400
#define CENTRAL_RECONNECT_DELAY             1600
#define CENTRAL_RECONNECT_BACKOFF_MAX       9600
#define ESTABLISH_LINK_TIMEOUT              4800
#define SIMPLE_SVC_SEARCH_MAX_SPAN          96U
#define CHAR4_DISC_MAX_RETRIES              4U
#define SVC_DISC_MAX_RETRIES                8U
#define SVC_DISC_FAIL_BACKOFF_TICKS         160U
#define CENTRAL_BATCH_STEP_WRIST_1          0U
#define CENTRAL_BATCH_STEP_FINGER_1         1U
#define CENTRAL_BATCH_STEP_WRIST_S          2U
#define CENTRAL_BATCH_STEP_FINGER_S         3U
#define CENTRAL_STOP_STEP_WRIST             0U
#define CENTRAL_STOP_STEP_FINGER            1U

enum { NODE_FINGER = 0, NODE_WRIST = 1, NODE_COUNT = 2 };

/* 外设 MAC地址 */
static const uint8_t PeerFingerAddr[B_ADDR_LEN] = {0x28, 0xD2, 0xD0, 0x62, 0x32, 0xDC};
static const uint8_t PeerWristAddr[B_ADDR_LEN]  = {0x15, 0xBD, 0xD0, 0x62, 0x32, 0xDC};

enum
{
    BLE_DISC_STATE_IDLE,
    BLE_DISC_STATE_SVC,
    BLE_DISC_STATE_CHAR1,
    BLE_DISC_STATE_CHAR4
};

typedef enum
{
    CENTRAL_WRITE_NONE = 0,
    CENTRAL_WRITE_CCCD,
    CENTRAL_WRITE_CHAR1,
    CENTRAL_WRITE_CHAR1_BATCH,
    CENTRAL_WRITE_CHAR1_NEXT,
    CENTRAL_WRITE_CHAR1_STOP
} centralWriteOp_t;

typedef struct
{
    const char *name;
    uint8_t     addr[B_ADDR_LEN];
    uint8_t     addrType;
    uint8_t     addrKnown;
    uint8_t     connecting;
    uint8_t     connected;
    uint8_t     ready;
    uint8_t     char4On;
    uint16_t    connHandle;
    uint16_t    svcStartHdl;
    uint16_t    svcEndHdl;
    uint16_t    char1ValHdl;
    uint16_t    char4ValHdl;
    uint16_t    char4CccdHdl;
    uint8_t     discState;
    uint8_t     char4DiscRetries;
    uint8_t     svcDiscRetries;
    uint8_t     discPending;
    uint8_t     discSchedulePending;
    uint8_t     discRetryPending;
    int8_t      rssi;
} centralNode_t;

/*********************************************************************
 * LOCAL VARIABLES
 */
static uint8_t           centralTaskId;
static centralNode_t     centralNodes[NODE_COUNT] = {
    { "Finger", {0}, 0, 0, 0, 0, 0, 0, GAP_CONNHANDLE_INIT, 0, 0, 0, 0, 0, BLE_DISC_STATE_IDLE, 0, 0, 0, 0, 0, 0 },
    { "Wrist",  {0}, 0, 0, 0, 0, 0, 0, GAP_CONNHANDLE_INIT, 0, 0, 0, 0, 0, BLE_DISC_STATE_IDLE, 0, 0, 0, 0, 0, 0 }
};
static int8_t            centralActiveNode = -1;
static int8_t            centralConnectingIdx = -1;
static uint8_t           centralProcedureInProgress = FALSE;
static centralWriteOp_t  centralPendingWrite = CENTRAL_WRITE_NONE;
static int8_t            centralNextWriteNode = -1;
static uint8_t           centralParamUpdate = TRUE;
static uint8_t           centralReconnectFails = 0U;
static uint8_t           centralPacedStartDone = FALSE;
static uint8_t           centralBatchStep = 0U;
static uint8_t           centralMeasureWanted = FALSE;
static uint8_t           centralStopPending = FALSE;
static uint8_t           centralStopStep = CENTRAL_STOP_STEP_WRIST;
static uint8_t           centralScanRes;
static uint8_t           s_wasBothLinked = 0U;
static uint8_t           s_prevWLink = 0U;
static uint8_t           s_prevFLink = 0U;
/* 允许发起“第二条”ACL：首节点 ready 后延迟置位，避免 0x3e 互踢 */
static uint8_t           s_allowSecondLink = 0U;
static gapDevRec_t       centralDevList[DEFAULT_MAX_SCAN_RES];

/*********************************************************************
 * LOCAL FUNCTIONS
 */
static void     centralProcessGATTMsg(gattMsgEvent_t *pMsg);
static void     centralRssiCB(uint16_t connHandle, int8_t rssi);
static void     centralEventCB(gapRoleEvent_t *pEvent);
static void     centralHciMTUChangeCB(uint16_t connHandle, uint16_t maxTxOctets, uint16_t maxRxOctets);
static void     centralPasscodeCB(uint8_t *deviceAddr, uint16_t connectionHandle,
                                  uint8_t uiInputs, uint8_t uiOutputs);
static void     centralPairStateCB(uint16_t connHandle, uint8_t state, uint8_t status);
static void     central_ProcessTMOSMsg(tmos_event_hdr_t *pMsg);
static void     centralGATTDiscoveryEvent(gattMsgEvent_t *pMsg, uint8_t nodeIdx);
static void     centralKickDiscWork(void);
static void     centralOnProcedureIdle(void);
static void     centralStartDiscovery(uint8_t nodeIdx);
static int8_t   centralFindNodeByConnHandle(uint16_t connHandle);
static int8_t   centralFindNodeByAddr(uint8_t *pAddr);
static uint8_t  centralBothAddrKnown(void);
static uint8_t  centralBothAddrDistinct(void);
static uint8_t  centralBothReady(void);
static void     centralTryConnectNext(void);
static void     centralStartScan(void);
static void     centralScheduleReconnect(void);
static void     centralResetNodeRuntime(centralNode_t *n);
static void     centralOnNodeReady(uint8_t nodeIdx);
static void     centralScheduleDiscovery(uint8_t nodeIdx, uint16_t delayTicks);
static void     centralDiscoverCharByUuid(uint8_t nodeIdx, uint16_t charUuid);
static void     centralWriteChar4CccdEnable(uint8_t nodeIdx);
static void     centralWriteChar1Cmd(uint8_t nodeIdx, uint8_t cmd);
static void     centralAdvanceChar1Batch(void);
static void     centralAdvanceChar1Next(void);
static void     centralTryStartBothSampling(void);
static void     centralBeginStopSequence(void);
static void     centralAdvanceStopSequence(void);
static void     centralSendPulseNext(void);
static void     centralMaybeAdvanceAfterPulse(void);
static void     centralHandleNotify(gattMsgEvent_t *pMsg, int8_t nodeIdx);
static uint8_t  centralIsDiscoveryGattMsg(gattMsgEvent_t *pMsg);
static uint16_t centralCharValueHandleFromReadByTypeRsp(gattMsgEvent_t *pMsg);
static void     centralClampSvcSearchWindow(centralNode_t *n);
static void     centralInitPeerAddrs(void);
static void     centralAddDeviceInfo(uint8_t *pAddr, uint8_t addrType);
static void     centralResolveTargetsFromScan(void);
static void     centralClearNodePresence(uint8_t nodeIdx);
static void     centralUpdateLinkParams(uint8_t nodeIdx);
static void     centralUiUpdateLink(void);

static gapCentralRoleCB_t centralRoleCB = {
    centralRssiCB,
    centralEventCB,
    centralHciMTUChangeCB
};

static gapBondCBs_t centralBondCB = {
    centralPasscodeCB,
    centralPairStateCB
};

/*********************************************************************
 * PUBLIC FUNCTIONS
 */
void Central_Init()
{
    centralTaskId = TMOS_ProcessEventRegister(Central_ProcessEvent);
    centralInitPeerAddrs();

    GAP_SetParamValue(TGAP_DISC_SCAN, DEFAULT_SCAN_DURATION);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MIN, DEFAULT_MIN_CONNECTION_INTERVAL);
    GAP_SetParamValue(TGAP_CONN_EST_INT_MAX, DEFAULT_MAX_CONNECTION_INTERVAL);
    GAP_SetParamValue(TGAP_CONN_EST_SUPERV_TIMEOUT, DEFAULT_CONNECTION_TIMEOUT);

    {
        uint32_t passkey = DEFAULT_PASSCODE;
        uint8_t  pairMode = DEFAULT_PAIRING_MODE;
        uint8_t  mitm = FALSE;
        uint8_t  ioCap = DEFAULT_IO_CAPABILITIES;
        uint8_t  bonding = DEFAULT_BONDING_MODE;

        GAPBondMgr_SetParameter(GAPBOND_CENT_DEFAULT_PASSCODE, sizeof(uint32_t), &passkey);
        GAPBondMgr_SetParameter(GAPBOND_CENT_PAIRING_MODE, sizeof(uint8_t), &pairMode);
        GAPBondMgr_SetParameter(GAPBOND_CENT_MITM_PROTECTION, sizeof(uint8_t), &mitm);
        GAPBondMgr_SetParameter(GAPBOND_CENT_IO_CAPABILITIES, sizeof(uint8_t), &ioCap);
        GAPBondMgr_SetParameter(GAPBOND_CENT_BONDING_ENABLED, sizeof(uint8_t), &bonding);
    }

    GATT_InitClient();
    GATT_RegisterForInd(centralTaskId);
    tmos_set_event(centralTaskId, START_DEVICE_EVT);
}

uint16_t Central_ProcessEvent(uint8_t task_id, uint16_t events)
{
    if(events & SYS_EVENT_MSG)
    {
        uint8_t *pMsg;

        if((pMsg = tmos_msg_receive(centralTaskId)) != NULL)
        {
            central_ProcessTMOSMsg((tmos_event_hdr_t *)pMsg);
            tmos_msg_deallocate(pMsg);
        }
        return (events ^ SYS_EVENT_MSG);
    }

    if(events & START_DEVICE_EVT)
    {
        GAPRole_CentralStartDevice(centralTaskId, &centralBondCB, &centralRoleCB);
        return (events ^ START_DEVICE_EVT);
    }

    if(events & ESTABLISH_LINK_TIMEOUT_EVT)
    {
        if(centralConnectingIdx >= 0 && centralConnectingIdx < NODE_COUNT)
        {
            PRINT("%s connect timeout, rescan\n",
                  centralNodes[centralConnectingIdx].name);
            centralNodes[centralConnectingIdx].connecting = 0;
            centralClearNodePresence((uint8_t)centralConnectingIdx);
            if(centralReconnectFails < 12U)
            {
                centralReconnectFails++;
            }
        }
        centralConnectingIdx = -1;
        centralUiUpdateLink();
        /* 已有一路时走退避重连，避免门闩关闭导致永久不连第二路 */
        if(centralNodes[NODE_FINGER].connected || centralNodes[NODE_WRIST].connected)
        {
            centralScheduleReconnect();
        }
        else if(centralBothAddrDistinct())
        {
            centralTryConnectNext();
        }
        else
        {
            centralScheduleReconnect();
        }
        return (events ^ ESTABLISH_LINK_TIMEOUT_EVT);
    }

    if(events & START_SVC_DISCOVERY_EVT)
    {
        centralKickDiscWork();
        return (events ^ START_SVC_DISCOVERY_EVT);
    }

    if(events & START_CHAR4_DISC_EVT)
    {
        centralKickDiscWork();
        return (events ^ START_CHAR4_DISC_EVT);
    }

    if(events & START_PARAM_UPDATE_EVT)
    {
        if(centralBothReady())
        {
            centralUpdateLinkParams(NODE_WRIST);
            centralUpdateLinkParams(NODE_FINGER);
        }
        return (events ^ START_PARAM_UPDATE_EVT);
    }

    if(events & START_RECONNECT_EVT)
    {
        if(centralNodes[NODE_FINGER].connected || centralNodes[NODE_WRIST].connected)
        {
            /* 已有一路时，重连另一路也要带门闩，避免连发 Establish */
            s_allowSecondLink = 1U;
            centralTryConnectNext();
        }
        else
        {
            s_allowSecondLink = 0U;
            centralStartScan();
        }
        return (events ^ START_RECONNECT_EVT);
    }

    if(events & START_SECOND_LINK_EVT)
    {
        s_allowSecondLink = 1U;
        PRINT("Second-link window open, try next peer\n");
        centralTryConnectNext();
        return (events ^ START_SECOND_LINK_EVT);
    }

    return 0;
}

static void central_ProcessTMOSMsg(tmos_event_hdr_t *pMsg)
{
    if(pMsg->event == GATT_MSG_EVENT)
    {
        centralProcessGATTMsg((gattMsgEvent_t *)pMsg);
    }
}

static int8_t centralFindNodeByConnHandle(uint16_t connHandle)
{
    uint8_t i;

    if(connHandle == GAP_CONNHANDLE_INIT)
    {
        return -1;
    }
    for(i = 0; i < NODE_COUNT; i++)
    {
        if(centralNodes[i].connected && centralNodes[i].connHandle == connHandle)
        {
            return (int8_t)i;
        }
    }
    return -1;
}

static int8_t centralFindNodeByAddr(uint8_t *pAddr)
{
    uint8_t i;

    for(i = 0; i < NODE_COUNT; i++)
    {
        if(centralNodes[i].addrKnown &&
           tmos_memcmp(centralNodes[i].addr, pAddr, B_ADDR_LEN))
        {
            return (int8_t)i;
        }
    }
    return -1;
}

static uint8_t centralBothAddrKnown(void)
{
    return centralNodes[NODE_FINGER].addrKnown && centralNodes[NODE_WRIST].addrKnown;
}

static uint8_t centralBothAddrDistinct(void)
{
    if(!centralBothAddrKnown())
    {
        return 0U;
    }
    /* tmos_memcmp ???? TRUE=?????FALSE=????????? memcmp ???? */
    return tmos_memcmp(centralNodes[NODE_FINGER].addr,
                       centralNodes[NODE_WRIST].addr,
                       B_ADDR_LEN) ? 0U : 1U;
}

static uint8_t centralBothReady(void)
{
    return centralNodes[NODE_FINGER].ready && centralNodes[NODE_WRIST].ready;
}

static void centralUiUpdateLink(void)
{
    /* 仅 GATT ready 才算“已连接”，避免 ACL 刚连上、发现失败又断时界面闪一下 */
    uint8_t w = centralNodes[NODE_WRIST].ready ? 1U : 0U;
    uint8_t f = centralNodes[NODE_FINGER].ready ? 1U : 0U;
    uint8_t wc = (!w && (centralNodes[NODE_WRIST].connecting ||
                         centralNodes[NODE_WRIST].connected)) ? 1U : 0U;
    uint8_t fc = (!f && (centralNodes[NODE_FINGER].connecting ||
                         centralNodes[NODE_FINGER].connected)) ? 1U : 0U;

    if(w && f && !s_wasBothLinked) {
        AlarmMgr_Raise(ALARM_LINK_OK);
    }
    s_prevWLink = w;
    s_prevFLink = f;
    s_wasBothLinked = (w && f) ? 1U : 0U;
    UI_SetLink(w, f, wc, fc);
}

static void centralResetNodeRuntime(centralNode_t *n)
{
    n->connecting = 0;
    n->connected = 0;
    n->ready = 0;
    n->char4On = 0;
    n->connHandle = GAP_CONNHANDLE_INIT;
    n->svcStartHdl = 0;
    n->svcEndHdl = 0;
    n->char1ValHdl = 0;
    n->char4ValHdl = 0;
    n->char4CccdHdl = 0;
    n->discState = BLE_DISC_STATE_IDLE;
    n->char4DiscRetries = 0;
    n->svcDiscRetries = 0;
    n->discPending = 0;
    n->discSchedulePending = 0;
    n->discRetryPending = 0;
}

static uint8_t centralConnectInProgress(void)
{
    uint8_t i;

    if(centralConnectingIdx >= 0)
    {
        return 1U;
    }
    for(i = 0; i < NODE_COUNT; i++)
    {
        if(centralNodes[i].connecting)
        {
            return 1U;
        }
    }
    return 0U;
}

static void centralInitPeerAddrs(void)
{
    uint8_t i;

    tmos_memcpy(centralNodes[NODE_FINGER].addr, PeerFingerAddr, B_ADDR_LEN);
    tmos_memcpy(centralNodes[NODE_WRIST].addr, PeerWristAddr, B_ADDR_LEN);
    for(i = 0; i < NODE_COUNT; i++)
    {
        centralNodes[i].addrType = ADDRTYPE_PUBLIC;
        centralNodes[i].addrKnown = 1U;
    }
}

static void centralClearNodePresence(uint8_t nodeIdx)
{
    if(nodeIdx >= NODE_COUNT)
    {
        return;
    }
    centralNodes[nodeIdx].rssi = 0;
}

static void centralUpdateLinkParams(uint8_t nodeIdx)
{
    centralNode_t *n;

    if(nodeIdx >= NODE_COUNT)
    {
        return;
    }
    n = &centralNodes[nodeIdx];
    if(!n->connected || n->connHandle == GAP_CONNHANDLE_INIT)
    {
        return;
    }
    GAPRole_UpdateLink(n->connHandle,
                       DEFAULT_UPDATE_MIN_CONN_INTERVAL,
                       DEFAULT_UPDATE_MAX_CONN_INTERVAL,
                       DEFAULT_UPDATE_SLAVE_LATENCY,
                       DEFAULT_UPDATE_CONN_TIMEOUT);
}

static void centralScheduleReconnect(void)
{
    uint16_t delay = CENTRAL_RECONNECT_DELAY;

    if(centralReconnectFails > 0U)
    {
        /* 0x3e 连发时拉长退避，避免第二条 ACL 打爆射频时序 */
        delay += (uint16_t)centralReconnectFails * 900U;
        if(delay > CENTRAL_RECONNECT_BACKOFF_MAX)
        {
            delay = CENTRAL_RECONNECT_BACKOFF_MAX;
        }
    }
    if(centralProcedureInProgress)
    {
        delay += 800U;
    }
    /* 已有一路时，重连前先关掉 SECOND_LINK 门闩，等 RECONNECT 到期再开 */
    if(centralNodes[NODE_FINGER].connected || centralNodes[NODE_WRIST].connected)
    {
        s_allowSecondLink = 0U;
        tmos_stop_task(centralTaskId, START_SECOND_LINK_EVT);
    }
    tmos_stop_task(centralTaskId, START_RECONNECT_EVT);
    tmos_start_task(centralTaskId, START_RECONNECT_EVT, delay);
}

static void centralStartScan(void)
{
    uint8_t i;
    uint8_t known = 0;

    if(centralNodes[NODE_WRIST].connected || centralNodes[NODE_FINGER].connected)
    {
        return;
    }

    (void)GAPRole_CentralCancelDiscovery();
    centralScanRes = 0;

    for(i = 0; i < NODE_COUNT; i++)
    {
        if(centralNodes[i].addrKnown)
        {
            known++;
        }
    }

    if(known > 0U)
    {
        PRINT("Discovering... (%u/%u in range)\n",
              (unsigned int)known, (unsigned int)NODE_COUNT);
    }
    else
    {
        PRINT("Discovering...\n");
    }
    GAPRole_CentralStartDiscovery(DEFAULT_DISCOVERY_MODE,
                                  DEFAULT_DISCOVERY_ACTIVE_SCAN,
                                  DEFAULT_DISCOVERY_WHITE_LIST);
}

static void centralTryConnectNext(void)
{
    uint8_t i;
    uint8_t upCount = 0U;
    static const uint8_t s_connectOrder[NODE_COUNT] = { NODE_WRIST, NODE_FINGER };

    if(centralConnectInProgress())
    {
        return;
    }
    if(centralProcedureInProgress)
    {
        return;
    }

    /* 等当前已连节点 GATT 就绪后再开第二条 ACL，避免 reason=3e 互踢 */
    for(i = 0; i < NODE_COUNT; i++)
    {
        if(centralNodes[i].connected && !centralNodes[i].ready)
        {
            return;
        }
        if(centralNodes[i].connected && centralNodes[i].ready)
        {
            upCount++;
        }
    }

    if(!centralBothAddrDistinct())
    {
        return;
    }

    /* 已有一路 ready：必须等 SECOND_LINK 门闩，禁止立即连第二路 */
    if(upCount >= 1U && !s_allowSecondLink)
    {
        return;
    }

    for(i = 0; i < NODE_COUNT; i++)
    {
        uint8_t        idx = s_connectOrder[i];
        centralNode_t *n = &centralNodes[idx];

        if(n->addrKnown && !n->connected && !n->connecting)
        {
            if(upCount >= 1U)
            {
                s_allowSecondLink = 0U; /* 消耗一次门闩 */
            }
            (void)GAPRole_CentralCancelDiscovery();
            centralConnectingIdx = (int8_t)idx;
            n->connecting = 1;
            GAPRole_CentralEstablishLink(DEFAULT_LINK_HIGH_DUTY_CYCLE,
                                         DEFAULT_LINK_WHITE_LIST,
                                         n->addrType,
                                         n->addr);
            tmos_start_task(centralTaskId, ESTABLISH_LINK_TIMEOUT_EVT, ESTABLISH_LINK_TIMEOUT);
            centralUiUpdateLink();
            PRINT("Connecting %s...\n", n->name);
            return;
        }
    }

    if(centralBothReady())
    {
        PRINT("All nodes ready, collecting PPG data\n");
    }
}

static void centralScheduleDiscovery(uint8_t nodeIdx, uint16_t delayTicks)
{
    centralNode_t *n = &centralNodes[nodeIdx];

    if(!n->connected || n->connHandle == GAP_CONNHANDLE_INIT)
    {
        return;
    }
    centralActiveNode = (int8_t)nodeIdx;
    tmos_stop_task(centralTaskId, START_SVC_DISCOVERY_EVT);
    n->discSchedulePending = 1U;
    tmos_start_task(centralTaskId, START_SVC_DISCOVERY_EVT, delayTicks);
}

static void centralKickDiscWork(void)
{
    uint8_t i;

    if(centralProcedureInProgress)
    {
        return;
    }

    for(i = 0; i < NODE_COUNT; i++)
    {
        centralNode_t *n = &centralNodes[i];

        if(n->discSchedulePending && n->connected && n->connHandle != GAP_CONNHANDLE_INIT)
        {
            n->discSchedulePending = 0U;
            centralStartDiscovery(i);
            return;
        }
    }

    for(i = 0; i < NODE_COUNT; i++)
    {
        centralNode_t *n = &centralNodes[i];
        uint16_t       charUuid = 0U;

        if(!n->discRetryPending || !n->connected || n->connHandle == GAP_CONNHANDLE_INIT)
        {
            continue;
        }
        if(n->discState == BLE_DISC_STATE_CHAR1)
        {
            charUuid = SIMPLEPROFILE_CHAR1_UUID;
        }
        else if(n->discState == BLE_DISC_STATE_CHAR4)
        {
            charUuid = SIMPLEPROFILE_CHAR4_UUID;
        }
        else
        {
            n->discRetryPending = 0U;
            continue;
        }

        n->discRetryPending = 0U;
        centralDiscoverCharByUuid(i, charUuid);
        return;
    }
}

static void centralOnProcedureIdle(void)
{
    if(centralProcedureInProgress)
    {
        return;
    }
    if(centralStopPending)
    {
        centralStopPending = FALSE;
        centralPendingWrite = CENTRAL_WRITE_NONE;
        centralNextWriteNode = -1;
        centralBatchStep = CENTRAL_BATCH_STEP_WRIST_1;
        centralBeginStopSequence();
        return;
    }
    centralKickDiscWork();
    centralTryConnectNext();
    if(centralMeasureWanted)
    {
        centralTryStartBothSampling();
    }
}

static void centralAddDeviceInfo(uint8_t *pAddr, uint8_t addrType)
{
    uint8_t i;

    if(pAddr == NULL)
    {
        return;
    }

    if(centralScanRes < DEFAULT_MAX_SCAN_RES)
    {
        for(i = 0; i < centralScanRes; i++)
        {
            if(tmos_memcmp(pAddr, centralDevList[i].addr, B_ADDR_LEN))
            {
                return;
            }
        }
        tmos_memcpy(centralDevList[centralScanRes].addr, pAddr, B_ADDR_LEN);
        centralDevList[centralScanRes].addrType = addrType;
        centralScanRes++;
        PRINT("Scan %u - Addr %02x %02x %02x %02x %02x %02x\n", centralScanRes,
              pAddr[0], pAddr[1], pAddr[2], pAddr[3], pAddr[4], pAddr[5]);
    }
}

static void centralResolveTargetsFromScan(void)
{
    uint8_t i;
    uint8_t j;
    static const uint8_t * const peerAddrs[NODE_COUNT] = {
        PeerFingerAddr,
        PeerWristAddr
    };

    for(j = 0; j < NODE_COUNT; j++)
    {
        centralNode_t *n = &centralNodes[j];

        for(i = 0; i < centralScanRes; i++)
        {
            if(tmos_memcmp(peerAddrs[j], centralDevList[i].addr, B_ADDR_LEN))
            {
                n->addrType = centralDevList[i].addrType;
                if(!n->addrKnown)
                {
                    n->addrKnown = 1;
                    PRINT("Found %s @ %02x:%02x:%02x:%02x:%02x:%02x\n",
                          n->name,
                          n->addr[0], n->addr[1], n->addr[2],
                          n->addr[3], n->addr[4], n->addr[5]);
                }
                break;
            }
        }
    }
}

static uint16_t centralCharValueHandleFromReadByTypeRsp(gattMsgEvent_t *pMsg)
{
    uint8_t *p = pMsg->msg.readByTypeRsp.pDataList;
    uint8_t  itemLen = pMsg->msg.readByTypeRsp.len;

    if(itemLen >= 7U && pMsg->msg.readByTypeRsp.numPairs > 0)
    {
        return BUILD_UINT16(p[3], p[4]);
    }
    if(pMsg->msg.readByTypeRsp.numPairs > 0)
    {
        return BUILD_UINT16(p[0], p[1]);
    }
    return 0;
}

static void centralClampSvcSearchWindow(centralNode_t *n)
{
    uint16_t sh = n->svcStartHdl;
    uint16_t eh = n->svcEndHdl;
    uint32_t capped = (uint32_t)sh + (uint32_t)SIMPLE_SVC_SEARCH_MAX_SPAN;
    uint16_t newEnd;

    if(sh == 0)
    {
        return;
    }

    newEnd = (capped > 0xFFFFu) ? 0xFFFFu : (uint16_t)capped;

    if((eh <= sh) || (eh == 0xFFFF) ||
       (((uint32_t)eh - (uint32_t)sh) > (uint32_t)SIMPLE_SVC_SEARCH_MAX_SPAN))
    {
        PRINT("%s SVC grp end %x unreliable; window %x..%x\n",
              n->name, eh, sh, newEnd);
        n->svcEndHdl = newEnd;
    }
}

static uint8_t centralIsDiscoveryGattMsg(gattMsgEvent_t *pMsg)
{
    int8_t nodeIdx;

    nodeIdx = centralFindNodeByConnHandle(pMsg->connHandle);
    if(nodeIdx < 0)
    {
        return FALSE;
    }
    if(centralNodes[nodeIdx].discState == BLE_DISC_STATE_IDLE)
    {
        return FALSE;
    }

    if(pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP || pMsg->method == ATT_READ_BY_TYPE_RSP)
    {
        return TRUE;
    }
    if(pMsg->method == ATT_ERROR_RSP)
    {
        uint8_t op = pMsg->msg.errorRsp.reqOpcode;
        if(op == ATT_FIND_BY_TYPE_VALUE_REQ || op == ATT_READ_BY_TYPE_REQ)
        {
            return TRUE;
        }
    }
    return FALSE;
}

static void centralDiscoverCharByUuid(uint8_t nodeIdx, uint16_t charUuid)
{
    centralNode_t *n = &centralNodes[nodeIdx];
    attReadByTypeReq_t req;
    bStatus_t          st;

    if(n->svcStartHdl == 0 || n->connHandle == GAP_CONNHANDLE_INIT)
    {
        n->discState = BLE_DISC_STATE_IDLE;
        return;
    }

    if(centralProcedureInProgress)
    {
        n->discRetryPending = 1U;
        tmos_start_task(centralTaskId, START_CHAR4_DISC_EVT, 32);
        return;
    }

    req.startHandle = n->svcStartHdl;
    req.endHandle = n->svcEndHdl;
    req.type.len = ATT_BT_UUID_SIZE;
    req.type.uuid[0] = LO_UINT16(charUuid);
    req.type.uuid[1] = HI_UINT16(charUuid);

    centralProcedureInProgress = TRUE;

    st = GATT_DiscCharsByUUID(n->connHandle, &req, centralTaskId);
    if(st != SUCCESS)
    {
        PRINT("%s Char %04X disc fail st=%02X\n", n->name, charUuid, st);
        centralProcedureInProgress = FALSE;
        if(charUuid == SIMPLEPROFILE_CHAR4_UUID && n->char4DiscRetries < CHAR4_DISC_MAX_RETRIES)
        {
            n->char4DiscRetries++;
            n->discRetryPending = 1U;
            tmos_start_task(centralTaskId, START_CHAR4_DISC_EVT, 48);
        }
        else
        {
            n->discState = BLE_DISC_STATE_IDLE;
            centralOnProcedureIdle();
        }
    }
}

static void centralStartDiscovery(uint8_t nodeIdx)
{
    centralNode_t *n = &centralNodes[nodeIdx];
    uint8_t        uuid[ATT_BT_UUID_SIZE] = {LO_UINT16(SIMPLEPROFILE_SERV_UUID),
                                             HI_UINT16(SIMPLEPROFILE_SERV_UUID)};
    bStatus_t      st;

    if(!n->connected || n->connHandle == GAP_CONNHANDLE_INIT)
    {
        return;
    }

    if(centralProcedureInProgress)
    {
        n->discSchedulePending = 1U;
        tmos_start_task(centralTaskId, START_SVC_DISCOVERY_EVT, 32);
        return;
    }

    n->discPending = 0;
    n->svcStartHdl = 0;
    n->svcEndHdl = 0;
    n->char1ValHdl = 0;
    n->char4ValHdl = 0;
    n->char4CccdHdl = 0;
    n->discState = BLE_DISC_STATE_SVC;
    n->svcDiscRetries = 0;

    PRINT("%s GATT discovery start conn=%02X\n", n->name, n->connHandle);

    centralProcedureInProgress = TRUE;
    st = GATT_DiscPrimaryServiceByUUID(n->connHandle, uuid, ATT_BT_UUID_SIZE, centralTaskId);
    if(st != SUCCESS)
    {
        centralProcedureInProgress = FALSE;
        PRINT("%s SVC disc fail st=%02X\n", n->name, st);
        if(n->svcDiscRetries < SVC_DISC_MAX_RETRIES)
        {
            n->svcDiscRetries++;
            n->discSchedulePending = 1U;
            tmos_start_task(centralTaskId, START_SVC_DISCOVERY_EVT, SVC_DISC_FAIL_BACKOFF_TICKS);
        }
        else
        {
            PRINT("%s SVC disc give up\n", n->name);
            n->discState = BLE_DISC_STATE_IDLE;
        }
    }
}

static void centralWriteChar4CccdEnable(uint8_t nodeIdx)
{
    centralNode_t *n = &centralNodes[nodeIdx];
    attWriteReq_t  req;
    uint8_t *      p;

    if(n->connHandle == GAP_CONNHANDLE_INIT || n->char4CccdHdl == 0)
    {
        return;
    }

    req.cmd = FALSE;
    req.sig = FALSE;
    req.handle = n->char4CccdHdl;
    req.len = 2;
    p = GATT_bm_alloc(n->connHandle, ATT_WRITE_REQ, req.len, NULL, 0);
    if(p == NULL)
    {
        PRINT("%s CCCD bm_alloc fail\n", n->name);
        return;
    }
    p[0] = 0x01;
    p[1] = 0x00;
    req.pValue = p;

    if(GATT_WriteCharValue(n->connHandle, &req, centralTaskId) != SUCCESS)
    {
        PRINT("%s CCCD write fail\n", n->name);
        GATT_bm_free((gattMsg_t *)&req, ATT_WRITE_REQ);
        return;
    }

    centralProcedureInProgress = TRUE;
    centralPendingWrite = CENTRAL_WRITE_CCCD;
}

static void centralWriteChar1Cmd(uint8_t nodeIdx, uint8_t cmd)
{
    centralNode_t *n = &centralNodes[nodeIdx];
    attWriteReq_t  req;
    uint8_t *      p;

    if(n->connHandle == GAP_CONNHANDLE_INIT || n->char1ValHdl == 0)
    {
        return;
    }

    req.cmd = FALSE;
    req.sig = FALSE;
    req.handle = n->char1ValHdl;
    req.len = 1;
    p = GATT_bm_alloc(n->connHandle, ATT_WRITE_REQ, req.len, NULL, 0);
    if(p == NULL)
    {
        PRINT("%s Char1 bm_alloc fail\n", n->name);
        return;
    }
    p[0] = cmd;
    req.pValue = p;

    if(GATT_WriteCharValue(n->connHandle, &req, centralTaskId) != SUCCESS)
    {
        PRINT("%s Char1 write fail\n", n->name);
        GATT_bm_free((gattMsg_t *)&req, ATT_WRITE_REQ);
        return;
    }

    centralProcedureInProgress = TRUE;
}

static void centralAdvanceChar1Batch(void)
{
    switch(centralBatchStep)
    {
        case CENTRAL_BATCH_STEP_WRIST_1:
            centralBatchStep = CENTRAL_BATCH_STEP_FINGER_1;
            centralNextWriteNode = NODE_FINGER;
            centralWriteChar1Cmd(NODE_FINGER, '1');
            PRINT("-> Sent '1' to Finger (paced batch start)\n");
            break;

        case CENTRAL_BATCH_STEP_FINGER_1:
            centralBatchStep = CENTRAL_BATCH_STEP_WRIST_S;
            centralNextWriteNode = NODE_FINGER;
            centralWriteChar1Cmd(NODE_FINGER, 'S');
            PRINT("-> Sent 'S' to Finger (sync window)\n");
            break;

        case CENTRAL_BATCH_STEP_WRIST_S:
            centralBatchStep = CENTRAL_BATCH_STEP_FINGER_S;
            centralNextWriteNode = NODE_WRIST;
            centralWriteChar1Cmd(NODE_WRIST, 'S');
            PRINT("-> Sent 'S' to Wrist (sync window)\n");
            break;

        default:
            centralPendingWrite = CENTRAL_WRITE_NONE;
            centralNextWriteNode = -1;
            centralBatchStep = CENTRAL_BATCH_STEP_WRIST_1;
            centralOnNodeReady(NODE_WRIST);
            centralOnNodeReady(NODE_FINGER);
            PRINT("-> Paced batch START done on both nodes\n");
            break;
    }
}

static void centralAdvanceChar1Next(void)
{
    switch(centralBatchStep)
    {
        case CENTRAL_BATCH_STEP_WRIST_1:
            centralBatchStep = CENTRAL_BATCH_STEP_FINGER_1;
            centralNextWriteNode = NODE_FINGER;
            centralWriteChar1Cmd(NODE_FINGER, 'N');
            PRINT("-> Sent 'N' to Finger (pulse next)\n");
            break;

        case CENTRAL_BATCH_STEP_FINGER_1:
            centralBatchStep = CENTRAL_BATCH_STEP_WRIST_S;
            centralNextWriteNode = NODE_FINGER;
            centralWriteChar1Cmd(NODE_FINGER, 'S');
            PRINT("-> Sent 'S' to Finger (sync window)\n");
            break;

        case CENTRAL_BATCH_STEP_WRIST_S:
            centralBatchStep = CENTRAL_BATCH_STEP_FINGER_S;
            centralNextWriteNode = NODE_WRIST;
            centralWriteChar1Cmd(NODE_WRIST, 'S');
            PRINT("-> Sent 'S' to Wrist (sync window)\n");
            break;

        default:
            centralPendingWrite = CENTRAL_WRITE_NONE;
            centralNextWriteNode = -1;
            centralBatchStep = CENTRAL_BATCH_STEP_WRIST_1;
            PWV_OnPulseNextDone();
            PRINT("-> Pulse NEXT+SYNC done on both nodes\n");
            break;
    }
}

static void centralBeginStopSequence(void)
{
    centralStopStep = CENTRAL_STOP_STEP_WRIST;
    centralAdvanceStopSequence();
}

static void centralAdvanceStopSequence(void)
{
    if(centralStopStep == CENTRAL_STOP_STEP_WRIST)
    {
        if(centralNodes[NODE_WRIST].connected &&
           centralNodes[NODE_WRIST].char1ValHdl != 0)
        {
            centralPendingWrite = CENTRAL_WRITE_CHAR1_STOP;
            centralWriteChar1Cmd(NODE_WRIST, '2');
            PRINT("-> Sent '2' to Wrist (stop)\n");
            return;
        }
        centralStopStep = CENTRAL_STOP_STEP_FINGER;
    }

    if(centralStopStep == CENTRAL_STOP_STEP_FINGER)
    {
        if(centralNodes[NODE_FINGER].connected &&
           centralNodes[NODE_FINGER].char1ValHdl != 0)
        {
            centralPendingWrite = CENTRAL_WRITE_CHAR1_STOP;
            centralWriteChar1Cmd(NODE_FINGER, '2');
            PRINT("-> Sent '2' to Finger (stop)\n");
            return;
        }
    }

    centralPendingWrite = CENTRAL_WRITE_NONE;
    centralNextWriteNode = -1;
    PRINT("-> Stop done on both nodes\n");
}

void Central_StartMeasurement(void)
{
    centralMeasureWanted = TRUE;
    hr_fusion_reset();
    centralOnProcedureIdle();
}

void Central_StopMeasurement(void)
{
    if(!centralMeasureWanted)
    {
        return;
    }
    centralMeasureWanted = FALSE;
    centralPacedStartDone = FALSE;
    if(centralProcedureInProgress)
    {
        centralStopPending = TRUE;
        return;
    }
    centralBeginStopSequence();
}

uint8_t Central_CanStartMeasurement(void)
{
    return centralNodes[NODE_FINGER].char4On &&
           centralNodes[NODE_WRIST].char4On &&
           centralNodes[NODE_FINGER].connected &&
           centralNodes[NODE_WRIST].connected;
}

uint8_t Central_IsMeasurementWanted(void)
{
    return centralMeasureWanted;
}

static void centralTryStartBothSampling(void)
{
    if(!centralMeasureWanted)
    {
        return;
    }
    if(centralPacedStartDone)
    {
        return;
    }
    if(!centralNodes[NODE_FINGER].char4On || !centralNodes[NODE_WRIST].char4On)
    {
        return;
    }
    if(!centralNodes[NODE_FINGER].connected || !centralNodes[NODE_WRIST].connected)
    {
        return;
    }
    if(centralProcedureInProgress)
    {
        return;
    }

    centralPacedStartDone = TRUE;
    centralPendingWrite = CENTRAL_WRITE_CHAR1_BATCH;
    centralBatchStep = CENTRAL_BATCH_STEP_WRIST_1;
    centralNextWriteNode = NODE_WRIST;
    centralWriteChar1Cmd(NODE_WRIST, '1');
    PRINT("-> Sent '1' to Wrist (paced batch start)\n");
}

static void centralSendPulseNext(void)
{
    if(!centralMeasureWanted)
    {
        return;
    }
    if(centralProcedureInProgress)
    {
        return;
    }
    if(!centralNodes[NODE_WRIST].ready || !centralNodes[NODE_FINGER].ready)
    {
        return;
    }

    centralPendingWrite = CENTRAL_WRITE_CHAR1_NEXT;
    centralBatchStep = CENTRAL_BATCH_STEP_WRIST_1;
    centralNextWriteNode = NODE_WRIST;
    centralWriteChar1Cmd(NODE_WRIST, 'N');
    PRINT("-> Sent 'N' to Wrist (pulse next)\n");
}

static void centralMaybeAdvanceAfterPulse(void)
{
    uint16_t doneId;

#if MEASURE_SESSION_PULSE_TARGET > 0U
    doneId = PWV_GetLastAdvancedPulseId();
    if(doneId >= MEASURE_SESSION_PULSE_TARGET)
    {
        PRINT("-> Pulse target %u reached (last id=%u), stopping\n",
              (unsigned)MEASURE_SESSION_PULSE_TARGET, (unsigned)doneId);
        Measure_OnPulseTargetReached();
        return;
    }
#endif
    centralSendPulseNext();
}

static void centralOnNodeReady(uint8_t nodeIdx)
{
    centralNode_t *n = &centralNodes[nodeIdx];

    n->ready = 1;
    PRINT("%s ready (Notify enabled)\n", n->name);
    centralUiUpdateLink();

    if(centralParamUpdate && centralBothReady())
    {
        tmos_stop_task(centralTaskId, START_PARAM_UPDATE_EVT);
        tmos_start_task(centralTaskId, START_PARAM_UPDATE_EVT, DEFAULT_PARAM_UPDATE_DELAY);
    }

    if(centralBothReady())
    {
        centralReconnectFails = 0U;
        s_allowSecondLink = 0U;
        tmos_stop_task(centralTaskId, START_SECOND_LINK_EVT);
        PRINT("All nodes ready, collecting PPG data\n");
        return;
    }

    /* 仅一路 ready：延迟再开第二路 */
    s_allowSecondLink = 0U;
    tmos_stop_task(centralTaskId, START_SECOND_LINK_EVT);
    tmos_start_task(centralTaskId, START_SECOND_LINK_EVT, CENTRAL_SECOND_LINK_DELAY);
    PRINT("Arm second-link in %u ticks\n", (unsigned)CENTRAL_SECOND_LINK_DELAY);
}

static void centralHandleNotify(gattMsgEvent_t *pMsg, int8_t nodeIdx)
{
    centralNode_t *n;
    uint16_t       plen;
    uint8_t *      pv;
    uint16_t       nHdl;
    char           buf[128];
    unsigned int   id = 0;
    unsigned long  ts = 0;
    unsigned int   b_idx = 0;
    unsigned long  beat_ts = 0;
    int            hr = -1;
    int            spo2 = -1;
    int            wf_std = 0;
    int            wf_prom = 0;
    unsigned int   wf_df = 0;
    unsigned int   wf_pec = 0;
    int            pi = -1;
    int            matched;
    pwv_wf_compact_t wf = {0};
    const char *   wf_pos;

    if(nodeIdx < 0 || nodeIdx >= NODE_COUNT)
    {
        return;
    }
    if(!centralMeasureWanted)
    {
        return;
    }

    n = &centralNodes[nodeIdx];
    plen = pMsg->msg.handleValueNoti.len;
    pv = pMsg->msg.handleValueNoti.pValue;
    nHdl = pMsg->msg.handleValueNoti.handle;

    if(n->char4ValHdl != 0 && nHdl != n->char4ValHdl)
    {
        return;
    }
    if(plen == 0 || plen >= sizeof(buf))
    {
        return;
    }

    tmos_memcpy(buf, pv, plen);
    buf[plen] = '\0';

    /* Phase2 紧凑格式（无 ts） */
    matched = sscanf(buf,
                     "{\"id\":%u,\"b_idx\":%u,\"hr\":%d,\"spo2\":%d}",
                     &id, &b_idx, &hr, &spo2);
    if(matched < 4)
    {
        /* 兼容旧格式（含 ts/beat_ts） */
        matched = sscanf(buf,
                         "{\"id\":%u,\"ts\":%lu,\"b_idx\":%u,\"beat_ts\":%lu,\"hr\":%d,\"spo2\":%d}",
                         &id, &ts, &b_idx, &beat_ts, &hr, &spo2);
        if(matched < 6)
        {
            PRINT("%s bad JSON (%u): %s\n", n->name, (unsigned)plen, buf);
            return;
        }
    }

    wf_pos = strstr(buf, "\"w\":[");
    if(wf_pos != NULL)
    {
        if(sscanf(wf_pos, "\"w\":[%d,%d,%u,%u]", &wf_std, &wf_prom, &wf_df, &wf_pec) == 4)
        {
            wf.std_x10   = (int16_t)wf_std;
            wf.prom_x100 = (int16_t)wf_prom;
            wf.df_x10    = (uint8_t)wf_df;
            wf.pec_x100  = (uint8_t)wf_pec;
            wf.valid     = 1U;
        }
    }
    else
    {
        wf_pos = strstr(buf, "\"wf\":");
        if(wf_pos != NULL)
        {
            if(sscanf(wf_pos, "\"wf\":{\"std\":%d,\"prom\":%d,\"df\":%u,\"pec\":%u}",
                       &wf_std, &wf_prom, &wf_df, &wf_pec) == 4)
            {
                wf.std_x10   = (int16_t)wf_std;
                wf.prom_x100 = (int16_t)wf_prom;
                wf.df_x10    = (uint8_t)wf_df;
                wf.pec_x100  = (uint8_t)wf_pec;
                wf.valid     = 1U;
            }
        }
    }

    wf_pos = strstr(buf, "\"pi\":");
    if(wf_pos != NULL)
    {
        if(sscanf(wf_pos, "\"pi\":%d", &pi) == 1 && pi >= 0 && pi <= 255)
        {
            wf.pi_x10 = (uint8_t)pi;
            if(!wf.valid)
            {
                wf.valid = 1U;
            }
        }
    }

    ts = TMOS_GetSystemClock();
    ts = (uint32_t)(((uint64_t)ts * 625ULL) / 1000ULL);
    beat_ts = (uint32_t)b_idx * 20U;

    if(nodeIdx == NODE_WRIST)
    {
        uint8_t paired;
        uint8_t hr_u8;
        hr_fusion_result_t hr_ml = {0};

        hr_u8 = hr_fusion_filter_wrist((uint16_t)id, (uint8_t)hr, (uint8_t)spo2,
                                       wf.valid ? &wf : NULL, &hr_ml);
        if(hr_u8 == 0U)
        {
            PRINT("[Wrist  pulse_id=%u] HR=%d rejected by hr_fusion\n", id, hr);
            if(PWV_OnFusionReject(1U, (uint16_t)id))
            {
                centralMaybeAdvanceAfterPulse();
            }
            return;
        }
        hr = (int)hr_u8;

        Measure_OnSample(MEASURE_SIDE_WRIST, hr, spo2);
        UI_SetWrist((uint8_t)hr, (uint8_t)spo2);
        paired = PWV_FeedWrist((uint16_t)id, (uint8_t)hr, (uint8_t)spo2,
                               (uint16_t)b_idx, (uint32_t)beat_ts,
                               wf.pi_x10,
                               wf.valid ? &wf : NULL);
        if(paired)
        {
            centralMaybeAdvanceAfterPulse();
        }
    }
    else
    {
        uint8_t paired;
        uint8_t hr_u8;
        hr_fusion_result_t hr_ml = {0};

        hr_u8 = hr_fusion_filter_finger((uint16_t)id, (uint8_t)hr, (uint8_t)spo2,
                                        wf.valid ? &wf : NULL, &hr_ml);
        if(hr_u8 == 0U)
        {
            PRINT("[Finger pulse_id=%u] HR=%d rejected by hr_fusion\n", id, hr);
            if(PWV_OnFusionReject(0U, (uint16_t)id))
            {
                centralMaybeAdvanceAfterPulse();
            }
            return;
        }
        hr = (int)hr_u8;

        Measure_OnSample(MEASURE_SIDE_FINGER, hr, spo2);
        UI_SetFinger((uint8_t)hr, (uint8_t)spo2);
        paired = PWV_FeedFinger((uint16_t)id, (uint8_t)hr, (uint8_t)spo2,
                                (uint16_t)b_idx, (uint32_t)beat_ts,
                                wf.pi_x10,
                                wf.valid ? &wf : NULL);
        if(paired)
        {
            centralMaybeAdvanceAfterPulse();
        }
    }

    PRINT("[%-6s pulse_id=%u t=%8lums b_idx=%u beat_ts=%lu] HR=%u SpO2=%u\n",
          n->name, id, ts, b_idx, beat_ts, (unsigned)hr, (unsigned)spo2);
}

static void centralProcessGATTMsg(gattMsgEvent_t *pMsg)
{
    int8_t        nodeIdx;
    centralNode_t *n;

    if(pMsg->method == ATT_HANDLE_VALUE_NOTI)
    {
        nodeIdx = centralFindNodeByConnHandle(pMsg->connHandle);
        if(nodeIdx >= 0)
        {
            centralHandleNotify(pMsg, nodeIdx);
        }
        GATT_bm_free(&pMsg->msg, pMsg->method);
        return;
    }

    if(centralIsDiscoveryGattMsg(pMsg))
    {
        nodeIdx = centralFindNodeByConnHandle(pMsg->connHandle);
        if(nodeIdx >= 0)
        {
            centralGATTDiscoveryEvent(pMsg, (uint8_t)nodeIdx);
        }
        GATT_bm_free(&pMsg->msg, pMsg->method);
        return;
    }

    nodeIdx = centralFindNodeByConnHandle(pMsg->connHandle);
    if(nodeIdx < 0 && centralActiveNode >= 0 &&
       centralNodes[centralActiveNode].connected)
    {
        /* adopt handle from first GATT response if stack reports different handle */
        nodeIdx = centralActiveNode;
        centralNodes[nodeIdx].connHandle = pMsg->connHandle;
    }
    if(nodeIdx < 0)
    {
        GATT_bm_free(&pMsg->msg, pMsg->method);
        return;
    }

    n = &centralNodes[nodeIdx];

    if((pMsg->method == ATT_EXCHANGE_MTU_RSP) ||
       ((pMsg->method == ATT_ERROR_RSP) &&
        (pMsg->msg.errorRsp.reqOpcode == ATT_EXCHANGE_MTU_REQ)))
    {
        if(pMsg->method == ATT_ERROR_RSP)
        {
            PRINT("%s Exchange MTU Error: %x\n", n->name, pMsg->msg.errorRsp.errCode);
        }
        centralProcedureInProgress = FALSE;
        if(n->discPending)
        {
            n->discPending = 0;
            centralScheduleDiscovery((uint8_t)nodeIdx, 16);
        }
    }
    else if(pMsg->method == ATT_MTU_UPDATED_EVENT)
    {
        PRINT("%s MTU: %x\n", n->name, pMsg->msg.mtuEvt.MTU);
        centralProcedureInProgress = FALSE;
        if(n->discPending)
        {
            n->discPending = 0;
            centralScheduleDiscovery((uint8_t)nodeIdx, 16);
        }
    }
    else if((pMsg->method == ATT_WRITE_RSP) ||
            ((pMsg->method == ATT_ERROR_RSP) &&
             (pMsg->msg.errorRsp.reqOpcode == ATT_WRITE_REQ)))
    {
        uint8_t keepBusy = FALSE;

        if(pMsg->method == ATT_ERROR_RSP)
        {
            centralWriteOp_t failedOp = centralPendingWrite;

            PRINT("%s Write Error: %x\n", n->name, pMsg->msg.errorRsp.errCode);
            centralPendingWrite = CENTRAL_WRITE_NONE;
            centralNextWriteNode = -1;
            if(failedOp == CENTRAL_WRITE_CHAR1_NEXT)
            {
                centralBatchStep = CENTRAL_BATCH_STEP_WRIST_1;
                PWV_OnPulseNextDone();
            }
            else if(failedOp == CENTRAL_WRITE_CHAR1_BATCH)
            {
                centralBatchStep = CENTRAL_BATCH_STEP_WRIST_1;
                centralPacedStartDone = FALSE;
            }
            else if(failedOp == CENTRAL_WRITE_CHAR1_STOP)
            {
                centralPendingWrite = CENTRAL_WRITE_NONE;
            }
        }
        else if(centralPendingWrite == CENTRAL_WRITE_CCCD)
        {
            PRINT("%s Char4 notify enabled\n", n->name);
            centralPendingWrite = CENTRAL_WRITE_NONE;
            n->char4On = 1U;
            if(Central_CanStartMeasurement()) {
                UI_NotifyMeasureState();
            }
            centralOnNodeReady((uint8_t)nodeIdx);
            keepBusy = FALSE;
        }
        else if(centralPendingWrite == CENTRAL_WRITE_CHAR1)
        {
            centralPendingWrite = CENTRAL_WRITE_NONE;
            centralOnNodeReady((uint8_t)nodeIdx);
        }
        else if(centralPendingWrite == CENTRAL_WRITE_CHAR1_BATCH)
        {
            centralAdvanceChar1Batch();
            keepBusy = (centralPendingWrite != CENTRAL_WRITE_NONE) ? TRUE : FALSE;
        }
        else if(centralPendingWrite == CENTRAL_WRITE_CHAR1_NEXT)
        {
            centralAdvanceChar1Next();
            keepBusy = (centralPendingWrite != CENTRAL_WRITE_NONE) ? TRUE : FALSE;
        }
        else if(centralPendingWrite == CENTRAL_WRITE_CHAR1_STOP)
        {
            if(centralStopStep == CENTRAL_STOP_STEP_WRIST)
            {
                centralStopStep = CENTRAL_STOP_STEP_FINGER;
                centralAdvanceStopSequence();
            }
            else
            {
                centralPendingWrite = CENTRAL_WRITE_NONE;
                centralNextWriteNode = -1;
                PRINT("-> Stop done on both nodes\n");
            }
            keepBusy = (centralPendingWrite != CENTRAL_WRITE_NONE) ? TRUE : FALSE;
        }

        if(!keepBusy)
        {
            centralProcedureInProgress = FALSE;
            centralOnProcedureIdle();
        }
    }

    GATT_bm_free(&pMsg->msg, pMsg->method);
}

static void centralGATTDiscoveryEvent(gattMsgEvent_t *pMsg, uint8_t nodeIdx)
{
    centralNode_t *n = &centralNodes[nodeIdx];

    if(n->discState == BLE_DISC_STATE_SVC)
    {
        if(pMsg->method == ATT_FIND_BY_TYPE_VALUE_RSP)
        {
            if(pMsg->msg.findByTypeValueRsp.numInfo > 0)
            {
                n->svcStartHdl = ATT_ATTR_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
                n->svcEndHdl = ATT_GRP_END_HANDLE(pMsg->msg.findByTypeValueRsp.pHandlesInfo, 0);
                PRINT("%s SVC handle : %x ~ %x\n", n->name, n->svcStartHdl, n->svcEndHdl);
                centralClampSvcSearchWindow(n);
            }

            if(pMsg->hdr.status == bleProcedureComplete)
            {
                centralProcedureInProgress = FALSE;
                if(n->svcStartHdl != 0)
                {
                    n->discState = BLE_DISC_STATE_CHAR1;
                    n->char4DiscRetries = 0;
                    centralDiscoverCharByUuid(nodeIdx, SIMPLEPROFILE_CHAR1_UUID);
                }
                else
                {
                    PRINT("%s SVC not found\n", n->name);
                    n->discState = BLE_DISC_STATE_IDLE;
                }
                centralOnProcedureIdle();
            }
        }
        else if(pMsg->method == ATT_ERROR_RSP)
        {
            centralProcedureInProgress = FALSE;
            PRINT("%s SVC discovery fail err=%x\n", n->name, pMsg->msg.errorRsp.errCode);
            n->discState = BLE_DISC_STATE_IDLE;
            centralOnProcedureIdle();
        }
    }
    else if(n->discState == BLE_DISC_STATE_CHAR1)
    {
        if(pMsg->method == ATT_READ_BY_TYPE_RSP)
        {
            if(pMsg->msg.readByTypeRsp.numPairs > 0)
            {
                n->char1ValHdl = centralCharValueHandleFromReadByTypeRsp(pMsg);
                PRINT("%s Char1 handle %x\n", n->name, n->char1ValHdl);
            }

            if(pMsg->hdr.status == bleProcedureComplete)
            {
                centralProcedureInProgress = FALSE;
                if(n->char1ValHdl != 0)
                {
                    n->discState = BLE_DISC_STATE_CHAR4;
                    n->char4DiscRetries = 0;
                    centralDiscoverCharByUuid(nodeIdx, SIMPLEPROFILE_CHAR4_UUID);
                }
                else
                {
                    PRINT("%s Char1 not found\n", n->name);
                    n->discState = BLE_DISC_STATE_IDLE;
                }
                centralOnProcedureIdle();
            }
        }
        else if(pMsg->method == ATT_ERROR_RSP)
        {
            centralProcedureInProgress = FALSE;
            PRINT("%s Char1 discovery fail err=%x\n", n->name, pMsg->msg.errorRsp.errCode);
            n->discState = BLE_DISC_STATE_IDLE;
            centralOnProcedureIdle();
        }
    }
    else if(n->discState == BLE_DISC_STATE_CHAR4)
    {
        if(pMsg->method == ATT_READ_BY_TYPE_RSP)
        {
            if(pMsg->msg.readByTypeRsp.numPairs > 0)
            {
                n->char4ValHdl = centralCharValueHandleFromReadByTypeRsp(pMsg);
                n->char4CccdHdl = n->char4ValHdl + 1U;
                PRINT("%s Char4 handle %x CCCD %x\n",
                      n->name, n->char4ValHdl, n->char4CccdHdl);
            }

            if(pMsg->hdr.status == bleProcedureComplete)
            {
                centralProcedureInProgress = FALSE;
                n->char4DiscRetries = 0;

                if(n->char4ValHdl != 0)
                {
                    n->discState = BLE_DISC_STATE_IDLE;
                    centralWriteChar4CccdEnable(nodeIdx);
                }
                else
                {
                    PRINT("%s Char4 not found span %x..%x\n",
                          n->name, n->svcStartHdl, n->svcEndHdl);
                    n->discState = BLE_DISC_STATE_IDLE;
                }
                centralOnProcedureIdle();
            }
        }
        else if(pMsg->method == ATT_ERROR_RSP)
        {
            centralProcedureInProgress = FALSE;
            PRINT("%s Char4 discovery fail err=%x\n", n->name, pMsg->msg.errorRsp.errCode);
            n->discState = BLE_DISC_STATE_IDLE;
            centralOnProcedureIdle();
        }
    }
}

static void centralRssiCB(uint16_t connHandle, int8_t rssi)
{
    (void)connHandle;
    (void)rssi;
}

static void centralHciMTUChangeCB(uint16_t connHandle, uint16_t maxTxOctets, uint16_t maxRxOctets)
{
    (void)connHandle;
    (void)maxTxOctets;
    (void)maxRxOctets;
}

static void centralEventCB(gapRoleEvent_t *pEvent)
{
    int8_t         nodeIdx;
    centralNode_t *n;
    attExchangeMTUReq_t mtuReq;

    switch(pEvent->gap.opcode)
    {
        case GAP_DEVICE_INIT_DONE_EVENT:
            centralStartScan();
            break;

        case GAP_DEVICE_INFO_EVENT:
            centralAddDeviceInfo(pEvent->deviceInfo.addr, pEvent->deviceInfo.addrType);
            break;

        case GAP_DEVICE_DISCOVERY_EVENT:
            centralResolveTargetsFromScan();
            if(!centralBothAddrDistinct())
            {
                if(centralBothAddrKnown())
                {
                    PRINT("Scan done: duplicate MAC, rescan\n");
                }
                else
                {
                    PRINT("Scan done, waiting for both nodes...\n");
                }
                centralScanRes = 0;
                centralStartScan();
            }
            else if(!centralConnectInProgress())
            {
                centralTryConnectNext();
            }
            break;

        case GAP_LINK_ESTABLISHED_EVENT:
        {
            int8_t pendingIdx;

            tmos_stop_task(centralTaskId, ESTABLISH_LINK_TIMEOUT_EVT);
            if(pEvent->gap.hdr.status == SUCCESS)
            {
                gapEstLinkReqEvent_t *lc = (gapEstLinkReqEvent_t *)pEvent;

                pendingIdx = centralConnectingIdx;
                if(pendingIdx >= 0)
                {
                    n = &centralNodes[pendingIdx];
                    if(!tmos_memcmp(n->addr, lc->devAddr, B_ADDR_LEN))
                    {
                        PRINT("ACL addr mismatch (expect %s), drop h=%02X\n",
                              n->name, lc->connectionHandle);
                        n->connecting = 0;
                        centralConnectingIdx = -1;
                        centralUiUpdateLink();
                        GAPRole_TerminateLink(lc->connectionHandle);
                        centralTryConnectNext();
                        break;
                    }
                    nodeIdx = pendingIdx;
                }
                else
                {
                    nodeIdx = centralFindNodeByAddr(lc->devAddr);
                    if(nodeIdx < 0)
                    {
                        PRINT("ACL ok but unknown peer, drop h=%02X\n", lc->connectionHandle);
                        GAPRole_TerminateLink(lc->connectionHandle);
                        break;
                    }
                }

                n = &centralNodes[nodeIdx];
                n->connecting = 0;
                n->connected = 1;
                n->connHandle = lc->connectionHandle;
                centralConnectingIdx = -1;
                centralActiveNode = nodeIdx;

                PRINT("%s connected h=%02X\n", n->name, n->connHandle);
                centralUiUpdateLink();
                (void)GAPRole_CentralCancelDiscovery();

                centralProcedureInProgress = TRUE;
                n->discPending = 1U;
                mtuReq.clientRxMTU = BLE_BUFF_MAX_LEN - 4;
                GATT_ExchangeMTU(n->connHandle, &mtuReq, centralTaskId);
            }
            else
            {
                if(centralConnectingIdx >= 0)
                {
                    PRINT("%s connect failed reason=%02X, rescan\n",
                          centralNodes[centralConnectingIdx].name,
                          pEvent->gap.hdr.status);
                    centralNodes[centralConnectingIdx].connecting = 0;
                    centralClearNodePresence((uint8_t)centralConnectingIdx);
                    centralConnectingIdx = -1;
                    centralUiUpdateLink();
                    if(centralReconnectFails < 12U)
                    {
                        centralReconnectFails++;
                    }
                }
                if(centralNodes[NODE_WRIST].connected || centralNodes[NODE_FINGER].connected)
                {
                    centralScheduleReconnect();
                }
                else if(centralBothAddrDistinct())
                {
                    centralTryConnectNext();
                }
                else
                {
                    centralScheduleReconnect();
                }
            }
        }
        break;

        case GAP_LINK_TERMINATED_EVENT:
        {
            nodeIdx = centralFindNodeByConnHandle(pEvent->linkTerminate.connectionHandle);
            if(nodeIdx >= 0)
            {
                PRINT("%s disconnected reason=%x\n",
                      centralNodes[nodeIdx].name,
                      pEvent->linkTerminate.reason);
                if(pEvent->linkTerminate.reason == 0x3e ||
                   pEvent->linkTerminate.reason == 0x08)
                {
                    if(centralReconnectFails < 12U)
                    {
                        centralReconnectFails++;
                    }
                }
                centralResetNodeRuntime(&centralNodes[nodeIdx]);
                if(Measure_IsActive())
                {
                    if(!centralNodes[NODE_WRIST].connected &&
                       !centralNodes[NODE_FINGER].connected)
                    {
                        Measure_NotifyLinkLost();
                    }
                }
                centralUiUpdateLink();
                if(centralActiveNode == nodeIdx)
                {
                    centralActiveNode = -1;
                }
                centralPendingWrite = CENTRAL_WRITE_NONE;
                centralProcedureInProgress = FALSE;
                centralPacedStartDone = FALSE;
                centralOnProcedureIdle();
                centralScheduleReconnect();
            }
        }
        break;

        default:
            break;
    }
}

static void centralPairStateCB(uint16_t connHandle, uint8_t state, uint8_t status)
{
    (void)connHandle;
    (void)state;
    (void)status;
}

static void centralPasscodeCB(uint8_t *deviceAddr, uint16_t connectionHandle,
                              uint8_t uiInputs, uint8_t uiOutputs)
{
    uint32_t passcode;

    (void)deviceAddr;
    passcode = tmos_rand() % 1000000;
    if(uiOutputs != 0)
    {
        PRINT("Passcode:%06d\n", (int)passcode);
    }
    GAPBondMgr_PasscodeRsp(connectionHandle, SUCCESS, passcode);
}

/************************ endfile @ central **************************/
