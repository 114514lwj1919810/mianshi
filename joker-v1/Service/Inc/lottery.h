/* AUTO-FILE-DOC
 * 文件职责：转盘抽奖。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef LOTTERY_H
#define LOTTERY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化幸运抽奖 Service 的运行状态。
 *
 * 调用位置：
 * - Game_Init() 上电自检流程启动前调用一次。
 *
 * 逻辑说明：
 * - 清空抽奖请求、运行阶段和待领取奖励。
 * - 清空 4×4 跑灯板，不影响 12 位数码管显示。
 * - 初始化本身不阻塞；抽奖请求开始后由Game层进入LUCKY并锁住游戏操作键。
 */
void Lottery_Init(void);

/**
 * @brief 提交一次幸运抽奖请求。
 *
 * 调用位置：
 * - Game_Task() 在 START 状态检测到落入 1 号或 9 号通道时调用。
 *
 * @param lucky_channel_mask 触发幸运抽奖的通道位，bit0 表示 1 号通道，bit8 表示 9 号通道。
 * @param occupied_middle_mask 当前2~8号已经常亮的通道位图，送灯目标不会重复选择。
 *
 * 逻辑说明：
 * - 本函数只记录请求，不切换 Game_Mode；Game层在抽奖/送灯完成前锁住游戏键。
 * - 1号或9号光眼触发后立即启动4×4跑灯和013滚动音，约2声“哒”后在0.7秒结束。
 * - 送灯流程结束后才恢复游戏键，避免送灯尚未确认就启动下一局。
 */
void Lottery_Request(uint16_t lucky_channel_mask,
                     uint16_t occupied_middle_mask);

/**
 * @brief 推进一次幸运抽奖并行任务。
 *
 * 调用位置：
 * - Game_Task() 每轮主循环调用。
 *
 * 逻辑说明：
 * - 本函数是非阻塞状态机，不使用 delay。
 * - 前三列做老虎机列滚动，每列只亮一个LED。
 * - 第1列0.2秒停、第2列0.4秒停、第3列约0.7秒停。
 * - 正账按C3×返奖率-C4-D即时计算；负账池只累计进入负账后的新押币。
 * - 玩家已有中间灯时，奖项可送1~6灯；无灯时只能返10/5/3币或空奖。
 * - 送灯约占30%，返币约占70%；送灯可补成5/6/7连线。
 * - 面板送灯行统一表示本轮送灯，实际灯数由结果中的目标位图决定。
 */
void Lottery_Task(void);

/**
 * @brief 取走本轮老虎机新产生的送灯结果。
 *
 * 调用位置：
 * - Game_Task() 在 Lottery_Task() 后调用。
 *
 * @param target_mask 算法预选的最终目标位图。
 * @param first_target_mask 第一次揭晓的单灯位图。
 * @param second_target_mask 送2~6灯时第二次揭晓的单灯位图。
 * @return 1~6表示送灯数量，0表示没有送灯结果。
 */
uint8_t Lottery_TakeGiftResult(uint16_t *target_mask,
                               uint16_t *first_target_mask,
                               uint16_t *second_target_mask);

/** 取走本轮10/5/3币结果；0表示没有币奖。 */
uint16_t Lottery_TakeCoinAward(void);

/** 输出队列暂时无法接收时，把同一笔币奖退回等待队列。 */
void Lottery_ReturnCoinAward(uint16_t award_coins);

/**
 * @brief 通知老虎机本轮送灯跑马和目标确认已经全部完成。
 *
 * 在通知前，4×4老虎机中奖行保持闪烁；通知后才允许开始下一次排队请求。
 */
void Lottery_NotifyAwardComplete(void);

/** P8清账时清空返奖预算、大奖池和累计基线；忙碌或保存失败返回0。 */
uint8_t Lottery_ResetForAccountClear(void);

/**
 * @brief 查询当前是否存在未处理完的幸运抽奖请求。
 *
 * @return 1 表示有幸运抽奖请求或抽奖流程未结束；0 表示没有。
 */
uint8_t Lottery_IsBusy(void);
/** 抽奖、送灯或币奖输出尚未完成时返回1，作为LUCKY之外的游戏键兜底互锁。 */
uint8_t Lottery_BlocksGameKeys(void);

#ifdef __cplusplus
}
#endif

#endif
