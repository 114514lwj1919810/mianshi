/* AUTO-FILE-DOC
 * 文件职责：出票器状态机。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "ticket.h"

#include "eye.h"
#include "gtim.h"

typedef enum
{
    TICKET_PHASE_IDLE = 0U,
    TICKET_PHASE_RUNNING,
    TICKET_PHASE_PAUSED,
    TICKET_PHASE_FAULT
} Ticket_Phase;

static volatile uint32_t s_remaining_count = 0U;
static volatile uint32_t s_dispensed_pending = 0U;
static uint32_t s_last_feedback_ms = 0U;
static Ticket_Phase s_phase = TICKET_PHASE_IDLE;

/** @brief 把 PD10 写成停止电平。 */
/**
 * @brief 出票器状态机：写入（对应符号 Ticket_WriteStop）。
 * @调用关系 直接调用者：ticket.c:Ticket_Init、ticket.c:Ticket_Task、ticket.c:Ticket_SkipRemaining、ticket.c:Ticket_ClearFault、ticket.c:Ticket_Pause、ticket.c:Ticket_Stop。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Ticket_WriteStop(void)
{
    HAL_GPIO_WritePin(TICKET_IN_PORT, TICKET_IN_PIN, TICKET_STOP_LEVEL);
}

/** @brief 把 PD10 写成运行电平，通过 CN2-H7 低端驱动启动彩票器。 */
/**
 * @brief 出票器状态机：写入（对应符号 Ticket_WriteRun）。
 * @调用关系 直接调用者：ticket.c:Ticket_Start。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Ticket_WriteRun(void)
{
    HAL_GPIO_WritePin(TICKET_IN_PORT, TICKET_IN_PIN, TICKET_RUN_LEVEL);
}

/** @brief 从空闲态启动一次闭环出票，启动前清掉 PF9 的历史边沿。 */
/**
 * @brief 出票器状态机：启动（对应符号 Ticket_Start）。
 * @调用关系 直接调用者：ticket.c:Ticket_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Ticket_Start(void)
{
    if (s_remaining_count == 0U)
    {
        return;
    }

    Eye_ClearCounter(EYE_COUNTER_TICKET_OUT);
    s_last_feedback_ms = Gtim_NowMs();
    s_phase = TICKET_PHASE_RUNNING;
    Ticket_WriteRun();
}

/**
 * @brief 出票器状态机：初始化（对应符号 Ticket_BootSafeInit）。
 * @调用关系 直接调用者：main.c:main。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Ticket_BootSafeInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();
    HAL_GPIO_WritePin(TICKET_IN_PORT, TICKET_IN_PIN, TICKET_STOP_LEVEL);
    gpio.Pin = TICKET_IN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TICKET_IN_PORT, &gpio);
}

/**
 * @brief 出票器状态机：初始化（对应符号 Ticket_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Ticket_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOD_CLK_ENABLE();

    /* 先写停止值再切输出，避免 GPIO 模式切换时在 H7 上产生一次误动作。 */
    HAL_GPIO_WritePin(TICKET_IN_PORT, TICKET_IN_PIN, TICKET_STOP_LEVEL);

    gpio.Pin = TICKET_IN_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TICKET_IN_PORT, &gpio);

    Ticket_WriteStop();
    s_remaining_count = 0U;
    s_dispensed_pending = 0U;
    s_last_feedback_ms = 0U;
    s_phase = TICKET_PHASE_IDLE;
}

/**
 * @brief 出票器状态机：周期处理（对应符号 Ticket_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：feedback_count、confirmed_count、next_dispensed；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Ticket_Task(void)
{
    uint32_t feedback_count;
    uint32_t confirmed_count;
    uint32_t next_dispensed;

    if (s_phase == TICKET_PHASE_IDLE)
    {
        Ticket_WriteStop();
        Ticket_Start();
        return;
    }

    if (s_phase != TICKET_PHASE_RUNNING)
    {
        Ticket_WriteStop();
        return;
    }

    feedback_count = Eye_TakeCounter(EYE_COUNTER_TICKET_OUT);
    if (feedback_count != 0U)
    {
        confirmed_count = feedback_count;
        if (confirmed_count > s_remaining_count)
        {
            confirmed_count = s_remaining_count;
        }

        s_remaining_count -= confirmed_count;
        next_dispensed = s_dispensed_pending + confirmed_count;
        if (next_dispensed < s_dispensed_pending)
        {
            next_dispensed = 0xFFFFFFFFUL;
        }
        s_dispensed_pending = next_dispensed;
        s_last_feedback_ms = Gtim_NowMs();
    }

    if (s_remaining_count == 0U)
    {
        Ticket_WriteStop();
        s_phase = TICKET_PHASE_IDLE;
        s_last_feedback_ms = 0U;
        return;
    }

    if (Gtim_IsElapsed(s_last_feedback_ms, TICKET_NO_FEEDBACK_TIMEOUT_MS) != 0U)
    {
        /* 保留剩余数量；排除缺票、卡票或 OUT 断线后，清故障即可继续。 */
        Ticket_WriteStop();
        s_phase = TICKET_PHASE_FAULT;
    }
}

/**
 * @brief 出票器状态机：执行内部处理（对应符号 Ticket_Add）。
 * @调用关系 直接调用者：ticket.c:Ticket_Test1、game.c:Game_AddLotteryPrizeAndPayout、game.c:Game_AddLinePrizeAndHold、game.c:Game_StartCardPayout、game.c:Game_Task。
 * @局部状态 主要临时变量：next_count、primask；均只在本次调用中有效。
 * @param ticket_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Ticket_Add(uint32_t ticket_count)
{
    uint32_t next_count;
    uint32_t primask;

    if (ticket_count == 0U)
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    next_count = s_remaining_count + ticket_count;
    if ((next_count < s_remaining_count) || (next_count > TICKET_PENDING_MAX))
    {
        if (primask == 0U)
        {
            __enable_irq();
        }
        return 0U;
    }

    s_remaining_count = next_count;

    if (primask == 0U)
    {
        __enable_irq();
    }

    return 1U;
}

/**
 * @brief 出票器状态机：读取并清除一次性事件（对应符号 Ticket_TakeDispensedCount）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Ticket_TakeDispensedCount(void)
{
    uint32_t result;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    result = s_dispensed_pending;
    s_dispensed_pending = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return result;
}

/**
 * @brief 出票器状态机：读取（对应符号 Ticket_GetRemaining）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Ticket_GetRemaining(void)
{
    return s_remaining_count;
}

/**
 * @brief 出票器状态机：执行内部处理（对应符号 Ticket_SkipRemaining）。
 * @调用关系 直接调用者：game.c:Game_TrySkipTicketOutput。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Ticket_SkipRemaining(void)
{
    uint32_t result;
    uint32_t primask;

    Ticket_WriteStop();

    primask = __get_PRIMASK();
    __disable_irq();
    result = s_remaining_count;
    s_remaining_count = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    s_last_feedback_ms = 0U;
    s_phase = TICKET_PHASE_IDLE;
    Eye_ClearCounter(EYE_COUNTER_TICKET_OUT);
    return result;
}

/**
 * @brief 出票器状态机：执行内部处理（对应符号 Ticket_IsBusy）。
 * @调用关系 直接调用者：game.c:Game_IsJackpotOutputComplete、game.c:Game_TrySkipTicketOutput、game.c:Game_UpdateLine7Settlement、game.c:Game_ProcessClearAccountRequest、game.c:Game_Task、standby.c:Standby_CanStart。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Ticket_IsBusy(void)
{
    /* PAUSED 仍代表存在尚未完成的出票批次，只是输出脚暂时处于停止电平。 */
    return ((s_phase == TICKET_PHASE_RUNNING) ||
            (s_phase == TICKET_PHASE_PAUSED)) ? 1U : 0U;
}

/**
 * @brief 出票器状态机：执行内部处理（对应符号 Ticket_HasFault）。
 * @调用关系 直接调用者：fault.c:Fault_Task、fault.c:Fault_IsCodeClear。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Ticket_HasFault(void)
{
    return (s_phase == TICKET_PHASE_FAULT) ? 1U : 0U;
}

/**
 * @brief 出票器状态机：清除（对应符号 Ticket_ClearFault）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Ticket_ClearFault(void)
{
    if (s_phase != TICKET_PHASE_FAULT)
    {
        return;
    }

    Ticket_WriteStop();
    s_last_feedback_ms = 0U;
    s_phase = TICKET_PHASE_IDLE;
}

/**
 * @brief 出票器状态机：执行内部处理（对应符号 Ticket_Pause）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Ticket_Pause(void)
{
    if (s_phase != TICKET_PHASE_RUNNING)
    {
        return;
    }

    Ticket_WriteStop();
    s_phase = TICKET_PHASE_PAUSED;
}

/**
 * @brief 出票器状态机：执行内部处理（对应符号 Ticket_Resume）。
 * @调用关系 直接调用者：game.c:Game_RestoreOutputsAfterFault。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Ticket_Resume(void)
{
    uint32_t feedback_count;
    uint32_t confirmed_count;

    if (s_phase != TICKET_PHASE_PAUSED)
    {
        return;
    }

    /*
     * 暂停到电机真正停下之间可能还会经过一张票。先收下这次尾脉冲，再直接
     * 恢复运行，不能回到IDLE让Ticket_Start清空计数器，否则长按消票时会少记票。
     */
    feedback_count = Eye_TakeCounter(EYE_COUNTER_TICKET_OUT);
    confirmed_count = (feedback_count > s_remaining_count) ?
                      s_remaining_count : feedback_count;
    s_remaining_count -= confirmed_count;
    s_dispensed_pending += confirmed_count;
    if (s_remaining_count == 0U)
    {
        Ticket_WriteStop();
        s_phase = TICKET_PHASE_IDLE;
        s_last_feedback_ms = 0U;
        return;
    }
    s_last_feedback_ms = Gtim_NowMs();
    s_phase = TICKET_PHASE_RUNNING;
    Ticket_WriteRun();
}

/**
 * @brief 出票器状态机：停止（对应符号 Ticket_Stop）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：primask；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Ticket_Stop(void)
{
    uint32_t primask;

    Ticket_WriteStop();

    primask = __get_PRIMASK();
    __disable_irq();
    s_remaining_count = 0U;
    s_dispensed_pending = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    s_last_feedback_ms = 0U;
    s_phase = TICKET_PHASE_IDLE;
    Eye_ClearCounter(EYE_COUNTER_TICKET_OUT);
}

/**
 * @brief 出票器状态机：执行内部处理（对应符号 Ticket_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Ticket_Test1(void)
{
    (void)Ticket_Add(1U);
}
