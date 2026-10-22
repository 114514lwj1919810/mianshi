/* AUTO-FILE-DOC
 * 文件职责：投币脉冲识别。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef COIN_H
#define COIN_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COIN_PORT                       GPIOF
#define COIN_PIN                        GPIO_PIN_15
#define COIN_VALID_LOW_MIN_MS           10U
#define COIN_VALID_LOW_MAX_MS           500U

void Coin_Init(void);

/**
 * @brief 投币 EXTI 中断专用标志函数。
 *
 * 调用位置：
 * - interrupt.c 的 EXTI15_10_IRQHandler() 清除 EXTI15 挂起位后调用。
 *
 * 逻辑说明：
 * - 下降沿记录开始时间，上升沿计算完整低电平脉宽。
 * - 10~500ms的完整低脉冲才累加待处理投币数；窄毛刺和长时间拉低不计币。
 * - 不在中断里更新游戏账目或刷新显示。
 */
void Coin_MarkExtiFromIrq(void);

/**
 * @brief 在前台处理投币 EXTI 标志。
 *
 * 调用位置：
 * - Game_Task() 每轮调用。
 */
void Coin_Task(void);

uint32_t Coin_GetCount(void);
uint32_t Coin_TakeCount(void);
uint8_t Coin_IsActive(void);

/**
 * @brief 调试用：临时屏蔽投币计数，并清掉已经积累的投币脉冲。
 *
 * 调用位置：
 * - 正常游戏流程不调用，仅保留给板级测试。
 *
 * 逻辑说明：
 * - 正常流程依靠双边沿10~500ms完整低脉冲确认过滤干扰。
 * - 本函数会先清掉 s_irq_pending 和 s_count，再开启一个屏蔽窗口。
 * - 屏蔽窗口内 Coin_Task() 会继续消费 EXTI 标志，但不会把它们累加成有效投币。
 *
 * @param block_ms 屏蔽时间，单位 ms；传 0 表示只清当前残留，不开启持续屏蔽。
 */
void Coin_Block(uint32_t block_ms);

/**
 * @brief 清除投币模块的旧待处理脉冲和旧累计计数。
 *
 * 调用位置：
 * - 正常游戏流程不调用；板级测试或重新初始化输入状态时使用。
 *
 * 逻辑说明：
 * - 同时清 s_irq_pending 和 s_count，比只调用 Coin_TakeCount() 更彻底。
 * - 清完后把滤波基准时间设置为当前时间，避免刚退出自检时的尾脉冲马上被算成新币。
 */
void Coin_Clear(void);


/**
 * @brief Coin_Test1：清除投币旧计数和旧中断标志
 *
 * 调用说明：
 * - 这是 BSP 手动测试示例函数，正常游戏流程不会自动调用。
 * - 调试时可以临时在 App/Game 里调用，确认后再注释掉。
 */
void Coin_Test1(void);

/**
 * @brief Coin_Test2：临时屏蔽投币输入 1 秒
 *
 * 调用说明：
 * - 这是 BSP 手动测试示例函数，正常游戏流程不会自动调用。
 * - 如果本函数会驱动真实输出，调用前要确认现场硬件安全。
 */
void Coin_Test2(void);

/**
 * @brief Coin_Test3：读取并取走当前投币计数
 *
 * 调用说明：
 * - 这是 BSP 手动测试示例函数，正常游戏流程不会自动调用。
 * - 主要用于给以后板级排查提供最小调用例子。
 */
void Coin_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
