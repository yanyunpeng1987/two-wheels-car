/**
 * @file bluetooth.c
 * @author Wang Ruifan
 * @brief 蓝牙发送与接收解读 
 * @version 0.3
 * @date 2025-01-16
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "bluetooth.h"
#include "main.h"
#include "control.h"
#include <string.h>
#include "persistent_storage.h"
#include <stdbool.h> 

// --- 宏定义 ---
#define BLUETOOTH_DMA_BUFFER_SIZE   128          // 单次DMA接收大小，可以适当增大
#define RX_RING_BUFFER_SIZE         1024        // 主接收环形缓冲区大小，必须足够大以容纳多个命令
#define MAX_CMD_LENGTH              128          // 最大命令长度
#define MAX_RESP_LENGTH             128         // 最大响应长度

// --- 静态变量 ---
static uint32_t last_periodic_update_tick = 0;// 用于周期性任务的时间戳

// DMA相关
static uint8_t rx_dma_buffers[2][BLUETOOTH_DMA_BUFFER_SIZE]; // DMA双缓冲
static uint32_t dma_buffer_index = 0;                       // 当前使用的DMA缓冲索引

// 环形缓冲区 (Ring Buffer) 用于中断和主循环解耦
static uint8_t g_rx_ring_buffer[RX_RING_BUFFER_SIZE];
static volatile uint16_t g_rx_write_index = 0; // 中断写入位置
static volatile uint16_t g_rx_read_index = 0;  // 主循环读取位置

// 命令解析相关
static char cmd_buffer[MAX_CMD_LENGTH] = {0}; // 命令组装缓冲区
static uint8_t cmd_index = 0;                 // 命令组装缓冲区索引

// 状态标志
static bool is_reporting_speed = false;
static bool is_reporting_distance = false;
static bool is_reporting_acceleration = false;

// 全局变量
bool enable_bluetooth_output = true; /* 是否输出数据到蓝牙 */

// --- 函数原型 ---
typedef bool (*CmdHandler)(const char *cmd_args, char *response);

// 命令结构体定义
typedef struct {
     char* cmd_type;   // 命令类型
    CmdHandler handler; // 命令处理函数指针
} CommandEntry;

// --- 命令处理函数声明  ---
static bool handle_pid(const char *cmd_args, char *response);
static bool handle_middle_angle(const char *cmd_args, char *response);
static bool handle_balance(const char *cmd_args, char *response);
static bool handle_speed(const char *cmd_args, char *response);
static bool handle_turn(const char *cmd_args, char *response);
static bool handle_report_speed(const char *cmd_args, char *response);
static bool handle_report_distance(const char *cmd_args, char *response);
static bool handle_report_acceleration(const char *cmd_args, char *response);
static bool handle_report_pid(const char *cmd_args, char *response);
static bool handle_report_voltage(const char *cmd_args, char *response);
static bool handle_report_init_pid(const char *cmd_args, char *response);
static bool handle_target_speed(const char *cmd_args, char *response);
int datw = 0;
// --- 命令处理表  ---
static const CommandEntry CMD_TABLE[] = {
    {"1", handle_middle_angle},
    {"2", handle_pid},
    {"3", handle_target_speed},
    {"4", handle_report_speed},
    {"5", handle_report_distance},
    {"6", handle_report_acceleration},
		{"7", handle_report_voltage},
		{"8", handle_report_pid},
    {0,  NULL} // 表结束标志
};

// 在文件顶部或函数外部定义新的状态
typedef enum {
    STATE_IDLE,                 // 等待 'C'
    STATE_GOT_C,                // 已收到 'C', 等待 'M'
    STATE_GOT_M,                // 已收到 'CM', 等待 'D'
    STATE_RECEIVING_PAYLOAD     // 已收到 "CMD", 正在接收数据
} ParserState;


volatile LogMessage g_log_ring_buffer[LOG_BUFFER_COUNT]; 
volatile uint16_t g_log_write_index = 0;                 
volatile uint16_t g_log_read_index = 0;                 

static char g_bt_tx_buffer[MAX_RESP_LENGTH];


// 定义一个静态变量来保存当前状态
static ParserState parser_state = STATE_IDLE;

// --- 内部辅助函数 ---
static void process_command(const char* command);

/******************************************************************************************/
/*                            初始化与中断处理                               */
/******************************************************************************************/

/**
 * @brief 蓝牙初始化
 */
void bluetooth_init(void)
{
    // 注册接收事件回调
    HAL_UART_RegisterRxEventCallback(&huart6, bluetooth_dma_rx_callback);
    // 开始DMA接收
    HAL_UARTEx_ReceiveToIdle_DMA(&huart6, rx_dma_buffers[dma_buffer_index], BLUETOOTH_DMA_BUFFER_SIZE);
}

/**
 * @brief 蓝牙DMA接收事件回调函数 
 * @brief  快速将DMA收到的数据存入环形缓冲区，然后立即退出。
 * @param huart UART句柄
 * @param Size 接收到的数据长度
 */
void bluetooth_dma_rx_callback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART6) 
    {
        uint32_t current_dma_idx = dma_buffer_index;
        dma_buffer_index = (dma_buffer_index + 1) % 2; // 切换DMA缓冲
			
        // 立即重新启动下一次DMA接收，确保通信不间断
        HAL_UARTEx_ReceiveToIdle_DMA(&huart6, rx_dma_buffers[dma_buffer_index], BLUETOOTH_DMA_BUFFER_SIZE);

        // 将数据从DMA缓冲区快速转移到环形缓冲区
        for (uint16_t i = 0; i < Size; i++)
        {
            uint16_t next_write_idx = (g_rx_write_index + 1) % RX_RING_BUFFER_SIZE;
            if (next_write_idx != g_rx_read_index) // 检查环形缓冲区是否已满
            {
                g_rx_ring_buffer[g_rx_write_index] = rx_dma_buffers[current_dma_idx][i];
                g_rx_write_index = next_write_idx;
            }
            else
            {
                break;
            }
        }
    }
}

/**
 * @brief 主循环任务函数
 * @brief 从环形缓冲区中取出数据，组装成完整指令并处理。
 * @note  此函数必须在 main 函数的 while(1) 循环中被持续调用。
 */

void bluetooth_main_loop_task(void)
{
    while (g_rx_read_index != g_rx_write_index)
    {
        char data = g_rx_ring_buffer[g_rx_read_index];
        g_rx_read_index = (g_rx_read_index + 1) % RX_RING_BUFFER_SIZE;

        switch (parser_state)
        {
            case STATE_IDLE:
                if (data == 'C') {
                    // 找到了帧头的第一个字符
                    cmd_index = 0; // 重置缓冲区索引
                    cmd_buffer[cmd_index++] = 'C';
                    parser_state = STATE_GOT_C; // 切换到下一个状态
                }
                // 如果不是'C'，则忽略该字节，状态保持不变
                break;

            case STATE_GOT_C:
                if (data == 'M') {
                    // 匹配成功，继续
                    cmd_buffer[cmd_index++] = 'M';
                    parser_state = STATE_GOT_M;
                } else {
                    // 匹配失败 (例如收到 "CX")，回到初始状态
                    parser_state = STATE_IDLE;
                    // 特殊情况：如果失败的字符是'C' (例如收到 "CC")，
                    // 我们可以直接开始新的匹配
                    if (data == 'C') {
                        cmd_index = 0;
                        cmd_buffer[cmd_index++] = 'C';
                        parser_state = STATE_GOT_C;
                    }
                }
                break;

            case STATE_GOT_M:
                if (data == 'D') {
                    // 帧头 "CMD" 完整匹配！
                    cmd_buffer[cmd_index++] = 'D';
                    parser_state = STATE_RECEIVING_PAYLOAD;
                } else {
                    // 匹配失败 (例如收到 "CMX")，回到初始状态
                    parser_state = STATE_IDLE;
                    // 同样，检查失败的字符是否为'C'
                    if (data == 'C') {
                        cmd_index = 0;
                        cmd_buffer[cmd_index++] = 'C';
                        parser_state = STATE_GOT_C;
                    }
                }
                break;

            case STATE_RECEIVING_PAYLOAD:
                if (data == '$') {
                    // 找到了帧尾，一条完整的指令接收完毕
                    cmd_buffer[cmd_index] = '\0'; // 添加字符串结束符
                    
                    // 注意：此时 cmd_buffer 中已经是 "CMD|1|-15.2"
                    // process_command 函数无需任何改动！
                    process_command(cmd_buffer);

                    // 重置所有状态，准备接收下一条指令
                    cmd_index = 0;
                    memset(cmd_buffer, 0, sizeof(cmd_buffer));
                    parser_state = STATE_IDLE;
                } else {
                    // 将数据存入缓冲区
                    if (cmd_index < sizeof(cmd_buffer) - 1) {
                        cmd_buffer[cmd_index++] = data;
                    } else {
                        // 缓冲区溢出，指令过长，视为非法指令
                        // 丢弃所有数据，回到初始状态
                        cmd_index = 0;
                        parser_state = STATE_IDLE;
                    }
                }
                break;
        } 
    } 
}

/******************************************************************************************/
/*                                 命令解析与执行                                        */
/******************************************************************************************/

/**
 * @brief 内部函数：处理一条完整的命令字符串
 * @param command 接收到的完整命令 (不包含结束符)
 */
static void process_command(const char* command)
{

       // 1. 基本检查和前缀验证
    if (command == NULL || strncmp(command, "CMD|", 4) != 0) {
        // 如果 command 为空 或 不是以 "CMD|" 开头，则为无效指令
        return;
    }

    bool cmd_handled = false;
    char response[MAX_RESP_LENGTH] = {0};

  // 2. 分离功能码和参数
    const char *body = command + 4; 
    
    //    查找参数分隔符 '|'
    const char *args_separator = strchr(body, '|');
    
    char function_code[16]; // 分配足够空间存储功能码
    const char *cmd_args;   // 指向正确的参数部分的指针

    if (args_separator != NULL) {
        size_t code_len = args_separator - body;
        if (code_len == 0 || code_len >= sizeof(function_code)) {
            return; // 功能码为空或太长，视为无效
        }
        
        strncpy(function_code, body, code_len);
        function_code[code_len] = '\0'; // 确保字符串结尾
        
        // 参数部分从 '|' 开始, cmd_args 指向 "|100|200"
        cmd_args = args_separator;
    } else {
        size_t code_len = strlen(body);
        if (code_len == 0 || code_len >= sizeof(function_code)) {
            return; // 功能码为空或太长
        }
        strcpy(function_code, body);
        cmd_args = ""; 
    }

    // 3. 查表并分发
    for (int i = 0; CMD_TABLE[i].handler != NULL; i++) {
        // 使用 strcmp 比较字符串内容
        if (strcmp(function_code, CMD_TABLE[i].cmd_type) == 0) {
            // 找到了匹配的功能码
            char response[MAX_RESP_LENGTH] = {0};
            
            // 调用对应的 handler，并传递正确的参数
            CMD_TABLE[i].handler(cmd_args, response);
            
            // 命令已处理，退出函数
            return; 
        }
    }

}


/******************************************************************************************/
/*                                 数据发送与周期性任务                                   */
/******************************************************************************************/

/**
 * @brief 蓝牙发送数据
 * @param data 要发送的数据
 * @param size 数据大小
 */

void bluetooth_send_data(const uint8_t *data, uint16_t size)
{
    if (enable_bluetooth_output && data != NULL && size > 0)
    {        
        uint32_t tickstart = HAL_GetTick();
        while ((huart6.gState & HAL_UART_STATE_BUSY_TX) == HAL_UART_STATE_BUSY_TX)
        {
            if ((HAL_GetTick() - tickstart) > 100)
            {
                HAL_UART_AbortTransmit(&huart6);
                return;
            }
        }

        if(HAL_UART_Transmit_DMA(&huart6, (uint8_t*)data, size) != HAL_OK)
        {
        }
    }
}


/**
 * @brief 蓝牙命令的响应消息发送
 * @param response 响应消息的字符串
 * @return true 字符串数据有效且发送成功
 * @return false 字符串数据无效或发送失败
 */
bool send_response(const char *response)
{
    if (response != NULL && response[0] > 0)
    {
        uint8_t msg_len = (uint8_t)response[0];
        if (msg_len > 0)
        {
            bluetooth_send_data((const uint8_t*)&response[1], msg_len);
            return true;
        }
    }
    return false;
}

/**
 * @brief 周期性处理需要持续发送的蓝牙数据
 * @note  此函数应在主循环中以固定频率调用
 */
void bluetooth_periodic_update(void)
{
	  if (HAL_GetTick() - last_periodic_update_tick >= 100) // 发送周期为100ms
    {
      // 更新时间戳，为下一次计时做准备
      last_periodic_update_tick = HAL_GetTick();
			
			// 为避免浮点数在不同上下文中值被改变，在函数开始时就拷贝
			int local_velocity_right = velocity_right;
			int local_velocity_left = velocity_left;
			int local_dis = dis;
			float local_accel_y = acceleration_y;
			float local_gyro_z = gyro_turn;
		
			// 检查是否需要报告控制参数
			if (is_reporting_speed)
			{
					snprintf(&g_bt_tx_buffer[1], sizeof(g_bt_tx_buffer) - 2, "CMD|4|%d|%d|$", local_velocity_right, local_velocity_left);
					g_bt_tx_buffer[0] = strlen(&g_bt_tx_buffer[1]);
					send_response(g_bt_tx_buffer);
					HAL_Delay(10);
			}
			if (is_reporting_distance)
			{
					snprintf(&g_bt_tx_buffer[1], sizeof(g_bt_tx_buffer) - 2, "CMD|5|%d|$",  local_dis);
					g_bt_tx_buffer[0] = strlen(&g_bt_tx_buffer[1]);
					send_response(g_bt_tx_buffer);
					HAL_Delay(10);
			}

			if (is_reporting_acceleration)
			{
					snprintf(&g_bt_tx_buffer[1], sizeof(g_bt_tx_buffer) - 2, "CMD|6|%.1f|%.1f|$", local_accel_y, local_gyro_z);
					g_bt_tx_buffer[0] = strlen(&g_bt_tx_buffer[1]);
					send_response(g_bt_tx_buffer);
					HAL_Delay(10);
			}
		}
}


static bool handle_middle_angle(const char *cmd_args, char *response)
{
	float value;
	if (sscanf(cmd_args + 1, "%f", &value) == 1)
	{
		pidparams.middle_angle = value ; // 设置中点角度
	  modify = 1;

	}
	return false; 
}

static bool handle_pid(const char *cmd_args, char *response)
{
		int sub_command;
		int param1, param2;
		if (sscanf(cmd_args, "|%d", &sub_command) != 1) {
        return false;
    }
	
    int items_scanned = sscanf(cmd_args, "|%d|%d|%d", &sub_command, &param1, &param2);

    if (items_scanned != 3) {
        return false;
    }

    switch (sub_command)
    {
        case 1:
            pidparams.balance_kp = param1;
            pidparams.balance_kd = param2;
						modify = 1;
            break;
				
        case 2:
            pidparams.velocity_kp = param1;
            pidparams.velocity_ki = param2;
						modify = 1;
            break;

        case 3:
            pidparams.turn_kp = param1;
            pidparams.turn_kd = param2;
						modify = 1;
            break;
       
        default:
            break;
    }
		return false;
	}


static bool handle_balance(const char *cmd_args, char *response)
{
	int kp_val, kd_val;
	int items_scanned = sscanf(cmd_args, "|%d|%d", &kp_val, &kd_val);
	if (items_scanned == 2){
		pidparams.balance_kp = kp_val;
		pidparams.balance_kd = kd_val;
	}
	return false;
}


static bool handle_speed(const char *cmd_args, char *response)
{
	int kp_val, ki_val;
	int items_scanned = sscanf(cmd_args, "|%d|%d", &kp_val, &ki_val);
	if (items_scanned == 2){
		pidparams.velocity_kp = kp_val;
		pidparams.velocity_ki = ki_val;
	}
	return false;
}


static bool handle_turn(const char *cmd_args, char *response)
{
	int kp_val, kd_val;
	int items_scanned = sscanf(cmd_args, "|%d|%d", &kp_val, &kd_val);
	if (items_scanned == 2){
		pidparams.turn_kp = kp_val;
		pidparams.turn_kd = kd_val;
	}
	return false;
}


static bool handle_report_speed(const char *cmd_args, char *response)
{
    int int_value;
    if (sscanf(cmd_args + 1, "%d", &int_value) == 1)
    {
        is_reporting_speed = (int_value == 1);
    }
    return false;
}

static bool handle_report_distance(const char *cmd_args, char *response)
{
    int int_value;
    if (sscanf(cmd_args+ 1, "%d", &int_value) == 1)
    {
        is_reporting_distance = (int_value == 1);
    }
    return false;
}


static bool handle_report_acceleration(const char *cmd_args, char *response)
{
    int int_value;
    if (sscanf(cmd_args+ 1, "%d", &int_value) == 1)
    {
        is_reporting_acceleration = (int_value == 1);
    }
    return false;
}


static bool handle_report_voltage(const char *cmd_args, char *response)
{
		int local_voltage = voltage * 1000;

    snprintf(&g_bt_tx_buffer[1], MAX_RESP_LENGTH - 2, "CMD|7|%d|$",
             (int)local_voltage);
    g_bt_tx_buffer[0] = strlen(&g_bt_tx_buffer[1]);
		send_response(g_bt_tx_buffer);
		HAL_Delay(10);
    return true; 
}

static bool handle_report_pid(const char *cmd_args, char *response)
{
    int sub_command = 1; // 默认为 1 

    // 1. 解析子命令
    if (cmd_args != NULL) {
        sscanf(cmd_args, "|%d", &sub_command);
    }

    // 2. 根据子命令决定是否重置PID值
    if (sub_command == 2) {
        // 子命令为 2，执行重置操作
        pidparams.middle_angle = -4.0f; 
        pidparams.balance_kp = 140;
        pidparams.balance_kd = 450;
        pidparams.velocity_kp = 30;
        pidparams.velocity_ki = 80;
        pidparams.turn_kp = 5;
        pidparams.turn_kd = 500;
    }

    // 3. 无论是否重置，都执行报告操作
    snprintf(&g_bt_tx_buffer[1], MAX_RESP_LENGTH - 2, "CMD|8|%.1f|%d|%d|%d|%d|%d|%d|$",
            pidparams.middle_angle,
             (int)pidparams.balance_kp,
             (int)pidparams.velocity_kp,
             (int)pidparams.turn_kp,
						 (int)pidparams.balance_kd,
						 (int)pidparams.velocity_ki,
             (int)pidparams.turn_kd);
    
    // 4. 发送响应
    g_bt_tx_buffer[0] = strlen(&g_bt_tx_buffer[1]);
    send_response(g_bt_tx_buffer);

    return true; // 表示命令成功处理并已发送响应
}

static bool handle_target_speed(const char *cmd_args, char *response)
{
	int speed_val, turn_val;
	int items_scanned = sscanf(cmd_args, "|%d|%d", &speed_val, &turn_val);
	
	if (items_scanned == 2){
		if (speed_val > 0){
			blue_back = 0;
			blue_front = 1;
		} else if(speed_val < 0){
			blue_front = 0;
			blue_back = 1;
		} else {
			blue_front = 0;
			blue_back = 0;
		}
	if (turn_val > 0){
			blue_left = 0;
			blue_right = 1;
		} else if(turn_val < 0){
			blue_right = 0;
			blue_left = 1;
		} else {
			blue_right = 0;
			blue_left = 0;
		}
	}
	return false;
}
