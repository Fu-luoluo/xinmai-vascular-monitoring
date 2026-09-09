#include "tp_store.h"
#include "ISP585.h"

/*
 * CH585 Data-Flash API uses offset addresses in 0x0000..0x7FFF (32KB),
 * same as BLE_SNV_ADDR (0x7000), NOT absolute 0x70000+.
 * Use page 0 for touch cal, away from BLE SNV at 0x7000.
 */
#define TP_CAL_DF_ADDR     0x0000u
#define TP_CAL_ADDR_MIN    40u
#define TP_CAL_ADDR_MAX    55u
#define TP_CAL_USED        (TP_CAL_ADDR_MAX - TP_CAL_ADDR_MIN)

__attribute__((aligned(4))) static uint8_t s_tp_page[EEPROM_PAGE_SIZE];
static uint8_t s_tp_loaded;
static uint8_t s_tp_dirty;

static int tp_cal_index(u16 addr)
{
    if(addr < TP_CAL_ADDR_MIN || addr >= TP_CAL_ADDR_MAX) {
        return -1;
    }
    return (int)(addr - TP_CAL_ADDR_MIN);
}

static void tp_page_load(void)
{
    if(!s_tp_loaded) {
        (void)EEPROM_READ(TP_CAL_DF_ADDR, s_tp_page, EEPROM_PAGE_SIZE);
        s_tp_loaded = 1;
    }
}

static uint32_t tp_page_commit(void)
{
    uint32_t ret;

    if(!s_tp_dirty) {
        return 0;
    }
    ret = EEPROM_ERASE(TP_CAL_DF_ADDR, EEPROM_PAGE_SIZE);
    if(ret != 0) {
        return ret;
    }
    ret = EEPROM_WRITE(TP_CAL_DF_ADDR, s_tp_page, EEPROM_PAGE_SIZE);
    if(ret == 0) {
        s_tp_dirty = 0;
    }
    return ret;
}

void AT24CXX_Init(void)
{
    s_tp_loaded = 0;
    s_tp_dirty = 0;
}

void AT24CXX_Commit(void)
{
    tp_page_load();
    (void)tp_page_commit();
}

u8 AT24CXX_ReadOneByte(u16 ReadAddr)
{
    int idx = tp_cal_index(ReadAddr);

    if(idx < 0) {
        return 0xFF;
    }
    tp_page_load();
    return s_tp_page[idx];
}

void AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite)
{
    int idx = tp_cal_index(WriteAddr);

    if(idx < 0) {
        return;
    }
    tp_page_load();
    if(s_tp_page[idx] != DataToWrite) {
        s_tp_page[idx] = DataToWrite;
        s_tp_dirty = 1;
    }
}

void AT24CXX_WriteLenByte(u16 WriteAddr, u32 DataToWrite, u8 Len)
{
    u8  i;
    int idx;

    tp_page_load();
    for(i = 0; i < Len; i++) {
        idx = tp_cal_index((u16)(WriteAddr + i));
        if(idx >= 0) {
            u8 b = (u8)(DataToWrite >> (i * 8));
            if(s_tp_page[idx] != b) {
                s_tp_page[idx] = b;
                s_tp_dirty = 1;
            }
        }
    }
}

u32 AT24CXX_ReadLenByte(u16 ReadAddr, u8 Len)
{
    u8  i;
    u32 temp = 0;

    for(i = 0; i < Len; i++) {
        temp <<= 8;
        temp += AT24CXX_ReadOneByte((u16)(ReadAddr + Len - i - 1));
    }
    return temp;
}
