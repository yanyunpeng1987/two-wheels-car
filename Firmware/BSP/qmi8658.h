#ifndef QMI8658_H
#define QMI8658_H

#include "imu.h"
#include "main.h"
#include "QMI8658Constants.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif


// 加速度计量程枚举
typedef enum {
    QMI8658_ACC_RANGE_2G = 0,
    QMI8658_ACC_RANGE_4G,
    QMI8658_ACC_RANGE_8G,
    QMI8658_ACC_RANGE_16G
} qmi8658_accel_range_t;

// 陀螺仪量程枚举
typedef enum {
    QMI8658_GYR_RANGE_16DPS = 0,
    QMI8658_GYR_RANGE_32DPS,
    QMI8658_GYR_RANGE_64DPS,
    QMI8658_GYR_RANGE_128DPS,
    QMI8658_GYR_RANGE_256DPS,
    QMI8658_GYR_RANGE_512DPS,
    QMI8658_GYR_RANGE_1024DPS
} qmi8658_gyro_range_t;

// 加速度计输出数据率枚举
typedef enum {
    QMI8658_ACC_ODR_1000Hz = 3,
    QMI8658_ACC_ODR_500Hz,
    QMI8658_ACC_ODR_250Hz,
    QMI8658_ACC_ODR_125Hz,
    QMI8658_ACC_ODR_62_5Hz,
    QMI8658_ACC_ODR_31_25Hz,
    QMI8658_ACC_ODR_LOWPOWER_128Hz = 12,
    QMI8658_ACC_ODR_LOWPOWER_21Hz,
    QMI8658_ACC_ODR_LOWPOWER_11Hz,
    QMI8658_ACC_ODR_LOWPOWER_3Hz
} qmi8658_accel_odr_t;

// 陀螺仪输出数据率枚举
typedef enum {
    QMI8658_GYR_ODR_7174_4Hz = 0,
    QMI8658_GYR_ODR_3587_2Hz,
    QMI8658_GYR_ODR_1793_6Hz,
    QMI8658_GYR_ODR_896_8Hz,
    QMI8658_GYR_ODR_448_4Hz,
    QMI8658_GYR_ODR_224_2Hz,
    QMI8658_GYR_ODR_112_1Hz,
    QMI8658_GYR_ODR_56_05Hz,
    QMI8658_GYR_ODR_28_025Hz
} qmi8658_gyro_odr_t;

// 低通滤波器模式枚举
typedef enum {
    QMI8658_LPF_MODE_0 = 0,     // 2.66% of ODR
    QMI8658_LPF_MODE_1,         // 3.63% of ODR
    QMI8658_LPF_MODE_2,         // 5.39% of ODR
    QMI8658_LPF_MODE_3          // 13.37% of ODR
} qmi8658_lpf_mode_t;

// 中断引脚枚举
typedef enum {
    QMI8658_INT_PIN1 = 0,
    QMI8658_INT_PIN2
} qmi8658_int_pin_t;

// 传感器数据结构体
typedef struct {
    float x;
    float y;
    float z;
} qmi8658_data_t;

// 原始数据结构体
typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} qmi8658_raw_data_t;

// QMI8658设备句柄结构体
typedef struct {
    SPI_HandleTypeDef *hspi;           // SPI句柄
    GPIO_TypeDef *cs_port;             // CS引脚端口
    uint16_t cs_pin;                   // CS引脚号
    float accel_scale;                 // 加速度计缩放因子
    float gyro_scale;                  // 陀螺仪缩放因子
    bool accel_enabled;                // 加速度计使能状态
    bool gyro_enabled;                 // 陀螺仪使能状态
    float gyro_offset[3];              // 陀螺仪偏移量
    uint32_t timeout;                  // 通信超时时间
} qmi8658_handle_t;

// 温度读取错误值定义
#ifndef QMI8658_INVALID_TEMP
#define QMI8658_INVALID_TEMP (-999.0f)
#endif

// 函数声明
int32_t qmi8658_init(qmi8658_handle_t *handle, SPI_HandleTypeDef *hspi, 
                     GPIO_TypeDef *cs_port, uint16_t cs_pin);
int32_t qmi8658_reset(qmi8658_handle_t *handle);
int32_t qmi8658_get_device_id(qmi8658_handle_t *handle, uint8_t *device_id);
float qmi8658_get_temperature(qmi8658_handle_t *handle);  // 返回 QMI8658_INVALID_TEMP 表示读取失败

// 配置函数
int32_t qmi8658_config_accel(qmi8658_handle_t *handle, qmi8658_accel_range_t range, 
                             qmi8658_accel_odr_t odr, qmi8658_lpf_mode_t lpf_mode, bool enable_lpf);
int32_t qmi8658_config_gyro(qmi8658_handle_t *handle, qmi8658_gyro_range_t range, 
                            qmi8658_gyro_odr_t odr, qmi8658_lpf_mode_t lpf_mode, bool enable_lpf);

// 使能/失能函数
int32_t qmi8658_enable_accel(qmi8658_handle_t *handle);
int32_t qmi8658_disable_accel(qmi8658_handle_t *handle);
int32_t qmi8658_enable_gyro(qmi8658_handle_t *handle);
int32_t qmi8658_disable_gyro(qmi8658_handle_t *handle);

// 中断配置函数
int32_t qmi8658_enable_int(qmi8658_handle_t *handle, qmi8658_int_pin_t pin);
int32_t qmi8658_disable_int(qmi8658_handle_t *handle, qmi8658_int_pin_t pin);
int32_t qmi8658_enable_data_ready_int(qmi8658_handle_t *handle);
int32_t qmi8658_disable_data_ready_int(qmi8658_handle_t *handle);

// 数据读取函数
int32_t qmi8658_read_accel_raw(qmi8658_handle_t *handle, qmi8658_raw_data_t *data);
int32_t qmi8658_read_accel(qmi8658_handle_t *handle, qmi8658_data_t *data);
int32_t qmi8658_read_gyro_raw(qmi8658_handle_t *handle, qmi8658_raw_data_t *data);
int32_t qmi8658_read_gyro(qmi8658_handle_t *handle, qmi8658_data_t *data);

// 电源管理函数
int32_t qmi8658_power_down(qmi8658_handle_t *handle);
int32_t qmi8658_power_on(qmi8658_handle_t *handle);

// 状态检查函数
bool qmi8658_is_accel_enabled(qmi8658_handle_t *handle);
bool qmi8658_is_gyro_enabled(qmi8658_handle_t *handle);
bool qmi8658_is_data_ready(qmi8658_handle_t *handle);

// 校准函数
void qmi8658_calibration_on_demand(qmi8658_handle_t *handle);

#ifdef __cplusplus
}
#endif

#endif // QMI8658_H 