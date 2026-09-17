#include "main.h"
#include "control.h"
#include "wondermind.h"
#include "control.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "i2c.h"
#include "buzzer.h"

// --- I2C & 协议定义 ---
#define WONDERMIND_SLAVE_ADDRESS (0x55 << 1)
#define I2C_TIMEOUT              100 // ms
#define POLL_INTERVAL_MS         100 // ms

// --- 外部变量引用 ---
extern I2C_HandleTypeDef hi2c3;

// --- 内部静态变量 ---
static uint32_t last_poll_tick = 0;
static char rx_json_buffer[1024];
MotionState_t motion_state = MOTION_IDLE;
int motion_target_distance = 0;
float motion_travelled_distance = 0;
int motion_target_angle = 0;
float motion_turned_angle = 0.0f;

// --- 静态函数原型 ---
static void parse_command(const char* json_str);
static bool send_frame(const uint8_t* data, uint16_t len);
static bool receive_frame(uint8_t* buffer, uint16_t* len);
static bool WonderMind_Receive_Data(uint8_t* buffer, uint16_t size);
static uint8_t calculate_checksum(const uint8_t* data, uint16_t len);
static bool register_tools(void);
static float turn_start_angle = 0.0f;
volatile HAL_StatusTypeDef demo ;
uint8_t dummy_buffer[64];
uint8_t header[4];
float current_speed_cms;
int WonderMind = 0;
#define WONDERMIND_MAX_FRAME_SIZE 256
static uint8_t i2c_rx_buffer[WONDERMIND_MAX_FRAME_SIZE];



bool WonderMind_Init(void) {
    uint32_t start_tick = HAL_GetTick(); // 记录开始时间

    while (1) {
        // 1. 检查设备是否就绪，使用较短的超时时间
        HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(&hi2c3, WONDERMIND_SLAVE_ADDRESS, 3, 50);

        if (status == HAL_OK) {
            // 设备找到，执行初始化序列
            hi2c3.Init.ClockSpeed = 400000;
            if (HAL_I2C_Init(&hi2c3) != HAL_OK) {
                // 如果提速失败，恢复并返回错误
                hi2c3.Init.ClockSpeed = 20000;
                HAL_I2C_Init(&hi2c3);
                return false;
            }
            HAL_Delay(5);

            register_tools(); // 调用MCP写入工具
						WonderMind_MCP_Finish(); // 发送MCP注册完成指令
						WonderMind = 1;
            buzzers[0].beep(&buzzers[0], 1000, 50, 50, 2);
            // 恢复I2C速率
            hi2c3.Init.ClockSpeed = 20000;
            HAL_I2C_Init(&hi2c3);
            HAL_Delay(5); // 稳定总线
            
            return true; // 初始化成功，正确退出
        }else{
            return false;
        }

    }
    
}

/**
 * @brief 在固定的时间节拍下，执行一步运动控制状态机的更新
 * @param dt 固定的时间步长 (单位：秒)，由调用者（中断）提供
 */

void Motion_Control_Update_Tick(float dt) {
    // 使用一个静态变量来处理刹车计时，因为它需要跨次调用保持状态
    static uint16_t braking_timer_ms = 0;

    // 1. 根据当前的运动状态，执行不同的逻辑
    switch (motion_state) {
        
        case MOTION_IDLE:
            // 空闲状态，什么也不做
            return; // 直接返回

        case MOTION_RUNNING:
            // --- 正在执行“直行”任务 ---
            {
                float current_speed_cms = (velocity_left + velocity_right) / 2.0f;
                motion_travelled_distance += current_speed_cms * dt;

                if (fabs(motion_travelled_distance) >= abs(motion_target_distance)) {
                    motion_state = MOTION_BRAKING; // 到达目标距离，切换到刹车
                    braking_timer_ms = 0; // **关键：在切换状态时，重置刹车计时器**
                }
            }
            break;

        case MOTION_RUNNING_ANGLE:
            // --- 正在执行“转向”任务 ---
            {
                float angle_turned = current_yaw_angle_degrees - turn_start_angle;
                
                motion_turned_angle = angle_turned;

                if (fabs(motion_turned_angle) >= abs(motion_target_angle)) {
                    motion_state = MOTION_BRAKING; // 到达目标角度，切换到刹车
                    braking_timer_ms = 0; // **关键：在切换状态时，重置刹车计时器**
                }
            }
            break;

        case MOTION_BRAKING:
            // --- 正在执行“刹车”任务 ---
            braking_timer_ms += (dt * 1000); // 累加时间 (ms)
            
            if (braking_timer_ms >= 150) {
                motion_state = MOTION_IDLE; // 刹车结束，返回空闲
                WonderMind_Send_Action_Finish();
            }
            break;

        default:
            motion_state = MOTION_IDLE;
            break;
    }
}


/**
 * @brief 在主循环中轮询并处理来自WonderMind的指令
 */

void WonderMind_Poll_And_Process(void) {

		if (WonderMind == 1) {
				Detection_WonderMind = 1;
				if (HAL_GetTick() - last_poll_tick < POLL_INTERVAL_MS) {
						return;
				}
				last_poll_tick = HAL_GetTick();

				uint16_t received_len = sizeof(rx_json_buffer);
				if (receive_frame((uint8_t*)rx_json_buffer, &received_len)) {
						if (received_len > 0) {
								rx_json_buffer[received_len] = '\0';
								parse_command(rx_json_buffer);
						}
				}
		}else{
			Detection_WonderMind = 0;
	}
}

/**
 * @brief 发送动作执行成功的响应
 */

void WonderMind_Send_Action_Finish(void) {
    char json_str[64];
    snprintf(json_str, sizeof(json_str), "{\"command\":\"action_finish\",\"params\":\"true\"}");
    send_frame((uint8_t*)json_str, strlen(json_str));
}

/**
 * @brief 发送小车状态信息
 */
void WonderMind_Send_Status(const char* params_str) {
    char json_str[256];
    snprintf(json_str, sizeof(json_str), "{\"command\":\"status\",\"params\":[%s]}", params_str);
    send_frame((uint8_t*)json_str, strlen(json_str));
}


/**
 * @brief 向 WonderMind 模块发送MCP工具注册成功信号
 */
void WonderMind_MCP_Finish(void) {
    char json_str[64];
    
    // 使用 snprintf 安全地构建 JSON 字符串
    snprintf(json_str, sizeof(json_str), 
             "{\"tool_name\":\"mcu.request\",\"mcp_setting\":\"vision\",\"params\":\"true\"}");
    // 调用已有的 send_frame 函数发送这个请求
    send_frame((uint8_t*)json_str, strlen(json_str));
		
}

/**
 * @brief 向 WonderMind 模块请求进行一次视觉识别
 * @param prompt 描述你想让大模型做什么的字符串
 */
void WonderMind_Request_Vision(const char* prompt) {
    char json_str[512];
    
    // 使用 snprintf 安全地构建 JSON 字符串
    snprintf(json_str, sizeof(json_str), 
             "{\"tool_name\":\"mcu.request\",\"command\":\"vision\",\"params\":\"%s\"}", 
             prompt);
    // 调用已有的 send_frame 函数发送这个请求
    send_frame((uint8_t*)json_str, strlen(json_str));
		
}

/**
 * @brief 解析收到的JSON指令字符串
 */

static void parse_command(const char* json_str) {
	
		 if (strstr(json_str, "move") != NULL) {
        int distance = 10;
        char* dist_ptr = strstr(json_str, "distance");
        char* speed_ptr = strstr(json_str, "speed");
				if (dist_ptr) sscanf(dist_ptr, "%*[^:]:%d", &distance);
			   
			 if (strstr(json_str, "\"backward\"") != NULL) {
            // 如果是后退，将距离值取反
            distance = -distance/2;
        }else{
					distance = distance/3*2;
				}
        
        if (motion_state == MOTION_IDLE) {
            motion_target_distance = distance;
            motion_travelled_distance = 0;
						velocity_multiple = 5;
            if (distance > 0)
							{ voice_front = 1; voice_back = 0; } 
            else 
							{ voice_front = 0; voice_back = 1; }
            motion_state = MOTION_RUNNING;
        }
    }
		 else if (strstr(json_str, "direction") != NULL) {
        int angle_val = 90;
        char* angle_ptr = strstr(json_str, "angle");
        if (angle_ptr) sscanf(angle_ptr, "%*[^:]:%d", &angle_val);

        if (motion_state == MOTION_IDLE) {
            if (strstr(json_str, "left")) {
								voice_left = 1; 
								voice_right = 0; 
								line_patrol_turn = 600;
                motion_target_angle = angle_val;
            } else { 
								voice_left = 0; 
								voice_right = 1; 
								line_patrol_turn = -600;
                motion_target_angle = angle_val;
            }
            
            motion_turned_angle = 0.0f;
            turn_start_angle = current_yaw_angle_degrees; 
            motion_state = MOTION_RUNNING_ANGLE;
        }
    }
    else if (strstr(json_str, "running_mode") != NULL) {
			if (strstr(json_str, "avoid")) {
					running_mode = 12;
					mode = 12;
			} 
			if (strstr(json_str, "line_patrol")) {
					running_mode = 8;
					mode = 8;
			} 
			if (strstr(json_str, "smart_line_patrol")) {
					running_mode = 13;
					mode = 13;
			} 
			if (strstr(json_str, "normal")) {
					running_mode = 0;
					mode = 0;
			} 
			if (strstr(json_str, "distance")) {
				char* dist_ptr = strstr(json_str, "distance");
				if (dist_ptr) sscanf(dist_ptr, "%*[^0-9-]%d", &avoid_distance);
			}
        
    WonderMind_Send_Action_Finish();
		}
    else if (strstr(json_str, "status_name") != NULL) {
        char params_str[128] = {0};
        if (strstr(json_str, "battery")) {
            char temp[32];
            sprintf(temp, "[\"battery\",\"%.1f\",\"V\"],", voltage);
            strcat(params_str, temp);
        }
        if (strstr(json_str, "angle")) {
            char temp[32];
            sprintf(temp, "[\"angle\",\"%.1f\"],", angle_balance);
            strcat(params_str, temp);
        }
				if (strstr(json_str, "distance")) {
            char temp[32];
            sprintf(temp, "[\"distance\",\"%d\",\"cm\"],", dis);
            strcat(params_str, temp);
        }
				if (strstr(json_str, "running_mode")) {
            char temp[32];
            sprintf(temp, "[\"running_mode\",\"%d\"],", running_mode);
            strcat(params_str, temp);
        }
        if (strlen(params_str) > 0) params_str[strlen(params_str) - 1] = '\0';
        WonderMind_Send_Status(params_str);
    }
		
		else if (strstr(json_str, "\"lr\"") != NULL) {
        // 设置默认值（黑色）
        int lr = 0, lg = 0, lb = 0;
        int rr = 0, rg = 0, rb = 0;

  			char* ptr;
        
        ptr = strstr(json_str, "\"lr\"");
				if (ptr) sscanf(ptr, "%*[^:]:%d", &lr);

				ptr = strstr(json_str, "\"lg\"");
				if (ptr) sscanf(ptr, "%*[^:]:%d", &lg);

				ptr = strstr(json_str, "\"lb\"");
				if (ptr) sscanf(ptr, "%*[^:]:%d", &lb);

				ptr = strstr(json_str, "\"rr\"");
				if (ptr) sscanf(ptr, "%*[^:]:%d", &rr);

				ptr = strstr(json_str, "\"rg\"");
				if (ptr) sscanf(ptr, "%*[^:]:%d", &rg);

				ptr = strstr(json_str, "\"rb\"");
				if (ptr) sscanf(ptr, "%*[^:]:%d", &rb);

        // 将解析出的值赋给全局的RGB数组
        // 注意进行范围检查和类型转换
        rgb_left[0] = (lr > 255) ? 255 : (lr < 0 ? 0 : lr);
        rgb_left[1] = (lg > 255) ? 255 : (lg < 0 ? 0 : lg);
        rgb_left[2] = (lb > 255) ? 255 : (lb < 0 ? 0 : lb);

        rgb_right[0] = (rr > 255) ? 255 : (rr < 0 ? 0 : rr);
        rgb_right[1] = (rg > 255) ? 255 : (rg < 0 ? 0 : rg);
        rgb_right[2] = (rb > 255) ? 255 : (rb < 0 ? 0 : rb);

        // 任务完成，发送结束信号
        WonderMind_Send_Action_Finish();
    }
		
		else if (strstr(json_str, "count") != NULL) {
			int count = 0; 
			char* ptr;
			ptr = strstr(json_str, "\"count\"");
			if (ptr) sscanf(ptr, "%*[^:]:%d", &count);
			buzzers[0].beep(&buzzers[0], 1000, 50, 50, count); 
			WonderMind_Send_Action_Finish();
    }
				
}

/**
 * @brief 向WonderMind模块注册所有可用的工具(功能)
 */
static bool register_tools(void) {
 
		const char* tool_move = "{\"tool_name\":\"self.balance_car.move\","
			"\"command\":\"控制平衡小车移动时调用这个工具。distance参数控制距离，单位是厘米(cm)，正数前进，负数后退。\","
			"\"params\":[[\"move\",\"string\"],[\"distance\",\"int\",-200,200]],\"block\":\"true\",\"return\":\"false\"}";
		if (!send_frame((uint8_t*)tool_move, strlen(tool_move))) return false;
			
    const char* tool_turn = "{\"tool_name\":\"self.balance_car.turn\","
			"\"command\":\"控制平衡小车原地旋转时调用这个工具。direction参数控制方向，'left'或'right'。angle参数控制旋转的角度，单位是度(°)。\","
			"\"params\":[[\"direction\",\"string\"],[\"angle\",\"int\",1,360]],\"block\":\"true\",\"return\":\"false\"}";
    if (!send_frame((uint8_t*)tool_turn, strlen(tool_turn))) return false;
		
		
    const char* tool_status = "{\"tool_name\":\"self.balance_car.get_status\",\"command\":\"获取平衡小车的实时状态时调用这个工具。"
			"可查询的状态包括：'battery', 'angle', 'distance', 'running_mode'。\",\"params\":[[\"status_name\",\"string\"]],\"block\":\"true\",\"return\":\"true\"}";
    if (!send_frame((uint8_t*)tool_status, strlen(tool_status))) return false;

    const char* tool_mode = 
			"{\"tool_name\":\"self.balance_car.set_mode\",\"command\":\"切换平衡小车的模式时调用这个工具。'distance'只在避障模式下使用。"
			"可切换的模式包括：'avoid', 'line_patrol','smart_line_patrol','normal'。\",\"params\":[[\"running_mode\",\"string\"],[\"distance\",\"int\"]],\"block\":\"true\",\"return\":\"false\"}";
    if (!send_frame((uint8_t*)tool_mode, strlen(tool_mode))) return false;

				
		const char* tool_led = 
        "{\"tool_name\":\"self.balance_car.set_led_color\","
        "\"command\":\"设置左右RGB灯颜色。lr,lg,lb是左灯RGB, rr,rg,rb是右灯RGB, 范围0-255。\","
        "\"params\":[[\"lr\",\"int\",0,255],[\"lg\",\"int\",0,255],[\"lb\",\"int\",0,255],"
                   "[\"rr\",\"int\",0,255],[\"rg\",\"int\",0,255],[\"rb\",\"int\",0,255]],"
        "\"block\":\"true\",\"return\":\"false\"}";
		if (!send_frame((uint8_t*)tool_led, strlen(tool_led))) return false;
	
		const char* tool_buzzer = 
			"{\"tool_name\":\"self.balance_car.set_buzzer\",\"command\":\"控制平衡小车的蜂鸣器时调用这个工具。'count'是蜂鸣器响的次数\","
			"\"params\":[[\"count\",\"int\"]],\"block\":\"true\",\"return\":\"false\"}";
    if (!send_frame((uint8_t*)tool_buzzer, strlen(tool_buzzer))) return false;
	
				
    const char* tool_finish = "{\"command\":\"mcp_setting\",\"params\":\"true\"}";
    if (!send_frame((uint8_t*)tool_finish, strlen(tool_finish))) return false;
    
    return true;
}

/**
 * @brief 将数据帧通过I2C发送出去
 */
static bool send_frame(const uint8_t* data, uint16_t len) {

	   // 检查数据和长度是否有效
    if (data == NULL || len == 0) {
        return false;
    }
		   // 直接发送 data 指针指向的内存区域，长度为 len
    if (HAL_I2C_Master_Transmit(&hi2c3, WONDERMIND_SLAVE_ADDRESS, (uint8_t*)data, len, I2C_TIMEOUT) == HAL_OK) {
        return true;
    }
		return false;
		
}

/**
 * @brief 从I2C接收数据帧
 */

static bool receive_frame_head(uint16_t* part_ID, uint16_t* part_num, uint16_t* data_len) {
    uint8_t header[8];

    /*step1 收取“帧头+长度”并校验*/
    if(WonderMind_Receive_Data(header, 8) != true){
       return false;
       }
                
    /*step1.1 校验帧头*/
    if (header[0] != 0xAA || header[1] != 0x55) return false;
				HAL_Delay(1);
                
    /*step1.2 解析帧长度并校验合法性*/
    *data_len = ((uint16_t)header[2] << 8) | header[3];

    if (*data_len == 0|| *data_len > 31){
				HAL_Delay(100);
				return false;
		} 

		/*step1.3 解析总分片数和当前分片ID*/
		*part_ID = ((uint16_t)header[5] << 8) | header[4]; 
		*part_num = ((uint16_t)header[7] << 8) | header[6];
		
		return true;
}


static bool receive_frame(uint8_t* buffer, uint16_t* len) {
		uint16_t data_len = 0;
		uint16_t part_ID = 0;
		uint16_t part_num = 0;
		uint16_t buffer_index = 0;
		uint16_t part_ID_temp = 0;
		uint16_t part_num_temp = 0;

		*len =0;

		/*step1 接收帧头并校验*/
		if(receive_frame_head(&part_ID, &part_num, &data_len)){
						*len += data_len;
		}else{
						return false;
		}

		/*step2 收取“帧数据+校验位”*/
		//如果当前收到的数据包不是第一包，说明前面丢包了，此时收到的是残缺的数据，直接退出
		if(part_ID != 1){
				return false;

		}else{  
				//从第一包开始连续接收全部数据
				//data_len(数据长度)+1(校验位)
				//每1包数据的校验位会在接收下一包数据接收时被覆盖
				//最后1包数据的校验位会被后续构造字符串的‘\0’字符覆盖
				for(int i=1; i<=part_num; i++){
						HAL_Delay(10);                
						//异常处理，判断读取是否正常
						if(WonderMind_Receive_Data((buffer + buffer_index), data_len + 1) != true){
										return false;
						}

						//确认校验位合法性
						if (buffer[buffer_index + data_len] == calculate_checksum((buffer + buffer_index), data_len)){
										buffer_index += data_len;

						}else{ //校验不合格，清空已经存储的数据并退出
										memset(buffer,0,(*len)+1);
										return false;
						}

						//接收下一包数据的帧头，获取下一包数据的长度、切片ID等信息，为下一包数据的接收做准备
						if(i < part_num){

										HAL_Delay(100);

										if(receive_frame_head(&part_ID_temp, &part_num_temp, &data_len)){
														*len += data_len;
										}else{
														return false;
										}

										/*验证下一包数据分片ID、总分片数*/
										//下一包数据的分片ID与本包不连续，说明发生丢包，直接退出
										if(( (part_ID + 1) != part_ID_temp ) || (part_num_temp != part_num) ){
														return false;
										}else{
														part_ID += 1;
										}
						}
		}
						return true;                        
		}
    return false;
}

/**
 * @brief I2C底层数据接收函数
 */
static bool WonderMind_Receive_Data(uint8_t* buffer, uint16_t size) {

    
    if (HAL_I2C_Master_Receive(&hi2c3, WONDERMIND_SLAVE_ADDRESS, buffer, size, I2C_TIMEOUT) != HAL_OK) {
			if (HAL_I2C_GetError(&hi2c3) != HAL_I2C_ERROR_NONE) {
            HAL_I2C_DeInit(&hi2c3);
            HAL_I2C_Init(&hi2c3);
        }
        return false; 
		}
    
    return true;
}


/**
 * @brief 计算数据的异或校验和
 */
static uint8_t calculate_checksum(const uint8_t* data, uint16_t len) {
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < len; i++) {
        checksum ^= data[i];
    }
    return checksum;
}