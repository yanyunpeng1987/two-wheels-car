#include "eight_way.h"
#include "i2c.h"
#include "string.h"
#include "usart.h"
#include <stdio.h> 

LineFollowHandleTypeDef LineFollow;

static uint8_t write_data(LineFollowHandleTypeDef* self, uint8_t* pdata, uint16_t size)
{

	  return HAL_I2C_Master_Transmit(&hi2c3, self->dev_addr << 1, pdata, size, 10);

}

static uint8_t read_data(LineFollowHandleTypeDef* self, uint8_t* pdata, uint16_t size)
{    
		return HAL_I2C_Master_Receive(&hi2c3, self->dev_addr << 1, pdata, size, 10);

}

static bool write_to_device(LineFollowHandleTypeDef* self, uint8_t reg, uint8_t* pdata, uint16_t size)
{
	  uint8_t trans_data[size + 1];
    trans_data[0] = reg;
    for (uint16_t i = 0; i < size; i++)
        trans_data[1 + i] = pdata[i];

    return (write_data(self, trans_data, sizeof(trans_data)) == HAL_OK);
}

static bool receive_from_device(LineFollowHandleTypeDef* self, uint8_t reg, uint8_t* pdata, uint16_t size)
{
	  if (HAL_I2C_Master_Transmit(&hi2c3, self->dev_addr << 1, &reg, 1, 10) != HAL_OK)
        return false;

    if (HAL_I2C_Master_Receive(&hi2c3, self->dev_addr << 1, pdata, size, 10) != HAL_OK)
        return false;
		
    return true;
}

void LineFollowIIC_init()
{
    memset(&LineFollow, 0, sizeof(LineFollow));
    LineFollow.write_data = write_data;
    LineFollow.read_data = read_data;
    LineFollow.dev_addr = LineFollow_ADDRESS;
}

bool LineFollowIIC_State(LineFollowHandleTypeDef* State)
{
		char buffer[64];
    if(receive_from_device(&LineFollow, LineFollow_STATE_REG, LineFollow.results, 1))
    {
        while(HAL_I2C_STATE_READY != HAL_I2C_GetState(&hi2c3));
        for(int i=0; i<sizeof(LineFollow.data);i++){
            State->data[i] = (LineFollow.results[0] >> i) & 0x01;
        }
				sprintf(buffer, "State: %d%d%d%d%d%d%d%d\r\n",
                State->data[0], State->data[1], State->data[2], State->data[3],
                State->data[4], State->data[5], State->data[6], State->data[7]);
        
        return true;
    }
    return false;
}

bool LineFollowIIC_Analog(LineFollowHandleTypeDef* Analog)
{
    uint8_t count = 0;
    if(receive_from_device(&LineFollow, LineFollow_ANALOG_REG, LineFollow.results, sizeof(LineFollow.results)))
    {
        while(HAL_I2C_STATE_READY != HAL_I2C_GetState(&hi2c3))
			        for(int i=0; i<sizeof(LineFollow.data);i++){
            Analog->data[i] = LineFollow.results[count] | (LineFollow.results[count+1] << 8);
            count += 2;
        }
        return true;
    }
    return false;
}

bool LineFollowIIC_Threshold(LineFollowHandleTypeDef* Threshold)
{
    uint8_t count = 0;
    if(receive_from_device(&LineFollow, LineFollow_THRESHOLD_REG, LineFollow.results, sizeof(LineFollow.results)))
    {
        while(HAL_I2C_STATE_READY != HAL_I2C_GetState(&hi2c3));
        for(int i=0; i<sizeof(LineFollow.data);i++){
            Threshold->data[i] = LineFollow.results[count] | (LineFollow.results[count+1] << 8);
            count += 2;
        }
        return true;
    }
    return false;
}
