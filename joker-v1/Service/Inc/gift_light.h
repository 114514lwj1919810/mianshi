/* AUTO-FILE-DOC
 * 文件职责：gift_light 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef GIFT_LIGHT_H
#define GIFT_LIGHT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define GIFT_LIGHT_MIDDLE_MASK          ((uint16_t)0x00FEU)
#define GIFT_LIGHT_MAX_PER_REQUEST      6U

/**
 * @brief 初始化送灯动画状态机。
 */
void GiftLight_Init(void);

/**
 * @brief 开始一次送灯流程。
 *
 * @param target_mask     算法已经选定的1~6个目标灯位图。
 * @param first_target_mask 第一次揭晓的单灯位图。
 * @param second_target_mask 送2~6灯时第二次揭晓的单灯位图。
 * @param occupied_mask   当前已经常亮的2~8号通道位图。
 *
 * @return 实际能够送出的灯数；没有空灯时返回0。
 *
 * 逻辑：
 * - 本模块只负责表现，不再随机选择目标。
 * - 非法、重复或已亮目标直接拒绝，避免动画与算法结果不一致。
 * - 每个目标依次执行1.1秒跑马和1秒闪烁确认。
 */
uint8_t GiftLight_Start(uint16_t target_mask,
                        uint16_t first_target_mask,
                        uint16_t second_target_mask,
                        uint16_t occupied_mask);
void GiftLight_IncludeOccupiedMask(uint16_t occupied_mask);

/**
 * @brief 推进非阻塞送灯动画。
 */
void GiftLight_Task(void);

/**
 * @brief 取走已经确认送出的通道位图。
 *
 * 每个目标完成1秒闪烁确认后置位一次。调用者应把返回位并入本局
 * 通道历史，从而参与连线计算和后续重复落入失败判断。
 */
uint16_t GiftLight_TakeConfirmedMask(void);

/**
 * @brief 取走本次送灯全部结束事件。
 */
uint8_t GiftLight_TakeCompleted(void);

/**
 * @brief 查询送灯动画是否正在运行。
 */
uint8_t GiftLight_IsBusy(void);

#ifdef __cplusplus
}
#endif

#endif
