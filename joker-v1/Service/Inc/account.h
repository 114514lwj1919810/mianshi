/* AUTO-FILE-DOC
 * 文件职责：查账数据与持久化。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef ACCOUNT_H
#define ACCOUNT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 彩票和卡片返奖价值使用百万分之一币滚动汇总，参数修改次数不会增加EEPROM占用。 */
#define ACCOUNT_PAYOUT_VALUE_SCALE     1000000ULL

/**
 * @brief 出奖来源。
 *
 * 作用：
 * - Game 层把奖励追加到 Payout 层时，同时告诉 Account 这笔奖励来自哪里。
 * - 连线出票和老虎机出币都可能进入同一个吐币电机队列，但账目上必须分开累计。
 */
typedef enum
{
    ACCOUNT_PRIZE_SOURCE_LINE = 0U,
    ACCOUNT_PRIZE_SOURCE_LOTTERY
} Account_PrizeSource;

/**
 * @brief EEPROM 里保存的最后一次有效 RTC 时间。
 *
 * 作用：
 * - PCF8563 没有可靠备用电池，或者电源插拔导致 RTC 掉到 2000-01-01 时，
 *   上电后可以先从 EEPROM 恢复到上一次保存的有效时间。
 * - 这个时间不能计算“断电期间经过了多久”，它只保证不要回到 2000-01-01。
 */
typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t valid;
} Account_RtcTime;

/**
 * @brief 账目和掉电保存数据快照。
 *
 * 字段说明：
 * - coin_in_total：旧主账混合历史流水；升级后仅按开始键实际押分增加。
 * - game_use_total：游戏消耗数，玩家按游戏键并成功扣掉 1 个 s_game_coin 时加 1。
 * - line_ticket_total：按出票键获得的币数，也就是连线得分兑换成的出币目标。
 * - lottery_payout_total：只通过老虎机抽奖产生的吐币数。
 * - payout_total：旧主账混合历史流水；升级后连线/彩金/老虎机开奖时记一次。
 *   循环转右屏和实际兑现均不重复加；旧值保留在C46/C47，C45标记混合账。
 * - physical_coin_in/out_total：实体投币器/吐币器计数，独立双槽保存，显示C1/C2。
 * - ticket_out_total：总出票数，PE9 彩票出口光眼实际确认吐出 1 张票时累计。
 * - card_out_total：总吐卡数，P1=4时PE9每确认吐出1张卡片就累计1。
 * - return_commitment_total：按出奖模式折算的新奖励累计值，C17用于分析奖励来源；
 *   奖励转屏或兑现不重复增加。V10预算按升级后C1/C2增量计算，此值不再控制亏空状态。
 * - ticket_payout_value_scaled：每张票按出票当时P6折算后的累计价值，单位为百万分之一币。
 * - card_payout_value_scaled：每张卡按吐卡当时P15折算后的累计价值，单位为百万分之一币。
 * - power_on_total：上电次数，Account_Init() 每次启动成功读账后加 1。
 * - game_round_total：游戏局数，按出票键结算一次或重复光眼失败一次都加 1。
 * - lottery_count_total：触发老虎机抽奖的次数，1/9 两侧返奖口每触发一次加 1。
 * - gift_light_total：老虎机实际确认送出的灯数，对应查账C16。
 * - gift_win_value_total：本局含送灯时最终结算的中奖币值，对应查账C17。
 * - game_score/game_prize/game_coin：掉电前左屏、中屏、右屏对应的游戏变量。
 */
typedef struct
{
    uint32_t coin_in_total;
    uint32_t game_use_total;
    uint32_t line_ticket_total;
    uint32_t lottery_payout_total;
    uint32_t payout_total;
    uint32_t ticket_out_total;
    uint32_t card_out_total;
    uint32_t return_commitment_total;
    uint64_t ticket_payout_value_scaled;
    uint64_t card_payout_value_scaled;
    uint32_t power_on_total;
    uint32_t game_round_total;
    uint32_t lottery_count_total;
    uint32_t gift_light_total;
    uint32_t gift_win_value_total;
    /* 新版四本账中的实体收支；旧主账的押分/中奖流水另行保留。 */
    uint32_t physical_coin_in_total;
    uint32_t physical_coin_out_total;
    uint16_t game_score;
    uint16_t game_prize;
    uint16_t game_coin;
    /* 实际成功入奖的3/5/10币分项；独立双槽记录，不由选奖次数推算。 */
    uint32_t lottery_coin3_value_total;
    uint32_t lottery_coin5_value_total;
    uint32_t lottery_coin10_value_total;
} Account_Data;

/**
 * @brief 初始化账目 Service。
 *
 * 调用位置：
 * - Game_Init() 调用一次。
 *
 * 逻辑说明：
 * - 从 EEPROM 的两个备份槽读取账目。
 * - 校验魔数、版本、长度和 CRC，选序号最新的一份。
 * - 如果两份都无效，则从全 0 默认账目开始。
 * - 上电次数 power_on_total 加 1，并马上尝试写回 EEPROM。
 */
void Account_Init(void);
/* 仅新 TF 固件 release 首次启动时调用；清账及玩家未结算余额。 */
uint8_t Account_ResetForUpgrade(void);

/**
 * @brief 账目后台保存任务。
 *
 * 调用位置：
 * - Game_Task() 每轮调用。
 *
 * 逻辑说明：
 * - 业务函数只改 RAM 并置 dirty 标志。
 * - 本函数在数据变化后等待 ACCOUNT_SAVE_DELAY_MS，再合并写一次 EEPROM。
 * - 这样投币、吐币连续发生时不会每个循环都写 EEPROM，能减少擦写次数。
 */
void Account_Task(void);

/**
 * @brief 查询 EEPROM 账目当前是否可写。
 *
 * @return 1 表示 EEPROM 可用；0 表示 EEPROM 未应答或保存失败。
 */
uint8_t Account_IsReady(void);

/**
 * @brief 查询上电时是否读到过有效账目。
 *
 * @return 1 表示读到了有效记录；0 表示使用默认全 0 记录。
 */
uint8_t Account_HasValidRecord(void);

/**
 * @brief 复制当前账目快照。
 *
 * @param data 调用者提供的接收结构体。
 *
 * 逻辑说明：
 * - 外部只能拿到一份拷贝，不能直接改 Account 内部全局变量。
 * - 后续查账页面显示投币总数、吐币总数时使用这个接口。
 */
void Account_GetData(Account_Data *data);

/**
 * @brief 把实际吐币和已经按历史P6折算的出票价值统一换算成整币。
 *
 * @param data 当前账目快照。
 *
 * @return 实际吐币数 + EEPROM中已固化的彩票累计等值币，结果向下取整。
 */
uint64_t Account_CalcPayoutEquivalent(const Account_Data *data);

/* 与C12共用的精确出奖价值，单位百万分之一币；算法不得用已取整的C12反推。 */
uint64_t Account_CalcPayoutValueScaled(const Account_Data *data);
uint32_t Account_GetStakeSinceEpoch(const Account_Data *data);
uint32_t Account_GetAwardSinceEpoch(const Account_Data *data);
uint8_t Account_IsMixedLedger(void);
void Account_AddPhysicalCoinIn(uint32_t count);
void Account_AddPhysicalCoinOut(uint32_t count);

/** @brief 按C4/C3计算C12整数返奖率，不使用当前P6重算历史彩票。 */
uint32_t Account_CalcPayoutRatePercent(const Account_Data *data);

/**
 * @brief 设置当前出票换算使用的P6。
 *
 * 上电迁移旧账时调用一次；以后P6改变由Account_AddTicketOut自动识别。
 */
void Account_SetTicketConversionP6(uint16_t tickets_per_coin);

/**
 * @brief 读取掉电前保存的三组游戏显示值。
 *
 * @param score 左屏 s_game_score。
 * @param prize 中屏 s_game_prize。
 * @param coin  右屏 s_game_coin。
 *
 * 调用位置：
 * - Game_Init() 读取后，等上电自检结束再恢复到数码管显示。
 */
void Account_GetGameState(uint16_t *score, uint16_t *prize, uint16_t *coin);

/**
 * @brief 保存当前三组游戏显示值到 Account RAM。
 *
 * 调用位置：
 * - Game_SetScore()/Game_SetPrize()/Game_SetCoin() 每次改变显示变量后调用。
 *
 * 逻辑说明：
 * - 本函数只更新 RAM 并标记需要保存。
 * - 真正写 EEPROM 由 Account_Task() 合并执行。
 */
void Account_SetGameState(uint16_t score, uint16_t prize, uint16_t coin);

/* 中屏为待转游戏币时置1；与三屏状态在同一条CRC记录内保存。 */
void Account_SetCreditTransfer(uint8_t active);
uint8_t Account_IsCreditTransfer(void);
/* Phase: 0=none, 1=base payout, 2=claim wait, 3=bonus payout.
 * Persisted in the SAME CRC-protected record as middle/right balances. */
void Account_SetJackpotSettlement(uint8_t phase, uint8_t tier, uint8_t had_gift);
uint8_t Account_GetJackpotSettlement(uint8_t *tier, uint8_t *had_gift);

/* 单机接管旧联机WAIT时，原子记录异常档位并解除等待；不动余额、出奖账。
 * 返回掩码bit0/1/2分别表示曾有5/6/7连线彩金需人工核对，不代表金额或次数。 */
void Account_ArchiveOrphanJackpot(uint8_t tier);
uint8_t Account_GetOrphanJackpotMask(void);

/**
 * @brief 读取 EEPROM 中保存的最后一次有效 RTC 时间。
 *
 * @param time 调用者提供的接收结构体。
 */
void Account_GetRtcTime(Account_RtcTime *time);

/**
 * @brief 保存最后一次有效 RTC 时间到 Account RAM，并标记后续写 EEPROM。
 *
 * @param time 要保存的时间；valid 必须为 1 才会作为有效 RTC 备份。
 */
void Account_SetRtcTime(const Account_RtcTime *time);

/**
 * @brief 累加实际押分总数（C3）；仅在成功启动一局后调用。
 *
 * @param count 本局押分的实体币等价值（P4可为2币/局）。
 */
void Account_AddCoinIn(uint32_t count);

/**
 * @brief 累加游戏消耗数。
 *
 * @param count 本次消耗的游戏币数量。
 */
void Account_AddGameUse(uint32_t count);

/**
 * @brief 回退尚未得到1~9落币结果的一次游戏消耗。
 *
 * @param count 需要回退的游戏币数量。
 */
void Account_RemoveGameUse(uint32_t count);

/**
 * @brief 累加连线出票数。
 *
 * @param count 本次由连线得分产生的出票数量。
 */
void Account_AddLineTicket(uint32_t count);

/**
 * @brief 累加老虎机出币数。
 *
 * @param count 本次由幸运抽奖产生的奖励数量。
 */
void Account_AddLotteryPayout(uint32_t count);

/**
 * @brief 累加游戏局数。
 *
 * @param count 本次新增局数。
 *
 * 计数规则：
 * - 玩家按出票键把左屏得分结算到中屏时，加 1 局。
 * - 中间 7 路重复光眼导致失败结束时，加 1 局。
 */
void Account_AddGameRound(uint32_t count);

/**
 * @brief 累加老虎机抽奖次数。
 *
 * @param count 本次新增抽奖次数。
 */
void Account_AddLotteryCount(uint32_t count);

/**
 * @brief 累加老虎机实际确认送出的灯数，对应C14。
 */
void Account_AddGiftLightCount(uint32_t count);

/**
 * @brief 累加“本局含送灯”时最终结算的中奖币值，对应C15。
 *
 * 传入值统一使用名义币值，所以吐币、彩票或卡片模式不会改变C15含义。
 */
void Account_AddGiftWinValue(uint32_t coin_value);

/**
 * @brief 累加已确认的连线、彩金和老虎机出奖值（C2）。
 *
 * @param count 本次开奖总数；实体吐币光眼和循环转屏不要调用。
 */
void Account_AddTotalPayout(uint32_t count);

/**
 * @brief 累加实际出票数。
 *
 * @param count PE9 彩票出口光眼实际确认吐出的票数。
 * @param tickets_per_coin 这些新彩票对应的当前P6。
 *
 * 说明：
 * - 只有P1=2或P1=3时，Game层才调用本接口。
 * - 每确认吐出1张票，C3累计值增加1。
 */
void Account_AddTicketOut(uint32_t count, uint16_t tickets_per_coin);

/**
 * @brief 累加实际吐卡数，并按本次吐卡任务的x币/y卡固化返奖价值。
 *
 * P15以后再修改时，已经累计的卡片价值不会按新比例重算。
 */
void Account_AddCardOut(uint32_t count,
                        uint16_t coins_per_batch,
                        uint16_t cards_per_batch);

/** @brief 保存尚未由PE9确认完成的吐卡任务，供掉电后继续。 */
void Account_SetCardPayoutPending(uint32_t card_count,
                                  uint16_t coins_per_batch,
                                  uint16_t cards_per_batch);

/** @brief 读取尚未完成的吐卡任务。 */
void Account_GetCardPayoutPending(uint32_t *card_count,
                                  uint16_t *coins_per_batch,
                                  uint16_t *cards_per_batch);

/**
 * @brief 累加已承诺返奖等值币数。
 *
 * P1=1、2或4时，每中奖1币累计1；P1=3同时吐币和出票时，每中奖1币累计2。
 */
void Account_AddReturnCommitment(uint32_t count);

/**
 * @brief 清除查账页面使用的累计账目。
 *
 * 调用位置：
 * - Game主循环收到本机或无线P8清账请求，并确认所有出奖任务空闲后调用。
 *
 * 逻辑说明：
 * - 清 C1~C9、C12、C14、C15对应的累计账目；当前玩家分数保持不变。
 * - 不清掉当前左屏/中屏/右屏游戏值，避免玩家当前分数和剩余出奖数被设置菜单误清。
 */
void Account_ClearBookData(void);

/**
 * @brief 立即把当前账目写入 EEPROM。
 *
 * 调用位置：
 * - Account_Init() 增加上电次数后调用一次。
 * - 后续如果做设置页面的“保存并退出”，也可以手动调用。
 *
 * @return 1 表示保存成功；0 表示 EEPROM 不可用或写入失败。
 */
uint8_t Account_SaveNow(void);

#ifdef __cplusplus
}
#endif

#endif
