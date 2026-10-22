/* AUTO-FILE-DOC
 * 文件职责：参数设置与持久化。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef SETTING_H
#define SETTING_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SETTING_PARAM_COUNT                         31U

/*
 * P1~P14 参数下标。
 *
 * 数组下标从 0 开始，所以 P1 对应 0，P14 对应 13。
 * 这些宏让 Game/Lottery/Account 代码不用直接写裸数字，后续看代码能直接知道含义。
 */
#define SETTING_PARAM_AWARD_MODE                    0U
#define SETTING_PARAM_SCORE_EXCHANGE_ENABLE         1U
#define SETTING_PARAM_COIN_TO_POINT                 2U
#define SETTING_PARAM_COINS_PER_POINT               3U
#define SETTING_PARAM_LOTTERY_PAYOUT_PERCENT        4U
#define SETTING_PARAM_TICKETS_PER_COIN              5U
#define SETTING_PARAM_LOTTERY_DIFFICULTY            6U
#define SETTING_PARAM_CLEAR_ACCOUNT                 7U
#define SETTING_PARAM_FACTORY_RESET                 8U
#define SETTING_PARAM_SOLENOID_EYE_CHECK            9U
#define SETTING_PARAM_AMBIENT_LED_FLASH             10U
#define SETTING_PARAM_DROP_INTERVAL_TENTH_SECOND    11U
#define SETTING_PARAM_VOLUME                        12U
#define SETTING_PARAM_JACKPOT_BASE                  13U
#define SETTING_PARAM_CARD_RATIO                    14U
#define SETTING_PARAM_STANDBY_SOUND_ENABLE          15U
#define SETTING_PARAM_JACKPOT_COINS_PER_POINT       16U
/* 内部下标17/18分别为现场P16大奖触发门槛、P17每押多少币转入D池1币。 */
#define SETTING_PARAM_MAJOR_DRAW_COINS                17U
#define SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL       18U
#define SETTING_PARAM_RS485_ADDRESS                   28U
/* 现场菜单P14：1中奖后按原流程直接下发；0先把左屏得分转成右屏游戏币。 */
#define SETTING_PARAM_IMMEDIATE_AWARD                 29U
/* 现场P15：赤字阶段目标降低0~100个百分点；有效返奖率最低为0。 */
#define SETTING_PARAM_RECOVERY_DROP                   30U
#define SETTING_RECOVERY_DROP_DEFAULT                 15U
#define SETTING_RECOVERY_DROP_MAX                     100U
#define SETTING_MAJOR_DRAW_COINS_MIN                 100U
#define SETTING_MAJOR_DRAW_COINS_MAX                 500U
#define SETTING_MAJOR_DRAW_COINS_DEFAULT             140U
#define SETTING_MAJOR_DRAW_COINS_STEP                 10U
#define SETTING_MAJOR_POOL_STAKE_MIN                  10U
#define SETTING_MAJOR_POOL_STAKE_MAX                  200U
#define SETTING_MAJOR_POOL_STAKE_DEFAULT              30U
/* P15在EEPROM内部编码为x*100+y，例如2币1卡保存为201。无线协议分字段传输。 */
#define SETTING_CARD_RATIO_FACTOR                   100U
#define SETTING_CARD_RATIO_MIN                      1U
#define SETTING_CARD_RATIO_MAX                      99U
#define SETTING_CARD_COINS_DEFAULT                  10U
#define SETTING_CARD_COUNT_DEFAULT                  1U

/* P17：累计多少实际游戏押分让公共彩金增加1分，仅允许10的倍数。 */
#define SETTING_JACKPOT_COINS_PER_POINT_MIN         10U
#define SETTING_JACKPOT_COINS_PER_POINT_MAX         990U
#define SETTING_JACKPOT_COINS_PER_POINT_STEP        10U
#define SETTING_JACKPOT_COINS_PER_POINT_DEFAULT     100U

/*
 * P6 按 P1 模式分为两个互斥区间：
 * - P1=2（只吐票）：24~50 张/币，默认35。
 * - P1=3（吐币并连带吐票）：1~10 张/币，默认10。
 */
#define SETTING_COMBINED_TICKETS_MIN                1U
#define SETTING_COMBINED_TICKETS_MAX                10U
#define SETTING_COMBINED_TICKETS_DEFAULT            10U
#define SETTING_TICKET_ONLY_TICKETS_MIN             24U
#define SETTING_TICKET_ONLY_TICKETS_MAX             50U
#define SETTING_TICKET_ONLY_TICKETS_DEFAULT         35U

#define SETTING_LOTTERY_DIFFICULTY_MIN               1U
#define SETTING_LOTTERY_DIFFICULTY_DEFAULT           2U
#define SETTING_LOTTERY_DIFFICULTY_MAX               5U
/*
 * P1 出奖模式。
 * - 1：出币模式。
 * - 2：出彩票模式。
 * - 3：既出币又出彩票。
 * - 4：吐卡片模式，中奖币值先存入中屏，按出票键后按P15换算并吐卡。
 */
#define SETTING_AWARD_MODE_COIN                     1U
#define SETTING_AWARD_MODE_TICKET                   2U
#define SETTING_AWARD_MODE_COIN_AND_TICKET          3U
#define SETTING_AWARD_MODE_CARD                     4U

/*
 * P10 电磁铁检测。
 * - 0：没有电磁铁检测光眼。
 * - 1：开启电磁铁光眼检测。
 * - 2：关闭电磁铁光眼检测。
 */
#define SETTING_SOLENOID_EYE_NONE                   0U
#define SETTING_SOLENOID_EYE_ENABLE                 1U
#define SETTING_SOLENOID_EYE_DISABLE                2U

/**
 * @brief 初始化设置 Service。
 *
 * 调用位置：
 * - Game_Init() 中，在 Account_Init() 后、Lottery_Init() 前调用一次。
 *
 * 逻辑说明：
 * - 先把 P1~P17 加载成默认值：
 *   1, 1, 1, 1, 95, 35, 2, 0, 0, 2, 1, 25, 15, 100, 2币1卡, 1, 100。
 * - 再从 EEPROM 的设置区读取保存记录。
 * - 如果魔数、版本、长度、校验都正确，就用 EEPROM 里的值覆盖默认值。
 * - 如果 EEPROM 没有有效记录，就把默认值写入 EEPROM，后续上电可直接读取。
 */
void Setting_Init(void);

/**
 * @brief 判断设置区最近一次 EEPROM 读取/保存是否正常。
 *
 * @return 1 表示 EEPROM 设置区可用；0 表示未读到或未写入有效记录。
 */
uint8_t Setting_IsReady(void);

/**
 * @brief 读取某个 P 参数当前值。
 *
 * @param index 参数下标，0 表示 P1，12 表示 P13。
 *
 * @return 参数值；下标越界时返回 0。
 */
uint16_t Setting_GetValue(uint8_t index);

/**
 * @brief 修改某个 P 参数并立即保存到 EEPROM。
 *
 * @param index 参数下标，0 表示 P1，12 表示 P13。
 * @param value 新参数值，当前显示层按 0~9999 使用。
 *
 * 逻辑说明：
 * - 本函数是设置页面“右屏数值被选中后按上下键”的保存入口。
 * - 值变化后马上写 EEPROM，这样断电后不会丢失设置。
 */
void Setting_SetValue(uint8_t index, uint16_t value);

/**
 * @brief 按产品表格允许值调整某个 P 参数。
 *
 * @param index    参数下标，0 表示 P1，12 表示 P13。
 * @param increase 1 表示向上调；0 表示向下调。
 *
 * @return 调整后的参数值；下标越界时返回 0。
 *
 * 逻辑说明：
 * - P1 只能在 1/2/3/4 之间切换。
 * - P2/P8/P9/P11 只能在 0/1 之间切换。
 * - P10 只能在 0/1/2 之间切换。
 * - P3/P4、P5、P7、P12、P13、P14、P17 按各自范围上下限限制。
 * - P7 是预留的综合返奖率五档，只允许 1~5，计划1档最高、5档最低；当前不参与开奖算法。
 * - P1=2时P6只能在24~50调整；P1=3时P6只能在1~10调整。
 * - 切换P1导致当前P6区间不匹配时，自动改为只吐票默认35或币票默认10。
 * - P1=1/4时P6可在两段之间调整，10向上跳到24，24向下跳到10。
 */
uint16_t Setting_AdjustValue(uint8_t index, uint8_t increase);

/**
 * @brief 判断 P1 是否允许实际出币。
 *
 * @return 1 表示 P1=1 或 P1=3，需要驱动吐币流程；0 表示彩票模式不出币。
 */
uint8_t Setting_IsCoinPayoutEnabled(void);

/**
 * @brief 判断 P1 是否允许出彩票。
 *
 * @return 1 表示 P1=2 或 P1=3；0 表示当前不是彩票输出模式。
 */
uint8_t Setting_IsTicketPayoutEnabled(void);

/** @brief 判断P1是否为吐卡片模式。 */
uint8_t Setting_IsCardPayoutEnabled(void);

/**
 * @brief 判断 P2 是否允许按出票/兑换键把左屏得分兑换出去。
 *
 * @return 1 表示允许；0 表示锁住出票/兑换得分功能。
 */
uint8_t Setting_IsScoreExchangeEnabled(void);

/** @brief P14奖励方式：1即玩即吐；0奖励暂存，先转成右屏游戏币。 */
uint8_t Setting_IsImmediateAwardEnabled(void);

uint16_t Setting_GetCoinToPoint(void);
uint16_t Setting_GetCoinsPerPoint(void);
uint16_t Setting_GetTicketsPerCoin(void);
/** @brief 获取P13中屏背景音乐芯片音量，范围0~30。 */
uint16_t Setting_GetBackgroundVolume(void);
/** @brief 获取P13右屏音效芯片音量，范围0~30。 */
uint16_t Setting_GetEffectVolume(void);
/**
 * @brief 单独调整P13中屏背景音乐音量并立即保存。
 * @param increase 1增加；0减少。
 */
void Setting_AdjustBackgroundVolume(uint8_t increase);
/**
 * @brief 同时设置P13中屏背景音乐音量和右屏音效音量，只保存一次EEPROM。
 */
void Setting_SetVolumePair(uint16_t background_volume, uint16_t effect_volume);
/** @brief 获取P15中间屏的x币，范围1~99。 */
uint16_t Setting_GetCardCoinsPerBatch(void);
/** @brief 获取P15右屏的y卡，范围1~99。 */
uint16_t Setting_GetCardsPerBatch(void);
/**
 * @brief 单独调整P15的x币或y卡并立即保存。
 * @param adjust_cards 0调整x币；1调整y卡。
 * @param increase 1增加；0减少。
 */
void Setting_AdjustCardRatio(uint8_t adjust_cards, uint8_t increase);
/**
 * @brief 同时设置P15中屏x币和右屏y卡，只保存一次EEPROM。
 */
void Setting_SetCardRatioPair(uint16_t coins_per_batch, uint16_t cards_per_batch);
/**
 * @brief  获取内部P7（现场P5）的预留综合返奖率档位
 * @param  无
 * @retval 1~5；计划1档综合返奖率最高、5档最低，当前仅保存和显示
 */
uint8_t Setting_GetLotteryDifficulty(void);
/** @brief 返回当前P6每币出票数；保留该接口供旧模块兼容。 */
uint16_t Setting_GetLineTicketRatio(void);
uint16_t Setting_GetJackpotBase(void);
/** @brief 获取P17彩金贡献门槛；每累计该数量实际游戏押分，公共彩金增加1分。 */
uint16_t Setting_GetJackpotCoinsPerPoint(void);

/** P29中屏：0单机、1联机。 */
uint8_t Setting_IsNetworkEnabled(void);
/** P29右屏：机台号0~7。 */
uint8_t Setting_GetMachineId(void);
/** 同时保存P29的单机/联机状态和0~7机台号。 */
void Setting_SetNetworkConfig(uint8_t network_enabled, uint8_t machine_id);

uint8_t Setting_IsSolenoidEyeCheckEnabled(void);
uint8_t Setting_IsAmbientLedFlashEnabled(uint16_t award_count);
/** @brief P16待机音效开关：0只保留背景音乐，1播放待机连线/送灯/彩金音效；不影响正常游戏。 */
uint8_t Setting_IsStandbySoundEnabled(void);

/**
 * @brief 恢复 P1~P17 为出厂默认值并写入 EEPROM。
 *
 * 调用位置：
 * - 设置页面 P9 调成 1 后退出设置时调用。
 */
void Setting_RestoreDefault(void);

/**
 * @brief 立即把当前 P1~P17 写入 EEPROM。
 *
 * @return 1 表示写入成功；0 表示 EEPROM 不可用或写入失败。
 */
uint8_t Setting_SaveNow(void);

#ifdef __cplusplus
}
#endif

#endif
