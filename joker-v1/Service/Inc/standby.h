/* AUTO-FILE-DOC
 * 文件职责：待机演示。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef STANDBY_H
#define STANDBY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化待机演示模块的内部时序。
 */
void Standby_Init(void);

/**
 * @brief 判断无币等待是否已经满足进入待机演示的条件。
 *
 * @param idle_start_ms Game 层进入无币 IDLE 的起始毫秒。
 * @param game_coin 当前可用游戏币数。
 * @param active_eye_mask 当前局已经触发并需要保留显示的光眼通道位图。
 *
 * @return 1 表示已无币 5 秒、没有待保留的光眼通道，且所有相关模块空闲。
 */
uint8_t Standby_CanStart(uint32_t idle_start_ms,
                         uint16_t game_coin,
                         uint16_t active_eye_mask);

/**
 * @brief 从逐灯5/6/7连线演示开始，之后循环老虎机送灯演示。
 * 所有分数、灯位及彩金变化仅用于本机显示，不改账目、不实际出奖。
 */
void Standby_Start(void);

/**
 * @brief 停止演示音效并恢复音量，解除本机彩金显示覆盖，清理演示灯光。
 */
void Standby_Stop(void);

/**
 * @brief 非阻塞推进待机数码管、光眼灯、4x4 灯板和音效动画。
 */
void Standby_Task(void);

/**
 * @brief 按当前待机动画帧刷新四路按键灯。
 *
 * Game 层在统一按键灯刷新位置调用，避免被普通游戏闪灯逻辑覆盖。
 */
void Standby_UpdateKeyLamps(void);

#ifdef __cplusplus
}
#endif

#endif
