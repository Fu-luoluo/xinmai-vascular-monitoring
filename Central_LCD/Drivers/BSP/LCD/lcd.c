//////////////////////////////////////////////////////////////////////////////////	 
//???????????????????????????????????????????????
//??????????????STM32F103RCT6,???????MiniSTM32??????,???72MHZ??????12MHZ
//QDtech-TFT??????? for STM32 IO???
//xiao??@ShenZhen QDtech co.,LTD
//??????:www.qdtft.com
//????????http://qdtech.taobao.com
//wiki?????????http://www.lcdwiki.com
//??????????????????????????????????
//???(????) :+86 0755-23594567 
//???:15989313508?????? 
//????:lcdwiki01@gmail.com    support@lcdwiki.com    goodtft@163.com 
//???????QQ:3002773612  3002778157
//????????QQ?:324828016
//????????:2018/08/09
//?????V1.0
//?????????????????
//Copyright(C) ?????????????????????? 2018-2028
//All rights reserved
/****************************************************************************************************
//=========================================???????================================================//
//     LCD???                STM32?????
//      VCC          ??        DC5V/3.3V      //???
//      GND          ??          GND          //?????
//=======================================??????????????==========================================//
//?????????????????????SPI????
//     LCD???                STM32?????    
//    SDI(MOSI)      ??          PB15         //?????SPI?????????????
//    SDO(MISO)      ??          PB14         //?????SPI??????????????????????????????????
//=======================================??????????????==========================================//
//     LCD??? 					      STM32????? 
//       LED         ??          PB9          //???????????????????????????????5V??3.3V
//       SCK         ??          PB13         //?????SPI??????????
//      DC/RS        ??          PB10         //?????????/??????????
//       RST         ??          PB12         //????????????????
//       CS          ??          PB11         //??????????????
//=========================================????????????=========================================//
//????????????????????????????????????????????????????????????????????
//	   LCD???                STM32????? 
//      T_IRQ        ??          PC10         //?????????????????
//      T_DO         ??          PC2          //??????SPI????????
//      T_DIN        ??          PC3          //??????SPI?????????
//      T_CS         ??          PC13         //???????????????
//      T_CLK        ??          PC0          //??????SPI??????????
**************************************************************************************************/	
 /* @attention
  *
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, QD electronic SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
**************************************************************************************************/	
#include "lcd.h"
#include "stdlib.h"
#include "tft_delay.h"
#include "SPI.h"

	   
//????LCD???????
//????????
_lcd_dev lcddev;

//???????,???????
u16 POINT_COLOR = 0x0000,BACK_COLOR = 0xFFFF;  
u16 DeviceCode;	 

/*****************************************************************************
 * @name       :void LCD_WR_REG(u8 data)
 * @date       :2018-08-09 
 * @function   :Write an 8-bit command to the LCD screen
 * @parameters :data:Command value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_REG(u8 data)
{ 
   	LCD_CS_CLR();     
	LCD_RS_CLR();	  
   	SPI_WriteByte(SPI2,data);
   	LCD_CS_SET();	
}

/*****************************************************************************
 * @name       :void LCD_WR_DATA(u8 data)
 * @date       :2018-08-09 
 * @function   :Write an 8-bit data to the LCD screen
 * @parameters :data:data value to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WR_DATA(u8 data)
{
   	LCD_CS_CLR();
	LCD_RS_SET();
   	SPI_WriteByte(SPI2,data);
   	LCD_CS_SET();
}

/*****************************************************************************
 * @name       :void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue)
 * @date       :2018-08-09 
 * @function   :Write data into registers
 * @parameters :LCD_Reg:Register address
                LCD_RegValue:Data to be written
 * @retvalue   :None
******************************************************************************/
void LCD_WriteReg(u8 LCD_Reg, u16 LCD_RegValue)
{	
	LCD_WR_REG(LCD_Reg);  
	LCD_WR_DATA(LCD_RegValue);	    		 
}	   

/*****************************************************************************
 * @name       :void LCD_WriteRAM_Prepare(void)
 * @date       :2018-08-09 
 * @function   :Write GRAM
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	 
void LCD_WriteRAM_Prepare(void)
{
	LCD_WR_REG(lcddev.wramcmd);
}	 

/*****************************************************************************
 * @name       :void Lcd_WriteData_16Bit(u16 Data)
 * @date       :2018-08-09 
 * @function   :Write an 16-bit command to the LCD screen
 * @parameters :Data:Data to be written
 * @retvalue   :None
******************************************************************************/	 
void Lcd_WriteData_16Bit(u16 Data)
{	
   	LCD_CS_CLR();
   	LCD_RS_SET();  
   	SPI_WriteByte(SPI2,Data>>8);
	SPI_WriteByte(SPI2,Data);
   	LCD_CS_SET();
}

/*****************************************************************************
 * @name       :void LCD_DrawPoint(u16 x,u16 y)
 * @date       :2018-08-09 
 * @function   :Write a pixel data at a specified location
 * @parameters :x:the x coordinate of the pixel
                y:the y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/	
void LCD_DrawPoint(u16 x,u16 y)
{
	LCD_SetCursor(x,y);//??????????? 
	Lcd_WriteData_16Bit(POINT_COLOR); 
}

/*****************************************************************************
 * @name       :void LCD_Clear(u16 Color)
 * @date       :2018-08-09 
 * @function   :Full screen filled LCD screen
 * @parameters :color:Filled color
 * @retvalue   :None
******************************************************************************/	
void LCD_Clear(u16 Color)
{
  unsigned int i,m;  
	LCD_SetWindows(0,0,lcddev.width-1,lcddev.height-1);   
	LCD_CS_CLR();
	LCD_RS_SET();
	for(i=0;i<lcddev.height;i++)
	{
    for(m=0;m<lcddev.width;m++)
    {	
			Lcd_WriteData_16Bit(Color);
		}
	}
	LCD_CS_SET();
} 

/*****************************************************************************
 * @name       :void LCD_GPIOInit(void)
 * @date       :2018-08-09 
 * @function   :Initialization LCD screen GPIO
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	
void LCD_GPIOInit(void)
{
	GPIOA_SetBits(GPIO_Pin_3 | GPIO_Pin_4 | LCD_BL_PIN | GPIO_Pin_12);
	GPIOA_ModeCfg(GPIO_Pin_3 | GPIO_Pin_4 | LCD_BL_PIN | GPIO_Pin_12, GPIO_ModeOut_PP_5mA);
}

/*****************************************************************************
 * @name       :void LCD_RESET(void)
 * @date       :2018-08-09 
 * @function   :Reset LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	
void LCD_RESET(void)
{
	LCD_RST_CLR();
	delay_ms(100);	
	LCD_RST_SET();
	delay_ms(50);
}

/*****************************************************************************
 * @name       :void LCD_RESET(void)
 * @date       :2018-08-09 
 * @function   :Initialization LCD screen
 * @parameters :None
 * @retvalue   :None
******************************************************************************/	 	 
void LCD_Init(void)
{  
	SPI2_Init();
	LCD_GPIOInit();
 	LCD_RESET();
	/* 3.5" ST7796S IPS — 移植自 MSP3525/3526 Hardware SPI 商家例程 */
	LCD_WR_REG(0x11);
	delay_ms(120);

	LCD_WR_REG(0x36);
	LCD_WR_DATA(0x48);

	LCD_WR_REG(0x3A);
	LCD_WR_DATA(0x55);

	LCD_WR_REG(0xF0);
	LCD_WR_DATA(0xC3);
	LCD_WR_REG(0xF0);
	LCD_WR_DATA(0x96);

	LCD_WR_REG(0xB4);
	LCD_WR_DATA(0x02);

	LCD_WR_REG(0xB7);
	LCD_WR_DATA(0xC6);

	LCD_WR_REG(0xC0);
	LCD_WR_DATA(0xC0);
	LCD_WR_DATA(0x00);

	LCD_WR_REG(0xC1);
	LCD_WR_DATA(0x13);

	LCD_WR_REG(0xC2);
	LCD_WR_DATA(0xA7);

	LCD_WR_REG(0xC5);
	LCD_WR_DATA(0x21);

	LCD_WR_REG(0xE8);
	LCD_WR_DATA(0x40);
	LCD_WR_DATA(0x8A);
	LCD_WR_DATA(0x1B);
	LCD_WR_DATA(0x1B);
	LCD_WR_DATA(0x23);
	LCD_WR_DATA(0x0A);
	LCD_WR_DATA(0xAC);
	LCD_WR_DATA(0x33);

	LCD_WR_REG(0xE0);
	LCD_WR_DATA(0xD2);
	LCD_WR_DATA(0x05);
	LCD_WR_DATA(0x08);
	LCD_WR_DATA(0x06);
	LCD_WR_DATA(0x05);
	LCD_WR_DATA(0x02);
	LCD_WR_DATA(0x2A);
	LCD_WR_DATA(0x44);
	LCD_WR_DATA(0x46);
	LCD_WR_DATA(0x39);
	LCD_WR_DATA(0x15);
	LCD_WR_DATA(0x15);
	LCD_WR_DATA(0x2D);
	LCD_WR_DATA(0x32);

	LCD_WR_REG(0xE1);
	LCD_WR_DATA(0x96);
	LCD_WR_DATA(0x08);
	LCD_WR_DATA(0x0C);
	LCD_WR_DATA(0x09);
	LCD_WR_DATA(0x09);
	LCD_WR_DATA(0x25);
	LCD_WR_DATA(0x2E);
	LCD_WR_DATA(0x43);
	LCD_WR_DATA(0x42);
	LCD_WR_DATA(0x35);
	LCD_WR_DATA(0x11);
	LCD_WR_DATA(0x11);
	LCD_WR_DATA(0x28);
	LCD_WR_DATA(0x2E);

	LCD_WR_REG(0xF0);
	LCD_WR_DATA(0x3C);
	LCD_WR_REG(0xF0);
	LCD_WR_DATA(0x69);

	delay_ms(120);
	LCD_WR_REG(0x21); /* IPS invert */
	LCD_WR_REG(0x29);

  	LCD_direction(USE_HORIZONTAL);
	LCD_LED_ON();
	LCD_Clear(WHITE);
}

void LCD_DisplaySleep(void)
{
	LCD_WR_REG(0x10);
	delay_ms(120);
}

void LCD_DisplayWake(void)
{
	LCD_WR_REG(0x11);
	delay_ms(120);
	LCD_WR_REG(0x29);
}
 
/*****************************************************************************
 * @name       :void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd)
 * @date       :2018-08-09 
 * @function   :Setting LCD display window
 * @parameters :xStar:the bebinning x coordinate of the LCD display window
								yStar:the bebinning y coordinate of the LCD display window
								xEnd:the endning x coordinate of the LCD display window
								yEnd:the endning y coordinate of the LCD display window
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetWindows(u16 xStar, u16 yStar,u16 xEnd,u16 yEnd)
{	
	LCD_WR_REG(lcddev.setxcmd);	
	LCD_WR_DATA(xStar>>8);
	LCD_WR_DATA(0x00FF&xStar);		
	LCD_WR_DATA(xEnd>>8);
	LCD_WR_DATA(0x00FF&xEnd);

	LCD_WR_REG(lcddev.setycmd);	
	LCD_WR_DATA(yStar>>8);
	LCD_WR_DATA(0x00FF&yStar);		
	LCD_WR_DATA(yEnd>>8);
	LCD_WR_DATA(0x00FF&yEnd);

	LCD_WriteRAM_Prepare();	//???????GRAM			
}   

/*****************************************************************************
 * @name       :void LCD_SetCursor(u16 Xpos, u16 Ypos)
 * @date       :2018-08-09 
 * @function   :Set coordinate value
 * @parameters :Xpos:the  x coordinate of the pixel
								Ypos:the  y coordinate of the pixel
 * @retvalue   :None
******************************************************************************/ 
void LCD_SetCursor(u16 Xpos, u16 Ypos)
{	  	    			
	LCD_SetWindows(Xpos,Ypos,Xpos,Ypos);	
} 

/*****************************************************************************
 * @name       :void LCD_direction(u8 direction)
 * @date       :2018-08-09 
 * @function   :Setting the display direction of LCD screen
 * @parameters :direction:0-0 degree
                          1-90 degree
													2-180 degree
													3-270 degree
 * @retvalue   :None
******************************************************************************/ 
void LCD_direction(u8 direction)
{ 
	lcddev.setxcmd=0x2A;
	lcddev.setycmd=0x2B;
	lcddev.wramcmd=0x2C;
	lcddev.dir = (u8)(direction % 4U);
	/* MADCTL 与商家 ST7796S 例程一致；dir 供 FT6336 坐标映射 */
	switch(lcddev.dir){
		case 0:
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;
			LCD_WriteReg(0x36,(1<<3)|(1<<6));
		break;
		case 1:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(1<<3)|(1<<5));
		break;
		case 2:
			lcddev.width=LCD_W;
			lcddev.height=LCD_H;
			LCD_WriteReg(0x36,(1<<3)|(1<<7));
		break;
		case 3:
			lcddev.width=LCD_H;
			lcddev.height=LCD_W;
			LCD_WriteReg(0x36,(1<<3)|(1<<7)|(1<<6)|(1<<5));
		break;
		default:break;
	}
}	 
