/**
 * @file lcd_porting.c
 * @author Wang Ruifan  
 * @brief LCD移植层实现文件
 * @version 0.1
 * @date 2025-04-08
 */

#include "lcd_porting.h"
#include "display_st7735.h"
#include "spi.h"
#include "main.h"

// 全局LCD对象变量
DisplayObjectTypeDef *lcd = NULL;

// ST7735对象实例
static ST7735ObjectTypeDef st7735_instance;

// SPI写入函数
static void spi_write_func(ST7735ObjectTypeDef *self, uint8_t *data, size_t len)
{
    HAL_SPI_Transmit(&hspi2, data, len, HAL_MAX_DELAY);
}

// 设置DC和CS引脚
static void set_dc_cs_func(ST7735ObjectTypeDef *self, uint32_t new_dc, uint32_t new_cs)
{
    HAL_GPIO_WritePin(LCD_DC_GPIO_Port, LCD_DC_Pin, new_dc ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LCD_CS_GPIO_Port, LCD_CS_Pin, new_cs ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// 设置复位引脚
static void set_res_func(ST7735ObjectTypeDef *self, uint32_t new_res)
{
    HAL_GPIO_WritePin(LCD_RES_GPIO_Port, LCD_RES_Pin, new_res ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

// 设置背光
static void set_backlight_func(ST7735ObjectTypeDef *self, uint32_t brightness)
{
    if (brightness > 0) {
        HAL_GPIO_WritePin(LCD_BK_GPIO_Port, LCD_BK_Pin, GPIO_PIN_SET);
    } else {
        HAL_GPIO_WritePin(LCD_BK_GPIO_Port, LCD_BK_Pin, GPIO_PIN_RESET);
    }
}

// 延时函数
static void sleep_ms_func(ST7735ObjectTypeDef *self, uint32_t ms)
{
    HAL_Delay(ms);
}

// LCD初始化函数
void lcds_init(void)
{
    // 初始化ST7735对象
    st7735_object_init(&st7735_instance);
    
    // 设置移植接口
    st7735_instance.spi_write = spi_write_func;
    st7735_instance.set_dc_cs = set_dc_cs_func;
    st7735_instance.set_res = set_res_func;
    st7735_instance.set_backlight = set_backlight_func;
    st7735_instance.sleep_ms = sleep_ms_func;
    
    // 将lcd指针指向ST7735对象的基类
    lcd = (DisplayObjectTypeDef *)&st7735_instance;
    
    // 执行完整的LCD初始化序列
    if (lcd) {
        // 打开背光
        lcd->set_backlight(lcd, 100);
        HAL_Delay(50);
        
        // 复位LCD
        lcd->reset(lcd);
        HAL_Delay(50);
        
        // 打开显示
        lcd->display_on(lcd);
        HAL_Delay(50);
    }
}