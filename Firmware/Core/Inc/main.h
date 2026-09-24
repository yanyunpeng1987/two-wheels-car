/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "imu.h"

extern uint8_t way_angle; // 滤波方式
extern uint8_t blue_front;
extern uint8_t blue_back;
extern uint8_t blue_right;
extern uint8_t blue_left;
extern uint8_t voice_front;
extern uint8_t voice_back;
extern uint8_t voice_left;
extern uint8_t voice_right;
extern uint8_t flag_velocity;
extern volatile uint8_t flag_move;

extern int motor_left;
extern int motor_right;

extern int temperature;    // 温度
extern volatile float voltage;        // 电压


extern float middle_angle;   // 平衡角度中值
extern float ccd_middle_angle;   // CCD平衡角度中值
extern float k210_middle_angle;   // K210平衡角度中值

extern float angle_balance;
extern float gyro_balance;
extern float gyro_turn;
extern uint8_t running_mode; // 运行模式  

extern uint8_t ld_successful_receive_flag;
extern uint8_t ccd_center_value; // CCD中心值
extern uint8_t ccd_threshold;    // CCD阈值

extern uint16_t adv[128];
extern uint16_t determin;
extern float move_x;
extern float move_z;


extern uint32_t distance; // 超声波距离
extern uint8_t pid_send;
extern uint8_t flag_follow;
extern uint8_t flag_avoid;

extern float acceleration_x;    // x轴加速度
extern float acceleration_y;    // x轴加速度
extern float acceleration_z;    // z轴加速度
extern float velocity_left;     // 左轮速度
extern float velocity_right;    // 右轮速度

extern volatile uint8_t delay_flag;  
extern volatile uint8_t delay_50;   // 50ms延时标记

extern float balance_kp;    // 平衡PID
extern float balance_kd;
extern float velocity_kp;   // 速度PID
extern float velocity_ki;
extern float turn_kp;        // 转向PID
extern float turn_kd;
extern float target_velocity;
extern float turn_amplitude;
extern imu_data_t imu_data;
extern uint8_t flag_avoid;
extern uint8_t flag_patrol; 

extern float pitch;
extern float roll;
extern float yaw;
extern int modify;
/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define USER_KEY_Pin GPIO_PIN_13
#define USER_KEY_GPIO_Port GPIOC
#define LED_Pin GPIO_PIN_14
#define LED_GPIO_Port GPIOC
#define CCD_AO_Pin GPIO_PIN_0
#define CCD_AO_GPIO_Port GPIOC
#define BUZZER_Pin GPIO_PIN_0
#define BUZZER_GPIO_Port GPIOA
#define ENCODER1_A_Pin GPIO_PIN_1
#define ENCODER1_A_GPIO_Port GPIOA
#define BATTERY_Pin GPIO_PIN_4
#define BATTERY_GPIO_Port GPIOA
#define ENCODER1_B_Pin GPIO_PIN_5
#define ENCODER1_B_GPIO_Port GPIOA
#define ENCODER2_A_Pin GPIO_PIN_6
#define ENCODER2_A_GPIO_Port GPIOA
#define ENCODER2_B_Pin GPIO_PIN_7
#define ENCODER2_B_GPIO_Port GPIOA
#define LCD_RES_Pin GPIO_PIN_2
#define LCD_RES_GPIO_Port GPIOB
#define LCD_DC_Pin GPIO_PIN_10
#define LCD_DC_GPIO_Port GPIOB
#define LCD_CS_Pin GPIO_PIN_12
#define LCD_CS_GPIO_Port GPIOB
#define LCD_SCK_Pin GPIO_PIN_13
#define LCD_SCK_GPIO_Port GPIOB
#define LCD_BK_Pin GPIO_PIN_14
#define LCD_BK_GPIO_Port GPIOB
#define LCD_MOSI_Pin GPIO_PIN_15
#define LCD_MOSI_GPIO_Port GPIOB
#define BLE_TX_Pin GPIO_PIN_6
#define BLE_TX_GPIO_Port GPIOC
#define BLE_RX_Pin GPIO_PIN_7
#define BLE_RX_GPIO_Port GPIOC
#define IMU_CS_Pin GPIO_PIN_8
#define IMU_CS_GPIO_Port GPIOC
#define I2C_SDA_Pin GPIO_PIN_9
#define I2C_SDA_GPIO_Port GPIOC
#define I2C_SCL_Pin GPIO_PIN_8
#define I2C_SCL_GPIO_Port GPIOA
#define CCD_CLK_Pin GPIO_PIN_15
#define CCD_CLK_GPIO_Port GPIOA
#define IMU_SCK_Pin GPIO_PIN_10
#define IMU_SCK_GPIO_Port GPIOC
#define IMU_MISO_Pin GPIO_PIN_11
#define IMU_MISO_GPIO_Port GPIOC
#define IMU_MOSI_Pin GPIO_PIN_12
#define IMU_MOSI_GPIO_Port GPIOC
#define IMU_INT2_Pin GPIO_PIN_2
#define IMU_INT2_GPIO_Port GPIOD
#define IMU_INT2_EXTI_IRQn EXTI2_IRQn
#define CCD_SI_Pin GPIO_PIN_3
#define CCD_SI_GPIO_Port GPIOB
/* Swap each motor output pair for the replacement motor polarity. */
#define M2_F_Pin GPIO_PIN_7
#define M2_F_GPIO_Port GPIOB
#define M2_B_Pin GPIO_PIN_6
#define M2_B_GPIO_Port GPIOB
#define M1_F_Pin GPIO_PIN_9
#define M1_F_GPIO_Port GPIOB
#define M1_B_Pin GPIO_PIN_8
#define M1_B_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
