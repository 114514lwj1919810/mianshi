/* AUTO-FILE-DOC
 * 文件职责：送灯输出。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "award_lamp.h"

#include "gtim.h"

static uint8_t s_award_lamp_blinking;
static uint8_t s_award_lamp_continuous;
static uint32_t s_award_lamp_start_ms;
static uint32_t s_award_lamp_duration_ms;

/**
 * @brief 送灯输出：写入（对应符号 AwardLamp_Write）。
 * @调用关系 直接调用者：award_lamp.c:AwardLamp_Init、award_lamp.c:AwardLamp_StartBlink、award_lamp.c:AwardLamp_StartBlinkContinuous、award_lamp.c:AwardLamp_Stop、award_lamp.c:AwardLamp_Task。
 * @param level 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void AwardLamp_Write(GPIO_PinState level)
{
    HAL_GPIO_WritePin(AWARD_LAMP_PORT, AWARD_LAMP_PIN, level);
}

/**
 * @brief 送灯输出：初始化（对应符号 AwardLamp_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void AwardLamp_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* 先写关闭电平再切换为输出，避免上电初始化瞬间误闪。 */
    AwardLamp_Write(AWARD_LAMP_OFF_LEVEL);
    gpio.Pin = AWARD_LAMP_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(AWARD_LAMP_PORT, &gpio);

    s_award_lamp_blinking = 0U;
    s_award_lamp_continuous = 0U;
    s_award_lamp_start_ms = 0U;
    s_award_lamp_duration_ms = 0U;
}

/**
 * @brief 送灯输出：启动（对应符号 AwardLamp_StartBlink）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param duration_ms 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void AwardLamp_StartBlink(uint32_t duration_ms)
{
    if (duration_ms == 0U)
    {
        AwardLamp_Stop();
        return;
    }

    s_award_lamp_start_ms = Gtim_NowMs();
    s_award_lamp_duration_ms = duration_ms;
    s_award_lamp_blinking = 1U;
    s_award_lamp_continuous = 0U;
    AwardLamp_Write(AWARD_LAMP_ON_LEVEL);
}

/**
 * @brief 送灯输出：启动（对应符号 AwardLamp_StartBlinkContinuous）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void AwardLamp_StartBlinkContinuous(void)
{
    s_award_lamp_start_ms = Gtim_NowMs();
    s_award_lamp_duration_ms = 0U;
    s_award_lamp_blinking = 1U;
    s_award_lamp_continuous = 1U;
    AwardLamp_Write(AWARD_LAMP_ON_LEVEL);
}

/**
 * @brief 送灯输出：停止（对应符号 AwardLamp_Stop）。
 * @调用关系 直接调用者：award_lamp.c:AwardLamp_StartBlink、award_lamp.c:AwardLamp_Task、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void AwardLamp_Stop(void)
{
    s_award_lamp_blinking = 0U;
    s_award_lamp_continuous = 0U;
    s_award_lamp_start_ms = 0U;
    s_award_lamp_duration_ms = 0U;
    AwardLamp_Write(AWARD_LAMP_OFF_LEVEL);
}

/**
 * @brief 送灯输出：周期处理（对应符号 AwardLamp_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：elapsed_ms；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void AwardLamp_Task(void)
{
    uint32_t elapsed_ms;

    if (s_award_lamp_blinking == 0U)
    {
        return;
    }

    elapsed_ms = Gtim_NowMs() - s_award_lamp_start_ms;
    if ((s_award_lamp_continuous == 0U) &&
        (elapsed_ms >= s_award_lamp_duration_ms))
    {
        AwardLamp_Stop();
        return;
    }

    if (((elapsed_ms / AWARD_LAMP_BLINK_HALF_PERIOD_MS) & 1U) == 0U)
    {
        AwardLamp_Write(AWARD_LAMP_ON_LEVEL);
    }
    else
    {
        AwardLamp_Write(AWARD_LAMP_OFF_LEVEL);
    }
}

/**
 * @brief 送灯输出：执行内部处理（对应符号 AwardLamp_IsBlinking）。
 * @调用关系 直接调用者：standby.c:Standby_CanStart。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t AwardLamp_IsBlinking(void)
{
    return s_award_lamp_blinking;
}
