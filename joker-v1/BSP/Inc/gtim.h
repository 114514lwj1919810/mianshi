/* AUTO-FILE-DOC
 * 文件职责：系统毫秒时基。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef GTIM_H
#define GTIM_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define GTIM_TICK_TIM                  TIM6
#define GTIM_TICK_IRQn                 TIM6_DAC_IRQn
#define GTIM_TICK_CLK_ENABLE()         do { __HAL_RCC_TIM6_CLK_ENABLE(); } while (0)
#define GTIM_20MS_PERIOD_MS            20U

void Gtim_Init(void);
void Gtim_Start(void);
void Gtim_Stop(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Gtim_Task(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Gtim_MarkTickFromIrq(void);

uint32_t Gtim_NowMs(void);
uint8_t Gtim_IsElapsed(uint32_t start_ms, uint32_t period_ms);
uint8_t Gtim_Take2msFlag(void);
uint8_t Gtim_Take10msFlag(void);
uint8_t Gtim_Take20msFlag(void);


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Gtim_Test1(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Gtim_Test2(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Gtim_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
