/* AUTO-FILE-DOC
 * 文件职责：奖励下发流程。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "payout.h"

#include "eye.h"
#include "motor.h"

typedef enum
{
    PAYOUT_PHASE_IDLE = 0U,
    PAYOUT_PHASE_RUNNING
} Payout_Phase;

static volatile Payout_Phase s_phase = PAYOUT_PHASE_IDLE;
static volatile uint16_t s_target_count = 0U;
static volatile uint16_t s_remaining_count = 0U;
static volatile uint16_t s_paid_pending = 0U;
static volatile uint8_t s_exchange_request = 0U;
static volatile uint8_t s_ticket_request = 0U;
static uint8_t s_paused = 0U;

/**
 * @brief 奖励下发流程：初始化（对应符号 Payout_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Payout_Init(void)
{
    s_paused = 0U;
    s_phase = PAYOUT_PHASE_IDLE;
    s_target_count = 0U;
    s_remaining_count = 0U;
    s_paid_pending = 0U;
    s_exchange_request = 0U;
    s_ticket_request = 0U;
    Motor_Stop(MOTOR_WIN_PRIZE);
}

/**
 * @brief 奖励下发流程：执行内部处理（对应符号 Payout_RequestExchange）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Payout_RequestExchange(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_exchange_request = 1U;
}

/**
 * @brief 奖励下发流程：执行内部处理（对应符号 Payout_RequestTicket）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Payout_RequestTicket(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_ticket_request = 1U;
}

/**
 * @brief 奖励下发流程：启动（对应符号 Payout_StartScore）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param payout_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Payout_StartScore(uint16_t payout_count)
{
    return Payout_AddPrize(payout_count);
}

/**
 * @brief 奖励下发流程：执行内部处理（对应符号 Payout_AddPrize）。
 * @调用关系 直接调用者：game.c:Game_AddLotteryPrizeAndPayout、game.c:Game_FinishTicketSkipHold、game.c:Game_Task、payout.c:Payout_StartScore。
 * @局部状态 主要临时变量：next_target、next_remaining；均只在本次调用中有效。
 * @param payout_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Payout_AddPrize(uint16_t payout_count)
{
    uint32_t next_target;
    uint32_t next_remaining;

    if (payout_count == 0U)
    {
        return 0U;
    }

    if (s_phase == PAYOUT_PHASE_RUNNING)
    {
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        next_target = (uint32_t)s_target_count + payout_count;
        next_remaining = (uint32_t)s_remaining_count + payout_count;
        s_target_count = (next_target > 65535U) ? 65535U : (uint16_t)next_target;
        s_remaining_count = (next_remaining > 65535U) ? 65535U : (uint16_t)next_remaining;
        s_exchange_request = 0U;
        s_ticket_request = 0U;
        return 1U;
    }

    s_target_count = payout_count;
    s_remaining_count = payout_count;
    s_paid_pending = 0U;
    s_exchange_request = 0U;
    s_ticket_request = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Eye_ClearCounter(EYE_COUNTER_SPIT_COIN);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (s_paused == 0U)
    {
        Motor_RunForward(MOTOR_WIN_PRIZE);
    }
    s_phase = PAYOUT_PHASE_RUNNING;

    return 1U;
}

/**
 * @brief 奖励下发流程：周期处理（对应符号 Payout_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：spit_count；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Payout_Task(void)
{
    uint32_t spit_count;

    if (s_phase != PAYOUT_PHASE_RUNNING)
    {
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    spit_count = Eye_TakeCounter(EYE_COUNTER_SPIT_COIN);

    while ((spit_count != 0U) && (s_remaining_count != 0U))
    {
        spit_count--;
        s_remaining_count--;
        s_paid_pending++;
    }

    if (s_remaining_count == 0U)
    {
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        Motor_Stop(MOTOR_WIN_PRIZE);
        s_target_count = 0U;
        s_phase = PAYOUT_PHASE_IDLE;
    }
}

/**
 * @brief 奖励下发流程：读取并清除一次性事件（对应符号 Payout_TakePaidCount）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：result；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Payout_TakePaidCount(void)
{
    uint16_t result;

    result = s_paid_pending;
    s_paid_pending = 0U;

    return result;
}

/**
 * @brief 奖励下发流程：读取（对应符号 Payout_GetRemaining）。
 * @调用关系 直接调用者：fault.c:Fault_CheckPayoutNoCount、game.c:Game_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Payout_GetRemaining(void)
{
    return s_remaining_count;
}

/**
 * @brief 奖励下发流程：执行内部处理（对应符号 Payout_IsBusy）。
 * @调用关系 直接调用者：fault.c:Fault_CheckPayoutNoCount、game.c:Game_IsJackpotOutputComplete、game.c:Game_UpdateLine7Settlement、game.c:Game_ProcessClearAccountRequest、game.c:Game_Task、standby.c:Standby_CanStart。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Payout_IsBusy(void)
{
    return (s_phase == PAYOUT_PHASE_RUNNING) ? 1U : 0U;
}

void Payout_Pause(void)
{
    s_paused = 1U;
    Motor_Stop(MOTOR_WIN_PRIZE);
}

void Payout_Resume(void)
{
    s_paused = 0U;
    if (s_remaining_count != 0U)
    {
        Motor_RunForward(MOTOR_WIN_PRIZE);
    }
}

uint8_t Payout_IsPaused(void)
{
    return s_paused;
}

uint16_t Payout_SkipRemaining(void)
{
    uint16_t skipped;
    Motor_Stop(MOTOR_WIN_PRIZE);
    /* 保留停车惯性产生的真实脉冲，不能把它们算成两次出币。 */
    Payout_Task();
    skipped = s_remaining_count;
    s_remaining_count = 0U;
    s_target_count = 0U;
    s_phase = PAYOUT_PHASE_IDLE;
    s_paused = 0U;
    return skipped;
}
