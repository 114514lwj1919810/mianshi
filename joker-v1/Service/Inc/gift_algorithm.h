/* AUTO-FILE-DOC
 * 文件职责：gift_algorithm 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef GIFT_ALGORITHM_H
#define GIFT_ALGORITHM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    GIFT_ALGORITHM_AWARD_NONE = 0U,
    GIFT_ALGORITHM_AWARD_GIFT_1,
    GIFT_ALGORITHM_AWARD_GIFT_2,
    GIFT_ALGORITHM_AWARD_COIN_5,
    GIFT_ALGORITHM_AWARD_COIN_3,
    GIFT_ALGORITHM_AWARD_GIFT_3,
    GIFT_ALGORITHM_AWARD_COIN_10,
    GIFT_ALGORITHM_AWARD_GIFT_4,
    GIFT_ALGORITHM_AWARD_GIFT_5,
    GIFT_ALGORITHM_AWARD_GIFT_6,
    GIFT_ALGORITHM_AWARD_GIFT_7,                 /* 保留奖型；当前要求已有底灯，最多送6灯，不生成此值 */
    GIFT_ALGORITHM_AWARD_TYPE_COUNT
} GiftAlgorithm_AwardType;

typedef enum
{
    GIFT_ALGORITHM_MISS_TEASE = 0U,
    GIFT_ALGORITHM_MISS_EARLY_MISMATCH = 1U
} GiftAlgorithm_MissStyle;

typedef struct
{
    GiftAlgorithm_AwardType type;
    uint16_t target_mask;
    uint16_t first_target_mask;
    uint16_t second_target_mask;
    uint16_t reserved_cost;
    GiftAlgorithm_MissStyle miss_style;
} GiftAlgorithm_Result;

/* 旧C16~C41算法诊断整体后移到C18~C43；C45是旧主账混合标志。 */
typedef struct
{
    uint32_t award_count[GIFT_ALGORITHM_AWARD_TYPE_COUNT]; /* 下标为奖型；0为空奖 */
    uint32_t guarantee_count;
    uint32_t limited_count;
    int64_t ledger_gap;      /* C3-C4，单位：币 */
    int64_t available;       /* 当前按账本计算的可返额度，单位：币 */
    uint64_t reserved;       /* 当前大奖池余额，单位：币 */
    uint8_t recovery;
    uint8_t effective_percent;
    uint8_t last_reason;     /* 1正账出奖 2负账出奖 3无可支付奖 8保存失败 */
    uint8_t last_drawn;      /* 最近奖型；255表示没有可支付奖 */
    uint8_t miss_streak;
    uint8_t guarantee_threshold; /* 新算法不使用保底，固定为0 */
    uint32_t coin_budget_before_cents; /* C51: 最近选币奖前预算，百分之一币 */
    uint32_t coin_granted;             /* C52: 最近成功批准的币奖，0/3/5/10 */
    uint32_t recovery_stakes;          /* C53: 当前负账段累计押分 */
    uint32_t recovery_funded_cents;    /* C54: 当前负账段累计补池，百分之一币 */
    uint32_t recovery_paid;            /* C55: 当前负账段已扣的小奖 */
    uint32_t rejected_grants;          /* C56: 本次开机被拒绝的异常奖次数 */
} GiftAlgorithm_Diagnostics;

/**
 * @brief  读取新返奖算法诊断数据
 * @param  occupied_mask 当前 2~8 号灯位图；仅保留接口兼容
 * @param  data 输出诊断数据；允许为 NULL
 * @retval None
 */
void GiftAlgorithm_GetDiagnostics(uint16_t occupied_mask, GiftAlgorithm_Diagnostics *data);

/**
 * @brief  初始化负账池、大奖池及 EEPROM 轮转状态
 * @param  无
 * @retval None
 */
void GiftAlgorithm_Init(void);

/* 每次真正押分后立即更新 E 池；电机停转后再配对保存账本与算法。 */
void GiftAlgorithm_OnStake(void);
void GiftAlgorithm_Task(void);
/* 一次开奖仅签发一份币奖许可；Game成功加奖后消耗。排队失败仍可重试。
 * 不二次扣池：预算已在Select签发前扣除并保存。 */
uint8_t GiftAlgorithm_CoinAwardAuthorized(uint16_t coins);
void GiftAlgorithm_CommitCoinAward(uint16_t coins);

/**
 * @brief  按当前正账额度或负账池选择一次 3/5/10 币或 1~6 灯奖励
 * @param  occupied_mask 当前 2~8 号灯位图，寄存器位掩码
 * @param  result 输出开奖结果；允许为 NULL
 * @retval None
 * @note 正账额度=C3×现场返奖率-C4-D；C4按实际派出币数全额扣除。
 * @note 负账期间仅将新押币按降低后的返奖率入池。
 * @note D池达到P16先尝试5/6/7连线，否则按预算尝试10、5、3币普通奖。
 * @note 玩家至少已有一盏2~8号灯时才允许送灯；最多补齐剩余六盏形成七连线。
 * @note 送灯按最终连线完整奖值扣预算，不按灯数或送灯前后奖值差计价。
 * @note P16设置D池实时余额达到多少币开始抽高连线，范围100~500，默认140，步长10。
 * @note D池达标后按90/175、55/175、30/175抽取5/6/7连线，无额外空档。
 * @note 抽奖送成5/6/7连线时，大奖池承担并扣除固定奖与对应彩金的总额；自然形成的连线不扣大奖池。
 */
void GiftAlgorithm_Select(uint16_t occupied_mask,
                          GiftAlgorithm_Result *result);

/**
 * @brief  清账时清除预算、大奖池和诊断状态
 * @param  无
 * @retval 1 保存成功；0 EEPROM 不可用或写入失败
 */
uint8_t GiftAlgorithm_ResetForAccountClear(void);

#ifdef __cplusplus
}
#endif

#endif
