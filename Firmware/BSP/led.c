/**
 * @file led.c
 * @author LuYongping
 * @brief LED闪烁
 * @version 0.1
 * @date 2025-07-10
 *
 * @copyright Copyright (c) 2025 Hiwonder
 */

#include "led.h"


/**
 * @brief LED闪烁
 *
 * @param time 闪烁频率
 * @return none
 */
void led_flash(uint16_t time) {
  static int count;
  if(0 == time) {
    LED_ON();
  } else {
//    ++count;
    if(count == time) {
      TOGGLE_LED();
      count = 0;
    }
  }
}


