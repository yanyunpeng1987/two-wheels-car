#ifndef __FLASH_IF_H
#define __FLASH_IF_H

#include "stm32f4xx_hal.h"

// 定义PID参数结构体
typedef struct {
	  float middle_angle;
    int balance_kp;
    int balance_kd;
    int velocity_kp;
    int velocity_ki;
    int turn_kp;
    int turn_kd;
		uint32_t magic_number;
} PID_Params;

extern PID_Params pidparams;

// 1. 定义存储区域的地址和扇区 (根据STM32F401RBT6)
#define FLASH_PID_STORE_SECTOR    FLASH_SECTOR_1            // 使用Sector 1
#define FLASH_PID_STORE_ADDRESS   0x08004000                // Sector 1 的起始地址

// 2. 声明外部函数
HAL_StatusTypeDef Write_PID_To_Flash(PID_Params *params);
void Read_PID_From_Flash(PID_Params *params);

#endif /* __FLASH_IF_H */