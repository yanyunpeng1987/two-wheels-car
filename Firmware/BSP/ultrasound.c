#include "ultrasound.h"
#include "i2c.h"
#include "string.h"

UltrasoundHandleTypeDef ultrasound;
void reset_i2c_bus(I2C_HandleTypeDef* hi2c);

static uint8_t write_data(UltrasoundHandleTypeDef* self, uint8_t* pdata, uint16_t size)
{
    return HAL_I2C_Master_Transmit(&hi2c3, self->dev_addr << 1, pdata, size, 10);
}

static uint8_t read_data(UltrasoundHandleTypeDef* self, uint8_t* pdata, uint16_t size)
{
    return HAL_I2C_Master_Receive(&hi2c3, self->dev_addr << 1, pdata, size, 10);
}

static bool write_to_device(UltrasoundHandleTypeDef* self, uint8_t reg, uint8_t* pdata, uint16_t size)
{
    uint8_t trans_data[size + 1];
    trans_data[0] = reg;
    for (uint16_t i = 0; i < size; i++)
        trans_data[1 + i] = pdata[i];

    return (write_data(self, trans_data, sizeof(trans_data)) == HAL_OK);
}


/**
 * @brief 从设备读寄存器，带I2C总线错误恢复功能
 */
bool receive_from_device(UltrasoundHandleTypeDef* self, uint8_t reg, uint8_t* pdata, uint16_t size)
{
    // 尝试第一次通信
    if (HAL_I2C_Master_Transmit(&hi2c3, self->dev_addr << 1, &reg, 1, 10) == HAL_OK)
    {
        if (HAL_I2C_Master_Receive(&hi2c3, self->dev_addr << 1, pdata, size, 10) == HAL_OK)
        {
            return true; // 第一次通信成功，直接返回
        }
    }

    // 如果代码执行到这里，说明第一次通信失败了
    // 打印日志或设置调试断点，方便观察
    // printf("I2C communication failed. Resetting I2C bus...\r\n");

    // 重启I2C总线
    reset_i2c_bus(&hi2c3);

    // 延时一小段时间，给从设备一些反应时间
    HAL_Delay(5); 

    // 再次尝试通信
    if (HAL_I2C_Master_Transmit(&hi2c3, self->dev_addr << 1, &reg, 1, 10) != HAL_OK)
    {
        // 如果第二次还是失败，可以选择再次重启，或者直接返回失败
        return false;
    }

    if (HAL_I2C_Master_Receive(&hi2c3, self->dev_addr << 1, pdata, size, 10) != HAL_OK)
    {
        return false;
    }

    // 第二次尝试成功
    return true;
}


/**
 * @brief 重启I2C总线 
 * @param hi2c: I2C句柄指针
 */
void reset_i2c_bus(I2C_HandleTypeDef* hi2c)
{
	  GPIO_InitTypeDef GPIO_InitStruct = {0};
		GPIO_TypeDef* scl_port = GPIOA;
    uint16_t scl_pin = GPIO_PIN_8;
    GPIO_TypeDef* sda_port = GPIOC;
    uint16_t sda_pin = GPIO_PIN_9;
		
    // 执行HAL库反初始化和初始化
    HAL_I2C_DeInit(hi2c);
	  

	  // 将 SCL 和 SDA 引脚配置为 GPIO 推挽输出模式
    GPIO_InitStruct.Pin = scl_pin;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP; // 推挽输出
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(scl_port, &GPIO_InitStruct); 

	    // SCL引脚的电平强制拉低
    HAL_GPIO_WritePin(scl_port, scl_pin, GPIO_PIN_RESET);
	  HAL_Delay(1);
    HAL_I2C_Init(hi2c);
    
}

bool get_ultrasound_raw_distance(uint8_t* pdata)
{
    return receive_from_device(&ultrasound, ULTRASOUND_DISTANCE_REG, pdata, 2);
}


/**
 * @brief 初始化超声波结构体
 */
void ultrasound_init(void)
{
    memset(&ultrasound, 0, sizeof(UltrasoundHandleTypeDef));
    ultrasound.dev_addr = ULTRASOUND_ADDRESS;
    ultrasound.write_data = write_data;
    ultrasound.read_data = read_data;
}

/**
 * @brief 获取超声波距离（带简单滤波）
 */
uint16_t get_ultrasound_distance(void)
{
    static uint8_t raw_data[2];
    static uint16_t filter_buf[4] = {0};
    uint32_t sum = 0;

    if (!receive_from_device(&ultrasound, ULTRASOUND_DISTANCE_REG, raw_data, 2))
        return 0;

    uint16_t raw_distance = ((uint16_t)raw_data[1] << 8) | raw_data[0];
    filter_buf[3] = raw_distance;

    for (uint8_t i = 0; i < 3; i++) {
        filter_buf[i] = filter_buf[i + 1];
        sum += filter_buf[i];
    }

    ultrasound.distance = (uint16_t)(sum / 3);
    return ultrasound.distance;
}

/**
 * @brief 设置超声波 RGB 灯颜色
 */
void set_ultrasound_color(uint8_t mode, uint8_t* left_rgb, uint8_t* right_rgb)
{
    uint8_t set_reg, rgb[6];

    ultrasound.rgb_mode = mode;
    for (uint8_t i = 0; i < 3; i++) {
        rgb[i] = left_rgb[i];
        rgb[i + 3] = right_rgb[i];
        ultrasound.left_rgb[i] = rgb[i];
        ultrasound.right_rgb[i] = rgb[i + 3];
    }

    switch (mode)
    {
        case 0: // 固定颜色模式
            set_reg = RGB_WORK_SIMPLE_MODE_REG;
            write_to_device(&ultrasound, RGB_WORK_MODE_REG, &set_reg, 1);
            write_to_device(&ultrasound, RGB_CONSTANT_INDEX_REG, rgb, 6);
            break;

        case 1: // 呼吸灯模式
            set_reg = RGB_WORK_BREATHING_MODE_REG;
            write_to_device(&ultrasound, RGB_WORK_MODE_REG, &set_reg, 1);
            write_to_device(&ultrasound, RGB_BREATHING_INDEX_REG, rgb, 6);
            break;

        default:
            break;
    }
}
