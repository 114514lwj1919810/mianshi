/* AUTO-FILE-DOC
 * 文件职责：震动检测。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "shake.h"

#include "gtim.h"

static volatile uint32_t s_irq_pending = 0U;
static volatile uint32_t s_count = 0U;
static uint32_t s_last_irq_ms = 0U;

/**
 * @brief 震动检测：执行内部处理（对应符号 Shake_IsActive）。
 * @调用关系 直接调用者：shake.c:Shake_Test2、fault.c:Fault_CheckShake。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Shake_IsActive(void)
{
    return (HAL_GPIO_ReadPin(SHAKE_PORT, SHAKE_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
}

/**
 * @brief 震动检测：初始化（对应符号 Shake_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Shake_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOF_CLK_ENABLE();

    gpio.Pin = SHAKE_PIN;
    gpio.Mode = GPIO_MODE_IT_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SHAKE_PORT, &gpio);

    Shake_Clear();
}

/**
 * @brief 震动检测：执行内部处理（对应符号 Shake_MarkExtiFromIrq）。
 * @调用关系 直接调用者：interrupt.c:EXTI9_5_IRQHandler。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Shake_MarkExtiFromIrq(void)
{
    s_irq_pending++;
}

/**
 * @brief 震动检测：周期处理（对应符号 Shake_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：pending、now_ms、primask；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Shake_Task(void)
{
    uint32_t pending;
    uint32_t now_ms;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    pending = s_irq_pending;
    s_irq_pending = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    now_ms = Gtim_NowMs();
    while (pending != 0U)
    {
        pending--;
        if ((uint32_t)(now_ms - s_last_irq_ms) >= SHAKE_EXTI_FILTER_MS)
        {
            s_last_irq_ms = now_ms;
            s_count++;
        }
    }
}

/**
 * @brief 震动检测：读取（对应符号 Shake_GetCount）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Shake_GetCount(void)
{
    return s_count;
}

/**
 * @brief 震动检测：读取并清除一次性事件（对应符号 Shake_TakeCount）。
 * @调用关系 直接调用者：shake.c:Shake_Test3、fault.c:Fault_CheckShake。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Shake_TakeCount(void)
{
    uint32_t result;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    result = s_count;
    s_count = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return result;
}

/**
 * @brief 震动检测：清除（对应符号 Shake_Clear）。
 * @调用关系 直接调用者：shake.c:Shake_Init、shake.c:Shake_Test1、fault.c:Fault_Clear、game.c:Game_Task。
 * @局部状态 主要临时变量：primask；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Shake_Clear(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    s_irq_pending = 0U;
    s_count = 0U;
    s_last_irq_ms = Gtim_NowMs();
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 震动检测：执行内部处理（对应符号 Shake_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Shake_Test1(void)
{
    Shake_Clear();
}

/**
 * @brief 震动检测：执行内部处理（对应符号 Shake_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Shake_Test2(void)
{
    (void)Shake_IsActive();
}

/**
 * @brief 震动检测：执行内部处理（对应符号 Shake_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Shake_Test3(void)
{
    (void)Shake_TakeCount();
}
