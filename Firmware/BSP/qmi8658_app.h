#ifndef QMI8658_APP_H
#define QMI8658_APP_H

#include "main.h"
#include "qmi8658.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


// 函数声明

/**
 * @brief QMI8658传感器初始化
 * @param hspi SPI句柄指针
 * @param cs_port CS引脚GPIO端口
 * @param cs_pin CS引脚号
 * @return 初始化结果
 */
int32_t qmi8658_app_init(SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin);

/**
 * @brief 读取IMU数据
 * @return 读取结果
 */
int32_t qmi8658_app_read_data(qmi8658_handle_t *handle, imu_data_t *imu_data);

/**
 * @brief 获取当前IMU数据
 * @param data 数据指针
 * @return 是否有新数据
 */
bool qmi8658_app_get_data(imu_data_t *data);

/**
 * @brief 获取设备信息
 */
void qmi8658_app_get_info(void);

/**
 * @brief 校准陀螺仪零偏
 * 在静止状态下调用此函数进行零偏校准
 * @param sample_count 采样次数
 * @return 校准结果
 */
int32_t qmi8658_app_calibrate_gyro(qmi8658_handle_t *handle, uint32_t sample_count);

/**
 * @brief 错误处理函数
 * @param error_code 错误代码
 */
void qmi8658_app_error_handler(int32_t error_code);

#ifdef __cplusplus
}
#endif

#endif // QMI8658_APP_H 