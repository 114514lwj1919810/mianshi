/* AUTO-FILE-DOC
 * 文件职责：查账数据与持久化。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "account.h"

#include "eeprom.h"
#include "gtim.h"
#include "jackpot.h"

#include <stddef.h>
#include <string.h>

/*
 * Account EEPROM 布局。
 *
 * AT24C256 一共有 32768 字节。
 * 这里先只占用最前面的 128 字节：
 * - 0x0000~0x003F：账目备份槽 0。
 * - 0x0040~0x007F：账目备份槽 1。
 *
 * 为什么用两个槽：
 * - EEPROM 写入时如果突然掉电，正在写的那一槽可能只写了一半。
 * - 另一槽仍然保留上一次完整数据。
 * - 下次上电时通过 CRC 和 sequence 序号选择最新且完整的一槽。
 */
#define ACCOUNT_EEPROM_SLOT0_ADDR       0x0000U
#define ACCOUNT_EEPROM_SLOT1_ADDR       0x0040U
#define ACCOUNT_RECORD_SIZE             64U
#define ACCOUNT_MAGIC                   0x54434341UL
#define ACCOUNT_VERSION                 1U
#define ACCOUNT_EPOCH_SLOT0_ADDR        0x1440U
#define ACCOUNT_EPOCH_SLOT1_ADDR        0x1460U
#define ACCOUNT_EPOCH_MAGIC             0x32475045UL
#define ACCOUNT_EPOCH_VERSION           1U
#define ACCOUNT_PHYSICAL_SLOT0_ADDR     0x1480U
#define ACCOUNT_PHYSICAL_SLOT1_ADDR     0x14A0U
#define ACCOUNT_PHYSICAL_MAGIC          0x59485041UL
#define ACCOUNT_PHYSICAL_VERSION        1U
#define ACCOUNT_TEST_CREDIT_CLEAR_MARKER 0xC109U
#define ACCOUNT_SAVE_DELAY_MS           200U

/* 新增实际入奖分项，双32字节槽。与0x1520/0x1540升级标记隔离。 */
#define ACCOUNT_COIN_PRIZE_SLOT0_ADDR   0x1580U
#define ACCOUNT_COIN_PRIZE_SLOT1_ADDR   0x15A0U
#define ACCOUNT_COIN_PRIZE_MAGIC        0x33525043UL
#define ACCOUNT_COIN_PRIZE_VERSION      1U

/*
 * C3彩票、C6卡片累计和各自历史换算价值使用独立双备份区。
 * - 版本1旧槽只用于迁移。
 * - 版本2旧槽位于0x0180~0x01DF，只保存彩票账目。
 * - 版本3新槽位于0x0300~0x037F，加入卡片账目和未完成吐卡任务，避开彩池区。
 */
#define ACCOUNT_TICKET_LEGACY_SLOT0_ADDR 0x0140U
#define ACCOUNT_TICKET_LEGACY_SLOT1_ADDR 0x0160U
#define ACCOUNT_TICKET_LEGACY_SIZE       32U
#define ACCOUNT_TICKET_V2_SLOT0_ADDR     0x0180U
#define ACCOUNT_TICKET_V2_SLOT1_ADDR     0x01B0U
#define ACCOUNT_TICKET_V2_RECORD_SIZE    48U
#define ACCOUNT_OUTPUT_SLOT0_ADDR        0x0300U
#define ACCOUNT_OUTPUT_SLOT1_ADDR        0x0340U
#define ACCOUNT_OUTPUT_RECORD_SIZE       64U
#define ACCOUNT_TICKET_MAGIC            0x544B4354UL
#define ACCOUNT_TICKET_LEGACY_VERSION   1U
#define ACCOUNT_TICKET_V2_VERSION       2U
#define ACCOUNT_OUTPUT_VERSION          3U

/*
 * C14/C15送灯账目使用独立双备份区，不改变已经投放机器上的64字节主账、
 * 彩票/卡片扩展记录格式。
 */
#define ACCOUNT_GIFT_SLOT0_ADDR          0x0380U
#define ACCOUNT_GIFT_SLOT1_ADDR          0x03A0U
#define ACCOUNT_GIFT_RECORD_SIZE         32U
#define ACCOUNT_GIFT_MAGIC               0x54464947UL
#define ACCOUNT_GIFT_VERSION             1U

/**
 * @brief EEPROM 里真正保存的 64 字节记录。
 *
 * 字段说明：
 * - magic：固定魔数，用来判断这块 EEPROM 是不是本程序写过的账目。
 * - version：结构版本，后续字段变化时可以升级。
 * - length：记录长度，当前固定 64 字节。
 * - sequence：保存序号，每成功保存一次加 1，用于两个槽之间比较新旧。
 * - game_xxx：掉电前显示在三组数码管上的业务值。
 * - xxx_total：累计账目。
 * - crc32：整条记录的 CRC32 校验值。计算 CRC 前本字段按 0 处理。
 * - game_round_total/lottery_count_total 放在旧版 reserved 区前 8 字节。
 *   这样旧 EEPROM 记录仍然能通过 CRC，旧记录里这两个新字段自然为 0。
 * - rtc_xxx：最后一次有效 RTC 时间，占用旧版 reserved 的 8 字节，不改变 64 字节记录长度。
 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint16_t game_score;
    uint16_t game_prize;
    uint16_t game_coin;
    uint16_t reserved0;
    uint32_t coin_in_total;
    uint32_t game_use_total;
    uint32_t line_ticket_total;
    uint32_t lottery_payout_total;
    uint32_t payout_total;
    uint32_t power_on_total;
    uint32_t crc32;
    uint32_t game_round_total;
    uint32_t lottery_count_total;
    uint16_t rtc_year;
    uint8_t rtc_month;
    uint8_t rtc_day;
    uint8_t rtc_hour;
    uint8_t rtc_minute;
    uint8_t rtc_second;
    uint8_t rtc_valid;
} Account_EepromRecord;

/* 旧主账格式不变：独立记录历史C1/C2基线，避免升级时丢失玩家余额或账目。 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t old_c1;
    uint32_t old_c2;
    uint32_t crc32;
    uint8_t reserved[8];
} Account_EpochRecord;
typedef char Account_EpochRecordSizeCheck[(sizeof(Account_EpochRecord) == 32U) ? 1 : -1];

/* 实体投币/吐币单独双备份，不能把旧主账 C1/C2 当成实体收支迁移。 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t coin_in;
    uint32_t coin_out;
    uint32_t crc32;
    uint8_t reserved[8];
} Account_PhysicalRecord;
typedef char Account_PhysicalRecordSizeCheck[
    (sizeof(Account_PhysicalRecord) == 32U) ? 1 : -1];

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t coin3_value;
    uint32_t coin5_value;
    uint32_t coin10_value;
    uint32_t reserved;
    uint32_t crc32;
} Account_CoinPrizeRecord;
typedef char Account_CoinPrizeRecordSizeCheck[
    (sizeof(Account_CoinPrizeRecord) == 32U) ? 1 : -1];

/**
 * @brief C3彩票、C6卡片和返奖换算值的独立EEPROM记录。
 *
 * 采用两个64字节槽轮流保存，掉电写坏一个槽时仍可读取另一个有效槽。
 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t ticket_out_total;
    uint32_t return_commitment_total;
    uint32_t ticket_payout_value_low;
    uint32_t ticket_payout_value_high;
    uint32_t card_out_total;
    uint32_t card_payout_value_low;
    uint32_t card_payout_value_high;
    uint32_t card_payout_pending;
    uint16_t conversion_p6;
    uint16_t conversion_remainder;
    uint16_t card_conversion_x;
    uint16_t card_conversion_y;
    uint16_t card_conversion_remainder;
    uint16_t card_pending_x;
    uint16_t card_pending_y;
    uint16_t reserved0;
    uint32_t crc32;
} Account_OutputRecord;

typedef char Account_OutputRecordSizeCheck[
    (sizeof(Account_OutputRecord) == ACCOUNT_OUTPUT_RECORD_SIZE) ? 1 : -1];

/* 版本2旧记录只读，用于把已经固化的彩票账目迁移到版本3新槽。 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t ticket_out_total;
    uint32_t return_commitment_total;
    uint32_t ticket_payout_value_low;
    uint32_t ticket_payout_value_high;
    uint16_t conversion_p6;
    uint16_t conversion_remainder;
    uint32_t crc32;
    uint8_t reserved[12];
} Account_TicketV2Record;

typedef char Account_TicketV2RecordSizeCheck[
    (sizeof(Account_TicketV2Record) == ACCOUNT_TICKET_V2_RECORD_SIZE) ? 1 : -1];

/* 已经投入过上一版固件的机器，从这两个32字节旧槽迁移C3和返奖承诺值。 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t ticket_out_total;
    uint32_t return_commitment_total;
    uint32_t crc32;
    uint8_t reserved[8];
} Account_TicketLegacyRecord;

typedef char Account_TicketLegacyRecordSizeCheck[
    (sizeof(Account_TicketLegacyRecord) == ACCOUNT_TICKET_LEGACY_SIZE) ? 1 : -1];

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint32_t gift_light_total;
    uint32_t gift_win_value_total;
    uint8_t reserved[8];
    uint32_t crc32;
} Account_GiftRecord;

typedef char Account_GiftRecordSizeCheck[
    (sizeof(Account_GiftRecord) == ACCOUNT_GIFT_RECORD_SIZE) ? 1 : -1];

/*
 * s_data 是 Service/account 的唯一账目 RAM 数据。
 *
 * 访问规则：
 * - account.c 内部可以直接改。
 * - 其它模块只能通过 Account_AddXxx()/Account_SetGameState() 修改。
 * - 查账页面只能通过 Account_GetData() 拿拷贝，不能直接改全局变量。
 */
static Account_Data s_data;
static uint8_t s_credit_transfer;
static uint16_t s_settlement_record;
static Account_RtcTime s_rtc_time;

/*
 * s_ready：EEPROM 当前是否可读写。
 * s_has_valid_record：上电时是否读到过有效记录。
 * s_dirty：RAM 数据是否已经变化但还没写入 EEPROM。
 * s_next_slot：下一次保存写哪个备份槽，0 表示槽 0，1 表示槽 1。
 * s_sequence：当前已保存记录的序号。
 * s_dirty_start_ms：第一次变 dirty 的时间，用来延迟合并保存。
 */
static uint8_t s_ready = 0U;
static uint8_t s_has_valid_record = 0U;
static uint8_t s_dirty = 0U;
static uint8_t s_next_slot = 0U;
static uint32_t s_sequence = 0U;
static uint32_t s_epoch_old_c1 = 0U;
static uint32_t s_epoch_old_c2 = 0U;
static uint32_t s_epoch_sequence = 0U;
static uint8_t s_epoch_next_slot = 0U;
static uint8_t s_epoch_valid = 0U;
static uint32_t s_physical_sequence = 0U;
static uint8_t s_physical_next_slot = 0U;
static uint8_t s_physical_dirty = 0U;
static uint32_t s_dirty_start_ms = 0U;
static uint8_t s_ticket_dirty = 0U;
static uint8_t s_ticket_next_slot = 0U;
static uint32_t s_ticket_sequence = 0U;
static uint16_t s_ticket_conversion_p6 = 0U;
static uint16_t s_ticket_conversion_remainder = 0U;
static uint8_t s_ticket_conversion_migration_pending = 0U;
static uint16_t s_card_conversion_x = 0U;
static uint16_t s_card_conversion_y = 0U;
static uint16_t s_card_conversion_remainder = 0U;
static uint32_t s_card_payout_pending = 0U;
static uint16_t s_card_pending_x = 0U;
static uint16_t s_card_pending_y = 0U;
static uint8_t s_gift_dirty = 0U;
static uint8_t s_gift_next_slot = 0U;
static uint32_t s_gift_sequence = 0U;
static uint32_t s_coin_prize_sequence = 0U;
static uint8_t s_coin_prize_next_slot = 0U;
static uint8_t s_coin_prize_dirty = 0U;

/**
 * @brief 对 32 位累计账目做饱和加法。
 *
 * @param value 被累加的账目变量。
 * @param add   本次要增加的数量。
 *
 * 逻辑说明：
 * - 账目累计值用 uint32_t，最大是 4294967295。
 * - 如果继续相加会溢出，就固定到最大值，不从 0 重新开始。
 */
/**
 * @brief 查账数据与持久化：校验（对应符号 Account_IsRtcTimeValid）。
 * @调用关系 直接调用者：account.c:Account_LoadRecord、account.c:Account_SetRtcTime。
 * @param time 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_IsRtcTimeValid(const Account_RtcTime *time)
{
    if (time == 0)
    {
        return 0U;
    }

    if (time->valid == 0U)
    {
        return 0U;
    }

    if ((time->year < 2024U) || (time->year > 2099U) ||
        (time->month < 1U) || (time->month > 12U) ||
        (time->day < 1U) || (time->day > 31U) ||
        (time->hour > 23U) ||
        (time->minute > 59U) ||
        (time->second > 59U))
    {
        return 0U;
    }

    return 1U;
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddSaturate）。
 * @调用关系 直接调用者：account.c:Account_LoadTicketData、account.c:Account_Init、account.c:Account_AddCoinIn、account.c:Account_AddGameUse、account.c:Account_AddLineTicket、account.c:Account_AddLotteryPayout、account.c:Account_AddGameRound、account.c:Account_AddLotteryCount。
 * @param value 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param add 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Account_AddSaturate(uint32_t *value, uint32_t add)
{
    if ((value == 0) || (add == 0U))
    {
        return;
    }

    if ((*value + add) < *value)
    {
        *value = 0xFFFFFFFFUL;
    }
    else
    {
        *value += add;
    }
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddSaturate64）。
 * @调用关系 直接调用者：account.c:Account_AddTicketOut、account.c:Account_AddCardOut。
 * @param value 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param add 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Account_AddSaturate64(uint64_t *value, uint64_t add)
{
    if ((value == 0) || (add == 0U))
    {
        return;
    }

    if ((*value + add) < *value)
    {
        *value = 0xFFFFFFFFFFFFFFFFULL;
    }
    else
    {
        *value += add;
    }
}

/**
 * @brief 记录 RAM 数据已经变化，后续需要写 EEPROM。
 *
 * 逻辑说明：
 * - 第一次变 dirty 时记录时间。
 * - 后续如果在 200ms 内又有投币、吐币或显示值变化，只继续保持 dirty，
 *   不重置起始时间，保证最多延迟 200ms 左右就会保存。
 */
/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_MarkDirty）。
 * @调用关系 直接调用者：account.c:Account_Init、account.c:Account_SetTicketConversionP6、account.c:Account_SetGameState、account.c:Account_SetRtcTime、account.c:Account_AddCoinIn、account.c:Account_AddGameUse、account.c:Account_RemoveGameUse、account.c:Account_AddLineTicket。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Account_MarkDirty(void)
{
    if (s_dirty == 0U)
    {
        s_dirty = 1U;
        s_dirty_start_ms = Gtim_NowMs();
    }
}

/**
 * @brief 计算一段内存的 CRC32。
 *
 * @param data 起始地址。
 * @param len  字节长度。
 *
 * @return CRC32 值。
 *
 * 逻辑说明：
 * - 这里使用常见的反射 CRC32 多项式 0xEDB88320。
 * - 每个字节进入 CRC 后，再逐 bit 推进 8 次。
 * - EEPROM 读回后重新算一次 CRC，如果不一致，说明这槽数据不完整或被破坏。
 */
/**
 * @brief 查账数据与持久化：计算 CRC（对应符号 Account_CalcCrc32）。
 * @调用关系 直接调用者：account.c:Account_RecordCrc、account.c:Account_OutputRecordCrc、account.c:Account_TicketV2RecordCrc、account.c:Account_TicketLegacyRecordCrc、account.c:Account_GiftRecordCrc。
 * @局部状态 主要临时变量：i、bit、crc；均只在本次调用中有效。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Account_CalcCrc32(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t bit;
    uint32_t crc = 0xFFFFFFFFUL;

    for (i = 0U; i < len; i++)
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
 * @brief 计算 EEPROM 记录的 CRC。
 *
 * @param record 待计算的记录。
 *
 * @return CRC32 值。
 *
 * 逻辑说明：
 * - crc32 字段本身不能参与自己的校验。
 * - 所以先复制一份临时记录，把 temp.crc32 清 0，再对完整 64 字节计算 CRC。
 */
/**
 * @brief 查账数据与持久化：计算 CRC（对应符号 Account_RecordCrc）。
 * @调用关系 直接调用者：account.c:Account_IsRecordValid、account.c:Account_BuildRecord。
 * @局部状态 主要临时变量：temp；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Account_RecordCrc(const Account_EepromRecord *record)
{
    Account_EepromRecord temp;

    temp = *record;
    temp.crc32 = 0U;

    return Account_CalcCrc32((const uint8_t *)&temp, (uint16_t)sizeof(temp));
}

static uint32_t Account_EpochCrc(const Account_EpochRecord *record)
{
    Account_EpochRecord temp = *record;
    temp.crc32 = 0U;
    return Account_CalcCrc32((const uint8_t *)&temp, (uint16_t)sizeof(temp));
}

static uint8_t Account_ReadEpoch(uint16_t addr, Account_EpochRecord *record)
{
    if (Eeprom_Read(addr, (uint8_t *)record, (uint16_t)sizeof(*record)) == 0U)
        return 0U;
    return ((record->magic == ACCOUNT_EPOCH_MAGIC) &&
            (record->version == ACCOUNT_EPOCH_VERSION) &&
            (record->length == sizeof(*record)) &&
            (record->crc32 == Account_EpochCrc(record))) ? 1U : 0U;
}

static uint8_t Account_SaveEpoch(void)
{
    Account_EpochRecord record;
    uint16_t addr;
    memset(&record, 0, sizeof(record));
    record.magic = ACCOUNT_EPOCH_MAGIC;
    record.version = ACCOUNT_EPOCH_VERSION;
    record.length = sizeof(record);
    record.sequence = s_epoch_sequence + 1U;
    record.old_c1 = s_epoch_old_c1;
    record.old_c2 = s_epoch_old_c2;
    record.crc32 = Account_EpochCrc(&record);
    addr = (s_epoch_next_slot == 0U) ? ACCOUNT_EPOCH_SLOT0_ADDR :
                                         ACCOUNT_EPOCH_SLOT1_ADDR;
    if (Eeprom_Write(addr, (const uint8_t *)&record, sizeof(record)) == 0U)
        return 0U;
    s_epoch_sequence = record.sequence;
    s_epoch_next_slot ^= 1U;
    s_epoch_valid = 1U;
    return 1U;
}

static uint8_t Account_LoadEpoch(void)
{
    Account_EpochRecord a, b;
    uint8_t va = Account_ReadEpoch(ACCOUNT_EPOCH_SLOT0_ADDR, &a);
    uint8_t vb = Account_ReadEpoch(ACCOUNT_EPOCH_SLOT1_ADDR, &b);
    const Account_EpochRecord *selected;
    if ((va == 0U) && (vb == 0U)) return 0U;
    if ((vb != 0U) && ((va == 0U) || (b.sequence >= a.sequence)))
    {
        selected = &b;
        s_epoch_next_slot = 0U;
    }
    else
    {
        selected = &a;
        s_epoch_next_slot = 1U;
    }
    s_epoch_old_c1 = selected->old_c1;
    s_epoch_old_c2 = selected->old_c2;
    s_epoch_sequence = selected->sequence;
    s_epoch_valid = 1U;
    return 1U;
}

/* 只统计实际成功追加的老虎机币奖；预选结果和未兑现的候选不计数。 */
static uint32_t Account_CoinPrizeCrc(const Account_CoinPrizeRecord *record)
{
    Account_CoinPrizeRecord copy = *record;
    copy.crc32 = 0U;
    return Account_CalcCrc32((const uint8_t *)&copy, sizeof(copy));
}

static uint8_t Account_CoinPrizeValid(const Account_CoinPrizeRecord *record)
{
    return ((record->magic == ACCOUNT_COIN_PRIZE_MAGIC) &&
            (record->version == ACCOUNT_COIN_PRIZE_VERSION) &&
            (record->length == sizeof(*record)) &&
            (record->crc32 == Account_CoinPrizeCrc(record))) ? 1U : 0U;
}

static uint8_t Account_CoinPrizeErased(const Account_CoinPrizeRecord *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint16_t i;
    for (i = 0U; i < sizeof(*record); i++)
        if (bytes[i] != 0xFFU) return 0U;
    return 1U;
}

static uint8_t Account_LoadCoinPrize(void)
{
    Account_CoinPrizeRecord a, b;
    const Account_CoinPrizeRecord *selected;
    uint8_t va, vb;
    if ((Eeprom_Read(ACCOUNT_COIN_PRIZE_SLOT0_ADDR, (uint8_t *)&a, sizeof(a)) == 0U) ||
        (Eeprom_Read(ACCOUNT_COIN_PRIZE_SLOT1_ADDR, (uint8_t *)&b, sizeof(b)) == 0U))
        return 0U;
    va = Account_CoinPrizeValid(&a);
    vb = Account_CoinPrizeValid(&b);
    if ((va == 0U) && (vb == 0U))
    {
        if ((Account_CoinPrizeErased(&a) == 0U) ||
            (Account_CoinPrizeErased(&b) == 0U))
        {
            Eeprom_ProtectData(ACCOUNT_COIN_PRIZE_SLOT0_ADDR);
            return 0U;
        }
        /* 老版本没有分项记录：不能用选奖次数伪造历史实际入奖分。 */
        s_coin_prize_dirty = 1U;
        return 1U;
    }
    if ((vb != 0U) && ((va == 0U) || ((int32_t)(b.sequence - a.sequence) > 0)))
    {
        selected = &b;
        s_coin_prize_next_slot = 0U;
    }
    else
    {
        selected = &a;
        s_coin_prize_next_slot = 1U;
    }
    s_data.lottery_coin3_value_total = selected->coin3_value;
    s_data.lottery_coin5_value_total = selected->coin5_value;
    s_data.lottery_coin10_value_total = selected->coin10_value;
    s_coin_prize_sequence = selected->sequence;
    return 1U;
}

static uint8_t Account_SaveCoinPrize(void)
{
    Account_CoinPrizeRecord record;
    uint16_t addr;
    if (s_coin_prize_dirty == 0U) return 1U;
    memset(&record, 0, sizeof(record));
    record.magic = ACCOUNT_COIN_PRIZE_MAGIC;
    record.version = ACCOUNT_COIN_PRIZE_VERSION;
    record.length = sizeof(record);
    record.sequence = s_coin_prize_sequence + 1U;
    if (record.sequence == 0U) record.sequence = 1U;
    record.coin3_value = s_data.lottery_coin3_value_total;
    record.coin5_value = s_data.lottery_coin5_value_total;
    record.coin10_value = s_data.lottery_coin10_value_total;
    record.crc32 = Account_CoinPrizeCrc(&record);
    addr = (s_coin_prize_next_slot == 0U) ? ACCOUNT_COIN_PRIZE_SLOT0_ADDR :
                                            ACCOUNT_COIN_PRIZE_SLOT1_ADDR;
    if (Eeprom_Write(addr, (const uint8_t *)&record, sizeof(record)) == 0U)
        return 0U;
    s_coin_prize_sequence = record.sequence;
    s_coin_prize_next_slot ^= 1U;
    s_coin_prize_dirty = 0U;
    return 1U;
}

static uint32_t Account_PhysicalCrc(const Account_PhysicalRecord *record)
{
    Account_PhysicalRecord temp = *record;
    temp.crc32 = 0U;
    return Account_CalcCrc32((const uint8_t *)&temp, (uint16_t)sizeof(temp));
}

static uint8_t Account_ReadPhysical(uint16_t addr, Account_PhysicalRecord *record)
{
    if (Eeprom_Read(addr, (uint8_t *)record, (uint16_t)sizeof(*record)) == 0U)
        return 0U;
    return ((record->magic == ACCOUNT_PHYSICAL_MAGIC) &&
            (record->version == ACCOUNT_PHYSICAL_VERSION) &&
            (record->length == sizeof(*record)) &&
            (record->crc32 == Account_PhysicalCrc(record))) ? 1U : 0U;
}

static uint8_t Account_LoadPhysical(void)
{
    Account_PhysicalRecord a, b;
    uint8_t va = Account_ReadPhysical(ACCOUNT_PHYSICAL_SLOT0_ADDR, &a);
    uint8_t vb = Account_ReadPhysical(ACCOUNT_PHYSICAL_SLOT1_ADDR, &b);
    const Account_PhysicalRecord *selected;
    if ((va == 0U) && (vb == 0U)) return 0U;
    if ((vb != 0U) && ((va == 0U) || (b.sequence >= a.sequence)))
    {
        selected = &b;
        s_physical_next_slot = 0U;
    }
    else
    {
        selected = &a;
        s_physical_next_slot = 1U;
    }
    s_data.physical_coin_in_total = selected->coin_in;
    s_data.physical_coin_out_total = selected->coin_out;
    s_physical_sequence = selected->sequence;
    return 1U;
}

static uint8_t Account_SavePhysical(void)
{
    Account_PhysicalRecord record;
    uint16_t addr;
    if (s_physical_dirty == 0U) return 1U;
    memset(&record, 0, sizeof(record));
    record.magic = ACCOUNT_PHYSICAL_MAGIC;
    record.version = ACCOUNT_PHYSICAL_VERSION;
    record.length = sizeof(record);
    record.sequence = s_physical_sequence + 1U;
    if (record.sequence == 0U) record.sequence = 1U;
    record.coin_in = s_data.physical_coin_in_total;
    record.coin_out = s_data.physical_coin_out_total;
    record.crc32 = Account_PhysicalCrc(&record);
    addr = (s_physical_next_slot == 0U) ? ACCOUNT_PHYSICAL_SLOT0_ADDR :
                                                ACCOUNT_PHYSICAL_SLOT1_ADDR;
    if (Eeprom_Write(addr, (const uint8_t *)&record, sizeof(record)) == 0U)
        return 0U;
    s_physical_sequence = record.sequence;
    s_physical_next_slot ^= 1U;
    s_physical_dirty = 0U;
    return 1U;
}

/**
 * @brief 查账数据与持久化：计算 CRC（对应符号 Account_OutputRecordCrc）。
 * @调用关系 直接调用者：account.c:Account_IsOutputRecordValid、account.c:Account_SaveTicketData。
 * @局部状态 主要临时变量：temp；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Account_OutputRecordCrc(const Account_OutputRecord *record)
{
    Account_OutputRecord temp;

    temp = *record;
    temp.crc32 = 0U;
    return Account_CalcCrc32((const uint8_t *)&temp, (uint16_t)sizeof(temp));
}

/**
 * @brief 查账数据与持久化：计算 CRC（对应符号 Account_TicketV2RecordCrc）。
 * @调用关系 直接调用者：account.c:Account_ReadTicketV2Slot。
 * @局部状态 主要临时变量：temp；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Account_TicketV2RecordCrc(const Account_TicketV2Record *record)
{
    Account_TicketV2Record temp;

    temp = *record;
    temp.crc32 = 0U;
    return Account_CalcCrc32((const uint8_t *)&temp, (uint16_t)sizeof(temp));
}

/**
 * @brief 查账数据与持久化：计算 CRC（对应符号 Account_TicketLegacyRecordCrc）。
 * @调用关系 直接调用者：account.c:Account_ReadTicketLegacySlot。
 * @局部状态 主要临时变量：temp；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Account_TicketLegacyRecordCrc(const Account_TicketLegacyRecord *record)
{
    Account_TicketLegacyRecord temp;

    temp = *record;
    temp.crc32 = 0U;
    return Account_CalcCrc32((const uint8_t *)&temp, (uint16_t)sizeof(temp));
}

/**
 * @brief 查账数据与持久化：计算 CRC（对应符号 Account_GiftRecordCrc）。
 * @调用关系 直接调用者：account.c:Account_IsGiftRecordValid、account.c:Account_SaveGiftData。
 * @局部状态 主要临时变量：temp；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Account_GiftRecordCrc(const Account_GiftRecord *record)
{
    Account_GiftRecord temp;

    temp = *record;
    temp.crc32 = 0U;
    return Account_CalcCrc32((const uint8_t *)&temp, (uint16_t)sizeof(temp));
}

/**
 * @brief 查账数据与持久化：校验（对应符号 Account_IsGiftRecordValid）。
 * @调用关系 直接调用者：account.c:Account_ReadGiftSlot。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_IsGiftRecordValid(const Account_GiftRecord *record)
{
    if ((record->magic != ACCOUNT_GIFT_MAGIC) ||
        (record->version != ACCOUNT_GIFT_VERSION) ||
        (record->length != ACCOUNT_GIFT_RECORD_SIZE))
    {
        return 0U;
    }

    return (record->crc32 == Account_GiftRecordCrc(record)) ? 1U : 0U;
}

/**
 * @brief 查账数据与持久化：读取（对应符号 Account_ReadGiftSlot）。
 * @调用关系 直接调用者：account.c:Account_LoadGiftData。
 * @param addr 按值传入；函数内部修改不会回写调用方变量。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_ReadGiftSlot(uint16_t addr, Account_GiftRecord *record)
{
    if (Eeprom_Read(addr, (uint8_t *)record, (uint16_t)sizeof(*record)) == 0U)
    {
        return 0U;
    }

    return Account_IsGiftRecordValid(record);
}

/**
 * @brief 查账数据与持久化：校验（对应符号 Account_IsOutputRecordValid）。
 * @调用关系 直接调用者：account.c:Account_ReadOutputSlot。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_IsOutputRecordValid(const Account_OutputRecord *record)
{
    if ((record->magic != ACCOUNT_TICKET_MAGIC) ||
        (record->version != ACCOUNT_OUTPUT_VERSION) ||
        (record->length != ACCOUNT_OUTPUT_RECORD_SIZE))
    {
        return 0U;
    }

    return (record->crc32 == Account_OutputRecordCrc(record)) ? 1U : 0U;
}

/**
 * @brief 查账数据与持久化：读取（对应符号 Account_ReadOutputSlot）。
 * @调用关系 直接调用者：account.c:Account_LoadTicketData。
 * @param addr 按值传入；函数内部修改不会回写调用方变量。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_ReadOutputSlot(uint16_t addr, Account_OutputRecord *record)
{
    if (Eeprom_Read(addr, (uint8_t *)record, (uint16_t)sizeof(*record)) == 0U)
    {
        return 0U;
    }

    return Account_IsOutputRecordValid(record);
}

/**
 * @brief 查账数据与持久化：读取（对应符号 Account_ReadTicketV2Slot）。
 * @调用关系 直接调用者：account.c:Account_LoadTicketData。
 * @param addr 按值传入；函数内部修改不会回写调用方变量。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_ReadTicketV2Slot(uint16_t addr,
                                        Account_TicketV2Record *record)
{
    if (Eeprom_Read(addr, (uint8_t *)record, (uint16_t)sizeof(*record)) == 0U)
    {
        return 0U;
    }

    if ((record->magic != ACCOUNT_TICKET_MAGIC) ||
        (record->version != ACCOUNT_TICKET_V2_VERSION) ||
        (record->length != ACCOUNT_TICKET_V2_RECORD_SIZE))
    {
        return 0U;
    }

    return (record->crc32 == Account_TicketV2RecordCrc(record)) ? 1U : 0U;
}

/**
 * @brief 查账数据与持久化：读取（对应符号 Account_ReadTicketLegacySlot）。
 * @调用关系 直接调用者：account.c:Account_LoadTicketData。
 * @param addr 按值传入；函数内部修改不会回写调用方变量。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_ReadTicketLegacySlot(uint16_t addr,
                                            Account_TicketLegacyRecord *record)
{
    if (Eeprom_Read(addr, (uint8_t *)record, (uint16_t)sizeof(*record)) == 0U)
    {
        return 0U;
    }

    if ((record->magic != ACCOUNT_TICKET_MAGIC) ||
        (record->version != ACCOUNT_TICKET_LEGACY_VERSION) ||
        (record->length != ACCOUNT_TICKET_LEGACY_SIZE))
    {
        return 0U;
    }

    return (record->crc32 == Account_TicketLegacyRecordCrc(record)) ? 1U : 0U;
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_LoadGiftData）。
 * @调用关系 直接调用者：account.c:Account_Init。
 * @局部状态 主要临时变量：slot0、slot1、selected、slot0_valid、slot1_valid；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Account_LoadGiftData(void)
{
    Account_GiftRecord slot0;
    Account_GiftRecord slot1;
    const Account_GiftRecord *selected = 0;
    uint8_t slot0_valid;
    uint8_t slot1_valid;

    s_data.gift_light_total = 0U;
    s_data.gift_win_value_total = 0U;
    s_gift_sequence = 0U;
    s_gift_next_slot = 0U;
    s_gift_dirty = 0U;

    slot0_valid = Account_ReadGiftSlot(ACCOUNT_GIFT_SLOT0_ADDR, &slot0);
    slot1_valid = Account_ReadGiftSlot(ACCOUNT_GIFT_SLOT1_ADDR, &slot1);

    if ((slot0_valid != 0U) &&
        ((slot1_valid == 0U) || (slot0.sequence >= slot1.sequence)))
    {
        selected = &slot0;
        s_gift_next_slot = 1U;
    }
    else if (slot1_valid != 0U)
    {
        selected = &slot1;
        s_gift_next_slot = 0U;
    }

    if (selected != 0)
    {
        s_data.gift_light_total = selected->gift_light_total;
        s_data.gift_win_value_total = selected->gift_win_value_total;
        s_gift_sequence = selected->sequence;
    }
    else
    {
        /* 首次升级到C14/C15时从0开始，并在本次初始化保存新格式。 */
        s_gift_dirty = 1U;
    }
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_SaveGiftData）。
 * @调用关系 直接调用者：account.c:Account_SaveNow。
 * @局部状态 主要临时变量：record、next_sequence、write_addr；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_SaveGiftData(void)
{
    Account_GiftRecord record;
    uint32_t next_sequence;
    uint16_t write_addr;

    if (s_gift_dirty == 0U)
    {
        return 1U;
    }

    next_sequence = s_gift_sequence + 1UL;
    if (next_sequence == 0UL)
    {
        next_sequence = 1UL;
    }

    memset(&record, 0, sizeof(record));
    record.magic = ACCOUNT_GIFT_MAGIC;
    record.version = ACCOUNT_GIFT_VERSION;
    record.length = ACCOUNT_GIFT_RECORD_SIZE;
    record.sequence = next_sequence;
    record.gift_light_total = s_data.gift_light_total;
    record.gift_win_value_total = s_data.gift_win_value_total;
    record.crc32 = Account_GiftRecordCrc(&record);

    write_addr = (s_gift_next_slot == 0U) ?
                 ACCOUNT_GIFT_SLOT0_ADDR : ACCOUNT_GIFT_SLOT1_ADDR;
    if (Eeprom_Write(write_addr,
                     (const uint8_t *)&record,
                     (uint16_t)sizeof(record)) == 0U)
    {
        return 0U;
    }

    s_gift_sequence = next_sequence;
    s_gift_next_slot = (s_gift_next_slot == 0U) ? 1U : 0U;
    s_gift_dirty = 0U;
    return 1U;
}

/** @brief 读取彩票/卡片扩展账目，并兼容迁移版本1和版本2旧记录。 */
/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_LoadTicketData）。
 * @调用关系 直接调用者：account.c:Account_Init。
 * @局部状态 主要临时变量：slot0、slot1、v2_slot0、v2_slot1、legacy0、legacy1、slot0_valid、slot1_valid、v2_slot0_valid、v2_slot1_valid、legacy0_valid、legacy1_valid、selected、v2_selected、legacy_selected；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Account_LoadTicketData(void)
{
    Account_OutputRecord slot0;
    Account_OutputRecord slot1;
    Account_TicketV2Record v2_slot0;
    Account_TicketV2Record v2_slot1;
    Account_TicketLegacyRecord legacy0;
    Account_TicketLegacyRecord legacy1;
    uint8_t slot0_valid;
    uint8_t slot1_valid;
    uint8_t v2_slot0_valid;
    uint8_t v2_slot1_valid;
    uint8_t legacy0_valid;
    uint8_t legacy1_valid;
    const Account_OutputRecord *selected;
    const Account_TicketV2Record *v2_selected;
    const Account_TicketLegacyRecord *legacy_selected;

    s_data.ticket_out_total = 0U;
    s_data.card_out_total = 0U;
    s_data.return_commitment_total = s_data.line_ticket_total;
    Account_AddSaturate(&s_data.return_commitment_total,
                        s_data.lottery_payout_total);
    s_data.ticket_payout_value_scaled = 0U;
    s_data.card_payout_value_scaled = 0U;
    s_ticket_sequence = 0U;
    s_ticket_next_slot = 0U;
    s_ticket_dirty = 0U;
    s_ticket_conversion_p6 = 0U;
    s_ticket_conversion_remainder = 0U;
    s_ticket_conversion_migration_pending = 1U;
    s_card_conversion_x = 0U;
    s_card_conversion_y = 0U;
    s_card_conversion_remainder = 0U;
    s_card_payout_pending = 0U;
    s_card_pending_x = 0U;
    s_card_pending_y = 0U;
    s_gift_dirty = 0U;
    s_gift_next_slot = 0U;
    s_gift_sequence = 0U;

    slot0_valid = Account_ReadOutputSlot(ACCOUNT_OUTPUT_SLOT0_ADDR, &slot0);
    slot1_valid = Account_ReadOutputSlot(ACCOUNT_OUTPUT_SLOT1_ADDR, &slot1);

    if ((slot0_valid != 0U) &&
        ((slot1_valid == 0U) || (slot0.sequence >= slot1.sequence)))
    {
        selected = &slot0;
        s_ticket_next_slot = 1U;
    }
    else if (slot1_valid != 0U)
    {
        selected = &slot1;
        s_ticket_next_slot = 0U;
    }
    else
    {
        selected = 0;
    }

    if (selected != 0)
    {
        s_data.ticket_out_total = selected->ticket_out_total;
        s_data.return_commitment_total = selected->return_commitment_total;
        s_data.ticket_payout_value_scaled =
            ((uint64_t)selected->ticket_payout_value_high << 32U) |
            (uint64_t)selected->ticket_payout_value_low;
        s_data.card_out_total = selected->card_out_total;
        s_data.card_payout_value_scaled =
            ((uint64_t)selected->card_payout_value_high << 32U) |
            (uint64_t)selected->card_payout_value_low;
        s_ticket_sequence = selected->sequence;
        s_ticket_conversion_p6 = selected->conversion_p6;
        s_ticket_conversion_remainder = selected->conversion_remainder;
        s_card_conversion_x = selected->card_conversion_x;
        s_card_conversion_y = selected->card_conversion_y;
        s_card_conversion_remainder = selected->card_conversion_remainder;
        s_card_payout_pending = selected->card_payout_pending;
        s_card_pending_x = selected->card_pending_x;
        s_card_pending_y = selected->card_pending_y;

        if ((s_ticket_conversion_p6 != 0U) &&
            (s_ticket_conversion_remainder < s_ticket_conversion_p6))
        {
            s_ticket_conversion_migration_pending = 0U;
        }
        else
        {
            /* 异常或迁移中掉电的记录，在Setting_Init后按保存的C3重新建立一次。 */
            s_data.ticket_payout_value_scaled = 0U;
            s_ticket_conversion_p6 = 0U;
            s_ticket_conversion_remainder = 0U;
            s_ticket_conversion_migration_pending = 1U;
            s_ticket_dirty = 1U;
        }

        if ((s_data.card_out_total != 0U) &&
            ((s_card_conversion_x == 0U) ||
             (s_card_conversion_y == 0U) ||
             (s_card_conversion_remainder >= s_card_conversion_y)))
        {
            /* CRC有效但卡片换算字段异常时保留C6，停止用错误比例继续累计。 */
            s_card_conversion_x = 0U;
            s_card_conversion_y = 0U;
            s_card_conversion_remainder = 0U;
            s_ticket_dirty = 1U;
        }

        if ((s_card_payout_pending != 0U) &&
            ((s_card_pending_x == 0U) || (s_card_pending_y == 0U)))
        {
            s_card_payout_pending = 0U;
            s_card_pending_x = 0U;
            s_card_pending_y = 0U;
            s_ticket_dirty = 1U;
        }
        return;
    }

    /* 没有版本3记录时，优先迁移已经固化过P6价值的版本2记录。 */
    v2_slot0_valid = Account_ReadTicketV2Slot(ACCOUNT_TICKET_V2_SLOT0_ADDR,
                                               &v2_slot0);
    v2_slot1_valid = Account_ReadTicketV2Slot(ACCOUNT_TICKET_V2_SLOT1_ADDR,
                                               &v2_slot1);
    if ((v2_slot0_valid != 0U) &&
        ((v2_slot1_valid == 0U) || (v2_slot0.sequence >= v2_slot1.sequence)))
    {
        v2_selected = &v2_slot0;
    }
    else if (v2_slot1_valid != 0U)
    {
        v2_selected = &v2_slot1;
    }
    else
    {
        v2_selected = 0;
    }

    if (v2_selected != 0)
    {
        s_data.ticket_out_total = v2_selected->ticket_out_total;
        s_data.return_commitment_total = v2_selected->return_commitment_total;
        s_data.ticket_payout_value_scaled =
            ((uint64_t)v2_selected->ticket_payout_value_high << 32U) |
            (uint64_t)v2_selected->ticket_payout_value_low;
        s_ticket_conversion_p6 = v2_selected->conversion_p6;
        s_ticket_conversion_remainder = v2_selected->conversion_remainder;

        if ((s_ticket_conversion_p6 != 0U) &&
            (s_ticket_conversion_remainder < s_ticket_conversion_p6))
        {
            s_ticket_conversion_migration_pending = 0U;
        }
        else
        {
            s_data.ticket_payout_value_scaled = 0U;
            s_ticket_conversion_p6 = 0U;
            s_ticket_conversion_remainder = 0U;
        }

        s_ticket_dirty = 1U;
        return;
    }

    legacy0_valid = Account_ReadTicketLegacySlot(
        ACCOUNT_TICKET_LEGACY_SLOT0_ADDR,
        &legacy0);
    legacy1_valid = Account_ReadTicketLegacySlot(
        ACCOUNT_TICKET_LEGACY_SLOT1_ADDR,
        &legacy1);

    if ((legacy0_valid != 0U) &&
        ((legacy1_valid == 0U) || (legacy0.sequence >= legacy1.sequence)))
    {
        legacy_selected = &legacy0;
    }
    else if (legacy1_valid != 0U)
    {
        legacy_selected = &legacy1;
    }
    else
    {
        legacy_selected = 0;
    }

    if (legacy_selected != 0)
    {
        s_data.ticket_out_total = legacy_selected->ticket_out_total;
        s_data.return_commitment_total = legacy_selected->return_commitment_total;
    }

    /* Setting_Init读出当前P6后，会把版本1旧C3按当时P6折算并写入版本3新槽。 */
    s_ticket_dirty = 1U;
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_SaveTicketData）。
 * @调用关系 直接调用者：account.c:Account_SaveNow。
 * @局部状态 主要临时变量：record、next_sequence、write_addr；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_SaveTicketData(void)
{
    Account_OutputRecord record;
    uint32_t next_sequence;
    uint16_t write_addr;

    if (s_ticket_dirty == 0U)
    {
        return 1U;
    }

    if (s_ticket_conversion_migration_pending != 0U)
    {
        return 1U;
    }

    next_sequence = s_ticket_sequence + 1UL;
    if (next_sequence == 0UL)
    {
        next_sequence = 1UL;
    }

    memset(&record, 0, sizeof(record));
    record.magic = ACCOUNT_TICKET_MAGIC;
    record.version = ACCOUNT_OUTPUT_VERSION;
    record.length = ACCOUNT_OUTPUT_RECORD_SIZE;
    record.sequence = next_sequence;
    record.ticket_out_total = s_data.ticket_out_total;
    record.return_commitment_total = s_data.return_commitment_total;
    record.ticket_payout_value_low =
        (uint32_t)s_data.ticket_payout_value_scaled;
    record.ticket_payout_value_high =
        (uint32_t)(s_data.ticket_payout_value_scaled >> 32U);
    record.card_out_total = s_data.card_out_total;
    record.card_payout_value_low =
        (uint32_t)s_data.card_payout_value_scaled;
    record.card_payout_value_high =
        (uint32_t)(s_data.card_payout_value_scaled >> 32U);
    record.card_payout_pending = s_card_payout_pending;
    record.conversion_p6 = s_ticket_conversion_p6;
    record.conversion_remainder = s_ticket_conversion_remainder;
    record.card_conversion_x = s_card_conversion_x;
    record.card_conversion_y = s_card_conversion_y;
    record.card_conversion_remainder = s_card_conversion_remainder;
    record.card_pending_x = s_card_pending_x;
    record.card_pending_y = s_card_pending_y;
    record.crc32 = Account_OutputRecordCrc(&record);

    write_addr = (s_ticket_next_slot == 0U) ?
                 ACCOUNT_OUTPUT_SLOT0_ADDR : ACCOUNT_OUTPUT_SLOT1_ADDR;
    if (Eeprom_Write(write_addr,
                     (const uint8_t *)&record,
                     (uint16_t)sizeof(record)) == 0U)
    {
        return 0U;
    }

    s_ticket_sequence = next_sequence;
    s_ticket_next_slot = (s_ticket_next_slot == 0U) ? 1U : 0U;
    s_ticket_dirty = 0U;
    return 1U;
}

/**
 * @brief 检查一条 EEPROM 记录是否有效。
 *
 * @param record 待检查记录。
 *
 * @return 1 表示有效；0 表示无效。
 */
/**
 * @brief 查账数据与持久化：校验（对应符号 Account_IsRecordValid）。
 * @调用关系 直接调用者：account.c:Account_ReadSlot。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_IsRecordValid(const Account_EepromRecord *record)
{
    if (record->magic != ACCOUNT_MAGIC)
    {
        return 0U;
    }

    if (record->version != ACCOUNT_VERSION)
    {
        return 0U;
    }

    if (record->length != ACCOUNT_RECORD_SIZE)
    {
        return 0U;
    }

    if (record->crc32 != Account_RecordCrc(record))
    {
        return 0U;
    }

    return 1U;
}

/**
 * @brief 从一个备份槽读取并校验记录。
 *
 * @param addr   EEPROM 起始地址。
 * @param record 接收记录。
 *
 * @return 1 表示读取并校验成功；0 表示失败。
 */
/**
 * @brief 查账数据与持久化：读取（对应符号 Account_ReadSlot）。
 * @调用关系 直接调用者：account.c:Account_Init。
 * @param addr 按值传入；函数内部修改不会回写调用方变量。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Account_ReadSlot(uint16_t addr, Account_EepromRecord *record)
{
    if (Eeprom_Read(addr, (uint8_t *)record, (uint16_t)sizeof(*record)) == 0U)
    {
        return 0U;
    }

    return Account_IsRecordValid(record);
}

/* A communication/CRC error is not proof of a new, empty EEPROM. */
static uint8_t Account_RecordIsErased(const Account_EepromRecord *record)
{
    const uint8_t *bytes = (const uint8_t *)record;
    uint16_t i;
    for (i = 0U; i < sizeof(*record); i++)
        if (bytes[i] != 0xFFU) return 0U;
    return 1U;
}

/**
 * @brief 把 EEPROM 记录内容加载到 s_data。
 *
 * @param record 已经校验通过的 EEPROM 记录。
 */
/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_LoadRecord）。
 * @调用关系 直接调用者：account.c:Account_Init。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Account_LoadRecord(const Account_EepromRecord *record)
{
    s_data.coin_in_total = record->coin_in_total;
    s_data.game_use_total = record->game_use_total;
    s_data.line_ticket_total = record->line_ticket_total;
    s_data.lottery_payout_total = record->lottery_payout_total;
    s_data.payout_total = record->payout_total;
    s_data.power_on_total = record->power_on_total;
    s_data.game_round_total = record->game_round_total;
    s_data.lottery_count_total = record->lottery_count_total;
    s_data.game_score = record->game_score;
    s_data.game_prize = record->game_prize;
    /* 右屏最多9999，bit15复用为中屏转游戏币标记，兼容旧记录的0标记。 */
    s_data.game_coin = record->game_coin & 0x7FFFU;
    s_credit_transfer = ((record->game_coin & 0x8000U) != 0U) ? 1U : 0U;
    /* 0xD1xx identifies the settlement journal in previously unused reserved0.
     * Old records (0xC109/zero) mean no recoverable settlement. */
    s_settlement_record = (((record->reserved0 & 0xFFE0U) == 0xD100U) ||
                           ((record->reserved0 & 0xFF00U) == 0xD200U)) ?
                          record->reserved0 : 0U;


    s_rtc_time.year = record->rtc_year;
    s_rtc_time.month = record->rtc_month;
    s_rtc_time.day = record->rtc_day;
    s_rtc_time.hour = record->rtc_hour;
    s_rtc_time.minute = record->rtc_minute;
    s_rtc_time.second = record->rtc_second;
    s_rtc_time.valid = record->rtc_valid;

    if (Account_IsRtcTimeValid(&s_rtc_time) == 0U)
    {
        memset(&s_rtc_time, 0, sizeof(s_rtc_time));
    }
}

/**
 * @brief 根据当前 s_data 组装一条 EEPROM 记录。
 *
 * @param record 待填写的记录。
 * @param sequence 本次保存序号。
 */
/**
 * @brief 查账数据与持久化：构造（对应符号 Account_BuildRecord）。
 * @调用关系 直接调用者：account.c:Account_SaveNow。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param sequence 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Account_BuildRecord(Account_EepromRecord *record, uint32_t sequence)
{
    memset(record, 0, sizeof(*record));

    record->magic = ACCOUNT_MAGIC;
    record->version = ACCOUNT_VERSION;
    record->length = ACCOUNT_RECORD_SIZE;
    record->sequence = sequence;
    record->game_score = s_data.game_score;
    record->game_prize = s_data.game_prize;
    record->game_coin = s_data.game_coin |
                        ((s_credit_transfer != 0U) ? 0x8000U : 0U);
    record->reserved0 = (s_settlement_record != 0U) ?
                        s_settlement_record : ACCOUNT_TEST_CREDIT_CLEAR_MARKER;
    record->coin_in_total = s_data.coin_in_total;
    record->game_use_total = s_data.game_use_total;
    record->line_ticket_total = s_data.line_ticket_total;
    record->lottery_payout_total = s_data.lottery_payout_total;
    record->payout_total = s_data.payout_total;
    record->power_on_total = s_data.power_on_total;
    record->game_round_total = s_data.game_round_total;
    record->lottery_count_total = s_data.lottery_count_total;
    record->rtc_year = s_rtc_time.year;
    record->rtc_month = s_rtc_time.month;
    record->rtc_day = s_rtc_time.day;
    record->rtc_hour = s_rtc_time.hour;
    record->rtc_minute = s_rtc_time.minute;
    record->rtc_second = s_rtc_time.second;
    record->rtc_valid = s_rtc_time.valid;
    record->crc32 = Account_RecordCrc(record);
}

/**
 * @brief 清空 RAM 账目为默认值。
 *
 * 逻辑说明：
 * - 只清 RAM，不主动擦 EEPROM。
 * - 第一次装新 EEPROM 或两槽都无效时使用。
 */
/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_LoadDefault）。
 * @调用关系 直接调用者：account.c:Account_Init。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Account_LoadDefault(void)
{
    memset(&s_data, 0, sizeof(s_data));
    s_credit_transfer = 0U;
    s_settlement_record = 0U;
    memset(&s_rtc_time, 0, sizeof(s_rtc_time));
}

/**
 * @brief 查账数据与持久化：初始化（对应符号 Account_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @局部状态 主要临时变量：slot0、slot1、slot0_valid、slot1_valid；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_Init(void)
{
    Account_EepromRecord slot0;
    Account_EepromRecord slot1;
    uint8_t slot0_valid;
    uint8_t slot1_valid;
    uint8_t physical_valid;

    Account_LoadDefault();

    s_ready = 0U;
    s_has_valid_record = 0U;
    s_dirty = 0U;
    s_next_slot = 0U;
    s_sequence = 0U;
    s_epoch_old_c1 = 0U;
    s_epoch_old_c2 = 0U;
    s_epoch_sequence = 0U;
    s_epoch_next_slot = 0U;
    s_epoch_valid = 0U;
    s_physical_sequence = 0U;
    s_physical_next_slot = 0U;
    s_physical_dirty = 0U;
    s_dirty_start_ms = Gtim_NowMs();
    s_ticket_dirty = 0U;
    s_ticket_next_slot = 0U;
    s_ticket_sequence = 0U;
    s_ticket_conversion_p6 = 0U;
    s_ticket_conversion_remainder = 0U;
    s_ticket_conversion_migration_pending = 0U;
    s_card_conversion_x = 0U;
    s_card_conversion_y = 0U;
    s_card_conversion_remainder = 0U;
    s_card_payout_pending = 0U;
    s_card_pending_x = 0U;
    s_card_pending_y = 0U;
    s_gift_dirty = 0U;
    s_gift_next_slot = 0U;
    s_gift_sequence = 0U;
    s_coin_prize_sequence = 0U;
    s_coin_prize_next_slot = 0U;
    s_coin_prize_dirty = 0U;

    if ((Eeprom_IsReady() == 0U) && (Eeprom_Check() == 0U))
    {
        /*
         * EEPROM 没有应答时，不把默认账目写回旧记录。
         * Eeprom_Init锁存存储故障，App_Loop显示E18并暂停游戏。
         */
        /* 无EEPROM时仍按本次上电的纯新口径运行，但明确不保证掉电保存。 */
        s_epoch_valid = 1U;
        return;
    }

    s_ready = 1U;

    memset(&slot0, 0, sizeof(slot0));
    memset(&slot1, 0, sizeof(slot1));
    slot0_valid = Account_ReadSlot(ACCOUNT_EEPROM_SLOT0_ADDR, &slot0);
    slot1_valid = Account_ReadSlot(ACCOUNT_EEPROM_SLOT1_ADDR, &slot1);

    if ((slot0_valid != 0U) && (slot1_valid != 0U))
    {
        if (slot0.sequence >= slot1.sequence)
        {
            Account_LoadRecord(&slot0);
            s_sequence = slot0.sequence;
            s_next_slot = 1U;
        }
        else
        {
            Account_LoadRecord(&slot1);
            s_sequence = slot1.sequence;
            s_next_slot = 0U;
        }
        s_has_valid_record = 1U;
    }
    else if (slot0_valid != 0U)
    {
        Account_LoadRecord(&slot0);
        s_sequence = slot0.sequence;
        s_next_slot = 1U;
        s_has_valid_record = 1U;
    }
    else if (slot1_valid != 0U)
    {
        Account_LoadRecord(&slot1);
        s_sequence = slot1.sequence;
        s_next_slot = 0U;
        s_has_valid_record = 1U;
    }
    else
    {
        /*
         * 双槽均无效时，只有成功读到两份全FF才属于可自动初始化的空片。
         * 读取失败或非空CRC损坏记录必须保留，不能覆盖为零账。
         */
        if ((Eeprom_GetFault() != 0U) ||
            (Account_RecordIsErased(&slot0) == 0U) ||
            (Account_RecordIsErased(&slot1) == 0U))
        {
            Eeprom_ProtectData(ACCOUNT_EEPROM_SLOT0_ADDR);
            s_ready = 0U;
            return;
        }
        Account_LoadDefault();
        s_sequence = 0U;
        s_next_slot = 0U;
    }

    if (s_has_valid_record != 0U)
    {
        Account_LoadTicketData();
        Account_LoadGiftData();
        if (Account_LoadCoinPrize() == 0U)
        {
            s_ready = 0U;
            return;
        }
    }
    else
    {
        /* 主账目从0重建时，同时用0覆盖可能残留的旧C3扩展记录。 */
        s_data.ticket_out_total = 0U;
        s_data.card_out_total = 0U;
        s_data.return_commitment_total = 0U;
        s_data.ticket_payout_value_scaled = 0U;
        s_data.card_payout_value_scaled = 0U;
        s_ticket_sequence = 0U;
        s_ticket_next_slot = 0U;
        s_ticket_conversion_p6 = 0U;
        s_ticket_conversion_remainder = 0U;
        s_ticket_conversion_migration_pending = 1U;
        s_card_conversion_x = 0U;
        s_card_conversion_y = 0U;
        s_card_conversion_remainder = 0U;
        s_card_payout_pending = 0U;
        s_card_pending_x = 0U;
        s_card_pending_y = 0U;
        s_data.gift_light_total = 0U;
        s_data.gift_win_value_total = 0U;
        s_gift_sequence = 0U;
        s_gift_next_slot = 0U;
        s_gift_dirty = 1U;
        s_ticket_dirty = 1U;
        s_coin_prize_dirty = 1U;
    }

    /* 先把旧流水保存为基线，再让新版C1/C2开始累计；旧主账一字节不改。
     * 若上次清账后意外断电，主账低于旧基线，则基线重置为0。 */
    if (Account_LoadEpoch() == 0U)
    {
        s_epoch_old_c1 = s_data.coin_in_total;
        s_epoch_old_c2 = s_data.payout_total;
        /* 首次升级连续写两槽；任一槽掉电写坏仍可从另一槽恢复基线。 */
        if (Account_SaveEpoch() == 0U) return;
        (void)Account_SaveEpoch();
    }
    else if ((s_data.coin_in_total < s_epoch_old_c1) ||
             (s_data.payout_total < s_epoch_old_c2))
    {
        s_epoch_old_c1 = 0U;
        s_epoch_old_c2 = 0U;
        if (Account_SaveEpoch() == 0U) return;
    }

    physical_valid = Account_LoadPhysical();
    if ((s_has_valid_record == 0U) || (physical_valid == 0U))
    {
        /* 首次升级没有可信的实体收支历史：从0单独开始，旧混合账仍保留。
         * 主账已整体丢失时不允许沿用孤立的实体收支扩展槽。 */
        s_data.physical_coin_in_total = 0U;
        s_data.physical_coin_out_total = 0U;
        s_physical_dirty = 1U;
    }

    Account_AddSaturate(&s_data.power_on_total, 1U);
    Account_MarkDirty();

    /*
     * 上电次数属于启动事实，初始化阶段立即写一次。
     * 如果此时写失败，dirty 会保留，后面 Account_Task() 还会继续尝试。
     */
    (void)Account_SaveNow();
}

/**
 * @brief 查账数据与持久化：周期处理（对应符号 Account_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_Task(void)
{
    if (s_dirty == 0U)
    {
        return;
    }

    if (Gtim_IsElapsed(s_dirty_start_ms, ACCOUNT_SAVE_DELAY_MS) == 0U)
    {
        return;
    }

    (void)Account_SaveNow();
}

/**
 * @brief 查账数据与持久化：读取（对应符号 Account_IsReady）。
 * @调用关系 直接调用者：game.c:Game_ProcessClearAccountRequest。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Account_IsReady(void)
{
    return s_ready;
}

/**
 * @brief 查账数据与持久化：校验（对应符号 Account_HasValidRecord）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Account_HasValidRecord(void)
{
    return s_has_valid_record;
}

/**
 * @brief 查账数据与持久化：读取（对应符号 Account_GetData）。
 * @调用关系 直接调用者：game.c:Game_ShowAccountProfit、game.c:Game_GetAccountPageValue、gift_algorithm.c:GiftAlgorithm_Select、machine_lock.c:MachineLock_GetCurrentProfit、machine_lock.c:MachineLock_CaptureQuery、remote_config.c:Remote_FillAccountValues。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_GetData(Account_Data *data)
{
    if (data == 0)
    {
        return;
    }

    *data = s_data;
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_CalcPayoutValueScaled）。
 * @调用关系 直接调用者：account.c:Account_CalcPayoutEquivalent、account.c:Account_CalcPayoutRatePercent。
 * @局部状态 主要临时变量：coin_value_scaled、payout_value_scaled；均只在本次调用中有效。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint64_t Account_CalcPayoutValueScaled(const Account_Data *data)
{
    if (data == 0)
    {
        return 0U;
    }
    /* C4在开奖时记一次；实体吐币、出票、吐卡仅属兑现，不再重复计入。 */
    return (uint64_t)Account_GetAwardSinceEpoch(data) *
           ACCOUNT_PAYOUT_VALUE_SCALE;
}

uint32_t Account_GetStakeSinceEpoch(const Account_Data *data)
{
    if ((data == 0) || (s_epoch_valid == 0U) ||
        (data->coin_in_total < s_epoch_old_c1)) return 0U;
    return data->coin_in_total - s_epoch_old_c1;
}

uint32_t Account_GetAwardSinceEpoch(const Account_Data *data)
{
    if ((data == 0) || (s_epoch_valid == 0U) ||
        (data->payout_total < s_epoch_old_c2)) return 0U;
    return data->payout_total - s_epoch_old_c2;
}

uint8_t Account_IsMixedLedger(void)
{
    return ((s_epoch_old_c1 != 0U) || (s_epoch_old_c2 != 0U)) ? 1U : 0U;
}

void Account_AddPhysicalCoinIn(uint32_t count)
{
    if (count == 0U) return;
    Account_AddSaturate(&s_data.physical_coin_in_total, count);
    s_physical_dirty = 1U;
    Account_MarkDirty();
}

void Account_AddPhysicalCoinOut(uint32_t count)
{
    if (count == 0U) return;
    Account_AddSaturate(&s_data.physical_coin_out_total, count);
    s_physical_dirty = 1U;
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_CalcPayoutEquivalent）。
 * @调用关系 直接调用者：gift_algorithm.c:GiftAlgorithm_GetCommitted。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint64_t Account_CalcPayoutEquivalent(const Account_Data *data)
{
    return Account_CalcPayoutValueScaled(data) /
           ACCOUNT_PAYOUT_VALUE_SCALE;
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_CalcPayoutRatePercent）。
 * @调用关系 直接调用者：game.c:Game_GetAccountPageValue、remote_config.c:Remote_FillAccountValues。
 * @局部状态 主要临时变量：payout_value_scaled、denominator、rate；均只在本次调用中有效。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Account_CalcPayoutRatePercent(const Account_Data *data)
{
    uint64_t payout_value_scaled;
    uint64_t denominator;
    uint64_t rate;

    if ((data == 0) || (Account_GetStakeSinceEpoch(data) == 0U))
    {
        return 0U;
    }

    payout_value_scaled = Account_CalcPayoutValueScaled(data);
    denominator = (uint64_t)Account_GetStakeSinceEpoch(data) *
                  ACCOUNT_PAYOUT_VALUE_SCALE;

    if (payout_value_scaled > (0xFFFFFFFFFFFFFFFFULL / 100U))
    {
        return 0xFFFFFFFFUL;
    }

    rate = (payout_value_scaled * 100U) / denominator;
    return (rate > 0xFFFFFFFFULL) ? 0xFFFFFFFFUL : (uint32_t)rate;
}

/**
 * @brief 查账数据与持久化：设置（对应符号 Account_SetTicketConversionP6）。
 * @调用关系 直接调用者：account.c:Account_AddTicketOut、game.c:Game_Init。
 * @局部状态 主要临时变量：numerator；均只在本次调用中有效。
 * @param tickets_per_coin 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_SetTicketConversionP6(uint16_t tickets_per_coin)
{
    uint64_t numerator;

    if (tickets_per_coin == 0U)
    {
        tickets_per_coin = 1U;
    }

    if (s_ticket_conversion_migration_pending != 0U)
    {
        numerator = (uint64_t)s_data.ticket_out_total *
                    ACCOUNT_PAYOUT_VALUE_SCALE;
        s_data.ticket_payout_value_scaled = numerator / tickets_per_coin;
        s_ticket_conversion_remainder =
            (uint16_t)(numerator % tickets_per_coin);
        s_ticket_conversion_p6 = tickets_per_coin;
        s_ticket_conversion_migration_pending = 0U;
        s_ticket_dirty = 1U;
        Account_MarkDirty();
        return;
    }

    if (s_ticket_conversion_p6 != tickets_per_coin)
    {
        /*
         * P6改变时只开启一个新换算段，已经累计的ticket_payout_value_scaled保持不变。
         * 旧段余数小于百万分之一币，不能带入新P6分母，因此在段切换时清零。
         */
        s_ticket_conversion_p6 = tickets_per_coin;
        s_ticket_conversion_remainder = 0U;
        s_ticket_dirty = 1U;
        Account_MarkDirty();
    }
}

/**
 * @brief 查账数据与持久化：读取（对应符号 Account_GetGameState）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @param score 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param prize 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param coin 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_GetGameState(uint16_t *score, uint16_t *prize, uint16_t *coin)
{
    if (score != 0)
    {
        *score = s_data.game_score;
    }

    if (prize != 0)
    {
        *prize = s_data.game_prize;
    }

    if (coin != 0)
    {
        *coin = s_data.game_coin;
    }
}

/**
 * @brief 查账数据与持久化：设置（对应符号 Account_SetGameState）。
 * @调用关系 直接调用者：game.c:Game_SetScore、game.c:Game_SetPrize、game.c:Game_SetCoin。
 * @param score 按值传入；函数内部修改不会回写调用方变量。
 * @param prize 按值传入；函数内部修改不会回写调用方变量。
 * @param coin 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_SetGameState(uint16_t score, uint16_t prize, uint16_t coin)
{
    if ((s_data.game_score == score) &&
        (s_data.game_prize == prize) &&
        (s_data.game_coin == coin))
    {
        return;
    }

    s_data.game_score = score;
    s_data.game_prize = prize;
    s_data.game_coin = coin;
    Account_MarkDirty();
}

void Account_SetCreditTransfer(uint8_t active)
{
    if (s_credit_transfer != active)
    {
        s_credit_transfer = active;
        Account_MarkDirty();
    }
}

uint8_t Account_IsCreditTransfer(void)
{
    return s_credit_transfer;
}

void Account_SetJackpotSettlement(uint8_t phase, uint8_t tier, uint8_t had_gift)
{
    uint16_t archived = (uint16_t)((uint16_t)Account_GetOrphanJackpotMask() << 5U);
    uint16_t value = (archived != 0U) ? (uint16_t)(0xD200U | archived) : 0U;
    if ((phase >= 1U) && (phase <= 3U) && (tier >= 5U) && (tier <= 7U))
    {
        value = (uint16_t)(((archived != 0U) ? 0xD200U : 0xD100U) | archived |
                          phase | ((tier - 4U) << 2U) |
                          ((had_gift != 0U) ? 0x10U : 0U));
    }
    if (s_settlement_record != value)
    {
        s_settlement_record = value;
        Account_MarkDirty();
    }
}

uint8_t Account_GetJackpotSettlement(uint8_t *tier, uint8_t *had_gift)
{
    uint8_t phase = (uint8_t)(s_settlement_record & 3U);
    uint8_t saved_tier = (uint8_t)((s_settlement_record >> 2U) & 3U);
    if (tier != 0) *tier = (saved_tier != 0U) ? (uint8_t)(saved_tier + 4U) : 0U;
    if (had_gift != 0) *had_gift = (uint8_t)((s_settlement_record >> 4U) & 1U);
    return (saved_tier != 0U) ? phase : 0U;
}

uint8_t Account_GetOrphanJackpotMask(void)
{
    return ((s_settlement_record & 0xFF00U) == 0xD200U) ?
        (uint8_t)((s_settlement_record >> 5U) & 7U) : 0U;
}

void Account_ArchiveOrphanJackpot(uint8_t tier)
{
    uint8_t mask = Account_GetOrphanJackpotMask();
    if ((tier < 5U) || (tier > 7U)) return;
    mask |= (uint8_t)(1U << (tier - 5U));
    /* 与清除活动WAIT同一主账CRC记录，断电只可能看到旧WAIT或完整隔离标志。
     * 不把未知彩金伪装成已发放，也不根据可能过期的本机彩金余额自行补奖。 */
    s_settlement_record = (uint16_t)(0xD200U | ((uint16_t)mask << 5U));
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：读取（对应符号 Account_GetRtcTime）。
 * @调用关系 直接调用者：game.c:Game_LoadAccountTimeForDisplay、game.c:Game_RestoreRtcFromAccount、game.c:Game_RtcBackupTask。
 * @param time 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_GetRtcTime(Account_RtcTime *time)
{
    if (time == 0)
    {
        return;
    }

    *time = s_rtc_time;
}

/**
 * @brief 查账数据与持久化：设置（对应符号 Account_SetRtcTime）。
 * @调用关系 直接调用者：game.c:Game_SaveRtcTimeToAccount、remote_config.c:Remote_SetTime。
 * @param time 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_SetRtcTime(const Account_RtcTime *time)
{
    if (Account_IsRtcTimeValid(time) == 0U)
    {
        return;
    }

    if ((s_rtc_time.year == time->year) &&
        (s_rtc_time.month == time->month) &&
        (s_rtc_time.day == time->day) &&
        (s_rtc_time.hour == time->hour) &&
        (s_rtc_time.minute == time->minute) &&
        (s_rtc_time.second == time->second) &&
        (s_rtc_time.valid == time->valid))
    {
        return;
    }

    s_rtc_time = *time;
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddCoinIn）。
 * @调用关系 直接调用者：game.c:Game_ProcessCoinCount。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddCoinIn(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.coin_in_total, count);
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddGameUse）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddGameUse(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.game_use_total, count);
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_RemoveGameUse）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_RemoveGameUse(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    if (count >= s_data.game_use_total)
    {
        s_data.game_use_total = 0U;
    }
    else
    {
        s_data.game_use_total -= count;
    }
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddLineTicket）。
 * @调用关系 直接调用者：game.c:Game_AddLinePrizeAndHold。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddLineTicket(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.line_ticket_total, count);
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddLotteryPayout）。
 * @调用关系 直接调用者：game.c:Game_AddLotteryPrizeAndPayout。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddLotteryPayout(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.lottery_payout_total, count);
    /* 本函数仅在Game_AddLotteryPrizeAndPayout成功入队后调用一次。
     * 回到右屏、实际吐币、退右屏余额都不再调用，避免循环模式重复记奖。 */
    if (count == 3U)
        Account_AddSaturate(&s_data.lottery_coin3_value_total, count);
    else if (count == 5U)
        Account_AddSaturate(&s_data.lottery_coin5_value_total, count);
    else if (count == 10U)
        Account_AddSaturate(&s_data.lottery_coin10_value_total, count);
    s_coin_prize_dirty = 1U;
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddGameRound）。
 * @调用关系 直接调用者：game.c:Game_FinishLineRound、game.c:Game_Task。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddGameRound(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.game_round_total, count);
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddLotteryCount）。
 * @调用关系 直接调用者：lottery.c:Lottery_Request。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddLotteryCount(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.lottery_count_total, count);
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddGiftLightCount）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddGiftLightCount(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.gift_light_total, count);
    s_gift_dirty = 1U;
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddGiftWinValue）。
 * @调用关系 直接调用者：game.c:Game_CompleteLineSettlement、game.c:Game_Task。
 * @param coin_value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddGiftWinValue(uint32_t coin_value)
{
    if (coin_value == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.gift_win_value_total, coin_value);
    s_gift_dirty = 1U;
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddTotalPayout）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddTotalPayout(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.payout_total, count);
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddTicketOut）。
 * @调用关系 直接调用者：game.c:Game_TrySkipTicketOutput、game.c:Game_Task。
 * @局部状态 主要临时变量：numerator、value_add；均只在本次调用中有效。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @param tickets_per_coin 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddTicketOut(uint32_t count, uint16_t tickets_per_coin)
{
    uint64_t numerator;
    uint64_t value_add;

    if (count == 0U)
    {
        return;
    }

    Account_SetTicketConversionP6(tickets_per_coin);
    numerator = ((uint64_t)count * ACCOUNT_PAYOUT_VALUE_SCALE) +
                s_ticket_conversion_remainder;
    value_add = numerator / s_ticket_conversion_p6;
    s_ticket_conversion_remainder =
        (uint16_t)(numerator % s_ticket_conversion_p6);

    Account_AddSaturate(&s_data.ticket_out_total, count);
    Account_AddSaturate64(&s_data.ticket_payout_value_scaled, value_add);
    s_ticket_dirty = 1U;
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddCardOut）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：numerator、value_add；均只在本次调用中有效。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @param coins_per_batch 按值传入；函数内部修改不会回写调用方变量。
 * @param cards_per_batch 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddCardOut(uint32_t count,
                        uint16_t coins_per_batch,
                        uint16_t cards_per_batch)
{
    uint64_t numerator;
    uint64_t value_add;

    if ((count == 0U) || (coins_per_batch == 0U) || (cards_per_batch == 0U))
    {
        return;
    }

    if ((s_card_conversion_x != coins_per_batch) ||
        (s_card_conversion_y != cards_per_batch))
    {
        /* 开启新的P15换算段，历史等值币保持不变，旧段不足百万分之一币的余数舍去。 */
        s_card_conversion_x = coins_per_batch;
        s_card_conversion_y = cards_per_batch;
        s_card_conversion_remainder = 0U;
    }

    numerator = ((uint64_t)count * (uint64_t)coins_per_batch *
                 ACCOUNT_PAYOUT_VALUE_SCALE) +
                s_card_conversion_remainder;
    value_add = numerator / cards_per_batch;
    s_card_conversion_remainder = (uint16_t)(numerator % cards_per_batch);

    Account_AddSaturate(&s_data.card_out_total, count);
    Account_AddSaturate64(&s_data.card_payout_value_scaled, value_add);
    s_ticket_dirty = 1U;
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：设置（对应符号 Account_SetCardPayoutPending）。
 * @调用关系 直接调用者：game.c:Game_StartCardPayout、game.c:Game_Task。
 * @param card_count 按值传入；函数内部修改不会回写调用方变量。
 * @param coins_per_batch 按值传入；函数内部修改不会回写调用方变量。
 * @param cards_per_batch 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_SetCardPayoutPending(uint32_t card_count,
                                  uint16_t coins_per_batch,
                                  uint16_t cards_per_batch)
{
    if (card_count == 0U)
    {
        coins_per_batch = 0U;
        cards_per_batch = 0U;
    }
    else if ((coins_per_batch == 0U) || (cards_per_batch == 0U))
    {
        return;
    }

    if ((s_card_payout_pending == card_count) &&
        (s_card_pending_x == coins_per_batch) &&
        (s_card_pending_y == cards_per_batch))
    {
        return;
    }

    s_card_payout_pending = card_count;
    s_card_pending_x = coins_per_batch;
    s_card_pending_y = cards_per_batch;
    s_ticket_dirty = 1U;
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：读取（对应符号 Account_GetCardPayoutPending）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @param card_count 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param coins_per_batch 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param cards_per_batch 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_GetCardPayoutPending(uint32_t *card_count,
                                  uint16_t *coins_per_batch,
                                  uint16_t *cards_per_batch)
{
    if (card_count != 0)
    {
        *card_count = s_card_payout_pending;
    }
    if (coins_per_batch != 0)
    {
        *coins_per_batch = s_card_pending_x;
    }
    if (cards_per_batch != 0)
    {
        *cards_per_batch = s_card_pending_y;
    }
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_AddReturnCommitment）。
 * @调用关系 直接调用者：game.c:Game_RecordReturnCommitment。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_AddReturnCommitment(uint32_t count)
{
    if (count == 0U)
    {
        return;
    }

    Account_AddSaturate(&s_data.return_commitment_total, count);
    s_ticket_dirty = 1U;
    Account_MarkDirty();
}

/**
 * @brief 查账数据与持久化：清除（对应符号 Account_ClearBookData）。
 * @调用关系 直接调用者：game.c:Game_ProcessClearAccountRequest。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Account_ClearBookData(void)
{
    uint32_t before_sequence = s_sequence;
    /*
     * P8 清除账目只清查账累计值，不清游戏现场值。
     * 这样玩家当前左屏得分、中屏待吐币数、右屏游戏分不会被设置菜单误清。
     */
    s_data.coin_in_total = 0U;
    s_data.game_use_total = 0U;
    s_data.line_ticket_total = 0U;
    s_data.lottery_payout_total = 0U;
    s_data.lottery_coin3_value_total = 0U;
    s_data.lottery_coin5_value_total = 0U;
    s_data.lottery_coin10_value_total = 0U;
    s_coin_prize_dirty = 1U;
    s_data.payout_total = 0U;
    s_data.ticket_out_total = 0U;
    s_data.card_out_total = 0U;
    s_data.return_commitment_total = 0U;
    s_data.ticket_payout_value_scaled = 0U;
    s_data.card_payout_value_scaled = 0U;
    s_ticket_conversion_remainder = 0U;
    s_card_conversion_x = 0U;
    s_card_conversion_y = 0U;
    s_card_conversion_remainder = 0U;
    s_data.power_on_total = 0U;
    s_data.game_round_total = 0U;
    s_data.lottery_count_total = 0U;
    s_data.gift_light_total = 0U;
    s_data.gift_win_value_total = 0U;
    s_data.physical_coin_in_total = 0U;
    s_data.physical_coin_out_total = 0U;
    Jackpot_ClearLocalPayoutTotal();
    s_ticket_dirty = 1U;
    s_gift_dirty = 1U;
    s_physical_dirty = 1U;
    Account_MarkDirty();
    /* 主账可能已写成功而附属出票/送灯记录失败；序号变化也意味着
     * C1/C2=0 已经落盘，此时必须同步重置基线，不能等下次重启。 */
    if ((Account_SaveNow() != 0U) || (s_sequence != before_sequence))
    {
        /* 清账后重新开始单一口径，原C1/C2历史已由操作者主动清除。 */
        s_epoch_old_c1 = 0U;
        s_epoch_old_c2 = 0U;
        (void)Account_SaveEpoch();
    }
}

uint8_t Account_ResetForUpgrade(void)
{
    Account_ClearBookData();
    if (Account_IsReady() == 0U) return 0U;
    Account_SetGameState(0U, 0U, 0U);
    Account_SetCreditTransfer(0U);
    Account_SetCardPayoutPending(0U, 0U, 0U);
    /* 升级明确放弃未完成的彩金及历史异常等待，不允许重启重放。 */
    s_settlement_record = 0U;
    Account_MarkDirty();
    return Account_SaveNow();
}

/**
 * @brief 查账数据与持久化：执行内部处理（对应符号 Account_SaveNow）。
 * @调用关系 直接调用者：account.c:Account_Init、account.c:Account_Task、account.c:Account_ClearBookData、game.c:Game_AdjustAccountTime、game.c:Game_StartCardPayout。
 * @局部状态 主要临时变量：record、next_sequence、write_addr；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Account_SaveNow(void)
{
    Account_EepromRecord record;
    uint32_t next_sequence;
    uint16_t write_addr;

    if ((Eeprom_IsReady() == 0U) && (Eeprom_Check() == 0U))
    {
        s_ready = 0U;
        return 0U;
    }

    s_ready = 1U;

    next_sequence = s_sequence + 1UL;
    if (next_sequence == 0UL)
    {
        next_sequence = 1UL;
    }

    Account_BuildRecord(&record, next_sequence);

    if (s_next_slot == 0U)
    {
        write_addr = ACCOUNT_EEPROM_SLOT0_ADDR;
    }
    else
    {
        write_addr = ACCOUNT_EEPROM_SLOT1_ADDR;
    }

    if (Eeprom_Write(write_addr, (const uint8_t *)&record, (uint16_t)sizeof(record)) == 0U)
    {
        s_ready = 0U;
        return 0U;
    }

    s_sequence = next_sequence;
    s_next_slot = (s_next_slot == 0U) ? 1U : 0U;
    s_has_valid_record = 1U;

    if (Account_SaveTicketData() == 0U)
    {
        /* 主账已保存但C3扩展区失败，保留dirty让后台继续重试。 */
        s_ready = 0U;
        return 0U;
    }

    if (Account_SaveGiftData() == 0U)
    {
        /* C14/C15扩展区失败时保留dirty，后台下一轮继续重试。 */
        s_ready = 0U;
        return 0U;
    }

    if (Account_SavePhysical() == 0U)
    {
        s_ready = 0U;
        return 0U;
    }

    if (Account_SaveCoinPrize() == 0U)
    {
        s_ready = 0U;
        return 0U;
    }

    s_dirty = 0U;

    return 1U;
}
