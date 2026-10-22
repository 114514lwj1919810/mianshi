/* AUTO-FILE-DOC
 * 文件职责：数码屏和灯扫描。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef SEGMENT_H
#define SEGMENT_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEGMENT_4094_COUNT              14U
#define SEGMENT_BIT_DELAY_US            5U
#define SEGMENT_POWER_CLEAR_FRAMES      6U
#define SEGMENT_POWER_CLEAR_GAP_MS      10U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define SEGMENT_CLK_PORT                GPIOG
#define SEGMENT_CLK_PIN                 GPIO_PIN_7
#define SEGMENT_LATCH_PORT              GPIOG
#define SEGMENT_LATCH_PIN               GPIO_PIN_8
#define SEGMENT_DATA_PORT               GPIOC
#define SEGMENT_DATA_PIN                GPIO_PIN_6

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_EarlySafeInit(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_Task(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_ClearAll(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_SetLeftNumber(uint16_t value);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_SetMiddleNumber(uint16_t value);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_SetRightNumber(uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
