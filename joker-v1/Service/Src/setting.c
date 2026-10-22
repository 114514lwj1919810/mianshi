/* AUTO-FILE-DOC
 * 文件职责：参数设置与持久化。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "setting.h"

#include "audio_manager.h"
#include "eeprom.h"
#include "machine_address.h"
#include <stddef.h>
#include <string.h>

#define SETTING_EEPROM_ADDR             0x0100U
/* 算法日志 0x0400..0x13FF 后的独立64字节镜像。旧固件只读主槽，
 * 新固件写镜像再写主槽，任一写入中途断电仍可恢复一份有效设置。 */
#define SETTING_EEPROM_MIRROR_ADDR      0x1400U
/* 现场P14/P15各留两份16字节记录；更新写非活动槽，掉电仍有旧值可读。 */
#define SETTING_EXTENSION_SLOT0_ADDR    0x00C0U
#define SETTING_EXTENSION_SLOT1_ADDR    0x00D0U
#define SETTING_MAGIC                   0x31544753UL
#define SETTING_VERSION                 3U
#define SETTING_PREVIOUS_VERSION        2U
#define SETTING_LEGACY_VERSION          1U
#define SETTING_RECORD_LEN              64U
#define SETTING_STORED_PARAM_COUNT      12U
#define SETTING_VOLUME_LEGACY_PERCENT_MARK 0xA5U
#define SETTING_VOLUME_LEVEL_MARK       0xA6U
#define SETTING_BACKGROUND_VOLUME_MARK  0xB6U
#define SETTING_JACKPOT_RESERVED_MARK   0x5AU
#define SETTING_CARD_RATIO_RESERVED_MARK 0xC4U
#define SETTING_STANDBY_SOUND_RESERVED_MARK 0xD6U
#define SETTING_JACKPOT_CONTRIBUTION_RESERVED_MARK 0xE7U
#define SETTING_MAJOR_DRAW_RESERVED_MARK 0xD2U
#define SETTING_MAJOR_POOL_STAKE_RESERVED_MARK 0xD3U
#define SETTING_EXTENSION_MAGIC         0x31584553UL
#define SETTING_EXTENSION_VERSION       3U
#define SETTING_EXTENSION_PREVIOUS_VERSION 2U
#define SETTING_EXTENSION_LEGACY_VERSION 1U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint16_t value[SETTING_STORED_PARAM_COUNT];
    uint32_t checksum;
    uint8_t reserved[28];
} Setting_Record;

typedef char Setting_RecordSizeMustBe64[
    (sizeof(Setting_Record) == SETTING_RECORD_LEN) ? 1 : -1];

/*
 * V3使用原0x00C0及空闲0x00D0两槽，generation区分新旧。
 * V1/V2仍可读，升级后写入V3；主设置记录和其他EEPROM区域不搬动。
 */
typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint8_t immediate_award;
    uint8_t immediate_award_inverse;
    uint8_t recovery_drop;
    uint8_t generation; /* V1/V2此字节为recovery_drop_inverse */
    uint32_t checksum;
} Setting_ExtensionRecord;

typedef char Setting_ExtensionRecordSizeMustBe16[
    (sizeof(Setting_ExtensionRecord) == 16U) ? 1 : -1];

/*
 * P1~P29 默认值。
 *
 * 默认表：
 * - P1=1：出币模式。
 * - P2=1：允许按出票/兑换键把得分兑换出去。
 * - P3=1：投 1 个币增加 1 个游戏分。
 * - P4=1：1 个币就能形成 1 个游戏分；如果调成 2/3，则 2/3 个币才加 1 分。
 * - 内部P5（现场P3）=95：正账返奖率95%，允许在85%~98%之间调整。
 * - P6=35：P1=2只吐票模式下一币对应35张票；P1=3时使用1~10张/币。
 * - 内部P7（现场P5）=2：综合返奖率五档预留值1~5，计划1档最高、5档最低，当前不参与开奖算法。
 * - P8=0：不清除账目；调成1后等待出奖全部结束，再清账、换新奖池并自动回0。
 * - P9=0：不恢复出厂；调成 1 后退出设置生效。
 * - P10=1：开启电磁铁光眼检测（此项不在现场菜单显示）。
 * - P11=1：中奖 20 以上允许周围 LED 氛围灯闪烁。
 * - P12=25：开始键再次有效间隔为 2.5 秒。
 * - P13中屏=2：背景音乐KT404C音量，0静音、30最大。
 * - P13右屏=10：音效KT404C音量，0静音、30最大。
 * - P14=20：彩池最低保留值。
 * - P15=1001：按x*100+y编码，表示默认10币吐1卡。
 * - P16=1：开启待机专用016音效；关闭时不影响正常游戏音效和背景音乐。
 * - P17=100：每累计100个实际游戏押分，公共彩金增加1分。
 * - P29右屏=0：默认机台号0；中屏单机/联机状态由机器地址记录单独保存。
 * - 内部下标17（现场P16）=140：D池实时余额达到140币开始抽高连线，允许100~500，步长10。
 * - 内部下标18（现场P17）=30：正账每押30币从C22普通预算转1币到D池，允许10~200。
 * - 下标19~27是已废弃返奖参数的空槽，固定为0且不再保存。
 * - P14(现场归并页)=1：中奖得分按原流程即玩即吐；0为先转游戏币、再手动兑奖。
 * - P15(现场归并页)=15：负账返奖率默认降低15个百分点，允许0~100；有效比例最低0%。
 */
static const uint16_t s_setting_default_value[SETTING_PARAM_COUNT] =
{
    1U, 1U, 1U, 1U, 95U, 35U, 2U, 0U, 0U, 1U, 1U, 25U,
    10U, 20U, 1001U, 1U, 100U,
    SETTING_MAJOR_DRAW_COINS_DEFAULT,
    SETTING_MAJOR_POOL_STAKE_DEFAULT,
    0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 1U,
    SETTING_RECOVERY_DROP_DEFAULT
};

static uint16_t s_setting_value[SETTING_PARAM_COUNT];
static uint16_t s_background_volume = 2U;
static uint8_t s_setting_ready = 0U;
static uint8_t s_primary_record_valid = 0U;
static uint8_t s_extension_present = 0U;
static uint8_t s_extension_active_slot = 0U;
static uint8_t s_extension_generation = 0U;

static uint8_t Setting_BytesAreErased(const void *data, uint16_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint16_t i;
    for (i = 0U; i < length; i++)
        if (bytes[i] != 0xFFU) return 0U;
    return 1U;
}

static uint32_t Setting_CalcExtensionChecksum(
    const Setting_ExtensionRecord *record)
{
    Setting_ExtensionRecord copy;
    const uint8_t *bytes;
    uint16_t i;
    uint32_t checksum = 2166136261UL;

    copy = *record;
    copy.checksum = 0U;
    bytes = (const uint8_t *)&copy;
    for (i = 0U; i < (uint16_t)sizeof(copy); i++)
    {
        checksum = (checksum * 33U) + bytes[i];
    }
    return checksum;
}

static uint8_t Setting_IsExtensionRecordValid(
    const Setting_ExtensionRecord *record)
{
    if ((record->magic != SETTING_EXTENSION_MAGIC) ||
        ((record->version != SETTING_EXTENSION_VERSION) &&
         (record->version != SETTING_EXTENSION_PREVIOUS_VERSION) &&
         (record->version != SETTING_EXTENSION_LEGACY_VERSION)) ||
        (record->length != (uint16_t)sizeof(*record)) ||
        (record->immediate_award > 1U) ||
        (record->immediate_award_inverse !=
         (uint8_t)(record->immediate_award ^ 0xFFU)))
    {
        return 0U;
    }
    if ((record->version == SETTING_EXTENSION_PREVIOUS_VERSION) &&
        ((record->recovery_drop > SETTING_RECOVERY_DROP_MAX) ||
         (record->generation != (uint8_t)(record->recovery_drop ^ 0xFFU))))
        return 0U;
    if ((record->version == SETTING_EXTENSION_VERSION) &&
        (record->recovery_drop > SETTING_RECOVERY_DROP_MAX))
        return 0U;
    return (record->checksum == Setting_CalcExtensionChecksum(record)) ? 1U : 0U;
}

static void Setting_BuildExtensionRecord(Setting_ExtensionRecord *record)
{
    memset(record, 0, sizeof(*record));
    record->magic = SETTING_EXTENSION_MAGIC;
    record->version = SETTING_EXTENSION_VERSION;
    record->length = (uint16_t)sizeof(*record);
    record->immediate_award =
        (uint8_t)s_setting_value[SETTING_PARAM_IMMEDIATE_AWARD];
    record->immediate_award_inverse =
        (uint8_t)(record->immediate_award ^ 0xFFU);
    record->recovery_drop = (uint8_t)s_setting_value[SETTING_PARAM_RECOVERY_DROP];
    record->generation = (uint8_t)(s_extension_generation + 1U);
    record->checksum = Setting_CalcExtensionChecksum(record);
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsTicketRatioCompatible）。
 * @调用关系 直接调用者：setting.c:Setting_NormalizeAwardTicketPair、setting.c:Setting_SetValue。
 * @param award_mode 按值传入；函数内部修改不会回写调用方变量。
 * @param tickets_per_coin 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Setting_IsTicketRatioCompatible(uint16_t award_mode,
                                               uint16_t tickets_per_coin)
{
    if (award_mode == SETTING_AWARD_MODE_TICKET)
    {
        return ((tickets_per_coin >= SETTING_TICKET_ONLY_TICKETS_MIN) &&
                (tickets_per_coin <= SETTING_TICKET_ONLY_TICKETS_MAX)) ? 1U : 0U;
    }

    if (award_mode == SETTING_AWARD_MODE_COIN_AND_TICKET)
    {
        return ((tickets_per_coin >= SETTING_COMBINED_TICKETS_MIN) &&
                (tickets_per_coin <= SETTING_COMBINED_TICKETS_MAX)) ? 1U : 0U;
    }

    return 1U;
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetTicketRatioDefaultForMode）。
 * @调用关系 直接调用者：setting.c:Setting_NormalizeAwardTicketPair、setting.c:Setting_SetValue。
 * @param award_mode 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Setting_GetTicketRatioDefaultForMode(uint16_t award_mode)
{
    return (award_mode == SETTING_AWARD_MODE_COIN_AND_TICKET) ?
           SETTING_COMBINED_TICKETS_DEFAULT :
           SETTING_TICKET_ONLY_TICKETS_DEFAULT;
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_NormalizeAwardTicketPair）。
 * @调用关系 直接调用者：setting.c:Setting_NormalizeAll。
 * @局部状态 主要临时变量：award_mode、tickets_per_coin；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Setting_NormalizeAwardTicketPair(void)
{
    uint16_t award_mode;
    uint16_t tickets_per_coin;

    award_mode = s_setting_value[SETTING_PARAM_AWARD_MODE];
    tickets_per_coin = s_setting_value[SETTING_PARAM_TICKETS_PER_COIN];
    if (Setting_IsTicketRatioCompatible(award_mode, tickets_per_coin) != 0U)
    {
        return 0U;
    }

    s_setting_value[SETTING_PARAM_TICKETS_PER_COIN] =
        Setting_GetTicketRatioDefaultForMode(award_mode);
    return 1U;
}

/**
 * @brief 判断某个 P 参数值是否在产品表格允许范围内。
 *
 * @param index 参数下标，0 表示 P1，11 表示 P12。
 * @param value 待检查的参数值。
 *
 * @return 1 表示合法；0 表示不合法。
 */
/**
 * @brief  判断指定参数值是否在产品允许范围内
 * @param  index 参数下标，范围：0~30
 * @param  value 待检查参数值，无量纲
 * @retval 1 合法；0 不合法
 */
static uint8_t Setting_IsValueAllowed(uint8_t index, uint16_t value)
{
    if (index == SETTING_PARAM_RECOVERY_DROP)
        return (value <= SETTING_RECOVERY_DROP_MAX) ? 1U : 0U;
    if (index == SETTING_PARAM_MAJOR_DRAW_COINS)
        return ((value >= SETTING_MAJOR_DRAW_COINS_MIN) &&
                (value <= SETTING_MAJOR_DRAW_COINS_MAX) &&
                (((value - SETTING_MAJOR_DRAW_COINS_MIN) %
                  SETTING_MAJOR_DRAW_COINS_STEP) == 0U)) ? 1U : 0U;
    if (index == SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL)
        return ((value >= SETTING_MAJOR_POOL_STAKE_MIN) &&
                (value <= SETTING_MAJOR_POOL_STAKE_MAX)) ? 1U : 0U;
    switch (index)
    {
        case 0U:
            return ((value >= 1U) && (value <= 4U)) ? 1U : 0U;

        case 1U:
        case 7U:
        case 8U:
        case 10U:
        case SETTING_PARAM_STANDBY_SOUND_ENABLE:
        case SETTING_PARAM_IMMEDIATE_AWARD:
            return (value <= 1U) ? 1U : 0U;

        case 2U:
            return ((value >= 1U) && (value <= 10U)) ? 1U : 0U;

        case 3U:
            return ((value >= 1U) && (value <= 5U)) ? 1U : 0U;

        case SETTING_PARAM_LOTTERY_DIFFICULTY:
            return ((value >= SETTING_LOTTERY_DIFFICULTY_MIN) &&
                    (value <= SETTING_LOTTERY_DIFFICULTY_MAX)) ? 1U : 0U;

        case 4U:
            return ((value >= 85U) && (value <= 98U)) ? 1U : 0U;

        case 5U:
            return (((value >= 1U) && (value <= 10U)) ||
                    ((value >= 24U) && (value <= 50U))) ? 1U : 0U;

        case 9U:
            return (value <= 2U) ? 1U : 0U;

        case 11U:
            return ((value >= 10U) && (value <= 30U)) ? 1U : 0U;

        case 12U:
            return (value <= 30U) ? 1U : 0U;

        case 13U:
            return (value <= 999U) ? 1U : 0U;

        case SETTING_PARAM_CARD_RATIO:
        {
            uint16_t coins = value / SETTING_CARD_RATIO_FACTOR; // 每批投入币数，单位：币
            uint16_t cards = value % SETTING_CARD_RATIO_FACTOR; // 每批输出卡数，单位：张

            return ((coins >= SETTING_CARD_RATIO_MIN) &&
                    (coins <= SETTING_CARD_RATIO_MAX) &&
                    (cards >= SETTING_CARD_RATIO_MIN) &&
                    (cards <= SETTING_CARD_RATIO_MAX)) ? 1U : 0U;
        }

        case SETTING_PARAM_JACKPOT_COINS_PER_POINT:
            return ((value >= SETTING_JACKPOT_COINS_PER_POINT_MIN) &&
                    (value <= SETTING_JACKPOT_COINS_PER_POINT_MAX) &&
                    ((value % SETTING_JACKPOT_COINS_PER_POINT_STEP) == 0U)) ? 1U : 0U;

        case SETTING_PARAM_RS485_ADDRESS:
            return (value <= MACHINE_ADDRESS_MAX_ID) ? 1U : 0U;

        default:
            return 0U;
    }
}

/**
 * @brief 把异常设置值修正到产品表格允许值。
 *
 * @param index 参数下标，0 表示 P1，11 表示 P12。
 * @param value EEPROM 读到或外部传入的值。
 *
 * @return 合法参数值。
 *
 * 逻辑说明：
 * - 枚举型参数如果不合法，直接回到该 P 的默认值。
 * - 范围型参数如果低于下限取下限，高于上限取上限。
 * - P6 有 1~10 和 24~50 两段；落在 11~23 时按距离吸附到 10 或 24。
 */
/**
 * @brief  将参数值限制到产品允许范围
 * @param  index 参数下标，范围：0~30
 * @param  value EEPROM或外部传入值，无量纲
 * @retval 修正后的合法参数值
 */
static uint16_t Setting_NormalizeValue(uint8_t index, uint16_t value)
{
    if (index == SETTING_PARAM_RECOVERY_DROP)
        return (value <= SETTING_RECOVERY_DROP_MAX) ? value : SETTING_RECOVERY_DROP_MAX;
    if (index == SETTING_PARAM_MAJOR_DRAW_COINS)
    {
        if (value < SETTING_MAJOR_DRAW_COINS_MIN)
            return SETTING_MAJOR_DRAW_COINS_MIN;
        if (value > SETTING_MAJOR_DRAW_COINS_MAX)
            return SETTING_MAJOR_DRAW_COINS_MAX;
        return (uint16_t)(SETTING_MAJOR_DRAW_COINS_MIN +
                          (((value - SETTING_MAJOR_DRAW_COINS_MIN) /
                            SETTING_MAJOR_DRAW_COINS_STEP) *
                           SETTING_MAJOR_DRAW_COINS_STEP));
    }
    if (index == SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL)
    {
        if (value < SETTING_MAJOR_POOL_STAKE_MIN)
            return SETTING_MAJOR_POOL_STAKE_MIN;
        if (value > SETTING_MAJOR_POOL_STAKE_MAX)
            return SETTING_MAJOR_POOL_STAKE_MAX;
        return value;
    }
    if (Setting_IsValueAllowed(index, value) != 0U)
    {
        return value;
    }

    switch (index)
    {
        case 2U:
            if (value < 1U)
            {
                return 1U;
            }
            if (value > 10U)
            {
                return 10U;
            }
            break;

        case 3U:
            if (value < 1U)
            {
                return 1U;
            }
            if (value > 5U)
            {
                return 5U;
            }
            break;

        case SETTING_PARAM_LOTTERY_DIFFICULTY:
            if (value < SETTING_LOTTERY_DIFFICULTY_MIN)
            {
                return SETTING_LOTTERY_DIFFICULTY_MIN;
            }
            if (value > SETTING_LOTTERY_DIFFICULTY_MAX)
            {
                return SETTING_LOTTERY_DIFFICULTY_MAX;
            }
            break;

        case 4U:
            if (value < 85U)
            {
                return 85U;
            }
            if (value > 98U)
            {
                return 98U;
            }
            break;

        case 5U:
            if (value < 1U)
            {
                return 1U;
            }
            if (value <= 10U)
            {
                return value;
            }
            if (value < 24U)
            {
                return ((uint16_t)(value - 10U) < (uint16_t)(24U - value)) ? 10U : 24U;
            }
            if (value > 50U)
            {
                return 50U;
            }
            break;

        case 11U:
            if (value < 10U)
            {
                return 10U;
            }
            if (value > 30U)
            {
                return 30U;
            }
            break;

        case 12U:
            if (value > 30U)
            {
                return 30U;
            }
            break;

        case 13U:
            if (value > 999U)
            {
                return 999U;
            }
            break;

        case SETTING_PARAM_CARD_RATIO:
            return s_setting_default_value[SETTING_PARAM_CARD_RATIO];

        case SETTING_PARAM_JACKPOT_COINS_PER_POINT:
            if (value < SETTING_JACKPOT_COINS_PER_POINT_MIN)
            {
                return SETTING_JACKPOT_COINS_PER_POINT_MIN;
            }
            if (value > SETTING_JACKPOT_COINS_PER_POINT_MAX)
            {
                return SETTING_JACKPOT_COINS_PER_POINT_MAX;
            }
            return s_setting_default_value[
                SETTING_PARAM_JACKPOT_COINS_PER_POINT];

        default:
            break;
    }

    return s_setting_default_value[index];
}

/**
 * @brief 检查并修正全部 P1~P12。
 *
 * @return 1 表示修正过至少一个参数；0 表示原本全部合法。
 */
/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_NormalizeAll）。
 * @调用关系 直接调用者：setting.c:Setting_Init。
 * @局部状态 主要临时变量：i、changed、normalized；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Setting_NormalizeAll(void)
{
    uint8_t i;
    uint8_t changed = 0U;
    uint16_t normalized;

    for (i = 0U; i < SETTING_PARAM_COUNT; i++)
    {
        normalized = Setting_NormalizeValue(i, s_setting_value[i]);
        if (normalized != s_setting_value[i])
        {
            s_setting_value[i] = normalized;
            changed = 1U;
        }
    }

    if (Setting_NormalizeAwardTicketPair() != 0U)
    {
        changed = 1U;
    }

    return changed;
}

/**
 * @brief 计算设置记录校验值。
 *
 * @param record 要校验的 64 字节设置记录。
 *
 * @return 32 位校验值。
 *
 * 逻辑说明：
 * - 先复制一份记录，把 checksum 字段临时清 0。
 * - 再逐字节做滚动累加。
 * - 这样任何一个 P 参数、魔数、版本或保留字节变化，校验值都会变化。
 */
/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_CalcChecksum）。
 * @调用关系 直接调用者：setting.c:Setting_IsRecordValid、setting.c:Setting_BuildRecord。
 * @局部状态 主要临时变量：temp、byte_ptr、i、checksum；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Setting_CalcChecksum(const Setting_Record *record)
{
    Setting_Record temp;
    const uint8_t *byte_ptr;
    uint16_t i;
    uint32_t checksum = 2166136261UL;

    temp = *record;
    temp.checksum = 0U;
    byte_ptr = (const uint8_t *)&temp;

    for (i = 0U; i < (uint16_t)sizeof(Setting_Record); i++)
    {
        checksum = (checksum * 33U) + byte_ptr[i];
    }

    return checksum;
}

/**
 * @brief 把默认 P 参数复制到 RAM。
 *
 * 调用位置：
 * - Setting_Init() 入口先调用，保证 EEPROM 无效时也有可用参数。
 */
/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_LoadDefault）。
 * @调用关系 直接调用者：setting.c:Setting_Init、setting.c:Setting_RestoreDefault。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Setting_LoadDefault(void)
{
    uint8_t i;

    for (i = 0U; i < SETTING_PARAM_COUNT; i++)
    {
        s_setting_value[i] = s_setting_default_value[i];
    }
    s_background_volume = 2U;
}

/**
 * @brief 判断 EEPROM 里读出的设置记录是否有效。
 *
 * @param record 待读取记录。
 *
 * @return 1 表示有效；0 表示无效。
 */
/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsRecordValid）。
 * @调用关系 直接调用者：setting.c:Setting_Init。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Setting_IsRecordValid(const Setting_Record *record)
{
    if (record == 0)
    {
        return 0U;
    }

    if (record->magic != SETTING_MAGIC)
    {
        return 0U;
    }

    if ((record->version != SETTING_VERSION) &&
        (record->version != SETTING_PREVIOUS_VERSION) &&
        (record->version != SETTING_LEGACY_VERSION))
    {
        return 0U;
    }

    if (record->length != SETTING_RECORD_LEN)
    {
        return 0U;
    }

    if (record->checksum != Setting_CalcChecksum(record))
    {
        return 0U;
    }

    return 1U;
}

/**
 * @brief 把有效 EEPROM 记录复制到 RAM 参数表。
 *
 * @param record 已经校验通过的设置记录。
 */
/**
 * @brief  把有效EEPROM记录复制并规范化到RAM参数表
 * @param  record 只读设置记录；不得为NULL
 * @retval None
 */
static void Setting_CopyRecordToRam(const Setting_Record *record)
{
    uint8_t i;                                     // 参数索引，数组下标
    uint16_t stored_volume;                       // 已保存音效音量，范围：0~100或0~30
    uint16_t stored_background_volume;            // 已保存背景音量，范围：0~30
    uint16_t stored_major_draw_coins;             // 已保存D池机会门槛，单位：币
    uint16_t stored_major_pool_stake;             // 已保存D池押分入池间隔，单位：币

    for (i = 0U; i < SETTING_STORED_PARAM_COUNT; i++)
    {
        s_setting_value[i] = record->value[i];
    }
    /* Destructive menu actions are current-session requests, never boot-time
     * settings. Do not replay a stale 1 saved before a reset or old corruption. */
    s_setting_value[SETTING_PARAM_CLEAR_ACCOUNT] = 0U;
    s_setting_value[SETTING_PARAM_FACTORY_RESET] = 0U;

    /* 版本1中的P7是彩票连线比例，升级后不能把旧值5误当成最高难度。 */
    if (record->version == SETTING_LEGACY_VERSION)
    {
        s_setting_value[SETTING_PARAM_LOTTERY_DIFFICULTY] =
            SETTING_LOTTERY_DIFFICULTY_DEFAULT;
    }

    stored_volume = (uint16_t)record->reserved[0] |
                    ((uint16_t)record->reserved[1] << 8U);

    if (record->reserved[2] == SETTING_VOLUME_LEVEL_MARK)
    {
        s_setting_value[SETTING_PARAM_VOLUME] = stored_volume;
    }
    else if (record->reserved[2] == SETTING_VOLUME_LEGACY_PERCENT_MARK)
    {
        /*
         * 旧固件P13使用0~100百分比。升级时只换算一次：
         * 旧50对应新15，旧100对应新30，旧0仍为静音。
         */
        if (stored_volume == 0U)
        {
            s_setting_value[SETTING_PARAM_VOLUME] = 0U;
        }
        else
        {
            if (stored_volume > 100U)
            {
                stored_volume = 100U;
            }
            s_setting_value[SETTING_PARAM_VOLUME] =
                (uint16_t)(1U + (((stored_volume - 1U) * 29U + 49U) / 99U));
        }
    }
    else
    {
        s_setting_value[SETTING_PARAM_VOLUME] = s_setting_default_value[SETTING_PARAM_VOLUME];
    }

    stored_background_volume = (uint16_t)record->reserved[9] |
                               ((uint16_t)record->reserved[10] << 8U);
    if (record->reserved[11] == SETTING_BACKGROUND_VOLUME_MARK)
    {
        s_background_volume =
            Setting_NormalizeValue(SETTING_PARAM_VOLUME,
                                   stored_background_volume);
    }
    else
    {
        /*
         * 旧记录只有一个P13。首次升级时让背景音乐和音效都继承旧音量，
         * 随后按新格式写回，避免升级后某一路突然变响或静音。
         */
        s_background_volume = s_setting_value[SETTING_PARAM_VOLUME];
    }

    if (record->reserved[5] == SETTING_JACKPOT_RESERVED_MARK)
    {
        s_setting_value[SETTING_PARAM_JACKPOT_BASE] = (uint16_t)record->reserved[3] |
                                                      ((uint16_t)record->reserved[4] << 8U);
    }
    else
    {
        s_setting_value[SETTING_PARAM_JACKPOT_BASE] = s_setting_default_value[SETTING_PARAM_JACKPOT_BASE];
    }

    if (record->reserved[8] == SETTING_CARD_RATIO_RESERVED_MARK)
    {
        s_setting_value[SETTING_PARAM_CARD_RATIO] =
            ((uint16_t)record->reserved[6] * SETTING_CARD_RATIO_FACTOR) +
            (uint16_t)record->reserved[7];
    }
    else
    {
        s_setting_value[SETTING_PARAM_CARD_RATIO] =
            s_setting_default_value[SETTING_PARAM_CARD_RATIO];
    }

    if (record->reserved[13] == SETTING_STANDBY_SOUND_RESERVED_MARK)
    {
        s_setting_value[SETTING_PARAM_STANDBY_SOUND_ENABLE] =
            record->reserved[12];
    }
    else
    {
        s_setting_value[SETTING_PARAM_STANDBY_SOUND_ENABLE] =
            s_setting_default_value[SETTING_PARAM_STANDBY_SOUND_ENABLE];
    }

    if (record->reserved[16] == SETTING_JACKPOT_CONTRIBUTION_RESERVED_MARK)
    {
        s_setting_value[SETTING_PARAM_JACKPOT_COINS_PER_POINT] =
            (uint16_t)record->reserved[14] |
            ((uint16_t)record->reserved[15] << 8U);
    }
    else
    {
        s_setting_value[SETTING_PARAM_JACKPOT_COINS_PER_POINT] =
            s_setting_default_value[SETTING_PARAM_JACKPOT_COINS_PER_POINT];
    }

    stored_major_draw_coins = (uint16_t)record->reserved[17] |
                              ((uint16_t)record->reserved[18] << 8U);
    if (record->reserved[19] == SETTING_MAJOR_DRAW_RESERVED_MARK)
    {
        s_setting_value[SETTING_PARAM_MAJOR_DRAW_COINS] =
            Setting_NormalizeValue(SETTING_PARAM_MAJOR_DRAW_COINS,
                                   stored_major_draw_coins);
    }
    else
    {
        s_setting_value[SETTING_PARAM_MAJOR_DRAW_COINS] =
            SETTING_MAJOR_DRAW_COINS_DEFAULT;
    }

    stored_major_pool_stake = (uint16_t)record->reserved[20] |
                              ((uint16_t)record->reserved[21] << 8U);
    s_setting_value[SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL] =
        (record->reserved[22] == SETTING_MAJOR_POOL_STAKE_RESERVED_MARK) ?
        Setting_NormalizeValue(SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL,
                               stored_major_pool_stake) :
        SETTING_MAJOR_POOL_STAKE_DEFAULT;

    s_setting_value[SETTING_PARAM_RS485_ADDRESS] =
        MachineAddress_GetModeValue();
}

/**
 * @brief 根据当前 RAM 参数表生成一份 EEPROM 记录。
 *
 * @param record 调用者提供的记录缓冲。
 */
/**
 * @brief  根据当前RAM参数生成一份EEPROM设置记录
 * @param  record 输出记录；不得为NULL
 * @retval None
 */
static void Setting_BuildRecord(Setting_Record *record)
{
    uint8_t i;                                     // 参数索引，数组下标

    memset(record, 0, sizeof(Setting_Record));
    record->magic = SETTING_MAGIC;
    record->version = SETTING_VERSION;
    record->length = SETTING_RECORD_LEN;

    for (i = 0U; i < SETTING_STORED_PARAM_COUNT; i++)
    {
        record->value[i] = s_setting_value[i];
    }
    record->value[SETTING_PARAM_CLEAR_ACCOUNT] = 0U;
    record->value[SETTING_PARAM_FACTORY_RESET] = 0U;

    record->reserved[0] = (uint8_t)s_setting_value[SETTING_PARAM_VOLUME];
    record->reserved[1] = (uint8_t)(s_setting_value[SETTING_PARAM_VOLUME] >> 8U);
    record->reserved[2] = SETTING_VOLUME_LEVEL_MARK;
    record->reserved[3] = (uint8_t)s_setting_value[SETTING_PARAM_JACKPOT_BASE];
    record->reserved[4] = (uint8_t)(s_setting_value[SETTING_PARAM_JACKPOT_BASE] >> 8U);
    record->reserved[5] = SETTING_JACKPOT_RESERVED_MARK;
    record->reserved[6] = (uint8_t)(s_setting_value[SETTING_PARAM_CARD_RATIO] /
                                    SETTING_CARD_RATIO_FACTOR);
    record->reserved[7] = (uint8_t)(s_setting_value[SETTING_PARAM_CARD_RATIO] %
                                    SETTING_CARD_RATIO_FACTOR);
    record->reserved[8] = SETTING_CARD_RATIO_RESERVED_MARK;
    record->reserved[9] = (uint8_t)s_background_volume;
    record->reserved[10] = (uint8_t)(s_background_volume >> 8U);
    record->reserved[11] = SETTING_BACKGROUND_VOLUME_MARK;
    record->reserved[12] =
        (uint8_t)s_setting_value[SETTING_PARAM_STANDBY_SOUND_ENABLE];
    record->reserved[13] = SETTING_STANDBY_SOUND_RESERVED_MARK;
    record->reserved[14] =
        (uint8_t)s_setting_value[SETTING_PARAM_JACKPOT_COINS_PER_POINT];
    record->reserved[15] =
        (uint8_t)(s_setting_value[SETTING_PARAM_JACKPOT_COINS_PER_POINT] >> 8U);
    record->reserved[16] = SETTING_JACKPOT_CONTRIBUTION_RESERVED_MARK;
    record->reserved[17] =
        (uint8_t)s_setting_value[SETTING_PARAM_MAJOR_DRAW_COINS];
    record->reserved[18] =
        (uint8_t)(s_setting_value[SETTING_PARAM_MAJOR_DRAW_COINS] >> 8U);
    record->reserved[19] = SETTING_MAJOR_DRAW_RESERVED_MARK;
    record->reserved[20] =
        (uint8_t)s_setting_value[SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL];
    record->reserved[21] = (uint8_t)(
        s_setting_value[SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL] >> 8U);
    record->reserved[22] = SETTING_MAJOR_POOL_STAKE_RESERVED_MARK;
    record->checksum = Setting_CalcChecksum(record);
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_ApplyVolume）。
 * @调用关系 直接调用者：setting.c:Setting_Init、setting.c:Setting_RestoreDefault、setting.c:Setting_SetValue、setting.c:Setting_AdjustBackgroundVolume、setting.c:Setting_SetVolumePair。
 * @局部状态 主要临时变量：effect_volume、background_volume；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Setting_ApplyVolume(void)
{
    uint16_t effect_volume;
    uint16_t background_volume;

    effect_volume = Setting_NormalizeValue(
        SETTING_PARAM_VOLUME,
        s_setting_value[SETTING_PARAM_VOLUME]);
    background_volume = Setting_NormalizeValue(
        SETTING_PARAM_VOLUME,
        s_background_volume);

    Audio_SetVolumeLevelOn(AUDIO_CHIP_1, (uint8_t)effect_volume);
    Audio_SetVolumeLevelOn(AUDIO_CHIP_2, (uint8_t)background_volume);
}
/* 扩展设置独立于主设置记录读取。即使主记录写到一半断电，
 * 仍能从双槽恢复现场P14/P15，不能把循环模式悄悄变回默认值。 */
static uint8_t Setting_LoadExtension(void)
{
    Setting_ExtensionRecord extension;
    Setting_ExtensionRecord extension_other;
    uint8_t extension_valid;
    uint8_t other_valid;

    memset(&extension, 0, sizeof(extension));
    extension_valid = ((Eeprom_Read(SETTING_EXTENSION_SLOT0_ADDR,
                         (uint8_t *)&extension,
                         (uint16_t)sizeof(extension)) != 0U) &&
        (Setting_IsExtensionRecordValid(&extension) != 0U)) ? 1U : 0U;
    memset(&extension_other, 0, sizeof(extension_other));
    other_valid = ((Eeprom_Read(SETTING_EXTENSION_SLOT1_ADDR,
                    (uint8_t *)&extension_other,
                    (uint16_t)sizeof(extension_other)) != 0U) &&
        (Setting_IsExtensionRecordValid(&extension_other) != 0U)) ? 1U : 0U;
    if ((other_valid != 0U) &&
        ((extension_valid == 0U) ||
         ((extension_other.version == SETTING_EXTENSION_VERSION) &&
          ((extension.version != SETTING_EXTENSION_VERSION) ||
           (((uint8_t)(extension_other.generation -
                       extension.generation) < 128U) &&
            (extension_other.generation != extension.generation))))))
    {
        extension = extension_other;
        extension_valid = 1U;
        s_extension_active_slot = 1U;
    }
    if (extension_valid == 0U)
    {
        if ((Setting_BytesAreErased(&extension, sizeof(extension)) == 0U) ||
            (Setting_BytesAreErased(&extension_other, sizeof(extension_other)) == 0U))
        {
            Eeprom_ProtectData(SETTING_EXTENSION_SLOT0_ADDR);
        }
        return 0U;
    }

    s_extension_present = 1U;
    if (extension.version == SETTING_EXTENSION_VERSION)
    {
        s_extension_generation = extension.generation;
    }
    s_setting_value[SETTING_PARAM_IMMEDIATE_AWARD] = extension.immediate_award;
    if (extension.version != SETTING_EXTENSION_LEGACY_VERSION)
    {
        s_setting_value[SETTING_PARAM_RECOVERY_DROP] = extension.recovery_drop;
    }
    return (extension.version == SETTING_EXTENSION_VERSION) ? 1U : 0U;
}

/**
 * @brief 参数设置与持久化：初始化（对应符号 Setting_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @局部状态 主要临时变量：record；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Setting_Init(void)
{
    Setting_Record record;
    Setting_Record mirror_record;
    uint8_t extension_valid;
    uint8_t record_valid = 0U;
    uint8_t mirror_needs_sync = 0U;

    Setting_LoadDefault();
    s_setting_value[SETTING_PARAM_RS485_ADDRESS] =
        MachineAddress_GetModeValue();
    s_setting_ready = 0U;
    s_primary_record_valid = 0U;
    s_extension_present = 0U;
    s_extension_active_slot = 0U;
    s_extension_generation = 0U;

    if ((Eeprom_IsReady() == 0U) && (Eeprom_Check() == 0U))
    {
        Setting_ApplyVolume();
        return;
    }

    extension_valid = Setting_LoadExtension();
    memset(&record, 0, sizeof(record));
    memset(&mirror_record, 0, sizeof(mirror_record));
    if ((Eeprom_Read(SETTING_EEPROM_ADDR, (uint8_t *)&record,
                     (uint16_t)sizeof(record)) != 0U) &&
        (Setting_IsRecordValid(&record) != 0U))
    {
        record_valid = 1U;
        s_primary_record_valid = 1U;
        if ((Eeprom_Read(SETTING_EEPROM_MIRROR_ADDR,
                         (uint8_t *)&mirror_record,
                         (uint16_t)sizeof(mirror_record)) == 0U) ||
            (Setting_IsRecordValid(&mirror_record) == 0U) ||
            (memcmp(&mirror_record, &record, sizeof(record)) != 0))
        {
            mirror_needs_sync = 1U;
        }
    }
    else if ((Eeprom_Read(SETTING_EEPROM_MIRROR_ADDR, (uint8_t *)&mirror_record,
                          (uint16_t)sizeof(mirror_record)) != 0U) &&
             (Setting_IsRecordValid(&mirror_record) != 0U))
    {
        record = mirror_record;
        record_valid = 1U;
    }
    if (record_valid != 0U)
    {
        Setting_CopyRecordToRam(&record);
        s_setting_ready = 1U;

        /* 旧版允许部分P参数超过产品范围；上电时重新约束并落盘。 */
        if ((Setting_NormalizeAll() != 0U) ||
            (record.version != SETTING_VERSION) ||
            (record.reserved[2] != SETTING_VOLUME_LEVEL_MARK) ||
            (record.reserved[11] != SETTING_BACKGROUND_VOLUME_MARK) ||
            (record.reserved[13] != SETTING_STANDBY_SOUND_RESERVED_MARK) ||
            (record.reserved[16] != SETTING_JACKPOT_CONTRIBUTION_RESERVED_MARK) ||
            (extension_valid == 0U) ||
            (s_primary_record_valid == 0U) ||
            (mirror_needs_sync != 0U))
        {
            (void)Setting_SaveNow();
        }
        Setting_ApplyVolume();
        return;
    }

    /*
     * 只允许成功读到双槽全FF的空片建立默认设置。
     * 读失败或非空记录校验失败时保护原始内容，由应用层进入E18。
     */
    if ((Eeprom_GetFault() != 0U) ||
        (Setting_BytesAreErased(&record, sizeof(record)) == 0U) ||
        (Setting_BytesAreErased(&mirror_record, sizeof(mirror_record)) == 0U))
    {
        Eeprom_ProtectData(SETTING_EEPROM_ADDR);
        Setting_ApplyVolume();
        return;
    }
    s_setting_ready = Setting_SaveNow();
    Setting_ApplyVolume();
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsReady）。
 * @调用关系 直接调用者：remote_config.c:Remote_MapSetStatus。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Setting_IsReady(void)
{
    return s_setting_ready;
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_RestoreDefault）。
 * @调用关系 直接调用者：game.c:Game_ApplySettingExitActions、remote_config.c:Remote_MapSetStatus。
 * @局部状态 主要临时变量：address_mode；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Setting_RestoreDefault(void)
{
    uint8_t network_enabled;
    uint8_t machine_id;

    /*
     * 原P9（现场归并后显示为P7）在退出设置页面时生效。
     * 这里直接把默认表复制回 RAM，再写入 EEPROM。
     * 默认表里 P9 本来就是 0，所以执行后不会反复恢复。
     */
    network_enabled = MachineAddress_IsNetworkEnabled();
    machine_id = MachineAddress_GetNodeId();
    Setting_LoadDefault();
    s_setting_value[SETTING_PARAM_RS485_ADDRESS] = machine_id;
    (void)MachineAddress_SetConfig(network_enabled, machine_id);
    (void)Setting_SaveNow();
    Setting_ApplyVolume();
}

uint8_t Setting_IsNetworkEnabled(void)
{
    return MachineAddress_IsNetworkEnabled();
}

uint8_t Setting_GetMachineId(void)
{
    return MachineAddress_GetNodeId();
}

void Setting_SetNetworkConfig(uint8_t network_enabled, uint8_t machine_id)
{
    if (MachineAddress_SetConfig(network_enabled, machine_id) != 0U)
    {
        s_setting_value[SETTING_PARAM_RS485_ADDRESS] = machine_id;
    }
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetValue）。
 * @调用关系 直接调用者：game.c:Game_ProcessClearAccountRequest、game.c:Game_ApplySettingExitActions、game.c:Game_ShowSettingPage、gift_algorithm.c:GiftAlgorithm_GetHeadroom、gift_algorithm.c:GiftAlgorithm_GetControlRateBp、gift_algorithm.c:GiftAlgorithm_GetHitPercent、gift_algorithm.c:GiftAlgorithm_GetMissCoinUpgradePercent、gift_algorithm.c:GiftAlgorithm_ChooseValueAdjustedAward。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetValue(uint8_t index)
{
    if (index >= SETTING_PARAM_COUNT)
    {
        return 0U;
    }

    return s_setting_value[index];
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_SetValue）。
 * @调用关系 直接调用者：game.c:Game_ProcessClearAccountRequest、jackpot.c:Jackpot_SlaveTask、remote_config.c:Remote_MapSetStatus、setting.c:Setting_AdjustValue、setting.c:Setting_AdjustCardRatio、setting.c:Setting_SetCardRatioPair。
 * @局部状态 主要临时变量：paired_ticket_value；均只在本次调用中有效。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Setting_SetValue(uint8_t index, uint16_t value)
{
    uint16_t paired_ticket_value;
    uint16_t previous_value;

    if (index >= SETTING_PARAM_COUNT)
    {
        return;
    }

    value = Setting_NormalizeValue(index, value);
    if (index == SETTING_PARAM_RS485_ADDRESS)
    {
        if (MachineAddress_SetModeValue((uint8_t)value) != 0U)
        {
            s_setting_value[index] = value;
        }
        else
        {
            s_setting_value[index] = MachineAddress_GetModeValue();
        }
        return;
    }
    paired_ticket_value = s_setting_value[SETTING_PARAM_TICKETS_PER_COIN];
    if (index == SETTING_PARAM_AWARD_MODE)
    {
        if (Setting_IsTicketRatioCompatible(value, paired_ticket_value) == 0U)
        {
            paired_ticket_value = Setting_GetTicketRatioDefaultForMode(value);
        }
    }
    else if ((index == SETTING_PARAM_TICKETS_PER_COIN) &&
             (Setting_IsTicketRatioCompatible(
                  s_setting_value[SETTING_PARAM_AWARD_MODE], value) == 0U))
    {
        value = Setting_GetTicketRatioDefaultForMode(
            s_setting_value[SETTING_PARAM_AWARD_MODE]);
        paired_ticket_value = value;
    }

    if ((s_setting_value[index] == value) &&
        ((index != SETTING_PARAM_AWARD_MODE) ||
         (s_setting_value[SETTING_PARAM_TICKETS_PER_COIN] == paired_ticket_value)))
    {
        if (index == SETTING_PARAM_VOLUME)
        {
            Setting_ApplyVolume();
        }
        return;
    }

    previous_value = s_setting_value[index];
    s_setting_value[index] = value;
    if (index == SETTING_PARAM_AWARD_MODE)
    {
        s_setting_value[SETTING_PARAM_TICKETS_PER_COIN] = paired_ticket_value;
    }
    if ((Setting_SaveNow() == 0U) &&
        ((index == SETTING_PARAM_IMMEDIATE_AWARD) ||
         (index == SETTING_PARAM_RECOVERY_DROP)))
    {
        /* 扩展记录没可靠落盘时，菜单不应显示一个重启就消失的值。 */
        s_setting_value[index] = previous_value;
    }
    if (index == SETTING_PARAM_VOLUME)
    {
        Setting_ApplyVolume();
    }
}

/**
 * @brief  按现场按键方向调整并保存一个参数
 * @param  index 参数下标，范围：0~30
 * @param  increase 0减小；非0增大
 * @retval 调整并规范化后的参数值
 */
uint16_t Setting_AdjustValue(uint8_t index, uint8_t increase)
{
    uint16_t value;                                // 调整中的参数值，无量纲

    if (index >= SETTING_PARAM_COUNT)
    {
        return 0U;
    }

    value = Setting_NormalizeValue(index, s_setting_value[index]);

    if (increase != 0U)
    {
        switch (index)
        {
            case SETTING_PARAM_RECOVERY_DROP:
                if (value < SETTING_RECOVERY_DROP_MAX) value++;
                break;
            case SETTING_PARAM_MAJOR_DRAW_COINS:
                if (value < SETTING_MAJOR_DRAW_COINS_MAX)
                    value = (uint16_t)(value + SETTING_MAJOR_DRAW_COINS_STEP);
                break;
            case SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL:
                if (value < SETTING_MAJOR_POOL_STAKE_MAX) value++;
                break;
            case 0U:
                value = (value >= 4U) ? 1U : (uint16_t)(value + 1U);
                break;

            case 1U:
            case 7U:
            case 8U:
            case 10U:
            case SETTING_PARAM_STANDBY_SOUND_ENABLE:
            case SETTING_PARAM_IMMEDIATE_AWARD:
                value = (value == 0U) ? 1U : 0U;
                break;

            case 2U:
                if (value < 10U)
                {
                    value++;
                }
                break;

            case 3U:
                if (value < 5U)
                {
                    value++;
                }
                break;

            case SETTING_PARAM_LOTTERY_DIFFICULTY:
                if (value < SETTING_LOTTERY_DIFFICULTY_MAX)
                {
                    value++;
                }
                break;

            case 4U:
                if (value < 98U)
                {
                    value++;
                }
                break;

            case 5U:
                if (s_setting_value[SETTING_PARAM_AWARD_MODE] ==
                    SETTING_AWARD_MODE_COIN_AND_TICKET)
                {
                    if (value < SETTING_COMBINED_TICKETS_MAX)
                    {
                        value++;
                    }
                }
                else if (s_setting_value[SETTING_PARAM_AWARD_MODE] ==
                         SETTING_AWARD_MODE_TICKET)
                {
                    if (value < SETTING_TICKET_ONLY_TICKETS_MAX)
                    {
                        value++;
                    }
                }
                else if (value < SETTING_COMBINED_TICKETS_MAX)
                {
                    value++;
                }
                else if (value == SETTING_COMBINED_TICKETS_MAX)
                {
                    value = SETTING_TICKET_ONLY_TICKETS_MIN;
                }
                else if (value < SETTING_TICKET_ONLY_TICKETS_MAX)
                {
                    value++;
                }
                break;

            case 9U:
                value = (value >= 2U) ? 0U : (uint16_t)(value + 1U);
                break;

            case 11U:
                if (value < 30U)
                {
                    value++;
                }
                break;

            case 12U:
                if (value < 30U)
                {
                    value++;
                }
                break;

            case 13U:
                if (value < 990U)
                {
                    value = (uint16_t)(value + 10U);
                }
                else if (value < 999U)
                {
                    value = 999U;
                }
                break;

            case SETTING_PARAM_JACKPOT_COINS_PER_POINT:
                if (value < SETTING_JACKPOT_COINS_PER_POINT_MAX)
                {
                    value = (uint16_t)(
                        value + SETTING_JACKPOT_COINS_PER_POINT_STEP);
                }
                break;

            case SETTING_PARAM_RS485_ADDRESS:
                value = (value >= MACHINE_ADDRESS_MAX_ID) ?
                        0U : (uint16_t)(value + 1U);
                break;

            default:
                break;
        }
    }
    else
    {
        switch (index)
        {
            case SETTING_PARAM_RECOVERY_DROP:
                if (value > 0U) value--;
                break;
            case SETTING_PARAM_MAJOR_DRAW_COINS:
                if (value > SETTING_MAJOR_DRAW_COINS_MIN)
                    value = (uint16_t)(value - SETTING_MAJOR_DRAW_COINS_STEP);
                break;
            case SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL:
                if (value > SETTING_MAJOR_POOL_STAKE_MIN) value--;
                break;
            case 0U:
                value = (value <= 1U) ? 4U : (uint16_t)(value - 1U);
                break;

            case 1U:
            case 7U:
            case 8U:
            case 10U:
            case SETTING_PARAM_STANDBY_SOUND_ENABLE:
            case SETTING_PARAM_IMMEDIATE_AWARD:
                value = (value == 0U) ? 1U : 0U;
                break;

            case 2U:
            case 3U:
                if (value > 1U)
                {
                    value--;
                }
                break;

            case SETTING_PARAM_LOTTERY_DIFFICULTY:
                if (value > SETTING_LOTTERY_DIFFICULTY_MIN)
                {
                    value--;
                }
                break;

            case 4U:
                if (value > 85U)
                {
                    value--;
                }
                break;

            case 5U:
                if (s_setting_value[SETTING_PARAM_AWARD_MODE] ==
                    SETTING_AWARD_MODE_COIN_AND_TICKET)
                {
                    if (value > SETTING_COMBINED_TICKETS_MIN)
                    {
                        value--;
                    }
                }
                else if (s_setting_value[SETTING_PARAM_AWARD_MODE] ==
                         SETTING_AWARD_MODE_TICKET)
                {
                    if (value > SETTING_TICKET_ONLY_TICKETS_MIN)
                    {
                        value--;
                    }
                }
                else if (value > SETTING_TICKET_ONLY_TICKETS_MIN)
                {
                    value--;
                }
                else if (value == SETTING_TICKET_ONLY_TICKETS_MIN)
                {
                    value = SETTING_COMBINED_TICKETS_MAX;
                }
                else if (value > SETTING_COMBINED_TICKETS_MIN)
                {
                    value--;
                }
                break;

            case 9U:
                value = (value == 0U) ? 2U : (uint16_t)(value - 1U);
                break;

            case 11U:
                if (value > 10U)
                {
                    value--;
                }
                break;

            case 12U:
                if (value > 0U)
                {
                    value--;
                }
                break;

            case 13U:
                if (value >= 999U)
                {
                    value = 990U;
                }
                else if (value >= 10U)
                {
                    value = (uint16_t)(value - 10U);
                }
                else
                {
                    value = 0U;
                }
                break;

            case SETTING_PARAM_JACKPOT_COINS_PER_POINT:
                if (value > SETTING_JACKPOT_COINS_PER_POINT_MIN)
                {
                    value = (uint16_t)(
                        value - SETTING_JACKPOT_COINS_PER_POINT_STEP);
                }
                break;

            case SETTING_PARAM_RS485_ADDRESS:
                value = (value == 0U) ?
                        MACHINE_ADDRESS_MAX_ID : (uint16_t)(value - 1U);
                break;

            default:
                break;
        }
    }

    Setting_SetValue(index, value);
    return s_setting_value[index];
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsCoinPayoutEnabled）。
 * @调用关系 直接调用者：game.c:Game_RecordReturnCommitment、game.c:Game_AddLotteryPrizeAndPayout、game.c:Game_AddLinePrizeAndHold、game.c:Game_FinishTicketSkipHold、game.c:Game_Task、gift_algorithm.c:GiftAlgorithm_GetOutputMultiplier。
 * @局部状态 主要临时变量：mode；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Setting_IsCoinPayoutEnabled(void)
{
    uint16_t mode = Setting_GetValue(SETTING_PARAM_AWARD_MODE);

    return ((mode == SETTING_AWARD_MODE_COIN) ||
            (mode == SETTING_AWARD_MODE_COIN_AND_TICKET)) ? 1U : 0U;
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsTicketPayoutEnabled）。
 * @调用关系 直接调用者：game.c:Game_RecordReturnCommitment、game.c:Game_AddLotteryPrizeAndPayout、game.c:Game_AddLinePrizeAndHold、game.c:Game_TrySkipTicketOutput、game.c:Game_Task、gift_algorithm.c:GiftAlgorithm_GetOutputMultiplier。
 * @局部状态 主要临时变量：mode；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Setting_IsTicketPayoutEnabled(void)
{
    uint16_t mode = Setting_GetValue(SETTING_PARAM_AWARD_MODE);

    return ((mode == SETTING_AWARD_MODE_TICKET) ||
            (mode == SETTING_AWARD_MODE_COIN_AND_TICKET)) ? 1U : 0U;
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsCardPayoutEnabled）。
 * @调用关系 直接调用者：game.c:Game_RecordReturnCommitment、game.c:Game_AddLotteryPrizeAndPayout、game.c:Game_AddLinePrizeAndHold、game.c:Game_StartCardPayout、game.c:Game_HandleTicketKey、game.c:Game_CompleteLineSettlement、game.c:Game_IsJackpotOutputComplete、game.c:Game_TrySkipTicketOutput。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Setting_IsCardPayoutEnabled(void)
{
    return (Setting_GetValue(SETTING_PARAM_AWARD_MODE) ==
            SETTING_AWARD_MODE_CARD) ? 1U : 0U;
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsScoreExchangeEnabled）。
 * @调用关系 直接调用者：game.c:Game_HandleTicketKey。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Setting_IsScoreExchangeEnabled(void)
{
    return (Setting_GetValue(SETTING_PARAM_SCORE_EXCHANGE_ENABLE) != 0U) ? 1U : 0U;
}

uint8_t Setting_IsImmediateAwardEnabled(void)
{
    return (Setting_GetValue(SETTING_PARAM_IMMEDIATE_AWARD) != 0U) ? 1U : 0U;
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetCoinToPoint）。
 * @调用关系 直接调用者：game.c:Game_CalcCreditFromCoinPulse。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetCoinToPoint(void)
{
    return Setting_NormalizeValue(SETTING_PARAM_COIN_TO_POINT,
                                  Setting_GetValue(SETTING_PARAM_COIN_TO_POINT));
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetCoinsPerPoint）。
 * @调用关系 直接调用者：game.c:Game_CalcCreditFromCoinPulse。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetCoinsPerPoint(void)
{
    return Setting_NormalizeValue(SETTING_PARAM_COINS_PER_POINT,
                                  Setting_GetValue(SETTING_PARAM_COINS_PER_POINT));
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetTicketsPerCoin）。
 * @调用关系 直接调用者：game.c:Game_CalcTicketPulseCount、game.c:Game_TrySkipTicketOutput、game.c:Game_Init、game.c:Game_Task、setting.c:Setting_GetLineTicketRatio。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetTicketsPerCoin(void)
{
    return Setting_NormalizeValue(SETTING_PARAM_TICKETS_PER_COIN,
                                  Setting_GetValue(SETTING_PARAM_TICKETS_PER_COIN));
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetBackgroundVolume）。
 * @调用关系 直接调用者：game.c:Game_ShowSettingPage、remote_config.c:Remote_GetParamPair、remote_config.c:Remote_DisplayTask、setting.c:Setting_AdjustBackgroundVolume。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetBackgroundVolume(void)
{
    return Setting_NormalizeValue(SETTING_PARAM_VOLUME,
                                  s_background_volume);
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetEffectVolume）。
 * @调用关系 直接调用者：game.c:Game_ShowSettingPage、remote_config.c:Remote_GetParamPair、remote_config.c:Remote_DisplayTask。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetEffectVolume(void)
{
    return Setting_NormalizeValue(SETTING_PARAM_VOLUME,
                                  Setting_GetValue(SETTING_PARAM_VOLUME));
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_AdjustBackgroundVolume）。
 * @调用关系 直接调用者：game.c:Game_TaskSettingMode。
 * @局部状态 主要临时变量：value；均只在本次调用中有效。
 * @param increase 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Setting_AdjustBackgroundVolume(uint8_t increase)
{
    uint16_t value;

    value = Setting_GetBackgroundVolume();
    if (increase != 0U)
    {
        if (value < 30U)
        {
            value++;
        }
    }
    else if (value > 0U)
    {
        value--;
    }

    if (s_background_volume != value)
    {
        s_background_volume = value;
        (void)Setting_SaveNow();
    }
    Setting_ApplyVolume();
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_SetVolumePair）。
 * @调用关系 直接调用者：remote_config.c:Remote_MapSetStatus。
 * @param background_volume 按值传入；函数内部修改不会回写调用方变量。
 * @param effect_volume 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Setting_SetVolumePair(uint16_t background_volume, uint16_t effect_volume)
{
    background_volume = Setting_NormalizeValue(SETTING_PARAM_VOLUME,
                                               background_volume);
    effect_volume = Setting_NormalizeValue(SETTING_PARAM_VOLUME,
                                           effect_volume);
    if ((s_background_volume != background_volume) ||
        (s_setting_value[SETTING_PARAM_VOLUME] != effect_volume))
    {
        s_background_volume = background_volume;
        s_setting_value[SETTING_PARAM_VOLUME] = effect_volume;
        (void)Setting_SaveNow();
    }
    Setting_ApplyVolume();
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetCardCoinsPerBatch）。
 * @调用关系 直接调用者：game.c:Game_StartCardPayout、game.c:Game_ShowSettingPage、game.c:Game_Task、remote_config.c:Remote_GetParamPair、remote_config.c:Remote_DisplayTask、setting.c:Setting_AdjustCardRatio。
 * @局部状态 主要临时变量：ratio；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetCardCoinsPerBatch(void)
{
    uint16_t ratio;

    ratio = Setting_NormalizeValue(SETTING_PARAM_CARD_RATIO,
                                   Setting_GetValue(SETTING_PARAM_CARD_RATIO));
    return ratio / SETTING_CARD_RATIO_FACTOR;
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetCardsPerBatch）。
 * @调用关系 直接调用者：game.c:Game_StartCardPayout、game.c:Game_ShowSettingPage、remote_config.c:Remote_GetParamPair、remote_config.c:Remote_DisplayTask、setting.c:Setting_AdjustCardRatio。
 * @局部状态 主要临时变量：ratio；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetCardsPerBatch(void)
{
    uint16_t ratio;

    ratio = Setting_NormalizeValue(SETTING_PARAM_CARD_RATIO,
                                   Setting_GetValue(SETTING_PARAM_CARD_RATIO));
    return ratio % SETTING_CARD_RATIO_FACTOR;
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_AdjustCardRatio）。
 * @调用关系 直接调用者：game.c:Game_TaskSettingMode。
 * @局部状态 主要临时变量：coins、cards；均只在本次调用中有效。
 * @param adjust_cards 按值传入；函数内部修改不会回写调用方变量。
 * @param increase 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Setting_AdjustCardRatio(uint8_t adjust_cards, uint8_t increase)
{
    uint16_t coins;
    uint16_t cards;

    coins = Setting_GetCardCoinsPerBatch();
    cards = Setting_GetCardsPerBatch();

    if (adjust_cards == 0U)
    {
        if (increase != 0U)
        {
            if (coins < SETTING_CARD_RATIO_MAX)
            {
                coins++;
            }
        }
        else if (coins > SETTING_CARD_RATIO_MIN)
        {
            coins--;
        }
    }
    else
    {
        if (increase != 0U)
        {
            if (cards < SETTING_CARD_RATIO_MAX)
            {
                cards++;
            }
        }
        else if (cards > SETTING_CARD_RATIO_MIN)
        {
            cards--;
        }
    }

    Setting_SetValue(SETTING_PARAM_CARD_RATIO,
                     (uint16_t)(coins * SETTING_CARD_RATIO_FACTOR + cards));
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_SetCardRatioPair）。
 * @调用关系 直接调用者：remote_config.c:Remote_MapSetStatus。
 * @局部状态 主要临时变量：ratio；均只在本次调用中有效。
 * @param coins_per_batch 按值传入；函数内部修改不会回写调用方变量。
 * @param cards_per_batch 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Setting_SetCardRatioPair(uint16_t coins_per_batch, uint16_t cards_per_batch)
{
    uint16_t ratio;

    if ((coins_per_batch < SETTING_CARD_RATIO_MIN) ||
        (coins_per_batch > SETTING_CARD_RATIO_MAX) ||
        (cards_per_batch < SETTING_CARD_RATIO_MIN) ||
        (cards_per_batch > SETTING_CARD_RATIO_MAX))
    {
        return;
    }

    ratio = (uint16_t)(coins_per_batch * SETTING_CARD_RATIO_FACTOR +
                       cards_per_batch);
    Setting_SetValue(SETTING_PARAM_CARD_RATIO, ratio);
}

/**
 * @brief  读取现场P5预留的综合返奖率档位
 * @param  无
 * @retval 1~5；计划1档综合返奖率最高、5档最低，当前仅保存和显示
 */
uint8_t Setting_GetLotteryDifficulty(void)
{
    return (uint8_t)Setting_NormalizeValue(
        SETTING_PARAM_LOTTERY_DIFFICULTY,
        Setting_GetValue(SETTING_PARAM_LOTTERY_DIFFICULTY));
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetLineTicketRatio）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetLineTicketRatio(void)
{
    return Setting_GetTicketsPerCoin();
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetJackpotBase）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_LoadDefaults、jackpot.c:Jackpot_SlaveTask、jackpot.c:Jackpot_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetJackpotBase(void)
{
    return Setting_NormalizeValue(SETTING_PARAM_JACKPOT_BASE,
                                  Setting_GetValue(SETTING_PARAM_JACKPOT_BASE));
}

/**
 * @brief 参数设置与持久化：读取（对应符号 Setting_GetJackpotCoinsPerPoint）。
 * @调用关系 直接调用者：jackpot.c:Jackpot_LoadDefaults、jackpot.c:Jackpot_SlaveTask、jackpot.c:Jackpot_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Setting_GetJackpotCoinsPerPoint(void)
{
    return Setting_NormalizeValue(
        SETTING_PARAM_JACKPOT_COINS_PER_POINT,
        Setting_GetValue(SETTING_PARAM_JACKPOT_COINS_PER_POINT));
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsSolenoidEyeCheckEnabled）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Setting_IsSolenoidEyeCheckEnabled(void)
{
    return (Setting_GetValue(SETTING_PARAM_SOLENOID_EYE_CHECK) == SETTING_SOLENOID_EYE_ENABLE) ? 1U : 0U;
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsAmbientLedFlashEnabled）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param award_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Setting_IsAmbientLedFlashEnabled(uint16_t award_count)
{
    if (Setting_GetValue(SETTING_PARAM_AMBIENT_LED_FLASH) == 0U)
    {
        return 0U;
    }

    return (award_count >= 20U) ? 1U : 0U;
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_IsStandbySoundEnabled）。
 * @调用关系 直接调用者：game.c:Game_Task、standby.c:Standby_Start、standby.c:Standby_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Setting_IsStandbySoundEnabled(void)
{
    return (Setting_GetValue(SETTING_PARAM_STANDBY_SOUND_ENABLE) != 0U) ? 1U : 0U;
}

/**
 * @brief 参数设置与持久化：设置（对应符号 Setting_SaveNow）。
 * @调用关系 直接调用者：setting.c:Setting_Init、setting.c:Setting_RestoreDefault、setting.c:Setting_SetValue、setting.c:Setting_AdjustBackgroundVolume、setting.c:Setting_SetVolumePair。
 * @局部状态 主要临时变量：record；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Setting_SaveNow(void)
{
    Setting_Record record;
    Setting_Record verified_record;
    Setting_ExtensionRecord extension;
    Setting_ExtensionRecord verified_extension;
    uint16_t extension_addr;
    uint8_t target_slot;

    if ((Eeprom_IsReady() == 0U) && (Eeprom_Check() == 0U))
    {
        s_setting_ready = 0U;
        return 0U;
    }

    Setting_BuildRecord(&record);
    /* 镜像是唯一有效副本时，先修复主槽；不能先覆盖唯一的镜像。 */
    if (s_primary_record_valid == 0U)
    {
        if ((Eeprom_Write(SETTING_EEPROM_ADDR, (const uint8_t *)&record,
                          (uint16_t)sizeof(record)) == 0U) ||
            (Eeprom_Read(SETTING_EEPROM_ADDR, (uint8_t *)&verified_record,
                         (uint16_t)sizeof(verified_record)) == 0U) ||
            (memcmp(&verified_record, &record, sizeof(record)) != 0))
        {
            s_setting_ready = 0U;
            return 0U;
        }
        s_primary_record_valid = 1U;
    }
    if ((Eeprom_Write(SETTING_EEPROM_MIRROR_ADDR, (const uint8_t *)&record,
                      (uint16_t)sizeof(record)) == 0U) ||
        (Eeprom_Read(SETTING_EEPROM_MIRROR_ADDR, (uint8_t *)&verified_record,
                     (uint16_t)sizeof(verified_record)) == 0U) ||
        (memcmp(&verified_record, &record, sizeof(record)) != 0))
    {
        s_setting_ready = 0U;
        return 0U;
    }
    if ((Eeprom_Write(SETTING_EEPROM_ADDR, (const uint8_t *)&record,
                      (uint16_t)sizeof(record)) == 0U) ||
        (Eeprom_Read(SETTING_EEPROM_ADDR, (uint8_t *)&verified_record,
                     (uint16_t)sizeof(verified_record)) == 0U) ||
        (memcmp(&verified_record, &record, sizeof(record)) != 0))
    {
        s_primary_record_valid = 0U;
        s_setting_ready = 0U;
        return 0U;
    }

    Setting_BuildExtensionRecord(&extension);
    target_slot = (s_extension_present != 0U) ?
                  (uint8_t)(s_extension_active_slot ^ 1U) : 0U;
    extension_addr = (target_slot == 0U) ?
                     SETTING_EXTENSION_SLOT0_ADDR :
                     SETTING_EXTENSION_SLOT1_ADDR;
    if (Eeprom_Write(extension_addr,
                     (const uint8_t *)&extension,
                     (uint16_t)sizeof(extension)) == 0U)
    {
        s_setting_ready = 0U;
        return 0U;
    }
    if ((Eeprom_Read(extension_addr, (uint8_t *)&verified_extension,
                     (uint16_t)sizeof(verified_extension)) == 0U) ||
        (memcmp(&verified_extension, &extension, sizeof(extension)) != 0))
    {
        s_setting_ready = 0U;
        return 0U;
    }

    s_extension_present = 1U;
    s_extension_active_slot = target_slot;
    s_extension_generation = extension.generation;
    s_setting_ready = 1U;
    return 1U;
}
