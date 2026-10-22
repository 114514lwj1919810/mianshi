/* AUTO-FILE-DOC
 * 文件职责：电机 GPIO 与定时动作。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef MOTOR_H
#define MOTOR_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define MOTOR_GAME_COIN_FORWARD_PORT        GPIOD
#define MOTOR_GAME_COIN_FORWARD_PIN         GPIO_PIN_8
#define MOTOR_GAME_COIN_REVERSE_PORT        GPIOD
#define MOTOR_GAME_COIN_REVERSE_PIN         GPIO_PIN_15

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define MOTOR_WIN_PRIZE_FORWARD_PORT        GPIOD
#define MOTOR_WIN_PRIZE_FORWARD_PIN         GPIO_PIN_9
#define MOTOR_WIN_PRIZE_REVERSE_PORT        GPIOG
#define MOTOR_WIN_PRIZE_REVERSE_PIN         GPIO_PIN_3

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define MOTOR_RUN_LEVEL                     GPIO_PIN_SET
#define MOTOR_STOP_LEVEL                    GPIO_PIN_RESET

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
typedef enum
{
    MOTOR_GAME_COIN = 0U,
    MOTOR_WIN_PRIZE,
    MOTOR_COUNT
} Motor_Id;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
typedef enum
{
    MOTOR_DIR_STOP = 0U,
    MOTOR_DIR_FORWARD,
    MOTOR_DIR_REVERSE
} Motor_Direction;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
typedef enum
{
    MOTOR_LINE_GAME_COIN_FORWARD = 0U,
    MOTOR_LINE_GAME_COIN_REVERSE,
    MOTOR_LINE_WIN_PRIZE_FORWARD,
    MOTOR_LINE_WIN_PRIZE_REVERSE,
    MOTOR_LINE_COUNT
} Motor_Line;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_EarlySafeInit(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_Task(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_Stop(Motor_Id motor);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_StopAll(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_RunForward(Motor_Id motor);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_RunReverse(Motor_Id motor);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_PulseForward(Motor_Id motor, uint32_t run_ms);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_PulseReverse(Motor_Id motor, uint32_t run_ms);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_WriteLine(Motor_Line line, uint8_t enable);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Motor_IsBusy(Motor_Id motor);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
Motor_Direction Motor_GetDirection(Motor_Id motor);


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_Test1(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_Test2(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Motor_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
