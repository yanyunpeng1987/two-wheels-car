/**
 * @file display_manager.c
 * @author waves
 * @brief 显示管理器实现文件
 * @version 0.1
 * @date 2025-04-08
 */

#include "display_manager.h"
#include "lcd_porting.h"
#include "tim.h"  
#include <stdio.h>
#include <string.h>
#include "main.h"
#include "control.h"
#include "bsp.h"
#include "usbh_hid_gamepad.h"
#include "k210.h"

// 私有变量
static char display_buffer[64];
static uint32_t last_lcd_update = 0;
static uint8_t display_initialized = 0;

// 私有函数声明
static void display_manager_update_main_screen(void);

/**
 * @brief 显示管理器初始化
 */
void display_manager_init(void)
{
    // 初始化LCD
    lcds_init();
    
    // 清屏
    display_manager_clear();
    
    // 标记已初始化
    display_initialized = 1;
    
    
    // 记录初始时间
    last_lcd_update = HAL_GetTick();
}

/**
 * @brief 显示管理器主处理函数
 * @note 需要在main循环中持续调用
 */
void display_manager_process(void)
{
    if (!display_initialized) {
        return;
    }
    
    uint32_t current_tick = HAL_GetTick();
    
    // 主界面每10ms更新一次
    if (current_tick - last_lcd_update >= 0.01 ) {
        display_manager_update_main_screen();
        last_lcd_update = current_tick;
    }
}

/**
 * @brief 清除显示屏
 */
void display_manager_clear(void)
{
    lcd_clear();
}

/**
 * @brief 根据模式ID获取模式名称字符串
 * @param mode 运行模式的枚举值
 * @return const char* 指向模式名称字符串的指针
 */
static const char* get_mode_name(running_mode_t mode)
{
    switch (mode)
    {
        case Normal_Mode:                   return "Normal";
        case Ultrasonic_Avoid_Mode:         return "Ult Avoid";
        case Ultrasonic_Follow_Mode:        return "Ult Follow";
        case Lidar_Avoid_Mode:              return "Lidar Avoid";
        case Lidar_Follow_Mode:             return "Lidar Follow";
        case Lidar_Guard_Mode:              return "Lidar Guard";
        case Lidar_Straight_Mode:           return "Lidar Straight";
        case CCD_Line_Patrol_Mode:          return "CCD Line";
        case Eight_Way_Mode:                return "Eight Way";
        case K210_Line_Patrol_Mode:         return "K210 Line";
        case K210_Objects_Follow_Mode:      return "K210 Follow";
        case K210_Self_Learning_Mode:       return "K210 Learning";
        case WonderMind_Avoid_Mode:         return "Smart Avoid";
        case WonderMind_Line_Patrol_Mode:   return "Smart Line";
        default:                            return "Unknown";
    }
}


/**
 * @brief 更新主屏幕显示
 */
static void display_manager_update_main_screen(void)
{

		int enable = 0;
		if (mode == running_mode){
			enable = 1;
		}else {
			enable = 0;}
	  // 第一行：电机启停
    snprintf(display_buffer, sizeof(display_buffer), "Flag_move: %d", flag_move);
    lcd_display_string(0, LCD_LEFT, LCD_CYAN, LCD_BLACK, 0, display_buffer);
	
	  // 第二行：运行模式
		const char *mode_name = get_mode_name((running_mode_t)mode);
    snprintf(display_buffer, sizeof(display_buffer), "Mode: %s",mode_name);
    lcd_display_string(1, LCD_LEFT, LCD_CYAN, LCD_BLACK, 0, display_buffer);
	
    // 第三行：倾斜角度
    snprintf(display_buffer, sizeof(display_buffer), "Angle: %.1f", angle_balance); 
    lcd_display_string(2, LCD_LEFT, LCD_YELLOW, LCD_BLACK, 0, display_buffer);

    // 第四行：角速度
    snprintf(display_buffer, sizeof(display_buffer), "Gyro: %.1f\xB0", gyro_turn);
    lcd_display_string(3, LCD_LEFT, LCD_YELLOW, LCD_BLACK, 0, display_buffer);

    // 第五行：左轮速度
    snprintf(display_buffer, sizeof(display_buffer), "L_velocity:%.2f ", velocity_left);
    lcd_display_string(4, LCD_LEFT, LCD_WHITE, LCD_BLACK, 0, display_buffer);

    // 第六行：右轮速度
    snprintf(display_buffer, sizeof(display_buffer), "R_velocity:%.2f ", velocity_right);
    lcd_display_string(5, LCD_LEFT, LCD_WHITE, LCD_BLACK, 0, display_buffer);

    // 第七行：电池电量
		snprintf(display_buffer, sizeof(display_buffer), "Voltage: %.1f V", voltage);
    lcd_display_string(6, LCD_LEFT, LCD_GREEN, LCD_BLACK, 0, display_buffer);
}

/**
 * @brief 显示错误信息
 * @param error_msg 错误信息
 */
void display_manager_show_error(const char *error_msg)
{
    display_manager_clear();
    lcd_display_string(1, LCD_CENTER, LCD_RED, LCD_BLACK, 0, "ERROR!");
    if (error_msg) {
        lcd_display_string(2, LCD_CENTER, LCD_WHITE, LCD_BLACK, 0, error_msg);
    }
    

} 