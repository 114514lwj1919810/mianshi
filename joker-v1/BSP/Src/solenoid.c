/* AUTO-FILE-DOC
 * 文件职责：电磁铁控制。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "solenoid.h"

#include "gtim.h"

static uint8_t s_busy = 0U;
static uint32_t s_start_ms = 0U;
static uint32_t s_pulse_ms = 0U;

/*
 * A raw, indefinite high level is unsafe for the coil.  Even diagnostic
 * callers must go through the same bounded pulse state used by gameplay.
 */
#define SOLENOID_MANUAL_HIGH_LIMIT_MS  500U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：初始化（对应符号 Solenoid_BootSafeInit）。
 * @调用关系 直接调用者：solenoid.c:Solenoid_EarlySafeInit、main.c:main。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_BootSafeInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();
    HAL_GPIO_WritePin(SOLENOID_PORT, SOLENOID_PIN, SOLENOID_RELEASE_LEVEL);
    gpio.Pin = SOLENOID_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SOLENOID_PORT, &gpio);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：初始化（对应符号 Solenoid_EarlySafeInit）。
 * @调用关系 直接调用者：gd32f407.c:GD32F407_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_EarlySafeInit(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Solenoid_BootSafeInit();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：初始化（对应符号 Solenoid_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOE_CLK_ENABLE();

    HAL_GPIO_WritePin(SOLENOID_PORT, SOLENOID_PIN, SOLENOID_RELEASE_LEVEL);

    gpio.Pin = SOLENOID_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SOLENOID_PORT, &gpio);

    s_busy = 0U;
    s_start_ms = 0U;
    s_pulse_ms = 0U;
    Solenoid_Release();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：执行内部处理（对应符号 Solenoid_Engage）。
 * @调用关系 直接调用者：solenoid.c:Solenoid_Pulse。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_Engage(void)
{
    HAL_GPIO_WritePin(SOLENOID_PORT, SOLENOID_PIN, SOLENOID_ENGAGE_LEVEL);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：执行内部处理（对应符号 Solenoid_Release）。
 * @调用关系 直接调用者：solenoid.c:Solenoid_Init、solenoid.c:Solenoid_Pulse、solenoid.c:Solenoid_Task、solenoid.c:Solenoid_Test1、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_Release(void)
{
    HAL_GPIO_WritePin(SOLENOID_PORT, SOLENOID_PIN, SOLENOID_RELEASE_LEVEL);
    s_busy = 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：设置（对应符号 Solenoid_SetHigh）。
 * @调用关系 直接调用者：self_test.c:SelfTest_Task、self_test.c:SelfTest_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_SetHigh(void)
{
    Solenoid_Pulse(SOLENOID_MANUAL_HIGH_LIMIT_MS);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：设置（对应符号 Solenoid_SetLow）。
 * @调用关系 直接调用者：self_test.c:SelfTest_Task、solenoid.c:Solenoid_Test3、self_test.c:SelfTest_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_SetLow(void)
{
    Solenoid_Release();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：执行内部处理（对应符号 Solenoid_Pulse）。
 * @调用关系 直接调用者：solenoid.c:Solenoid_Test2、game.c:Game_FinishLineRound、game.c:Game_Task。
 * @param pulse_ms 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_Pulse(uint32_t pulse_ms)
{
    if (pulse_ms == 0U)
    {
        Solenoid_Release();
        return;
    }

    Solenoid_Engage();
    s_start_ms = Gtim_NowMs();
    s_pulse_ms = pulse_ms;
    s_busy = 1U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：周期处理（对应符号 Solenoid_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_Task(void)
{
    if ((s_busy != 0U) && (Gtim_IsElapsed(s_start_ms, s_pulse_ms) != 0U))
    {
        Solenoid_Release();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电磁铁控制：执行内部处理（对应符号 Solenoid_IsBusy）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Solenoid_IsBusy(void)
{
    return s_busy;
}

/**
 * @brief 电磁铁控制：执行内部处理（对应符号 Solenoid_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Solenoid_Release();
}

/**
 * @brief 电磁铁控制：执行内部处理（对应符号 Solenoid_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_Test2(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Solenoid_Pulse(300U);
}

/**
 * @brief 电磁铁控制：执行内部处理（对应符号 Solenoid_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Solenoid_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Solenoid_SetLow();
}
