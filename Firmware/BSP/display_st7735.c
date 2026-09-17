/**
 * @file display_st7735.c
 * @author Lu Yongping (Lucas@hiwonder.com) - Merged and Refactored
 * @brief ST7735 显示屏驱动 
 * @version 0.3
 * @date 2023-11-15
 *
 * @copyright Copyright (c) 2023
 */

#include "display_st7735.h"
#include <string.h>

//============================================================================
// 前向声明 (Forward Declarations)
//============================================================================
static void st7735_reset(DisplayObjectTypeDef *self);
static void st7735_display_on(DisplayObjectTypeDef *self);
static void st7735_display_off(DisplayObjectTypeDef *self);
static void st7735_set_backlight_base(DisplayObjectTypeDef *self, uint32_t brightness);

// -- 高效绘图函数 --
static void st7735_fill_rect(DisplayObjectTypeDef *self, int x, int y, int width, int height, uint32_t color);
static void st7735_set_fill_image(DisplayObjectTypeDef *self, uint32_t color);
static void st7735_draw_pixel(DisplayObjectTypeDef *self, int x, int y, uint32_t color);
static void st7735_draw_line(DisplayObjectTypeDef *self, int x1, int y1, int x2, int y2, uint32_t color);
static void st7735_draw_bitmap(DisplayObjectTypeDef *self, int x1, int y1, int x2, int y2, uint8_t *data);
static void st7735_draw_char(DisplayObjectTypeDef *self, uint16_t x, uint16_t y, char c, uint32_t color, uint32_t bgColor, const FontDef *font);
static uint8_t st7735_draw_string(DisplayObjectTypeDef *self, uint16_t x, uint16_t y, const char *str, uint32_t color, uint32_t bgColor, const FontDef *font);

//============================================================================
// 底层通信函数 (Low-Level Communication)
//============================================================================
static void st7735_write_command(ST7735ObjectTypeDef *self, uint8_t cmd)
{
    if (self->set_dc_cs) self->set_dc_cs(self, 0, 0); // DC=0 (Command), CS=0 (Active)
    if (self->spi_write) self->spi_write(self, &cmd, 1);
    if (self->set_dc_cs) self->set_dc_cs(self, 1, 1); // CS=1 (Inactive)
}

static void st7735_write_data(ST7735ObjectTypeDef *self, uint8_t *data, size_t len)
{
    if (self->set_dc_cs) self->set_dc_cs(self, 1, 0); // DC=1 (Data), CS=0 (Active)
    if (self->spi_write) self->spi_write(self, data, len);
    if (self->set_dc_cs) self->set_dc_cs(self, 1, 1); // CS=1 (Inactive)
}

//============================================================================
// 窗口设置辅助函数 (Windowing Helpers)
//============================================================================
static void st7735_set_window(ST7735ObjectTypeDef *self, int x0, int y0, int x1, int y1)
{
    // Column Address Set
    st7735_write_command(self, ST7735_CASET);
    uint8_t col_data[] = {(x0 + self->x_offset) >> 8, (x0 + self->x_offset) & 0xFF, (x1 + self->x_offset) >> 8, (x1 + self->x_offset) & 0xFF};
    st7735_write_data(self, col_data, 4);

    // Row Address Set
    st7735_write_command(self, ST7735_RASET);
    uint8_t row_data[] = {(y0 + self->y_offset) >> 8, (y0 + self->y_offset) & 0xFF, (y1 + self->y_offset) >> 8, (y1 + self->y_offset) & 0xFF};
    st7735_write_data(self, row_data, 4);

    // Write to RAM
    st7735_write_command(self, ST7735_RAMWR);
}

//============================================================================
// 核心控制函数 (Core Control Functions)
//============================================================================
static void st7735_reset(DisplayObjectTypeDef *self_base)
{
    ST7735ObjectTypeDef *self = (ST7735ObjectTypeDef *)self_base;

    // 硬件复位
    if (self->set_res) self->set_res(self, 0);
    if (self->sleep_ms) self->sleep_ms(self, 100);
    if (self->set_res) self->set_res(self, 1);
    if (self->sleep_ms) self->sleep_ms(self, 100);
    
    // 软件复位
    st7735_write_command(self, ST7735_SWRESET);
    if (self->sleep_ms) self->sleep_ms(self, 150);
    
    // 退出睡眠
    st7735_write_command(self, ST7735_SLPOUT);
    if (self->sleep_ms) self->sleep_ms(self, 500);
    
    // 设置颜色模式: 16-bit/pixel (RGB565)
    st7735_write_command(self, ST7735_COLMOD);
    uint8_t colmod_data = COLOR_MODE_16BIT;
    st7735_write_data(self, &colmod_data, 1);
    
    // 内存访问控制 (方向设置)
    // 0xA0 (MY | MV | BGR) 适用于常见的 160x80 横屏模块
    st7735_write_command(self, ST7735_MADCTL);
    uint8_t madctl_data = 0xA0;
    st7735_write_data(self, &madctl_data, 1);
    
    // 开启反转模式, 如果颜色不对，请改为 ST7735_INVOFF
    st7735_write_command(self, ST7735_INVON); 
//    st7735_write_command(self, ST7735_INVOFF);
    
    // 正常显示模式
    st7735_write_command(self, ST7735_NORON);
    if (self->sleep_ms) self->sleep_ms(self, 10);
    
    // 清屏为黑色
    st7735_set_fill_image(self_base, ST7735_BLACK);

    // 打开显示
    st7735_display_on(self_base);

    // 设置背光
    st7735_set_backlight_base(self_base, 100);
}

static void st7735_display_on(DisplayObjectTypeDef *self_base)
{
    st7735_write_command((ST7735ObjectTypeDef *)self_base, ST7735_DISPON);
}

static void st7735_display_off(DisplayObjectTypeDef *self_base)
{
    st7735_write_command((ST7735ObjectTypeDef *)self_base, ST7735_DISPOFF);
}

static void st7735_set_backlight_base(DisplayObjectTypeDef *self_base, uint32_t brightness)
{
    ST7735ObjectTypeDef *self = (ST7735ObjectTypeDef *)self_base;
    if (self->set_backlight) {
        self->set_backlight(self, brightness);
    }
}

//============================================================================
// 高效绘图函数实现 (High-Performance Drawing Implementations)
//============================================================================

static void st7735_fill_rect(DisplayObjectTypeDef *self_base, int x, int y, int width, int height, uint32_t color)
{
    ST7735ObjectTypeDef *self = (ST7735ObjectTypeDef *)self_base;
    if (x >= self->base.width || y >= self->base.height) return;
    if ((x + width) > self->base.width) width = self->base.width - x;
    if ((y + height) > self->base.height) height = self->base.height - y;

    st7735_set_window(self, x, y, x + width - 1, y + height - 1);

    size_t total_pixels = (size_t)width * (size_t)height;
    uint8_t color_data[] = {color >> 8, color & 0xFF};

    // 使用缓冲区进行批量写入以提高效率
    #define FILL_BUFFER_SIZE 256 // 128 pixels * 2 bytes/pixel = 256 bytes
    uint8_t buffer[FILL_BUFFER_SIZE];
    for(size_t i = 0; i < FILL_BUFFER_SIZE; i += 2) {
        buffer[i] = color_data[0];
        buffer[i+1] = color_data[1];
    }
    
    if (self->set_dc_cs) self->set_dc_cs(self, 1, 0); // 准备写入数据

    size_t chunks = total_pixels / (FILL_BUFFER_SIZE / 2);
    for(size_t i = 0; i < chunks; ++i) {
        if (self->spi_write) self->spi_write(self, buffer, FILL_BUFFER_SIZE);
    }
    size_t remainder = total_pixels % (FILL_BUFFER_SIZE / 2);
    if (remainder > 0) {
        if (self->spi_write) self->spi_write(self, buffer, remainder * 2);
    }
    
    if (self->set_dc_cs) self->set_dc_cs(self, 1, 1); // 结束写入
}

static void st7735_set_fill_image(DisplayObjectTypeDef *self_base, uint32_t color)
{
    st7735_fill_rect(self_base, 0, 0, self_base->width, self_base->height, color);
}

static void st7735_draw_pixel(DisplayObjectTypeDef *self_base, int x, int y, uint32_t color)
{
    ST7735ObjectTypeDef *self = (ST7735ObjectTypeDef *)self_base;
    if (x < 0 || x >= self->base.width || y < 0 || y >= self->base.height) {
        return;
    }
    st7735_set_window(self, x, y, x, y);
    uint8_t data[] = {color >> 8, (uint8_t)color};
    st7735_write_data(self, data, 2);
}

static void st7735_draw_line(DisplayObjectTypeDef *self_base, int x1, int y1, int x2, int y2, uint32_t color)
{
    int t;
    int xerr = 0, yerr = 0, delta_x, delta_y, distance;
    int incx, incy, uRow, uCol;
    delta_x = x2 - x1;
    delta_y = y2 - y1;
    uRow = x1;
    uCol = y1;
    if (delta_x > 0) incx = 1;
    else if (delta_x == 0) incx = 0;
    else { incx = -1; delta_x = -delta_x; }
    if (delta_y > 0) incy = 1;
    else if (delta_y == 0) incy = 0;
    else { incy = -1; delta_y = -delta_y; }
    if (delta_x > delta_y) distance = delta_x;
    else distance = delta_y;
    for (t = 0; t <= distance; t++) {
        st7735_draw_pixel(self_base, uRow, uCol, color);
        xerr += delta_x;
        yerr += delta_y;
        if (xerr > distance) {
            xerr -= distance;
            uRow += incx;
        }
        if (yerr > distance) {
            yerr -= distance;
            uCol += incy;
        }
    }
}

static void st7735_draw_bitmap(DisplayObjectTypeDef *self_base, int x1, int y1, int x2, int y2, uint8_t *data)
{
    ST7735ObjectTypeDef *self = (ST7735ObjectTypeDef *)self_base;
    st7735_set_window(self, x1, y1, x2, y2);
    st7735_write_data(self, data, 2 * ((x2 - x1) + 1) * ((y2 - y1) + 1));
}

static void st7735_draw_char(DisplayObjectTypeDef *self_base, uint16_t x, uint16_t y, char c, uint32_t color, uint32_t bgColor, const FontDef *font)
{
    ST7735ObjectTypeDef *self = (ST7735ObjectTypeDef *)self_base;
    if (c < 32 || c > 126 || !font || !font->data) return;

    st7735_set_window(self, x, y, x + font->width - 1, y + font->height - 1);
    
    // 为一个字符的像素创建一个缓冲区
    uint8_t char_buffer[font->width * font->height * 2];
    uint32_t buf_ptr = 0;

    for (uint32_t i = 0; i < font->height; i++) {
        // 假设字体数据从字符 ' ' (ASCII 32) 开始
        uint32_t b = font->data[(c - 32) * font->height + i];
        for (uint32_t j = 0; j < font->width; j++) {
            if ((b << j) & 0x8000) { // For 16-bit wide fonts, check from MSB
                char_buffer[buf_ptr++] = color >> 8;
                char_buffer[buf_ptr++] = color & 0xFF;
            } else {
                char_buffer[buf_ptr++] = bgColor >> 8;
                char_buffer[buf_ptr++] = bgColor & 0xFF;
            }
        }
    }
    st7735_write_data(self, char_buffer, sizeof(char_buffer));
}

static uint8_t st7735_draw_string(DisplayObjectTypeDef *self_base, uint16_t x, uint16_t y, const char *str, uint32_t color, uint32_t bgColor, const FontDef *font)
{
    uint16_t start_x = x;
    uint8_t lines = 0;

    while (*str) {
        if (x + font->width > self_base->width || *str == '\n') {
            if (x < self_base->width) {
                st7735_fill_rect(self_base, x, y, self_base->width - x, font->height, bgColor);
            }
            x = start_x;
            y += font->height;
            lines++;
            if (y + font->height > self_base->height) {
                break; 
            }
            if (*str == '\n') {
                str++;
                continue;
            }
        }
        
        st7735_draw_char(self_base, x, y, *str, color, bgColor, font);
        x += font->width;
        str++;
    }

    if (x < self_base->width) {
        st7735_fill_rect(self_base, x, y, self_base->width - x, font->height, bgColor);
    }

    return lines;
}


//============================================================================
// 对象初始化 (Object Initialization)
//============================================================================
void st7735_object_init(ST7735ObjectTypeDef *self)
{
    if (!self) {
        return;
    }
    
    // 设置基本属性 (根据你的屏幕型号修改)
    self->base.width = 160;
    self->base.height = 80;
    self->x_offset = 0;
    self->y_offset = 24;
    
    // --- 绑定函数指针 ---
    self->base.reset = st7735_reset;
    self->base.display_on = st7735_display_on;
    self->base.display_off = st7735_display_off;
    self->base.set_backlight = st7735_set_backlight_base;
    self->base.set_fill_image = st7735_set_fill_image;
    self->base.fill_rect = st7735_fill_rect;
    self->base.draw_pixel = st7735_draw_pixel;
    self->base.draw_line = st7735_draw_line;
    self->base.draw_bitmap = st7735_draw_bitmap;
    self->base.draw_char = st7735_draw_char;
    self->base.draw_string = st7735_draw_string;
    
    // 清空移植接口 (需要在外部设置)
    self->spi_write = NULL;
    self->set_dc_cs = NULL;
    self->set_res = NULL;
    self->set_backlight = NULL;
    self->sleep_ms = NULL;
}