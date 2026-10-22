/* AUTO-FILE-DOC
 * 文件职责：雨刷电机控制。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "wiper_motor.h"

#include "gtim.h"

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
static uint8_t s_running = 0U;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
static uint8_t s_stop_delay_active = 0U;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
static uint32_t s_stop_delay_start_ms = 0U;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
static uint32_t s_stop_delay_ms = 0U;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 雨刷电机控制：初始化（对应符号 WiperMotor_EarlySafeInit）。
 * @调用关系 直接调用者：gd32f407.c:GD32F407_Init、wiper_motor.c:WiperMotor_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_EarlySafeInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    __HAL_RCC_GPIOB_CLK_ENABLE();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(WIPER_MOTOR_PORT, WIPER_MOTOR_PIN, WIPER_MOTOR_STOP_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    gpio.Pin = WIPER_MOTOR_PIN;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    gpio.Mode = GPIO_MODE_OUTPUT_PP;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    gpio.Pull = GPIO_PULLDOWN;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    gpio.Speed = GPIO_SPEED_FREQ_LOW;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_Init(WIPER_MOTOR_PORT, &gpio);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_running = 0U;
    s_stop_delay_active = 0U;
    s_stop_delay_start_ms = 0U;
    s_stop_delay_ms = 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 雨刷电机控制：初始化（对应符号 WiperMotor_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_Init(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    WiperMotor_EarlySafeInit();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    WiperMotor_Stop();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 雨刷电机控制：周期处理（对应符号 WiperMotor_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_Task(void)
{
    if (s_stop_delay_active == 0U)
    {
        return;
    }

    if (Gtim_IsElapsed(s_stop_delay_start_ms, s_stop_delay_ms) != 0U)
    {
        WiperMotor_Stop();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 雨刷电机控制：执行内部处理（对应符号 WiperMotor_Run）。
 * @调用关系 直接调用者：wiper_motor.c:WiperMotor_Set、wiper_motor.c:WiperMotor_Test2、game.c:Game_RestoreOutputsAfterFault、game.c:Game_SetCoin。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_Run(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_stop_delay_active = 0U;
    s_stop_delay_start_ms = 0U;
    s_stop_delay_ms = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(WIPER_MOTOR_PORT, WIPER_MOTOR_PIN, WIPER_MOTOR_RUN_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_running = 1U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 雨刷电机控制：停止（对应符号 WiperMotor_Stop）。
 * @调用关系 直接调用者：wiper_motor.c:WiperMotor_Init、wiper_motor.c:WiperMotor_Task、wiper_motor.c:WiperMotor_StopAfter、wiper_motor.c:WiperMotor_Set、wiper_motor.c:WiperMotor_Test1、game.c:Game_Init、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_Stop(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_stop_delay_active = 0U;
    s_stop_delay_start_ms = 0U;
    s_stop_delay_ms = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(WIPER_MOTOR_PORT, WIPER_MOTOR_PIN, WIPER_MOTOR_STOP_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_running = 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 雨刷电机控制：停止（对应符号 WiperMotor_StopAfter）。
 * @调用关系 直接调用者：wiper_motor.c:WiperMotor_Test3、game.c:Game_SetCoin。
 * @param delay_ms 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_StopAfter(uint32_t delay_ms)
{
    if (delay_ms == 0U)
    {
        WiperMotor_Stop();
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(WIPER_MOTOR_PORT, WIPER_MOTOR_PIN, WIPER_MOTOR_RUN_LEVEL);
    s_running = 1U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_stop_delay_start_ms = Gtim_NowMs();
    s_stop_delay_ms = delay_ms;
    s_stop_delay_active = 1U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 雨刷电机控制：设置（对应符号 WiperMotor_Set）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param enable 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_Set(uint8_t enable)
{
    if (enable != 0U)
    {
        WiperMotor_Run();
    }
    else
    {
        WiperMotor_Stop();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 雨刷电机控制：执行内部处理（对应符号 WiperMotor_IsRunning）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t WiperMotor_IsRunning(void)
{
    return s_running;
}

/**
 * @brief 雨刷电机控制：执行内部处理（对应符号 WiperMotor_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    WiperMotor_Stop();
}

/**
 * @brief 雨刷电机控制：执行内部处理（对应符号 WiperMotor_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_Test2(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    WiperMotor_Run();
}

/**
 * @brief 雨刷电机控制：执行内部处理（对应符号 WiperMotor_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void WiperMotor_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    WiperMotor_StopAfter(1000U);
}
