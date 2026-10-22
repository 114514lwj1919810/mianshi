/* AUTO-FILE-DOC
 * 文件职责：机械计数器脉冲。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "meter.h"

#include "gtim.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} Meter_Pin;

typedef enum
{
    METER_STATE_IDLE = 0U,
    METER_STATE_LOW,
    METER_STATE_GAP
} Meter_State;

typedef struct
{
    uint32_t pending_count;
    uint32_t state_start_ms;
    Meter_State state;
} Meter_Runtime;

static const Meter_Pin s_meter_pin[METER_COUNT] =
{
    {METER_COIN_IN_PORT, METER_COIN_IN_PIN},
    {METER_TICKET_OUT_PORT, METER_TICKET_OUT_PIN},
    {METER_PAYOUT_OUT_PORT, METER_PAYOUT_OUT_PIN}
};

static Meter_Runtime s_meter[METER_COUNT];

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 机械计数器脉冲：写入（对应符号 Meter_WriteIdle）。
 * @调用关系 直接调用者：meter.c:Meter_Task、meter.c:Meter_ClearPending。
 * @param meter 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Meter_WriteIdle(Meter_Id meter)
{
    HAL_GPIO_WritePin(s_meter_pin[meter].port,
                      s_meter_pin[meter].pin,
                      METER_IDLE_LEVEL);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 机械计数器脉冲：写入（对应符号 Meter_WriteActive）。
 * @调用关系 直接调用者：meter.c:Meter_StartNextPulse。
 * @param meter 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Meter_WriteActive(Meter_Id meter)
{
    HAL_GPIO_WritePin(s_meter_pin[meter].port,
                      s_meter_pin[meter].pin,
                      METER_ACTIVE_LEVEL);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 机械计数器脉冲：启动（对应符号 Meter_StartNextPulse）。
 * @调用关系 直接调用者：meter.c:Meter_Task。
 * @param meter 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Meter_StartNextPulse(Meter_Id meter)
{
    if (s_meter[meter].pending_count == 0U)
    {
        return;
    }

    s_meter[meter].pending_count--;
    s_meter[meter].state = METER_STATE_LOW;
    s_meter[meter].state_start_ms = Gtim_NowMs();
    Meter_WriteActive(meter);
}

/**
 * @brief 机械计数器脉冲：初始化（对应符号 Meter_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：gpio、i；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Meter_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t i;

    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(GPIOB,
                      METER_COIN_IN_PIN | METER_TICKET_OUT_PIN | METER_PAYOUT_OUT_PIN,
                      METER_IDLE_LEVEL);

    gpio.Pin = METER_COIN_IN_PIN | METER_TICKET_OUT_PIN | METER_PAYOUT_OUT_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &gpio);

    HAL_GPIO_WritePin(GPIOB,
                      METER_COIN_IN_PIN | METER_TICKET_OUT_PIN | METER_PAYOUT_OUT_PIN,
                      METER_IDLE_LEVEL);

    for (i = 0U; i < (uint8_t)METER_COUNT; i++)
    {
        s_meter[i].pending_count = 0U;
        s_meter[i].state_start_ms = 0U;
        s_meter[i].state = METER_STATE_IDLE;
    }
}

/**
 * @brief 机械计数器脉冲：周期处理（对应符号 Meter_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：i、meter；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Meter_Task(void)
{
    uint8_t i;
    Meter_Id meter;

    for (i = 0U; i < (uint8_t)METER_COUNT; i++)
    {
        meter = (Meter_Id)i;

        switch (s_meter[meter].state)
        {
            case METER_STATE_IDLE:
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Meter_WriteIdle(meter);
                Meter_StartNextPulse(meter);
                break;

            case METER_STATE_LOW:
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                if (Gtim_IsElapsed(s_meter[meter].state_start_ms, METER_PULSE_LOW_MS) != 0U)
                {
                    Meter_WriteIdle(meter);
                    s_meter[meter].state = METER_STATE_GAP;
                    s_meter[meter].state_start_ms = Gtim_NowMs();
                }
                break;

            case METER_STATE_GAP:
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Meter_WriteIdle(meter);
                if (Gtim_IsElapsed(s_meter[meter].state_start_ms, METER_PULSE_GAP_MS) != 0U)
                {
                    s_meter[meter].state = METER_STATE_IDLE;
                }
                break;

            default:
                s_meter[meter].state = METER_STATE_IDLE;
                Meter_WriteIdle(meter);
                break;
        }
    }
}

/**
 * @brief 机械计数器脉冲：执行内部处理（对应符号 Meter_AddPulse）。
 * @调用关系 直接调用者：meter.c:Meter_Test1、meter.c:Meter_Test2、meter.c:Meter_Test3、game.c:Game_ProcessCoinCount、game.c:Game_Task。
 * @局部状态 主要临时变量：next_count、primask；均只在本次调用中有效。
 * @param meter 按值传入；函数内部修改不会回写调用方变量。
 * @param pulse_count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Meter_AddPulse(Meter_Id meter, uint32_t pulse_count)
{
    uint32_t next_count;
    uint32_t primask;

    if ((meter >= METER_COUNT) || (pulse_count == 0U))
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    next_count = s_meter[meter].pending_count + pulse_count;
    if (next_count > METER_PENDING_MAX)
    {
        next_count = METER_PENDING_MAX;
    }
    s_meter[meter].pending_count = next_count;

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 机械计数器脉冲：清除（对应符号 Meter_ClearPending）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：primask；均只在本次调用中有效。
 * @param meter 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Meter_ClearPending(Meter_Id meter)
{
    uint32_t primask;

    if (meter >= METER_COUNT)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    s_meter[meter].pending_count = 0U;
    s_meter[meter].state = METER_STATE_IDLE;
    s_meter[meter].state_start_ms = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    Meter_WriteIdle(meter);
}

/**
 * @brief 机械计数器脉冲：执行内部处理（对应符号 Meter_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Meter_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Meter_AddPulse(METER_COIN_IN, 1U);
}

/**
 * @brief 机械计数器脉冲：执行内部处理（对应符号 Meter_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Meter_Test2(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Meter_AddPulse(METER_TICKET_OUT, 1U);
}

/**
 * @brief 机械计数器脉冲：执行内部处理（对应符号 Meter_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Meter_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Meter_AddPulse(METER_PAYOUT_OUT, 1U);
}
