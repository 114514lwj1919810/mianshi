/* AUTO-FILE-DOC
 * 文件职责：SPI 总线驱动。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

#ifndef __BSP_SPI_H
#define __BSP_SPI_H

#include "gd32f407_hal.h"
#include <stdint.h>

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
extern SPI_HandleTypeDef hspi1;
extern volatile uint32_t g_spi1_error_count;
extern volatile HAL_StatusTypeDef g_spi1_last_status;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void SPI1_Init(void);
HAL_StatusTypeDef SPI1_TransferByte(uint8_t tx, uint8_t *rx);
uint8_t SPI1_WriteByte(uint8_t byte);

#endif
