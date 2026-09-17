/**
 * @file encoder.h
 * @author LuYongping
 * @brief 编码器计数
 * @version 0.1
 * @date 2025-07-09
 * 
 * @copyright Copyright (c) 2025 Hiwonder
 * 
 */



#ifndef __ENCODER_H__
#define __ENCODER_H__


#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  读取编码器计数值
 * 
 * @param timx 
 * @return int 
 */
int read_encoder(uint8_t timx);

#ifdef __cplusplus
}
#endif

#endif /* __ENCODER_H__ */
