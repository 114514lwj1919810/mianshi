/* AUTO-FILE-DOC
 * 文件职责：nrf24l01_port 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

#ifndef __NRF24L01_PORT_H
#define __NRF24L01_PORT_H

#include "gd32f407_hal.h"

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_IRQ_CONFIGURED        0U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_GPIO_CLOCK_ENABLE()         \
    do {                                  \
        __HAL_RCC_GPIOA_CLK_ENABLE();     \
        __HAL_RCC_GPIOE_CLK_ENABLE();     \
        __HAL_RCC_SYSCFG_CLK_ENABLE();    \
    } while (0)

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_CSN_GPIO_PORT         GPIOA
#define NRF24_CSN_GPIO_PIN          GPIO_PIN_3

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_CE_GPIO_PORT          GPIOA
#define NRF24_CE_GPIO_PIN           GPIO_PIN_2

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_IRQ_GPIO_PORT         GPIOE
#define NRF24_IRQ_GPIO_PIN          GPIO_PIN_9
#define NRF24_IRQ_EXTI_IRQn         EXTI9_5_IRQn

#endif
