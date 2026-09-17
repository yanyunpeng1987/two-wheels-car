/**
 * @file lcd_display.h
 * @author Wang Ruifan
 * @brief LCD显示器头文件
 * @version 0.1
 * @date 2025-04-08
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef __LCD_DISPLAY_H
#define __LCD_DISPLAY_H

#include "main.h"
#include "display_st7735.h"
#include "display.h"
#include "stdio.h"
#include "string.h"

#define LCD_EDGE 5 // LCD边缘留白
#define LCD_LINE_MARGIN 0 // LCD行间距
#define LCD_MAX_LINE 7 // LCD最大行数

typedef struct display_data display_data; // 定义结构体类型

typedef enum {
	LCD_BLACK = 0x00,	// 黑色
	LCD_CYAN = 0x01,	// 青色
	LCD_YELLOW = 0x02,	// 黄色
	LCD_MAGENTA = 0x03,	// 洋红色
	LCD_BlUE = 0x04,	// 蓝色
	LCD_GREEN = 0x05,	// 绿色
	LCD_RED = 0x06,		// 红色
	LCD_WHITE = 0x07,	// 白色
} lcd_color;

typedef enum {
	LCD_LEFT = 0x00,
	LCD_CENTER = 0x01,
	LCD_RIGHT = 0x02,
} lcd_align;

struct display_data
{
	uint8_t line; // 行号
	lcd_align align; // 对齐方式
	lcd_color color; // 字符串颜色
	lcd_color bgColor; // 字符串背景颜色
	uint8_t font; // 字体
	char str[50];	  // 字符串
};

// 函数声明
void lcds_init(void);
void lcd_display_string(uint8_t line, lcd_align align, lcd_color color, lcd_color bgColor, uint8_t font, const char *str);
void lcd_clear(void);

#endif /* __LCD_DISPLAY_H */ 