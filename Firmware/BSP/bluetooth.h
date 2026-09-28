#ifndef BLUETOOTH_H
#define BLUETOOTH_H

#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include "usart.h"

// 蓝牙命令类型枚举
typedef enum {
    CMD_FORWARD = 0,
    CMD_BACKWARD,
    CMD_LEFT,
    CMD_RIGHT,
    CMD_STOP
} cmd_type_t;

// 外部变量声明
extern bool enable_bluetooth_output;
extern bool debug_to_bluetooth;
extern bool enable_angle_output;
extern int datw;

// 函数声明
bool send_response(const char *response);
void bluetooth_init(void);
void bluetooth_process_rx_data(char data);
void bluetooth_send_data(const uint8_t *data, uint16_t size);
void bluetooth_dma_rx_callback(UART_HandleTypeDef *huart, uint16_t length);
void bluetooth_tx_complete_callback(UART_HandleTypeDef *huart);
void bluetooth_handle_command(void);
void bluetooth_periodic_update(void);
void bluetooth_main_loop_task(void);
void bluetooth_uart_error_callback(UART_HandleTypeDef *huart);
void bluetooth_control_update(uint8_t normal_mode);

typedef struct {
    uint32_t rx_errors;
    uint32_t rx_restart_errors;
    uint32_t rx_overflows;
    uint32_t invalid_frames;
    uint32_t stale_frames;
    uint32_t tx_errors;
} BluetoothLinkStats;
extern volatile BluetoothLinkStats bluetooth_link_stats;
/* Motion-epoch rejects are also counted in stale_frames; service frames are kept. */
extern volatile uint32_t bluetooth_epoch_rejected_frames;
extern volatile uint32_t bluetooth_epoch_service_frames;
#define DEBUG_PRINT_BUFFER_SIZE     256


// 1. 在这里添加共享的宏定义
#define LOG_BUFFER_COUNT 32      // 能缓存8条日志
#define LOG_MSG_SIZE     128    // 每条日志最大长度

// 2. 在这里添加共享的结构体定义
typedef struct {
    uint8_t data[LOG_MSG_SIZE];
    uint16_t size;
} LogMessage;


// --- 为共享的全局变量添加 'extern' 声明 ---

// 3. 使用 'extern' 关键字声明环形缓冲区的全局变量，这样其他文件就知道它们的存在
extern volatile LogMessage g_log_ring_buffer[LOG_BUFFER_COUNT];
extern volatile uint16_t g_log_write_index;
extern volatile uint16_t g_log_read_index;



#endif /* BLUETOOTH_H */
