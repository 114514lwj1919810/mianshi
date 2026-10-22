/* AUTO-FILE-DOC
 * 文件职责：电机 GPIO 与定时动作。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "motor.h"

#include "gtim.h"

/* 一台逻辑电机对应正/反两个控制输出。GPIO_TypeDef * 是外设寄存器基址，
 * 只保存地址，不复制寄存器；pin 是可按位组合的 GPIO_PIN_x 掩码。 */
typedef struct
{
    GPIO_TypeDef *forward_port;
    uint16_t forward_pin;
    GPIO_TypeDef *reverse_port;
    uint16_t reverse_pin;
} Motor_PinMap;

/* 把四条物理控制线展平成测试通道；产测可按 channel 单独测量电平。 */
typedef struct
{
    Motor_Id motor;
    Motor_Direction direction;
    GPIO_TypeDef *port;
    uint16_t pin;
} Motor_LineMap;

/* 下标严格等于 Motor_Id；改引脚时只改 motor.h 映射宏，不在业务层散写端口。 */
static const Motor_PinMap s_motor_pin[MOTOR_COUNT] =
{
    {MOTOR_GAME_COIN_FORWARD_PORT, MOTOR_GAME_COIN_FORWARD_PIN, MOTOR_GAME_COIN_REVERSE_PORT, MOTOR_GAME_COIN_REVERSE_PIN},
    {MOTOR_WIN_PRIZE_FORWARD_PORT, MOTOR_WIN_PRIZE_FORWARD_PIN, MOTOR_WIN_PRIZE_REVERSE_PORT, MOTOR_WIN_PRIZE_REVERSE_PIN}
};

/* 产测通道到“逻辑电机 + 方向 + GPIO”的只读查表。 */
static const Motor_LineMap s_motor_line[MOTOR_LINE_COUNT] =
{
    {MOTOR_GAME_COIN, MOTOR_DIR_FORWARD, MOTOR_GAME_COIN_FORWARD_PORT, MOTOR_GAME_COIN_FORWARD_PIN},
    {MOTOR_GAME_COIN, MOTOR_DIR_REVERSE, MOTOR_GAME_COIN_REVERSE_PORT, MOTOR_GAME_COIN_REVERSE_PIN},
    {MOTOR_WIN_PRIZE, MOTOR_DIR_FORWARD, MOTOR_WIN_PRIZE_FORWARD_PORT, MOTOR_WIN_PRIZE_FORWARD_PIN},
    {MOTOR_WIN_PRIZE, MOTOR_DIR_REVERSE, MOTOR_WIN_PRIZE_REVERSE_PORT, MOTOR_WIN_PRIZE_REVERSE_PIN}
};

/* 每台电机独立的非阻塞脉冲状态：busy=1 后由 Motor_Task 检查经过时间并停止。 */
static volatile uint8_t s_busy[MOTOR_COUNT] = {0U, 0U};

/* 脉冲开始时刻，使用无符号差值处理毫秒计数器回绕。 */
static volatile uint32_t s_start_ms[MOTOR_COUNT] = {0U, 0U};

/* 本次命令要求保持输出的毫秒数。 */
static volatile uint32_t s_run_ms[MOTOR_COUNT] = {0U, 0U};

/* 软件最后一次写入的方向，仅代表运行期 GPIO 状态；复位高阻期仍由硬件上下拉决定。 */
static volatile Motor_Direction s_direction[MOTOR_COUNT] = {MOTOR_DIR_STOP, MOTOR_DIR_STOP};

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：校验（对应符号 Motor_IsValidId）。
 * @调用关系 直接调用者：motor.c:Motor_Stop、motor.c:Motor_RunForward、motor.c:Motor_RunReverse、motor.c:Motor_PulseForward、motor.c:Motor_PulseReverse、motor.c:Motor_IsBusy、motor.c:Motor_GetDirection。
 * @param motor 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Motor_IsValidId(Motor_Id motor)
{
    return ((uint32_t)motor < (uint32_t)MOTOR_COUNT) ? 1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：校验（对应符号 Motor_StopValid）。
 * @调用关系 直接调用者：motor.c:Motor_Task、motor.c:Motor_Stop、motor.c:Motor_StopAll、motor.c:Motor_PulseForward、motor.c:Motor_PulseReverse、motor.c:Motor_WriteLine。
 * @param motor 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Motor_StopValid(Motor_Id motor)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].forward_port, s_motor_pin[motor].forward_pin, MOTOR_STOP_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].reverse_port, s_motor_pin[motor].reverse_pin, MOTOR_STOP_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_busy[motor] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_start_ms[motor] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_run_ms[motor] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_direction[motor] = MOTOR_DIR_STOP;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：初始化（对应符号 Motor_EarlySafeInit）。
 * @调用关系 直接调用者：gd32f407.c:GD32F407_Init、motor.c:Motor_Init、main.c:main。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_EarlySafeInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    /* Game motor: H5/H12 = PD8/PD15; payout motor: H6/H14 = PD9/PG3. */
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    /* Preload the safe level before changing the pins to output mode. */
    HAL_GPIO_WritePin(GPIOD,
                      MOTOR_GAME_COIN_FORWARD_PIN |
                      MOTOR_GAME_COIN_REVERSE_PIN |
                      MOTOR_WIN_PRIZE_FORWARD_PIN,
                      MOTOR_STOP_LEVEL);
    HAL_GPIO_WritePin(GPIOG,
                      MOTOR_WIN_PRIZE_REVERSE_PIN,
                      MOTOR_STOP_LEVEL);

    gpio.Pin = MOTOR_GAME_COIN_FORWARD_PIN |
               MOTOR_GAME_COIN_REVERSE_PIN |
               MOTOR_WIN_PRIZE_FORWARD_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &gpio);

    gpio.Pin = MOTOR_WIN_PRIZE_REVERSE_PIN;
    HAL_GPIO_Init(GPIOG, &gpio);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：初始化（对应符号 Motor_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_Init(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Motor_EarlySafeInit();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_busy[MOTOR_GAME_COIN] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_busy[MOTOR_WIN_PRIZE] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_start_ms[MOTOR_GAME_COIN] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_start_ms[MOTOR_WIN_PRIZE] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_run_ms[MOTOR_GAME_COIN] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_run_ms[MOTOR_WIN_PRIZE] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_direction[MOTOR_GAME_COIN] = MOTOR_DIR_STOP;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_direction[MOTOR_WIN_PRIZE] = MOTOR_DIR_STOP;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Motor_StopAll();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：周期处理（对应符号 Motor_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：motor；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_Task(void)
{
    Motor_Id motor;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    motor = MOTOR_GAME_COIN;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    while (motor < MOTOR_COUNT)
    {
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        if ((s_busy[motor] != 0U) && (Gtim_IsElapsed(s_start_ms[motor], s_run_ms[motor]) != 0U))
        {
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            Motor_StopValid(motor);
        }

        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        motor = (Motor_Id)((uint32_t)motor + 1U);
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：停止（对应符号 Motor_Stop）。
 * @调用关系 直接调用者：game.c:Game_Task、payout.c:Payout_Init、payout.c:Payout_Task。
 * @param motor 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_Stop(Motor_Id motor)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Motor_IsValidId(motor) == 0U)
    {
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Motor_StopValid(motor);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：停止（对应符号 Motor_StopAll）。
 * @调用关系 直接调用者：motor.c:Motor_Init、motor.c:Motor_Test1、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_StopAll(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Motor_StopValid(MOTOR_GAME_COIN);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Motor_StopValid(MOTOR_WIN_PRIZE);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：执行内部处理（对应符号 Motor_RunForward）。
 * @调用关系 直接调用者：game.c:Game_RestoreOutputsAfterFault、game.c:Game_Task、payout.c:Payout_AddPrize。
 * @param motor 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_RunForward(Motor_Id motor)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Motor_IsValidId(motor) == 0U)
    {
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].reverse_port, s_motor_pin[motor].reverse_pin, MOTOR_STOP_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].forward_port, s_motor_pin[motor].forward_pin, MOTOR_RUN_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_busy[motor] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_start_ms[motor] = 0U;
    s_run_ms[motor] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_direction[motor] = MOTOR_DIR_FORWARD;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：执行内部处理（对应符号 Motor_RunReverse）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param motor 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_RunReverse(Motor_Id motor)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Motor_IsValidId(motor) == 0U)
    {
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].forward_port, s_motor_pin[motor].forward_pin, MOTOR_STOP_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].reverse_port, s_motor_pin[motor].reverse_pin, MOTOR_RUN_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_busy[motor] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_start_ms[motor] = 0U;
    s_run_ms[motor] = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_direction[motor] = MOTOR_DIR_REVERSE;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：执行内部处理（对应符号 Motor_PulseForward）。
 * @调用关系 直接调用者：motor.c:Motor_Test2、motor.c:Motor_Test3。
 * @param motor 按值传入；函数内部修改不会回写调用方变量。
 * @param run_ms 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_PulseForward(Motor_Id motor, uint32_t run_ms)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Motor_IsValidId(motor) == 0U)
    {
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (run_ms == 0U)
    {
        Motor_StopValid(motor);
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].reverse_port, s_motor_pin[motor].reverse_pin, MOTOR_STOP_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].forward_port, s_motor_pin[motor].forward_pin, MOTOR_RUN_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_start_ms[motor] = Gtim_NowMs();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_run_ms[motor] = run_ms;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_busy[motor] = 1U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_direction[motor] = MOTOR_DIR_FORWARD;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：执行内部处理（对应符号 Motor_PulseReverse）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param motor 按值传入；函数内部修改不会回写调用方变量。
 * @param run_ms 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_PulseReverse(Motor_Id motor, uint32_t run_ms)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Motor_IsValidId(motor) == 0U)
    {
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (run_ms == 0U)
    {
        Motor_StopValid(motor);
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].forward_port, s_motor_pin[motor].forward_pin, MOTOR_STOP_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_WritePin(s_motor_pin[motor].reverse_port, s_motor_pin[motor].reverse_pin, MOTOR_RUN_LEVEL);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_start_ms[motor] = Gtim_NowMs();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_run_ms[motor] = run_ms;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_busy[motor] = 1U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_direction[motor] = MOTOR_DIR_REVERSE;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：写入（对应符号 Motor_WriteLine）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：motor、direction；均只在本次调用中有效。
 * @param line 按值传入；函数内部修改不会回写调用方变量。
 * @param enable 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_WriteLine(Motor_Line line, uint8_t enable)
{
    Motor_Id motor;
    Motor_Direction direction;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if ((uint32_t)line >= (uint32_t)MOTOR_LINE_COUNT)
    {
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    motor = s_motor_line[line].motor;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    direction = s_motor_line[line].direction;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_busy[motor] = 0U;
    s_start_ms[motor] = 0U;
    s_run_ms[motor] = 0U;

    if (enable != 0U)
    {
        if (direction == MOTOR_DIR_FORWARD)
        {
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            HAL_GPIO_WritePin(s_motor_pin[motor].reverse_port, s_motor_pin[motor].reverse_pin, MOTOR_STOP_LEVEL);
        }
        else
        {
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            HAL_GPIO_WritePin(s_motor_pin[motor].forward_port, s_motor_pin[motor].forward_pin, MOTOR_STOP_LEVEL);
        }

        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        HAL_GPIO_WritePin(s_motor_line[line].port, s_motor_line[line].pin, MOTOR_RUN_LEVEL);

        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        s_direction[motor] = direction;
    }
    else
    {
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        HAL_GPIO_WritePin(s_motor_line[line].port, s_motor_line[line].pin, MOTOR_STOP_LEVEL);

        if (s_direction[motor] == direction)
        {
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            Motor_StopValid(motor);
        }
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：执行内部处理（对应符号 Motor_IsBusy）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param motor 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Motor_IsBusy(Motor_Id motor)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Motor_IsValidId(motor) == 0U)
    {
        return 0U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    return s_busy[motor];
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 电机 GPIO 与定时动作：读取（对应符号 Motor_GetDirection）。
 * @调用关系 直接调用者：fault.c:Fault_CheckGameMotorNoCount、game.c:Game_Task。
 * @param motor 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
Motor_Direction Motor_GetDirection(Motor_Id motor)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Motor_IsValidId(motor) == 0U)
    {
        return MOTOR_DIR_STOP;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    return s_direction[motor];
}

/**
 * @brief 电机 GPIO 与定时动作：执行内部处理（对应符号 Motor_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Motor_StopAll();
}

/**
 * @brief 电机 GPIO 与定时动作：执行内部处理（对应符号 Motor_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_Test2(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Motor_PulseForward(MOTOR_GAME_COIN, 300U);
}

/**
 * @brief 电机 GPIO 与定时动作：执行内部处理（对应符号 Motor_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Motor_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Motor_PulseForward(MOTOR_WIN_PRIZE, 300U);
}
