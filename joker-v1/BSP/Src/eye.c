/* AUTO-FILE-DOC
 * 文件职责：落球光眼扫描。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "eye.h"

#include "gtim.h"
#include "motor.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} Eye_Pin;

typedef struct
{
    uint8_t sample_active;
    uint8_t stable_active;
    uint8_t stable_ticks;
    uint8_t armed;
} Eye_ChannelRuntime;

typedef struct
{
    uint32_t count;
    uint32_t last_irq_ms;
} Eye_CounterRuntime;

static const Eye_Pin s_channel_pin[EYE_CHANNEL_COUNT] =
{
    {EYE_CH1_PORT, EYE_CH1_PIN},
    {EYE_CH2_PORT, EYE_CH2_PIN},
    {EYE_CH3_PORT, EYE_CH3_PIN},
    {EYE_CH4_PORT, EYE_CH4_PIN},
    {EYE_CH5_PORT, EYE_CH5_PIN},
    {EYE_CH6_PORT, EYE_CH6_PIN},
    {EYE_CH7_PORT, EYE_CH7_PIN},
    {EYE_CH8_PORT, EYE_CH8_PIN},
    {EYE_CH9_PORT, EYE_CH9_PIN}
};

static const Eye_Pin s_counter_pin[EYE_COUNTER_COUNT] =
{
    {EYE_GAME_MOTOR_PORT, EYE_GAME_MOTOR_PIN},
    {EYE_SPIT_COIN_PORT, EYE_SPIT_COIN_PIN},
    {EYE_TICKET_OUT_PORT, EYE_TICKET_OUT_PIN},
    {EYE_SOLENOID_CHECK_PORT, EYE_SOLENOID_CHECK_PIN}
};

static Eye_ChannelRuntime s_channel[EYE_CHANNEL_COUNT];
/* PB0 在 EXTI0 中断内写计数，前台任务读取，必须禁止编译器缓存。 */
static volatile Eye_CounterRuntime s_counter[EYE_COUNTER_COUNT];
static volatile uint32_t s_counter_irq_pending[EYE_COUNTER_COUNT];
static volatile uint16_t s_channel_trigger_mask = 0U;
static volatile uint8_t s_counter_event_mask = 0U;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：初始化（对应符号 Eye_InitInput）。
 * @调用关系 直接调用者：eye.c:Eye_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param pins 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Eye_InitInput(GPIO_TypeDef *port, uint16_t pins)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &gpio);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：初始化（对应符号 Eye_InitExti）。
 * @调用关系 直接调用者：eye.c:Eye_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param pins 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Eye_InitExti(GPIO_TypeDef *port, uint16_t pins)
{
    GPIO_InitTypeDef gpio = {0};

    gpio.Pin = pins;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(port, &gpio);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：读取（对应符号 Eye_ReadChannel）。
 * @调用关系 直接调用者：eye.c:Eye_Init、eye.c:Eye_ScanChannels2ms。
 * @param channel 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Eye_ReadChannel(Eye_Channel channel)
{
    if (channel >= EYE_CHANNEL_COUNT)
    {
        return 0U;
    }

    return (HAL_GPIO_ReadPin(s_channel_pin[channel].port, s_channel_pin[channel].pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：初始化（对应符号 Eye_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：i、active；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eye_Init(void)
{
    uint8_t i;
    uint8_t active;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();

    Eye_InitInput(GPIOF, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4);
    Eye_InitInput(GPIOE, GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5 | GPIO_PIN_6);

    Eye_InitExti(GPIOB, GPIO_PIN_0);
    Eye_InitExti(GPIOF, GPIO_PIN_5 | GPIO_PIN_9 | GPIO_PIN_10);

    for (i = 0U; i < (uint8_t)EYE_CHANNEL_COUNT; i++)
    {
        active = Eye_ReadChannel((Eye_Channel)i);
        s_channel[i].sample_active = active;
        s_channel[i].stable_active = active;
        s_channel[i].stable_ticks = EYE_CHANNEL_STABLE_TICKS;
        s_channel[i].armed = (active == 0U) ? 1U : 0U;
    }

    for (i = 0U; i < (uint8_t)EYE_COUNTER_COUNT; i++)
    {
        s_counter[i].count = 0U;
        s_counter[i].last_irq_ms = 0U;
        s_counter_irq_pending[i] = 0U;
    }

    s_channel_trigger_mask = 0U;
    s_counter_event_mask = 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：执行内部处理（对应符号 Eye_ScanChannels2ms）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：i、raw_active；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eye_ScanChannels2ms(void)
{
    uint8_t i;
    uint8_t raw_active;

    for (i = 0U; i < (uint8_t)EYE_CHANNEL_COUNT; i++)
    {
        raw_active = Eye_ReadChannel((Eye_Channel)i);

        if (raw_active != s_channel[i].sample_active)
        {
            s_channel[i].sample_active = raw_active;
            s_channel[i].stable_ticks = 0U;
            continue;
        }

        if (s_channel[i].stable_ticks < EYE_CHANNEL_STABLE_TICKS)
        {
            s_channel[i].stable_ticks++;
        }

        if ((s_channel[i].stable_ticks >= EYE_CHANNEL_STABLE_TICKS) &&
            (s_channel[i].stable_active != raw_active))
        {
            s_channel[i].stable_active = raw_active;

            if (raw_active != 0U)
            {
                if (s_channel[i].armed != 0U)
                {
                    s_channel_trigger_mask |= (uint16_t)(1U << i);
                    s_channel[i].armed = 0U;
                }
            }
            else
            {
                s_channel[i].armed = 1U;
            }
        }
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：执行内部处理（对应符号 Eye_MarkCounterFromIrq）。
 * @调用关系 直接调用者：interrupt.c:EXTI0_IRQHandler、interrupt.c:EXTI9_5_IRQHandler、interrupt.c:EXTI15_10_IRQHandler。
 * @param counter 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eye_MarkCounterFromIrq(Eye_Counter counter)
{
    if (counter >= EYE_COUNTER_COUNT)
    {
        return;
    }

    if (counter == EYE_COUNTER_GAME_MOTOR)
    {
        /* 只认电机运转期间 PB0 的第一个下降沿，不等待4ms或上升沿。
         * 在中断内立即切断输出，后续抖动因电机已停而不重复计数。 */
        if ((Motor_GetDirection(MOTOR_GAME_COIN) == MOTOR_DIR_FORWARD) &&
            (s_counter[counter].count == 0U) &&
            (HAL_GPIO_ReadPin(s_counter_pin[counter].port,
                              s_counter_pin[counter].pin) == GPIO_PIN_RESET))
        {
            Motor_Stop(MOTOR_GAME_COIN);
            s_counter[counter].last_irq_ms = HAL_GetTick();
            s_counter[counter].count = 1U;
            s_counter_event_mask |= (uint8_t)(1U << (uint8_t)counter);
        }
        return;
    }

    s_counter_irq_pending[counter]++;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：周期处理（对应符号 Eye_ExtiTask）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：i、pending、now_ms、primask；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eye_ExtiTask(void)
{
    uint8_t i;
    uint32_t pending;
    uint32_t now_ms;
    uint32_t primask;

    for (i = 0U; i < (uint8_t)EYE_COUNTER_COUNT; i++)
    {
        if (i == (uint8_t)EYE_COUNTER_GAME_MOTOR)
        {
            continue;
        }

        primask = __get_PRIMASK();
        __disable_irq();
        pending = s_counter_irq_pending[i];
        s_counter_irq_pending[i] = 0U;
        if (primask == 0U)
        {
            __enable_irq();
        }

        while (pending != 0U)
        {
            pending--;
            now_ms = Gtim_NowMs();

            if ((uint32_t)(now_ms - s_counter[i].last_irq_ms) >= EYE_EXTI_FILTER_MS)
            {
                s_counter[i].last_irq_ms = now_ms;
                s_counter[i].count++;
                s_counter_event_mask |= (uint8_t)(1U << i);
            }
        }
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：读取（对应符号 Eye_GetChannelActiveMask）。
 * @调用关系 直接调用者：eye.c:Eye_Test3、fault.c:Fault_IsCodeClear、fault.c:Fault_CheckChannelJam、game.c:Game_Task。
 * @局部状态 主要临时变量：i、mask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Eye_GetChannelActiveMask(void)
{
    uint8_t i;
    uint16_t mask = 0U;

    for (i = 0U; i < (uint8_t)EYE_CHANNEL_COUNT; i++)
    {
        if (s_channel[i].stable_active != 0U)
        {
            mask |= (uint16_t)(1U << i);
        }
    }

    return mask;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：读取并清除一次性事件（对应符号 Eye_TakeChannelTriggerMask）。
 * @调用关系 直接调用者：eye.c:Eye_Test1、game.c:Game_DiscardMenuPlayInputs、game.c:Game_Task。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Eye_TakeChannelTriggerMask(void)
{
    uint16_t result;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    result = s_channel_trigger_mask;
    s_channel_trigger_mask = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return result;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：读取（对应符号 Eye_GetCounter）。
 * @调用关系 直接调用者：fault.c:Fault_CheckGameMotorNoCount。
 * @param counter 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Eye_GetCounter(Eye_Counter counter)
{
    if (counter >= EYE_COUNTER_COUNT)
    {
        return 0U;
    }

    return s_counter[counter].count;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：读取并清除一次性事件（对应符号 Eye_TakeCounter）。
 * @调用关系 直接调用者：ticket.c:Ticket_Task、game.c:Game_DiscardMenuPlayInputs、game.c:Game_Task、payout.c:Payout_Task。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @param counter 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Eye_TakeCounter(Eye_Counter counter)
{
    uint32_t result;
    uint32_t primask;

    if (counter >= EYE_COUNTER_COUNT)
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    result = s_counter[counter].count;
    s_counter[counter].count = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return result;
}

/**
 * @brief 落球光眼扫描：执行内部处理（对应符号 Eye_IsCounterActive）。
 * @调用关系 直接调用者：fault.c:Fault_IsCodeClear、fault.c:Fault_CheckCounterJam。
 * @param counter 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Eye_IsCounterActive(Eye_Counter counter)
{
    if (counter >= EYE_COUNTER_COUNT)
    {
        return 0U;
    }

    return (HAL_GPIO_ReadPin(s_counter_pin[counter].port, s_counter_pin[counter].pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：清除（对应符号 Eye_ClearCounter）。
 * @调用关系 直接调用者：eye.c:Eye_Test2、ticket.c:Ticket_Start、ticket.c:Ticket_SkipRemaining、ticket.c:Ticket_Stop、game.c:Game_Task、payout.c:Payout_AddPrize。
 * @局部状态 主要临时变量：primask；均只在本次调用中有效。
 * @param counter 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eye_ClearCounter(Eye_Counter counter)
{
    uint32_t primask;

    if (counter >= EYE_COUNTER_COUNT)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    s_counter[counter].count = 0U;
    s_counter_irq_pending[counter] = 0U;
    s_counter_event_mask &= (uint8_t)~(uint8_t)(1U << (uint8_t)counter);
    __HAL_GPIO_EXTI_CLEAR_IT(s_counter_pin[counter].pin);
    if (counter == EYE_COUNTER_GAME_MOTOR)
    {
        HAL_NVIC_ClearPendingIRQ(EXTI0_IRQn);
    }
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 落球光眼扫描：读取并清除一次性事件（对应符号 Eye_TakeCounterEventMask）。
 * @调用关系 直接调用者：game.c:Game_DiscardMenuPlayInputs、game.c:Game_Task。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Eye_TakeCounterEventMask(void)
{
    uint8_t result;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    result = s_counter_event_mask;
    s_counter_event_mask = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return result;
}

/**
 * @brief 落球光眼扫描：执行内部处理（对应符号 Eye_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eye_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Eye_TakeChannelTriggerMask();
}

/**
 * @brief 落球光眼扫描：执行内部处理（对应符号 Eye_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eye_Test2(void)
{
    uint8_t i;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    for (i = 0U; i < (uint8_t)EYE_COUNTER_COUNT; i++)
    {
        Eye_ClearCounter((Eye_Counter)i);
    }
}

/**
 * @brief 落球光眼扫描：执行内部处理（对应符号 Eye_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eye_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Eye_GetChannelActiveMask();
}
