#include "qmi8658_app.h"
#include "qmi8658.h"
#include "main.h"


// 全局变量
qmi8658_handle_t qmi8658_handle;

bool imu_data_ready_flag = false;

/**
 * @brief QMI8658传感器初始化
 * @param hspi SPI句柄指针
 * @param cs_port CS引脚GPIO端口
 * @param cs_pin CS引脚号
 * @return 初始化结果
 */
int32_t qmi8658_app_init(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin)
{
    int32_t result;
    
    // 初始化QMI8658
    result = qmi8658_init(&qmi8658_handle, hspi, cs_port, cs_pin);
    if (result != IMU_OK) {
        return result;
    }

    qmi8658_calibration_on_demand(&qmi8658_handle);
    
    // 配置加速度计：±2g量程，250Hz输出率，使能低通滤波器
    result = qmi8658_config_accel(&qmi8658_handle, 
                                  QMI8658_ACC_RANGE_4G, 
                                  QMI8658_ACC_ODR_250Hz, 
                                  QMI8658_LPF_MODE_0, 
                                  false); // 关闭低通， 低通有滞后性， 影响响应速度
    if (result != IMU_OK) {
        return result;
    }
    
    // 配置陀螺仪：±2048dps量程，1000Hz输出率，使能低通滤波器
    result = qmi8658_config_gyro(&qmi8658_handle, 
                                 QMI8658_GYR_RANGE_1024DPS, 
                                 QMI8658_GYR_ODR_224_2Hz, 
                                 QMI8658_LPF_MODE_0, 
                                 false);
    if (result != IMU_OK) {
        return result;
    }
    
    // 使能加速度计和陀螺仪
    result = qmi8658_enable_gyro(&qmi8658_handle);
    if (result != IMU_OK) {
        return result;
    }
		
    result = qmi8658_enable_accel(&qmi8658_handle);
    if (result != IMU_OK) {
        return result;
    }
    
    // 使能INT2引脚和数据就绪中断
    result = qmi8658_enable_int(&qmi8658_handle, QMI8658_INT_PIN2);
    if (result != IMU_OK) {
        return result;
    }
    
    result = qmi8658_enable_data_ready_int(&qmi8658_handle);
    if (result != IMU_OK) {
        return result;
    }
    
    return IMU_OK;
}

/**
 * @brief 读取IMU数据
 * @return 读取结果
 */
int32_t qmi8658_app_read_data(qmi8658_handle_t *handle, imu_data_t *imu_data)
{
    qmi8658_data_t accel_data, gyro_data;
    int32_t result;
    
    // 读取加速度计数据
    result = qmi8658_read_accel(handle, &accel_data);
    if (result != IMU_OK) {
        return result;
    }
    
    // 读取陀螺仪数据
    result = qmi8658_read_gyro(handle, &gyro_data);
    if (result != IMU_OK) {
        return result;
    }
    
    // 更新全局数据
    imu_data->accel[0] = accel_data.x;
    imu_data->accel[1] = accel_data.y;
    imu_data->accel[2] = accel_data.z;
    
    imu_data->gyro[0] = gyro_data.x;
    imu_data->gyro[1] = gyro_data.y ;
    imu_data->gyro[2] = gyro_data.z;
    
    imu_data->temperature = qmi8658_get_temperature(handle);
    imu_data->timestamp = HAL_GetTick();
    
    imu_data_ready_flag = true;
    
    return IMU_OK;
}



/**
 * @brief 获取设备信息
 */
void qmi8658_app_get_info(void)
{
    uint8_t device_id;
    
    if (qmi8658_get_device_id(&qmi8658_handle, &device_id) == IMU_OK) {
        #ifdef DEBUG_PRINT
        printf("QMI8658 Device ID: 0x%02X\r\n", device_id);
        printf("Accelerometer enabled: %s\r\n", 
               qmi8658_is_accel_enabled(&qmi8658_handle) ? "Yes" : "No");
        printf("Gyroscope enabled: %s\r\n", 
               qmi8658_is_gyro_enabled(&qmi8658_handle) ? "Yes" : "No");
        #endif
    }
}

/**
 * @brief 校准陀螺仪零偏
 * 在静止状态下调用此函数进行零偏校准
 */
int32_t qmi8658_app_calibrate_gyro(qmi8658_handle_t *handle, uint32_t sample_count)
{
    qmi8658_data_t gyro_data;
    uint32_t valid_samples = 0;
    float gyro_offset[3] = {0};
    
    for (uint32_t i = 0; i < sample_count; i++) {
        if (qmi8658_read_gyro(&qmi8658_handle, &gyro_data) == IMU_OK) {
            gyro_offset[0] += gyro_data.x;
            gyro_offset[1] += gyro_data.y;
            gyro_offset[2] += gyro_data.z;
            valid_samples++;
        }
        HAL_Delay(10);  // 100Hz采样率
    }
    
    if (valid_samples > 0) {
        handle->gyro_offset[0] = gyro_offset[0] / valid_samples;
        handle->gyro_offset[1] = gyro_offset[1] / valid_samples;
        handle->gyro_offset[2] = gyro_offset[2] / valid_samples;
        
        #ifdef DEBUG_PRINT
        printf("Gyro calibration completed.\r\n");
        printf("Offset: X=%.3f, Y=%.3f, Z=%.3f (dps)\r\n", 
               handle->gyro_offset[0], handle->gyro_offset[1], handle->gyro_offset[2]);
        #endif
        
        // 在这里可以保存零偏值到Flash或EEPROM中
        
        return IMU_OK;
    }
    
    return IMU_ERROR;
}

/**
 * @brief 错误处理函数
 */
void qmi8658_app_error_handler(int32_t error_code)
{
    switch (error_code) {
        case IMU_ERROR:
            #ifdef DEBUG_PRINT
            printf("QMI8658 Error: Communication error\r\n");
            #endif
            break;
            
        case IMU_TIMEOUT:
            #ifdef DEBUG_PRINT
            printf("QMI8658 Error: Timeout\r\n");
            #endif
            break;
            
        default:
            #ifdef DEBUG_PRINT
            printf("QMI8658 Error: Unknown error code %ld\r\n", error_code);
            #endif
            break;
    }
    
    // 可以在这里添加错误恢复逻辑
    // 例如：重新初始化传感器
} 