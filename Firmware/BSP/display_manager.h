/**
 * @file display_manager.h
 * @author Your Name
 * @brief 显示管理器头文件
 * @version 0.1
 * @date 2025-04-08
 */

#ifndef __DISPLAY_MANAGER_H
#define __DISPLAY_MANAGER_H

#include <stdint.h>
#include "lcd_display.h"

// 显示管理器函数
void display_manager_init(void);
void display_manager_process(void);  // 主处理函数，需要在main循环中调用
void display_manager_clear(void);
void display_manager_show_error(const char *error_msg);

#endif /* __DISPLAY_MANAGER_H */ 