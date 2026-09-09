#ifndef UI_PROFILE_H
#define UI_PROFILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void UI_Profile_Init(void);
void UI_Profile_Open(void);
void UI_Profile_Destroy(void);
void UI_Profile_FormatSummary(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* UI_PROFILE_H */
