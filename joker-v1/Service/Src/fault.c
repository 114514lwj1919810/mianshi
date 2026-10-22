/* AUTO-FILE-DOC
 * 文件职责：故障管理。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "fault.h"

#include "audio_manager.h"
#include "coin.h"
#include "eye.h"
#include "game.h"
#include "gtim.h"
#include "motor.h"
#include "payout.h"
#include "segment.h"
#include "shake.h"
#include "ticket.h"

#define FAULT_ACTIVE_HOLD_MS       3000U
#define FAULT_NO_COUNT_TIMEOUT_MS   8000U
#define FAULT_SHAKE_RECOVER_MS     5000U
#define FAULT_AUDIO_SHAKE_ALARM    9U
#define FAULT_SHAKE_PULSE_WINDOW_MS 1000U
#define FAULT_SHAKE_PULSE_THRESHOLD 3U

static Fault_Code s_fault_code = FAULT_CODE_NONE;
static uint32_t s_fault_start_ms = 0U;
static uint32_t s_channel_active_start_ms[EYE_CHANNEL_COUNT];
static uint32_t s_counter_active_start_ms[EYE_COUNTER_COUNT];
static uint32_t s_coin_active_start_ms = 0U;
static uint32_t s_shake_active_start_ms = 0U;
static uint32_t s_shake_pulse_window_start_ms = 0U;
static uint32_t s_shake_pulse_count = 0U;
static uint32_t s_game_motor_start_ms = 0U;
static uint8_t s_payout_was_busy = 0U;
static uint16_t s_payout_last_remaining = 0U;
static uint32_t s_payout_last_count_ms = 0U;

static void Fault_ResetTimers(void);
static uint8_t Fault_IsHeld(uint8_t active, uint32_t *start_ms, uint32_t hold_ms);
static uint8_t Fault_IsCodeClear(Fault_Code code);
static void Fault_CheckChannelJam(void);
static void Fault_CheckCounterJam(Eye_Counter counter, Fault_Code code);
static void Fault_CheckCoinJam(void);
static void Fault_CheckShake(void);
static void Fault_CheckGameMotorNoCount(void);
static void Fault_CheckPayoutNoCount(void);

/**
 * @brief 故障管理：初始化（对应符号 Fault_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Fault_Init(void)
{
    s_fault_code = FAULT_CODE_NONE;
    s_fault_start_ms = 0U;
    Fault_ResetTimers();
    Segment_SetFaultCode(0U);
}

/**
 * @brief 故障管理：周期处理（对应符号 Fault_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Fault_Task(void)
{
    if (s_fault_code != FAULT_CODE_NONE)
    {
        if (s_fault_code != FAULT_CODE_E14)
        {
            Segment_SetFaultCode((uint8_t)s_fault_code);
        }
        if ((s_fault_code == FAULT_CODE_E11) ||
            (s_fault_code == FAULT_CODE_E12) ||
            (s_fault_code == FAULT_CODE_E16) ||
            (s_fault_code == FAULT_CODE_E17))
        {
            /*
             * E11/E12空币、E16上通道卡币以及E17主机通讯超时都是锁存故障。
             * 补币或清理通道后不能自行恢复，必须由操作员按故障清除键。
             */
        }
        else if (s_fault_code == FAULT_CODE_E15)
        {
            if (Gtim_IsElapsed(s_fault_start_ms, FAULT_SHAKE_RECOVER_MS) != 0U)
            {
                Fault_Clear();
            }
        }
        else if (Fault_IsCodeClear(s_fault_code) != 0U)
        {
            Fault_Clear();
        }
        return;
    }

    Fault_CheckChannelJam();
    Fault_CheckCounterJam(EYE_COUNTER_SOLENOID_CHECK, FAULT_CODE_E10);
    Fault_CheckCounterJam(EYE_COUNTER_GAME_MOTOR, FAULT_CODE_E11);
    Fault_CheckCounterJam(EYE_COUNTER_SPIT_COIN, FAULT_CODE_E12);
    Fault_CheckCoinJam();
    Fault_CheckShake();
    Fault_CheckGameMotorNoCount();
    Fault_CheckPayoutNoCount();

    if ((s_fault_code == FAULT_CODE_NONE) && (Ticket_HasFault() != 0U))
    {
        Fault_Report(FAULT_CODE_E14);
    }
}

/**
 * @brief 故障管理：执行内部处理（对应符号 Fault_IsActive）。
 * @调用关系 直接调用者：game.c:Game_Task、remote_config.c:Remote_DataService。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Fault_IsActive(void)
{
    return (s_fault_code != FAULT_CODE_NONE) ? 1U : 0U;
}

/**
 * @brief 故障管理：读取（对应符号 Fault_GetCode）。
 * @调用关系 直接调用者：game.c:Game_Task、remote_config.c:Remote_DataService。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
Fault_Code Fault_GetCode(void)
{
    return s_fault_code;
}

/**
 * @brief 故障管理：执行内部处理（对应符号 Fault_Report）。
 * @调用关系 直接调用者：fault.c:Fault_Task、fault.c:Fault_CheckChannelJam、fault.c:Fault_CheckCounterJam、fault.c:Fault_CheckCoinJam、fault.c:Fault_CheckShake、fault.c:Fault_CheckGameMotorNoCount、fault.c:Fault_CheckPayoutNoCount、game.c:Game_Task。
 * @param code 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Fault_Report(Fault_Code code)
{
    if ((code == FAULT_CODE_NONE) || (s_fault_code != FAULT_CODE_NONE))
    {
        return;
    }

    s_fault_code = code;
    s_fault_start_ms = Gtim_NowMs();
    if (s_fault_code != FAULT_CODE_E14)
    {
        Segment_SetFaultCode((uint8_t)s_fault_code);
    }
    if (s_fault_code == FAULT_CODE_E15)
    {
        /*
         * E15 期间整机只允许播放 09 晃动报警音：
         * 专用入口会立即清空并停止两块板，把09循环设为音效板唯一命令；
         * 故障解除前，投币、按键、抽奖和背景音乐命令都会被底层拒绝。
         */
        Audio_StartExclusiveAlarm(FAULT_AUDIO_SHAKE_ALARM);
    }
}

/**
 * @brief 故障管理：清除（对应符号 Fault_Clear）。
 * @调用关系 直接调用者：fault.c:Fault_Task、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Fault_Clear(void)
{
    if (s_fault_code == FAULT_CODE_E15)
    {
        /* 清掉仍在等待的报警命令并立即停止当前09循环。 */
        Audio_StopExclusiveAlarm();
    }

    s_fault_code = FAULT_CODE_NONE;
    s_fault_start_ms = 0U;
    Fault_ResetTimers();
    Shake_Clear();
    Segment_SetFaultCode(0U);
}

/**
 * @brief 故障管理：设置（对应符号 Fault_ResetTimers）。
 * @调用关系 直接调用者：fault.c:Fault_Init、fault.c:Fault_Clear。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Fault_ResetTimers(void)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)EYE_CHANNEL_COUNT; i++)
    {
        s_channel_active_start_ms[i] = 0U;
    }

    for (i = 0U; i < (uint8_t)EYE_COUNTER_COUNT; i++)
    {
        s_counter_active_start_ms[i] = 0U;
    }

    s_coin_active_start_ms = 0U;
    s_shake_active_start_ms = 0U;
    s_shake_pulse_window_start_ms = 0U;
    s_shake_pulse_count = 0U;
    s_game_motor_start_ms = 0U;
    s_payout_was_busy = 0U;
    s_payout_last_remaining = 0U;
    s_payout_last_count_ms = 0U;
}

/**
 * @brief 故障管理：执行内部处理（对应符号 Fault_IsHeld）。
 * @调用关系 直接调用者：fault.c:Fault_CheckChannelJam、fault.c:Fault_CheckCounterJam、fault.c:Fault_CheckCoinJam、fault.c:Fault_CheckShake、fault.c:Fault_CheckGameMotorNoCount。
 * @param active 按值传入；函数内部修改不会回写调用方变量。
 * @param start_ms 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param hold_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Fault_IsHeld(uint8_t active, uint32_t *start_ms, uint32_t hold_ms)
{
    if (active == 0U)
    {
        *start_ms = 0U;
        return 0U;
    }

    if (*start_ms == 0U)
    {
        *start_ms = Gtim_NowMs();
        return 0U;
    }

    return Gtim_IsElapsed(*start_ms, hold_ms);
}

/**
 * @brief 故障管理：清除（对应符号 Fault_IsCodeClear）。
 * @调用关系 直接调用者：fault.c:Fault_Task。
 * @param code 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Fault_IsCodeClear(Fault_Code code)
{
    if ((code >= FAULT_CODE_E01) && (code <= FAULT_CODE_E09))
    {
        return ((Eye_GetChannelActiveMask() &
                 (uint16_t)(1UL << ((uint8_t)code - (uint8_t)FAULT_CODE_E01))) == 0U) ? 1U : 0U;
    }

    switch (code)
    {
        case FAULT_CODE_E10:
            return (Eye_IsCounterActive(EYE_COUNTER_SOLENOID_CHECK) == 0U) ? 1U : 0U;

        case FAULT_CODE_E11:
            return (Eye_IsCounterActive(EYE_COUNTER_GAME_MOTOR) == 0U) ? 1U : 0U;

        case FAULT_CODE_E12:
            return (Eye_IsCounterActive(EYE_COUNTER_SPIT_COIN) == 0U) ? 1U : 0U;

        case FAULT_CODE_E13:
            return (Coin_IsActive() == 0U) ? 1U : 0U;

        case FAULT_CODE_E14:
            return (Ticket_HasFault() == 0U) ? 1U : 0U;

        case FAULT_CODE_E15:
            return 1U;

        case FAULT_CODE_E16:
            /* 锁存故障，只允许Game_Task收到KEY_CLEAR后主动清除。 */
            return 0U;

        case FAULT_CODE_E17:
            /* Game_Task等待主机恢复，或在人工清除时原子隔离未完成的彩金申请。 */
            return 0U;

        case FAULT_CODE_NONE:
        default:
            return 1U;
    }
}
/**
 * @brief 故障管理：检查（对应符号 Fault_CheckChannelJam）。
 * @调用关系 直接调用者：fault.c:Fault_Task。
 * @局部状态 主要临时变量：i、mask；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Fault_CheckChannelJam(void)
{
    uint8_t i;
    uint16_t mask;

    mask = Eye_GetChannelActiveMask();
    for (i = 0U; i < (uint8_t)EYE_CHANNEL_COUNT; i++)
    {
        if (Fault_IsHeld(((mask & (uint16_t)(1UL << i)) != 0U) ? 1U : 0U,
                         &s_channel_active_start_ms[i],
                         FAULT_ACTIVE_HOLD_MS) != 0U)
        {
            Fault_Report((Fault_Code)((uint8_t)FAULT_CODE_E01 + i));
            return;
        }
    }
}

/**
 * @brief 故障管理：检查（对应符号 Fault_CheckCounterJam）。
 * @调用关系 直接调用者：fault.c:Fault_Task。
 * @param counter 按值传入；函数内部修改不会回写调用方变量。
 * @param code 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Fault_CheckCounterJam(Eye_Counter counter, Fault_Code code)
{
    if (s_fault_code != FAULT_CODE_NONE)
    {
        return;
    }

    if (Fault_IsHeld(Eye_IsCounterActive(counter),
                     &s_counter_active_start_ms[(uint8_t)counter],
                     FAULT_ACTIVE_HOLD_MS) != 0U)
    {
        Fault_Report(code);
    }
}

/**
 * @brief 故障管理：检查（对应符号 Fault_CheckCoinJam）。
 * @调用关系 直接调用者：fault.c:Fault_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Fault_CheckCoinJam(void)
{
    if (s_fault_code != FAULT_CODE_NONE)
    {
        return;
    }

    if (Fault_IsHeld(Coin_IsActive(), &s_coin_active_start_ms, FAULT_ACTIVE_HOLD_MS) != 0U)
    {
        Fault_Report(FAULT_CODE_E13);
    }
}

/**
 * @brief 故障管理：检查（对应符号 Fault_CheckShake）。
 * @调用关系 直接调用者：fault.c:Fault_Task。
 * @局部状态 主要临时变量：shake_count、now_ms；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Fault_CheckShake(void)
{
    uint32_t shake_count;
    uint32_t now_ms;

    if (s_fault_code != FAULT_CODE_NONE)
    {
        return;
    }

    /*
     * 防摇器长线和未接插座容易受电机/电磁铁瞬态干扰。
     * 单个PF8下降沿不再立刻报E15；1秒内累计3个有效边沿才视为摇晃。
     * 真正持续拉低3秒的防摇信号仍由下面的电平保持检测报告E15。
     */
    shake_count = Shake_TakeCount();
    now_ms = Gtim_NowMs();
    if (shake_count != 0U)
    {
        if ((s_shake_pulse_count == 0U) ||
            (Gtim_IsElapsed(s_shake_pulse_window_start_ms,
                            FAULT_SHAKE_PULSE_WINDOW_MS) != 0U))
        {
            s_shake_pulse_window_start_ms = now_ms;
            s_shake_pulse_count = 0U;
        }

        if ((0xFFFFFFFFUL - s_shake_pulse_count) < shake_count)
        {
            s_shake_pulse_count = 0xFFFFFFFFUL;
        }
        else
        {
            s_shake_pulse_count += shake_count;
        }

        if (s_shake_pulse_count >= FAULT_SHAKE_PULSE_THRESHOLD)
        {
            Fault_Report(FAULT_CODE_E15);
            return;
        }
    }
    else if ((s_shake_pulse_count != 0U) &&
             (Gtim_IsElapsed(s_shake_pulse_window_start_ms,
                             FAULT_SHAKE_PULSE_WINDOW_MS) != 0U))
    {
        s_shake_pulse_window_start_ms = 0U;
        s_shake_pulse_count = 0U;
    }

    if (Fault_IsHeld(Shake_IsActive(), &s_shake_active_start_ms, FAULT_ACTIVE_HOLD_MS) != 0U)
    {
        Fault_Report(FAULT_CODE_E15);
    }
}

/**
 * @brief 故障管理：检查（对应符号 Fault_CheckGameMotorNoCount）。
 * @调用关系 直接调用者：fault.c:Fault_Task。
 * @局部状态 主要临时变量：motor_running；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Fault_CheckGameMotorNoCount(void)
{
    uint8_t motor_running;

    if (s_fault_code != FAULT_CODE_NONE)
    {
        return;
    }

    motor_running = ((Game_GetMode() == GAME_MODE_START) &&
                     (Motor_GetDirection(MOTOR_GAME_COIN) != MOTOR_DIR_STOP)) ? 1U : 0U;
    if (Eye_GetCounter(EYE_COUNTER_GAME_MOTOR) != 0U)
    {
        s_game_motor_start_ms = 0U;
        return;
    }

    if (Fault_IsHeld(motor_running, &s_game_motor_start_ms, FAULT_NO_COUNT_TIMEOUT_MS) != 0U)
    {
        Fault_Report(FAULT_CODE_E11);
    }
}

/**
 * @brief 故障管理：检查（对应符号 Fault_CheckPayoutNoCount）。
 * @调用关系 直接调用者：fault.c:Fault_Task。
 * @局部状态 主要临时变量：remaining；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Fault_CheckPayoutNoCount(void)
{
    uint16_t remaining;

    if (s_fault_code != FAULT_CODE_NONE)
    {
        return;
    }

    if ((Payout_IsBusy() == 0U) || (Payout_GetRemaining() == 0U) ||
        (Payout_IsPaused() != 0U))
    {
        s_payout_was_busy = 0U;
        s_payout_last_remaining = 0U;
        s_payout_last_count_ms = 0U;
        return;
    }

    remaining = Payout_GetRemaining();
    if ((s_payout_was_busy == 0U) || (remaining != s_payout_last_remaining))
    {
        s_payout_was_busy = 1U;
        s_payout_last_remaining = remaining;
        s_payout_last_count_ms = Gtim_NowMs();
        return;
    }

    if (Gtim_IsElapsed(s_payout_last_count_ms, FAULT_NO_COUNT_TIMEOUT_MS) != 0U)
    {
        Fault_Report(FAULT_CODE_E12);
    }
}
