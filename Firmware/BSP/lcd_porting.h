/**
 * @file lcd_porting.h
 * @author Wang Ruifan
 * @brief LCD移植层头文件
 * @version 0.1
 * @date 2025-04-08
 *
 * @copyright Copyright (c) 2025
 *
 */

#ifndef __LCD_PORTING_H
#define __LCD_PORTING_H

#include "display.h"

// 函数声明
void lcds_init(void);

// 外部变量声明
extern DisplayObjectTypeDef *lcd;

#endif /* __LCD_PORTING_H */ 