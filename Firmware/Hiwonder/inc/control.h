#ifndef __CONTROL_H
#define __CONTROL_H
#include "main.h"
#include "show.h"
#include "tim.h"
#include "ultrasound.h"
#include "ccd.h"
#include "control_stop_trace.h"
#include "battery_monitor.h"

extern int Sensor_Left,Sensor_Middle,Sensor_Right,Sensor;

#ifndef PI
#define PI 3.14159265							//PI圆周率
#endif

#define Control_Frequency  200.0	//编码器读取频率
#define Diameter_67  6.7				//轮子直径67mm
#define EncoderMultiples   4.0 		//编码器倍频数
#define Encoder_precision  13.0 	//编码器精度 13线
#define Reduction_Ratio  30.0			//减速比30
#define Perimeter  21.03867 			//周长，单位cm

//小车各模式定义
typedef enum {       		  
	Normal_Mode                   		   	   = 0,  // 正常模式
	Ultrasonic_Avoid_Mode          		   		 = 1,  // 超声波避障模式
	Ultrasonic_Follow_Mode         		  	   = 2,  // 超声波跟随模式
	Lidar_Avoid_Mode              		   	   = 3,  // 雷达避障模式
	Lidar_Follow_Mode             		    	 = 4,  // 雷达跟随模式
	Lidar_Guard_Mode              		    	 = 5,  // 雷达警卫模式
	Lidar_Straight_Mode           		    	 = 6,  // 雷达循墙模式
	CCD_Line_Patrol_Mode           		 		   = 7,  // CCD巡线模式
	Eight_Way_Mode                  		 		 = 8,  // 八路巡线模式     
	K210_Line_Patrol_Mode            		 		 = 9,  // K210巡线模式
	K210_Objects_Follow_Mode       		 			 = 10, // K210物体跟随模式
	K210_Self_Learning_Mode         		 		 = 11, // K210自主学习模式
	WonderMind_Avoid_Mode 									 = 12, // 智能避障模式
	WonderMind_Line_Patrol_Mode 				  	 = 13, // 智能巡线模式

} running_mode_t;

    
#define MIN_LEVEL           1 // 最小档位
#define MAX_LEVEL           5 // 最大档位


int balance(float angle,float gyro);
int velocity(int encoder_left,int encoder_right);
int turn(float gyro);
void set_pwm(int motor_left,int motor_right);
void key_scan(void);
void mode_switch_by_wheel(int motor_right);
void limit_pwm(void);
int pwm_limit(int IN,int max,int min);
uint8_t turn_off(float angle);
void get_angle(uint8_t way);
int myabs(int a);
int pick_up(void);
int put_down(void);
void get_velocity_form_encoder(int encoder_left,int encoder_right);
uint8_t Choose(void);
void ult_mode(void);
void lidar_avoid(void);
void lidar_straight(void);
void lidar_follow(void);
void ccd_mode(void);
int ccd_turn(uint8_t ccd,float gyro);//转向控制
int k210_turn(uint8_t x,float gyro);//转向控制
int k210_self_learning_turn(void);//转向控制
int eight_way_turn();//转向控制
int intelligent_transportation_turn();//转向控制
void ele_mode(void);
void select_middle_angle(void);
uint8_t select_running_mode(void);
void gamepad_scan(void);
void handle_low_voltage_alarm(void);
uint8_t control_stop_snapshot(ControlStopSample *sample);
extern volatile ControlStopTrace control_stop_trace;
extern BatteryMonitor battery_monitor;

extern uint8_t rgb_left[3];
extern uint8_t rgb_right[3];
extern uint8_t raw[2];
extern uint16_t dis;
extern int encoder_left;
extern int encoder_right;
extern int mode;
extern float current_yaw_angle_degrees;
extern int velocity_multiple;
extern int avoid_distance;
extern int Detection_WonderMind;
extern int recog;
extern int line_patrol_turn;

#endif
