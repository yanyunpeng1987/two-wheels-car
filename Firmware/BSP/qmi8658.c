#include "qmi8658.h"
#include <math.h>

// 自定义错误值定义，代替 NAN 以避免编译警告
#ifndef QMI8658_INVALID_TEMP
#define QMI8658_INVALID_TEMP (-999.0f)
#endif

// 私有函数声明
static int32_t qmi8658_write_reg(qmi8658_handle_t *handle, uint8_t reg, uint8_t data);
static int32_t qmi8658_read_reg(qmi8658_handle_t *handle, uint8_t reg, uint8_t *data);
static int32_t qmi8658_read_regs(qmi8658_handle_t *handle, uint8_t reg, uint8_t *data, uint16_t len);
static int32_t qmi8658_modify_reg(qmi8658_handle_t *handle, uint8_t reg, uint8_t mask, uint8_t data);

static inline void qmi8658_cs_low(qmi8658_handle_t *handle);
static inline void qmi8658_cs_high(qmi8658_handle_t *handle);

/**
 * @brief CS引脚拉低
 */
static inline void qmi8658_cs_low(qmi8658_handle_t *handle)
{
    HAL_GPIO_WritePin(handle->cs_port, handle->cs_pin, GPIO_PIN_RESET);
}

/**
 * @brief CS引脚拉高
 */
static inline void qmi8658_cs_high(qmi8658_handle_t *handle)
{
    HAL_GPIO_WritePin(handle->cs_port, handle->cs_pin, GPIO_PIN_SET);
}

/**
 * @brief 写寄存器
 */
static int32_t qmi8658_write_reg(qmi8658_handle_t *handle, uint8_t reg, uint8_t data)
{
    uint8_t tx_data[2];
    HAL_StatusTypeDef status;
    
    tx_data[0] = reg & 0x7F;  // 写操作，最高位为0
    tx_data[1] = data;
    
    qmi8658_cs_low(handle);
    status = HAL_SPI_Transmit(handle->hspi, tx_data, 2, handle->timeout);
    qmi8658_cs_high(handle);
    
    return (status == HAL_OK) ? IMU_OK : IMU_ERROR;
}

/**
 * @brief 读寄存器
 */
static int32_t qmi8658_read_reg(qmi8658_handle_t *handle, uint8_t reg, uint8_t *data)
{
    uint8_t tx_data = reg | 0x80;  // 读操作，最高位为1
    HAL_StatusTypeDef status;
    
    qmi8658_cs_low(handle);
    status = HAL_SPI_Transmit(handle->hspi, &tx_data, 1, handle->timeout);
    if (status == HAL_OK) {
        status = HAL_SPI_Receive(handle->hspi, data, 1, handle->timeout);
    }
    qmi8658_cs_high(handle);
    
    return (status == HAL_OK) ? IMU_OK : IMU_ERROR;
}

/**
 * @brief 读多个寄存器
 */
static int32_t qmi8658_read_regs(qmi8658_handle_t *handle, uint8_t reg, uint8_t *data, uint16_t len)
{
    uint8_t tx_data = reg | 0x80;  // 读操作，最高位为1
    HAL_StatusTypeDef status;
    
    qmi8658_cs_low(handle);
    status = HAL_SPI_Transmit(handle->hspi, &tx_data, 1, handle->timeout);
    if (status == HAL_OK) {
        status = HAL_SPI_Receive(handle->hspi, data, len, handle->timeout);
    }
    qmi8658_cs_high(handle);
    
    return (status == HAL_OK) ? IMU_OK : IMU_ERROR;
}

/**
 * @brief 修改寄存器（读-修改-写）
 */
static int32_t qmi8658_modify_reg(qmi8658_handle_t *handle, uint8_t reg, uint8_t mask, uint8_t data)
{
    uint8_t reg_data;
    int32_t result;
    
    result = qmi8658_read_reg(handle, reg, &reg_data);
    if (result != IMU_OK) {
        return result;
    }
    
    reg_data = (reg_data & ~mask) | (data & mask);
    return qmi8658_write_reg(handle, reg, reg_data);
}

/**
 * @brief 初始化QMI8658
 */
int32_t qmi8658_init(qmi8658_handle_t *handle, SPI_HandleTypeDef *hspi, 
                     GPIO_TypeDef *cs_port, uint16_t cs_pin)
{
    if (!handle || !hspi || !cs_port) {
        return IMU_ERROR;
    }
    
    handle->hspi = hspi;
    handle->cs_port = cs_port;
    handle->cs_pin = cs_pin;
    handle->timeout = 1000;  // 默认超时时间1秒
    handle->accel_enabled = false;
    handle->gyro_enabled = false;
    handle->accel_scale = 2.0f / 32768.0f;  // 默认±2g
    handle->gyro_scale = 16.0f / 32768.0f;  // 默认±16dps
    
    // 初始化CS引脚为高电平
    qmi8658_cs_high(handle);
    HAL_Delay(10);
    
    // 复位芯片
    int32_t result = qmi8658_reset(handle);
    if (result != IMU_OK) {
        return result;
    }
    
    HAL_Delay(50);
    
    // 检查设备ID
    uint8_t device_id;
    result = qmi8658_get_device_id(handle, &device_id);
    if (result != IMU_OK || device_id != QMI8658_REG_WHOAMI_DEFAULT) {
        return IMU_ERROR;
    }
    
    // 使用STATUSINT.bit7作为CTRL9握手
    result = qmi8658_write_reg(handle, QMI8658_REG_CTRL8, 0x80);
    
    return result;
}

/**
 * @brief 复位QMI8658
 */
int32_t qmi8658_reset(qmi8658_handle_t *handle)
{
    int32_t result;
    uint8_t reg_data;
    uint32_t start_time;
    
    // 发送复位命令
    result = qmi8658_write_reg(handle, QMI8658_REG_RESET, QMI8658_REG_RESET_DEFAULT);
    if (result != IMU_OK) {
        return result;
    }
    
    // 等待复位完成，最多15ms
    start_time = HAL_GetTick();
    while ((HAL_GetTick() - start_time) < 15) {
        result = qmi8658_read_reg(handle, QMI8658_REG_RST_RESULT, &reg_data);
        if (result == IMU_OK && reg_data == QMI8658_REG_RST_RESULT_VAL) {
            // 使能地址自增
            return qmi8658_modify_reg(handle, QMI8658_REG_CTRL1, (1 << 6), (1 << 6));
        }
        HAL_Delay(1);
    }
    
    return IMU_TIMEOUT;
}

/**
 * @brief 获取设备ID
 */
int32_t qmi8658_get_device_id(qmi8658_handle_t *handle, uint8_t *device_id)
{
    return qmi8658_read_reg(handle, QMI8658_REG_WHOAMI, device_id);
}

/**
 * @brief 获取温度
 */
float qmi8658_get_temperature(qmi8658_handle_t *handle)
{
    uint8_t temp_data[2];
    int32_t result;
    
    result = qmi8658_read_regs(handle, QMI8658_REG_TEMPEARTURE_L, temp_data, 2);
    if (result != IMU_OK) {
        return QMI8658_INVALID_TEMP;
    }
    
    return (float)temp_data[1] + ((float)temp_data[0] / 256.0f);
}

/**
 * @brief 配置加速度计
 */
int32_t qmi8658_config_accel(qmi8658_handle_t *handle, qmi8658_accel_range_t range, 
                             qmi8658_accel_odr_t odr, qmi8658_lpf_mode_t lpf_mode, bool enable_lpf)
{
    int32_t result;
    bool was_enabled = handle->accel_enabled;
    
    // 如果正在运行，先禁用
    if (was_enabled) {
        result = qmi8658_disable_accel(handle);
        if (result != IMU_OK) return result;
    }
    
    // 设置量程
    result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL2, 0x70, (range << 4));
    if (result != IMU_OK) return result;
    
    // 更新缩放因子
    switch (range) {
        case QMI8658_ACC_RANGE_2G:
            handle->accel_scale = 2.0f / 32768.0f;
            break;
        case QMI8658_ACC_RANGE_4G:
            handle->accel_scale = 4.0f / 32768.0f;
            break;
        case QMI8658_ACC_RANGE_8G:
            handle->accel_scale = 8.0f / 32768.0f;
            break;
        case QMI8658_ACC_RANGE_16G:
            handle->accel_scale = 16.0f / 32768.0f;
            break;
    }
    
    // 设置输出数据率
    result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL2, 0x0F, odr);
    if (result != IMU_OK) return result;
    
    // 配置低通滤波器
    if (enable_lpf) {
        result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL5, 0x01, 0x01);
        if (result != IMU_OK) return result;
        
        result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL5, 0x06, (lpf_mode << 1));
        if (result != IMU_OK) return result;
    } else {
        result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL5, 0x01, 0x00);
        if (result != IMU_OK) return result;
    }
    
    // 如果之前是使能的，重新使能
    if (was_enabled) {
        result = qmi8658_enable_accel(handle);
    }
    
    return result;
}

/**
 * @brief 配置陀螺仪
 */
int32_t qmi8658_config_gyro(qmi8658_handle_t *handle, qmi8658_gyro_range_t range, 
                            qmi8658_gyro_odr_t odr, qmi8658_lpf_mode_t lpf_mode, bool enable_lpf)
{
    int32_t result;
    bool was_enabled = handle->gyro_enabled;
    
    // 如果正在运行，先禁用
    if (was_enabled) {
        result = qmi8658_disable_gyro(handle);
        if (result != IMU_OK) return result;
    }
    
    // 设置量程
    result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL3, 0x70, (range << 4));
    if (result != IMU_OK) return result;
    
    // 更新缩放因子
    switch (range) {
        case QMI8658_GYR_RANGE_16DPS:
            handle->gyro_scale = 16.0f / 32768.0f;
            break;
        case QMI8658_GYR_RANGE_32DPS:
            handle->gyro_scale = 32.0f / 32768.0f;
            break;
        case QMI8658_GYR_RANGE_64DPS:
            handle->gyro_scale = 64.0f / 32768.0f;
            break;
        case QMI8658_GYR_RANGE_128DPS:
            handle->gyro_scale = 128.0f / 32768.0f;
            break;
        case QMI8658_GYR_RANGE_256DPS:
            handle->gyro_scale = 256.0f / 32768.0f;
            break;
        case QMI8658_GYR_RANGE_512DPS:
            handle->gyro_scale = 512.0f / 32768.0f;
            break;
        case QMI8658_GYR_RANGE_1024DPS:
            handle->gyro_scale = 1024.0f / 32768.0f;
            break;
    }
    
    // 设置输出数据率
    result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL3, 0x0F, odr);
    if (result != IMU_OK) return result;
    
    // 配置低通滤波器
    if (enable_lpf) {
        result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL5, 0x10, 0x10);
        if (result != IMU_OK) return result;
        
        result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL5, 0x60, (lpf_mode << 5));
        if (result != IMU_OK) return result;
    } else {
        result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL5, 0x10, 0x00);
        if (result != IMU_OK) return result;
    }
    
    // 如果之前是使能的，重新使能
    if (was_enabled) {
        result = qmi8658_enable_gyro(handle);
    }
    
    return result;
}

/**
 * @brief 使能加速度计
 */
int32_t qmi8658_enable_accel(qmi8658_handle_t *handle)
{
    int32_t result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL7, 0x01, 0x01);
    if (result == IMU_OK) {
        handle->accel_enabled = true;
    }
    return result;
}

/**
 * @brief 禁用加速度计
 */
int32_t qmi8658_disable_accel(qmi8658_handle_t *handle)
{
    int32_t result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL7, 0x01, 0x00);
    if (result == IMU_OK) {
        handle->accel_enabled = false;
    }
    return result;
}

/**
 * @brief 使能陀螺仪
 */
int32_t qmi8658_enable_gyro(qmi8658_handle_t *handle)
{
    int32_t result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL7, 0x02, 0x02);
    if (result == IMU_OK) {
        handle->gyro_enabled = true;
    }
    return result;
}

/**
 * @brief 禁用陀螺仪
 */
int32_t qmi8658_disable_gyro(qmi8658_handle_t *handle)
{
    int32_t result = qmi8658_modify_reg(handle, QMI8658_REG_CTRL7, 0x02, 0x00);
    if (result == IMU_OK) {
        handle->gyro_enabled = false;
    }
    return result;
}

/**
 * @brief 使能中断引脚
 */
int32_t qmi8658_enable_int(qmi8658_handle_t *handle, qmi8658_int_pin_t pin)
{
    uint8_t bit = (pin == QMI8658_INT_PIN1) ? 3 : 4;
    return qmi8658_modify_reg(handle, QMI8658_REG_CTRL1, (1 << bit), (1 << bit));
}

/**
 * @brief 禁用中断引脚
 */
int32_t qmi8658_disable_int(qmi8658_handle_t *handle, qmi8658_int_pin_t pin)
{
    uint8_t bit = (pin == QMI8658_INT_PIN1) ? 3 : 4;
    return qmi8658_modify_reg(handle, QMI8658_REG_CTRL1, (1 << bit), 0x00);
}

/**
 * @brief 使能数据就绪中断
 */
int32_t qmi8658_enable_data_ready_int(qmi8658_handle_t *handle)
{
    return qmi8658_modify_reg(handle, QMI8658_REG_CTRL7, (1 << 5), 0x00);
}

/**
 * @brief 禁用数据就绪中断
 */
int32_t qmi8658_disable_data_ready_int(qmi8658_handle_t *handle)
{
    return qmi8658_modify_reg(handle, QMI8658_REG_CTRL7, (1 << 5), (1 << 5));
}

/**
 * @brief 读取加速度计原始数据
 */
int32_t qmi8658_read_accel_raw(qmi8658_handle_t *handle, qmi8658_raw_data_t *data)
{
    uint8_t buffer[6];
    int32_t result;
    
    result = qmi8658_read_regs(handle, QMI8658_REG_AX_L, buffer, 6);
    if (result != IMU_OK) {
        return result;
    }
    
    data->x = (int16_t)((buffer[1] << 8) | buffer[0]);
    data->y = (int16_t)((buffer[3] << 8) | buffer[2]);
    data->z = (int16_t)((buffer[5] << 8) | buffer[4]);
    
    return IMU_OK;
}

/**
 * @brief 读取加速度计数据（单位：g）
 */
int32_t qmi8658_read_accel(qmi8658_handle_t *handle, qmi8658_data_t *data)
{
    qmi8658_raw_data_t raw_data;
    int32_t result;
    
    result = qmi8658_read_accel_raw(handle, &raw_data);
    if (result != IMU_OK) {
        return result;
    }
    
    data->x = raw_data.x * handle->accel_scale;
    data->y = raw_data.y * handle->accel_scale;
    data->z = raw_data.z * handle->accel_scale;
    
    return IMU_OK;
}

/**
 * @brief 读取陀螺仪原始数据
 */
int32_t qmi8658_read_gyro_raw(qmi8658_handle_t *handle, qmi8658_raw_data_t *data)
{
    uint8_t buffer[6];
    int32_t result;
    
    result = qmi8658_read_regs(handle, QMI8658_REG_GX_L, buffer, 6);
    if (result != IMU_OK) {
        return result;
    }
    
    data->x = (int16_t)((buffer[1] << 8) | buffer[0]);
    data->y = (int16_t)((buffer[3] << 8) | buffer[2]);
    data->z = (int16_t)((buffer[5] << 8) | buffer[4]);
    
    return IMU_OK;
}

/**
 * @brief 读取陀螺仪数据（单位：dps）
 */
int32_t qmi8658_read_gyro(qmi8658_handle_t *handle, qmi8658_data_t *data)
{
    qmi8658_raw_data_t raw_data;
    int32_t result;
    
    result = qmi8658_read_gyro_raw(handle, &raw_data);
    if (result != IMU_OK) {
        return result;
    }
    
    data->x = raw_data.x * handle->gyro_scale;
    data->y = raw_data.y * handle->gyro_scale;
    data->z = raw_data.z * handle->gyro_scale;
    
    return IMU_OK;
}

/**
 * @brief 电源关闭
 */
int32_t qmi8658_power_down(qmi8658_handle_t *handle)
{
    int32_t result;
    
    result = qmi8658_disable_accel(handle);
    if (result != IMU_OK) return result;
    
    result = qmi8658_disable_gyro(handle);
    if (result != IMU_OK) return result;
    
    return qmi8658_modify_reg(handle, QMI8658_REG_CTRL1, 0x02, 0x02);
}

/**
 * @brief 电源开启
 */
int32_t qmi8658_power_on(qmi8658_handle_t *handle)
{
    return qmi8658_modify_reg(handle, QMI8658_REG_CTRL1, 0x02, 0x00);
}

/**
 * @brief 检查加速度计是否使能
 */
bool qmi8658_is_accel_enabled(qmi8658_handle_t *handle)
{
    uint8_t reg_data;
    if (qmi8658_read_reg(handle, QMI8658_REG_CTRL7, &reg_data) == IMU_OK) {
        handle->accel_enabled = (reg_data & 0x01) != 0;
        return handle->accel_enabled;
    }
    return false;
}

/**
 * @brief 检查陀螺仪是否使能
 */
bool qmi8658_is_gyro_enabled(qmi8658_handle_t *handle)
{
    uint8_t reg_data;
    if (qmi8658_read_reg(handle, QMI8658_REG_CTRL7, &reg_data) == IMU_OK) {
        handle->gyro_enabled = (reg_data & 0x02) != 0;
        return handle->gyro_enabled;
    }
    return false;
}

/**
 * @brief 检查数据是否就绪
 */
bool qmi8658_is_data_ready(qmi8658_handle_t *handle)
{
    uint8_t status;
    if (qmi8658_read_reg(handle, QMI8658_REG_STATUS0, &status) == IMU_OK) {
        return (status & (STATUS0_ACCE_AVAIL | STATUS0_GYRO_AVAIL)) != 0;
    }
    return false;
} 


void qmi8658_calibration_on_demand(qmi8658_handle_t *handle)
{
   // qmi8658_write_reg(handle, QMI8658_REG_RESET, 0xB0);
    HAL_Delay(10);	// delay
    qmi8658_write_reg(handle, QMI8658_REG_CTRL9, QMI8658_CTRL9_CMD_ON_DEMAND_CALI);
//    HAL_Delay(2000);	// delay 1600ms above
    qmi8658_write_reg(handle, QMI8658_REG_CTRL9, QMI8658_CTRL9_CMD_NOP);
    HAL_Delay(100);	// delay
}