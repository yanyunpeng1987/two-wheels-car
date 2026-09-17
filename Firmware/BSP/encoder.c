/**
 * @file encoder.c
 * @author LuYongping
 * @brief 编码器计数
 * @version 0.1
 * @date 2025-07-09
 * 
 * @copyright Copyright (c) 2025 Hiwonder
 */

#include "bsp.h"
#include "encoder.h"

/**
 * @brief  读取编码器计数值
 *
 * @param timx 定时器编号
 * @return int 编码器计数值
 */
int read_encoder(uint8_t timx) {
  int encoder_tim;
  switch(timx) {
  case 2:
    encoder_tim = (short)TIM2->CNT;
    TIM2->CNT = 0;
    break;
  case 3:
    encoder_tim = (short)TIM3->CNT;
    TIM3->CNT = 0;
    break;
  default:
    encoder_tim = 0;
    break;
  }
  return encoder_tim;
}
