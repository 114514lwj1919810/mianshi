/* AUTO-FILE-DOC
 * 文件职责：游戏主状态机。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef GAME_H
#define GAME_H

#include <stdint.h>

/* MMDD + 四位发布编号；例如10200001、10200002、10210002。C48分屏显示。 */
#define GAME_FIRMWARE_VERSION 10210007UL

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 游戏 Service 主状态。
 *
 * 状态说明：
 * - GAME_MODE_POST：上电自检状态，当前只跑数码管自检。
 * - GAME_MODE_IDLE：无币等待状态，等待投币、设置、查账等上层命令。
 * - GAME_MODE_STANDBY：无币 IDLE 满 5 秒后进入，循环演示数码管、音效和灯光。
 * - GAME_MODE_READY：已有投币或分数，等待玩家按开始键。
 * - GAME_MODE_START：游戏开始，等待游戏电机计数光眼和 9 路落币通道光眼。
 * - GAME_MODE_LUCKY：1/9号眼触发抽奖后的锁定状态，等待抽奖和送灯完整结束。
 * - GAME_MODE_ONLINE：连线得分状态，后续接 score 模块。
 * - GAME_MODE_TICKET_HOLD：按出票键后，中屏先保持 1.2 秒给玩家看清，再启动吐币。
 * - GAME_MODE_OVER：本轮结束状态，后续接账目、显示和清场逻辑。
 * - GAME_MODE_SETTING：设置页面状态，查看/修改 P1~P16，投币和落币光眼无效。
 * - GAME_MODE_ACCOUNT：查看C1~C50；收支及奖励来源集中在C1~C19，C48为固件版本，C49/C50为旧账历史。
 * - GAME_MODE_FAULT：故障状态，后续接 fault 模块。
 */
typedef enum
{
    GAME_MODE_POST = 0U,
    GAME_MODE_IDLE,
    GAME_MODE_STANDBY,
    GAME_MODE_READY,
    GAME_MODE_START,
    GAME_MODE_LUCKY,
    GAME_MODE_ONLINE,
    GAME_MODE_LINE7_HOLD,
    GAME_MODE_JACKPOT_WAIT,
    GAME_MODE_TICKET_HOLD,
    GAME_MODE_TICKET_SKIP_HOLD,
    GAME_MODE_OVER,
    GAME_MODE_SETTING,
    GAME_MODE_ACCOUNT,
    GAME_MODE_FAULT
} Game_Mode;

/**
 * @brief 初始化游戏 Service 状态机。
 *
 * 调用位置：
 * - App_Init() 在 BSP、NVIC 和 TIM6 都启动完成后调用一次。
 *
 * 逻辑说明：
 * - Account_Init() 从 EEPROM 读取账目和掉电前的 s_game_score/s_game_prize/s_game_coin。
 * - 初始化 Lottery/Payout 并行 Service。
 * - 设置状态为 GAME_MODE_POST。
 * - 启动 Service/self_test.c 的上电自检流程。
 * - 自检结束后再把 EEPROM 恢复值写回三组数码管。
 */
void Game_Init(void);

/**
 * @brief 执行一次游戏 Service 主任务。
 *
 * 调用位置：
 * - App_Loop() 每轮只调用本函数。
 *
 * 逻辑说明：
 * - 先消费 TIM6、投币、计数光眼等中断标志。
 * - 再执行数码管刷新、按键查询、通道光眼查询、电磁铁释放检查。
 * - Lottery/Payout 作为周期任务每轮推进；抽奖和送灯期间由LUCKY状态锁住游戏键。
 * - Account_Task() 负责把变化后的账目和三组显示值延迟写入 EEPROM。
 * - 最后推进上电自检和游戏状态机。
 */
void Game_Task(void);

/**
 * @brief 读取当前游戏状态。
 *
 * 调用位置：
 * - 后续调试显示、故障处理或页面判断时调用。
 *
 * @return 当前 Game_Mode 状态。
 */
Game_Mode Game_GetMode(void);

/**
 * @brief 设置左侧分数显示值。
 *
 * 调用位置：
 * - 后续 score/lottery/payout 等 Service 计算完分数后调用。
 *
 * 逻辑说明：
 * - 更新内存变量和数码管缓冲区，并同步给 account 模块用于掉电恢复。
 */
void Game_SetScore(uint16_t value);

/**
 * @brief 设置中间奖励显示值。
 *
 * 调用位置：
 * - 后续 lottery/payout 等 Service 更新奖励值时调用。
 */
void Game_SetPrize(uint16_t value);

/**
 * @brief 设置右侧投币显示值。
 *
 * 调用位置：
 * - 投币事件被 Game_Task() 消费后调用。
 * - 后续账目恢复或清零投币数时也可调用。
 */
void Game_SetCoin(uint16_t value);

/* 算法/查账只读调用：右屏可兑余额+中屏/票/卡欠付，百万分之一币，不含左屏奖。 */
uint64_t Game_GetPendingReturnValueScaled(void);

#ifdef __cplusplus
}
#endif

#endif
