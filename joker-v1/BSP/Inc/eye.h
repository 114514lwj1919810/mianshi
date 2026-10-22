/* AUTO-FILE-DOC
 * 文件职责：落球光眼扫描。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef EYE_H
#define EYE_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EYE_CH1_PORT                    GPIOF
#define EYE_CH1_PIN                     GPIO_PIN_4
#define EYE_CH2_PORT                    GPIOF
#define EYE_CH2_PIN                     GPIO_PIN_3
#define EYE_CH3_PORT                    GPIOF
#define EYE_CH3_PIN                     GPIO_PIN_2
#define EYE_CH4_PORT                    GPIOF
#define EYE_CH4_PIN                     GPIO_PIN_1
#define EYE_CH5_PORT                    GPIOF
#define EYE_CH5_PIN                     GPIO_PIN_0
#define EYE_CH6_PORT                    GPIOE
#define EYE_CH6_PIN                     GPIO_PIN_6
#define EYE_CH7_PORT                    GPIOE
#define EYE_CH7_PIN                     GPIO_PIN_5
#define EYE_CH8_PORT                    GPIOE
#define EYE_CH8_PIN                     GPIO_PIN_4
#define EYE_CH9_PORT                    GPIOE
#define EYE_CH9_PIN                     GPIO_PIN_3

#define EYE_GAME_MOTOR_PORT             GPIOB
#define EYE_GAME_MOTOR_PIN              GPIO_PIN_0
#define EYE_SPIT_COIN_PORT              GPIOF
#define EYE_SPIT_COIN_PIN               GPIO_PIN_10
#define EYE_TICKET_OUT_PORT             GPIOF
#define EYE_TICKET_OUT_PIN              GPIO_PIN_9
#define EYE_SOLENOID_CHECK_PORT         GPIOF
#define EYE_SOLENOID_CHECK_PIN          GPIO_PIN_5

#define EYE_CHANNEL_STABLE_TICKS        3U
#define EYE_EXTI_FILTER_MS              20U

typedef enum
{
    EYE_CHANNEL_1 = 0U,
    EYE_CHANNEL_2,
    EYE_CHANNEL_3,
    EYE_CHANNEL_4,
    EYE_CHANNEL_5,
    EYE_CHANNEL_6,
    EYE_CHANNEL_7,
    EYE_CHANNEL_8,
    EYE_CHANNEL_9,
    EYE_CHANNEL_COUNT
} Eye_Channel;

typedef enum
{
    EYE_COUNTER_GAME_MOTOR = 0U,
    EYE_COUNTER_SPIT_COIN,
    EYE_COUNTER_TICKET_OUT,
    EYE_COUNTER_SOLENOID_CHECK,
    EYE_COUNTER_COUNT
} Eye_Counter;

void Eye_Init(void);
void Eye_ScanChannels2ms(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Eye_MarkCounterFromIrq(Eye_Counter counter);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Eye_ExtiTask(void);

uint16_t Eye_GetChannelActiveMask(void);
uint16_t Eye_TakeChannelTriggerMask(void);
uint32_t Eye_GetCounter(Eye_Counter counter);
uint32_t Eye_TakeCounter(Eye_Counter counter);
uint8_t Eye_IsCounterActive(Eye_Counter counter);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Eye_ClearCounter(Eye_Counter counter);

uint8_t Eye_TakeCounterEventMask(void);


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Eye_Test1(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Eye_Test2(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Eye_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
