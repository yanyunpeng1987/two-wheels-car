#include "bsp.h"
#include <math.h>
#include "main.h"
#include "control.h"
#include <stdint.h>
#include "KF.h"
#include "filter.h"
#include "ultrasound.h"
#include "usart.h"
#include "k210.h"
#include "eight_way.h"
#include "usb_host.h"
#include "usbh_hid_gamepad.h"
#include "buzzer.h"
#include "lidar.h"
#include "wondermind.h" 
#include "persistent_storage.h"


MV_RESULT_ST mv_result; 
MV_RESULT_RECOG mv_result_recog; 
LineFollowHandleTypeDef LineFollowLearn;

extern qmi8658_handle_t qmi8658_handle;
int sensor_left,sensor_middle,sensor_right,sensor;
extern uint8_t running_mode;
extern int motor_right;
extern int motor_left;
extern float angle;
extern float gyro;
extern uint8_t raw[2];
extern uint16_t dis;
static int turn_pwm;      // 转向环PWM变量
extern int lidar_turn;
int velocity_multiple = 3;
int turn_multiple = 2;
int mode = 0;
float current_yaw_angle_degrees = 0.0f;
int avoid_distance = 30;
int Detection_WonderMind = 0;
int recog = 0;
int line_patrol_turn = 0;
typedef enum {
    LEARN_STATE_IDLE,       // 状态：空闲，等待指令
    LEARN_STATE_TURNING,    // 状态：正在执行转弯
} LearningState_t;

static LearningState_t g_learning_state = LEARN_STATE_IDLE; // 当前状态
static float g_turn_start_angle = 0.0f; // 记录转弯开始时的角度
static uint32_t event_inhibit_timer = 0;
static uint32_t vision_request_inhibit_until = 0;
static uint32_t recog_reset_time = 0;  

#define CONTROL_PERIOD_MS   4       // 定义您的 EXTI 中断周期，单位毫秒
#define CONTROL_PERIOD_S    0.004f  // 对应的秒，用于计算 (4 / 1000.0f)
// --- 低电压报警相关定义 ---
#define LOW_VOLTAGE_THRESHOLD 9.0f  // 低电压阈值 (9.0V)
#define BEEP_OFF_DURATION     1000  // 蜂鸣器每次响起的间隔时间 (ms)

// 报警状态机
typedef enum {
    ALARM_STATE_IDLE,       // 状态：空闲/电压正常
    ALARM_STATE_BEEPING,    // 状态：正在鸣响
    ALARM_STATE_SILENT      // 状态：鸣响后的静默期
} AlarmState_t;

typedef enum {
    CROSSROAD_STATE_NONE,         // 状态：不在路口，正常循线
    CROSSROAD_STATE_BRAKING,      // 状态：刚到路口，正在刹车
    CROSSROAD_STATE_RECOGNIZING,  // 状态：已停稳，正在发送视觉请求并等待
} CrossroadState_t;

static CrossroadState_t crossroad_state = CROSSROAD_STATE_NONE; // 当前路口状态

static AlarmState_t alarm_state = ALARM_STATE_IDLE;
static uint32_t alarm_timer = 0; // 用于警报状态切换的计时器

float gamepad_speed = 0.0f;  // 用于存储手柄的线性速度指令
float gamepad_turn = 0.0f;   // 用于存储手柄的线性转向指令

/**************************************************************************
Function: Control function
Input   : none
Output  : none
函数功能：所有的控制代码都在这里面
         5ms外部中断由MPU6050的INT引脚触发
         严格保证采样和数据处理的时间同步
入口参数：无
返回  值：无
**************************************************************************/

/**
 * @brief 控制函数
 * @note 所有的控制代码都在这里
 *       4ms外部中断由QMI8658的INT引脚触发
 *       严格保证采样和数据处理的时间同步
 * @param GPIO_Pin 中断触发引脚
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    //电压测量相关变量

    static uint8_t flag_target; // 提供8ms基准, 4ms * 2
    int encoder_left;  // 左轮编码器脉冲计数
    int encoder_right; // 右轮编码器脉冲计数
    int balance_pwm;   // 平衡环PWM变量
    int velocity_pwm;  // 速度环PWM变量
		static uint16_t braking_timer_ms = 0; // 用于刹车的专用计时器

    if(GPIO_Pin == IMU_INT2_Pin) {
			  static uint32_t last_volt_read_time = 0;
				MX_USB_HOST_Process();
			
				LidarApps_Process();

        flag_target=!flag_target;

        get_angle(way_angle);               //更新姿态，5ms一次，更高的采样频率可以改善卡尔曼滤波和互补滤波的效果
			
				const float dt = 1.0f / Control_Frequency;
				float yaw_increment = gyro_turn * dt * (180.0f / PI);
				current_yaw_angle_degrees += yaw_increment;
			
        encoder_left = -read_encoder(3);    //读取左轮编码器的值，前进为正，后退为负， TIM3M2, CC1CC2
        encoder_right = -read_encoder(2);   //读取右轮编码器的值，前进为正，后退为负   TIM3M1, CC3CC4
				//左轮A相接TIM2_CH1, 右轮A相接TIM4_CH2,故这里两个编码器的极性相同
        get_velocity_form_encoder(encoder_left,encoder_right); //编码器读数转速度（cm/s）
        if(1 == delay_flag) {
            ++delay_50;
            if(10 == delay_50) {  		//给主函数提供40ms的精准延时，示波器需要40ms高精度延时
                delay_50 = 0;
                delay_flag = 0;
                ld_successful_receive_flag = 0;
            }
        }

        if(1 == flag_target) {               //8ms控制一次
					if(running_mode != CCD_Line_Patrol_Mode ) { 
						if (HAL_GetTick() - last_volt_read_time > 100) {
							voltage = get_battery_volt() / 100.0f; 
							last_volt_read_time = HAL_GetTick();  // 启动500ms的事件抑制期
					}
					}
				}
					if (flag_move == 1) { // 只有在电机允许运行时，才检测“拿起”
						if (pick_up()) {
								flag_move = 0; // 检测到拿起，立即禁止电机运行
								event_inhibit_timer = HAL_GetTick() + 300;  // 启动300ms的事件抑制期
						}
				} else { // 如果电机是禁止的，就检测“放下”
						if (put_down()) {
								flag_move = 1; // 检测到放下，允许电机运行
								event_inhibit_timer = HAL_GetTick() + 300;
						}
				}

				key_scan(); //扫描按键状态 单击双击可以改变小车运行状态
				mode_switch_by_wheel(encoder_right); //调用滚轮模式切换模式
				select_middle_angle(); //平衡角度切换
				gamepad_scan();				 //手柄模式
				lidar_avoid();    		 //雷达避障模式
				lidar_follow();   		 //雷达跟随模式
				lidar_straight(); 		 //雷达走直线模式
				ccd_mode();       		 //CCD巡线

				balance_pwm=balance(angle_balance,gyro_balance);    //平衡PID控制 Gyro_Balance平衡角速度极性：前倾为正，后倾为负
				velocity_pwm=velocity(encoder_left,encoder_right);  //速度环PID控制，速度反馈是正反馈，就是小车快的时候要慢下来就需要再跑快一点
			
				
				if(running_mode == CCD_Line_Patrol_Mode) {        //CCD循迹下的转向环控制
						turn_pwm=ccd_turn(ccd_center_value,gyro_turn);

				}else if(running_mode == K210_Line_Patrol_Mode || running_mode == K210_Objects_Follow_Mode) {        //k210循迹下的转向环控制
						turn_pwm=k210_turn(mv_result.x,gyro_turn);
				}else if(running_mode == Eight_Way_Mode) {        //八路巡线转向环控制
						turn_pwm=eight_way_turn();
				}else if(running_mode == K210_Self_Learning_Mode) {        //k210自主学习下的转向环控制
						turn_pwm=k210_self_learning_turn();
				}else if(running_mode == Lidar_Avoid_Mode || running_mode == Lidar_Follow_Mode || running_mode == Lidar_Straight_Mode || running_mode == Lidar_Guard_Mode) {        //雷达避障、雷达跟随、雷达警卫、雷达循墙转向环控制
						turn_pwm=lidar_turn;
				}else if(running_mode == WonderMind_Line_Patrol_Mode) {        //智能巡线转向环控制
						turn_pwm=intelligent_transportation_turn();
				}else {
					turn_pwm=turn(gyro_turn);						  //转向环PID控制
				}
//				pwm_turn_1 = turn_pwm;
				   Motion_Control_Update_Tick(CONTROL_PERIOD_S);

        // 2. 处理刹车状态的计时
        if (motion_state == MOTION_BRAKING) {
            braking_timer_ms += CONTROL_PERIOD_MS; // 累加时间
            if (braking_timer_ms >= 150) { // 刹车时间到
                voice_front = 0;
                voice_back = 0;
								voice_left = 0;
								voice_right = 0;
                motion_state = MOTION_IDLE;
                WonderMind_Send_Action_Finish();
                braking_timer_ms = 0; // 重置计时器
            }
        } else {
            braking_timer_ms = 0; // 如果不是刹车状态，就重置计时器
        }
				
				//PWM值正数使小车前进，负数使小车后退   
				motor_left = balance_pwm + velocity_pwm + turn_pwm;       //计算左轮电机最终PWM
				motor_right = balance_pwm + velocity_pwm - turn_pwm;      //计算右轮电机最终PWM

				
				//PWM限幅
				motor_left=pwm_limit(motor_left, 4000, -4000);
				motor_right=pwm_limit(motor_right, 4000, -4000);

				//如果不存在异常
				if(turn_off(angle_balance) == 0 ) {
						set_pwm(motor_left,motor_right);         					//赋值给PWM寄存器
				} else {
					set_pwm(0, 0);
			}
    }
	}

/**
 * @brief 直立PD控制
 *
 * @param Angle 角度
 * @param Gyro 角速度
 * @return int 直立控制PWM
 */
int balance(float angle,float gyro) {
    float angle_bias;
    float gyro_bias;
    int balance_pwm;

    angle_bias = pidparams.middle_angle - angle;    //求出平衡的角度中值和机械相关
    gyro_bias = 0 - gyro;

    //计算平衡控制的电机PWM  PD控制 kp是P系数 kd是D系数
    balance_pwm = (-pidparams.balance_kp * angle_bias) - (gyro_bias * pidparams.balance_kd);
    return balance_pwm;
}

/**
 * @brief 速度控制PWM
 * @note 修改前进后退速度
 * @param encoder_left 左轮编码器读数
 * @param encoder_right 右轮编码器读数
 * @return int 速度控制PWM
 */
int velocity(int encoder_left,int encoder_right) {
    static float velocity,Encoder_Least,Encoder_bias,Movement;
    static float Encoder_Integral;

    //================遥控前进后退部分====================//
		// 语音控制前后
			if(Normal_Mode == running_mode) {
				target_velocity = 5 * velocity_multiple;

				if (motion_state != MOTION_IDLE) {
					if (voice_front) {
							Movement = 5;
					} else if (voice_back) {
							Movement = -5;
					} else {
							Movement = 0; 
					}
				} 
				// 手柄或蓝牙遥控
				else {
						if (gamepad_speed != 0.0f) {
								// 优先使用手柄的线性速度值
								Movement = gamepad_speed;
						} else if (1 == blue_front) {
								// 如果手柄无输入，则检查蓝牙指令 (保持原有功能)
								Movement = target_velocity; 
						} else if (1 == blue_back) {
								Movement = -target_velocity;
						} else {
								Movement = 0; // 无任何手动指令
						}
				}
		}else {
			target_velocity = 5;
		}				
			
    //=============超声波功能==================//
    // 超声波跟随
		if(Ultrasonic_Follow_Mode == running_mode) {

			// --- 1. 定义控制参数 ---
			#define TARGET_DISTANCE 30  // 目标跟随距离 
			#define DISTANCE_KP     0.1 // 比例系数 
			#define DISTANCE_KD     0.1 // 微分系数 

			// --- 2. 声明静态变量来保存上一次的误差 ---
			static int last_error = 0;
			
			// --- 3. 计算当前误差 ---
			// 误差 > 0: 目标太远，需要前进
			// 误差 < 0: 目标太近，需要后退
			int current_error = dis - TARGET_DISTANCE;
			
			// --- 4. 计算误差变化量 (微分项) ---
			int error_derivative = current_error - last_error;

			// --- 5. 计算PD控制器输出 ---
			// 比例项：让小车朝目标距离移动
			// 微分项：防止小车移动过快产生震荡
			float pd_output = (DISTANCE_KP * current_error) + (DISTANCE_KD * error_derivative);

			// --- 6. 将控制器输出转换为速度指令 ---
			Movement = pd_output * target_velocity; 

			// --- 7. 更新上一次的误差 ---
			last_error = current_error;

			// ---  增加一个死区，防止在目标点附近小范围抖动 ---
			if (abs(current_error) < 5) { // 如果距离误差在5cm以内
					Movement = 0;
			}
	}
			
    // 超声波避障、智能避障
    if(Ultrasonic_Avoid_Mode == running_mode || WonderMind_Avoid_Mode == running_mode){
			if (dis > avoid_distance)  {
        Movement = target_velocity ;
    }else {
		    Movement = 0;
		}
	}
		//=============== 雷达功能 ==================//

		 // 雷达避障、雷达走直线
    if(Lidar_Avoid_Mode == running_mode || Lidar_Straight_Mode == running_mode) {
        if (g_sectors.front_dist > 0.3) {
					Movement= target_velocity ;
				}else {
					Movement= 0;
    }
	}
		// 雷达警卫
    if(Lidar_Guard_Mode == running_mode ) {
					Movement= 0;
	}
		
		// 雷达跟随
    if(Lidar_Follow_Mode == running_mode) {
				#define MAX_FOLLOW_DIST    2.0f   // 超过这个距离就认为目标丢失
				#define TARGET_FOLLOW_DIST 0.3f   // 目标跟随距离
				#define DIST_KP  6.0f             // 距离P控制器 
				#define DIST_KD  1.5f             // 距离D控制器 
				static float last_dist_error = 0.0f;
				float out_put = 0.0f; // 存储PD控制器的输出
				if (g_sectors.front_dist < MAX_FOLLOW_DIST) {
					// --- 距离控制 (P控制器) ---
					float dist_error = g_sectors.front_dist - TARGET_FOLLOW_DIST;
					// 误差 > 0: 目标太远，前进
					// 误差 < 0: 目标太近，后退
					out_put = DIST_KP * dist_error - DIST_KD * (dist_error - last_dist_error);
					Movement = target_velocity  * out_put;
					last_dist_error = dist_error;
    }
	}
		
		//=============CCD/八路巡线==================//
    if(running_mode == CCD_Line_Patrol_Mode || running_mode == Eight_Way_Mode ) {
			
       Movement= 1.5*target_velocity ;
    }
		
	 if(running_mode == WonderMind_Line_Patrol_Mode) {
			if (recog == 0){
       Movement= 0.5*target_velocity ;
			}else{
				Movement= 0 ;
			}
    }
    //=============== K210功能 ==================//
		// k210物体跟随
		if ( running_mode == K210_Objects_Follow_Mode ) {
			if (mv_result.w < 80){
				Movement = target_velocity ;
			}else if (mv_result.w  > 100){
				Movement = -target_velocity ;
			}else {
				Movement = 0;
			}	
		 }
		
		// k210视觉巡线
		if ( running_mode == K210_Line_Patrol_Mode) {
				if (mv_result.id == 1){
					Movement = target_velocity ;
				}else {
				Movement = 0;
			}	
			}
				
		// k210自主学习
		if (running_mode == K210_Self_Learning_Mode) {
				if (mv_result_recog.index == 3 ){
				Movement = target_velocity;
				}else{
					Movement = 0  ;
				}
			}
		
    //================速度PI控制器=====================//
    Encoder_Least = 0 - (encoder_left + encoder_right);                    //获取最新速度偏差=目标速度（此处为零）-测量速度（左右编码器之和）
    Encoder_bias *= 0.86f;		                                          //一阶低通滤波器
    Encoder_bias += Encoder_Least * 0.14f;	                              //一阶低通滤波器，减缓速度变化
    Encoder_Integral += Encoder_bias;                                  //积分出位移 积分时间：10ms
    Encoder_Integral = Encoder_Integral + Movement;                       //接收遥控器数据，控制前进后退

    // 积分限幅
    if(Encoder_Integral > 10000) {
        Encoder_Integral = 10000;
    }
    if(Encoder_Integral < -10000) {
        Encoder_Integral = -10000;
    }

    // 速度控制
    velocity = -Encoder_bias * pidparams.velocity_kp - Encoder_Integral * pidparams.velocity_ki;

    //电机关闭后清除积分
    if(1 == turn_off(angle_balance) || 1 == flag_move) {
        Encoder_Integral = 0;
    }
    return velocity;
}

/**
 * @brief 转向控制
 *
 * @param gyro Z轴角速度
 * @return int 转向控制PWM
 */
int turn(float gyro) {
    static float turn_target;
    static float turn_pwm;

    // 修改转向速度，请修改Turn_Amplitude即可
    float Kp = pidparams.turn_kp;
    float Kd;

    //===================遥控左右旋转部分=================//
		if (gamepad_turn != 0.0f) {
				// 优先使用手柄的线性转向值
				turn_target = gamepad_turn;
		}
		else if(1 == blue_left || 1 == voice_left)	{
				// 如果手柄无输入，则检查蓝牙或语音指令
				turn_target = -turn_amplitude * turn_multiple;
		} else if(1 == blue_right || 1 == voice_right) {
				turn_target = turn_amplitude * turn_multiple;
		} else {
				turn_target = 0;
		}
    if(Ultrasonic_Avoid_Mode == running_mode || WonderMind_Avoid_Mode == running_mode ){
			if (dis < 30) {
        turn_target = turn_amplitude *turn_multiple;
    }
	}
    Kd = (gamepad_speed != 0.0f || 1 == blue_front || 1 == blue_back) ? pidparams.turn_kd : 0;    //转向的时候取消陀螺仪的纠正 有点模糊PID的思想

    //===================转向PD控制器=================//
    turn_pwm = (turn_target * Kp) + (gyro * Kd) + move_z; //结合Z轴陀螺仪进行PD控制
    return turn_pwm;								 				    //转向环PWM右转为正，左转为负
}


/**
 * @brief 赋值给PWM寄存器
 *
 * @param motor_left 左轮PWM
 * @param motor_right 右轮PWM
 */
void set_pwm(int motor_left,int motor_right) {
    if(motor_left > 0) {
        TIM4->CCR1 = motor_left;
        TIM4->CCR2 = 0;
    } else if(motor_left < 0) {
        TIM4->CCR1 = 0;
        TIM4->CCR2 = -motor_left;
    } else {
        TIM4->CCR1 = 0;
        TIM4->CCR2 = 0;
    }
    if(motor_right > 0) {
        TIM4->CCR4 = motor_right;
        TIM4->CCR3 = 0;
    } else if(motor_right < 0) {
        TIM4->CCR4 = 0;
        TIM4->CCR3 = -motor_right;
    } else {
        TIM4->CCR4 = 0;
        TIM4->CCR3 = 0;
    }
}

/**
 * @brief 限制PWM赋值
 *
 * @param IN 输入参数
 * @param max 限幅最大值
 * @param min 限幅最小值
 * @return int 限幅后的值
 */
int pwm_limit(int in,int max,int min) {
    int out = in;
    if(out > max) {
        out = max;
    }
    if(out < min) {
        out = min;
    }
    return out;
}


/**
 * @brief 按键修改小车运行状态
 *
 * @return none
 */
void key_scan(void) {
	KeyEvent_t event = key_event_scan();
	 // 根据返回的事件类型执行操作
    switch(event) {
        case KEY_EVENT_SINGLE_CLICK: // 单击
            if (0 == flag_move){
                mode++;
                if( mode > 13){
                    mode = 0;
                }
            }
            break;

        case KEY_EVENT_DOUBLE_CLICK: // 双击
						running_mode = mode;
				    buzzers[0].beep(&buzzers[0], 1000, 50, 50, 2); 
            break;

        case KEY_EVENT_LONG_PRESS: // 长按
            flag_move = !flag_move;
            buzzers[0].beep(&buzzers[0], 1000, 100, 100, 1);
            break;

        case KEY_EVENT_NONE:
        default:
            // 没有事件发生，什么也不做
            break;
    }
}

/**
 * @brief 异常关闭电机
 *
 * @param angle 小车倾角
 * @param voltage 电压
 * @return uint8_t 1：异常  0：正常
 */

uint8_t turn_off(float angle) {
    //倾角大于80度关闭电机, flag_move置0，即长按控制关闭电机
		if((angle < -80) || (angle > 80) || flag_move == 0) {
        return 1;
    } else {
        return 0;
    }
}


/**
 * @brief 获取角度
 *
 * @param way 1：卡尔曼 2：互补滤波
 * @return none
 */
void get_angle(uint8_t way) {

    qmi8658_app_read_data(&qmi8658_handle, &imu_data);
    temperature = imu_data.temperature;  //读取 IMU 内置温度传感器数据，近似表示主板温度。

    const float accel_x = imu_data.accel[0];
    const float accel_y = imu_data.accel[1];
    const float accel_z = imu_data.accel[2];

    const float gyro_x = imu_data.gyro[0] / 180.0f * PI;
    const float gyro_y = imu_data.gyro[1] / 180.0f * PI;
    const float gyro_z = imu_data.gyro[2] / 180.0f * PI;


    switch(way) {
    case 1:
        pitch= KF_X(accel_y, accel_z, -gyro_x) / PI * 180.0f;//卡尔曼滤波
        roll = KF_Y(accel_x, accel_z, gyro_y) / PI * 180.0f;
        break;
    default:
        pitch= KF_X(accel_y,accel_z,-gyro_x )/ PI * 180;//卡尔曼滤波
        roll = KF_Y(accel_x,accel_z,gyro_y) / PI * 180;
        break;
    }  

    gyro_balance = -gyro_x;     //更新平衡角速度
    angle_balance = pitch;      //更新平衡倾角
    acceleration_x = accel_x;   //更新x轴加速度计
		acceleration_y = accel_y;   //更新y轴加速度计
		acceleration_z = accel_z;   //更新Z轴加速度计
    gyro_turn = gyro_z;         //更新转向角速度
}


/**
 * @brief 手柄遥控
 *
 * @return none
 */

void gamepad_scan(void) {
    static uint32_t last_buttons = 0;
    static uint32_t last_buttons_state = 0;

    // --- 定义摇杆参数 ---
    const int JOYSTICK_DEADZONE = 10;       // 摇杆死区，防止摇杆不回中时漂移
    const float JOYSTICK_MAX_VALUE = 127.0f; // 摇杆最大值，用float类型以保证计算精度

    if (info != NULL) {
				if (info->buttons == 8 && info->buttons != last_buttons){
								buzzers[0].beep(&buzzers[0], 1000, 100, 100, 1);
							}
        // --- 垂直摇杆 (ly) -> 控制前进后退速度 ---
        if (myabs(info->ly) > JOYSTICK_DEADZONE) {
            // 将摇杆值 (-127 ~ 127) 映射到目标速度范围
            // info->ly / JOYSTICK_MAX_VALUE 得到一个 -1.0 到 1.0 的比例
            // 再乘以最大速度，就得到了线性速度
            gamepad_speed = (info->ly / JOYSTICK_MAX_VALUE) * (5.0f * velocity_multiple);
        } else {
            gamepad_speed = 0.0f; // 在死区内，速度为0
        }

        // --- 水平摇杆 (rx) -> 控制左右转向速度 ---
        if (myabs(info->rx) > JOYSTICK_DEADZONE) {
            // 原理同上，映射到目标转向范围
            gamepad_turn = (info->rx / JOYSTICK_MAX_VALUE) * (turn_amplitude * turn_multiple);
        } else {
            gamepad_turn = 0.0f; // 在死区内，转向为0
        }
        // =============================================================
				if (info->buttons == 4096){
						pidparams.middle_angle += 0.01;		
						modify = 1;
					}else if (info->buttons == 256){
						pidparams.middle_angle -= 0.01;
						modify = 1;			
					}

					
				if (info->buttons != 0 && info->buttons != last_buttons_state) {

					switch (info->buttons) {
							case 16384: 
									velocity_multiple++;
									break;
							case 1: 
									velocity_multiple--;
									break;
							case 32768: 
									turn_multiple++;
									break;
							case 2: 
									turn_multiple--;
									break;
					}

					if (velocity_multiple > MAX_LEVEL) {
								velocity_multiple = MAX_LEVEL; 
						}
						if (velocity_multiple < MIN_LEVEL) {
								velocity_multiple = MIN_LEVEL; 
						}

						if (turn_multiple > MAX_LEVEL) {
								turn_multiple = MAX_LEVEL; 
						}
						if (turn_multiple < MIN_LEVEL) {
								turn_multiple = MIN_LEVEL;
						}
				}

    } else {
        // 如果手柄未连接，确保指令为0
        gamepad_speed = 0.0f;
        gamepad_turn = 0.0f;
    }
	last_buttons = info->buttons;
	last_buttons_state = info->buttons;
}
		
    

/**
 * @brief 绝对值函数
 *
 * @param a 需要计算绝对值的数
 * @return int 绝对值
 */
int myabs(int a) {
    return a < 0 ? -a : a;
}


/**
 * @brief 检测小车是否被拿起
 *
 * @return int 1:小车被拿起  0：小车未被拿起
 */
int pick_up() {
			//小车的Z轴加速度过大或者轮胎因为正反馈达到转速阈值
			if (voltage >11.0){ //电池电量会影响电机最大转速
					if(myabs(velocity_left)+myabs(velocity_right)>180 || acceleration_z > 1.7 ) {
						return 1;  //检测到小车被拿起
				}
			}else {
					if(myabs(velocity_left)+myabs(velocity_right)>170 || acceleration_z > 1.7) {
						return 1;  //检测到小车被拿起
				}
			}	
    return 0;
}

/**
 * @brief 检测小车是否被放下
 *
 * @param angle 平衡角度
 * @param velocity_left 左轮速度
 * @param velocity_right 右轮速度
 * @return int 1：小车放下  0：小车未放下
 */
int put_down() {
        //小车的轮胎在未上电的时候被人为转动
        if((velocity_left > 10) && (velocity_right > 10) && (velocity_left < 30) && (velocity_right < 30)) {
            return 1;   //检测到小车被放下
        }
    return 0;
}

/**
 * @brief 编码器读数转换为速度（mm/s)
 *
 * @param encoder_left 左轮编码器读数
 * @param encoder_right 右轮编码器读数
 * @return none
 */
void get_velocity_form_encoder(int encoder_left,int encoder_right) {
    float rotation_speed_l;
    float rotation_speed_r;

    //电机转速  转速=编码器读数（5ms每次）*读取频率/倍频数/减速比/编码器精度
    rotation_speed_l = encoder_left * Control_Frequency / EncoderMultiples / Reduction_Ratio / Encoder_precision;
    velocity_left = rotation_speed_l * PI * Diameter_67;		//求出编码器速度=转速*周长 cm/s

    rotation_speed_r = encoder_right * Control_Frequency / EncoderMultiples / Reduction_Ratio / Encoder_precision;
    velocity_right = rotation_speed_r * PI * Diameter_67;		//求出编码器速度=转速*周长 cm/s

}

/**
 * @brief 通过转动右轮切换模式 
 * @param encoder_right 右轮的编码器累计值
 */
void mode_switch_by_wheel(int encoder_right)
{
    // --- 静态变量，用于保存上一次的状态 ---
    static int last_encoder_right = 0;
    static uint32_t last_switch_time = 0;
    
    // --- 可配置的阈值 ---
    const int ENCODER_CHANGE_THRESHOLD = 2;   // 编码器脉冲变化量阈值
    const uint32_t SWITCH_COOLDOWN_MS = 300;  // 切换冷却时间(毫秒)

    // 如果处于事件抑制期，则直接返回
    if (HAL_GetTick() < event_inhibit_timer) {
        last_encoder_right = encoder_right; // 抑制期也要更新，防误触
        return;
    }
		
    // 只有在电机完全停止时才执行模式切换逻辑
    if (flag_move != 0 || myabs(velocity_left) > 5 || myabs(velocity_right) > 5) {
        last_encoder_right = encoder_right; // 运行时更新，防误触
        return;
    }

    // 如果距离上次成功切换的时间太短，则直接返回 (防抖)
    if (HAL_GetTick() - last_switch_time < SWITCH_COOLDOWN_MS) {
        last_encoder_right = encoder_right; // 冷却期更新，防误触
        return;
    }

    // 计算自上次检测以来的编码器变化量
    int encoder_delta = encoder_right - last_encoder_right;

    // 检查是否为正转 (模式增加)
    if (encoder_delta > ENCODER_CHANGE_THRESHOLD) {
        mode++;
        // 处理上溢：如果超过最大模式，则循环到0
        if (mode > 13) { 
            mode = 0;
        }
        
        // 蜂鸣器提示音 
        buzzers[0].beep(&buzzers[0], 1100, 50, 50, 1); 
        
        // 更新状态以防止连续触发
        last_encoder_right = encoder_right;
        last_switch_time = HAL_GetTick();
    }
    // 检查是否为反转 (模式减少)
    else if (encoder_delta < -ENCODER_CHANGE_THRESHOLD) {
        mode--;
        // 处理下溢：如果小于0，则循环到最大模式
        if (mode < 0) {
            mode = 13;
        }

        // 蜂鸣器提示音 
        buzzers[0].beep(&buzzers[0], 900, 50, 50, 1);

        // 更新状态以防止连续触发
        last_encoder_right = encoder_right;
        last_switch_time = HAL_GetTick();
    }
}
/**
 * @brief 超声波模式运行
 *
 * @param none
 * @return none
 */
void ult_mode(void) {
			// 处于超声波相关功能时， 获取超声波测量距离值
	if(K210_Line_Patrol_Mode != running_mode ||  K210_Objects_Follow_Mode != running_mode ||  K210_Self_Learning_Mode != running_mode ) {
				set_ultrasound_color(0, rgb_left, rgb_right); // 设置RGB灯颜色

				if (receive_from_device(&ultrasound, ULTRASOUND_DISTANCE_REG, raw, 2)) {
						dis = ((raw[1] << 8) | raw[0]) / 10;
						if (dis > 200){
							dis = 0; 
						}
					} else {
						dis = 1000; // 读取失败
					}

				if (Detection_WonderMind == 0 || WonderMind_Avoid_Mode == running_mode) {
						if(Ultrasonic_Avoid_Mode == running_mode || Ultrasonic_Follow_Mode == running_mode || WonderMind_Avoid_Mode == running_mode) {	

									if (dis > 0 && dis < 30)  // 小于30cm为障碍物
										{
											rgb_left[0] = 255; rgb_left[1] = 0; rgb_left[2] = 0; // 红色
											rgb_right[0] = 255; rgb_right[1] = 0; rgb_right[2] = 0; // 红色
										}
										else if (dis == 0 || dis == 0xFFFF || dis > 200)
										{
											rgb_left[0] = 0; rgb_left[1] = 0; rgb_left[2] = 0; // 黑色，异常
											rgb_right[0] = 0; rgb_right[1] = 0; rgb_right[2] = 0; // 黑色，异常
										}
										else
										{
											rgb_left[0] = 0; rgb_left[1] = 255; rgb_left[2] = 0; // 绿色，正常
											rgb_right[0] = 0; rgb_right[1] = 255; rgb_right[2] = 0; // 绿色，正常
										}
								}else if (CCD_Line_Patrol_Mode == running_mode || Eight_Way_Mode == running_mode ){
												rgb_left[0] = 0; rgb_left[1] = 0; rgb_left[2] = 0; // 黑色,防止影响巡线
												rgb_right[0] = 0; rgb_right[1] = 0; rgb_right[2] = 0; // 黑色，防止影响巡线
								}else {
												rgb_left[0] = 255; rgb_left[1] = 255; rgb_left[2] = 255; // 白色
												rgb_right[0] = 255; rgb_right[1] = 255; rgb_right[2] = 255; // 白色
								}
					}
	}
}

/**
 * @brief 线性CCD取中值
 *
 * @param none
 * @return none
 */
void  find_ccd_center_value(void) {
		 static uint16_t i,j,Left,Right;
		 static uint16_t value1_max,value1_min,last_ccd_center_value;

		 value1_max=ADV[0];  //动态阈值算法，读取最大和最小值
		 for(i=5; i<123; i++) { //两边各去掉5个点
			 if(value1_max<=ADV[i])
				 value1_max=ADV[i];
		 }
		 value1_min=ADV[0];  //最小值
		 for(i=5; i<123; i++) {
			 if(value1_min>=ADV[i])
				 value1_min=ADV[i];
		 }
		 ccd_threshold=(value1_max+value1_min)/2;	  //计算出本次中线提取的阈值
		 for(i = 5; i<118; i++) { //寻找左边跳变沿
			 if(ADV[i]>ccd_threshold&&ADV[i+1]>ccd_threshold&&ADV[i+2]>ccd_threshold&&ADV[i+3]<ccd_threshold&&ADV[i+4]<ccd_threshold&&ADV[i+5]<ccd_threshold) {
				 Left=i;
				 break;
			 }
		 }
		 for(j = 118; j>5; j--) { //寻找右边跳变沿
			 if(ADV[j]<ccd_threshold&&ADV[j+1]<ccd_threshold&&ADV[j+2]<ccd_threshold&&ADV[j+3]>ccd_threshold&&ADV[j+4]>ccd_threshold&&ADV[j+5]>ccd_threshold) {
				 Right=j;
				 break;
			 }
		 }
		ccd_center_value=(Right+Left)/2;//计算中线位置
		if(myabs(ccd_center_value-last_ccd_center_value)>90)   //计算中线的偏差，如果太大
			ccd_center_value=last_ccd_center_value;    //则取上一次的值
		last_ccd_center_value=ccd_center_value;  //保存上一次的偏差
	}

/**
 * @brief CCD巡线模式运行
 *
 * @param none
 * @return none
 */
void ccd_mode(void) {
   if(running_mode == CCD_Line_Patrol_Mode) {
       find_ccd_center_value();
   }
}

	/**
 * @brief 八路巡线转向控制
 *
 * @param none
 * @return none
 */

int eight_way_turn(void) {
		float Turn = 0;
		int kp = 200;
		static float last_error = 0;
		const int sensor_weights[8] = {-4, -3, -2, -1, 1, 2, 3, 4}; 
   if(running_mode == Eight_Way_Mode) {
			if(LineFollowIIC_State(&LineFollowLearn)) {
				int weighted_sum = 0;   // 加权和
        int active_sensors = 0; // 被触发的传感器数量
        int last_triggered_sensor = -1; // 记录最后一个被触发的传感器的索引
				
				  // 1. 计算加权和与被触发的传感器数量
        for (int i = 0; i < 8; i++) {
            if (LineFollowLearn.data[i] == 1) {
                weighted_sum += sensor_weights[i];
                active_sensors++;
                last_triggered_sensor = i;
            }
        }

        // 2. 根据传感器状态计算误差 (Error)
        float error = 0;
        if (active_sensors > 0) {
            // 如果有传感器被触发，误差 = 加权和 / 触发数量
            // 这可以平滑地计算出黑线中心的偏移量
            error = (float)weighted_sum / active_sensors;
        } else {
            // 如果所有传感器都未触发（丢线），需要特殊处理
            // 如果上次在最左边丢线，就向左猛打；如果在最右边，就向右猛打
            if (last_error > 2) { // 记录上次的误差
                error = 5; // 赋予一个较大的误差值，强制向右转
            } else if (last_error < -2) {
                error = -5; // 强制向左转
            } else {
                // 如果在中间丢线，则直行
                error = 0;
            }
        }
				Turn = (int)(kp * error);

        last_error = error;
		}
	}
	 return Turn;
}
/**
 * @brief K210模式转向控制 巡线
 *
 * @param  x  线条中点
 * @param gyro Z轴陀螺仪
 * @return int 转向控制PWM
 */
int k210_turn(uint8_t x,float gyro) { //转向控制
		float Turn;
		float Bias,kp=3,Kd=0.12;
		Bias= x - 160;
		Turn=Bias*kp+gyro*Kd;
		return Turn;
}

/**
 * @brief K210模式转向控制 巡线
 *
 * @param  x  线条中点
 * @param gyro Z轴陀螺仪
 * @return int 转向控制PWM
 */

int k210_self_learning_turn() {
    // --- 状态机：阶段一 (空闲状态) ---
    if (g_learning_state == LEARN_STATE_IDLE) {
        
        // 检查是否收到新的转弯指令
        if (mv_result_recog.index == 1) { // 收到“右转”指令
            // 1. 记录任务开始时的角度
            g_turn_start_angle = current_yaw_angle_degrees;
            // 2. 记录要执行的PWM指令
            turn_pwm = 500;
            // 3. 切换到“正在转弯”状态
            g_learning_state = LEARN_STATE_TURNING;
            
        } else if (mv_result_recog.index == 2) { // 收到“左转”指令
            // 1. 记录任务开始时的角度
            g_turn_start_angle = current_yaw_angle_degrees;
            // 2. 记录要执行的PWM指令
           turn_pwm = -500;
            // 3. 切换到“正在转弯”状态
            g_learning_state = LEARN_STATE_TURNING;
        }
    }

    // --- 状态机：阶段二 (正在转弯状态) ---
    if (g_learning_state == LEARN_STATE_TURNING) {
        
        // 计算从任务开始到现在，已经转过的角度
        float angle_turned = current_yaw_angle_degrees - g_turn_start_angle;
        
        // 使用绝对值来判断是否转够了90度
        if (fabs(angle_turned) >= 90.0f) {
            g_learning_state = LEARN_STATE_IDLE; // 切换回空闲状态
            turn_pwm = 0;             
        }
    }
    return turn_pwm;
}


/**
 * @brief CCD模式转向控制 巡线
 *
 * @param ccd CCD提取的中线
 * @param gyro Z轴陀螺仪
 * @return int 转向控制PWM
 */

int ccd_turn(uint8_t ccd,float gyro) { //转向控制
   float Turn;
   float Bias,kp=13,Kd=0.12;
   Bias=ccd-64;
   Turn=Bias*kp+gyro*Kd;
	
   return Turn;
}

/**
 * @brief 智能巡线转向控制
 *
 * @param none
 * @return none
 */

static uint32_t g_vision_request_trigger_time = 0; // 记录何时可以触发视觉请求
static uint8_t  vision_request_sent = 0;           // 标志位：0=未发送, 1=已发送
int intelligent_transportation_turn(void) {
    #define KP_GAIN 200
    #define LINE_LOST_THRESHOLD 2.0f
    #define LINE_RECOVERY_ERROR 5.0f
    #define VISION_REQUEST_COOLDOWN_MS 5000 // 视觉请求冷却时间 5秒
    #define VISION_PROCESS_TIMEOUT_MS  4000 // 整个视觉处理的超时时间 4秒
    #define VISION_STOP_DELAY_MS       1000 // 丢线后停止多久再发送请求 1秒 

    int Turn = 0;
    static float last_error = 0;
    const int sensor_weights[8] = {-4, -3, -2, -1, 1, 2, 3, 4}; 

    if (running_mode == WonderMind_Line_Patrol_Mode) {
        if (LineFollowIIC_State(&LineFollowLearn)) {
            
            // 1. 检查整个视觉识别流程是否超时
            if (recog == 1 && HAL_GetTick() >= recog_reset_time) {
                recog = 0; // 超时，强制恢复正常行驶状态
            }

            // 2. 如果当前处于视觉识别状态 (recog == 1)
            if (recog == 1) {
                // 检查是否到达了发送视觉请求的时间点，并且之前还未发送过
                if (vision_request_sent == 0 && HAL_GetTick() >= g_vision_request_trigger_time) {
                    WonderMind_Request_Vision("判断画面中路标的方向，你的返回只能是direction加上left或者right，不要任何解释和说明");
                    buzzers[0].beep(&buzzers[0], 1000, 50, 50, 1);
                    vision_request_sent = 1; // 标记为已发送，防止重复发送
                }
                Turn = line_patrol_turn; 
                return Turn;
            }
                        
            int weighted_sum = 0;
            int active_sensors = 0;
            
            for (int i = 0; i < 8; i++) {
                if (LineFollowLearn.data[i] == 1) {
                    weighted_sum += sensor_weights[i];
                    active_sensors++;
                }
            }

            float error = 0;
            if (active_sensors > 0) {
                // --- 场景A: 正常循线 ---
                error = (float)weighted_sum / active_sensors;
                last_error = error;

            } else {
                // --- 场景B: 丢线 (所有传感器都在白色区域) ---
                
                // 检查是否过了冷却时间，可以启动新一轮的视觉识别
                if (HAL_GetTick() >= vision_request_inhibit_until) {
                    // --- 进入视觉识别准备阶段 ---
                    recog = 1;
                    vision_request_sent = 0; // 重置发送标志
                    line_patrol_turn = 0; // 让小车停止转向，准备停车拍照

                    // 设置定时器
                    // 1. 设置一个稍后的时间点，用于真正发送视觉请求
                    g_vision_request_trigger_time = HAL_GetTick() + VISION_STOP_DELAY_MS; 
                    // 2. 设置整个识别流程的超时保护
                    recog_reset_time = HAL_GetTick() + VISION_PROCESS_TIMEOUT_MS;
                    // 3. 设置下一次可以启动识别的冷却时间
                    vision_request_inhibit_until = HAL_GetTick() + VISION_REQUEST_COOLDOWN_MS;
                }
                
                // 无论是否启动视觉，都执行“丢线恢复”逻辑，尝试找回黑线
                if (last_error > LINE_LOST_THRESHOLD) {
                    error = LINE_RECOVERY_ERROR;
                } else if (last_error < -LINE_LOST_THRESHOLD) {
                    error = -LINE_RECOVERY_ERROR;
                } else {
                    error = 0;
                }
            }
            
            // 统一计算最终的转向PWM值
            Turn = (int)(KP_GAIN * error);
            return Turn;
        }
    }
    
    return 0; 
}

/**
 * @brief 小车机械中值的选择
 *
 * @param none
 * @return none
 */
void select_middle_angle(void) {                 //机械中值选择，避免安装上K210、CCD巡线装备时小车往前冲的现象
		if(running_mode == CCD_Line_Patrol_Mode)
			pidparams.middle_angle = ccd_middle_angle;
		else if(running_mode == K210_Line_Patrol_Mode || running_mode == K210_Objects_Follow_Mode || running_mode == K210_Self_Learning_Mode )
			pidparams.middle_angle = k210_middle_angle;
}

/**
 * @brief 雷达避障模式
 *
 * @param none
 * @return none
 */
void lidar_avoid(void) {
	
    const float DANGER_DIST = 0.5f;
    const float SAFE_DIST = 0.5f;
    static bool is_turning = false;
		if(running_mode == Lidar_Avoid_Mode){
			if (is_turning) {
					if (g_sectors.front_dist > SAFE_DIST) {
							is_turning = false;
					}
			} else {
					if (g_sectors.front_dist < DANGER_DIST) {
							is_turning = true;
					}
			}

			if (is_turning) {
					if (g_sectors.left_dist > g_sectors.right_dist) {
							lidar_turn = -500;
					} else {
							lidar_turn = 500;
					}
			} else {
					lidar_turn = 0;
			}
}

}


/**
 * @brief 雷达跟随、雷达警卫模式
 *
 * @param none
 * @return none
 */
void lidar_follow(void) {
	// --- 跟随模式的PID参数和设定值 ---
		#define ANGLE_KP 30.0f           // 角度P控制器
		#define ANGLE_KD 10.0f           // 角度D控制器 
		#define ANGLE_DEADZONE_DEG    15.0f  // 角度死区，单位：度。±15度以内不进行旋转
		static float last_angle_error = 0.0f;
	  float out_put = 0.0f; // 存储PD控制器的输出
	
		if(running_mode == Lidar_Follow_Mode || running_mode == Lidar_Guard_Mode){
					float angle_error = g_sectors.front_angle;
					if (angle_error > 180.0f) {
							angle_error = angle_error - 360.0f;
					}
					 if (fabs(angle_error) > ANGLE_DEADZONE_DEG) {
					 // 计算转向速度
					// 误差 > 0 (目标在左边, e.g. 10度), 应该向左转 (正角速度)
					// 误差 < 0 (目标在右边, e.g. -10度/350度), 应该向右转 (负角速度)
					lidar_turn = ANGLE_KP * angle_error - ANGLE_KD * (angle_error - last_angle_error);
					last_angle_error = angle_error;
					 }else {
					// 没找到目标或目标太远，停止
					lidar_turn = 0;
			}
		}
	}
					

/**
 * @brief 雷达循墙
 *
 * @param none
 * @return none
 */
void lidar_straight(void) {
	// --- 跟随模式的PID参数和设定值 ---
		#define DIS_KP 5000.0f           // 距离P控制器 
		#define DIS_KD 3000.0f           // 距离D控制器 
		#define TARGET_WALL_DIS 0.4f   // 默认是雷达右侧与墙的距离 (40cm)
		#define DIS_DEADZONE_DEG    0.01f  // 距离死区，单位：米。±0.01 m以内不进行旋转

		static float last_dis_error = 0.0f;
	  float out_put = 0.0f; // 存储PD控制器的输出
	
		if(running_mode == Lidar_Straight_Mode){
				float dis_error = g_sectors.right_front_dist - TARGET_WALL_DIS;
				if (fabs(dis_error) > DIS_DEADZONE_DEG && dis_error < 0.3f) {
				 // 计算转向速度
				// 误差 > 0 应该向右转 
				// 误差 < 0 应该向左转 
					lidar_turn = DIS_KP * dis_error - DIS_KD * (dis_error - last_dis_error);
				last_dis_error = dis_error;
				 }else if(fabs(dis_error) < DIS_DEADZONE_DEG || dis_error > 0.3f || g_sectors.front_dist < 0.3){
				// 没找到目标或目标太远，停止
				lidar_turn = 0;
		}
	}
}

/**
 * @brief 处理低电压报警逻辑 (非阻塞状态机)
 * @note  此函数应在主控制循环中被周期性调用。
 */

void handle_low_voltage_alarm(void)
{
    // --- 1. 检查是否进入低电压状态 ---
    if (voltage > 5.0f && voltage < LOW_VOLTAGE_THRESHOLD) {
        
        // --- 2. 如果电压低，则运行报警状态机 ---
        uint32_t current_tick = HAL_GetTick();

        switch (alarm_state) {
            
            case ALARM_STATE_IDLE:
                // 从正常状态第一次进入低电压状态
                // 立即开始鸣响
								buzzers[0].beep(&buzzers[0], 1000, 50, 50, 2); 
                alarm_state = ALARM_STATE_BEEPING;
                alarm_timer = current_tick; // 记录鸣响开始时间
                break;

            case ALARM_STATE_BEEPING:
                // 正在鸣响状态，检查鸣响时间是否结束
                if (current_tick - alarm_timer >= 50) {
                    // 鸣响结束，进入静默期
                    alarm_state = ALARM_STATE_SILENT;
                    alarm_timer = current_tick; // 记录静默期开始时间
                }
                break;

            case ALARM_STATE_SILENT:
                // 正在静默期，检查静默时间是否结束
                if (current_tick - alarm_timer >= BEEP_OFF_DURATION) {
                    // 静默期结束，返回到IDLE状态，准备下一次鸣响
                    // 在下一次调用此函数时，会因为状态是IDLE而立即再次鸣响
                    alarm_state = ALARM_STATE_IDLE;
                }
                break;
        }

    } else {
        // --- 3. 如果电压恢复正常 ---
        // 立即重置报警状态机到空闲状态
        alarm_state = ALARM_STATE_IDLE;
    }
}





