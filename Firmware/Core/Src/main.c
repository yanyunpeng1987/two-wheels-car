/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "dma.h"
#include "i2c.h"
#include "spi.h"
#include "tim.h"
#include "usart.h"
#include "usb_host.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "bsp.h"
#include "lcd_debug.h"  
#include "display_manager.h"
#include "ultrasound.h"
#include "control.h"
#include "ccd.h"
#include "k210.h"
#include "eight_way.h"
#include "bluetooth.h"
#include <stdio.h>
#include "buzzer.h"
#include "bluetooth.h"
#include "lidar.h"
#include "wondermind.h" 
#include "persistent_storage.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

uint8_t way_angle = 1; // 滤波方式
uint8_t blue_front;
uint8_t blue_back;
uint8_t blue_right;
uint8_t blue_left;
uint8_t voice_front;
uint8_t voice_back;
uint8_t voice_left;
uint8_t voice_right;
volatile uint8_t flag_move = 0;

int motor_left;
int motor_right;

int temperature;   // 温度
volatile float voltage; // Latest successful raw battery voltage (V).
float angle;
float gyro;

float ccd_middle_angle = -5.4;  // 平衡角度中值
float k210_middle_angle = -8;  // 平衡角度中值

float angle_balance;
float gyro_balance;
float gyro_turn;

uint8_t running_mode= 0; // 运行模式

uint8_t ld_successful_receive_flag;
uint8_t ccd_center_value; // CCD中心值
uint8_t ccd_threshold;    // CCD阈值
uint8_t rgb_left[3] = {255, 255, 255};
uint8_t rgb_right[3] = {255, 255, 255};
uint8_t raw[2];
uint16_t dis = 0;
uint16_t adv[128] = {0};
uint16_t determin;
float move_x;
float move_z;

uint32_t distance; // 超声波距离
uint8_t pid_send;

uint8_t flag_follow = 0; // 跟随标志
uint8_t flag_avoid = 0;  // 避障标志
uint8_t flag_patrol = 0; // 巡线标志

float acceleration_x;    // x轴加速度
float acceleration_y;    // x轴加速度
float acceleration_z;    // z轴加速度
float velocity_left;     // 左轮速度
float velocity_right;    // 右轮速度

volatile uint8_t delay_flag;  
volatile uint8_t delay_50;   // 50ms延时标记

float target_velocity = 2;
float turn_amplitude= 80;

imu_data_t imu_data;

float pitch;
float roll;
float yaw;
int modify = 0;

PID_Params pidparams;
int32_t encoder_left = 1;  
int32_t encoder_right = 0;

#define ADC_CHANNEL_COUNT 1
volatile uint16_t adc_dma_buffer[ADC_CHANNEL_COUNT];



//MV_RESULT_ST mv_result; 

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_USB_HOST_Process(void);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
/* Keep Flash reads read-only even when started after a debugger session. */
static uint8_t startup_flash_read_only(void)
{
    if (HAL_FLASH_Unlock() != HAL_OK) {
        return 0U;
    }
    CLEAR_BIT(FLASH->CR, FLASH_CR_PG);
    if (HAL_FLASH_Lock() != HAL_OK) {
        return 0U;
    }
    return (FLASH->CR & (FLASH_CR_PG | FLASH_CR_LOCK)) == FLASH_CR_LOCK;
}

/* Run only after all callback dependencies and PID parameters are ready. */
static void startup_enable_control(void)
{
    flag_move = 0U;
    set_pwm(0, 0);
    (void)read_encoder(2);
    (void)read_encoder(3);
    __HAL_GPIO_EXTI_CLEAR_IT(IMU_INT2_Pin);
    HAL_NVIC_ClearPendingIRQ(EXTI2_IRQn);
    /* A latched-high DRDY may not provide a new rising edge after init. */
    if (HAL_GPIO_ReadPin(IMU_INT2_GPIO_Port, IMU_INT2_Pin) == GPIO_PIN_SET) {
        __HAL_GPIO_EXTI_GENERATE_SWIT(IMU_INT2_Pin);
    }
    HAL_NVIC_EnableIRQ(EXTI2_IRQn);
}

int main(void)
{

  /* USER CODE BEGIN 1 */
  HAL_NVIC_DisableIRQ(EXTI2_IRQn);
  if (startup_flash_read_only() == 0U) {
      Error_Handler();
  }
  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */ 

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */

  MX_GPIO_Init();
  MX_DMA_Init();
  MX_USART1_UART_Init();
  MX_SPI3_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_ADC1_Init();
  MX_USART6_UART_Init();
  MX_SPI2_Init();
  MX_TIM5_Init();
  MX_USB_HOST_Init();
	MX_I2C3_Init();
  MX_USART2_UART_Init();
  /* USER CODE BEGIN 2 */
  if (qmi8658_app_init(&hspi3, IMU_CS_GPIO_Port, IMU_CS_Pin) != IMU_OK) {
      Error_Handler();
  }
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_1);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_2);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_3);
  HAL_TIM_PWM_Start(&htim4, TIM_CHANNEL_4);
  HAL_TIM_Encoder_Start(&htim2, TIM_CHANNEL_ALL);
  HAL_TIM_Encoder_Start(&htim3, TIM_CHANNEL_ALL);
    /* ADC1 has no DMA handle; battery and CCD use polling conversions. */
	// 初始化蜂鸣器
	buzzers_init();
	HAL_Delay(500);
  Lidar_Init(&huart2); // 初始化雷达驱动

	// 初始化蓝牙
	bluetooth_init();
	// 初始化显示管理器
	display_manager_init();

	LineFollowIIC_init();
	buzzers[0].beep(&buzzers[0], 2000, 100, 100, 1);
  ultrasound_init(); // 初始化超声波传感器
	ccd_init();//初始化CCD
	WonderMind_Init();//初始化大模型模块
	
	//从Flash加载PID参数
  Read_PID_From_Flash(&pidparams);
	if (*(uint32_t*)&pidparams.magic_number != 0xDEADBEEF)
	{
			pidparams.middle_angle = -3.5f; 
			pidparams.balance_kp = 140;  
			pidparams.balance_kd = 450;
			pidparams.velocity_kp = 30;
			pidparams.velocity_ki = 80;
			pidparams.turn_kp = 5;
			pidparams.turn_kd = 500;
			pidparams.magic_number = 0xDEADBEEF;
			
			// 将默认值写入Flash，以便下次启动时使用
			Write_PID_To_Flash(&pidparams); 
	}
  handle_low_voltage_alarm(); // Seed raw voltage before the control IRQ starts.
  startup_enable_control();
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */

  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
    // 显示管理器处理
    display_manager_process();
		buzzers[0].refresh(&buzzers[0]);
		bluetooth_main_loop_task();
    bluetooth_periodic_update();         // 调用数据报告函数
		ult_mode();
		handle_low_voltage_alarm();       // 低压警报
		// K210 数据获取
		if(running_mode == K210_Line_Patrol_Mode || running_mode == K210_Objects_Follow_Mode) {       
				WonderMV_HAL_GetResult(COLOR_REG, &mv_result);

		}	else if(running_mode == K210_Self_Learning_Mode) {       
				WonderMV_HAL_GetINDEX(OBJECT_REG, &mv_result_recog);  
		}
					
				// CCD传感器数据获取
			if(running_mode == CCD_Line_Patrol_Mode ) {       
				ccd_read_data();
				print_ccd_data();
		}

		WonderMind_Poll_And_Process(); 				// 大模型模块数据发送与处理
		if (flag_move == 0 && modify == 1 ){   //检查PID参数是否变化，有变化就写入flash 
				modify = 0;
				Write_PID_To_Flash(&pidparams);
		}

  
  /* USER CODE END 3 */
	}
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_PWR_VOLTAGESCALING_CONFIG     (PWR_REGULATOR_VOLTAGE_SCALE2);

  /** Initializes the RCC Oscilla     tors according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 8;
  RCC_OscInitStruct.PLL.PLLN = 168;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV4;
  RCC_OscInitStruct.PLL.PLLQ = 7;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size) {
    if (huart->Instance == USART2) {
        Lidar_RxCallback(huart, Size);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        Lidar_ErrorCallback(huart);
    }
}


/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
