#ifndef __TP_STORE_H
#define __TP_STORE_H

#include "tft_port.h"

/* Drop-in for merchant 24CXX API used by touch.c (CH585 Data-Flash) */
void AT24CXX_Init(void);
void AT24CXX_Commit(void);
u8   AT24CXX_ReadOneByte(u16 ReadAddr);
void AT24CXX_WriteOneByte(u16 WriteAddr, u8 DataToWrite);
void AT24CXX_WriteLenByte(u16 WriteAddr, u32 DataToWrite, u8 Len);
u32  AT24CXX_ReadLenByte(u16 ReadAddr, u8 Len);

#endif
