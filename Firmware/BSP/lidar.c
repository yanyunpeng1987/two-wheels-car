// File: lidar.c
// Description: Lidar driver implementation for MS200, using real-time data stream mode.

#include "lidar.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdbool.h>

// --- 私有定义 ---
#define LIDAR_RX_DMA_BUFFER_SIZE  256
#define LIDAR_PARSE_BUFFER_SIZE   512
#define LIDAR_PACKET_HEADER       0x54
#define LIDAR_POINTS_PER_PACKET   12

// --- CRC-8 查表 ---
static const uint8_t crc8_table[256] = {
    0x00, 0x4d, 0x9a, 0xd7, 0x79, 0x34, 0xe3, 0xae, 0xf2, 0xbf, 0x68, 0x25, 0x8b, 0xc6, 0x11, 0x5c,
    0xa9, 0xe4, 0x33, 0x7e, 0xd0, 0x9d, 0x4a, 0x07, 0x5b, 0x16, 0xc1, 0x8c, 0x22, 0x6f, 0xb8, 0xf5,
    0x1f, 0x52, 0x85, 0xc8, 0x66, 0x2b, 0xfc, 0xb1, 0xed, 0xa0, 0x77, 0x3a, 0x94, 0xd9, 0x0e, 0x43,
    0xb6, 0xfb, 0x2c, 0x61, 0xcf, 0x82, 0x55, 0x18, 0x44, 0x09, 0xde, 0x93, 0x3d, 0x70, 0xa7, 0xea,
    0x3e, 0x73, 0xa4, 0xe9, 0x47, 0x0a, 0xdd, 0x90, 0xcc, 0x81, 0x56, 0x1b, 0xb5, 0xf8, 0x2f, 0x62,
    0x97, 0xda, 0x0d, 0x40, 0xee, 0xa3, 0x74, 0x39, 0x65, 0x28, 0xff, 0xb2, 0x1c, 0x51, 0x86, 0xcb,
    0x21, 0x6c, 0xbb, 0xf6, 0x58, 0x15, 0xc2, 0x8f, 0xd3, 0x9e, 0x49, 0x04, 0xaa, 0xe7, 0x30, 0x7d,
    0x88, 0xc5, 0x12, 0x5f, 0xf1, 0xbc, 0x6b, 0x26, 0x7a, 0x37, 0xe0, 0xad, 0x03, 0x4e, 0x99, 0xd4,
    0x7c, 0x31, 0xe6, 0xab, 0x05, 0x48, 0x9f, 0xd2, 0x8e, 0xc3, 0x14, 0x59, 0xf7, 0xba, 0x6d, 0x20,
    0xd5, 0x98, 0x4f, 0x02, 0xac, 0xe1, 0x36, 0x7b, 0x27, 0x6a, 0xbd, 0xf0, 0x5e, 0x13, 0xc4, 0x89,
    0x63, 0x2e, 0xf9, 0xb4, 0x1a, 0x57, 0x80, 0xcd, 0x91, 0xdc, 0x0b, 0x46, 0xe8, 0xa5, 0x72, 0x3f,
    0xca, 0x87, 0x50, 0x1d, 0xb3, 0xfe, 0x29, 0x64, 0x38, 0x75, 0xa2, 0xef, 0x41, 0x0c, 0xdb, 0x96,
    0x42, 0x0f, 0xd8, 0x95, 0x3b, 0x76, 0xa1, 0xec, 0xb0, 0xfd, 0x2a, 0x67, 0xc9, 0x84, 0x53, 0x1e,
    0xeb, 0xa6, 0x71, 0x3c, 0x92, 0xdf, 0x08, 0x45, 0x19, 0x54, 0x83, 0xce, 0x60, 0x2d, 0xfa, 0xb7,
    0x5d, 0x10, 0xc7, 0x8a, 0x24, 0x69, 0xbe, 0xf3, 0xaf, 0xe2, 0x35, 0x78, 0xd6, 0x9b, 0x4c, 0x01,
    0xf4, 0xb9, 0x6e, 0x23, 0x8d, 0xc0, 0x17, 0x5a, 0x06, 0x4b, 0x9c, 0xd1, 0x7f, 0x32, 0xe5, 0xa8
};

// --- 雷达原始包结构体 ---
#pragma pack(push, 1)
typedef struct {
    uint8_t  header;
    uint8_t  point_num;
    uint16_t speed;
    uint16_t start_angle;
    struct {
        uint16_t distance;
        uint8_t  intensity;
    } points[LIDAR_POINTS_PER_PACKET];
    uint16_t end_angle;
    uint16_t timestamp;
    uint8_t  crc8;
} LidarPacket_t;
#pragma pack(pop)


// --- 应用配置 ---
#define APP_MAX_POINTS 500
#define SECTOR_HALF_ANGLE 30.0f


// --- 全局变量定义 ---
LidarSectors_t g_sectors; 

// --- 外部变量和函数声明 ---
extern uint8_t running_mode;
int lidar_turn = 0;

// --- 静态(文件内)变量 ---
static LidarPoint_t g_raw_points_buffer[APP_MAX_POINTS];
static LidarSectors_t g_sectors_active;

// --- 前向声明 ---
static void update_sectors_with_new_points(LidarSectors_t* sectors, LidarPoint_t* points, uint16_t count);
static void reset_sectors(LidarSectors_t* sectors);
static float last_angle_of_previous_batch;
static uint32_t last_update_time_ms;



// --- 私有变量 ---
static UART_HandleTypeDef *g_lidar_huart;
static uint8_t g_rx_dma_buffer[LIDAR_RX_DMA_BUFFER_SIZE];
static uint8_t g_parse_buffer[LIDAR_PARSE_BUFFER_SIZE];
static uint16_t g_parse_buffer_idx = 0;

static LidarPoint_t g_lidar_points_buffer[LIDAR_MAX_POINTS_IN_BUFFER];
static volatile uint16_t g_new_points_count = 0;

// --- 私有函数原型 ---
static void Lidar_Parse(void);
static uint8_t ComputeCRC8(uint8_t *data, int len);

// --- 公开函数实现 ---
void Lidar_Init(UART_HandleTypeDef *huart_handle) {
    g_lidar_huart = huart_handle;
    HAL_UARTEx_ReceiveToIdle_DMA(g_lidar_huart, g_rx_dma_buffer, LIDAR_RX_DMA_BUFFER_SIZE);
}

uint16_t Lidar_GetNewPoints(LidarPoint_t* out_points, uint16_t max_points) {
    uint16_t count_to_copy = 0;
    
    __disable_irq();
    if (g_new_points_count > 0) {
        count_to_copy = (g_new_points_count < max_points) ? g_new_points_count : max_points;
        memcpy(out_points, g_lidar_points_buffer, count_to_copy * sizeof(LidarPoint_t));
        g_new_points_count = 0; // 取走后清零
    }
    __enable_irq();
    
    return count_to_copy;
}

void Lidar_RxCallback(UART_HandleTypeDef *huart, uint16_t Size) {
	    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_ORE)) {
        __HAL_UART_CLEAR_OREFLAG(huart); // 手动清除ORE标志
    }
			
    if (huart->Instance != g_lidar_huart->Instance) return;
    
    if (g_parse_buffer_idx + Size < LIDAR_PARSE_BUFFER_SIZE) {
        memcpy(&g_parse_buffer[g_parse_buffer_idx], g_rx_dma_buffer, Size);
        g_parse_buffer_idx += Size;
    } else {
        g_parse_buffer_idx = 0;
    }
    
    Lidar_Parse();
    
    HAL_UARTEx_ReceiveToIdle_DMA(g_lidar_huart, g_rx_dma_buffer, LIDAR_RX_DMA_BUFFER_SIZE);
}

// --- 私有函数实现 ---
static uint8_t ComputeCRC8(uint8_t *data, int len) {
    uint8_t crc = 0;
    while (len--) {
        crc = crc8_table[crc ^ *data++];
    }
    return crc;
}

static void Lidar_Parse(void) {
    const uint16_t packet_len = sizeof(LidarPacket_t);
    uint16_t consumed_bytes = 0;

    for (uint16_t i = 0; (i + packet_len) <= g_parse_buffer_idx; i++) {
        LidarPacket_t* packet = (LidarPacket_t*)&g_parse_buffer[i];

        if (packet->header != LIDAR_PACKET_HEADER) continue;
        if ((packet->point_num & 0x1F) != LIDAR_POINTS_PER_PACKET) continue;
        if (ComputeCRC8((uint8_t*)packet, packet_len - 1) != packet->crc8) continue;
        
        float start_angle = packet->start_angle / 100.0f;
        float end_angle = packet->end_angle / 100.0f;
        float angle_diff = end_angle - start_angle;
        if (angle_diff < 0) angle_diff += 360.0f;
        
        __disable_irq();
        for (int j = 0; j < LIDAR_POINTS_PER_PACKET; j++) {
            if (g_new_points_count < LIDAR_MAX_POINTS_IN_BUFFER) {
                LidarPoint_t* p = &g_lidar_points_buffer[g_new_points_count];
                
                p->distance = packet->points[j].distance / 1000.0f;
                p->quality = packet->points[j].intensity;
                
                if (LIDAR_POINTS_PER_PACKET > 1) {
                    p->angle = start_angle + (angle_diff / (float)(LIDAR_POINTS_PER_PACKET - 1)) * j;
                } else {
                    p->angle = start_angle;
                }
                if (p->angle >= 360.0f) p->angle -= 360.0f;

                if (p->distance > 0.01f) {
                   g_new_points_count++;
                }
            }
        }
        __enable_irq();
        
        consumed_bytes = i + packet_len;
        i += packet_len - 1;
    }
    
    if (consumed_bytes > 0 && consumed_bytes <= g_parse_buffer_idx) {
        memmove(g_parse_buffer, &g_parse_buffer[consumed_bytes], g_parse_buffer_idx - consumed_bytes);
        g_parse_buffer_idx -= consumed_bytes;
    }
}



void Lidar_ErrorCallback(UART_HandleTypeDef *huart) {
    // 检查是否是 Overrun 错误
    if (HAL_UART_GetError(huart) & HAL_UART_ERROR_ORE) {
        // 强制重启接收
        HAL_UART_DMAStop(huart);
        HAL_UARTEx_ReceiveToIdle_DMA(g_lidar_huart, g_rx_dma_buffer, LIDAR_RX_DMA_BUFFER_SIZE);
    }
}

void LidarApps_Init(void) {
    reset_sectors(&g_sectors_active);
    reset_sectors(&g_sectors);
	  last_angle_of_previous_batch = 0.0f;
    last_update_time_ms = 0;
}
void LidarApps_Process(void) {
    uint16_t new_points_count;
    new_points_count = Lidar_GetNewPoints(g_raw_points_buffer, APP_MAX_POINTS);

    if (new_points_count > 0) {
        bool revolution_completed = false;
        
        float first_angle_of_current_batch = g_raw_points_buffer[0].angle;
        uint32_t current_time_ms = HAL_GetTick(); // 获取当前系统时间

        if (last_angle_of_previous_batch > 300.0f && first_angle_of_current_batch < 60.0f) {
            revolution_completed = true;
        }
        last_angle_of_previous_batch = g_raw_points_buffer[new_points_count - 1].angle;
        
        // 如果检测到翻转，或距离上次更新超过一定时间，则认为一圈完成
        // 雷达转速为10Hz(100ms/圈)，设置超时为150ms
        if (revolution_completed || (current_time_ms - last_update_time_ms > 150)) {
            __disable_irq();
            memcpy(&g_sectors, &g_sectors_active, sizeof(LidarSectors_t));
            __enable_irq();

            reset_sectors(&g_sectors_active);
            last_update_time_ms = current_time_ms; // 关键：更新后记录当前时间
        }
        
        update_sectors_with_new_points(&g_sectors_active, g_raw_points_buffer, new_points_count);
    }

}

// --- 核心逻辑函数 ---
static void reset_sectors(LidarSectors_t* sectors) {
    sectors->front_dist = 999.0f;
		sectors->front_angle = 0.0f;
    sectors->back_dist  = 999.0f;
    sectors->left_dist  = 999.0f;
    sectors->right_dist = 999.0f;
		sectors->right_front_dist = 999.0f; 
}

static void update_sectors_with_new_points(LidarSectors_t* sectors, LidarPoint_t* points, uint16_t count) {
	
    for (uint16_t i = 0; i < count; i++) {
        float angle = points[i].angle;
        float dist = points[i].distance;

        if (dist <= 0.01f) continue;

        if (angle > (360.0f - SECTOR_HALF_ANGLE) || angle < SECTOR_HALF_ANGLE) {
            if (dist < sectors->front_dist) {
							sectors->front_dist = dist; sectors->front_angle = angle;}
        } else if (angle > (180.0f - SECTOR_HALF_ANGLE) && angle < (180.0f + SECTOR_HALF_ANGLE)) {
            if (dist < sectors->back_dist) sectors->back_dist = dist;
        } else if (angle > (270.0f - SECTOR_HALF_ANGLE) && angle < (270.0f + SECTOR_HALF_ANGLE)) {
            if (dist < sectors->left_dist) sectors->left_dist = dist;
        } else if (angle > (90.0f - SECTOR_HALF_ANGLE) && angle < (90.0f + SECTOR_HALF_ANGLE)) {
            if (dist < sectors->right_dist) sectors->right_dist = dist;
        }	else if (angle > 0.0f && angle < 60.0f) {
            if (dist < sectors->right_front_dist) sectors->right_front_dist = dist;
        }
    }
}


