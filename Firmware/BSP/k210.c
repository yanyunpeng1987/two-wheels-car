#include "k210.h"
#include "main.h"   // 包含main.h以访问全局I2C句柄（hi2c3）
#include "i2c.h"    // 通常hi2c3的声明在这里，确保包含
#include <string.h>
#include <stdio.h>

extern UART_HandleTypeDef huart1;
extern ADC_HandleTypeDef hadc1;

// 声明外部I2C句柄，这个句柄通常由STM32CubeMX在i2c.h中声明
extern I2C_HandleTypeDef hi2c3; 

// 用于接收K210模块数据的内部缓冲区 (共9个字节)
static uint8_t rec_buffer[9];
static uint8_t rec_buffer1[1];

// 私有I2C写入数据（用于发送寄存器地址或少量数据）
static bool wondermv_i2c_write(uint8_t dev_addr, uint8_t* pdata, uint16_t size) {
    // HAL库的I2C地址需要是8位的（7位地址左移1位）
    return (HAL_I2C_Master_Transmit(&hi2c3, dev_addr << 1, pdata, size, WONDERMV_I2C_TIMEOUT) == HAL_OK);
}

// 私有I2C读取数据
static bool wondermv_i2c_read(uint8_t dev_addr, uint8_t* pdata, uint16_t size) {
    // HAL库的I2C地址需要是8位的
    return (HAL_I2C_Master_Receive(&hi2c3, dev_addr << 1, pdata, size, WONDERMV_I2C_TIMEOUT) == HAL_OK);
}


/**
 * @brief 从WonderMV模块的指定寄存器读取数据
 * @param reg 要读取的寄存器地址
 * @param pdata 数据缓冲区指针
 * @param size 要读取的字节数
 * @return true 成功，false 失败
 */
static bool wondermv_i2c_read_reg(uint8_t reg, uint8_t* pdata, uint16_t size) {
    // 首先发送寄存器地址
    if (!wondermv_i2c_write(WONDERMV_ADDR, &reg, 1)) {
        return false;
    }
    // 然后读取数据
    if (!wondermv_i2c_read(WONDERMV_ADDR, pdata, size)) {
        return false;
    }
    return true;
}

/**
 * @brief 从WonderMV模块获取结果数据
 * @param reg 要读取的寄存器地址 
 * @param result 指向MV_RESULT_ST结构体的指针，用于存储读取到的结果
 * @return true 成功，false 失败
 */
bool WonderMV_HAL_GetResult(uint8_t reg, MV_RESULT_ST* result) {
    // 从指定寄存器读取9个字节的数据到rec_buffer
    if (!wondermv_i2c_read_reg(reg, rec_buffer, sizeof(rec_buffer))) {
        // 读取失败，将结果结构体清零并返回false
        memset(result, 0, sizeof(MV_RESULT_ST));
        return false;
    }
    
    // 如果读取成功，解析数据并填充结果结构体
    result->id = rec_buffer[0];
    result->x = BYTE_TO_HW(rec_buffer[2], rec_buffer[1]);
    result->y = BYTE_TO_HW(rec_buffer[4], rec_buffer[3]);
    result->w = BYTE_TO_HW(rec_buffer[6], rec_buffer[5]);
    result->h = BYTE_TO_HW(rec_buffer[8], rec_buffer[7]);
    
    return true;
}

bool WonderMV_HAL_GetINDEX (uint8_t reg , MV_RESULT_RECOG* result){
	    // 从指定寄存器读取1个字节的数据到rec_buffer
    if (!wondermv_i2c_read_reg(reg, rec_buffer1, sizeof(rec_buffer1))) {
        // 读取失败，将结果结构体清零并返回false
        memset(result, 0, sizeof(MV_RESULT_RECOG));
        return false;
    }
    
    // 如果读取成功，解析数据并填充结果结构体
    result->index = rec_buffer1[0];
			
    return true;
}


