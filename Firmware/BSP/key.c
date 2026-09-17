/**
 * @file key.c
 * @brief 统一的按键事件检测 (单击, 双击, 长按)
 * @version 0.3
 * @date 2025-07-09
 *
 * @copyright Copyright (c) 2025 Hiwonder
 */

#include "bsp.h"
#include "key.h"
#include "control.h"

/**
 * @brief 统一的按键事件扫描函数 (状态机实现)
 * @return KeyEvent_t 返回检测到的按键事件。
 * @note 此函数应在主循环或定时器中以约10ms的频率被周期性调用。
 */
KeyEvent_t key_event_scan(void)
{
    // --- 状态机状态定义 ---
    typedef enum {
        STATE_IDLE,                         // 0. 空闲状态
        STATE_DEBOUNCE_PRESS,               // 1. 按下消抖
        STATE_WAIT_FOR_RELEASE,             // 2. 等待释放 (长按/单击判断)
        STATE_DEBOUNCE_RELEASE,             // 3. 释放消抖
        STATE_WAIT_FOR_SECOND_CLICK,        // 4. 等待第二次点击 (双击判断)
        STATE_CONSUME_RELEASE,              // 5. 消耗双击后的释放动作
        STATE_WAIT_FOR_LONG_PRESS_RELEASE   // 6. 消耗长按后的释放动作
    } KeyState_t;

    // --- 状态机内部变量 ---
    static KeyState_t current_state = STATE_IDLE;
    static uint32_t state_timer = 0;

    // --- 时间阈值定义 ---
    const uint32_t DEBOUNCE_TIME = 20;       // 消抖时间 (ms)
    const uint32_t LONG_PRESS_TIME = 1000;   // 长按时间 (ms)
    const uint32_t DOUBLE_CLICK_GAP = 300;   // 双击最大间隔 (ms)

    // --- 函数局部变量 ---
    KeyEvent_t event_to_return = KEY_EVENT_NONE;
    uint8_t key_is_pressed = (READ_KEY() == 0); // 0表示按下

    // --- 状态机核心逻辑 ---
    switch (current_state)
    {
        case STATE_IDLE:
            if (key_is_pressed) {
                current_state = STATE_DEBOUNCE_PRESS;
                state_timer = HAL_GetTick();
            }
            break;

        case STATE_DEBOUNCE_PRESS:
            if ((HAL_GetTick() - state_timer) >= DEBOUNCE_TIME) {
                if (key_is_pressed) { // 确认按下
                    current_state = STATE_WAIT_FOR_RELEASE;
                    state_timer = HAL_GetTick(); // 重置计时器用于长按判断
                } else { // 抖动，返回空闲
                    current_state = STATE_IDLE;
                }
            }
            break;

        case STATE_WAIT_FOR_RELEASE:
            if (!key_is_pressed) { // 按键已释放
                current_state = STATE_DEBOUNCE_RELEASE;
                state_timer = HAL_GetTick();
            } else if ((HAL_GetTick() - state_timer) >= LONG_PRESS_TIME) { // 持续按下超过长按时间
                event_to_return = KEY_EVENT_LONG_PRESS;
                current_state = STATE_WAIT_FOR_LONG_PRESS_RELEASE; // 进入等待长按释放状态
            }
            break;
            
        case STATE_DEBOUNCE_RELEASE:
            if ((HAL_GetTick() - state_timer) >= DEBOUNCE_TIME) {
                if (!key_is_pressed) { // 确认释放
                    current_state = STATE_WAIT_FOR_SECOND_CLICK;
                    state_timer = HAL_GetTick(); // 重置计时器用于双击判断
                } else { // 抖动，返回等待释放
                    current_state = STATE_WAIT_FOR_RELEASE;
                }
            }
            break;

        case STATE_WAIT_FOR_SECOND_CLICK:
            if (key_is_pressed) { // 在间隔内再次按下，判定为双击
                event_to_return = KEY_EVENT_DOUBLE_CLICK;
                current_state = STATE_CONSUME_RELEASE; // 进入消耗释放状态
            } else if ((HAL_GetTick() - state_timer) >= DOUBLE_CLICK_GAP) { // 超时未按下，判定为单击
                event_to_return = KEY_EVENT_SINGLE_CLICK;
                current_state = STATE_IDLE; // 单击事件完成，返回空闲
            }
            break;
						
        case STATE_CONSUME_RELEASE:
            // 等待双击后的按键释放
            if (!key_is_pressed) {
                current_state = STATE_IDLE;
            }
            break;

        case STATE_WAIT_FOR_LONG_PRESS_RELEASE:
            // 等待长按后的按键释放
            if (!key_is_pressed) {
                current_state = STATE_IDLE;
            }
            break;
            
        default: // 任何异常状态都复位到空闲
            current_state = STATE_IDLE;
            break;
    }

    return event_to_return;
}