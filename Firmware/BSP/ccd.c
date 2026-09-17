/**
 * @file ccd.c
 * @brief TSL1401 线性 CCD 传感器驱动
 * @note  独占使用 ADC1 以进行同步读写。
 */

#include "ccd.h"
#include <stdio.h>
#include <string.h>

// 外部硬件句柄
extern ADC_HandleTypeDef hadc1;
extern UART_HandleTypeDef huart1;

// 全局CCD数据数组, 存储128个像素的原始ADC值 (0-4095)
uint16_t ADV[128];

/**
 * @brief 微秒级延时函数
 * @param us 延时时间 (us)
 */
static void delay_us(volatile uint32_t us)
{
    // 此系数需要根据MCU的主频(HCLK)来精确校准
    // 对于 STM32F401 @ 84MHz, HCLK是84MHz, 一个循环约占3-4个指令周期
    // (84 / 4) ~= 21, 可根据示波器微调以获得更精确的延时
    us *= (SystemCoreClock / 1000000 / 4);
    while (us--);
}

/**
 * @brief 初始化CCD相关的GPIO引脚
 */
void ccd_init(void)
{
    // 确保时钟线(CLK)和数据启动线(SI)初始为低电平
    HAL_GPIO_WritePin(CCD_SI_GPIO_Port, CCD_SI_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(CCD_CLK_GPIO_Port, CCD_CLK_Pin, GPIO_PIN_RESET);
}

/**
 * @brief 采集一帧128像素的CCD图像数据
 * @note  此函数采用CPU阻塞等待的方式进行ADC转换，以保证
 *        ADC采样与CCD像素时钟的严格同步。
 */
void ccd_read_data(void)
{
    // --- 确保ADC配置为CCD通道 ---
    // 虽然CubeMX已默认配置，但这是一个良好的防御性编程习惯，
    // 防止在调用 get_battery_volt() 后ADC配置未被恢复。
    ADC_ChannelConfTypeDef sConfig = {0};
    sConfig.Channel = ADC_CHANNEL_10; // CCD使用的ADC通道
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES; // CCD使用高速采样
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        // 可以在此处理ADC通道配置错误
        return;
    }

    // --- 产生SI和第一个CLK脉冲，开始积分和数据输出准备 ---
    HAL_GPIO_WritePin(CCD_SI_GPIO_Port, CCD_SI_Pin, GPIO_PIN_SET);
    delay_us(1);
    HAL_GPIO_WritePin(CCD_CLK_GPIO_Port, CCD_CLK_Pin, GPIO_PIN_SET);
    delay_us(1);
    HAL_GPIO_WritePin(CCD_SI_GPIO_Port, CCD_SI_Pin, GPIO_PIN_RESET);
    delay_us(1);
    HAL_GPIO_WritePin(CCD_CLK_GPIO_Port, CCD_CLK_Pin, GPIO_PIN_RESET);

    // --- 循环读取128个像素点 ---
    for (int i = 0; i < 128; i++)
    {
        delay_us(1); // 短暂延时，等待上一个像素点的模拟电压在AO引脚上稳定下来
        
        // 产生CLK上升沿，将当前像素点的电压锁定在内部采样保持电容上
        HAL_GPIO_WritePin(CCD_CLK_GPIO_Port, CCD_CLK_Pin, GPIO_PIN_SET);
        delay_us(1);

        // 启动ADC转换，阻塞等待，然后读取结果
        HAL_ADC_Start(&hadc1);
        if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK) // 等待转换完成
        {
					// 读取12位ADC原始值 (范围 0-4095),并且右移4位
            ADV[i] = HAL_ADC_GetValue(&hadc1)  >> 4;
        }
        else
        {
            ADV[i] = 0; // 如果转换超时，将该像素点记为0
        }
        // 产生CLK下降沿，准备输出下一个像素
        HAL_GPIO_WritePin(CCD_CLK_GPIO_Port, CCD_CLK_Pin, GPIO_PIN_RESET);
    }

    // 根据TSL1401时序图，最后还需要一个额外的脉冲来结束整个读周期
    delay_us(1);
    HAL_GPIO_WritePin(CCD_CLK_GPIO_Port, CCD_CLK_Pin, GPIO_PIN_SET);
    delay_us(1);
    HAL_GPIO_WritePin(CCD_CLK_GPIO_Port, CCD_CLK_Pin, GPIO_PIN_RESET);
}

/**
 * @brief 通过串口发送CCD图像数据到上位机进行显示
 */
void print_ccd_data(void)
{
    // 定义通信协议的帧头和帧尾
    uint8_t frame_header[] = {0x02, 0xFD};
    uint8_t frame_footer[] = {0xFD, 0x02};
    // 创建一个缓冲区用于存放8位的图像数据
		char buffer[10];
		
		// 按照协议格式发送数据
		HAL_UART_Transmit(&huart1, frame_header, sizeof(frame_header), HAL_MAX_DELAY);

    for (int i = 0; i < 128; i++)
    {
				sprintf(buffer, "%d ", ADV[i]);
			  HAL_UART_Transmit(&huart1, (uint8_t *)buffer, strlen(buffer), HAL_MAX_DELAY);

    }

    HAL_UART_Transmit(&huart1, frame_footer, sizeof(frame_footer), HAL_MAX_DELAY);
}