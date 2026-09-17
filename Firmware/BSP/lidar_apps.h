// File: lidar_apps.h
// Description: Lidar application layer interface.

#ifndef INC_LIDAR_APPS_H_
#define INC_LIDAR_APPS_H_

#include "main.h"
typedef struct {
    float front_dist;
		float front_angle; 
    float back_dist;
    float left_dist;
    float right_dist;
		float right_front_dist; 
} LidarSectors_t;

extern LidarSectors_t g_sectors; 
void LidarApps_RunTasks(void);
void LidarApps_Init(void);
void LidarApps_Process(void);


#endif /* INC_LIDAR_APPS_H_ */