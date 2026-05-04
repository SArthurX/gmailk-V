#ifndef _DEV_CONFIG_H_
#define _DEV_CONFIG_H_
/***********************************************************************************************************************
			------------------------------------------------------------------------
			|\\\																///|
			|\\\					Hardware interface							///|
			------------------------------------------------------------------------
***********************************************************************************************************************/
#include "Debug.h"
#ifdef USE_BCM2835_LIB
    #include <bcm2835.h>
#elif USE_WIRINGPI_LIB
    #include <wiringPi.h>
    #include <wiringPiSPI.h>
	#include <wiringPiI2C.h>
#elif USE_WIRINGX_LIB
    #include "wiringx.h"
#elif USE_DEV_LIB
    #include <lgpio.h>
    #define LFLAGS 0
    #define NUM_MAXBUF  4
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#define USE_SPI 1
#define USE_IIC 0

#ifndef WIRINGX_PLATFORM
#define WIRINGX_PLATFORM "milkv_duo256m"
#endif

#ifndef SPI_CHANNEL
#define SPI_CHANNEL 0
#endif

#ifndef SPI_SPEED_HZ
#define SPI_SPEED_HZ 10000000
#endif

#ifndef SPI_USE_HW_CS
#define SPI_USE_HW_CS 1
#endif

#define IIC_CMD        0X00
#define IIC_RAM        0X40


/**
 * data
**/
#define UBYTE   uint8_t
#define UWORD   uint16_t
#define UDOUBLE uint32_t

//OLED Define
#ifndef OLED_CS
#define OLED_CS         9
#endif

#ifndef OLED_RST
#define OLED_RST        20
#endif

#ifndef OLED_DC
#define OLED_DC         19
#endif


#define OLED_CS_0      DEV_Digital_Write(OLED_CS,0)
#define OLED_CS_1      DEV_Digital_Write(OLED_CS,1)

#define OLED_RST_0      DEV_Digital_Write(OLED_RST,0)
#define OLED_RST_1      DEV_Digital_Write(OLED_RST,1)

#define OLED_DC_0       DEV_Digital_Write(OLED_DC,0)
#define OLED_DC_1       DEV_Digital_Write(OLED_DC,1)

/*------------------------------------------------------------------------------------------------------*/

UBYTE DEV_ModuleInit(void);
void  DEV_ModuleExit(void);

void DEV_GPIO_Mode(UWORD Pin, UWORD Mode);
void DEV_Digital_Write(UWORD Pin, UBYTE Value);
UBYTE DEV_Digital_Read(UWORD Pin);
void DEV_Delay_ms(UDOUBLE xms);

void I2C_Write_Byte(uint8_t value, uint8_t Cmd);
void DEV_SPI_WriteByte(UBYTE Value);
void DEV_SPI_Write_nByte(uint8_t *pData, uint32_t Len);

#endif
