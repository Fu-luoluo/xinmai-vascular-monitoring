#include "history_store.h"
#include "flash_layout.h"
#include "flash_store.h"
#include "CONFIG.h"
#include "HAL.h"
#include <string.h>

#define HISTORY_HDR_MAGIC        0x48495331u  /* 'HIS1' */
#define HISTORY_HDR_VERSION      2u
#define HISTORY_RECORD_SIZE      ((uint16_t)sizeof(history_record_t))
#define HISTORY_META_SECTOR_SIZE W25Q_SECTOR_SIZE
#define HISTORY_DATA_BASE        (FLASH_PART_HIST_BASE + HISTORY_META_SECTOR_SIZE)
#define HISTORY_DATA_SIZE        (FLASH_PART_HIST_SIZE - HISTORY_META_SECTOR_SIZE)
#define HISTORY_MAX_RECORDS      (HISTORY_DATA_SIZE / HISTORY_RECORD_SIZE)
#define HISTORY_RING_CAP         HISTORY_STORE_CAP

static uint8_t history_slot_valid(uint32_t slot)
{
    return (slot < HISTORY_RING_CAP && slot < HISTORY_MAX_RECORDS) ? 1u : 0u;
}

typedef struct __attribute__((packed))
{
    uint32_t magic;
    uint16_t version;
    uint16_t record_size;
    uint32_t head;
    uint32_t count;
    uint32_t total;
} history_hdr_t;

static history_hdr_t s_hdr;
static history_record_t s_ram_cache[HISTORY_RAM_CACHE_SIZE];
static uint8_t s_ram_cache_count;
static uint8_t s_ready;

static uint32_t history_now_sec(void)
{
    return TMOS_GetSystemClock() / MS1_TO_SYSTEM_TIME(1000);
}

static uint8_t history_record_addr(uint32_t slot, uint32_t *abs_addr)
{
    if(!history_slot_valid(slot) || abs_addr == NULL) {
        return 1;
    }
    *abs_addr = HISTORY_DATA_BASE + (slot * HISTORY_RECORD_SIZE);
    return 0;
}

static uint8_t history_flush_hdr(void)
{
    if(FlashStore_EraseSector(FLASH_PART_HIST_BASE) != 0) {
        return 1;
    }
    return FlashStore_Program(FLASH_PART_HIST_BASE, (const uint8_t *)&s_hdr, sizeof(s_hdr));
}

static uint8_t history_format(void)
{
    memset(&s_hdr, 0, sizeof(s_hdr));
    s_hdr.magic = HISTORY_HDR_MAGIC;
    s_hdr.version = HISTORY_HDR_VERSION;
    s_hdr.record_size = HISTORY_RECORD_SIZE;
    s_hdr.head = 0;
    s_hdr.count = 0;
    s_hdr.total = 0;
    return history_flush_hdr();
}

static void history_push_ram_cache(const history_record_t *rec)
{
    uint8_t i;

    if(rec == NULL) {
        return;
    }
    if(s_ram_cache_count < HISTORY_RAM_CACHE_SIZE) {
        s_ram_cache[s_ram_cache_count++] = *rec;
        return;
    }
    for(i = 1; i < HISTORY_RAM_CACHE_SIZE; i++) {
        s_ram_cache[i - 1] = s_ram_cache[i];
    }
    s_ram_cache[HISTORY_RAM_CACHE_SIZE - 1] = *rec;
}

static uint8_t history_read_slot(uint32_t slot, history_record_t *out)
{
    uint32_t abs_addr;

    if(out == NULL || history_record_addr(slot, &abs_addr) != 0) {
        return 1;
    }
    return FlashStore_Read(abs_addr, (uint8_t *)out, sizeof(*out));
}

static uint8_t history_write_slot(uint32_t slot, const history_record_t *rec)
{
    uint32_t abs_addr;

    if(rec == NULL || history_record_addr(slot, &abs_addr) != 0) {
        return 1;
    }
    if((abs_addr % W25Q_SECTOR_SIZE) == 0) {
        if(FlashStore_EraseSector(abs_addr) != 0) {
            return 1;
        }
    }
    return FlashStore_Program(abs_addr, (const uint8_t *)rec, sizeof(*rec));
}

uint8_t HistoryStore_Init(void)
{
    history_hdr_t hdr;

    s_ready = 0;
    s_ram_cache_count = 0;
    if(FlashStore_Ensure() != 0) {
        return 1;
    }

    if(FlashStore_Read(FLASH_PART_HIST_BASE, (uint8_t *)&hdr, sizeof(hdr)) != 0) {
        return 1;
    }
    if(hdr.magic != HISTORY_HDR_MAGIC ||
       hdr.version != HISTORY_HDR_VERSION ||
       hdr.record_size != HISTORY_RECORD_SIZE ||
       hdr.head >= HISTORY_RING_CAP ||
       hdr.count > HISTORY_RING_CAP) {
        if(history_format() != 0) {
            return 1;
        }
    } else {
        s_hdr = hdr;
    }

    s_ready = 1;

    if(s_hdr.count > 0) {
        uint32_t n = s_hdr.count;
        uint32_t i;

        if(n > HISTORY_RAM_CACHE_SIZE) {
            n = HISTORY_RAM_CACHE_SIZE;
        }
        for(i = 0; i < n; i++) {
            uint32_t slot = (s_hdr.head + HISTORY_RING_CAP - 1u - i) % HISTORY_RING_CAP;
            (void)history_read_slot(slot, &s_ram_cache[n - 1u - i]);
        }
        s_ram_cache_count = (uint8_t)n;
    }

    PRINT("HistoryStore OK: count=%lu total=%lu\r\n",
          (unsigned long)s_hdr.count, (unsigned long)s_hdr.total);
    return 0;
}

uint8_t HistoryStore_Ensure(void)
{
    if(s_ready && FlashStore_IsReady()) {
        return 0;
    }
    return HistoryStore_Init();
}

uint8_t HistoryStore_IsReady(void)
{
    return s_ready;
}

uint8_t HistoryStore_AppendSession(measureEndReason_t reason, const pwv_session_t *snap)
{
    history_record_t rec;
    uint32_t slot;

    if(snap == NULL || HistoryStore_Ensure() != 0) {
        return 1;
    }

    memset(&rec, 0, sizeof(rec));
    rec.seq = s_hdr.total + 1u;
    rec.ts_sec = history_now_sec();
    rec.reason = (uint8_t)reason;
    rec.pwv_valid = snap->pwv_valid ? 1u : 0u;
    rec.w_hr = snap->w_hr;
    rec.w_spo2 = snap->w_spo2;
    rec.f_hr = snap->f_hr;
    rec.f_spo2 = snap->f_spo2;
    if(snap->pwv_valid) {
        rec.pwv_x10 = (int16_t)(snap->pwv * 10.0f);
        rec.pwv_raw_x10 = (uint16_t)(snap->pwv_raw * 10.0f + 0.5f);
    }

    slot = s_hdr.head % HISTORY_RING_CAP;
    if(history_write_slot(slot, &rec) != 0) {
        return 1;
    }

    s_hdr.head = (s_hdr.head + 1u) % HISTORY_RING_CAP;
    if(s_hdr.count < HISTORY_RING_CAP) {
        s_hdr.count++;
    }
    s_hdr.total++;

    if(history_flush_hdr() != 0) {
        return 1;
    }

    history_push_ram_cache(&rec);
    PRINT("HistoryStore append seq=%lu pwv_valid=%u\r\n",
          (unsigned long)rec.seq, (unsigned)rec.pwv_valid);
    return 0;
}

uint32_t HistoryStore_GetCount(void)
{
    return s_hdr.count;
}

uint32_t HistoryStore_GetTotal(void)
{
    return s_hdr.total;
}

uint8_t HistoryStore_GetRecent(uint32_t index, history_record_t *out)
{
    uint32_t slot;

    if(!s_ready || out == NULL || index >= s_hdr.count) {
        return 1;
    }

    if(index < s_ram_cache_count) {
        *out = s_ram_cache[s_ram_cache_count - 1u - index];
        return 0;
    }

    slot = (s_hdr.head + HISTORY_RING_CAP - 1u - index) % HISTORY_RING_CAP;
    return history_read_slot(slot, out);
}

uint8_t HistoryStore_GetRamCacheCount(void)
{
    return s_ram_cache_count;
}
