/* AUTO-FILE-DOC
 * 文件职责：系统毫秒时基。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "gtim.h"

static TIM_HandleTypeDef s_tim6;
static volatile uint32_t s_irq_ticks = 0U;
static volatile uint32_t s_ms = 0U;
static volatile uint8_t s_flag_2ms = 0U;
static volatile uint8_t s_flag_10ms = 0U;
static volatile uint8_t s_flag_20ms = 0U;
static uint8_t s_div_2ms = 0U;
static uint8_t s_div_10ms = 0U;
static volatile uint8_t s_irq_div_20ms = 0U;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：初始化（对应符号 Gtim_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Gtim_Init(void)
{
    GTIM_TICK_CLK_ENABLE();

    s_tim6.Instance = GTIM_TICK_TIM;
    s_tim6.Init.Prescaler = 8399U;
    s_tim6.Init.CounterMode = TIM_COUNTERMODE_UP;
    s_tim6.Init.Period = 9U;
    s_tim6.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    s_tim6.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    HAL_TIM_Base_Init(&s_tim6);

    s_irq_ticks = 0U;
    s_ms = 0U;
    s_flag_2ms = 0U;
    s_flag_10ms = 0U;
    s_flag_20ms = 0U;
    s_div_2ms = 0U;
    s_div_10ms = 0U;
    s_irq_div_20ms = 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：启动（对应符号 Gtim_Start）。
 * @调用关系 直接调用者：app_main.c:App_Init、gtim.c:Gtim_Test3。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Gtim_Start(void)
{
    HAL_TIM_Base_Start_IT(&s_tim6);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：停止（对应符号 Gtim_Stop）。
 * @调用关系 直接调用者：gtim.c:Gtim_Test3。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Gtim_Stop(void)
{
    HAL_TIM_Base_Stop_IT(&s_tim6);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：周期处理（对应符号 Gtim_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：ticks、primask；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Gtim_Task(void)
{
    uint32_t ticks;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    ticks = s_irq_ticks;
    s_irq_ticks = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    while (ticks != 0U)
    {
        ticks--;
        s_ms++;

        s_div_2ms++;
        if (s_div_2ms >= 2U)
        {
            s_div_2ms = 0U;
            s_flag_2ms = 1U;
        }

        s_div_10ms++;
        if (s_div_10ms >= 10U)
        {
            s_div_10ms = 0U;
            s_flag_10ms = 1U;
        }
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：执行内部处理（对应符号 Gtim_MarkTickFromIrq）。
 * @调用关系 直接调用者：interrupt.c:TIM6_DAC_IRQHandler。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Gtim_MarkTickFromIrq(void)
{
    if ((__HAL_TIM_GET_FLAG(&s_tim6, TIM_FLAG_UPDATE) != RESET) &&
        (__HAL_TIM_GET_IT_SOURCE(&s_tim6, TIM_IT_UPDATE) != RESET))
    {
        __HAL_TIM_CLEAR_IT(&s_tim6, TIM_IT_UPDATE);
        s_irq_ticks++;

        s_irq_div_20ms++;
        if (s_irq_div_20ms >= GTIM_20MS_PERIOD_MS)
        {
            s_irq_div_20ms = 0U;
            s_flag_20ms = 1U;
        }
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：执行内部处理（对应符号 Gtim_NowMs）。
 * @调用关系 直接调用者：self_test.c:SelfTest_Init、self_test.c:SelfTest_Task、award_lamp.c:AwardLamp_StartBlink、award_lamp.c:AwardLamp_StartBlinkContinuous、award_lamp.c:AwardLamp_Task、coin.c:Coin_Block、eye.c:Eye_GameMotorPulseTask、eye.c:Eye_ExtiTask。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Gtim_NowMs(void)
{
    return s_ms;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：执行内部处理（对应符号 Gtim_IsElapsed）。
 * @调用关系 直接调用者：self_test.c:SelfTest_Task、coin.c:Coin_Task、gtim.c:Gtim_Test2、meter.c:Meter_Task、motor.c:Motor_Task、solenoid.c:Solenoid_Task、ticket.c:Ticket_Task、wiper_motor.c:WiperMotor_Task。
 * @param start_ms 按值传入；函数内部修改不会回写调用方变量。
 * @param period_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Gtim_IsElapsed(uint32_t start_ms, uint32_t period_ms)
{
    return ((uint32_t)(Gtim_NowMs() - start_ms) >= period_ms) ? 1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：读取并清除一次性事件（对应符号 Gtim_Take2msFlag）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Gtim_Take2msFlag(void)
{
    uint8_t result;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    result = s_flag_2ms;
    s_flag_2ms = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return result;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：读取并清除一次性事件（对应符号 Gtim_Take10msFlag）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Gtim_Take10msFlag(void)
{
    uint8_t result;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    result = s_flag_10ms;
    s_flag_10ms = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return result;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 系统毫秒时基：读取并清除一次性事件（对应符号 Gtim_Take20msFlag）。
 * @调用关系 直接调用者：segment.c:Segment_Task、segment.c:Segment_Task。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Gtim_Take20msFlag(void)
{
    uint8_t result;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    result = s_flag_20ms;
    s_flag_20ms = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return result;
}

/**
 * @brief 系统毫秒时基：执行内部处理（对应符号 Gtim_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Gtim_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Gtim_NowMs();
}

/**
 * @brief 系统毫秒时基：执行内部处理（对应符号 Gtim_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：now_ms；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Gtim_Test2(void)
{
    uint32_t now_ms = Gtim_NowMs();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Gtim_IsElapsed(now_ms, 100U);
}

/**
 * @brief 系统毫秒时基：执行内部处理（对应符号 Gtim_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Gtim_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Gtim_Stop();
    Gtim_Start();
}
