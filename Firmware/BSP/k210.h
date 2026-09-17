#ifndef __k210_H
#define __k210_H

#include "stm32f4xx_hal.h" // 包含HAL库主头文件
#include <stdbool.h>       // C语言布尔类型

// K210 WonderMV模块的I2C地址和寄存器定义
#define WONDERMV_ADDR           0x32  // K210模块的7位I2C从机地址
#define WONDERMV_I2C_TIMEOUT    10   // I2C通信超时时间，单位ms

#define COLOR_REG      0x01
#define OBJECT_REG     0x23

// BYTE_TO_HW宏定义 (用于将2个uint8_t合成一个uint16_t)
#define BYTE_TO_HW(A, B) ((((uint16_t)(A)) << 8) | (uint8_t)(B))


// K210模块返回结果的数据结构体
typedef struct {
  uint8_t id;
  uint16_t w;
  uint16_t h;
  uint16_t x;
  uint16_t y;
} MV_RESULT_ST;


typedef struct{
  uint8_t index;
}MV_RESULT_RECOG;


extern MV_RESULT_ST mv_result; 
extern MV_RESULT_RECOG mv_result_recog; 


// 对外接口函数声明
bool WonderMV_HAL_GetResult(uint8_t reg, MV_RESULT_ST* result);
bool WonderMV_HAL_GetINDEX(uint8_t reg, MV_RESULT_RECOG* result);

#endif // __k210_H
