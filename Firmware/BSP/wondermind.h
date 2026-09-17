#ifndef __WONDERMIND_H
#define __WONDERMIND_H

#include "stdint.h"
#include <stdbool.h>

typedef enum {
					MOTION_IDLE, 
					MOTION_RUNNING, 
					MOTION_RUNNING_ANGLE, 
					MOTION_BRAKING } MotionState_t;

extern MotionState_t motion_state;
extern int motion_target_distance;
extern float motion_travelled_distance;

//extern volatile  HAL_StatusTypeDef demo ;
extern uint8_t dummy_buffer[64];
extern  uint8_t header[4];
extern int count_1;
extern int length ;
extern int distance_1;
extern float current_speed_cms;
void Motion_Control_Update_Tick(float dt);
/**
 * @brief 初始化WonderMind通信模块。
 *        该函数会扫描并等待从机就绪，然后向从机发送工具注册指令。
 * @return bool true表示初始化成功, false表示失败.
 */
bool WonderMind_Init(void);

/**
 * @brief 主循环中调用的轮询和处理函数。
 *        该函数会定时向WonderMind请求指令，并处理收到的指令。
 */
void WonderMind_Poll_And_Process(void);

/**
 * @brief 在主循环中调用的运动控制状态机处理函数。
 */
void Motion_Control_Process(void);

/**
 * @brief 当动作完成时，调用此函数通知WonderMind。
 */
void WonderMind_Send_Action_Finish(void);

/**
 * @brief 当需要返回状态数据时，调用此函数。
 * @param params_str 格式为 "[[\"key\",\"value\"],...]" 的字符串.
 */
void WonderMind_Send_Status(const char* params_str);

/**
 * @brief 让WonderMind调用摄像头。
 */
void WonderMind_Request_Vision(const char* params_str);


/**
 * @brief 发送MCP注册完成指令。
 */
void WonderMind_MCP_Finish(void);


#endif /* __WONDERMIND_H */