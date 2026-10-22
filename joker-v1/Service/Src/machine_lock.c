/* AUTO-FILE-DOC
 * 文件职责：运行限时与解码锁。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "machine_lock.h"

#include "account.h"
#include "audio_manager.h"
#include "eeprom.h"
#include "gtim.h"
#include "jackpot.h"
#include "key.h"
#include "segment.h"

#include <stddef.h>
#include <string.h>

#define MACHINE_LOCK_SLOT0_ADDR             0x7000U
#define MACHINE_LOCK_SLOT1_ADDR             0x7040U
#define MACHINE_LOCK_MAGIC                  0x314B434CUL
#define MACHINE_LOCK_VERSION                3U
#define MACHINE_LOCK_PREVIOUS_VERSION       2U
#define MACHINE_LOCK_LEGACY_VERSION         1U
#define MACHINE_LOCK_ADMIN_PASSWORD         11451419UL
#define MACHINE_LOCK_DEFAULT_PASSWORD       10000000UL
#define MACHINE_LOCK_CODE_MODULO            100000000UL
#define MACHINE_LOCK_HOLD_MS                2000UL
#define MACHINE_LOCK_MINUTE_MS              60000UL
#define MACHINE_LOCK_BLINK_MS               300UL
#define MACHINE_LOCK_DECODE_PROMPT_MS       300000UL
#define MACHINE_LOCK_DECODE_PROMPT_INDEX    28U

typedef enum
{
    MACHINE_LOCK_UI_NONE = 0U,
    MACHINE_LOCK_UI_A0,
    MACHINE_LOCK_UI_A1,
    MACHINE_LOCK_UI_A2,
    MACHINE_LOCK_UI_H,
    MACHINE_LOCK_UI_F0,
    MACHINE_LOCK_UI_U0,
    MACHINE_LOCK_UI_U1
} MachineLock_Ui;

typedef struct
{
    uint8_t was_pressed;
    uint8_t long_fired;
    uint32_t press_start_ms;
} MachineLock_Hold;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t size;
    uint32_t sequence;
    uint32_t interval_minutes;
    uint32_t total_runtime_minutes;
    uint32_t cycle_runtime_minutes;
    uint32_t lock_runtime_minutes;
    uint32_t lock_count;
    uint8_t locked;
    uint8_t reserved0[3];
    uint32_t staff_password;
    uint8_t reserved[20];
    uint32_t crc32;
} MachineLock_Record;

typedef char MachineLock_RecordSizeMustBe64[
    (sizeof(MachineLock_Record) == 64U) ? 1 : -1];

static MachineLock_Ui s_ui;
static MachineLock_Hold s_hold_clear;
static MachineLock_Hold s_hold_account;
static MachineLock_Hold s_hold_set;
static MachineLock_Hold s_hold_user;
static uint8_t s_digits[8];
static uint8_t s_selected_digit;
static uint8_t s_h_page;
static uint8_t s_code_edited;
static uint8_t s_locked;
static uint8_t s_active_slot;
static uint8_t s_have_record;
static uint8_t s_check_valid;
static uint32_t s_sequence;
static uint32_t s_interval_minutes;
static uint32_t s_total_runtime_minutes;
static uint32_t s_cycle_runtime_minutes;
static uint32_t s_lock_runtime_minutes;
static uint32_t s_lock_count;
static uint32_t s_staff_password;
static uint32_t s_minute_accumulator_ms;
static uint8_t s_runtime_save_pending;
static uint32_t s_last_task_ms;
static uint32_t s_random_state;
static uint32_t s_h_runtime;
static uint32_t s_h_coin_in;
static uint32_t s_h_coin_out;
static uint32_t s_h_check;
static int32_t s_h_profit;
static uint32_t s_h_lock_count;
static uint32_t s_decode_prompt_start_ms;

/**
 * @brief 运行限时与解码锁：计算 CRC（对应符号 MachineLock_Crc32）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_RecordValid、machine_lock.c:MachineLock_Save。
 * @局部状态 主要临时变量：crc、i、bit；均只在本次调用中有效。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param length 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t MachineLock_Crc32(const uint8_t *data, uint32_t length)
{
    uint32_t crc;
    uint32_t i;
    uint8_t bit;

    crc = 0xFFFFFFFFUL;
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
 * @brief 运行限时与解码锁：校验（对应符号 MachineLock_RecordValid）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_ReadSlot。
 * @局部状态 主要临时变量：crc；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t MachineLock_RecordValid(const MachineLock_Record *record)
{
    uint32_t crc;

    if ((record->magic != MACHINE_LOCK_MAGIC) ||
        ((record->version != MACHINE_LOCK_VERSION) &&
         (record->version != MACHINE_LOCK_PREVIOUS_VERSION) &&
         (record->version != MACHINE_LOCK_LEGACY_VERSION)) ||
        (record->size != (uint16_t)sizeof(MachineLock_Record)) ||
        (record->locked > 1U) ||
        ((record->version == MACHINE_LOCK_VERSION) &&
         (record->staff_password >= MACHINE_LOCK_CODE_MODULO)))
    {
        return 0U;
    }

    crc = MachineLock_Crc32((const uint8_t *)record,
                            (uint32_t)offsetof(MachineLock_Record, crc32));
    return (crc == record->crc32) ? 1U : 0U;
}

/**
 * @brief 运行限时与解码锁：读取（对应符号 MachineLock_ReadSlot）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_Save、machine_lock.c:MachineLock_Init。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t MachineLock_ReadSlot(uint16_t address,
                                    MachineLock_Record *record)
{
    if (Eeprom_Read(address,
                    (uint8_t *)record,
                    (uint16_t)sizeof(*record)) == 0U)
    {
        return 0U;
    }
    return MachineLock_RecordValid(record);
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_Save）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_TaskUi、machine_lock.c:MachineLock_Init、machine_lock.c:MachineLock_Task。
 * @局部状态 主要临时变量：record、verify、target_slot、address；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_Save(void)
{
    MachineLock_Record record;
    MachineLock_Record verify;
    uint8_t target_slot;
    uint16_t address;

    memset(&record, 0, sizeof(record));
    record.magic = MACHINE_LOCK_MAGIC;
    record.version = MACHINE_LOCK_VERSION;
    record.size = (uint16_t)sizeof(record);
    record.sequence = s_sequence + 1UL;
    record.interval_minutes = s_interval_minutes;
    record.total_runtime_minutes = s_total_runtime_minutes;
    record.cycle_runtime_minutes = s_cycle_runtime_minutes;
    record.lock_runtime_minutes = s_lock_runtime_minutes;
    record.lock_count = s_lock_count;
    record.locked = s_locked;
    record.staff_password = s_staff_password;
    record.crc32 = MachineLock_Crc32(
        (const uint8_t *)&record,
        (uint32_t)offsetof(MachineLock_Record, crc32));

    target_slot = (s_have_record != 0U) ?
                  (uint8_t)(s_active_slot ^ 1U) : 0U;
    address = (target_slot == 0U) ?
              MACHINE_LOCK_SLOT0_ADDR : MACHINE_LOCK_SLOT1_ADDR;

    if ((Eeprom_Write(address,
                      (const uint8_t *)&record,
                      (uint16_t)sizeof(record)) != 0U) &&
        (MachineLock_ReadSlot(address, &verify) != 0U) &&
        (verify.sequence == record.sequence))
    {
        s_sequence = record.sequence;
        s_active_slot = target_slot;
        s_have_record = 1U;
    }
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_ValueToDigits）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_RenderEightDigitValue、machine_lock.c:MachineLock_StartUi。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @param digits 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_ValueToDigits(uint32_t value, uint8_t digits[8])
{
    int8_t i;

    value %= MACHINE_LOCK_CODE_MODULO;
    for (i = 7; i >= 0; i--)
    {
        digits[(uint8_t)i] = (uint8_t)(value % 10UL);
        value /= 10UL;
    }
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_DigitsToValue）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_TaskUi。
 * @局部状态 主要临时变量：i、value；均只在本次调用中有效。
 * @param digits 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t MachineLock_DigitsToValue(const uint8_t digits[8])
{
    uint8_t i;
    uint32_t value;

    value = 0U;
    for (i = 0U; i < 8U; i++)
    {
        value = value * 10UL + (uint32_t)digits[i];
    }
    return value;
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_RenderDigits）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_TaskUi。
 * @局部状态 主要临时变量：blank_index；均只在本次调用中有效。
 * @param label 按值传入；函数内部修改不会回写调用方变量。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_RenderDigits(uint8_t label, uint8_t index)
{
    int8_t blank_index;

    blank_index = -1;
    if (((Gtim_NowMs() / MACHINE_LOCK_BLINK_MS) & 1UL) == 0UL)
    {
        blank_index = (int8_t)s_selected_digit;
    }

    if (label == (uint8_t)'A')
    {
        Segment_SetLeftANumber(index);
    }
    else if (label == (uint8_t)'U')
    {
        Segment_SetLeftUNumber(index);
    }
    else
    {
        Segment_SetLeftFNumber(index);
    }
    Segment_SetMiddleRightDigits(s_digits, blank_index);
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_RenderEightDigitValue）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_RenderQuery、machine_lock.c:MachineLock_TaskUi。
 * @局部状态 主要临时变量：digits；均只在本次调用中有效。
 * @param label 按值传入；函数内部修改不会回写调用方变量。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_RenderEightDigitValue(uint8_t label,
                                              uint8_t index,
                                              uint32_t value)
{
    uint8_t digits[8];

    MachineLock_ValueToDigits(value, digits);
    if (label == (uint8_t)'H')
    {
        Segment_SetLeftHNumber(index);
    }
    else if (label == (uint8_t)'A')
    {
        Segment_SetLeftANumber(index);
    }
    else if (label == (uint8_t)'U')
    {
        Segment_SetLeftUNumber(index);
    }
    Segment_SetMiddleRightDigits(digits, -1);
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_RenderLocked）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_RenderLocked(void)
{
    Segment_SetLeftFixed4(0U);
    Segment_SetMiddleFixed4(0U);
    Segment_SetRightFixed4(0U);
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_Random）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_CaptureQuery。
 * @局部状态 主要临时变量：value；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t MachineLock_Random(void)
{
    uint32_t value;

    value = s_random_state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    if (value == 0U)
    {
        value = 0x6D2B79F5UL;
    }
    s_random_state = value;
    return value;
}

/**
 * @brief 运行限时与解码锁：读取（对应符号 MachineLock_GetCurrentProfit）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_Task。
 * @局部状态 主要临时变量：data、award_total；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static int64_t MachineLock_GetCurrentProfit(void)
{
    Account_Data data;
    uint64_t award_total;

    Account_GetData(&data);
    award_total = Account_GetAwardSinceEpoch(&data);
    if ((uint64_t)Account_GetStakeSinceEpoch(&data) >= award_total)
    {
        return (int64_t)((uint64_t)Account_GetStakeSinceEpoch(&data) - award_total);
    }
    return -(int64_t)(award_total - (uint64_t)Account_GetStakeSinceEpoch(&data));
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_CaptureQuery）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_StartUi。
 * @局部状态 主要临时变量：data、award_total、tail、first、profit；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_CaptureQuery(void)
{
    Account_Data data;
    uint64_t award_total;
    uint64_t stake_total;
    uint32_t tail;
    uint32_t first;
    int64_t profit;

    Account_GetData(&data);
    award_total = Account_GetAwardSinceEpoch(&data);
    stake_total = Account_GetStakeSinceEpoch(&data);
    s_h_runtime = (s_locked != 0U) ?
                  s_lock_runtime_minutes : s_total_runtime_minutes;
    if (s_h_runtime > 99999999UL)
    {
        s_h_runtime = 99999999UL;
    }
    /* H2/H3镜像新版C1/C2实体收支；打码利润仍按总押/总中，
     * 不因奖励尚未从中屏实际吐完而误判为盈利。 */
    s_h_coin_in = (data.physical_coin_in_total > 99999999UL) ?
                  99999999UL : data.physical_coin_in_total;
    s_h_coin_out = (data.physical_coin_out_total > 99999999UL) ?
                   99999999UL : data.physical_coin_out_total;
    if (stake_total >= award_total)
    {
        profit = (int64_t)(stake_total - award_total);
    }
    else
    {
        profit = -(int64_t)(award_total - stake_total);
    }
    if (profit > 99999999LL)
    {
        profit = 99999999LL;
    }
    else if (profit < -9999999LL)
    {
        profit = -9999999LL;
    }
    s_h_profit = (int32_t)profit;
    s_h_lock_count = (s_lock_count > 99999999UL) ?
                     99999999UL : s_lock_count;

    first = (MachineLock_Random() % 3UL) + 1UL;
    tail = MachineLock_Random() % 10000000UL;
    s_h_check = first * 10000000UL + tail;
    s_check_valid = 1U;
}

/**
 * @brief 运行限时与解码锁：启动（对应符号 MachineLock_StartUi）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_UpdateHold、machine_lock.c:MachineLock_UpdateDualHold、machine_lock.c:MachineLock_TaskUi。
 * @param ui 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_StartUi(MachineLock_Ui ui)
{
    s_ui = ui;
    s_selected_digit = 0U;
    s_code_edited = 0U;
    memset(s_digits, 0, sizeof(s_digits));

    if (ui == MACHINE_LOCK_UI_A1)
    {
        MachineLock_ValueToDigits(s_interval_minutes, s_digits);
    }
    else if (ui == MACHINE_LOCK_UI_U1)
    {
        MachineLock_ValueToDigits(s_staff_password, s_digits);
    }
    else if (ui == MACHINE_LOCK_UI_H)
    {
        s_h_page = 1U;
        MachineLock_CaptureQuery();
    }
    else if ((ui == MACHINE_LOCK_UI_A2) && (s_check_valid == 0U))
    {
        /* A2 is the password-protected calculator fallback. */
        MachineLock_CaptureQuery();
    }
}

/**
 * @brief 运行限时与解码锁：更新（对应符号 MachineLock_UpdateHold）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_Task。
 * @局部状态 主要临时变量：pressed、now；均只在本次调用中有效。
 * @param hold 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param key 按值传入；函数内部修改不会回写调用方变量。
 * @param target 按值传入；函数内部修改不会回写调用方变量。
 * @param allow_entry 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t MachineLock_UpdateHold(MachineLock_Hold *hold,
                                      Key_Id key,
                                      MachineLock_Ui target,
                                      uint8_t allow_entry)
{
    uint8_t pressed;
    uint32_t now;

    pressed = Key_IsPressed(key);
    now = Gtim_NowMs();

    if (pressed != 0U)
    {
        if (hold->was_pressed == 0U)
        {
            hold->was_pressed = 1U;
            hold->long_fired = 0U;
            hold->press_start_ms = now;
        }
        else if ((hold->long_fired == 0U) &&
                 ((uint32_t)(now - hold->press_start_ms) >=
                  MACHINE_LOCK_HOLD_MS))
        {
            hold->long_fired = 1U;
            (void)Key_TakePress(key);
            if ((allow_entry != 0U) && (s_ui == MACHINE_LOCK_UI_NONE))
            {
                MachineLock_StartUi(target);
                return 1U;
            }
        }
    }
    else if (hold->was_pressed != 0U)
    {
        if (hold->long_fired != 0U)
        {
            (void)Key_TakePress(key);
        }
        hold->was_pressed = 0U;
        hold->long_fired = 0U;
    }

    return 0U;
}

/**
 * @brief 运行限时与解码锁：更新（对应符号 MachineLock_UpdateDualHold）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_Task。
 * @局部状态 主要临时变量：pressed、now；均只在本次调用中有效。
 * @param hold 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param first_key 按值传入；函数内部修改不会回写调用方变量。
 * @param second_key 按值传入；函数内部修改不会回写调用方变量。
 * @param target 按值传入；函数内部修改不会回写调用方变量。
 * @param allow_entry 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t MachineLock_UpdateDualHold(MachineLock_Hold *hold,
                                          Key_Id first_key,
                                          Key_Id second_key,
                                          MachineLock_Ui target,
                                          uint8_t allow_entry)
{
    uint8_t pressed;
    uint32_t now;

    pressed = ((Key_IsPressed(first_key) != 0U) &&
               (Key_IsPressed(second_key) != 0U)) ? 1U : 0U;
    now = Gtim_NowMs();
    if (pressed != 0U)
    {
        if (hold->was_pressed == 0U)
        {
            hold->was_pressed = 1U;
            hold->long_fired = 0U;
            hold->press_start_ms = now;
        }
        else if ((hold->long_fired == 0U) &&
                 ((uint32_t)(now - hold->press_start_ms) >=
                  MACHINE_LOCK_HOLD_MS))
        {
            hold->long_fired = 1U;
            (void)Key_TakePress(first_key);
            (void)Key_TakePress(second_key);
            if ((allow_entry != 0U) && (s_ui == MACHINE_LOCK_UI_NONE))
            {
                MachineLock_StartUi(target);
                return 1U;
            }
        }
    }
    else if (hold->was_pressed != 0U)
    {
        if (hold->long_fired != 0U)
        {
            (void)Key_TakePress(first_key);
            (void)Key_TakePress(second_key);
        }
        hold->was_pressed = 0U;
        hold->long_fired = 0U;
    }
    return 0U;
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_AdjustDigit）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_TaskUi。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_AdjustDigit(void)
{
    if (Key_TakePress(KEY_LEFT) != 0U)
    {
        s_selected_digit = (s_selected_digit == 0U) ?
                           7U : (uint8_t)(s_selected_digit - 1U);
    }
    if (Key_TakePress(KEY_RIGHT) != 0U)
    {
        s_selected_digit = (uint8_t)((s_selected_digit + 1U) % 8U);
    }
    if (Key_TakePress(KEY_UP) != 0U)
    {
        s_digits[s_selected_digit] =
            (uint8_t)((s_digits[s_selected_digit] + 1U) % 10U);
        s_code_edited = 1U;
    }
    if (Key_TakePress(KEY_DOWN) != 0U)
    {
        s_digits[s_selected_digit] =
            (s_digits[s_selected_digit] == 0U) ?
            9U : (uint8_t)(s_digits[s_selected_digit] - 1U);
        s_code_edited = 1U;
    }
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_RenderQuery）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_TaskUi。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_RenderQuery(void)
{
    switch (s_h_page)
    {
        case 1U:
            MachineLock_RenderEightDigitValue((uint8_t)'H', 1U, s_h_runtime);
            break;
        case 2U:
            MachineLock_RenderEightDigitValue((uint8_t)'H', 2U, s_h_coin_in);
            break;
        case 3U:
            MachineLock_RenderEightDigitValue((uint8_t)'H', 3U, s_h_coin_out);
            break;
        case 4U:
            MachineLock_RenderEightDigitValue((uint8_t)'H', 4U, s_h_check);
            break;
        case 5U:
            Segment_SetLeftHNumber(5U);
            Segment_SetMiddleRightSigned((int64_t)s_h_profit);
            break;
        case 6U:
        default:
            MachineLock_RenderEightDigitValue((uint8_t)'H', 6U, s_h_lock_count);
            break;
    }
}

/**
 * @brief 运行限时与解码锁：周期处理（对应符号 MachineLock_TaskUi）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_Task。
 * @局部状态 主要临时变量：value、expected；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void MachineLock_TaskUi(void)
{
    uint32_t value;
    uint32_t expected;

    if (s_ui == MACHINE_LOCK_UI_A0)
    {
        MachineLock_AdjustDigit();
        if ((s_code_edited != 0U) &&
            (MachineLock_DigitsToValue(s_digits) == s_staff_password))
        {
            MachineLock_StartUi(MACHINE_LOCK_UI_A1);
        }
        else if (Key_TakePress(KEY_CLEAR) != 0U)
        {
            s_ui = MACHINE_LOCK_UI_NONE;
        }
        if (s_ui == MACHINE_LOCK_UI_A0)
        {
            MachineLock_RenderDigits((uint8_t)'A', 0U);
        }
    }
    else if (s_ui == MACHINE_LOCK_UI_A1)
    {
        MachineLock_AdjustDigit();
        if (Key_TakePress(KEY_CLEAR) != 0U)
        {
            value = MachineLock_DigitsToValue(s_digits);
            if (value != s_interval_minutes)
            {
                s_interval_minutes = value;
                /* A changed interval always starts a fresh timing cycle. */
                s_cycle_runtime_minutes = 0U;
                MachineLock_Save();
            }
            MachineLock_StartUi(MACHINE_LOCK_UI_A2);
        }
        if (s_ui == MACHINE_LOCK_UI_A1)
        {
            MachineLock_RenderDigits((uint8_t)'A', 1U);
        }
    }
    else if (s_ui == MACHINE_LOCK_UI_A2)
    {
        expected = MachineLock_CalculateCode(s_h_runtime,
                                             s_h_coin_in,
                                             s_h_coin_out,
                                             s_h_check,
                                             s_h_profit,
                                             s_h_lock_count);
        if (Key_TakePress(KEY_CLEAR) != 0U)
        {
            s_ui = MACHINE_LOCK_UI_NONE;
        }
        if (s_ui == MACHINE_LOCK_UI_A2)
        {
            MachineLock_RenderEightDigitValue((uint8_t)'A', 2U, expected);
        }
    }
    else if (s_ui == MACHINE_LOCK_UI_H)
    {
        if ((Key_TakePress(KEY_UP) != 0U) ||
            (Key_TakePress(KEY_RIGHT) != 0U))
        {
            s_h_page = (s_h_page >= 6U) ? 1U : (uint8_t)(s_h_page + 1U);
        }
        if ((Key_TakePress(KEY_DOWN) != 0U) ||
            (Key_TakePress(KEY_LEFT) != 0U))
        {
            s_h_page = (s_h_page <= 1U) ? 6U : (uint8_t)(s_h_page - 1U);
        }
        if (Key_TakePress(KEY_ACCOUNT) != 0U)
        {
            s_ui = MACHINE_LOCK_UI_NONE;
        }
        if (s_ui == MACHINE_LOCK_UI_H)
        {
            MachineLock_RenderQuery();
        }
    }
    else if (s_ui == MACHINE_LOCK_UI_F0)
    {
        MachineLock_AdjustDigit();
        expected = MachineLock_CalculateCode(s_h_runtime,
                                             s_h_coin_in,
                                             s_h_coin_out,
                                             s_h_check,
                                             s_h_profit,
                                             s_h_lock_count);
        if ((s_code_edited != 0U) &&
            (s_check_valid != 0U) &&
            (MachineLock_DigitsToValue(s_digits) == expected))
        {
            s_locked = 0U;
            s_cycle_runtime_minutes = 0U;
            s_decode_prompt_start_ms = Gtim_NowMs();
            MachineLock_Save();
            s_ui = MACHINE_LOCK_UI_NONE;
        }
        else if (Key_TakePress(KEY_CLEAR) != 0U)
        {
            s_ui = MACHINE_LOCK_UI_NONE;
        }
        if (s_ui == MACHINE_LOCK_UI_F0)
        {
            MachineLock_RenderDigits((uint8_t)'F', 0U);
        }
    }
    else if (s_ui == MACHINE_LOCK_UI_U0)
    {
        MachineLock_AdjustDigit();
        if ((s_code_edited != 0U) &&
            (MachineLock_DigitsToValue(s_digits) ==
             MACHINE_LOCK_ADMIN_PASSWORD))
        {
            MachineLock_StartUi(MACHINE_LOCK_UI_U1);
        }
        else if (Key_TakePress(KEY_CLEAR) != 0U)
        {
            s_ui = MACHINE_LOCK_UI_NONE;
        }
        if (s_ui == MACHINE_LOCK_UI_U0)
        {
            MachineLock_RenderDigits((uint8_t)'U', 0U);
        }
    }
    else if (s_ui == MACHINE_LOCK_UI_U1)
    {
        MachineLock_AdjustDigit();
        if (Key_TakePress(KEY_CLEAR) != 0U)
        {
            s_staff_password = MachineLock_DigitsToValue(s_digits);
            MachineLock_Save();
            s_ui = MACHINE_LOCK_UI_NONE;
        }
        if (s_ui == MACHINE_LOCK_UI_U1)
        {
            MachineLock_RenderDigits((uint8_t)'U', 1U);
        }
    }
}

/**
 * @brief 运行限时与解码锁：初始化（对应符号 MachineLock_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @局部状态 主要临时变量：slot0、slot1、selected、valid0、valid1、previous_next_lock、remaining_minutes；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void MachineLock_Init(void)
{
    MachineLock_Record slot0;
    MachineLock_Record slot1;
    const MachineLock_Record *selected;
    uint8_t valid0;
    uint8_t valid1;
    uint32_t previous_next_lock;
    uint32_t remaining_minutes;

    memset(&s_hold_clear, 0, sizeof(s_hold_clear));
    memset(&s_hold_account, 0, sizeof(s_hold_account));
    memset(&s_hold_set, 0, sizeof(s_hold_set));
    memset(&s_hold_user, 0, sizeof(s_hold_user));
    s_ui = MACHINE_LOCK_UI_NONE;
    s_check_valid = 0U;
    s_minute_accumulator_ms = 0U;
    s_last_task_ms = Gtim_NowMs();
    s_decode_prompt_start_ms = s_last_task_ms;

    valid0 = MachineLock_ReadSlot(MACHINE_LOCK_SLOT0_ADDR, &slot0);
    valid1 = MachineLock_ReadSlot(MACHINE_LOCK_SLOT1_ADDR, &slot1);
    selected = (const MachineLock_Record *)0;
    s_have_record = 0U;
    s_active_slot = 0U;

    if ((valid0 != 0U) && (valid1 != 0U))
    {
        if ((int32_t)(slot1.sequence - slot0.sequence) > 0)
        {
            selected = &slot1;
            s_active_slot = 1U;
        }
        else
        {
            selected = &slot0;
        }
    }
    else if (valid0 != 0U)
    {
        selected = &slot0;
    }
    else if (valid1 != 0U)
    {
        selected = &slot1;
        s_active_slot = 1U;
    }

    if (selected != (const MachineLock_Record *)0)
    {
        s_sequence = selected->sequence;
        s_interval_minutes = selected->interval_minutes;
        s_total_runtime_minutes = selected->total_runtime_minutes;
        if (selected->version != MACHINE_LOCK_LEGACY_VERSION)
        {
            s_cycle_runtime_minutes = selected->cycle_runtime_minutes;
        }
        else
        {
            /* Version 1 stored an absolute next-lock total-runtime value. */
            previous_next_lock = selected->cycle_runtime_minutes;
            if (s_interval_minutes == 0U)
            {
                s_cycle_runtime_minutes = 0U;
            }
            else if (selected->locked != 0U)
            {
                s_cycle_runtime_minutes = s_interval_minutes;
            }
            else if (previous_next_lock > s_total_runtime_minutes)
            {
                remaining_minutes = previous_next_lock -
                                    s_total_runtime_minutes;
                s_cycle_runtime_minutes =
                    (remaining_minutes >= s_interval_minutes) ?
                    0U : s_interval_minutes - remaining_minutes;
            }
            else
            {
                s_cycle_runtime_minutes = s_interval_minutes;
            }
        }
        s_lock_runtime_minutes = selected->lock_runtime_minutes;
        s_lock_count = selected->lock_count;
        s_locked = selected->locked;
        s_staff_password = (selected->version == MACHINE_LOCK_VERSION) ?
                           selected->staff_password :
                           MACHINE_LOCK_DEFAULT_PASSWORD;
        s_have_record = 1U;
        if (selected->version != MACHINE_LOCK_VERSION)
        {
            MachineLock_Save();
        }
    }
    else
    {
        s_sequence = 0U;
        s_interval_minutes = 0U;
        s_total_runtime_minutes = 0U;
        s_cycle_runtime_minutes = 0U;
        s_lock_runtime_minutes = 0U;
        s_lock_count = 0U;
        s_locked = 0U;
        s_staff_password = MACHINE_LOCK_DEFAULT_PASSWORD;
    }

    s_random_state = 0xA5F1523DUL ^ s_total_runtime_minutes ^
                     (s_lock_count * 2654435761UL) ^ Gtim_NowMs();
    if (s_random_state == 0U)
    {
        s_random_state = 0x6D2B79F5UL;
    }
}

/**
 * @brief 运行限时与解码锁：周期处理（对应符号 MachineLock_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：now、elapsed、allow_entry、dual_pressed；均只在本次调用中有效。
 * @param post_done 按值传入；函数内部修改不会回写调用方变量。
 * @param safe_to_stop 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineLock_Task(uint8_t post_done, uint8_t safe_to_stop)
{
    uint32_t now;
    uint32_t elapsed;
    uint8_t allow_entry;
    uint8_t dual_pressed;

    now = Gtim_NowMs();
    elapsed = (uint32_t)(now - s_last_task_ms);
    s_last_task_ms = now;
    s_minute_accumulator_ms += elapsed;
    while (s_minute_accumulator_ms >= MACHINE_LOCK_MINUTE_MS)
    {
        s_minute_accumulator_ms -= MACHINE_LOCK_MINUTE_MS;
        s_total_runtime_minutes++;
        if ((s_locked == 0U) && (s_interval_minutes != 0U))
        {
            s_cycle_runtime_minutes++;
        }
        /* 上币START时只更新RAM分钟数，稍后在安全状态一次性落盘。 */
        s_runtime_save_pending = 1U;
    }

    if ((s_runtime_save_pending != 0U) && (safe_to_stop != 0U))
    {
        MachineLock_Save();
        s_runtime_save_pending = 0U;
    }

    if (post_done == 0U)
    {
        return 0U;
    }

    if ((s_locked == 0U) &&
        (s_ui == MACHINE_LOCK_UI_NONE) &&
        (safe_to_stop != 0U) &&
        (s_interval_minutes != 0U) &&
        (s_cycle_runtime_minutes >= s_interval_minutes) &&
        (MachineLock_GetCurrentProfit() > 0LL))
    {
        s_locked = 1U;
        s_lock_runtime_minutes = s_total_runtime_minutes;
        s_lock_count++;
        s_check_valid = 0U;
        s_decode_prompt_start_ms = now;
        MachineLock_Save();
    }

    if (s_locked != 0U)
    {
        if (((uint32_t)(now - s_decode_prompt_start_ms) >=
             MACHINE_LOCK_DECODE_PROMPT_MS) &&
            (Audio_IsLoudSequenceBusy() == 0U) &&
            (Audio_PlayLoudIndexOn(AUDIO_CHIP_1,
                                   MACHINE_LOCK_DECODE_PROMPT_INDEX,
                                   0U) != 0U))
        {
            s_decode_prompt_start_ms = now;
        }
    }
    else
    {
        s_decode_prompt_start_ms = now;
    }

    allow_entry = ((safe_to_stop != 0U) || (s_locked != 0U)) ? 1U : 0U;
    dual_pressed = ((Key_IsPressed(KEY_ACCOUNT) != 0U) &&
                    (Key_IsPressed(KEY_SET) != 0U)) ? 1U : 0U;
    (void)MachineLock_UpdateDualHold(&s_hold_user,
                                     KEY_ACCOUNT,
                                     KEY_SET,
                                     MACHINE_LOCK_UI_U0,
                                     allow_entry);
    (void)MachineLock_UpdateHold(&s_hold_clear,
                                 KEY_CLEAR,
                                 MACHINE_LOCK_UI_A0,
                                 allow_entry);
    (void)MachineLock_UpdateHold(&s_hold_account,
                                 KEY_ACCOUNT,
                                 MACHINE_LOCK_UI_H,
                                 (dual_pressed == 0U) ? allow_entry : 0U);
    (void)MachineLock_UpdateHold(&s_hold_set,
                                 KEY_SET,
                                 MACHINE_LOCK_UI_F0,
                                 (dual_pressed == 0U) ? allow_entry : 0U);

    if (s_ui != MACHINE_LOCK_UI_NONE)
    {
        MachineLock_TaskUi();
    }

    if ((s_ui == MACHINE_LOCK_UI_NONE) && (s_locked != 0U))
    {
        MachineLock_RenderLocked();
    }

    return MachineLock_IsBlocking();
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_IsBlocking）。
 * @调用关系 直接调用者：game.c:Game_Task、machine_lock.c:MachineLock_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineLock_IsBlocking(void)
{
    return ((s_locked != 0U) ||
            (s_ui != MACHINE_LOCK_UI_NONE)) ? 1U : 0U;
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_IsLocked）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineLock_IsLocked(void)
{
    return s_locked;
}

void MachineLock_ResetStaffPassword(void)
{
    if (s_staff_password != MACHINE_LOCK_DEFAULT_PASSWORD)
    {
        s_staff_password = MACHINE_LOCK_DEFAULT_PASSWORD;
        MachineLock_Save();
    }
}

/**
 * @brief 运行限时与解码锁：执行内部处理（对应符号 MachineLock_CalculateCode）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_TaskUi。
 * @局部状态 主要临时变量：formula、profit_mod、value；均只在本次调用中有效。
 * @param runtime_minutes 按值传入；函数内部修改不会回写调用方变量。
 * @param coin_in 按值传入；函数内部修改不会回写调用方变量。
 * @param coin_out 按值传入；函数内部修改不会回写调用方变量。
 * @param check_code 按值传入；函数内部修改不会回写调用方变量。
 * @param profit 按值传入；函数内部修改不会回写调用方变量。
 * @param lock_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t MachineLock_CalculateCode(uint32_t runtime_minutes,
                                   uint32_t coin_in,
                                   uint32_t coin_out,
                                   uint32_t check_code,
                                   int32_t profit,
                                   uint32_t lock_count)
{
    uint32_t formula;
    uint32_t profit_mod;
    uint64_t value;

    formula = check_code / 10000000UL;
    if (profit >= 0)
    {
        profit_mod = (uint32_t)profit % MACHINE_LOCK_CODE_MODULO;
    }
    else
    {
        profit_mod = (MACHINE_LOCK_CODE_MODULO -
                     ((uint32_t)(-profit) % MACHINE_LOCK_CODE_MODULO)) %
                     MACHINE_LOCK_CODE_MODULO;
    }

    if (formula == 1U)
    {
        value = (uint64_t)runtime_minutes * 37ULL +
                (uint64_t)coin_in * 17ULL +
                (uint64_t)coin_out * 29ULL +
                (uint64_t)profit_mod * 13ULL +
                (uint64_t)lock_count * 97ULL +
                (uint64_t)check_code * 7ULL + 31415926ULL;
    }
    else if (formula == 2U)
    {
        value = (uint64_t)runtime_minutes * 61ULL +
                (uint64_t)coin_in * 43ULL +
                (uint64_t)coin_out * 11ULL +
                (uint64_t)profit_mod * 19ULL +
                (uint64_t)lock_count * 131ULL +
                (uint64_t)check_code * 3ULL + 27182818ULL;
    }
    else
    {
        value = (uint64_t)runtime_minutes * 23ULL +
                (uint64_t)coin_in * 71ULL +
                (uint64_t)coin_out * 53ULL +
                (uint64_t)profit_mod * 31ULL +
                (uint64_t)lock_count * 173ULL +
                (uint64_t)check_code * 9ULL + 16180339ULL;
    }

    while (value >= (uint64_t)MACHINE_LOCK_CODE_MODULO)
    {
        value /= 10ULL;
    }
    return (uint32_t)value;
}
