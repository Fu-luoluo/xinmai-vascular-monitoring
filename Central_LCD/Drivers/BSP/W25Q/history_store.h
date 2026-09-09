#ifndef HISTORY_STORE_H
#define HISTORY_STORE_H

#include <stdint.h>
#include "pwv.h"
#include "measure_ctrl.h"

#define HISTORY_RAM_CACHE_SIZE   12u
#define HISTORY_STORE_CAP        120u  /* 环形缓冲最多保留 120 条，满后覆盖最早 */

typedef struct __attribute__((packed))
{
    uint32_t seq;
    uint32_t ts_sec;
    uint8_t  reason;
    uint8_t  pwv_valid;
    uint8_t  w_hr;
    uint8_t  w_spo2;
    uint8_t  f_hr;
    uint8_t  f_spo2;
    int16_t  pwv_x10;
    uint16_t pwv_raw_x10;
} history_record_t;

uint8_t HistoryStore_Init(void);
uint8_t HistoryStore_Ensure(void);
uint8_t HistoryStore_IsReady(void);
uint8_t HistoryStore_AppendSession(measureEndReason_t reason, const pwv_session_t *snap);
uint32_t HistoryStore_GetCount(void);
uint32_t HistoryStore_GetTotal(void);
uint8_t HistoryStore_GetRecent(uint32_t index, history_record_t *out);
uint8_t HistoryStore_GetRamCacheCount(void);

#endif /* HISTORY_STORE_H */
