/* AUTO-FILE-DOC
 * 文件职责：震动检测。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef SHAKE_H
#define SHAKE_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SHAKE_PORT                 GPIOF
#define SHAKE_PIN                  GPIO_PIN_8
#define SHAKE_EXTI_FILTER_MS       50U

void Shake_Init(void);
void Shake_MarkExtiFromIrq(void);
void Shake_Task(void);
uint32_t Shake_GetCount(void);
uint32_t Shake_TakeCount(void);
uint8_t Shake_IsActive(void);
void Shake_Clear(void);

void Shake_Test1(void);
void Shake_Test2(void);
void Shake_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
