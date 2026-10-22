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
static uint8_t s_eye_led_frame[EYE_LED_4094_COUNT];

static const uint8_t s_blank_frame[SEGMENT_4094_COUNT] = {0U};
static const uint8_t s_eye_led_blank_frame[EYE_LED_4094_COUNT] = {0U};

/*
 * Channel number -> physical bit in the two cascaded eye-board 4094 chips.
 * Mapping was verified on the actual cabinet by the prior walking-one test.
 * Array index 0..8 corresponds to eye channel 1..9.
 */
static const uint8_t s_eye_channel_to_4094_bit[9] =
{
    3U, 2U, 1U, 0U, 4U, 12U, 13U, 14U, 15U
};

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
static const uint8_t s_lottery_lamp_frame_index[SEGMENT_LOTTERY_ROW_COUNT][SEGMENT_LOTTERY_TOTAL_COL_COUNT] =
{
    {12U, 12U, 13U, 13U},
    {12U, 12U, 13U, 13U},
    {12U, 12U, 13U, 13U},
    {12U, 12U, 13U, 13U}
};

static const uint8_t s_lottery_lamp_bit_mask[SEGMENT_LOTTERY_ROW_COUNT][SEGMENT_LOTTERY_TOTAL_COL_COUNT] =
{
    {(uint8_t)(1U << 4U), (uint8_t)(1U << 3U), (uint8_t)(1U << 7U), (uint8_t)(1U << 0U)},
    {(uint8_t)(1U << 5U), (uint8_t)(1U << 2U), (uint8_t)(1U << 6U), (uint8_t)(1U << 1U)},
    {(uint8_t)(1U << 6U), (uint8_t)(1U << 1U), (uint8_t)(1U << 5U), (uint8_t)(1U << 2U)},
    {(uint8_t)(1U << 7U), (uint8_t)(1U << 0U), (uint8_t)(1U << 4U), (uint8_t)(1U << 3U)}
};

static const uint8_t s_digit_code[10] =
{
    0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U,
    0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU
};

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define SEGMENT_CODE_BLANK             0x00U
#define SEGMENT_CODE_P                 0x73U
#define SEGMENT_CODE_A                 0x77U
#define SEGMENT_CODE_H                 0x76U
#define SEGMENT_CODE_F                 0x71U
#define SEGMENT_CODE_U                 0x3EU
#define SEGMENT_CODE_C                 0x39U
#define SEGMENT_CODE_E                 0x79U
#define SEGMENT_CODE_MINUS             0x40U

static void Segment_Init55AATest(void);
static void Segment_RefreshNow(void);
static void Segment_ClearFourDigits(uint8_t start_index);
static void Segment_SetLeftLetterNumber(uint8_t letter_code, uint8_t index);
static void Segment_EyeLedClockPulse(void);
static void Segment_EyeLedLatchPulse(void);
static void Segment_EyeLedSendFrame(const uint8_t *frame);
static void Segment_EyeLedRefreshNow(void);

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
                      SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN |
                      EYE_LED_CLK_PIN | EYE_LED_LATCH_PIN | EYE_LED_DATA_PIN,
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(SEGMENT_DATA_PORT, SEGMENT_DATA_PIN, GPIO_PIN_SET);

    gpio.Pin = SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN |
               EYE_LED_CLK_PIN | EYE_LED_LATCH_PIN | EYE_LED_DATA_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SEGMENT_CLK_PORT, &gpio);

    gpio.Pin = SEGMENT_DATA_PIN;
    HAL_GPIO_Init(SEGMENT_DATA_PORT, &gpio);

    gpio.Pin = SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN |
               EYE_LED_CLK_PIN | EYE_LED_LATCH_PIN | EYE_LED_DATA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(SEGMENT_CLK_PORT, &gpio);

    gpio.Pin = SEGMENT_DATA_PIN;
    HAL_GPIO_Init(SEGMENT_DATA_PORT, &gpio);

    HAL_GPIO_WritePin(SEGMENT_CLK_PORT,
                      SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN |
                      EYE_LED_CLK_PIN | EYE_LED_LATCH_PIN | EYE_LED_DATA_PIN,
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

/* Eye-board LED chain: PG4=C, PG5=L, PG6=DI; two 4094 chips. */
/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_EyeLedClockPulse）。
 * @调用关系 直接调用者：segment.c:Segment_EyeLedSendFrame。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_EyeLedClockPulse(void)
{
    Segment_LineLow(EYE_LED_CLK_PORT, EYE_LED_CLK_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
    Segment_LineHigh(EYE_LED_CLK_PORT, EYE_LED_CLK_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
    Segment_LineLow(EYE_LED_CLK_PORT, EYE_LED_CLK_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
}

/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_EyeLedLatchPulse）。
 * @调用关系 直接调用者：segment.c:Segment_EyeLedSendFrame。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_EyeLedLatchPulse(void)
{
    Segment_LineLow(EYE_LED_LATCH_PORT, EYE_LED_LATCH_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
    Segment_LineHigh(EYE_LED_LATCH_PORT, EYE_LED_LATCH_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
    Segment_LineLow(EYE_LED_LATCH_PORT, EYE_LED_LATCH_PIN);
    delay_us(SEGMENT_BIT_DELAY_US);
}

/**
 * @brief 数码屏和灯扫描：编码并发送（对应符号 Segment_EyeLedSendFrame）。
 * @调用关系 直接调用者：segment.c:Segment_EyeLedRefreshNow、segment.c:Segment_Init。
 * @局部状态 主要临时变量：byte_index、bit_index、bit_value；均只在本次调用中有效。
 * @param frame 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_EyeLedSendFrame(const uint8_t *frame)
{
    uint8_t byte_index;
    uint8_t bit_index;
    uint8_t bit_value;

    Segment_LineLow(EYE_LED_CLK_PORT, EYE_LED_CLK_PIN);
    Segment_LineLow(EYE_LED_LATCH_PORT, EYE_LED_LATCH_PIN);

    for (byte_index = 0U; byte_index < EYE_LED_4094_COUNT; byte_index++)
    {
        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            bit_value = (((uint8_t)(frame[(EYE_LED_4094_COUNT - 1U) - byte_index] << bit_index) & 0x80U) != 0U) ? 1U : 0U;

            if (bit_value != 0U)
            {
                Segment_LineHigh(EYE_LED_DATA_PORT, EYE_LED_DATA_PIN);
            }
            else
            {
                Segment_LineLow(EYE_LED_DATA_PORT, EYE_LED_DATA_PIN);
            }

            delay_us(SEGMENT_BIT_DELAY_US);
            Segment_EyeLedClockPulse();
        }
    }

    Segment_EyeLedLatchPulse();
}

/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_EyeLedRefreshNow）。
 * @调用关系 直接调用者：segment.c:Segment_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_EyeLedRefreshNow(void)
{
    Segment_EyeLedSendFrame(s_eye_led_frame);
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
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetFourDigitsFixed）。
 * @调用关系 直接调用者：segment.c:Segment_SetLeftFixed4、segment.c:Segment_SetMiddleFixed4、segment.c:Segment_SetRightFixed4。
 * @局部状态 主要临时变量：i、divisor、work_value、primask；均只在本次调用中有效。
 * @param start_index 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_SetFourDigitsFixed(uint8_t start_index, uint16_t value)
{
    uint8_t i;
    uint16_t divisor = 1000U;
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
        s_frame[(uint8_t)(start_index + i)] = s_digit_code[work_value / divisor];
        work_value = (uint16_t)(work_value % divisor);
        divisor = (uint16_t)(divisor / 10U);
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：清除（对应符号 Segment_ClearFourDigits）。
 * @调用关系 直接调用者：segment.c:Segment_ClearLeft、segment.c:Segment_ClearMiddle、segment.c:Segment_ClearRight。
 * @局部状态 主要临时变量：i、primask；均只在本次调用中有效。
 * @param start_index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_ClearFourDigits(uint8_t start_index)
{
    uint8_t i;
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    for (i = 0U; i < 4U; i++)
    {
        s_frame[(uint8_t)(start_index + i)] = SEGMENT_CODE_BLANK;
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetLeftLetterNumber）。
 * @调用关系 直接调用者：segment.c:Segment_SetLeftPNumber、segment.c:Segment_SetLeftCNumber、segment.c:Segment_SetLeftANumber、segment.c:Segment_SetLeftHNumber、segment.c:Segment_SetLeftFNumber、segment.c:Segment_SetLeftUNumber。
 * @局部状态 主要临时变量：tens、ones、primask；均只在本次调用中有效。
 * @param letter_code 按值传入；函数内部修改不会回写调用方变量。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Segment_SetLeftLetterNumber(uint8_t letter_code, uint8_t index)
{
    uint8_t tens;
    uint8_t ones;
    uint32_t primask;

    if (index > 99U)
    {
        index = 99U;
    }

    tens = (uint8_t)(index / 10U);
    ones = (uint8_t)(index % 10U);

    primask = __get_PRIMASK();
    __disable_irq();

    s_frame[0] = SEGMENT_CODE_BLANK;
    s_frame[1] = SEGMENT_CODE_BLANK;
    s_frame[2] = SEGMENT_CODE_BLANK;
    s_frame[3] = SEGMENT_CODE_BLANK;

    if (index < 10U)
    {
        s_frame[2] = letter_code;
        s_frame[3] = s_digit_code[ones];
    }
    else
    {
        s_frame[1] = letter_code;
        s_frame[2] = s_digit_code[tens];
        s_frame[3] = s_digit_code[ones];
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
                      SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN |
                      EYE_LED_CLK_PIN | EYE_LED_LATCH_PIN | EYE_LED_DATA_PIN,
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(SEGMENT_DATA_PORT, SEGMENT_DATA_PIN, GPIO_PIN_SET);

    gpio.Pin = SEGMENT_CLK_PIN | SEGMENT_LATCH_PIN |
               EYE_LED_CLK_PIN | EYE_LED_LATCH_PIN | EYE_LED_DATA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(SEGMENT_CLK_PORT, &gpio);

    gpio.Pin = SEGMENT_DATA_PIN;
    HAL_GPIO_Init(SEGMENT_DATA_PORT, &gpio);

    Segment_ClearAll();
    Segment_EyeLedClearAll();

    for (clear_count = 0U; clear_count < SEGMENT_POWER_CLEAR_FRAMES; clear_count++)
    {
        Segment_SendFrame(s_blank_frame);
        Segment_EyeLedSendFrame(s_eye_led_blank_frame);
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
        Segment_EyeLedRefreshNow();
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

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetLeftFixed4）。
 * @调用关系 直接调用者：segment.c:Segment_Test2、machine_lock.c:MachineLock_RenderLocked。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetLeftFixed4(uint16_t value)
{
    Segment_SetFourDigitsFixed(0U, value);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetMiddleFixed4）。
 * @调用关系 直接调用者：segment.c:Segment_Test2、game.c:Game_ShowAccountPage、machine_lock.c:MachineLock_RenderLocked。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetMiddleFixed4(uint16_t value)
{
    Segment_SetFourDigitsFixed(4U, value);
}

void Segment_SetMiddleFixed3(uint16_t value)
{
    uint32_t primask;

    value %= 1000U;
    primask = __get_PRIMASK();
    __disable_irq();
    s_frame[4] = SEGMENT_CODE_BLANK;
    s_frame[5] = s_digit_code[(value / 100U) % 10U];
    s_frame[6] = s_digit_code[(value / 10U) % 10U];
    s_frame[7] = s_digit_code[value % 10U];
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetRightFixed4）。
 * @调用关系 直接调用者：segment.c:Segment_Test2、game.c:Game_ShowValueOnMiddleRight、game.c:Game_ShowAccountPage、machine_lock.c:MachineLock_RenderLocked。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetRightFixed4(uint16_t value)
{
    Segment_SetFourDigitsFixed(8U, value);
}

/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetMiddleRightSigned）。
 * @调用关系 直接调用者：game.c:Game_ShowAccountProfit、machine_lock.c:MachineLock_RenderQuery。
 * @局部状态 主要临时变量：magnitude、digit_count、index、negative、primask；均只在本次调用中有效。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetMiddleRightSigned(int64_t value)
{
    uint64_t magnitude;
    uint8_t digit_count = 0U;
    uint8_t index;
    uint8_t negative = (value < 0) ? 1U : 0U;
    uint32_t primask;

    if (negative != 0U)
    {
        /* This form is also safe if value is INT64_MIN. */
        magnitude = (uint64_t)(-(value + 1)) + 1ULL;
        if (magnitude > 9999999ULL)
        {
            magnitude = 9999999ULL;
        }
    }
    else
    {
        magnitude = (uint64_t)value;
        if (magnitude > 99999999ULL)
        {
            magnitude = 99999999ULL;
        }
    }

    primask = __get_PRIMASK();
    __disable_irq();

    for (index = 4U; index < 12U; index++)
    {
        s_frame[index] = SEGMENT_CODE_BLANK;
    }

    do
    {
        s_frame[(uint8_t)(11U - digit_count)] =
            s_digit_code[magnitude % 10ULL];
        magnitude /= 10ULL;
        digit_count++;
    } while ((magnitude != 0ULL) && (digit_count < 8U));

    if (negative != 0U)
    {
        s_frame[(uint8_t)(11U - digit_count)] = SEGMENT_CODE_MINUS;
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：清除（对应符号 Segment_ClearLeft）。
 * @调用关系 直接调用者：game.c:Game_ShowSettingPage、game.c:Game_ShowAccountPage、remote_config.c:Remote_DisplayTask。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_ClearLeft(void)
{
    Segment_ClearFourDigits(0U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：清除（对应符号 Segment_ClearMiddle）。
 * @调用关系 直接调用者：segment.c:Segment_SetFaultCode、game.c:Game_ShowValueOnMiddleRight、game.c:Game_ShowSettingPage、game.c:Game_ShowAccountPage、remote_config.c:Remote_ShowRight3、remote_config.c:Remote_DisplayTask。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_ClearMiddle(void)
{
    Segment_ClearFourDigits(4U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：清除（对应符号 Segment_ClearRight）。
 * @调用关系 直接调用者：game.c:Game_ShowSettingPage、game.c:Game_ShowAccountPage、remote_config.c:Remote_DisplayTask。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_ClearRight(void)
{
    Segment_ClearFourDigits(8U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetLeftPNumber）。
 * @调用关系 直接调用者：game.c:Game_ShowSettingPage、remote_config.c:Remote_DisplayTask。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetLeftPNumber(uint8_t index)
{
    Segment_SetLeftLetterNumber(SEGMENT_CODE_P, index);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetLeftCNumber）。
 * @调用关系 直接调用者：game.c:Game_ShowAccountPage、remote_config.c:Remote_DisplayTask。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetLeftCNumber(uint8_t index)
{
    Segment_SetLeftLetterNumber(SEGMENT_CODE_C, index);
}

/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetLeftANumber）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_RenderDigits、machine_lock.c:MachineLock_RenderEightDigitValue。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetLeftANumber(uint8_t index)
{
    Segment_SetLeftLetterNumber(SEGMENT_CODE_A, index);
}

/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetLeftHNumber）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_RenderEightDigitValue、machine_lock.c:MachineLock_RenderQuery。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetLeftHNumber(uint8_t index)
{
    Segment_SetLeftLetterNumber(SEGMENT_CODE_H, index);
}

/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetLeftFNumber）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_RenderDigits。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetLeftFNumber(uint8_t index)
{
    Segment_SetLeftLetterNumber(SEGMENT_CODE_F, index);
}

/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetLeftUNumber）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_RenderDigits、machine_lock.c:MachineLock_RenderEightDigitValue。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetLeftUNumber(uint8_t index)
{
    Segment_SetLeftLetterNumber(SEGMENT_CODE_U, index);
}

void Segment_SetLeftENumber(uint8_t index)
{
    Segment_SetLeftLetterNumber(SEGMENT_CODE_E, index);
}

void Segment_SetMiddleENumber(uint8_t index)
{
    uint8_t tens;
    uint8_t ones;
    uint32_t primask;

    if (index > 99U)
    {
        index = 99U;
    }
    tens = (uint8_t)(index / 10U);
    ones = (uint8_t)(index % 10U);

    primask = __get_PRIMASK();
    __disable_irq();
    s_frame[4] = SEGMENT_CODE_BLANK;
    s_frame[5] = SEGMENT_CODE_E;
    s_frame[6] = s_digit_code[tens];
    s_frame[7] = s_digit_code[ones];
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetMiddleRightDigits）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_RenderDigits、machine_lock.c:MachineLock_RenderEightDigitValue。
 * @局部状态 主要临时变量：i、digit、primask；均只在本次调用中有效。
 * @param digits 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param blank_index 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetMiddleRightDigits(const uint8_t digits[8], int8_t blank_index)
{
    uint8_t i;
    uint8_t digit;
    uint32_t primask;

    if (digits == (const uint8_t *)0)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    for (i = 0U; i < 8U; i++)
    {
        digit = digits[i];
        if ((blank_index == (int8_t)i) || (digit > 9U))
        {
            s_frame[(uint8_t)(4U + i)] = SEGMENT_CODE_BLANK;
        }
        else
        {
            s_frame[(uint8_t)(4U + i)] = s_digit_code[digit];
        }
    }
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_SetFaultCode）。
 * @调用关系 直接调用者：fault.c:Fault_Init、fault.c:Fault_Task、fault.c:Fault_Report、fault.c:Fault_Clear、game.c:Game_Task。
 * @局部状态 主要临时变量：i、tens、ones、primask；均只在本次调用中有效。
 * @param code 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_SetFaultCode(uint8_t code)
{
    uint8_t i;
    uint8_t tens;
    uint8_t ones;
    uint32_t primask;

    if (code == 0U)
    {
        Segment_ClearMiddle();
        return;
    }

    if (code > 99U)
    {
        code = 99U;
    }

    tens = (uint8_t)(code / 10U);
    ones = (uint8_t)(code % 10U);

    primask = __get_PRIMASK();
    __disable_irq();

    for (i = 0U; i < 4U; i++)
    {
        s_frame[i] = SEGMENT_CODE_BLANK;
        s_frame[(uint8_t)(8U + i)] = SEGMENT_CODE_BLANK;
    }

    s_frame[4] = SEGMENT_CODE_BLANK;
    s_frame[5] = SEGMENT_CODE_E;
    s_frame[6] = s_digit_code[tens];
    s_frame[7] = s_digit_code[ones];

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief Clear all two-4094 eye-board LED outputs.
 *
 * The actual PG4/PG5/PG6 transfer occurs on the next 20 ms call of
 * Segment_Task().
 */
/**
 * @brief 数码屏和灯扫描：清除（对应符号 Segment_EyeLedClearAll）。
 * @调用关系 直接调用者：segment.c:Segment_Init、segment.c:Segment_Test1、game.c:Game_FinishLineRound、game.c:Game_StartFailEyeBlink、game.c:Game_RestoreGameDisplay、game.c:Game_Init、game.c:Game_Task、gift_light.c:GiftLight_ShowRunFrame。
 * @局部状态 主要临时变量：i、primask；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_EyeLedClearAll(void)
{
    uint8_t i;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    for (i = 0U; i < EYE_LED_4094_COUNT; i++)
    {
        s_eye_led_frame[i] = 0U;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief Turn on the eye-board LEDs corresponding to an Eye channel mask.
 *
 * eye_channel_mask bit0..bit8 mean channel 1..9. The board wiring is not
 * sequential, so this function translates channel bits to the measured 4094
 * output bits before setting them in the two-byte LED frame.
 *
 * This is an OR operation: lamps that were already on remain on.
 */
/**
 * @brief 数码屏和灯扫描：设置（对应符号 Segment_EyeLedSetChannelMask）。
 * @调用关系 直接调用者：game.c:Game_StartFailEyeBlink、game.c:Game_UpdateFailEyeBlink、game.c:Game_RestoreGameDisplay、game.c:Game_Task、gift_light.c:GiftLight_ShowRunFrame、gift_light.c:GiftLight_ShowConfirmFrame、gift_light.c:GiftLight_Task、self_test.c:SelfTest_ShowEyeLed。
 * @局部状态 主要临时变量：channel_index、bit_index、primask；均只在本次调用中有效。
 * @param eye_channel_mask 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_EyeLedSetChannelMask(uint16_t eye_channel_mask)
{
    uint8_t channel_index;
    uint8_t bit_index;
    uint32_t primask;

    eye_channel_mask &= 0x01FFU;
    if (eye_channel_mask == 0U)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    for (channel_index = 0U; channel_index < 9U; channel_index++)
    {
        if ((eye_channel_mask & (uint16_t)(1U << channel_index)) != 0U)
        {
            bit_index = s_eye_channel_to_4094_bit[channel_index];
            s_eye_led_frame[bit_index / 8U] |= (uint8_t)(1U << (bit_index % 8U));
        }
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 数码屏和灯扫描：清除（对应符号 Segment_EyeLedClearChannelMask）。
 * @调用关系 直接调用者：game.c:Game_UpdateFailEyeBlink、lottery.c:Lottery_Task。
 * @局部状态 主要临时变量：channel_index、bit_index、primask；均只在本次调用中有效。
 * @param eye_channel_mask 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_EyeLedClearChannelMask(uint16_t eye_channel_mask)
{
    uint8_t channel_index;
    uint8_t bit_index;
    uint32_t primask;

    eye_channel_mask &= 0x01FFU;
    if (eye_channel_mask == 0U)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();

    for (channel_index = 0U; channel_index < 9U; channel_index++)
    {
        if ((eye_channel_mask & (uint16_t)(1U << channel_index)) != 0U)
        {
            bit_index = s_eye_channel_to_4094_bit[channel_index];
            s_eye_led_frame[bit_index / 8U] &= (uint8_t)(~(uint8_t)(1U << (bit_index % 8U)));
        }
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 数码屏和灯扫描：清除（对应符号 Segment_LotteryLampClearAll）。
 * @调用关系 直接调用者：segment.c:Segment_Test1、lottery.c:Lottery_UpdateWinBlink、lottery.c:Lottery_StartOneRequest、lottery.c:Lottery_Init、lottery.c:Lottery_Task、self_test.c:SelfTest_StopLampTests、self_test.c:SelfTest_Init、standby.c:Standby_Stop。
 * @局部状态 主要临时变量：primask；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_LotteryLampClearAll(void)
{
    uint32_t primask;

    primask = __get_PRIMASK();
    __disable_irq();

    s_frame[12] = 0U;
    s_frame[13] = 0U;

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 数码屏和灯扫描：刷新显示（对应符号 Segment_LotteryLampShow）。
 * @调用关系 直接调用者：segment.c:Segment_Test3、lottery.c:Lottery_UpdateWinBlink、lottery.c:Lottery_StartOneRequest、lottery.c:Lottery_Task、self_test.c:SelfTest_ShowLotteryRow、standby.c:Standby_ShowDiagonal、standby.c:Standby_BeginPart、standby.c:Standby_Task。
 * @局部状态 主要临时变量：row、col、frame_index、bit_mask、primask；均只在本次调用中有效。
 * @param col1_row 按值传入；函数内部修改不会回写调用方变量。
 * @param col2_row 按值传入；函数内部修改不会回写调用方变量。
 * @param col3_row 按值传入；函数内部修改不会回写调用方变量。
 * @param prize_row 按值传入；函数内部修改不会回写调用方变量。
 * @param show_prize 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_LotteryLampShow(uint8_t col1_row,
                             uint8_t col2_row,
                             uint8_t col3_row,
                             uint8_t prize_row,
                             uint8_t show_prize)
{
    uint8_t row[SEGMENT_LOTTERY_SLOT_COL_COUNT];
    uint8_t col;
    uint8_t frame_index;
    uint8_t bit_mask;
    uint32_t primask;

    row[0] = col1_row;
    row[1] = col2_row;
    row[2] = col3_row;

    primask = __get_PRIMASK();
    __disable_irq();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_frame[12] = 0U;
    s_frame[13] = 0U;

    for (col = 0U; col < SEGMENT_LOTTERY_SLOT_COL_COUNT; col++)
    {
        if (row[col] < SEGMENT_LOTTERY_ROW_COUNT)
        {
            frame_index = s_lottery_lamp_frame_index[row[col]][col];
            bit_mask = s_lottery_lamp_bit_mask[row[col]][col];
            s_frame[frame_index] |= bit_mask;
        }
    }

    if ((show_prize != 0U) && (prize_row < SEGMENT_LOTTERY_ROW_COUNT))
    {
        frame_index = s_lottery_lamp_frame_index[prize_row][3U];
        bit_mask = s_lottery_lamp_bit_mask[prize_row][3U];
        s_frame[frame_index] |= bit_mask;
    }

    if (primask == 0U)
    {
        __enable_irq();
    }
}

/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Segment_ClearAll();
    Segment_LotteryLampClearAll();
    Segment_EyeLedClearAll();
}

/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_Test2(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Segment_SetLeftFixed4(1234U);
    Segment_SetMiddleFixed4(5678U);
    Segment_SetRightFixed4(90U);
}

/**
 * @brief 数码屏和灯扫描：执行内部处理（对应符号 Segment_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Segment_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Segment_LotteryLampShow(0U, 0U, 0U, 0U, 1U);
}
