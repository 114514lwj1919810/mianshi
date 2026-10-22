/* AUTO-FILE-DOC
 * 文件职责：彩金显示板通信。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef JACKPOT_DISPLAY_H
#define JACKPOT_DISPLAY_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 新 GD32 板 P2/CN6 控制线：
 *   OUT-OC / PC11 -> C (4094 clock)
 *   OUT-OS / PD0  -> L (4094 latch/strobe)
 *   OUT-OD / PD1  -> D (4094 serial data DI)
 *
 * 电平和段码以已上板验证的“H743鹿小班测试”为准：三路均采用低有效写法，
 * 逻辑 1 对应 GPIO 物理低电平，逻辑 0 对应 GPIO 物理高电平。
 */
#define JACKPOT_DISPLAY_CLK_PORT       GPIOC
#define JACKPOT_DISPLAY_CLK_PIN        GPIO_PIN_11
#define JACKPOT_DISPLAY_LATCH_PORT     GPIOD
#define JACKPOT_DISPLAY_LATCH_PIN      GPIO_PIN_0
#define JACKPOT_DISPLAY_DATA_PORT      GPIOD
#define JACKPOT_DISPLAY_DATA_PIN       GPIO_PIN_1

/* 彩金板4094输出为0有效：段码在移入4094前逐位取反。 */
#define JACKPOT_DISPLAY_OUTPUT_ACTIVE_LOW  1U

/* 当前彩金范围为 0~999，对应三位数码管、三片级联 4094。 */
#define JACKPOT_DISPLAY_4094_COUNT     3U
#define JACKPOT_DISPLAY_MAX_VALUE      999U
#define JACKPOT_DISPLAY_BIT_DELAY_US   5U
#define JACKPOT_DISPLAY_REFRESH_MS     20U
#define JACKPOT_DISPLAY_BLINK_MS       250U
#define JACKPOT_DISPLAY_CLEAR_FRAMES   6U
#define JACKPOT_DISPLAY_CLEAR_GAP_MS   10U

void JackpotDisplay_EarlySafeInit(void);
void JackpotDisplay_Init(void);
void JackpotDisplay_SetValue(uint16_t value);
void JackpotDisplay_SetBlink(uint8_t enable);
/* 本机演示覆盖：真实值仍在后台更新；结束演示恢复最新真实值。 */
void JackpotDisplay_SetDemo(uint16_t value, uint8_t blink);
void JackpotDisplay_EndDemo(void);
void JackpotDisplay_Task(void);
void JackpotDisplay_Clear(void);

#ifdef __cplusplus
}
#endif

#endif
