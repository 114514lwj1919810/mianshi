/* AUTO-FILE-DOC
 * 文件职责：gd32f407 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef JOKER_GD32F407_H
#define JOKER_GD32F407_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GD32F407_HEARTBEAT_PORT GPIOC
#define GD32F407_HEARTBEAT_PIN  GPIO_PIN_13

void GD32F407_Init(void);
void GD32F407_HeartbeatSet(uint8_t high_level);
void GD32F407_HeartbeatToggle(void);
void GD32F407_Test1(void);
void GD32F407_Test2(void);
void GD32F407_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
