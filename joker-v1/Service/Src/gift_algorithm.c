/*
 * 元气小丑返奖算法。
 *
 * 正账可返额度按 C3×现场返奖率-C4-D 计算，不另存普通抽奖余额；
 * 已派奖励由C4按实际币数全额扣除，不再乘一次返奖率后重复进入预算。
 * 负账时每次成功押分立即按降低后的返奖率补入独立 E 池，
 * C3-C4 回到非负时清空 E 池。普通币奖在预算内按10、5、3币顺序选择。
 * 正账时每累计P17个押币，从普通预算C22转1币入大奖池D；
 * 负账押分不累计D池进度，普通预算不足1币时延后转入。D池达到P16即抽取
 * 5/6/7连线，概率分别为90/175、55/175、30/175，不另设空档。池内余额仍必须
 * 覆盖固定奖和对应彩金总额；只有实际派出高连线奖励时才从大奖池扣除真实派币数。
 * 自然形成的5/6/7连线不经过本算法，也不扣大奖池。
 */
#include "gift_algorithm.h"

#include "account.h"
#include "eeprom.h"
#include "gtim.h"
#include "jackpot.h"
#include "score.h"
#include "setting.h"

#include <stddef.h>
#include <string.h>

#define GIFT_ALGORITHM_MIDDLE_MASK              ((uint16_t)0x00FEU)
#define GIFT_ALGORITHM_BUDGET_SCALE             ACCOUNT_PAYOUT_VALUE_SCALE
#define GIFT_ALGORITHM_CANDIDATE_MAX            127U
#define GIFT_ALGORITHM_MAJOR_DRAW_TOTAL         175U  /* 大奖池高连线抽取总权重 */
#define GIFT_ALGORITHM_MAJOR_LINE5_WEIGHT        90U  /* 5连线抽取权重 */
#define GIFT_ALGORITHM_MAJOR_LINE6_WEIGHT        55U  /* 6连线抽取权重 */
#define GIFT_ALGORITHM_MAJOR_LINE7_WEIGHT        30U  /* 7连线抽取权重 */
#define GIFT_ALGORITHM_MAJOR_STATE_MARKER       0xA5U /* 新大奖池抽取状态布局标记 */

#define GIFT_ALGORITHM_STATE_BASE_ADDR          0x0400U
#define GIFT_ALGORITHM_STATE_SLOT_SIZE          128U
#define GIFT_ALGORITHM_STATE_SLOT_COUNT         32U
#define GIFT_ALGORITHM_STATE_MAGIC              0x31414647UL
#define GIFT_ALGORITHM_STATE_VERSION            15U
#define GIFT_ALGORITHM_PREVIOUS_STATE_VERSION   14U

#pragma pack(push, 1)
typedef struct
{
    uint32_t magic;                              /* 记录魔数，无量纲 */
    uint16_t version;                            /* 算法记录版本，无量纲 */
    uint16_t length;                             /* 记录长度，单位：字节 */
    uint32_t sequence;                           /* 轮转保存序号，无量纲 */
    uint32_t random_state;                       /* 随机状态，无量纲 */
    uint32_t last_stake;                         /* 负账池已计入的 C3，单位：币 */
    uint64_t recovery_pool_scaled;               /* 负账池余额，单位：百万分之一币 */
    uint32_t major_pool;                         /* 大奖池余额，单位：币 */
    uint32_t reserved_major_source;              /* 已废弃的旧大奖池来源字段 */
    uint32_t award_count[GIFT_ALGORITHM_AWARD_TYPE_COUNT]; /* 各奖型次数，单位：次 */
    uint32_t limited_count;                      /* 因预算不足空奖次数，单位：次 */
    uint8_t last_reason;                         /* 最近原因码，无量纲 */
    uint8_t last_drawn;                          /* 最近奖型，无量纲 */
    uint8_t recovery;                            /* C3-C4 是否为负，无量纲 */
    uint8_t effective_percent;                   /* 本次预算比例，单位：% */
    uint16_t reserved_major_word0;               /* 保留字段，保持记录长度 */
    uint16_t reserved_major_word1;               /* 保留字段，保持记录长度 */
    uint32_t major_draw_pending;                 /* 满P17但C22预算不足、待转入D池的币数 */
    uint16_t major_draw_remainder;               /* 尚未满P17的正账押分余数 */
    uint8_t major_draw_marker;                   /* 新大奖池抽取状态布局标记，无量纲 */
    uint64_t recovery_funded_scaled;             /* 当前负账段累计入池 */
    uint32_t recovery_paid;                      /* 当前负账段已扣小奖 */
    uint32_t recovery_begin_stake;               /* 当前负账段开始时总押分 */
    uint32_t recovery_stakes;                    /* 当前负账段已计入押分 */
    uint8_t reserved[5];                         /* 保持128字节旧布局大小 */
    uint32_t crc32;                              /* 记录 CRC32，无量纲 */
} GiftAlgorithm_StateRecord;
#pragma pack(pop)

typedef char GiftAlgorithm_StateRecordSizeCheck[
    (sizeof(GiftAlgorithm_StateRecord) == GIFT_ALGORITHM_STATE_SLOT_SIZE) ? 1 : -1];

typedef struct
{
    uint16_t target_mask;                        /* 目标灯位图，寄存器位掩码 */
    uint16_t first_mask;                         /* 首次揭晓灯位图，寄存器位掩码 */
    uint16_t second_mask;                        /* 第二次揭晓灯位图，寄存器位掩码 */
    uint16_t budget_cost;                        /* 普通预算成本，单位：币 */
    uint16_t major_cost;                         /* 抽奖送出高连线的固定奖与彩金总成本，单位：币 */
    uint16_t total_award;                        /* 连线与彩金合计，单位：币 */
    uint8_t gift_count;                          /* 送灯数量，单位：盏 */
    uint8_t tier;                                /* 最终连线档位，5~7 或 0 */
} GiftAlgorithm_Candidate;

static uint32_t s_random_state = 0x6D2B79F5UL;   /* 随机状态，无量纲 */
static uint32_t s_state_sequence;                /* 当前保存序号，无量纲 */
static uint8_t s_state_next_slot;                /* 下一保存槽，数组下标 */
static uint32_t s_last_stake;                    /* 负账池已计入的 C3，单位：币 */
static uint64_t s_recovery_pool_scaled;          /* 负账池余额，单位：百万分之一币 */
static uint32_t s_major_pool;                    /* 大奖池余额，单位：币 */
static uint32_t s_major_draw_pending;            /* 待从C22转入D池的币数 */
static uint16_t s_major_draw_remainder;          /* 正账押分对P17取余 */
static uint8_t s_recovery;                       /* C3-C4 为负标志，无量纲 */
static uint8_t s_effective_percent;              /* 当前比例，单位：% */
static uint8_t s_budget_dirty;                   /* 本轮押分预算尚未配对持久化 */
static uint64_t s_recovery_funded_scaled;
static uint32_t s_recovery_paid;
static uint32_t s_recovery_begin_stake;
static uint32_t s_recovery_stakes;
static uint16_t s_coin_grant;                    /* 已扣预算、尚未入账的一次性许可 */
static uint16_t s_coin_grant_inverse;
static GiftAlgorithm_Diagnostics s_diagnostics; /* 查账诊断快照，无量纲 */

/**
 * @brief  计算返奖状态记录的 CRC32
 * @param  data 输入数据；不得为 NULL
 * @param  length 数据长度，范围：0~65535，单位：字节
 * @retval CRC32 校验值
 */
static uint32_t GiftAlgorithm_Crc32(const void *data, uint16_t length)
{
    const uint8_t *bytes = (const uint8_t *)data; // 输入字节序列，单位：字节
    uint32_t crc = 0xFFFFFFFFUL;                  // CRC32 中间值，无量纲
    uint16_t index;                              // 字节索引，数组下标
    uint8_t bit;                                 // 位索引，范围：0~7

    for (index = 0U; index < length; index++)
    {
        crc ^= bytes[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = ((crc & 1U) != 0U) ?
                  ((crc >> 1U) ^ 0xEDB88320UL) : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFUL;
}

/**
 * @brief  生成下一随机数
 * @param  无
 * @retval 1~0xFFFFFFFF 的伪随机值
 */
static uint32_t GiftAlgorithm_NextRandom(void)
{
    uint32_t value = s_random_state;              // 当前随机值，无量纲

    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    if (value == 0U)
    {
        value = Gtim_NowMs() | 1U;
    }
    s_random_state = value;
    return value;
}

/**
 * @brief  生成指定上限内的随机数
 * @param  limit 上限，范围：1~0xFFFFFFFF，无量纲
 * @retval 0~limit-1；limit 为 0 时返回 0
 */
static uint32_t GiftAlgorithm_RandomBelow(uint32_t limit)
{
    return (limit == 0U) ? 0U : (GiftAlgorithm_NextRandom() % limit);
}

/**
 * @brief  读取当前正账或负账返奖率
 * @param  recovery 0 使用现场返奖率；1 再减去回收降低百分点
 * @retval 0~100，单位：%
 */
static uint8_t GiftAlgorithm_GetConfiguredPercent(uint8_t recovery)
{
    uint16_t percent = Setting_GetValue(             // 现场返奖率，单位：%
        SETTING_PARAM_LOTTERY_PAYOUT_PERCENT);
    uint16_t drop = Setting_GetValue(                // 负账降低值，单位：百分点
        SETTING_PARAM_RECOVERY_DROP);

    if (percent > 100U)
    {
        percent = 100U;
    }
    if (recovery != 0U)
    {
        percent = (drop < percent) ? (uint16_t)(percent - drop) : 0U;
    }
    return (uint8_t)percent;
}

/**
 * @brief  统计位图中的置位数量
 * @param  mask 输入位图，寄存器位掩码
 * @retval 置位数量，范围：0~16
 */
static uint8_t GiftAlgorithm_CountBits(uint16_t mask)
{
    uint8_t count = 0U;                           // 置位数量，单位：位

    while (mask != 0U)
    {
        count = (uint8_t)(count + (uint8_t)(mask & 1U));
        mask >>= 1U;
    }
    return count;
}

/**
 * @brief  从位图中随机取一个灯位
 * @param  mask 输入灯位图；至少包含一个有效位
 * @retval 单灯位图；mask 为 0 时返回 0
 */
static uint16_t GiftAlgorithm_PickBit(uint16_t mask)
{
    uint8_t wanted;                               // 目标置位序号，数组下标
    uint8_t seen = 0U;                            // 已扫描置位数，单位：位
    uint8_t channel;                              // 灯通道，范围：1~7
    uint8_t count = GiftAlgorithm_CountBits(mask); // 可选灯数，单位：盏

    if (count == 0U)
    {
        return 0U;
    }
    wanted = (uint8_t)GiftAlgorithm_RandomBelow(count);
    for (channel = 1U; channel <= 7U; channel++)
    {
        uint16_t bit = (uint16_t)(1U << channel); // 当前灯位，寄存器位掩码

        if ((mask & bit) == 0U)
        {
            continue;
        }
        if (seen == wanted)
        {
            return bit;
        }
        seen++;
    }
    return 0U;
}

/**
 * @brief  饱和增加 32 位计数
 * @param  value 输入输出计数；不得为 NULL
 * @param  add 增量，范围：0~0xFFFFFFFF，无量纲
 * @retval None
 */
static void GiftAlgorithm_AddSaturate(uint32_t *value, uint32_t add)
{
    if ((0xFFFFFFFFUL - *value) < add)
    {
        *value = 0xFFFFFFFFUL;
    }
    else
    {
        *value += add;
    }
}

/**
 * @brief  判断轮转序号是否更新
 * @param  candidate 候选序号，无量纲
 * @param  current 当前序号，无量纲
 * @retval 1 候选更新；0 候选不更新
 */
static uint8_t GiftAlgorithm_IsNewer(uint32_t candidate, uint32_t current)
{
    return ((int32_t)(candidate - current) > 0) ? 1U : 0U;
}

/**
 * @brief  校验 EEPROM 中的返奖状态记录
 * @param  record 输入记录；不得为 NULL
 * @retval 1 有效；0 无效
 */
static uint8_t GiftAlgorithm_StateValid(const GiftAlgorithm_StateRecord *record)
{
    if ((record->magic != GIFT_ALGORITHM_STATE_MAGIC) ||
        ((record->version != GIFT_ALGORITHM_STATE_VERSION) &&
         (record->version != GIFT_ALGORITHM_PREVIOUS_STATE_VERSION)) ||
        (record->length != sizeof(GiftAlgorithm_StateRecord)) ||
        (record->recovery > 1U) ||
        (record->effective_percent > 100U) ||
        (record->major_draw_marker != GIFT_ALGORITHM_MAJOR_STATE_MARKER) ||
        (record->major_draw_remainder >= SETTING_MAJOR_POOL_STAKE_MAX))
    {
        return 0U;
    }
    if (record->version == GIFT_ALGORITHM_STATE_VERSION)
    {
        uint64_t paid = (uint64_t)record->recovery_paid * GIFT_ALGORITHM_BUDGET_SCALE;
        if ((record->last_stake < record->recovery_begin_stake) ||
            (record->last_stake - record->recovery_begin_stake != record->recovery_stakes) ||
            (record->recovery_funded_scaled >
                (uint64_t)record->recovery_stakes * GIFT_ALGORITHM_BUDGET_SCALE) ||
            (paid > record->recovery_funded_scaled) ||
            (record->recovery_pool_scaled != record->recovery_funded_scaled - paid)) return 0U;
    }
    return (record->crc32 == GiftAlgorithm_Crc32(
        record, (uint16_t)offsetof(GiftAlgorithm_StateRecord, crc32))) ? 1U : 0U;
}

/**
 * @brief  把有效记录载入运行状态
 * @param  record 输入记录；不得为 NULL
 * @retval None
 */
static void GiftAlgorithm_ApplyState(const GiftAlgorithm_StateRecord *record)
{
    s_random_state = record->random_state;
    s_state_sequence = record->sequence;
    s_last_stake = record->last_stake;
    s_recovery_pool_scaled = record->recovery_pool_scaled;
    if (record->version == GIFT_ALGORITHM_STATE_VERSION)
    {
        s_recovery_funded_scaled = record->recovery_funded_scaled;
        s_recovery_paid = record->recovery_paid;
        s_recovery_begin_stake = record->recovery_begin_stake;
        s_recovery_stakes = record->recovery_stakes;
    }
    else
    {
        /* V14没有累计入池/扣池证据，不能把未知旧余额当作新许可。
         * 仅重建E池；普通账本和D池不受此迁移影响。 */
        s_recovery_pool_scaled = 0U;
        s_recovery_funded_scaled = 0U;
        s_recovery_paid = 0U;
        s_recovery_begin_stake = record->last_stake;
        s_recovery_stakes = 0U;
    }
    s_major_pool = record->major_pool;
    s_major_draw_pending = record->major_draw_pending;
    s_major_draw_remainder = record->major_draw_remainder;
    s_recovery = record->recovery;
    s_effective_percent = record->effective_percent;
    memcpy(s_diagnostics.award_count, record->award_count,
           sizeof(record->award_count));
    s_diagnostics.guarantee_count = 0U;
    s_diagnostics.limited_count = record->limited_count;
    s_diagnostics.last_reason = record->last_reason;
    s_diagnostics.last_drawn = record->last_drawn;
}

/**
 * @brief  从 EEPROM 轮转区读取最新返奖状态
 * @param  无
 * @retval 1 读取成功；0 没有有效记录
 */
static uint8_t GiftAlgorithm_LoadState(void)
{
    GiftAlgorithm_StateRecord record;             // 当前槽记录，无量纲
    GiftAlgorithm_StateRecord selected;           // 最新有效记录，无量纲
    uint8_t found = 0U;                            // 已找到有效记录标志，无量纲
    uint8_t slot;                                  // EEPROM 槽索引，数组下标

    if (Eeprom_IsReady() == 0U)
    {
        return 0U;
    }
    for (slot = 0U; slot < GIFT_ALGORITHM_STATE_SLOT_COUNT; slot++)
    {
        uint16_t address = (uint16_t)(GIFT_ALGORITHM_STATE_BASE_ADDR +
            (uint16_t)slot * GIFT_ALGORITHM_STATE_SLOT_SIZE); // 槽地址，单位：字节

        if ((Eeprom_Read(address, (uint8_t *)&record,
                         (uint16_t)sizeof(record)) == 0U) ||
            (GiftAlgorithm_StateValid(&record) == 0U))
        {
            continue;
        }
        if ((found == 0U) ||
            (GiftAlgorithm_IsNewer(record.sequence, selected.sequence) != 0U))
        {
            selected = record;
            s_state_next_slot = (uint8_t)((slot + 1U) %
                                          GIFT_ALGORITHM_STATE_SLOT_COUNT);
            found = 1U;
        }
    }
    if (found != 0U)
    {
        GiftAlgorithm_ApplyState(&selected);
    }
    return found;
}

/**
 * @brief  把当前返奖状态写入下一 EEPROM 轮转槽
 * @param  无
 * @retval 1 保存成功；0 EEPROM 不可用或写入失败
 */
static uint8_t GiftAlgorithm_SaveState(void)
{
    GiftAlgorithm_StateRecord record;             // 待写记录，无量纲
    uint16_t address;                              // EEPROM 写地址，单位：字节

    if (Eeprom_IsReady() == 0U)
    {
        return 0U;
    }
    memset(&record, 0, sizeof(record));
    record.magic = GIFT_ALGORITHM_STATE_MAGIC;
    record.version = GIFT_ALGORITHM_STATE_VERSION;
    record.length = sizeof(record);
    record.sequence = s_state_sequence + 1U;
    record.random_state = s_random_state;
    record.last_stake = s_last_stake;
    record.recovery_pool_scaled = s_recovery_pool_scaled;
    record.recovery_funded_scaled = s_recovery_funded_scaled;
    record.recovery_paid = s_recovery_paid;
    record.recovery_begin_stake = s_recovery_begin_stake;
    record.recovery_stakes = s_recovery_stakes;
    record.major_pool = s_major_pool;
    record.major_draw_pending = s_major_draw_pending;
    record.major_draw_remainder = s_major_draw_remainder;
    record.major_draw_marker = GIFT_ALGORITHM_MAJOR_STATE_MARKER;
    memcpy(record.award_count, s_diagnostics.award_count,
           sizeof(record.award_count));
    record.limited_count = s_diagnostics.limited_count;
    record.last_reason = s_diagnostics.last_reason;
    record.last_drawn = s_diagnostics.last_drawn;
    record.recovery = s_recovery;
    record.effective_percent = s_effective_percent;
    record.crc32 = GiftAlgorithm_Crc32(
        &record, (uint16_t)offsetof(GiftAlgorithm_StateRecord, crc32));
    address = (uint16_t)(GIFT_ALGORITHM_STATE_BASE_ADDR +
        (uint16_t)s_state_next_slot * GIFT_ALGORITHM_STATE_SLOT_SIZE);
    if (Eeprom_Write(address, (const uint8_t *)&record,
                     (uint16_t)sizeof(record)) == 0U)
    {
        return 0U;
    }
    s_state_sequence = record.sequence;
    s_state_next_slot = (uint8_t)((s_state_next_slot + 1U) %
                                  GIFT_ALGORITHM_STATE_SLOT_COUNT);
    return 1U;
}

/**
 * @brief  取得升级后 C3-C4 的有符号余额
 * @param  account 输入账本快照；不得为 NULL
 * @retval 正数表示机器当前赢币，负数表示玩家当前赢币，单位：币
 */
static int64_t GiftAlgorithm_GetBalance(const Account_Data *account)
{
    return (int64_t)Account_GetStakeSinceEpoch(account) -
           (int64_t)Account_GetAwardSinceEpoch(account);
}

/* E池只由有据可查的累计补入减累计扣款产生，不能只相信可变余额。
 * 总补入不能超过本负账段实际押分，扣款不能超过总补入。异常时拒绝使用。 */
static uint64_t GiftAlgorithm_RecoveryAvailable(void)
{
    uint64_t paid = (uint64_t)s_recovery_paid * GIFT_ALGORITHM_BUDGET_SCALE;
    if ((s_recovery_funded_scaled >
         (uint64_t)s_recovery_stakes * GIFT_ALGORITHM_BUDGET_SCALE) ||
        (paid > s_recovery_funded_scaled)) return 0U;
    return s_recovery_funded_scaled - paid;
}

static void GiftAlgorithm_ResetRecovery(uint32_t begin_stake)
{
    s_recovery_pool_scaled = 0U;
    s_recovery_funded_scaled = 0U;
    s_recovery_paid = 0U;
    s_recovery_begin_stake = begin_stake;
    s_recovery_stakes = 0U;
}

/**
 * @brief  同步正负账状态和负账池
 * @param  account 输入账本快照；不得为 NULL
 * @retval 自上次同步起新增的押分，单位：币
 */
static uint32_t GiftAlgorithm_UpdateBudget(const Account_Data *account)
{
    uint32_t stake = Account_GetStakeSinceEpoch(account); // 当前 C3，单位：币
    uint32_t delta = 0U;                           // 本次新增押分，单位：币
    int64_t balance = GiftAlgorithm_GetBalance(account); // C3-C4，单位：币

    if (stake < s_last_stake)
    {
        GiftAlgorithm_ResetRecovery(stake);
        s_major_pool = 0U;
        s_major_draw_pending = 0U;
        s_major_draw_remainder = 0U;
        s_recovery = 0U;
        s_last_stake = stake;
    }

    delta = stake - s_last_stake;

    if (balance < 0)
    {
        if (s_recovery == 0U)
        {
            s_recovery = 1U;
            GiftAlgorithm_ResetRecovery(s_last_stake);
        }
        /* 亏空期间既不存D池，也不把此前的零头留到恢复正账后补存。 */
        s_major_draw_pending = 0U;
        s_major_draw_remainder = 0U;
        s_effective_percent = GiftAlgorithm_GetConfiguredPercent(1U);
        /* 用独立的本段基准去重；抽奖、菜单查询或重复OnStake均不能重复补池。 */
        if ((stake < s_recovery_begin_stake) ||
            (stake - s_recovery_begin_stake < s_recovery_stakes))
            GiftAlgorithm_ResetRecovery(stake);
        s_recovery_funded_scaled +=
            (uint64_t)(stake - s_recovery_begin_stake - s_recovery_stakes) * s_effective_percent *
            (GIFT_ALGORITHM_BUDGET_SCALE / 100U);
        s_recovery_stakes = stake - s_recovery_begin_stake;
        s_recovery_pool_scaled = GiftAlgorithm_RecoveryAvailable();
        s_last_stake = stake;
    }
    else
    {
        s_recovery = 0U;
        GiftAlgorithm_ResetRecovery(stake);
        s_last_stake = stake;
    }
    s_effective_percent = GiftAlgorithm_GetConfiguredPercent(s_recovery);

    return delta;
}

/* 下方定义在OnStake之后；这里仅用于调用，不在每次抽奖时赠送D池额度。 */
static void GiftAlgorithm_FundMajorPool(const Account_Data *account,
                                        uint32_t new_stake);

/** 每次成功扣押分后立即刷新 E 池及正账D池押分进度。 */
void GiftAlgorithm_OnStake(void)
{
    Account_Data account;
    uint32_t new_stake;

    Account_GetData(&account);
    new_stake = GiftAlgorithm_UpdateBudget(&account);
    GiftAlgorithm_FundMajorPool(&account, new_stake);
    /* 账本先于算法状态写入 EEPROM，避免重启后 last_stake 超前。 */
    s_budget_dirty = 1U;
}

/** 非落币阶段配对保存本轮押分账本和 E 池。 */
void GiftAlgorithm_Task(void)
{
    if (s_budget_dirty == 0U)
    {
        return;
    }
    if (Account_SaveNow() == 0U)
    {
        return;
    }
    if (GiftAlgorithm_SaveState() != 0U)
    {
        s_budget_dirty = 0U;
    }
}

/**
 * @brief  按C3×返奖率-C4-D计算当前普通3/5/10币奖额度
 * @param  account 输入账本快照；不得为 NULL
 * @retval 可返额度，单位：百万分之一币
 */
static uint64_t GiftAlgorithm_GetAvailableScaled(const Account_Data *account)
{
    int64_t balance = GiftAlgorithm_GetBalance(account); // C3-C4，单位：币
    uint8_t percent = GiftAlgorithm_GetConfiguredPercent(0U); // 正账返奖率，单位：%
    uint32_t stake = Account_GetStakeSinceEpoch(account); // 当前C3，单位：币
    uint32_t award = Account_GetAwardSinceEpoch(account); // 当前C4，单位：币
    uint64_t earned_scaled;                      // C3按P3形成的累计预算，单位：百万分之一币
    uint64_t committed_scaled;                   // C4与D已占用预算，单位：百万分之一币

    if (balance < 0)
    {
        return GiftAlgorithm_RecoveryAvailable();
    }
    earned_scaled = (uint64_t)stake * percent *
        (GIFT_ALGORITHM_BUDGET_SCALE / 100U);
    committed_scaled = ((uint64_t)award + s_major_pool) *
        GIFT_ALGORITHM_BUDGET_SCALE;
    return (earned_scaled > committed_scaled) ?
        (earned_scaled - committed_scaled) : 0U;
}

/**
 * @brief  正账每满P17押分，从C22普通预算转1币到D池
 * @param  account 当前账本快照
 * @param  new_stake 本次新增押分；余额跨过零点的本次押分不计入D池
 * @retval None
 */
static void GiftAlgorithm_FundMajorPool(const Account_Data *account,
                                        uint32_t new_stake)
{
    uint16_t interval = Setting_GetValue(SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL);
    uint64_t total;
    uint64_t newly_due;
    uint64_t available;
    uint32_t transfer;
    int64_t balance = GiftAlgorithm_GetBalance(account);

    if (balance < 0)
    {
        return;
    }
    if ((interval < SETTING_MAJOR_POOL_STAKE_MIN) ||
        (interval > SETTING_MAJOR_POOL_STAKE_MAX))
    {
        interval = SETTING_MAJOR_POOL_STAKE_DEFAULT;
    }
    /* 新押分若刚好填平亏空，本次不算正账押分。 */
    if ((new_stake != 0U) && ((balance - (int64_t)new_stake) >= 0))
    {
        total = (uint64_t)s_major_draw_remainder + new_stake;
        newly_due = total / interval;
        s_major_draw_remainder = (uint16_t)(total % interval);
        if (newly_due > (uint64_t)(UINT32_MAX - s_major_draw_pending))
            s_major_draw_pending = UINT32_MAX;
        else
            s_major_draw_pending += (uint32_t)newly_due;
    }
    available = GiftAlgorithm_GetAvailableScaled(account) /
                GIFT_ALGORITHM_BUDGET_SCALE;
    transfer = (available < s_major_draw_pending) ?
               (uint32_t)available : s_major_draw_pending;
    if (transfer > UINT32_MAX - s_major_pool)
        transfer = UINT32_MAX - s_major_pool;
    s_major_pool += transfer;
    s_major_draw_pending -= transfer;
}

/**
 * @brief  计算指定 5/6/7 连线对应的彩金
 * @param  tier 连线档位；有效值：5、6、7
 * @param  jackpot 当前彩金存数，范围：0~999，单位：币
 * @retval 彩金金额，单位：币；无效档位返回 0
 */
static uint16_t GiftAlgorithm_CalcJackpot(uint8_t tier, uint16_t jackpot)
{
    uint16_t percent;                              // 彩金比例，单位：%

    if (tier == 5U)
    {
        percent = 25U;
    }
    else if (tier == 6U)
    {
        percent = 50U;
    }
    else if (tier == 7U)
    {
        percent = 100U;
    }
    else
    {
        return 0U;
    }
    return (uint16_t)(((uint32_t)jackpot * percent) / 100U);
}

/**
 * @brief  把 1~6 个灯数映射为奖型枚举
 * @param  count 送灯数量，范围：1~6，单位：盏
 * @retval 对应送灯奖型；无效数量返回 GIFT_ALGORITHM_AWARD_NONE
 */
static GiftAlgorithm_AwardType GiftAlgorithm_GiftType(uint8_t count)
{
    static const GiftAlgorithm_AwardType type[6] =
    {
        GIFT_ALGORITHM_AWARD_GIFT_1,
        GIFT_ALGORITHM_AWARD_GIFT_2,
        GIFT_ALGORITHM_AWARD_GIFT_3,
        GIFT_ALGORITHM_AWARD_GIFT_4,
        GIFT_ALGORITHM_AWARD_GIFT_5,
        GIFT_ALGORITHM_AWARD_GIFT_6
    };                                             // 灯数到奖型映射，无量纲

    return ((count >= 1U) && (count <= 6U)) ?
           type[count - 1U] : GIFT_ALGORITHM_AWARD_NONE;
}

/**
 * @brief  枚举当前灯面可支付的送灯连线
 * @param  occupied 当前已亮 2~8 号灯，寄存器位掩码
 * @param  available_scaled 保留兼容入参；高连线送灯只由大奖池支付
 * @param  candidates 输出候选数组；容量至少为 127 项
 * @retval 候选数量，范围：0~127
 */
static uint8_t GiftAlgorithm_BuildCandidates(
    uint16_t occupied,
    uint64_t available_scaled,
    GiftAlgorithm_Candidate *candidates)
{
    uint16_t available;                            // 空灯位图，寄存器位掩码
    uint16_t base_score;                              // 当前自然连线奖，单位：币
    uint16_t jackpot = Jackpot_GetCurrent();         // 当前彩金存数，单位：币
    uint16_t subset;                                 // 1~7 位组合，无量纲
    uint8_t count = 0U;                              // 候选数量，单位：项

    occupied &= GIFT_ALGORITHM_MIDDLE_MASK;
    if (occupied == 0U)
    {
        return 0U;
    }
    available = (uint16_t)(GIFT_ALGORITHM_MIDDLE_MASK & ~occupied);
    base_score = Score_CalcLineScore(occupied);
    for (subset = 1U; subset <= 0x007FU; subset++)
    {
        uint16_t target = 0U;                        // 目标灯位图，寄存器位掩码
        uint16_t final_score;                        // 送灯后固定奖，单位：币
        uint16_t line_cost;                          // 最终连线完整固定奖，单位：币
        uint16_t bonus;                              // 对应彩金，单位：币
        uint8_t bit_index;                           // 七位组合索引，数组下标
        uint8_t tier;                                // 连线档位，5~7 或 0

        for (bit_index = 0U; bit_index < 7U; bit_index++)
        {
            uint16_t channel_bit = (uint16_t)(1U << (bit_index + 1U)); // 实际灯位，寄存器位掩码

            if (((subset & (uint16_t)(1U << bit_index)) != 0U) &&
                ((available & channel_bit) != 0U))
            {
                target |= channel_bit;
            }
            else if ((subset & (uint16_t)(1U << bit_index)) != 0U)
            {
                target = 0U;
                break;
            }
        }
        if (target == 0U)
        {
            continue;
        }
        final_score = Score_CalcLineScore((uint16_t)(occupied | target));
        if (final_score <= base_score)
        {
            continue;
        }
        line_cost = final_score;
        tier = Score_GetJackpotTier((uint16_t)(occupied | target));
        /* 送灯只用于5/6/7连线；2~4连线仍可由真实落币自然形成。 */
        if (tier < 5U)
        {
            continue;
        }
        bonus = GiftAlgorithm_CalcJackpot(tier, jackpot);

        candidates[count].target_mask = target;
        candidates[count].first_mask = GiftAlgorithm_PickBit(target);
        candidates[count].second_mask = GiftAlgorithm_PickBit(
            (uint16_t)(target & ~candidates[count].first_mask));
        candidates[count].gift_count = GiftAlgorithm_CountBits(target);
        candidates[count].tier = tier;
        candidates[count].total_award = (uint16_t)(line_cost + bonus);
        candidates[count].budget_cost = 0U;
        candidates[count].major_cost = candidates[count].total_award;
        if (s_major_pool < candidates[count].major_cost)
        {
            continue;
        }
        count++;
        if (count >= GIFT_ALGORITHM_CANDIDATE_MAX)
        {
            break;
        }
    }
    (void)available_scaled;
    return count;
}

/**
 * @brief  尝试生成指定档位的大奖池送灯结果
 * @param  occupied 当前已亮 2~8 号灯，寄存器位掩码
 * @param  tier 目标连线档位；有效值：5、6、7
 * @param  result 输出开奖结果；不得为 NULL
 * @retval 1 已生成；0 当前灯面没有该档位可支付候选
 */
static uint8_t GiftAlgorithm_SelectMajorGift(uint16_t occupied,
                                             uint8_t tier,
                                             GiftAlgorithm_Result *result)
{
    GiftAlgorithm_Candidate candidates[GIFT_ALGORITHM_CANDIDATE_MAX]; // 可支付候选，单位：项
    uint8_t count = GiftAlgorithm_BuildCandidates(occupied,
        0U, candidates);                            // 候选数量，单位：项
    uint8_t index;                                 // 候选索引，数组下标
    uint8_t matched = 0U;                          // 符合目标档位的候选数量，单位：项
    uint8_t selected = 0U;                         // 选中候选索引，数组下标

    for (index = 0U; index < count; index++)
    {
        if (candidates[index].tier != tier)
        {
            continue;
        }
        matched++;
        if (GiftAlgorithm_RandomBelow(matched) == 0U)
        {
            selected = index;
        }
    }
    if (matched == 0U)
    {
        return 0U;
    }
    result->type = GiftAlgorithm_GiftType(candidates[selected].gift_count);
    result->target_mask = candidates[selected].target_mask;
    result->first_target_mask = candidates[selected].first_mask;
    result->second_target_mask = candidates[selected].second_mask;
    result->reserved_cost = candidates[selected].total_award;
    s_major_pool -= candidates[selected].major_cost;
    return 1U;
}

/**
 * @brief  按90/175、55/175、30/175抽取一次大奖池连线档位
 * @param  无
 * @retval 5、6、7 对应连线档位
 */
static uint8_t GiftAlgorithm_DrawMajorTier(void)
{
    uint32_t drawn = GiftAlgorithm_RandomBelow(       // 抽取值，范围：0~174
        GIFT_ALGORITHM_MAJOR_DRAW_TOTAL);

    if (drawn < GIFT_ALGORITHM_MAJOR_LINE5_WEIGHT)
    {
        return 5U;
    }
    drawn -= GIFT_ALGORITHM_MAJOR_LINE5_WEIGHT;
    if (drawn < GIFT_ALGORITHM_MAJOR_LINE6_WEIGHT)
    {
        return 6U;
    }
    drawn -= GIFT_ALGORITHM_MAJOR_LINE6_WEIGHT;
    return 7U;
}

/**
 * @brief  D池实时余额达到P16时尝试生成大奖池送灯结果
 * @param  occupied 当前已亮 2~8 号灯，寄存器位掩码
 * @param  result 输出开奖结果；不得为 NULL
 * @retval 1 已生成5/6/7连线送灯；0 D池未达门槛、灯型不成立或不可支付
 */
static uint8_t GiftAlgorithm_TryMajorDraw(uint16_t occupied,
                                          GiftAlgorithm_Result *result)
{
    uint8_t tier;                                 // 本次抽中的连线档位，范围：5~7
    uint16_t threshold = Setting_GetValue(        // P16实时余额门槛，单位：币
        SETTING_PARAM_MAJOR_DRAW_COINS);

    if ((threshold < SETTING_MAJOR_DRAW_COINS_MIN) ||
        (threshold > SETTING_MAJOR_DRAW_COINS_MAX) ||
        (((threshold - SETTING_MAJOR_DRAW_COINS_MIN) %
          SETTING_MAJOR_DRAW_COINS_STEP) != 0U))
    {
        threshold = SETTING_MAJOR_DRAW_COINS_DEFAULT;
    }
    if (s_major_pool < threshold)
    {
        return 0U;
    }
    tier = GiftAlgorithm_DrawMajorTier();
    return GiftAlgorithm_SelectMajorGift(occupied, tier, result);
}

/**
 * @brief  尝试按当前普通可返额度生成 3/5/10 币奖
 * @param  available_scaled 当前普通可返额度，单位：百万分之一币
 * @param  result 输出开奖结果；不得为 NULL
 * @retval 1 已生成；0 预算不足 3 币
 */
static uint8_t GiftAlgorithm_SelectCoin(uint64_t available_scaled,
                                        GiftAlgorithm_Result *result)
{
    uint64_t coins = available_scaled / GIFT_ALGORITHM_BUDGET_SCALE; // 可用整币额度，单位：币
    uint8_t award;                                 // 直接币奖，单位：币

    if (coins >= 10U)
    {
        award = 10U;
        result->type = GIFT_ALGORITHM_AWARD_COIN_10;
    }
    else if (coins >= 5U)
    {
        award = 5U;
        result->type = GIFT_ALGORITHM_AWARD_COIN_5;
    }
    else if (coins >= 3U)
    {
        award = 3U;
        result->type = GIFT_ALGORITHM_AWARD_COIN_3;
    }
    else
    {
        return 0U;
    }
    result->reserved_cost = award;
    if (s_recovery != 0U)
    {
        /* 先复核再扣款，不能发生无符号减法下溢后变成巨额预算。 */
        if (GiftAlgorithm_RecoveryAvailable() <
            (uint64_t)award * GIFT_ALGORITHM_BUDGET_SCALE)
        {
            result->type = GIFT_ALGORITHM_AWARD_NONE;
            result->reserved_cost = 0U;
            return 0U;
        }
        s_recovery_paid += award;
        s_recovery_pool_scaled = GiftAlgorithm_RecoveryAvailable();
    }
    return 1U;
}

/**
 * @brief  清空一个开奖结果
 * @param  result 输出结果；不得为 NULL
 * @retval None
 */
static void GiftAlgorithm_ClearResult(GiftAlgorithm_Result *result)
{
    memset(result, 0, sizeof(*result));
    result->type = GIFT_ALGORITHM_AWARD_NONE;
    result->miss_style = (GiftAlgorithm_RandomBelow(100U) < 70U) ?
        GIFT_ALGORITHM_MISS_TEASE : GIFT_ALGORITHM_MISS_EARLY_MISMATCH;
}

/**
 * @brief  初始化新返奖算法及掉电状态
 * @param  无
 * @retval None
 */
void GiftAlgorithm_Init(void)
{
    Account_Data account;                         // 当前账本快照，无量纲
    int64_t balance;                              // C3-C4，单位：币

    Account_GetData(&account);
    memset(&s_diagnostics, 0, sizeof(s_diagnostics));
    s_diagnostics.last_drawn = 255U;
    s_random_state ^= (Gtim_NowMs() | 1U);
    s_state_sequence = 0U;
    s_state_next_slot = 0U;
    s_last_stake = Account_GetStakeSinceEpoch(&account);
    GiftAlgorithm_ResetRecovery(s_last_stake);
    s_coin_grant = 0U;
    s_coin_grant_inverse = 0xFFFFU;
    s_major_pool = 0U;
    balance = GiftAlgorithm_GetBalance(&account);
    s_major_draw_pending = 0U;
    s_major_draw_remainder = 0U;
    s_recovery = (balance < 0) ? 1U : 0U;
    s_effective_percent = GiftAlgorithm_GetConfiguredPercent(s_recovery);
    s_budget_dirty = 0U;

    if (GiftAlgorithm_LoadState() == 0U)
    {
        (void)GiftAlgorithm_SaveState();
    }
    else if (Account_GetStakeSinceEpoch(&account) < s_last_stake)
    {
        (void)GiftAlgorithm_ResetForAccountClear();
    }
}

/**
 * @brief  按新口径选择一次抽奖结果
 * @param  occupied_mask 当前已亮 2~8 号灯，寄存器位掩码
 * @param  result 输出开奖结果；允许为 NULL，NULL 时不执行开奖
 * @retval None
 */
void GiftAlgorithm_Select(uint16_t occupied_mask,
                          GiftAlgorithm_Result *result)
{
    Account_Data account;                         // 当前账本快照，无量纲
    GiftAlgorithm_StateRecord backup;             // 保存失败回滚快照，无量纲
    uint32_t delta;                               // 本次新增押分，单位：币
    uint64_t available_scaled;                    // 本次可返额度，单位：百万分之一币
    uint8_t awarded;                              // 已生成奖励标志，无量纲

    if (result == NULL)
    {
        return;
    }
    GiftAlgorithm_ClearResult(result);
    /* 上一笔尚未成功交给Game时，不能签发下一笔并覆盖许可。 */
    if (s_coin_grant != 0U) return;
    Account_GetData(&account);
    memset(&backup, 0, sizeof(backup));
    backup.version = GIFT_ALGORITHM_STATE_VERSION;
    backup.sequence = s_state_sequence;
    backup.random_state = s_random_state;
    backup.last_stake = s_last_stake;
    backup.recovery_pool_scaled = s_recovery_pool_scaled;
    backup.recovery_funded_scaled = s_recovery_funded_scaled;
    backup.recovery_paid = s_recovery_paid;
    backup.recovery_begin_stake = s_recovery_begin_stake;
    backup.recovery_stakes = s_recovery_stakes;
    backup.major_pool = s_major_pool;
    backup.major_draw_pending = s_major_draw_pending;
    backup.major_draw_remainder = s_major_draw_remainder;
    backup.major_draw_marker = GIFT_ALGORITHM_MAJOR_STATE_MARKER;
    backup.recovery = s_recovery;
    backup.effective_percent = s_effective_percent;
    memcpy(backup.award_count, s_diagnostics.award_count,
           sizeof(backup.award_count));
    backup.limited_count = s_diagnostics.limited_count;
    backup.last_reason = s_diagnostics.last_reason;
    backup.last_drawn = s_diagnostics.last_drawn;

    delta = GiftAlgorithm_UpdateBudget(&account);
    GiftAlgorithm_FundMajorPool(&account, delta);
    if (((delta != 0U) || (s_budget_dirty != 0U)) &&
        (Account_SaveNow() == 0U))
    {
        GiftAlgorithm_ApplyState(&backup);
        s_diagnostics.last_reason = 8U;
        return;
    }
    available_scaled = GiftAlgorithm_GetAvailableScaled(&account);
    s_diagnostics.coin_budget_before_cents = (available_scaled / 10000U > UINT32_MAX) ?
        UINT32_MAX : (uint32_t)(available_scaled / 10000U);
    s_diagnostics.coin_granted = 0U;

    awarded = GiftAlgorithm_TryMajorDraw(
        (uint16_t)(occupied_mask & GIFT_ALGORITHM_MIDDLE_MASK), result);
    if (awarded == 0U)
    {
        awarded = GiftAlgorithm_SelectCoin(available_scaled, result);
    }

    if (awarded == 0U)
    {
        GiftAlgorithm_AddSaturate(&s_diagnostics.limited_count, 1U);
        s_diagnostics.last_reason = 3U;
        s_diagnostics.last_drawn = GIFT_ALGORITHM_AWARD_NONE;
    }
    else
    {
        s_diagnostics.last_reason = (s_recovery != 0U) ? 2U : 1U;
        s_diagnostics.last_drawn = (uint8_t)result->type;
    }
    GiftAlgorithm_AddSaturate(
        &s_diagnostics.award_count[result->type], 1U);

    if (GiftAlgorithm_SaveState() == 0U)
    {
        GiftAlgorithm_ClearResult(result);
        GiftAlgorithm_ApplyState(&backup);
        s_diagnostics.last_reason = 8U;
    }
    else
    {
        s_budget_dirty = 0U;
        if ((result->type == GIFT_ALGORITHM_AWARD_COIN_3) ||
            (result->type == GIFT_ALGORITHM_AWARD_COIN_5) ||
            (result->type == GIFT_ALGORITHM_AWARD_COIN_10))
        {
            s_coin_grant = result->reserved_cost;
            s_coin_grant_inverse = (uint16_t)~s_coin_grant;
            s_diagnostics.coin_granted = s_coin_grant;
        }
    }
}

uint8_t GiftAlgorithm_CoinAwardAuthorized(uint16_t coins)
{
    if (((coins == 3U) || (coins == 5U) || (coins == 10U)) &&
        (coins == s_coin_grant) &&
        ((uint16_t)(s_coin_grant ^ s_coin_grant_inverse) == 0xFFFFU)) return 1U;
    GiftAlgorithm_AddSaturate(&s_diagnostics.rejected_grants, 1U);
    /* 损坏的许可作废，不退回预算；避免伪奖重试或永久挡住后续开奖。 */
    s_coin_grant = 0U;
    s_coin_grant_inverse = 0xFFFFU;
    return 0U;
}

void GiftAlgorithm_CommitCoinAward(uint16_t coins)
{
    if ((coins != 0U) && (coins == s_coin_grant))
    {
        s_coin_grant = 0U;
        s_coin_grant_inverse = 0xFFFFU;
    }
}

/**
 * @brief  读取返奖算法诊断快照
 * @param  occupied_mask 当前已亮灯位；仅保留接口兼容
 * @param  data 输出诊断数据；允许为 NULL
 * @retval None
 */
void GiftAlgorithm_GetDiagnostics(uint16_t occupied_mask,
                                  GiftAlgorithm_Diagnostics *data)
{
    Account_Data account;                         // 当前账本快照，无量纲
    uint64_t available_scaled;
    int64_t balance;

    (void)occupied_mask;
    if (data == NULL)
    {
        return;
    }
    Account_GetData(&account);
    *data = s_diagnostics;
    data->reserved = s_major_pool;
    balance = GiftAlgorithm_GetBalance(&account);
    data->ledger_gap = balance;
    available_scaled = GiftAlgorithm_GetAvailableScaled(&account);
    data->available = (int64_t)(available_scaled /
                                GIFT_ALGORITHM_BUDGET_SCALE);
    data->recovery = (balance < 0) ? 1U : 0U;
    data->effective_percent = GiftAlgorithm_GetConfiguredPercent(data->recovery);
    data->miss_streak = 0U;
    data->guarantee_threshold = 0U;
    data->recovery_stakes = s_recovery_stakes;
    data->recovery_funded_cents = (s_recovery_funded_scaled / 10000U > UINT32_MAX) ?
        UINT32_MAX : (uint32_t)(s_recovery_funded_scaled / 10000U);
    data->recovery_paid = s_recovery_paid;
}

/**
 * @brief  清账时清除负账池、大奖池和新算法诊断
 * @param  无
 * @retval 1 保存成功；0 EEPROM 不可用或写入失败
 */
uint8_t GiftAlgorithm_ResetForAccountClear(void)
{
    Account_Data account;                         // 清账后的账本快照，无量纲
    int64_t balance;                              // 清账后的 C3-C4，单位：币

    Account_GetData(&account);
    s_last_stake = Account_GetStakeSinceEpoch(&account);
    GiftAlgorithm_ResetRecovery(s_last_stake);
    s_coin_grant = 0U;
    s_coin_grant_inverse = 0xFFFFU;
    s_major_pool = 0U;
    balance = GiftAlgorithm_GetBalance(&account);
    s_major_draw_pending = 0U;
    s_major_draw_remainder = 0U;
    s_recovery = (balance < 0) ? 1U : 0U;
    s_effective_percent = GiftAlgorithm_GetConfiguredPercent(s_recovery);
    memset(&s_diagnostics, 0, sizeof(s_diagnostics));
    s_diagnostics.last_drawn = 255U;
    s_random_state ^= (Gtim_NowMs() | 1U);
    if (GiftAlgorithm_SaveState() == 0U)
    {
        s_budget_dirty = 1U;
        return 0U;
    }
    s_budget_dirty = 0U;
    return 1U;
}
