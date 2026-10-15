#ifndef BT_TEST_HAL_H
#define BT_TEST_HAL_H
#include <stdint.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY, HAL_TIMEOUT } HAL_StatusTypeDef;
typedef enum { HAL_UART_RXEVENT_TC, HAL_UART_RXEVENT_HT, HAL_UART_RXEVENT_IDLE } HAL_UART_RxEventTypeTypeDef;
typedef struct { unsigned int ht_enabled; } DMA_HandleTypeDef;
typedef struct { void *Instance; DMA_HandleTypeDef *hdmarx; } UART_HandleTypeDef;
typedef struct { uint32_t CYCCNT; } DWT_Type;
extern DWT_Type test_dwt;
extern uint32_t SystemCoreClock;
#define DWT (&test_dwt)
#define USART6 ((void *)6)
#define DMA_IT_HT 1U
#define HAL_UART_TX_COMPLETE_CB_ID 1U
#define __HAL_DMA_DISABLE_IT(handle, mask) ((void)(mask), (handle)->ht_enabled = 0U)
#define __DMB() ((void)0)
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t previous);
HAL_StatusTypeDef HAL_UARTEx_ReceiveToIdle_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_RegisterRxEventCallback(UART_HandleTypeDef *, void (*)(UART_HandleTypeDef *, uint16_t));
HAL_StatusTypeDef HAL_UART_RegisterCallback(UART_HandleTypeDef *, unsigned int, void (*)(UART_HandleTypeDef *));
HAL_UART_RxEventTypeTypeDef HAL_UARTEx_GetRxEventType(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
#endif
