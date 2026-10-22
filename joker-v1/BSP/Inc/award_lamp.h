/* AUTO-FILE-DOC
 * 文件职责：送灯输出。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef AWARD_LAMP_H
#define AWARD_LAMP_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 引脚设计表中的 H13/OUT-H13 实际连接到 MCU 的 PG2。 */
#define AWARD_LAMP_PORT                 GPIOG
#define AWARD_LAMP_PIN                  GPIO_PIN_2
#define AWARD_LAMP_ON_LEVEL             GPIO_PIN_SET
#define AWARD_LAMP_OFF_LEVEL            GPIO_PIN_RESET
#define AWARD_LAMP_BLINK_HALF_PERIOD_MS 500U

void AwardLamp_Init(void);
void AwardLamp_StartBlink(uint32_t duration_ms);
void AwardLamp_StartBlinkContinuous(void);
void AwardLamp_Stop(void);
void AwardLamp_Task(void);
uint8_t AwardLamp_IsBlinking(void);

#ifdef __cplusplus
}
#endif

#endif
