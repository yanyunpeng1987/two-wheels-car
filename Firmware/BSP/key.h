/**
 * @file key.h
 * @author LuYongping
 * @brief Key scan
 * @version 0.1
 * @date 2025-07-09
 *
 * @copyright Copyright (c) 2025 Hiwonder
 *
 */

#ifndef __KEY_H__
#define __KEY_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    KEY_EVENT_NONE = 0,
    KEY_EVENT_SINGLE_CLICK,
    KEY_EVENT_DOUBLE_CLICK,
    KEY_EVENT_LONG_PRESS
} KeyEvent_t;
KeyEvent_t key_event_scan(void);

#define READ_KEY()	 HAL_GPIO_ReadPin(USER_KEY_GPIO_Port, USER_KEY_Pin)
uint8_t click_N_Double(uint8_t time);
uint8_t click(void);
uint8_t Long_Press(void);

#ifdef __cplusplus
}
#endif

#endif /* __KEY_H__ */
