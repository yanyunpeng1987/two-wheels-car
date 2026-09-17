/**
 * @file display.h
 * @author Lu Yongping (Lucas@hiwonder.com)
 * @brief 显示设备基础接口
 * @version 0.2 (Upgraded)
 * @date 2023-11-15
 *
 * @copyright Copyright (c) 2023
 *
 */

#ifndef __DISPLAY_H__
#define __DISPLAY_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include "fonts.h"

/**
  * @defgroup Display Display
  * @{
  * @defgroup base
  * @{
 */

typedef struct DisplayObject DisplayObjectTypeDef;

/**
  * @brief 所有显示屏设备的基类
  */
struct DisplayObject {
    uint16_t width;            /**< @brief 屏幕像素宽度 */
    uint16_t height;           /**< @brief 屏幕像素高度 */
	
	/* 接口 */
    void (*reset)(DisplayObjectTypeDef *self);
    void (*display_on)(DisplayObjectTypeDef *self);
    void (*display_off)(DisplayObjectTypeDef *self);
    void (*set_backlight)(DisplayObjectTypeDef *self, uint32_t brightness);

    /**
      * @brief 全屏填充颜色接口 (Corrected comment)
      * @param self 屏幕对象指针
      * @param color 颜色
      * @retval None.
    */
    void (*set_fill_image)(DisplayObjectTypeDef *self, uint32_t color);

    /**
      * @brief 填充矩形接口 (NEW)
      * @param self 屏幕对象指针
      * @param x 矩形左上角X坐标
      * @param y 矩形左上角Y坐标
      * @param width 矩形宽度
      * @param height 矩形高度
      * @param color 填充颜色
      * @retval None.
    */
    void (*fill_rect)(DisplayObjectTypeDef *self, int x, int y, int width, int height, uint32_t color);

    void (*draw_pixel)(DisplayObjectTypeDef *self, int x, int y, uint32_t color);
    
    /**
      * @brief 画线接口 (NEW)
      * @param self 屏幕对象指针
      * @param x1 起点X坐标
      * @param y1 起点Y坐标
      * @param x2 终点X坐标
      * @param y2 终点Y坐标
      * @param color 线的颜色
      * @retval None.
    */
    void (*draw_line)(DisplayObjectTypeDef *self, int x1, int y1, int x2, int y2, uint32_t color);

    void (*draw_bitmap)(DisplayObjectTypeDef *self, int x1, int y1, int x2, int y2, uint8_t *data);
    void (*draw_char)(DisplayObjectTypeDef *self, uint16_t x, uint16_t y, char c, uint32_t color, uint32_t bgColor, const FontDef *font);
    uint8_t (*draw_string)(DisplayObjectTypeDef *self, uint16_t x, uint16_t y, const char *str, uint32_t color, uint32_t bgColor, const FontDef *font);
};

/**
  * @}
  * @}
 */

#endif