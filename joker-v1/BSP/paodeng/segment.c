/* AUTO-FILE-DOC
 * 文件职责：数码屏和灯扫描。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "segment.h"

#include "gtim.h"
#include "./SYSTEM/delay/delay.h"

#define SEGMENT_INIT_55AA_HOLD_MS      100U

static uint8_t s_frame[SEGMENT_4094_COUNT];

static const uint8_t s_blank_frame[SEGMENT_4094_COUNT] = {0U};
static const uint8_t s_digit_code[10] =
{
    0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U,
    0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU
};

static void Segment_Init55AATest(void);
static void Segment_RefreshNow(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：初始化（对应符号 Segment_EarlySafeInit）。
 * @调用关系 直接调用者：gd32f407.c:GD32F407_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_EarlySafeInit(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(SEGMENT_CLK_PORT,
                      SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN,
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(SEGMENT_DATA_PORT, SEGMENT_DATA_PIN, GPIO_PIN_SET);

    gpio.Pin = SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SEGMENT_CLK_PORT, &gpio);

    gpio.Pin = SEGMENT_DATA_PIN;
    HAL_GPIO_Init(SEGMENT_DATA_PORT, &gpio);

    gpio.Pin = SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SEGMENT_CLK_PORT, &gpio);

    gpio.Pin = SEGMENT_DATA_PIN;
    HAL_GPIO_Init(SEGMENT_DATA_PORT, &gpio);

    HAL_GPIO_WritePin(SEGMENT_CLK_PORT,
                      SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN,
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(SEGMENT_DATA_PORT, SEGMENT_DATA_PIN, GPIO_PIN_SET);
}
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_LineLow）。
 * @调用关系 直接调用者：segment.c:Segment_ClockPulse、segment.c:Segment_LatchPulse、segment.c:Segment_SendFrame、segment.c:Segment_ClockPulse、segment.c:Segment_LatchPulse、segment.c:Segment_EyeLedClockPulse、segment.c:Segment_EyeLedLatchPulse、segment.c:Segment_EyeLedSendFrame。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param pin 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_LineLow(GPIO_TypeDef *port, uint16_t pin)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_LineHigh）。
 * @调用关系 直接调用者：segment.c:Segment_ClockPulse、segment.c:Segment_LatchPulse、segment.c:Segment_SendFrame、segment.c:Segment_ClockPulse、segment.c:Segment_LatchPulse、segment.c:Segment_EyeLedClockPulse、segment.c:Segment_EyeLedLatchPulse、segment.c:Segment_EyeLedSendFrame。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param pin 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_LineHigh(GPIO_TypeDef *port, uint16_t pin)
{
    HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_ClockPulse）。
 * @调用关系 直接调用者：segment.c:Segment_SendFrame、segment.c:Segment_SendFrame。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_ClockPulse(void)
{
    Segment_LineLow(SEGMENT_CLK_PORT, SEGMENT_CLK_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
    Segment_LineHigh(SEGMENT_CLK_PORT, SEGMENT_CLK_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
    Segment_LineLow(SEGMENT_CLK_PORT, SEGMENT_CLK_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_LatchPulse）。
 * @调用关系 直接调用者：segment.c:Segment_SendFrame、segment.c:Segment_SendFrame。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_LatchPulse(void)
{
    Segment_LineLow(SEGMENT_LATCH_PORT, SEGMENT_LATCH_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
    Segment_LineHigh(SEGMENT_LATCH_PORT, SEGMENT_LATCH_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
    Segment_LineLow(SEGMENT_LATCH_PORT, SEGMENT_LATCH_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：编码并发送（对应符号 Segment_SendFrame）。
 * @调用关系 直接调用者：segment.c:Segment_Init55AATest、segment.c:Segment_Init、segment.c:Segment_RefreshNow、segment.c:Segment_Init55AATest、segment.c:Segment_Init、segment.c:Segment_RefreshNow。
 * @局部状态 主要临时变量：byte_index、bit_index、bit_value；均只在本次调用中有效。
 * @param frame 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_SendFrame(const uint8_t *frame)
{
    uint8_t byte_index;
    uint8_t bit_index;
    uint8_t bit_value;

    Segment_LineLow(SEGMENT_CLK_PORT, SEGMENT_CLK_PIN);
    Segment_LineLow(SEGMENT_LATCH_PORT, SEGMENT_LATCH_PIN);

    for (byte_index = 0U; byte_index < SEGMENT_4094_COUNT; byte_index++)
    {
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            bit_value = (((uint8_t)(frame[(SEGMENT_4094_COUNT - 1U) - byte_index] << bit_index) & 0x80U) != 0U) ? 1U : 0U;

            if (bit_value != 0U)
            {
                Segment_LineHigh(SEGMENT_DATA_PORT, SEGMENT_DATA_PIN);
            }
            else
            {
                Segment_LineLow(SEGMENT_DATA_PORT, SEGMENT_DATA_PIN);
            }

            delay_us(SEGMENT_BIT_DELAY_US);
            Segment_ClockPulse();
        }
    }

    Segment_LatchPulse();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：初始化（对应符号 Segment_Init55AATest）。
 * @调用关系 直接调用者：segment.c:Segment_Init、segment.c:Segment_Init。
 * @局部状态 主要临时变量：i、test_frame；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_Init55AATest(void)
{
    uint8_t i;
    uint8_t test_frame[SEGMENT_4094_COUNT];

    for (i = 0U; i < SEGMENT_4094_COUNT; i++)
    {
        test_frame[i] = 0x55U;
    }
    Segment_SendFrame(test_frame);
    delay_ms(SEGMENT_INIT_55AA_HOLD_MS);

    for (i = 0U; i < SEGMENT_4094_COUNT; i++)
    {
        test_frame[i] = 0xAAU;
    }
    Segment_SendFrame(test_frame);
    delay_ms(SEGMENT_INIT_55AA_HOLD_MS);

    Segment_SendFrame(s_blank_frame);
    delay_ms(SEGMENT_POWER_CLEAR_GAP_MS);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetFourDigits）。
 * @调用关系 直接调用者：segment.c:Segment_SetLeftNumber、segment.c:Segment_SetMiddleNumber、segment.c:Segment_SetRightNumber、segment.c:Segment_SetLeftNumber、segment.c:Segment_SetMiddleNumber、segment.c:Segment_SetRightNumber。
 * @局部状态 主要临时变量：i、work_value、primask；均只在本次调用中有效。
 * @param start_index 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_SetFourDigits(uint8_t start_index, uint16_t value)
{
    uint8_t i;
    uint16_t work_value = value;
    uint32_t primask;

    if (work_value > 9999U)
    {
        work_value = 9999U;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    for (i = 0U; i < 4U; i++)
    {
        s_frame[(uint8_t)(start_index + i)] = 0U;
    }

    if (work_value == 0U)
    {
        s_frame[(uint8_t)(start_index + 3U)] = s_digit_code[0];
    }
    else
    {
        for (i = 0U; (i < 4U) && (work_value != 0U); i++)
        {
            s_frame[(uint8_t)(start_index + 3U - i)] = s_digit_code[work_value % 10U];
            work_value = (uint16_t)(work_value / 10U);
        }
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：初始化（对应符号 Segment_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：gpio、clear_count；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t clear_count;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    //Segment_ClearAll();

    __HAL_RCC_GPIOG_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    HAL_GPIO_WritePin(SEGMENT_CLK_PORT,
                      SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN,
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(SEGMENT_DATA_PORT, SEGMENT_DATA_PIN, GPIO_PIN_SET);

    gpio.Pin = SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SEGMENT_CLK_PORT, &gpio);

    gpio.Pin = SEGMENT_DATA_PIN;
    HAL_GPIO_Init(SEGMENT_DATA_PORT, &gpio);

    Segment_ClearAll();

    for (clear_count = 0U; clear_count < SEGMENT_POWER_CLEAR_FRAMES; clear_count++)
    {
        Segment_SendFrame(s_blank_frame);
        delay_ms(SEGMENT_POWER_CLEAR_GAP_MS);
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Segment_Init55AATest();

}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：周期处理（对应符号 Segment_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_Task(void)
{
    if (Gtim_Take20msFlag() != 0U)
    {
        Segment_RefreshNow();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：清除（对应符号 Segment_ClearAll）。
 * @调用关系 直接调用者：app_main.c:App_Init、segment.c:Segment_Init、self_test.c:SelfTest_ShowStable123、self_test.c:SelfTest_Task、segment.c:Segment_Init、segment.c:Segment_Test1、game.c:Game_RestoreGameDisplay、game.c:Game_EnterSettingMode。
 * @局部状态 主要临时变量：i、primask；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_ClearAll(void)
{
    uint8_t i;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    for (i = 0U; i < SEGMENT_4094_COUNT; i++)
    {
        s_frame[i] = 0U;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_RefreshNow）。
 * @调用关系 直接调用者：segment.c:Segment_Task、segment.c:Segment_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_RefreshNow(void)
{
    Segment_SendFrame(s_frame);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetLeftNumber）。
 * @调用关系 直接调用者：self_test.c:SelfTest_ShowDigit、self_test.c:SelfTest_ShowIdleZero、game.c:Game_RestoreGameDisplay、game.c:Game_SetScore、self_test.c:SelfTest_ShowDigit、self_test.c:SelfTest_ShowIdleZero、standby.c:Standby_BeginPart、standby.c:Standby_BeginLineAward。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetLeftNumber(uint16_t value)
{
    Segment_SetFourDigits(0U, value);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetMiddleNumber）。
 * @调用关系 直接调用者：self_test.c:SelfTest_ShowStable123、self_test.c:SelfTest_ShowDigit、self_test.c:SelfTest_ShowIdleZero、game.c:Game_TrySkipTicketOutput、game.c:Game_RestoreGameDisplay、game.c:Game_ShowValueOnMiddleRight、game.c:Game_ShowSettingPage、game.c:Game_Task。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetMiddleNumber(uint16_t value)
{
    Segment_SetFourDigits(4U, value);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetRightNumber）。
 * @调用关系 直接调用者：self_test.c:SelfTest_ShowDigit、self_test.c:SelfTest_ShowIdleZero、game.c:Game_RestoreGameDisplay、game.c:Game_ShowValueOnMiddleRight、game.c:Game_ShowSettingPage、game.c:Game_SetCoin、remote_config.c:Remote_ShowRight3、remote_config.c:Remote_DisplayTask。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetRightNumber(uint16_t value)
{
    Segment_SetFourDigits(8U, value);
}
