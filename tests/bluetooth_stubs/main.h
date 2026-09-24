#ifndef BT_TEST_MAIN_H
#define BT_TEST_MAIN_H
#include "stm32f4xx_hal.h"
extern uint8_t blue_front, blue_back, blue_left, blue_right, running_mode;
extern volatile uint8_t flag_move;
extern int modify;
extern volatile float voltage;
extern float velocity_left, velocity_right, acceleration_y, gyro_turn;
#endif
