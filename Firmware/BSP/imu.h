/**
 * @file imu.h
 * @author LuYongping
 * @brief 
 * @version 0.1
 * @date 2025-07-10
 * 
 * @copyright Copyright (c) 2025 Hiwonder
 * 
 */


#ifndef __IMU_H__
#define __IMU_H__

#include <stdint.h>

// IMU数据结构体
typedef struct {
    float accel[3];  // 加速度数据 [x, y, z] (g)
    float gyro[3];   // 陀螺仪数据 [x, y, z] (dps)
    float temperature; // 温度 (°C)
    uint32_t timestamp; // 时间戳
} imu_data_t;


// 错误代码定义
#define IMU_OK                  (0)
#define IMU_ERROR               (-1)
#define IMU_TIMEOUT             (-2)


#endif
