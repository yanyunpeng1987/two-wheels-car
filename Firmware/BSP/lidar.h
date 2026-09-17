// File: lidar.h
// Description: Lidar driver interface for MS200, using real-time data stream mode.

#ifndef INC_LIDAR_H_
#define INC_LIDAR_H_

#include "main.h"
#include <stdbool.h>
#include <stdint.h>

// --- 公开配置 ---
#define LIDAR_MAX_POINTS_IN_BUFFER 1000 // 驱动内部缓冲区的最大点数，防止溢出

void Lidar_ErrorCallback(UART_HandleTypeDef *huart);
typedef struct {
    float front_dist;
		float front_angle; 
    float back_dist;
    float left_dist;
    float right_dist;
		float right_front_dist; 
} LidarSectors_t;

extern LidarSectors_t g_sectors; 
void LidarApps_RunTasks(void);
void LidarApps_Init(void);
void LidarApps_Process(void);

// 单个雷达点的数据结构
typedef struct {
    float    angle;    // 角度 (0-359.99 度)
    float    distance; // 距离 (米)
    uint8_t  quality;  // 信号质量 (0-255)
} LidarPoint_t;

// --- 公开函数原型 ---

/**
 * @brief 初始化雷达驱动，并启动DMA接收
 * @param huart_handle 指向用于雷达通信的UART句柄
 */
void Lidar_Init(UART_HandleTypeDef *huart_handle);

/**
 * @brief 获取自上次调用以来所有新接收到的雷达点
 * @param out_points 用于存储点的缓冲区
 * @param max_points 缓冲区的最大容量
 * @return uint16_t 实际获取到的点的数量
 * @note  这是一个“破坏性”读取，获取后内部缓冲区会清零。
 */
uint16_t Lidar_GetNewPoints(LidarPoint_t* out_points, uint16_t max_points);

/**
 * @brief UART接收事件回调函数，必须在 `HAL_UARTEx_RxEventCallback` 中调用
 * @param huart 触发中断的UART句柄
 * @param Size 接收到的数据长度
 */
void Lidar_RxCallback(UART_HandleTypeDef *huart, uint16_t Size);

#endif /* INC_LIDAR_H_ */