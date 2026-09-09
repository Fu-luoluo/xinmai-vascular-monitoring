/**
 * @file touch.c
 * 3.5" FT6336 capacitive touch — keeps TP_Init / TP_Scan API for LVGL
 * Ported from QD MSP3525/3526 Hardware SPI demo (ctpiic + ft6336).
 */
#include "touch.h"
#include "touch_port.h"
#include "lcd.h"
#include "tft_delay.h"
#include "CH58x_common.h"

#define FT_CMD_WR                 0x70U
#define FT_CMD_RD                 0x71U
#define FT_REG_NUM_FINGER         0x02U
#define FT_TP1_REG                0x03U
#define FT_ID_G_CIPHER_MID        0x9FU
#define FT_ID_G_FOCALTECH_ID      0xA8U
#define FT_ID_G_CIPHER_HIGH       0xA3U

_m_tp_dev tp_dev = {
    TP_Init,
    TP_Scan,
    TP_Adjust,
    0, 0, 0, 0, 0,
    0, 0, 0, 0,
    0,
};

static void FT6336_RD_Reg(uint16_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    CTP_IIC_Start();
    CTP_IIC_Send_Byte(FT_CMD_WR);
    CTP_IIC_Wait_Ack();
    CTP_IIC_Send_Byte((uint8_t)(reg & 0xFFU));
    CTP_IIC_Wait_Ack();
    CTP_IIC_Start();
    CTP_IIC_Send_Byte(FT_CMD_RD);
    CTP_IIC_Wait_Ack();
    for(i = 0U; i < len; i++) {
        buf[i] = CTP_IIC_Read_Byte(i == (len - 1U) ? 0U : 1U);
    }
    CTP_IIC_Stop();
}

static uint8_t FT6336_Init(void)
{
    uint8_t temp[2];

    CTP_IIC_Init();
    CTP_RST_Low();
    delay_ms(10);
    CTP_RST_High();
    delay_ms(300);

    FT6336_RD_Reg(FT_ID_G_FOCALTECH_ID, &temp[0], 1);
    FT6336_RD_Reg(FT_ID_G_CIPHER_MID, &temp[0], 2);
    FT6336_RD_Reg(FT_ID_G_CIPHER_HIGH, &temp[0], 1);
    (void)temp;
    return 0U;
}

u8 TP_Scan(u8 tp)
{
    uint8_t buf[4];
    uint8_t mode = 0U;
    uint16_t raw_x;
    uint16_t raw_y;

    (void)tp;

    /* 每次都读：旧版每 10 次才采样，易导致按下被误清、点击失灵 */
    FT6336_RD_Reg(FT_REG_NUM_FINGER, &mode, 1);
    if(mode && mode < 3U) {
        FT6336_RD_Reg(FT_TP1_REG, buf, 4);
        raw_x = (uint16_t)(((uint16_t)(buf[0] & 0x0FU) << 8) + buf[1]);
        raw_y = (uint16_t)(((uint16_t)(buf[2] & 0x0FU) << 8) + buf[3]);

        switch(lcddev.dir) {
        case 0:
            tp_dev.x = raw_x;
            tp_dev.y = raw_y;
            break;
        case 1:
            tp_dev.y = (u16)(lcddev.height - raw_x);
            tp_dev.x = raw_y;
            break;
        case 2:
            tp_dev.x = (u16)(lcddev.width - raw_x);
            tp_dev.y = (u16)(lcddev.height - raw_y);
            break;
        case 3:
        default:
            tp_dev.y = raw_x;
            tp_dev.x = (u16)(lcddev.width - raw_y);
            break;
        }

        if(tp_dev.x != 0U || tp_dev.y != 0U) {
            tp_dev.sta = TP_PRES_DOWN | TP_CATH_PRES;
            return 1U;
        }
        mode = 0U;
    }

    if(mode == 0U) {
        if(tp_dev.sta & TP_PRES_DOWN) {
            tp_dev.sta &= (u8)(~TP_PRES_DOWN);
        } else {
            tp_dev.x = 0;
            tp_dev.y = 0;
            tp_dev.sta = 0;
        }
    }
    return (tp_dev.sta & TP_PRES_DOWN) ? 1U : 0U;
}

void TP_Save_Adjdata(void)
{
}

u8 TP_Get_Adjdata(void)
{
    tp_dev.xfac = 1.0f;
    tp_dev.yfac = 1.0f;
    tp_dev.xoff = 0;
    tp_dev.yoff = 0;
    return 1U;
}

void TP_Adjust(void)
{
    /* 电容屏无需四点校准 */
    TP_Get_Adjdata();
}

u8 TP_Init(void)
{
    TP_Port_GPIO_Init();
    if(FT6336_Init()) {
        return 1U;
    }
    TP_Get_Adjdata();
    tp_dev.touchtype = 0U;
    return 0U;
}

void TP_Write_Byte(u8 num) { (void)num; }
u16 TP_Read_AD(u8 CMD) { (void)CMD; return 0; }
u16 TP_Read_XOY(u8 xy) { (void)xy; return 0; }
u8 TP_Read_XY(u16 *x, u16 *y)
{
    if(x) {
        *x = tp_dev.x;
    }
    if(y) {
        *y = tp_dev.y;
    }
    return (tp_dev.sta & TP_PRES_DOWN) ? 1U : 0U;
}
u8 TP_Read_XY2(u16 *x, u16 *y) { return TP_Read_XY(x, y); }
void TP_Drow_Touch_Point(u16 x, u16 y, u16 color) { (void)x; (void)y; (void)color; }
void TP_Draw_Big_Point(u16 x, u16 y, u16 color) { (void)x; (void)y; (void)color; }
void TP_Adj_Info_Show(u16 x0, u16 y0, u16 x1, u16 y1, u16 x2, u16 y2, u16 x3, u16 y3, u16 fac)
{
    (void)x0; (void)y0; (void)x1; (void)y1;
    (void)x2; (void)y2; (void)x3; (void)y3; (void)fac;
}
void TP_Test_Run(u16 duration_ms) { (void)duration_ms; }
