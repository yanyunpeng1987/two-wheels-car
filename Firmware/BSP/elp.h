#ifndef __ELP_H__
#define __ELP_H__

#include "stdint.h"

typedef struct{
  uint16_t  Sensor_1;
	uint16_t  Sensor_2;
  uint16_t  Sensor_3;
  uint16_t  Sensor_4;
	uint16_t  Sensor_5;
  uint16_t  Sensor_6;
	uint16_t  Sensor_7;
}SENSOR_RESULT;


extern SENSOR_RESULT sensor_result; 
extern int elp_position;

void Guass_Way(void);
uint16_t ELP_ReadData(void);

#endif