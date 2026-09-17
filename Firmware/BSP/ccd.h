#ifndef __CCD_H
#define __CCD_H

#include "main.h"
#include "stm32f4xx_hal.h"
#include <stdint.h>

#define CCD_SI_Pin          GPIO_PIN_3
#define CCD_SI_GPIO_Port    GPIOB
#define CCD_AO_Pin 					GPIO_PIN_0
#define CCD_AO_GPIO_Port 		GPIOC
#define CCD_CLK_Pin         GPIO_PIN_15
#define CCD_CLK_GPIO_Port   GPIOA

extern uint16_t ADV[128];

void ccd_init(void);
void ccd_read_data(void);
void print_ccd_data(void);

#endif
