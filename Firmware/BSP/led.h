/**
 * @file led.h
 * @author LuYongping
 * @brief
 * @version 0.1
 * @date 2025-07-10
 *
 * @copyright Copyright (c) 2025 Hiwonder
 */
#ifndef INC_LED_H_
#define INC_LED_H_

#include "main.h"

#define LED_ON() 		 HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_RESET)
#define LED_OFF()	     HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, GPIO_PIN_SET)
#define TOGGLE_LED()	 HAL_GPIO_TogglePin(LED_GPIO_Port, LED_Pin)


/**
 * @brief LED闪烁
 *
 * @param time 闪烁频率
 */
void led_flash(uint16_t time);

#endif /* INC_LED_H_ */
