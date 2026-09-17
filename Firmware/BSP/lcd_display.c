/**
 * @file lcd_display.c
 * @author Wang Ruifan
 * @brief LCD显示器实现文件
 * @version 0.1
 * @date 2025-04-08
 */

#include "lcd_display.h"
#include "lcd_porting.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>

// 颜色映射表
static const uint16_t color_map[] = {
    ST7735_BLACK,   // LCD_BLACK
    ST7735_BLUE,    // LCD_BLUE
    ST7735_RED,     // LCD_RED
    ST7735_GREEN,   // LCD_GREEN
    ST7735_CYAN,    // LCD_CYAN
    ST7735_MAGENTA, // LCD_MAGENTA
    ST7735_YELLOW,  // LCD_YELLOW
    ST7735_WHITE    // LCD_WHITE
};

// 字体表
static const FontDef *font_table[] = {
    &Font_7x10,      // 字体0
    &Font_11x18,     // 字体1
    &Font_Custom     // 字体2
};

// LCD清屏函数
void lcd_clear(void)
{
    if (lcd && lcd->set_fill_image) {
        lcd->set_fill_image(lcd, ST7735_BLACK);
    }
}

// LCD显示字符串函数
void lcd_display_string(uint8_t line, lcd_align align, lcd_color color, lcd_color bgColor, uint8_t font, const char *str)
{
    if (!str || line >= LCD_MAX_LINE) return;

    // 选择字体
    const FontDef *selected_font = &Font_7x10; // 可根据 font 参数切换
    uint16_t font_width  = selected_font->width;
    uint16_t font_height = selected_font->height;

    // 计算行的 Y 坐标
    uint16_t y = LCD_EDGE + line * (font_height + LCD_LINE_MARGIN);

    // 默认左对齐
    uint16_t x = LCD_EDGE;

    // === 根据对齐方式调整X坐标 ===
    if (align == LCD_CENTER) {
        uint16_t str_width = strlen(str) * selected_font->width;
        x = (lcd->width - str_width) / 2;
    } else if (align == LCD_RIGHT) {
        uint16_t str_width = strlen(str) * selected_font->width;
        x = lcd->width - str_width - LCD_EDGE;
    }

    // ---- 绘制字符串 ----
    if (lcd->draw_string) {
        // draw_string 内部每个字符带背景色
        lcd->draw_string(lcd, x, y, str, color_map[color], color_map[bgColor], selected_font);
    }

    // ---- 填充左边空白部分（居中或右对齐时） ----
    if (x > 0 && lcd->fill_rect) {
        lcd->fill_rect(lcd, 0, y, x, font_height, color_map[bgColor]);
    }

    // ---- 填充右边空白部分 ----
    uint16_t right_edge = x + strlen(str) * font_width;
    if (right_edge < lcd->width && lcd->fill_rect) {
        lcd->fill_rect(lcd, right_edge, y, lcd->width - right_edge, font_height, color_map[bgColor]);
    }
}