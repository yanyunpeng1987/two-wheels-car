#ifndef __DELAY_H__
#define __DELAY_H__

#include <stdint.h>

// Register address
#define DWT_CTRL        *(uint32_t*)0xE0001000
#define DWT_CYCCNT      *(uint32_t*)0xE0001004
#define DEM_CR          *(uint32_t*)0xE000EDFC

//HAL_Delay() microsecond function overflow value
//CYCCNT register / main frequency is one thousandth, the maximum number of microseconds that can be contained
// 4294967295/(HAL_RCC_GetSysClockFreq()/1000)

#define __HAL_MAX_DELAY  4294967295/(HAL_RCC_GetSysClockFreq()/1000)

void delay_init(void);
void delay_us(uint32_t us);
void delay_ms(uint16_t ms);
void HAL_Delay_us(uint32_t us);
uint32_t DWT_CNT_GET(void);

#endif /* __DELAY_H__ */

