/* AUTO-FILE-DOC
 * 文件职责：彩金计算与联机事件。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "jackpot.h"

#include "eeprom.h"
#include "gtim.h"
#include "setting.h"
#include "./MODBUS/modbus.h"

#include <string.h>

#define JACKPOT_EEPROM_SLOT0_ADDR       0x0200U
#define JACKPOT_EEPROM_SLOT1_ADDR       0x0280U
#define JACKPOT_RECORD_SIZE             128U
#define JACKPOT_MAGIC                   0x50434A4AUL
#define JACKPOT_VERSION                 1U
#define JACKPOT_SAVE_DELAY_MS           200U
#define JACKPOT_MASTER_STEP_INTERVAL_MS 5U
#define JACKPOT_MASTER_WRITE_TURNAROUND_MS 50U
#define JACKPOT_SESSION_MARK            0x5AU
#define JACKPOT_CLAIM_SESSION_MARK      0xC3U
#define JACKPOT_CONTRIBUTION_MARK_16BIT 0x96U
#define JACKPOT_CONTRIBUTION_MARK_32BIT 0xD7U
#define JACKPOT_LOCAL_SEQUENCE_HIGH_POS 12U
#define JACKPOT_LAST_SEQUENCE_HIGH_POS  14U
#define JACKPOT_REMAINDER_16BIT_POS      30U
#define JACKPOT_REMAINDER_MARK_POS       32U
#define JACKPOT_REMAINDER_16BIT_MARK     0xE8U
#define JACKPOT_LOCAL_PAYOUT_TOTAL_POS    33U
#define JACKPOT_LOCAL_PAYOUT_MARK_POS     37U
#define JACKPOT_LOCAL_PAYOUT_MARK         0xA9U
#define JACKPOT_ANOMALY_COUNT_POS         38U
#define JACKPOT_SAFE_CURRENT_POS          40U
#define JACKPOT_ANOMALY_MARK_POS          42U
#define JACKPOT_ANOMALY_NODE_POS          43U
#define JACKPOT_ANOMALY_MARK              0xB6U
#define JACKPOT_ANOMALY_UNKNOWN_NODE      0xFFU
#define JACKPOT_LAST_SESSION_ID         127U

#define JACKPOT_MAX_VALUE               999U
#define JACKPOT_SEQUENCE_WRAP_HIGH_MIN  0xFFFF0000UL
#define JACKPOT_SEQUENCE_WRAP_LOW_MAX   0x0000FFFFUL
#define JACKPOT_MAX_CONTRIBUTION_STEP   3UL
#define JACKPOT_FIRST_CLAIM_ID          0U
#define JACKPOT_LAST_CLAIM_ID           61U
#define JACKPOT_COMMAND_BASE            62U
#define JACKPOT_COMMAND_CURRENT         63U

#define JACKPOT_EVENT_CLAIM_SHIFT       10U
#define JACKPOT_EVENT_TIER_SHIFT        8U
#define JACKPOT_EVENT_PENDING_MASK      0x0080U
#define JACKPOT_EVENT_SESSION_MASK      0x007FU
#define JACKPOT_DOWNLINK_COMMAND_SHIFT  10U
#define JACKPOT_DOWNLINK_VALUE_MASK     0x03FFU

#define JACKPOT_MASTER_NODE_ID          0U
#define JACKPOT_FIRST_SLAVE_ID          1U
#define JACKPOT_LAST_SLAVE_ID           7U
#define JACKPOT_NODE_COUNT              8U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint16_t current;
    uint16_t base_snapshot;
    uint16_t local_contribution_sequence_low;
    uint8_t local_claim_id;
    uint8_t local_claim_pending;
    uint8_t local_claim_tier;
    uint8_t reserved0;
    uint16_t last_contribution_sequence_low[JACKPOT_NODE_COUNT];
    uint8_t contribution_sequence_valid[JACKPOT_NODE_COUNT];
    uint8_t last_claim_id[JACKPOT_NODE_COUNT];
    uint8_t last_claim_valid[JACKPOT_NODE_COUNT];
    uint16_t last_award[JACKPOT_NODE_COUNT];
    uint32_t crc32;
    uint8_t reserved[44];
} Jackpot_Record;

typedef char Jackpot_RecordSizeCheck[
    (sizeof(Jackpot_Record) == JACKPOT_RECORD_SIZE) ? 1 : -1];

typedef enum
{
    JACKPOT_MASTER_PHASE_START_READ = 0U,
    JACKPOT_MASTER_PHASE_WAIT_READ,
    JACKPOT_MASTER_PHASE_START_WRITE,
    JACKPOT_MASTER_PHASE_WAIT_WRITE
} Jackpot_MasterPhase;

typedef enum
{
    JACKPOT_WRITE_NONE = 0U,
    JACKPOT_WRITE_AWARD,
    JACKPOT_WRITE_BASE,
    JACKPOT_WRITE_CURRENT,
    JACKPOT_WRITE_CONTRIBUTION_DIVISOR
} Jackpot_WriteType;

static uint16_t s_current;
/* 仅由合法的押分、连线扣奖、设置基值及主机同步入口更新。 */
static uint16_t s_confirmed_current;
static uint16_t s_anomaly_count;
static uint8_t s_last_anomaly_node;
static uint16_t s_base;
static uint32_t s_local_contribution_sequence;
static uint16_t s_local_coin_remainder;
static uint16_t s_contribution_coins_per_point;
static uint32_t s_local_payout_total;
static uint8_t s_local_session_id;
static uint8_t s_local_claim_id;
static uint8_t s_local_claim_pending;
static uint8_t s_local_claim_tier;
static uint8_t s_award_ready;
static uint16_t s_award_pending;
static uint8_t s_operational;

static uint32_t s_last_contribution_sequence[JACKPOT_NODE_COUNT];
static uint8_t s_contribution_sequence_valid[JACKPOT_NODE_COUNT];
static uint32_t s_large_sequence_candidate[JACKPOT_NODE_COUNT];
static uint8_t s_large_sequence_candidate_valid[JACKPOT_NODE_COUNT];
static uint32_t s_before_large_rebaseline[JACKPOT_NODE_COUNT];
static uint8_t s_large_rebaseline_uncredited[JACKPOT_NODE_COUNT];
static uint8_t s_last_claim_id[JACKPOT_NODE_COUNT];
static uint8_t s_last_claim_session[JACKPOT_NODE_COUNT];
static uint8_t s_last_claim_valid[JACKPOT_NODE_COUNT];
static uint16_t s_last_award[JACKPOT_NODE_COUNT];

static uint8_t s_dirty;
static uint8_t s_storage_deferred;
static uint8_t s_next_slot;
static uint32_t s_sequence;
static uint32_t s_dirty_start_ms;

static Jackpot_MasterPhase s_master_phase;
static uint8_t s_master_slave_id;
static Jackpot_WriteType s_master_write_type;
static uint8_t s_current_broadcast_pending;
static uint32_t s_master_step_start_ms;
static uint16_t s_master_pending_write_register;
static uint16_t s_master_pending_write_value;
static uint16_t s_synced_current[JACKPOT_NODE_COUNT];
static uint16_t s_synced_base[JACKPOT_NODE_COUNT];
static uint16_t s_synced_contribution_divisor[JACKPOT_NODE_COUNT];

static uint16_t Jackpot_BuildEvent(void);
static void Jackpot_MarkDirty(void);
static void Jackpot_InvalidateCurrentSync(void);

/* 饱和计数，不占用错误界面；节点0~7表示异常贡献，0xFF表示本机值异常。 */
static void Jackpot_RecordAnomaly(uint8_t node_id)
{
    if (s_anomaly_count != 0xFFFFU)
    {
        s_anomaly_count++;
    }
    s_last_anomaly_node = node_id;
    Jackpot_MarkDirty();
}

static void Jackpot_ValidateCurrent(void)
{
    if (s_current != s_confirmed_current)
    {
        s_current = s_confirmed_current;
        Jackpot_RecordAnomaly(JACKPOT_ANOMALY_UNKNOWN_NODE);
        Jackpot_InvalidateCurrentSync();
    }
}

/**
 * @brief 彩金计算与联机事件：读取（对应符号 Jackpot_ReadU16）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_LoadRecord。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Jackpot_ReadU16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

/**
 * @brief 彩金计算与联机事件：写入（对应符号 Jackpot_WriteU16）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_BuildRecord。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_WriteU16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

/**
 * @brief 彩金计算与联机事件：读取（对应符号 Jackpot_ReadU32）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_LoadRecord。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Jackpot_ReadU32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

/**
 * @brief 彩金计算与联机事件：写入（对应符号 Jackpot_WriteU32）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_BuildRecord。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_WriteU32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

/**
 * @brief 彩金计算与联机事件：计算 CRC（对应符号 Jackpot_CalcCrc32）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_RecordCrc。
 * @局部状态 主要临时变量：crc、i、bit；均只在本次调用中有效。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param length 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Jackpot_CalcCrc32(const uint8_t *data, uint16_t length)
{
    uint32_t crc = 0xFFFFFFFFUL;
    uint16_t i;
    uint8_t bit;

    for (i = 0U; i < length; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1UL) != 0UL)
            {
                crc = (crc >> 1U) ^ 0xEDB88320UL;
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return ~crc;
}

/**
 * @brief 彩金计算与联机事件：计算 CRC（对应符号 Jackpot_RecordCrc）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_IsRecordValid、jackpot.c:Jackpot_BuildRecord。
 * @局部状态 主要临时变量：temp；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Jackpot_RecordCrc(const Jackpot_Record *record)
{
    Jackpot_Record temp;

    temp = *record;
    temp.crc32 = 0U;
    return Jackpot_CalcCrc32((const uint8_t *)&temp,
                             (uint16_t)sizeof(temp));
}

/**
 * @brief 彩金计算与联机事件：校验（对应符号 Jackpot_IsRecordValid）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_ReadSlot。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Jackpot_IsRecordValid(const Jackpot_Record *record)
{
    if ((record->magic != JACKPOT_MAGIC) ||
        (record->version != JACKPOT_VERSION) ||
        (record->length != JACKPOT_RECORD_SIZE) ||
        (record->crc32 != Jackpot_RecordCrc(record)))
    {
        return 0U;
    }

    return 1U;
}

/**
 * @brief 彩金计算与联机事件：读取（对应符号 Jackpot_ReadSlot）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_Init。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Jackpot_ReadSlot(uint16_t address, Jackpot_Record *record)
{
    if (Eeprom_Read(address, (uint8_t *)record,
                    (uint16_t)sizeof(*record)) == 0U)
    {
        return 0U;
    }

    return Jackpot_IsRecordValid(record);
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_LoadDefaults）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_LoadDefaults(void)
{
    s_base = Setting_GetJackpotBase();
    s_current = s_base;
    s_confirmed_current = s_current;
    s_anomaly_count = 0U;
    s_last_anomaly_node = JACKPOT_ANOMALY_UNKNOWN_NODE;
    s_contribution_coins_per_point =
        Setting_GetJackpotCoinsPerPoint();
    s_local_contribution_sequence = 0U;
    s_local_coin_remainder = 0U;
    s_local_payout_total = 0U;
    s_local_session_id = JACKPOT_LAST_SESSION_ID;
    s_local_claim_id = JACKPOT_LAST_CLAIM_ID;
    s_local_claim_pending = 0U;
    s_local_claim_tier = 0U;
    memset(s_last_contribution_sequence, 0,
           sizeof(s_last_contribution_sequence));
    memset(s_contribution_sequence_valid, 0,
           sizeof(s_contribution_sequence_valid));
    memset(s_large_sequence_candidate_valid, 0,
           sizeof(s_large_sequence_candidate_valid));
    memset(s_large_rebaseline_uncredited, 0,
           sizeof(s_large_rebaseline_uncredited));
    memset(s_last_claim_id, 0, sizeof(s_last_claim_id));
    memset(s_last_claim_session, 0, sizeof(s_last_claim_session));
    memset(s_last_claim_valid, 0, sizeof(s_last_claim_valid));
    memset(s_last_award, 0, sizeof(s_last_award));
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_LoadRecord）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_Init。
 * @局部状态 主要临时变量：id；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_LoadRecord(const Jackpot_Record *record)
{
    uint8_t id;

    s_current = (record->current > JACKPOT_MAX_VALUE) ?
                JACKPOT_MAX_VALUE : record->current;
    if ((record->reserved[11] == JACKPOT_CONTRIBUTION_MARK_16BIT) ||
        (record->reserved[11] == JACKPOT_CONTRIBUTION_MARK_32BIT))
    {
        s_local_contribution_sequence =
            record->local_contribution_sequence_low;
        if (record->reserved[JACKPOT_REMAINDER_MARK_POS] ==
            JACKPOT_REMAINDER_16BIT_MARK)
        {
            s_local_coin_remainder = Jackpot_ReadU16(
                &record->reserved[JACKPOT_REMAINDER_16BIT_POS]);
        }
        else
        {
            s_local_coin_remainder = record->reserved0;
        }
        for (id = 0U; id < JACKPOT_NODE_COUNT; id++)
        {
            s_last_contribution_sequence[id] =
                record->last_contribution_sequence_low[id];
        }

        if (record->reserved[11] == JACKPOT_CONTRIBUTION_MARK_32BIT)
        {
            s_local_contribution_sequence |=
                (uint32_t)Jackpot_ReadU16(
                    &record->reserved[JACKPOT_LOCAL_SEQUENCE_HIGH_POS])
                << 16U;
            for (id = 0U; id < JACKPOT_NODE_COUNT; id++)
            {
                s_last_contribution_sequence[id] |=
                    (uint32_t)Jackpot_ReadU16(
                        &record->reserved[
                            JACKPOT_LAST_SEQUENCE_HIGH_POS + (id * 2U)])
                    << 16U;
            }
        }

        if (s_local_coin_remainder >= s_contribution_coins_per_point)
        {
            s_local_contribution_sequence +=
                (uint32_t)(s_local_coin_remainder /
                           s_contribution_coins_per_point);
            s_local_coin_remainder =
                (uint16_t)(s_local_coin_remainder %
                           s_contribution_coins_per_point);
        }

        memcpy(s_contribution_sequence_valid,
               record->contribution_sequence_valid,
               sizeof(s_contribution_sequence_valid));
    }
    else
    {
        s_local_contribution_sequence = 0U;
        s_local_coin_remainder = 0U;
        memset(s_last_contribution_sequence, 0,
               sizeof(s_last_contribution_sequence));
        memset(s_contribution_sequence_valid, 0,
               sizeof(s_contribution_sequence_valid));
    }

    if (record->reserved[1] == JACKPOT_SESSION_MARK)
    {
        s_local_session_id = (uint8_t)(record->reserved[0] &
                                       JACKPOT_EVENT_SESSION_MASK);
    }
    else
    {
        s_local_session_id = JACKPOT_LAST_SESSION_ID;
    }
    s_local_claim_id = record->local_claim_id;
    if (s_local_claim_id > JACKPOT_LAST_CLAIM_ID)
    {
        s_local_claim_id = JACKPOT_LAST_CLAIM_ID;
    }
    s_local_claim_pending = (record->local_claim_pending != 0U) ? 1U : 0U;
    s_local_claim_tier = record->local_claim_tier;
    memcpy(s_last_claim_id, record->last_claim_id,
           sizeof(s_last_claim_id));
    if (record->reserved[10] == JACKPOT_CLAIM_SESSION_MARK)
    {
        memcpy(s_last_claim_session, &record->reserved[2],
               sizeof(s_last_claim_session));
    }
    else
    {
        memset(s_last_claim_session, 0xFF,
               sizeof(s_last_claim_session));
    }
    memcpy(s_last_claim_valid, record->last_claim_valid,
           sizeof(s_last_claim_valid));
    memcpy(s_last_award, record->last_award,
           sizeof(s_last_award));

    if (record->reserved[JACKPOT_LOCAL_PAYOUT_MARK_POS] ==
        JACKPOT_LOCAL_PAYOUT_MARK)
    {
        s_local_payout_total = Jackpot_ReadU32(
            &record->reserved[JACKPOT_LOCAL_PAYOUT_TOTAL_POS]);
    }
    else
    {
        s_local_payout_total = 0U;
    }

    if (s_current < s_base)
    {
        s_current = s_base;
    }
    if (record->reserved[JACKPOT_ANOMALY_MARK_POS] ==
        JACKPOT_ANOMALY_MARK)
    {
        uint16_t saved_safe = Jackpot_ReadU16(
            &record->reserved[JACKPOT_SAFE_CURRENT_POS]);
        s_anomaly_count = Jackpot_ReadU16(
            &record->reserved[JACKPOT_ANOMALY_COUNT_POS]);
        s_last_anomaly_node =
            record->reserved[JACKPOT_ANOMALY_NODE_POS];
        /* 新格式额外保存上次确认值；CRC 正确但当前值不一致也不采信突变。 */
        if ((saved_safe >= s_base) &&
            (saved_safe <= JACKPOT_MAX_VALUE) &&
            (s_current != saved_safe))
        {
            s_current = saved_safe;
            Jackpot_RecordAnomaly(JACKPOT_ANOMALY_UNKNOWN_NODE);
        }
    }
    s_confirmed_current = s_current;
}

/**
 * @brief 彩金计算与联机事件：构造（对应符号 Jackpot_BuildRecord）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_SaveNow。
 * @局部状态 主要临时变量：id；均只在本次调用中有效。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param sequence 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_BuildRecord(Jackpot_Record *record, uint32_t sequence)
{
    uint8_t id;

    memset(record, 0, sizeof(*record));
    record->magic = JACKPOT_MAGIC;
    record->version = JACKPOT_VERSION;
    record->length = JACKPOT_RECORD_SIZE;
    record->sequence = sequence;
    record->current = s_current;
    record->base_snapshot = s_base;
    record->local_contribution_sequence_low =
        (uint16_t)s_local_contribution_sequence;
    record->local_claim_id = s_local_claim_id;
    record->local_claim_pending = s_local_claim_pending;
    record->local_claim_tier = s_local_claim_tier;
    record->reserved0 = (uint8_t)s_local_coin_remainder;
    for (id = 0U; id < JACKPOT_NODE_COUNT; id++)
    {
        record->last_contribution_sequence_low[id] =
            (uint16_t)s_last_contribution_sequence[id];
    }
    memcpy(record->contribution_sequence_valid,
           s_contribution_sequence_valid,
           sizeof(s_contribution_sequence_valid));
    memcpy(record->last_claim_id, s_last_claim_id,
           sizeof(s_last_claim_id));
    memcpy(record->last_claim_valid, s_last_claim_valid,
           sizeof(s_last_claim_valid));
    memcpy(record->last_award, s_last_award,
           sizeof(s_last_award));
    record->reserved[0] = s_local_session_id;
    record->reserved[1] = JACKPOT_SESSION_MARK;
    memcpy(&record->reserved[2], s_last_claim_session,
           sizeof(s_last_claim_session));
    record->reserved[10] = JACKPOT_CLAIM_SESSION_MARK;
    record->reserved[11] = JACKPOT_CONTRIBUTION_MARK_32BIT;
    Jackpot_WriteU16(
        &record->reserved[JACKPOT_LOCAL_SEQUENCE_HIGH_POS],
        (uint16_t)(s_local_contribution_sequence >> 16U));
    for (id = 0U; id < JACKPOT_NODE_COUNT; id++)
    {
        Jackpot_WriteU16(
            &record->reserved[
                JACKPOT_LAST_SEQUENCE_HIGH_POS + (id * 2U)],
            (uint16_t)(s_last_contribution_sequence[id] >> 16U));
    }
    Jackpot_WriteU16(
        &record->reserved[JACKPOT_REMAINDER_16BIT_POS],
        s_local_coin_remainder);
    record->reserved[JACKPOT_REMAINDER_MARK_POS] =
        JACKPOT_REMAINDER_16BIT_MARK;
    Jackpot_WriteU32(
        &record->reserved[JACKPOT_LOCAL_PAYOUT_TOTAL_POS],
        s_local_payout_total);
    record->reserved[JACKPOT_LOCAL_PAYOUT_MARK_POS] =
        JACKPOT_LOCAL_PAYOUT_MARK;
    Jackpot_WriteU16(&record->reserved[JACKPOT_ANOMALY_COUNT_POS],
                     s_anomaly_count);
    Jackpot_WriteU16(&record->reserved[JACKPOT_SAFE_CURRENT_POS],
                     s_confirmed_current);
    record->reserved[JACKPOT_ANOMALY_MARK_POS] = JACKPOT_ANOMALY_MARK;
    record->reserved[JACKPOT_ANOMALY_NODE_POS] = s_last_anomaly_node;
    record->crc32 = Jackpot_RecordCrc(record);
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_MarkDirty）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_ApplyContributionDivisor、jackpot.c:Jackpot_ApplyContributionSequence、jackpot.c:Jackpot_ResolveAward、jackpot.c:Jackpot_MasterPrepareWrite、jackpot.c:Jackpot_SlaveTask、jackpot.c:Jackpot_Init、jackpot.c:Jackpot_Task、jackpot.c:Jackpot_AddLocalCoins。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_MarkDirty(void)
{
    if (s_dirty == 0U)
    {
        s_dirty = 1U;
        s_dirty_start_ms = Gtim_NowMs();
    }
}

/**
 * @brief 彩金计算与联机事件：更新（对应符号 Jackpot_UpdateLocalRegisters）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_ApplyContributionDivisor、jackpot.c:Jackpot_SlaveTask、jackpot.c:Jackpot_Init、jackpot.c:Jackpot_SetOperational、jackpot.c:Jackpot_AddLocalCoins、jackpot.c:Jackpot_RequestAward、jackpot.c:Jackpot_TakeAward。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_UpdateLocalRegisters(void)
{
    Modbus_SetRegister(
        JACKPOT_REGISTER_CONTRIBUTION_HIGH,
        (uint16_t)(s_local_contribution_sequence >> 16U));
    Modbus_SetRegister(
        JACKPOT_REGISTER_CONTRIBUTION_LOW,
        (uint16_t)s_local_contribution_sequence);
    Modbus_SetRegister(JACKPOT_REGISTER_EVENT, Jackpot_BuildEvent());
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_SaveNow）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_Task。
 * @局部状态 主要临时变量：record、next_sequence、address；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Jackpot_SaveNow(void)
{
    Jackpot_Record record;
    uint32_t next_sequence;
    uint16_t address;

    if ((Eeprom_IsReady() == 0U) && (Eeprom_Check() == 0U))
    {
        return 0U;
    }

    next_sequence = s_sequence + 1UL;
    if (next_sequence == 0UL)
    {
        next_sequence = 1UL;
    }

    Jackpot_BuildRecord(&record, next_sequence);
    address = (s_next_slot == 0U) ?
              JACKPOT_EEPROM_SLOT0_ADDR : JACKPOT_EEPROM_SLOT1_ADDR;

    if (Eeprom_Write(address, (const uint8_t *)&record,
                     (uint16_t)sizeof(record)) == 0U)
    {
        return 0U;
    }

    s_sequence = next_sequence;
    s_next_slot = (s_next_slot == 0U) ? 1U : 0U;
    s_dirty = 0U;
    return 1U;
}

uint8_t Jackpot_ResetForUpgrade(void)
{
    Jackpot_LoadDefaults();
    s_award_ready = 0U;
    s_award_pending = 0U;
    Jackpot_InvalidateCurrentSync();
    Jackpot_UpdateLocalRegisters();
    return Jackpot_SaveNow();
}

/**
 * @brief 彩金计算与联机事件：校验（对应符号 Jackpot_InvalidateCurrentSync）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_ApplyContributionSequence、jackpot.c:Jackpot_ResolveAward、jackpot.c:Jackpot_Task。
 * @局部状态 主要临时变量：id；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_InvalidateCurrentSync(void)
{
    uint8_t id;

    s_current_broadcast_pending = 1U;
    for (id = JACKPOT_FIRST_SLAVE_ID;
         id <= JACKPOT_LAST_SLAVE_ID; id++)
    {
        s_synced_current[id] = 0xFFFFU;
    }
}

/**
 * @brief 彩金计算与联机事件：校验（对应符号 Jackpot_InvalidateBaseSync）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_Task。
 * @局部状态 主要临时变量：id；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_InvalidateBaseSync(void)
{
    uint8_t id;

    for (id = JACKPOT_FIRST_SLAVE_ID;
         id <= JACKPOT_LAST_SLAVE_ID; id++)
    {
        s_synced_base[id] = 0xFFFFU;
    }
}

/**
 * @brief 彩金计算与联机事件：校验（对应符号 Jackpot_InvalidateContributionDivisorSync）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_ApplyContributionDivisor。
 * @局部状态 主要临时变量：id；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_InvalidateContributionDivisorSync(void)
{
    uint8_t id;

    for (id = JACKPOT_FIRST_SLAVE_ID;
         id <= JACKPOT_LAST_SLAVE_ID; id++)
    {
        s_synced_contribution_divisor[id] = 0xFFFFU;
    }
}

/**
 * @brief 彩金计算与联机事件：校验（对应符号 Jackpot_IsContributionDivisorValid）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_ApplyContributionDivisor、jackpot.c:Jackpot_SlaveTask。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Jackpot_IsContributionDivisorValid(uint16_t value)
{
    return ((value >= SETTING_JACKPOT_COINS_PER_POINT_MIN) &&
            (value <= SETTING_JACKPOT_COINS_PER_POINT_MAX) &&
            ((value % SETTING_JACKPOT_COINS_PER_POINT_STEP) == 0U)) ?
           1U : 0U;
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_ApplyContributionDivisor）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_SlaveTask、jackpot.c:Jackpot_Task。
 * @局部状态 主要临时变量：contribution_count；均只在本次调用中有效。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_ApplyContributionDivisor(uint16_t value)
{
    uint32_t contribution_count;

    if ((Jackpot_IsContributionDivisorValid(value) == 0U) ||
        (value == s_contribution_coins_per_point))
    {
        return;
    }

    s_contribution_coins_per_point = value;
    contribution_count = s_local_coin_remainder / value;
    s_local_coin_remainder =
        (uint16_t)(s_local_coin_remainder % value);
    s_local_contribution_sequence += contribution_count;
    Jackpot_InvalidateContributionDivisorSync();
    Jackpot_UpdateLocalRegisters();
    Jackpot_MarkDirty();
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_ApplyContributionSequence）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_MasterTask。
 * @局部状态 主要临时变量：delta、previous、first_sequence；均只在本次调用中有效。
 * @param node_id 按值传入；函数内部修改不会回写调用方变量。
 * @param sequence 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_ApplyContributionSequence(uint8_t node_id,
                                              uint32_t sequence)
{
    uint32_t delta;
    uint16_t previous;

    Jackpot_ValidateCurrent();

    if (node_id >= JACKPOT_NODE_COUNT)
    {
        return;
    }

    if (s_contribution_sequence_valid[node_id] == 0U)
    {
        /* 首次连接只确认对方当前累计序号。旧版把它当作从0新增的贡献，
         * 主机记录丢失、换主板或从机长期单独运行后会瞬间冲到999。
         * 本次快照之前的贡献不能安全区分是否已结算，因此不追补。 */
        s_contribution_sequence_valid[node_id] = 1U;
        s_last_contribution_sequence[node_id] = sequence;
        s_large_sequence_candidate_valid[node_id] = 0U;
        Jackpot_MarkDirty();
        return;
    }

    if (sequence < s_last_contribution_sequence[node_id])
    {
        if ((s_last_contribution_sequence[node_id] >=
             JACKPOT_SEQUENCE_WRAP_HIGH_MIN) &&
            (sequence <= JACKPOT_SEQUENCE_WRAP_LOW_MAX))
        {
            delta = (0xFFFFFFFFUL -
                     s_last_contribution_sequence[node_id]) + 1UL + sequence;
        }
        else
        {
            /* 上一次异常高值只作了临时基线、尚未计入新贡献时，
             * 若读数回到原高水位附近，撤销临时基线，避免后续永远不加彩。 */
            if ((s_large_rebaseline_uncredited[node_id] != 0U) &&
                (sequence >= s_before_large_rebaseline[node_id]) &&
                ((sequence - s_before_large_rebaseline[node_id]) <=
                 JACKPOT_MAX_CONTRIBUTION_STEP))
            {
                s_last_contribution_sequence[node_id] = sequence;
                s_large_rebaseline_uncredited[node_id] = 0U;
                s_large_sequence_candidate_valid[node_id] = 0U;
                Jackpot_MarkDirty();
                return;
            }
            /* 非正常回绕的序号倒退可能是从机掉电后回到旧 EEPROM
             * 快照，也可能是同地址设备更换。不能把低值设为新基准：
             * 它下一次跳回旧高值时会把历史贡献再加一次，甚至冲到999。
             * 保持已确认的高水位；从机重新追上后只计真正新增部分。 */
            s_large_sequence_candidate_valid[node_id] = 0U;
            return;
        }
    }
    else
    {
        delta = sequence - s_last_contribution_sequence[node_id];
    }
    if (delta == 0U)
    {
        s_large_sequence_candidate_valid[node_id] = 0U;
        return;
    }

    /* 正常轮询间隔内不可能凭空增加超过3个彩金点。清账不会重置
     * 贡献序号；若主机/从机快照不同步或同地址设备混入，旧代码会
     * 直接将巨大差值封顶到999。连续两次读到同一高区间后只重建
     * 序号基线，不追补无法验证的历史差额；单次坏帧不会污染基线。 */
    if (delta > JACKPOT_MAX_CONTRIBUTION_STEP)
    {
        if ((s_large_sequence_candidate_valid[node_id] != 0U) &&
            (sequence >= s_large_sequence_candidate[node_id]) &&
            ((sequence - s_large_sequence_candidate[node_id]) <=
             JACKPOT_MAX_CONTRIBUTION_STEP))
        {
            s_before_large_rebaseline[node_id] =
                s_last_contribution_sequence[node_id];
            s_large_rebaseline_uncredited[node_id] = 1U;
            s_last_contribution_sequence[node_id] = sequence;
            s_large_sequence_candidate_valid[node_id] = 0U;
            Jackpot_MarkDirty();
        }
        else
        {
            if (s_large_sequence_candidate_valid[node_id] == 0U)
            {
                Jackpot_RecordAnomaly(node_id);
            }
            s_large_sequence_candidate[node_id] = sequence;
            s_large_sequence_candidate_valid[node_id] = 1U;
        }
        return;
    }
    s_large_sequence_candidate_valid[node_id] = 0U;


    previous = s_current;
    if (delta >= (uint32_t)(JACKPOT_MAX_VALUE - s_current))
    {
        s_current = JACKPOT_MAX_VALUE;
    }
    else
    {
        s_current = (uint16_t)(s_current + (uint16_t)delta);
    }
    s_last_contribution_sequence[node_id] = sequence;
    s_large_rebaseline_uncredited[node_id] = 0U;
    s_confirmed_current = s_current;
    if (s_current != previous)
    {
        Jackpot_InvalidateCurrentSync();
    }

    Jackpot_MarkDirty();
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_ResolveAward）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_MasterPrepareWrite、jackpot.c:Jackpot_RequestAward。
 * @局部状态 主要临时变量：percent、award、remaining；均只在本次调用中有效。
 * @param line_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Jackpot_ResolveAward(uint8_t line_count)
{
    Jackpot_ValidateCurrent();
#if (JACKPOT_LINE_BONUS_ENABLED == 0U)
    /*
     * 兼容混合版本联网：即使旧版从机仍发来5/6/7连线申请，
     * 新版主机也只回复0彩金，不扣s_current。原比例代码仍保留在条件编译的恢复分支。
     */
    (void)line_count;
    return 0U;
#else
    uint16_t percent;
    uint16_t award;
    uint16_t remaining;

    if (line_count == 5U)
    {
        percent = 25U;
    }
    else if (line_count == 6U)
    {
        percent = 50U;
    }
    else if (line_count == 7U)
    {
        percent = 100U;
    }
    else
    {
        return 0U;
    }

    award = (uint16_t)(((uint32_t)s_current * percent) / 100U);
    remaining = (uint16_t)(s_current - award);
    s_current = (remaining < s_base) ? s_base : remaining;
    s_confirmed_current = s_current;
    Jackpot_InvalidateCurrentSync();
    Jackpot_MarkDirty();
    return award;
#endif
}

/**
 * @brief 彩金计算与联机事件：构造（对应符号 Jackpot_BuildEvent）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_UpdateLocalRegisters。
 * @局部状态 主要临时变量：tier_code；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Jackpot_BuildEvent(void)
{
    uint16_t tier_code = 0U;

    if (s_local_claim_tier == 5U)
    {
        tier_code = 1U;
    }
    else if (s_local_claim_tier == 6U)
    {
        tier_code = 2U;
    }
    else if (s_local_claim_tier == 7U)
    {
        tier_code = 3U;
    }

    return (uint16_t)(((uint16_t)s_local_claim_id << JACKPOT_EVENT_CLAIM_SHIFT) |
                      (tier_code << JACKPOT_EVENT_TIER_SHIFT) |
                      (((s_operational != 0U) &&
                        (s_local_claim_pending != 0U)) ?
                       JACKPOT_EVENT_PENDING_MASK : 0U) |
                      (s_local_session_id & JACKPOT_EVENT_SESSION_MASK));
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_DecodeTier）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_MasterPrepareWrite。
 * @局部状态 主要临时变量：code；均只在本次调用中有效。
 * @param event 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Jackpot_DecodeTier(uint16_t event)
{
    uint8_t code = (uint8_t)((event >> JACKPOT_EVENT_TIER_SHIFT) & 0x03U);

    if (code == 1U)
    {
        return 5U;
    }
    if (code == 2U)
    {
        return 6U;
    }
    if (code == 3U)
    {
        return 7U;
    }
    return 0U;
}

/**
 * @brief 彩金计算与联机事件：构造（对应符号 Jackpot_BuildDownlink）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_MasterPrepareWrite、jackpot.c:Jackpot_MasterTask。
 * @param command 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Jackpot_BuildDownlink(uint8_t command, uint16_t value)
{
    return (uint16_t)(((uint16_t)command << JACKPOT_DOWNLINK_COMMAND_SHIFT) |
                      (value & JACKPOT_DOWNLINK_VALUE_MASK));
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_MasterAdvance）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_MasterTask。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_MasterAdvance(void)
{
    s_master_slave_id++;
    if (s_master_slave_id > JACKPOT_LAST_SLAVE_ID)
    {
        s_master_slave_id = JACKPOT_FIRST_SLAVE_ID;
    }
    s_master_phase = JACKPOT_MASTER_PHASE_START_READ;
    s_master_write_type = JACKPOT_WRITE_NONE;
    s_master_step_start_ms = Gtim_NowMs();
}

/**
 * @brief 彩金计算与联机事件：写入（对应符号 Jackpot_MasterPrepareWrite）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_MasterTask。
 * @局部状态 主要临时变量：claim_id、session_id、tier、award；均只在本次调用中有效。
 * @param slave_id 按值传入；函数内部修改不会回写调用方变量。
 * @param event 按值传入；函数内部修改不会回写调用方变量。
 * @param register_address 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param write_value 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param write_type 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Jackpot_MasterPrepareWrite(uint8_t slave_id,
                                          uint16_t event,
                                          uint16_t *register_address,
                                          uint16_t *write_value,
                                          Jackpot_WriteType *write_type)
{
    uint8_t claim_id;
    uint8_t session_id;
    uint8_t tier;
    uint16_t award;

    if ((event & JACKPOT_EVENT_PENDING_MASK) != 0U)
    {
        claim_id = (uint8_t)(event >> JACKPOT_EVENT_CLAIM_SHIFT);
        session_id = (uint8_t)(event & JACKPOT_EVENT_SESSION_MASK);
        tier = Jackpot_DecodeTier(event);
        if ((claim_id <= JACKPOT_LAST_CLAIM_ID) && (tier != 0U))
        {
            if ((s_last_claim_valid[slave_id] != 0U) &&
                (s_last_claim_id[slave_id] == claim_id) &&
                (s_last_claim_session[slave_id] == session_id))
            {
                award = s_last_award[slave_id];
            }
            else
            {
                award = Jackpot_ResolveAward(tier);
                s_last_claim_id[slave_id] = claim_id;
                s_last_claim_session[slave_id] = session_id;
                s_last_claim_valid[slave_id] = 1U;
                s_last_award[slave_id] = award;
                Jackpot_MarkDirty();
            }

            *register_address = JACKPOT_REGISTER_DOWNLINK;
            *write_value = Jackpot_BuildDownlink(claim_id, award);
            *write_type = JACKPOT_WRITE_AWARD;
            return 1U;
        }
    }

    if (s_synced_contribution_divisor[slave_id] !=
        s_contribution_coins_per_point)
    {
        *register_address = JACKPOT_REGISTER_CONTRIBUTION_DIVISOR;
        *write_value = s_contribution_coins_per_point;
        *write_type = JACKPOT_WRITE_CONTRIBUTION_DIVISOR;
        return 1U;
    }

    if (s_synced_base[slave_id] != s_base)
    {
        *register_address = JACKPOT_REGISTER_DOWNLINK;
        *write_value = Jackpot_BuildDownlink(JACKPOT_COMMAND_BASE, s_base);
        *write_type = JACKPOT_WRITE_BASE;
        return 1U;
    }

    if (s_synced_current[slave_id] != s_current)
    {
        *register_address = JACKPOT_REGISTER_DOWNLINK;
        *write_value = Jackpot_BuildDownlink(
            JACKPOT_COMMAND_CURRENT, s_current);
        *write_type = JACKPOT_WRITE_CURRENT;
        return 1U;
    }

    return 0U;
}

/**
 * @brief 彩金计算与联机事件：周期处理（对应符号 Jackpot_MasterTask）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_Task。
 * @局部状态 主要临时变量：result_slave、start_register、values、value_count、write_register、downlink、contribution_sequence、write_type、status；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_MasterTask(void)
{
    uint8_t result_slave;
    uint16_t start_register;
    uint16_t values[3];
    uint16_t value_count;
    uint16_t write_register;
    uint16_t downlink;
    uint32_t contribution_sequence;
    Jackpot_WriteType write_type;
    Modbus_Status status;

    Jackpot_ApplyContributionSequence(
        JACKPOT_MASTER_NODE_ID,
        s_local_contribution_sequence);

    if (s_master_phase == JACKPOT_MASTER_PHASE_START_READ)
    {
        if (Gtim_IsElapsed(s_master_step_start_ms,
                           JACKPOT_MASTER_STEP_INTERVAL_MS) == 0U)
        {
            return;
        }

        if (s_current_broadcast_pending != 0U)
        {
            downlink = Jackpot_BuildDownlink(JACKPOT_COMMAND_CURRENT,
                                              s_current);
            if (Modbus_MasterBroadcastWrite(
                    JACKPOT_REGISTER_DOWNLINK, downlink) != 0U)
            {
                s_current_broadcast_pending = 0U;
                s_master_step_start_ms = Gtim_NowMs();
            }
            return;
        }

        if (Modbus_MasterRead(s_master_slave_id,
                              JACKPOT_REGISTER_CONTRIBUTION_HIGH,
                              3U) != 0U)
        {
            s_master_phase = JACKPOT_MASTER_PHASE_WAIT_READ;
        }
        return;
    }

    if (s_master_phase == JACKPOT_MASTER_PHASE_WAIT_READ)
    {
        if (Modbus_MasterTakeReadResult(&result_slave, &start_register,
                                        values, 3U,
                                        &value_count) != 0U)
        {
            if ((result_slave == s_master_slave_id) &&
                (start_register == JACKPOT_REGISTER_CONTRIBUTION_HIGH) &&
                (value_count == 3U))
            {
                contribution_sequence =
                    ((uint32_t)values[0] << 16U) | values[1];
                Jackpot_ApplyContributionSequence(result_slave,
                                                  contribution_sequence);
                if (Jackpot_MasterPrepareWrite(result_slave, values[2],
                                               &write_register,
                                               &downlink,
                                               &write_type) != 0U)
                {
                    /*
                     * The GD32 board uses an automatic RS485 direction
                     * circuit.  Do not transmit FC06 in the same task pass
                     * that consumed its FC03 reply: allow the slave driver
                     * to release the bus and return to receive mode first.
                     */
                    s_master_pending_write_register = write_register;
                    s_master_pending_write_value = downlink;
                    s_master_write_type = write_type;
                    s_master_step_start_ms = Gtim_NowMs();
                    s_master_phase = JACKPOT_MASTER_PHASE_START_WRITE;
                    return;
                }
            }

            Jackpot_MasterAdvance();
            return;
        }

        status = Modbus_MasterGetStatus();
        if ((status != MODBUS_STATUS_BUSY) &&
            (status != MODBUS_STATUS_IDLE))
        {
            Jackpot_MasterAdvance();
        }
        return;
    }

    if (s_master_phase == JACKPOT_MASTER_PHASE_START_WRITE)
    {
        if (Gtim_IsElapsed(s_master_step_start_ms,
                           JACKPOT_MASTER_WRITE_TURNAROUND_MS) == 0U)
        {
            return;
        }

        if (Modbus_MasterWrite(s_master_slave_id,
                               s_master_pending_write_register,
                               s_master_pending_write_value) != 0U)
        {
            s_master_phase = JACKPOT_MASTER_PHASE_WAIT_WRITE;
        }
        return;
    }

    status = Modbus_MasterGetStatus();
    if (status == MODBUS_STATUS_BUSY)
    {
        return;
    }

    if (status == MODBUS_STATUS_SUCCESS)
    {
        if (s_master_write_type == JACKPOT_WRITE_BASE)
        {
            s_synced_base[s_master_slave_id] = s_base;
        }
        else if (s_master_write_type == JACKPOT_WRITE_CURRENT)
        {
            s_synced_current[s_master_slave_id] = s_current;
        }
        else if (s_master_write_type ==
                 JACKPOT_WRITE_CONTRIBUTION_DIVISOR)
        {
            s_synced_contribution_divisor[s_master_slave_id] =
                s_contribution_coins_per_point;
        }
    }

    Jackpot_MasterAdvance();
}

/**
 * @brief 彩金计算与联机事件：周期处理（对应符号 Jackpot_SlaveTask）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_Task。
 * @局部状态 主要临时变量：contribution_divisor、downlink、command、value；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Jackpot_SlaveTask(void)
{
    uint16_t contribution_divisor;
    uint16_t downlink;
    uint8_t command;
    uint16_t value;

    Jackpot_ValidateCurrent();

    contribution_divisor = Modbus_GetRegister(
        JACKPOT_REGISTER_CONTRIBUTION_DIVISOR);
    if ((Jackpot_IsContributionDivisorValid(contribution_divisor) != 0U) &&
        (Setting_GetJackpotCoinsPerPoint() != contribution_divisor))
    {
        Setting_SetValue(SETTING_PARAM_JACKPOT_COINS_PER_POINT,
                         contribution_divisor);
        Jackpot_ApplyContributionDivisor(contribution_divisor);
    }

    Jackpot_UpdateLocalRegisters();
    downlink = Modbus_GetRegister(JACKPOT_REGISTER_DOWNLINK);
    command = (uint8_t)(downlink >> JACKPOT_DOWNLINK_COMMAND_SHIFT);
    value = (uint16_t)(downlink & JACKPOT_DOWNLINK_VALUE_MASK);

    if ((command <= JACKPOT_LAST_CLAIM_ID) &&
        (s_local_claim_pending != 0U) &&
        (command == s_local_claim_id))
    {
        s_award_pending = value;
        s_award_ready = 1U;
        Modbus_SetRegister(JACKPOT_REGISTER_DOWNLINK, 0U);
    }
    else if ((command == JACKPOT_COMMAND_BASE) &&
             (value <= JACKPOT_MAX_VALUE))
    {
        if (Setting_GetJackpotBase() != value)
        {
            Setting_SetValue(SETTING_PARAM_JACKPOT_BASE, value);
        }
    }
    else if ((command == JACKPOT_COMMAND_CURRENT) &&
             (value <= JACKPOT_MAX_VALUE) &&
             (s_current != value))
    {
        s_current = value;
        /* 主机的绝对值同步是协议授权操作，可包含正常的大奖扣减。 */
        s_confirmed_current = s_current;
        Jackpot_MarkDirty();
    }
}

/**
 * @brief 彩金计算与联机事件：初始化（对应符号 Jackpot_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @局部状态 主要临时变量：slot0、slot1、slot0_valid、slot1_valid、id；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Jackpot_Init(void)
{
    Jackpot_Record slot0;
    Jackpot_Record slot1;
    uint8_t slot0_valid;
    uint8_t slot1_valid;
    uint8_t id;

    Jackpot_LoadDefaults();
    s_dirty = 0U;
    s_next_slot = 0U;
    s_sequence = 0U;
    s_dirty_start_ms = Gtim_NowMs();
    s_award_ready = 0U;
    s_award_pending = 0U;
    s_operational = 0U;
    s_master_phase = JACKPOT_MASTER_PHASE_START_READ;
    s_master_slave_id = JACKPOT_FIRST_SLAVE_ID;
    s_master_write_type = JACKPOT_WRITE_NONE;
    s_current_broadcast_pending = 1U;
    s_master_step_start_ms = Gtim_NowMs();
    s_master_pending_write_register = 0U;
    s_master_pending_write_value = 0U;

    for (id = 0U; id < JACKPOT_NODE_COUNT; id++)
    {
        s_synced_current[id] = 0xFFFFU;
        s_synced_base[id] = 0xFFFFU;
        s_synced_contribution_divisor[id] = 0xFFFFU;
    }

    if ((Eeprom_IsReady() == 0U) && (Eeprom_Check() == 0U))
    {
        return;
    }

    slot0_valid = Jackpot_ReadSlot(JACKPOT_EEPROM_SLOT0_ADDR, &slot0);
    slot1_valid = Jackpot_ReadSlot(JACKPOT_EEPROM_SLOT1_ADDR, &slot1);

    if ((slot0_valid != 0U) && (slot1_valid != 0U))
    {
        if (slot0.sequence >= slot1.sequence)
        {
            Jackpot_LoadRecord(&slot0);
            s_sequence = slot0.sequence;
            s_next_slot = 1U;
        }
        else
        {
            Jackpot_LoadRecord(&slot1);
            s_sequence = slot1.sequence;
            s_next_slot = 0U;
        }
    }
    else if (slot0_valid != 0U)
    {
        Jackpot_LoadRecord(&slot0);
        s_sequence = slot0.sequence;
        s_next_slot = 1U;
    }
    else if (slot1_valid != 0U)
    {
        Jackpot_LoadRecord(&slot1);
        s_sequence = slot1.sequence;
        s_next_slot = 0U;
    }
    else
    {
        Jackpot_MarkDirty();
    }

#if (JACKPOT_LINE_BONUS_ENABLED == 0U)
    /*
     * A previous firmware may have saved a line-jackpot claim immediately
     * before power loss.  The disabled build has no game state waiting to
     * consume its reply, so cancel that stale claim during migration.
     */
    if ((s_local_claim_pending != 0U) || (s_local_claim_tier != 0U))
    {
        s_local_claim_pending = 0U;
        s_local_claim_tier = 0U;
        Jackpot_MarkDirty();
    }
#endif

    /*
     * The GD32 board can change between slave and master with SW2.  A claim
     * saved while it was an offline slave must not block a later standalone
     * boot as address 000: a master resolves its own new claims locally and
     * has no upstream node that could finish the old slave request.
     *
     * Pending claims are now kept until Game restores/quarantines the account
     * journal durably. Clearing only this journal strands the saved game WAIT.
     */
    /* 非分机启动也先保留旧申请：Game恢复层会把孤立WAIT原子隔离到主账，
     * 主账保存成功后才Confirm清申请。不能先清申请再留下永远收不到回复的WAIT。 */

    /*
     * A pending claim is a persistent transaction.  Keep its original
     * session token across a slave reboot so the master recognises it as
     * the same request and retries the cached award instead of deducting
     * the pool a second time.  A new session is allocated only when there
     * is no unfinished slave claim (or when this board boots as master).
     */
    if (s_local_claim_pending == 0U)
    {
        s_local_session_id =
            (s_local_session_id >= JACKPOT_LAST_SESSION_ID) ?
            0U : (uint8_t)(s_local_session_id + 1U);
        Jackpot_MarkDirty();
    }

    if (Modbus_GetRole() == MODBUS_ROLE_SLAVE)
    {
        Jackpot_UpdateLocalRegisters();
        Modbus_SetRegister(JACKPOT_REGISTER_DOWNLINK, 0U);
    }
}

/**
 * @brief 彩金计算与联机事件：周期处理（对应符号 Jackpot_Task）。
 * @调用关系 直接调用者：app_main.c:App_Loop。
 * @局部状态 主要临时变量：new_base、new_contribution_divisor；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Jackpot_Task(void)
{
    uint16_t new_base = Setting_GetJackpotBase();
    uint16_t new_contribution_divisor =
        Setting_GetJackpotCoinsPerPoint();

    /* 两种角色共用此检查：任何绕过合法入口写入的彩金变化都回滚。 */
    Jackpot_ValidateCurrent();

    /*
     * Power-on self-test owns the machine until the running-light sequence
     * finishes.  In particular, a GD32 master must not deduct a slave claim
     * and a slave must not consume a downlink during POST.
     */
    if (s_operational == 0U)
    {
        return;
    }

    Jackpot_ApplyContributionDivisor(new_contribution_divisor);

    if (new_base != s_base)
    {
        s_base = new_base;
        if (s_current < s_base)
        {
            s_current = s_base;
            s_confirmed_current = s_current;
        }
        Jackpot_InvalidateBaseSync();
        Jackpot_InvalidateCurrentSync();
        Jackpot_MarkDirty();
    }

    if (Modbus_GetRole() == MODBUS_ROLE_MASTER)
    {
        Jackpot_MasterTask();
    }
    else if (Modbus_GetRole() == MODBUS_ROLE_SLAVE)
    {
        Jackpot_SlaveTask();
    }
    else
    {
        /* P29单机模式：只结算本机押分和本机彩金，不产生任何485收发。 */
        Jackpot_ApplyContributionSequence(JACKPOT_MASTER_NODE_ID,
                                          s_local_contribution_sequence);
    }

    if ((s_storage_deferred == 0U) && (s_dirty != 0U) &&
        (Gtim_IsElapsed(s_dirty_start_ms, JACKPOT_SAVE_DELAY_MS) != 0U))
    {
        (void)Jackpot_SaveNow();
    }
}

void Jackpot_SetStorageDeferred(uint8_t deferred)
{
    s_storage_deferred = (deferred != 0U) ? 1U : 0U;
}

/**
 * @brief 彩金计算与联机事件：设置（对应符号 Jackpot_SetOperational）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：operational；均只在本次调用中有效。
 * @param enabled 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Jackpot_SetOperational(uint8_t enabled)
{
    uint8_t operational = (enabled != 0U) ? 1U : 0U;

    if (s_operational == operational)
    {
        return;
    }

    s_operational = operational;
    if (Modbus_GetRole() == MODBUS_ROLE_SLAVE)
    {
        /* Publish a persisted pending claim only after POST has completed. */
        Jackpot_UpdateLocalRegisters();
    }
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_AddLocalCoins）。
 * @调用关系 直接调用者：game.c:GAME_MODE_READY成功启动一局时。
 * @局部状态 主要临时变量：total、contribution_count；均只在本次调用中有效。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Jackpot_AddLocalCoins(uint32_t count)
{
    uint32_t total;
    uint32_t contribution_count;

    if (count == 0U)
    {
        return;
    }

    total = (uint32_t)s_local_coin_remainder + count;
    contribution_count = total / s_contribution_coins_per_point;
    s_local_coin_remainder =
        (uint16_t)(total % s_contribution_coins_per_point);
    s_local_contribution_sequence =
        s_local_contribution_sequence + contribution_count;

    Jackpot_UpdateLocalRegisters();
    Jackpot_MarkDirty();
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_AddLocalPayout）。
 * @调用关系 直接调用者：game.c:Game_AddLinePrizeAndHold。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Jackpot_AddLocalPayout(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    if ((0xFFFFFFFFUL - s_local_payout_total) < count)
    {
        s_local_payout_total = 0xFFFFFFFFUL;
    }
    else
    {
        s_local_payout_total += count;
    }
    Jackpot_MarkDirty();
}

/**
 * @brief 彩金计算与联机事件：读取（对应符号 Jackpot_GetLocalPayoutTotal）。
 * @调用关系 直接调用者：game.c:Game_GetAccountAwardTotal、game.c:Game_GetAccountPageValue、machine_lock.c:MachineLock_GetCurrentProfit、machine_lock.c:MachineLock_CaptureQuery、remote_config.c:Remote_FillAccountValues。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Jackpot_GetLocalPayoutTotal(void)
{
    return s_local_payout_total;
}

/**
 * @brief 彩金计算与联机事件：清除（对应符号 Jackpot_ClearLocalPayoutTotal）。
 * @调用关系 直接调用者：account.c:Account_ClearBookData。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Jackpot_ClearLocalPayoutTotal(void)
{
    if (s_local_payout_total == 0U)
    {
        return;
    }

    s_local_payout_total = 0U;
    Jackpot_MarkDirty();
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_RequestAward）。
 * @调用关系 直接调用者：game.c:Game_UpdateJackpotSettlement。
 * @param line_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Jackpot_RequestAward(uint8_t line_count)
{
#if (JACKPOT_LINE_BONUS_ENABLED == 0U)
    /* 当前版本不产生新的连线彩金申请。 */
    (void)line_count;
    return 0U;
#else
    uint8_t previous_claim_id;
    if ((line_count < 5U) || (line_count > 7U))
    {
        return 0U;
    }
    /* A reset may leave the account at BASE while the claim is already saved.
     * Reuse that transaction; never allocate another request number. */
    if (s_local_claim_pending != 0U)
    {
        return (s_local_claim_tier == line_count) ? 1U : 0U;
    }

    previous_claim_id = s_local_claim_id;
    s_local_claim_id = (s_local_claim_id >= JACKPOT_LAST_CLAIM_ID) ?
                       JACKPOT_FIRST_CLAIM_ID :
                       (uint8_t)(s_local_claim_id + 1U);

    if (Modbus_GetRole() != MODBUS_ROLE_SLAVE)
    {
        s_award_pending = Jackpot_ResolveAward(line_count);
        s_award_ready = 1U;
        return 1U;
    }

    if (Modbus_GetRole() != MODBUS_ROLE_SLAVE)
    {
        return 0U;
    }

    s_local_claim_tier = line_count;
    s_local_claim_pending = 1U;
    /* Save the request/session BEFORE exposing the pending bit to the master. */
    if (Jackpot_SaveNow() == 0U)
    {
        s_local_claim_id = previous_claim_id;
        s_local_claim_tier = 0U;
        s_local_claim_pending = 0U;
        return 0U;
    }
    Jackpot_UpdateLocalRegisters();
    return 1U;
#endif
}

/**
 * @brief 彩金计算与联机事件：读取并清除一次性事件（对应符号 Jackpot_TakeAward）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param award 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Jackpot_PeekAward(uint16_t *award)
{
    if ((award == 0) || (s_award_ready == 0U))
    {
        return 0U;
    }

    *award = s_award_pending;
    return 1U;
}

uint8_t Jackpot_ConfirmAward(void)
{
    uint8_t tier = s_local_claim_tier;
    if (s_local_claim_pending != 0U)
    {
        s_local_claim_pending = 0U;
        s_local_claim_tier = 0U;
        if (Jackpot_SaveNow() == 0U)
        {
            s_local_claim_pending = 1U;
            s_local_claim_tier = tier;
            return 0U;
        }
        Jackpot_UpdateLocalRegisters();
    }
    s_award_pending = 0U;
    s_award_ready = 0U;
    return 1U;
}

/* Legacy API retained for other callers; game.c uses peek -> save -> confirm. */
uint8_t Jackpot_TakeAward(uint16_t *award)
{
    if (Jackpot_PeekAward(award) == 0U) return 0U;
    return Jackpot_ConfirmAward();
}

/**
 * @brief 彩金计算与联机事件：读取（对应符号 Jackpot_GetPendingTier）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Jackpot_GetPendingTier(void)
{
    if ((s_local_claim_pending == 0U) ||
        (s_local_claim_tier < 5U) ||
        (s_local_claim_tier > 7U))
    {
        return 0U;
    }

    return s_local_claim_tier;
}

/**
 * @brief 彩金计算与联机事件：读取（对应符号 Jackpot_GetCurrent）。
 * @调用关系 直接调用者：app_main.c:App_Init、app_main.c:App_Loop、game.c:Game_GetAccountPageValue、remote_config.c:Remote_FillAccountValues、standby.c:Standby_BeginPart、standby.c:Standby_BeginLineAward、standby.c:Standby_Stop、standby.c:Standby_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Jackpot_GetCurrent(void)
{
    return s_confirmed_current;
}

/* 内部诊断数据，不弹出 E 码，不改变游戏状态。 */
uint16_t Jackpot_GetAnomalyCount(void)
{
    return s_anomaly_count;
}

uint8_t Jackpot_GetLastAnomalyNode(void)
{
    return s_last_anomaly_node;
}

/**
 * @brief 彩金计算与联机事件：执行内部处理（对应符号 Jackpot_IsBaseEditable）。
 * @调用关系 直接调用者：game.c:Game_TaskSettingMode、remote_config.c:Remote_MapSetStatus。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Jackpot_IsBaseEditable(void)
{
    return (Modbus_GetRole() != MODBUS_ROLE_SLAVE) ? 1U : 0U;
}
