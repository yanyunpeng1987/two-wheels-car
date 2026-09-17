#ifndef __LCD_DEBUG_H__
#define __LCD_DEBUG_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

// LCD调试函数
void lcd_debug_test_pins(void);
void lcd_debug_test_spi(void);
void lcd_debug_basic_init(void);

#ifdef __cplusplus
}
#endif

#endif /* __LCD_DEBUG_H__ */ 