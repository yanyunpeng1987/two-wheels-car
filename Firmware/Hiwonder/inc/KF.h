#ifndef __KF_H
#define __KF_H


/**
 * @brief 卡尔曼滤波
 * 
 * @param acce_X 加速度X轴
 * @param acce_Z 加速度Z轴
 * @param gyro_Y 角速度Y轴
 * @return float 
 */
float KF_Y(float acce_X, float acce_Z, float gyro_Y);


/**
 * @brief 卡尔曼滤波y轴
 * 
 * @param acce_Y 加速度Y轴
 * @param acce_Z 加速度Z轴
 * @param gyro_X 角速度X轴
 * @return float 
 */
float KF_X(float acce_Y, float acce_Z, float gyro_X);


/**
 * @brief 矩阵乘法
 * 
 * @param A_row A矩阵的行数
 * @param A_col A矩阵的列数
 * @param B_row B矩阵的行数
 * @param B_col B矩阵的列数
 * @param A A矩阵
 * @param B B矩阵
 * @param C 结果矩阵
 */
void mul(int A_row, int A_col, int B_row, int B_col, float A[][A_col], float B[][B_col], float C[][B_col]);
















#endif





