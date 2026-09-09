#ifndef __HOST_LINK_H
#define __HOST_LINK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*HostLinkEventFn)(const char *json_line);

void HostLink_Init(void);
void HostLink_Poll(void);
void HostLink_DispatchPending(void);
void HostLink_ApplyCachedWifiList(void);
void HostLink_TaskStart(void);
uint8_t HostLink_TakeWifiList(char *buf, uint16_t buf_len);
uint8_t HostLink_TakeWifiStatus(char *buf, uint16_t buf_len);
void HostLink_SetEventFn(HostLinkEventFn fn);
uint8_t HostLink_IsOtaActive(void);
void HostLink_ReportBoot(uint32_t version, uint8_t ota_status);
uint32_t HostLink_GetRxLineCount(void);
uint32_t HostLink_GetWifiListCount(void);
uint32_t HostLink_GetWifiStatusCount(void);
uint32_t HostLink_GetRxDropCount(void);

void HostLink_CmdPing(void);

void HostLink_CmdWifiScan(void);
void HostLink_CmdWifiStatus(void);
void HostLink_CmdWifiForget(void);
void HostLink_CmdWifiConnect(const char *ssid, const char *pass);

#ifdef __cplusplus
}
#endif

#endif /* __HOST_LINK_H */
