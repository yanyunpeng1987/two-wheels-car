/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    adc.c
  * @brief   This file provides code for the configuration
  *          of the ADC instances.
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
#include "adc.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

ADC_HandleTypeDef hadc1;

/* ADC1 init function */
void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Configure the global features of the ADC (Clock, Resolution, Data Alignment and number of conversion)
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV8;
  hadc1.Init.Resolution = ADC_RESOLUTION_12B;
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.EOCSelection = ADC_EOC_SEQ_CONV;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure for the selected ADC regular channel its corresponding rank in the sequencer and its sample time.
  */
  sConfig.Channel = ADC_CHANNEL_10;
  sConfig.Rank = 1;
  sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

void HAL_ADC_MspInit(ADC_HandleTypeDef* adcHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspInit 0 */

  /* USER CODE END ADC1_MspInit 0 */
    /* ADC1 clock enable */
    __HAL_RCC_ADC1_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    /**ADC1 GPIO Configuration
    PC0     ------> ADC1_IN10
    PA4     ------> ADC1_IN4
    */
    GPIO_InitStruct.Pin = CCD_AO_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(CCD_AO_GPIO_Port, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = BATTERY_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(BATTERY_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN ADC1_MspInit 1 */

  /* USER CODE END ADC1_MspInit 1 */
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef* adcHandle)
{

  if(adcHandle->Instance==ADC1)
  {
  /* USER CODE BEGIN ADC1_MspDeInit 0 */

  /* USER CODE END ADC1_MspDeInit 0 */
    /* Peripheral clock disable */
    __HAL_RCC_ADC1_CLK_DISABLE();

    /**ADC1 GPIO Configuration
    PC0     ------> ADC1_IN10
    PA4     ------> ADC1_IN4
    */
    HAL_GPIO_DeInit(CCD_AO_GPIO_Port, CCD_AO_Pin);

    HAL_GPIO_DeInit(BATTERY_GPIO_Port, BATTERY_Pin);

  /* USER CODE BEGIN ADC1_MspDeInit 1 */

  /* USER CODE END ADC1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/**
  * @brief  读取指定ADC通道的值
  * @param  Channel: 要读取的ADC通道，例如 ADC_CHANNEL_4, ADC_CHANNEL_10
  * @retval ADC转换后的数字值
  * @note   这个函数是“原子”的，它会自己完成通道配置、启动、转换和读取。
  */
uint16_t Get_ADC_Value(uint32_t Channel)
{
    ADC_ChannelConfTypeDef sConfig = {0};

    // 1. 配置要读取的通道
    sConfig.Channel = Channel;
    sConfig.Rank = 1; 
    // 注意：这里的采样时间可以根据不同通道的需求设置
    // 为了兼容性，可以统一使用一个较长的时间，或者通过参数传入
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES; // 使用为CCD设置的超长采样时间
                                                     // 对于慢速变化的电压信号，长采样时间也是有益的
    
    // 应用通道配置
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
        return 0; 
    }

    // 2. 启动ADC转换
    HAL_ADC_Start(&hadc1);

    // 3. 等待转换完成 (阻塞方式)
    HAL_ADC_PollForConversion(&hadc1, 100); // 设置一个合理的超时

    // 4. 返回读取到的值
    return HAL_ADC_GetValue(&hadc1);
}
/**
  * @brief  读取电池电压，采用动态切换ADC通道的方式
  * @retval 电池电压，单位厘伏 (cV)，例如 1250 代表 12.50V
  * @note   此函数会临时改变ADC1的配置。为避免冲突，它不应在
  *         高实时性的任务（如CCD读取）中被频繁调用。
  */
int get_battery_volt(void)
{
    ADC_ChannelConfTypeDef sConfig = {0};
    uint16_t adc_raw_value = 0;

    // 2. 配置ADC通道为电池电压通道 (ADC_CHANNEL_4)
    sConfig.Channel = ADC_CHANNEL_4;
    sConfig.Rank = 1;
    // 电池电压变化很慢，可以使用较长的采样时间以获得更准确、更稳定的结果
    sConfig.SamplingTime = ADC_SAMPLETIME_480CYCLES;
    if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
    {
      // 配置失败，返回一个错误值
      return -1;
    }

    // 3. 启动一次单次转换
    HAL_ADC_Start(&hadc1);

    // 4. 等待转换完成，并读取结果
    if (HAL_ADC_PollForConversion(&hadc1, 100) == HAL_OK)
    {
        adc_raw_value = HAL_ADC_GetValue(&hadc1);
    }
    
    // 5. 将ADC通道配置切回默认的CCD通道
    //    这确保了下一次 ccd_read_data() 调用时ADC处于正确的状态。
    sConfig.Channel = ADC_CHANNEL_10;
    sConfig.Rank = 1;
    sConfig.SamplingTime = ADC_SAMPLETIME_15CYCLES; // 恢复CCD的高速采样时间
    HAL_ADC_ConfigChannel(&hadc1, &sConfig);

    // 6. 根据原始值计算实际电压
    const int volt = (int)(adc_raw_value * 3.3f * 11.0f * 100.0f / 4096.0f);
    
    return volt;
}

/* USER CODE END 1 */
