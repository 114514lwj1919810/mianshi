/* AUTO-FILE-DOC
 * 文件职责：机械计数器脉冲。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef METER_H
#define METER_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define METER_COIN_IN_PORT             GPIOB
#define METER_COIN_IN_PIN              GPIO_PIN_12
#define METER_TICKET_OUT_PORT          GPIOB
#define METER_TICKET_OUT_PIN           GPIO_PIN_13
#define METER_PAYOUT_OUT_PORT          GPIOB
#define METER_PAYOUT_OUT_PIN           GPIO_PIN_14

#define METER_IDLE_LEVEL               GPIO_PIN_SET
#define METER_ACTIVE_LEVEL             GPIO_PIN_RESET
#define METER_PULSE_LOW_MS             50U
#define METER_PULSE_GAP_MS             50U
#define METER_PENDING_MAX              60000UL

typedef enum
{
    METER_COIN_IN = 0U,
    METER_TICKET_OUT,
    METER_PAYOUT_OUT,
    METER_COUNT
} Meter_Id;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Meter_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Meter_Task(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Meter_AddPulse(Meter_Id meter, uint32_t pulse_count);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Meter_ClearPending(Meter_Id meter);


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Meter_Test1(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Meter_Test2(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Meter_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
