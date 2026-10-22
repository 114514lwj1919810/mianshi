/* AUTO-FILE-DOC
 * 文件职责：投币脉冲识别。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "coin.h"

#include "gtim.h"

static volatile uint32_t s_irq_pending = 0U;
static volatile uint32_t s_count = 0U;
static uint8_t s_block_active = 0U;
static uint32_t s_block_start_ms = 0U;
static uint32_t s_block_ms = 0U;
static volatile uint8_t s_irq_low_active = 0U;
static volatile uint32_t s_irq_low_start_ms = 0U;

/**
 * @brief 初始化 PF15 为投币外部中断输入。
 *
 * 调用位置：
 * - App_Init() 在 Interrupt_Init() 之前调用。
 *
 * 逻辑说明：
 * - 打开 GPIOF 时钟。
 * - PF15配置为上拉输入、上升沿和下降沿EXTI。
 * - 下降沿记录低脉冲开始，上升沿确认脉宽并提交一个完整投币。
 * - 本函数只配置 GPIO，NVIC 统一由 Interrupt_Init() 打开。
 */
/**
 * @brief 投币脉冲识别：初始化（对应符号 Coin_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Coin_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOF_CLK_ENABLE();

    gpio.Pin = COIN_PIN;
    gpio.Mode = GPIO_MODE_IT_RISING_FALLING;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(COIN_PORT, &gpio);

    s_irq_pending = 0U;
    s_count = 0U;
    s_block_active = 0U;
    s_block_start_ms = 0U;
    s_block_ms = 0U;
    s_irq_low_active = 0U;
    s_irq_low_start_ms = 0U;
}

/**
 * @brief 投币 EXTI 中断专用标志函数。
 *
 * 调用位置：
 * - EXTI15_10_IRQHandler() 清掉 GPIO_PIN_15 挂起位后调用。
 *
 * 逻辑说明：
 * - 下降沿使用HAL_GetTick()记录低电平开始时间。
 * - 上升沿计算低电平持续时间，只有10~500ms的完整脉冲才把s_irq_pending加1。
 * - 每个完整脉冲独立提交，不依赖前台轮询高电平，因此快速连续投币不会互相覆盖。
 */
/**
 * @brief 投币脉冲识别：执行内部处理（对应符号 Coin_MarkExtiFromIrq）。
 * @调用关系 直接调用者：interrupt.c:EXTI15_10_IRQHandler。
 * @局部状态 主要临时变量：now_ms、pulse_low_ms、input_low；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Coin_MarkExtiFromIrq(void)
{
    uint32_t now_ms;
    uint32_t pulse_low_ms;
    uint8_t input_low;

    now_ms = HAL_GetTick();
    input_low = Coin_IsActive();

    if (input_low != 0U)
    {
        if (s_irq_low_active == 0U)
        {
            s_irq_low_start_ms = now_ms;
            s_irq_low_active = 1U;
        }
        return;
    }

    if (s_irq_low_active == 0U)
    {
        return;
    }

    pulse_low_ms = (uint32_t)(now_ms - s_irq_low_start_ms);
    s_irq_low_active = 0U;
    s_irq_low_start_ms = 0U;

    if ((pulse_low_ms >= COIN_VALID_LOW_MIN_MS) &&
        (pulse_low_ms <= COIN_VALID_LOW_MAX_MS))
    {
        if (s_irq_pending != 0xFFFFFFFFUL)
        {
            s_irq_pending++;
        }
    }
}

/**
 * @brief 在前台处理投币待处理标志。
 *
 * 调用位置：
 * - Game_Task() 每轮调用。
 *
 * 逻辑说明：
 * - 先关中断取走 s_irq_pending 并清零，避免和 EXTI 中断同时读写。
 * - 中断层已经按完整低脉宽确认投币；前台只原子取走有效脉冲总数。
 * - 不再轮询PF15低电平或等待20ms高电平释放，避免快速连续投币时吞掉下一边沿。
 */
/**
 * @brief 投币脉冲识别：周期处理（对应符号 Coin_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：pending、primask；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Coin_Task(void)
{
    uint32_t pending;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    pending = s_irq_pending;
    s_irq_pending = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    if (s_block_active != 0U)
    {
        /*
         * 屏蔽窗口内，所有 PF15 边沿都只消费、不计数。
         * 如果本轮刚好到了屏蔽结束时间，也把已经取出的 pending 丢掉，
         * 防止屏蔽期间残留的最后一个干扰脉冲在结束瞬间变成投币。
         */
        if (Gtim_IsElapsed(s_block_start_ms, s_block_ms) == 0U)
        {
            return;
        }

        s_block_active = 0U;
        s_block_start_ms = 0U;
        s_block_ms = 0U;
        return;
    }

    if (pending != 0U)
    {
        s_count += pending;
    }
}

/**
 * @brief 清除投币模块的旧待处理脉冲和旧累计计数。
 *
 * 调用位置：
 * - Game_Task() 自检结束时调用，用来丢弃自检期间或上电抖动产生的旧投币事件。
 *
 * 逻辑说明：
 * - s_irq_pending 是中断里累加、还没被 Coin_Task() 前台处理的待处理边沿。
 * - s_count 是 Coin_Task() 通过滤波后得到、还没被 Game_Task() 消费的有效投币数。
 * - 两个变量都要清零，才能保证自检结束后的第一个币从干净状态开始计数。
 * - 同时重置中断层正在测量的低脉冲状态。
 */
/**
 * @brief 投币脉冲识别：清除（对应符号 Coin_Clear）。
 * @调用关系 直接调用者：coin.c:Coin_Test1。
 * @局部状态 主要临时变量：primask；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Coin_Clear(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    s_irq_pending = 0U;
    s_count = 0U;
    s_block_active = 0U;
    s_block_start_ms = 0U;
    s_block_ms = 0U;
    s_irq_low_active = 0U;
    s_irq_low_start_ms = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}
/**
 * @brief 临时屏蔽投币计数，并清掉已经积累的投币脉冲。
 *
 * 调用位置：
 * - Game_Task() 在出票按键确认、重复光眼结束、吐币电机运行期间调用。
 *
 * 逻辑说明：
 * - s_irq_pending 是中断里已经记录、但 Coin_Task() 还没处理的 PF15 边沿。
 * - s_count 是 Coin_Task() 已经确认、但 Game_Task() 还没取走的有效投币数。
 * - 出票或结束动作期间如果 PF15 被干扰，这两个变量都不能留给后续 READY/IDLE 状态消费。
 * - block_ms 非 0 时开启持续屏蔽；屏蔽期间 Coin_Task() 会丢掉所有投币边沿。
 * - 同时重置中断层正在测量的低脉冲状态。
 */
/**
 * @brief 投币脉冲识别：执行内部处理（对应符号 Coin_Block）。
 * @调用关系 直接调用者：coin.c:Coin_Test2。
 * @局部状态 主要临时变量：primask；均只在本次调用中有效。
 * @param block_ms 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Coin_Block(uint32_t block_ms)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();
    s_irq_pending = 0U;
    s_count = 0U;
    if (block_ms != 0U)
    {
        s_block_active = 1U;
        s_block_start_ms = Gtim_NowMs();
        s_block_ms = block_ms;
    }
    else
    {
        s_block_active = 0U;
        s_block_start_ms = 0U;
        s_block_ms = 0U;
    }
    s_irq_low_active = 0U;
    s_irq_low_start_ms = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 读取累计投币计数，不清零。
 *
 * 调用位置：
 * - 后续 account/setting 调试页面或账目统计可以调用。
 *
 * @return 当前累计投币脉冲数。
 */
/**
 * @brief 投币脉冲识别：读取（对应符号 Coin_GetCount）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Coin_GetCount(void)
{
    return s_count;
}

/**
 * @brief 取走累计投币计数并清零。
 *
 * 调用位置：
 * - Game_Task() 每轮在状态分支前统一调用。
 *
 * 逻辑说明：
 * - 读取和清零 s_count 时短暂关中断，避免计数被并发修改。
 *
 * @return 清零前的投币脉冲数。
 */
/**
 * @brief 投币脉冲识别：读取并清除一次性事件（对应符号 Coin_TakeCount）。
 * @调用关系 直接调用者：coin.c:Coin_Test3、game.c:Game_Task。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t Coin_TakeCount(void)
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
 * @brief 投币脉冲识别：执行内部处理（对应符号 Coin_IsActive）。
 * @调用关系 直接调用者：coin.c:Coin_MarkExtiFromIrq、fault.c:Fault_IsCodeClear、fault.c:Fault_CheckCoinJam。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Coin_IsActive(void)
{
    return (HAL_GPIO_ReadPin(COIN_PORT, COIN_PIN) == GPIO_PIN_RESET) ? 1U : 0U;
}

/**
 * @brief 投币模块测试 1：清除旧投币计数。
 *
 * 作用：
 * - 调用 Coin_Clear()。
 * - 用于上电或调试前清掉 PF15 上残留的投币边沿。
 */
/**
 * @brief 投币脉冲识别：执行内部处理（对应符号 Coin_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Coin_Test1(void)
{
    Coin_Clear();
}

/**
 * @brief 投币模块测试 2：屏蔽投币 1 秒。
 *
 * 作用：
 * - 调用 Coin_Block(1000)。
 * - 用于测试“出票/电磁铁动作期间不计假币”的屏蔽逻辑。
 */
/**
 * @brief 投币脉冲识别：执行内部处理（对应符号 Coin_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Coin_Test2(void)
{
    Coin_Block(1000U);
}

/**
 * @brief 投币模块测试 3：取走当前投币计数。
 *
 * 作用：
 * - 调用 Coin_TakeCount()。
 * - 返回值这里故意丢弃，只作为“如何消费投币数”的调用例子。
 */
/**
 * @brief 投币脉冲识别：执行内部处理（对应符号 Coin_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Coin_Test3(void)
{
    (void)Coin_TakeCount();
}
