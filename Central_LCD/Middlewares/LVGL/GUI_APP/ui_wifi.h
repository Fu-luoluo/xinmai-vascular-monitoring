#ifndef UI_WIFI_H
#define UI_WIFI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void UI_WiFi_Init(void);
void UI_WiFi_Open(void);
void UI_WiFi_Release(void);
void UI_WiFi_Leave(void);
void UI_WiFi_OnHostLine(const char *json_line);
uint8_t UI_WiFi_IsActive(void);
uint8_t UI_WiFi_IsPassView(void);
void UI_WiFi_GetStatusSummary(char *buf, size_t len);
void UI_WiFi_UpdateDebug(void);
void UI_WiFi_ApplyPending(void);

#ifdef __cplusplus
}
#endif

#endif /* UI_WIFI_H */
