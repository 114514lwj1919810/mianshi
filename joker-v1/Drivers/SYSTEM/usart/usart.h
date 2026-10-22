/**
 * @file usart.h
 * @brief USART1 printf 输出和接收辅助函数声明。
 */
#ifndef _USART_H
#define _USART_H

#include "stdio.h"
#include "./SYSTEM/sys/sys.h"


/*******************************************************************************************************/
/* 引脚和串口 定义 */

#define USART_TX_GPIO_PORT              GPIOC
#define USART_TX_GPIO_PIN               GPIO_PIN_12
#define USART_TX_GPIO_AF                GPIO_AF8_UART5
#define USART_TX_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOC_CLK_ENABLE(); }while(0)

#define USART_RX_GPIO_PORT              GPIOD
#define USART_RX_GPIO_PIN               GPIO_PIN_2
#define USART_RX_GPIO_AF                GPIO_AF8_UART5
#define USART_RX_GPIO_CLK_ENABLE()      do{ __HAL_RCC_GPIOD_CLK_ENABLE(); }while(0)

#define USART_UX                        UART4
#define USART_UX_IRQn                   UART4_IRQn
#define USART_UX_IRQHandler             UART4_IRQHandler
#define USART_UX_CLK_ENABLE()           do{ __HAL_RCC_UART5_CLK_ENABLE(); }while(0)
#define AUDIO_UART_TX_GPIO_PORT          GPIOA
#define AUDIO_UART_TX_GPIO_PIN           GPIO_PIN_0
#define AUDIO_UART_TX_GPIO_AF            GPIO_AF8_UART4
#define AUDIO_UART_RX_GPIO_PORT          GPIOA
#define AUDIO_UART_RX_GPIO_PIN           GPIO_PIN_1
#define AUDIO_UART_RX_GPIO_AF            GPIO_AF8_UART4
#define AUDIO_UART                       UART3
#define AUDIO_UART_IRQn                  UART3_IRQn
#define AUDIO_UART_CLK_ENABLE()          do{ __HAL_RCC_UART4_CLK_ENABLE(); }while(0)
#define AUDIO_UART_GPIO_CLK_ENABLE()     do{ __HAL_RCC_GPIOA_CLK_ENABLE(); }while(0)

#define AUDIO2_UART_TX_GPIO_PORT         GPIOB
#define AUDIO2_UART_TX_GPIO_PIN          GPIO_PIN_10
#define AUDIO2_UART_TX_GPIO_AF           GPIO_AF7_USART3
#define AUDIO2_UART_RX_GPIO_PORT         GPIOB
#define AUDIO2_UART_RX_GPIO_PIN          GPIO_PIN_11
#define AUDIO2_UART_RX_GPIO_AF           GPIO_AF7_USART3
#define AUDIO2_UART                      USART2
#define AUDIO2_UART_IRQn                 USART2_IRQn
#define AUDIO2_UART_CLK_ENABLE()         do{ __HAL_RCC_USART3_CLK_ENABLE(); }while(0)
#define AUDIO2_UART_GPIO_CLK_ENABLE()    do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)

#define USART_RX_FIFO_SIZE 256U
#define AUDIO_UART_RX_FIFO_SIZE 64U
#define AUDIO_UART_CHANNEL_1    0U
#define AUDIO_UART_CHANNEL_2    1U
#define AUDIO_UART_CHANNEL_COUNT 2U
#define USART_REC_LEN   200                     /* 定义最大接收字节数 200 */
#define USART_EN_RX     1                       /* 使能（1）/禁止（0）串口1接收 */
#define RXBUFFERSIZE    1                       /* 缓存大小 */

extern UART_HandleTypeDef g_uart1_handle;       /* UART句柄 */
extern UART_HandleTypeDef g_audio_uart1_handle;
extern UART_HandleTypeDef g_audio_uart2_handle;

extern uint8_t  g_usart_rx_buf[USART_REC_LEN];  /* 接收缓冲,最大USART_REC_LEN个字节.末字节为换行符 */
extern uint16_t g_usart_rx_sta;                 /* 接收状态标记 */
extern uint8_t g_rx_buffer[RXBUFFERSIZE];
extern volatile uint16_t g_usart_rx_head;
extern volatile uint16_t g_usart_rx_tail;       /* HAL库USART接收Buffer */

/*******************************************************************************************************/

void usart_init(uint32_t baudrate);             /* 串口初始化函数 */
void audio_usart_init(uint32_t baudrate);
uint16_t audio_usart_read(uint8_t channel, uint8_t *data, uint16_t len);
void audio_usart_clear_rx(uint8_t channel);
void usart_send_data(const uint8_t *data, uint16_t len);
uint16_t usart_read(uint8_t *data, uint16_t len);
uint16_t usart_available(void);
void usart_clear_rx(void);
void usart_restart_rx(void);

#endif
