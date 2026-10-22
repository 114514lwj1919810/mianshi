/* AUTO-FILE-DOC
 * 文件职责：彩金显示板通信。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "jackpot_display.h"
#include "gtim.h"

#include "./SYSTEM/delay/delay.h"

static uint8_t s_frame[JACKPOT_DISPLAY_4094_COUNT];
static uint16_t s_value = 0xFFFFU;
static uint8_t s_dirty;
static uint8_t s_ready;
static uint32_t s_last_refresh_ms;
static uint8_t s_blink_enabled;
static uint8_t s_blink_visible;
static uint32_t s_blink_tick_ms;
/* Local presentation layer; real/network SetValue calls do not overwrite it. */
static uint8_t s_demo_frame[JACKPOT_DISPLAY_4094_COUNT];
static uint8_t s_demo_active;
static uint8_t s_demo_blink;
static uint16_t s_demo_value;

static const uint8_t s_blank_frame[JACKPOT_DISPLAY_4094_COUNT] = {0U};

/* H743 鹿小班测试在实际彩金板上验证通过的低有效段码。 */
static const uint8_t s_digit_code[10] =
{
    0xFAU, 0x90U, 0x79U, 0xD9U, 0x93U,
    0xCBU, 0xEBU, 0x98U, 0xFBU, 0xDBU
};

/* 与已验证4094时序一致；这里只写传输电平，不处理输出端段码极性。 */
/**
 * @brief 彩金显示板通信：写入（对应符号 JackpotDisplay_Write）。
 * @调用关系 直接调用者：jackpot_display.c:JackpotDisplay_ClockPulse、jackpot_display.c:JackpotDisplay_LatchPulse、jackpot_display.c:JackpotDisplay_SendFrame。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param pin 按值传入；函数内部修改不会回写调用方变量。
 * @param logical_high 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void JackpotDisplay_Write(GPIO_TypeDef *port,
                                 uint16_t pin,
                                 uint8_t logical_high)
{
    HAL_GPIO_WritePin(port, pin,
                      (logical_high != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

/**
 * @brief 彩金显示板通信：执行内部处理（对应符号 JackpotDisplay_ClockPulse）。
 * @调用关系 直接调用者：jackpot_display.c:JackpotDisplay_SendFrame。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void JackpotDisplay_ClockPulse(void)
{
    JackpotDisplay_Write(JACKPOT_DISPLAY_CLK_PORT,
                         JACKPOT_DISPLAY_CLK_PIN, 0U);
    delay_us(JACKPOT_DISPLAY_BIT_DELAY_US);
    JackpotDisplay_Write(JACKPOT_DISPLAY_CLK_PORT,
                         JACKPOT_DISPLAY_CLK_PIN, 1U);
    delay_us(JACKPOT_DISPLAY_BIT_DELAY_US);
    JackpotDisplay_Write(JACKPOT_DISPLAY_CLK_PORT,
                         JACKPOT_DISPLAY_CLK_PIN, 0U);
    delay_us(JACKPOT_DISPLAY_BIT_DELAY_US);
}

/**
 * @brief 彩金显示板通信：执行内部处理（对应符号 JackpotDisplay_LatchPulse）。
 * @调用关系 直接调用者：jackpot_display.c:JackpotDisplay_SendFrame。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void JackpotDisplay_LatchPulse(void)
{
    JackpotDisplay_Write(JACKPOT_DISPLAY_LATCH_PORT,
                         JACKPOT_DISPLAY_LATCH_PIN, 0U);
    delay_us(JACKPOT_DISPLAY_BIT_DELAY_US);
    JackpotDisplay_Write(JACKPOT_DISPLAY_LATCH_PORT,
                         JACKPOT_DISPLAY_LATCH_PIN, 1U);
    delay_us(JACKPOT_DISPLAY_BIT_DELAY_US);
    JackpotDisplay_Write(JACKPOT_DISPLAY_LATCH_PORT,
                         JACKPOT_DISPLAY_LATCH_PIN, 0U);
    delay_us(JACKPOT_DISPLAY_BIT_DELAY_US);
}

/* 发送顺序与现有 segment.c 一致：高下标字节先发，每字节 bit7 先发。 */
/**
 * @brief 彩金显示板通信：编码并发送（对应符号 JackpotDisplay_SendFrame）。
 * @调用关系 直接调用者：jackpot_display.c:JackpotDisplay_Init、jackpot_display.c:JackpotDisplay_Task。
 * @局部状态 主要临时变量：byte_index、bit_index、bit_value、tx_byte；均只在本次调用中有效。
 * @param frame 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void JackpotDisplay_SendFrame(const uint8_t *frame)
{
    uint8_t byte_index;
    uint8_t bit_index;
    uint8_t bit_value;
    uint8_t tx_byte;

    JackpotDisplay_Write(JACKPOT_DISPLAY_CLK_PORT,
                         JACKPOT_DISPLAY_CLK_PIN, 0U);
    JackpotDisplay_Write(JACKPOT_DISPLAY_LATCH_PORT,
                         JACKPOT_DISPLAY_LATCH_PIN, 0U);

    for (byte_index = 0U;
         byte_index < JACKPOT_DISPLAY_4094_COUNT;
         byte_index++)
    {
        tx_byte = frame[(JACKPOT_DISPLAY_4094_COUNT - 1U) - byte_index];
#if JACKPOT_DISPLAY_OUTPUT_ACTIVE_LOW
        /* 彩金的4094输出0有效，仅反转待锁存数据，不改变C/L/D时序。 */
        tx_byte = (uint8_t)(~tx_byte);
#endif

        for (bit_index = 0U; bit_index < 8U; bit_index++)
        {
            bit_value = (((uint8_t)(tx_byte << bit_index) & 0x80U) != 0U) ?
                        1U : 0U;

            if (bit_value != 0U)
            {
                JackpotDisplay_Write(JACKPOT_DISPLAY_DATA_PORT,
                                     JACKPOT_DISPLAY_DATA_PIN, 1U);
            }
            else
            {
                JackpotDisplay_Write(JACKPOT_DISPLAY_DATA_PORT,
                                     JACKPOT_DISPLAY_DATA_PIN, 0U);
            }

            delay_us(JACKPOT_DISPLAY_BIT_DELAY_US);
            JackpotDisplay_ClockPulse();
        }
    }

    JackpotDisplay_LatchPulse();
}

/**
 * @brief 彩金显示板通信：构造（对应符号 JackpotDisplay_BuildFrame）。
 * @调用关系 直接调用者：jackpot_display.c:JackpotDisplay_SetValue、jackpot_display.c:JackpotDisplay_SetDemo。
 * @局部状态 主要临时变量：i、work_value；均只在本次调用中有效。
 * @param frame 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void JackpotDisplay_BuildFrame(uint8_t *frame, uint16_t value)
{
    uint8_t i;
    uint16_t work_value;

    work_value = (value > JACKPOT_DISPLAY_MAX_VALUE) ?
                 JACKPOT_DISPLAY_MAX_VALUE : value;

    for (i = 0U; i < JACKPOT_DISPLAY_4094_COUNT; i++)
    {
        frame[i] = 0U;
    }

    if (work_value == 0U)
    {
        frame[JACKPOT_DISPLAY_4094_COUNT - 1U] = s_digit_code[0];
        return;
    }

    for (i = 0U;
         (i < JACKPOT_DISPLAY_4094_COUNT) && (work_value != 0U);
         i++)
    {
        frame[(JACKPOT_DISPLAY_4094_COUNT - 1U) - i] =
            s_digit_code[work_value % 10U];
        work_value = (uint16_t)(work_value / 10U);
    }
}

/**
 * @brief 彩金显示板通信：初始化（对应符号 JackpotDisplay_EarlySafeInit）。
 * @调用关系 直接调用者：gd32f407.c:GD32F407_Init。
 * @局部状态 主要临时变量：gpio、data_pins；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void JackpotDisplay_EarlySafeInit(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint16_t data_pins;

    data_pins = JACKPOT_DISPLAY_LATCH_PIN | JACKPOT_DISPLAY_DATA_PIN;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();

    HAL_GPIO_WritePin(JACKPOT_DISPLAY_CLK_PORT,
                      JACKPOT_DISPLAY_CLK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, data_pins, GPIO_PIN_RESET);

    gpio.Pin = JACKPOT_DISPLAY_CLK_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(JACKPOT_DISPLAY_CLK_PORT, &gpio);

    gpio.Pin = data_pins;
    HAL_GPIO_Init(GPIOD, &gpio);

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLUP;
    gpio.Pin = JACKPOT_DISPLAY_CLK_PIN;
    HAL_GPIO_Init(JACKPOT_DISPLAY_CLK_PORT, &gpio);
    gpio.Pin = data_pins;
    HAL_GPIO_Init(GPIOD, &gpio);

    HAL_GPIO_WritePin(JACKPOT_DISPLAY_CLK_PORT,
                      JACKPOT_DISPLAY_CLK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, data_pins, GPIO_PIN_RESET);
}

/**
 * @brief 彩金显示板通信：初始化（对应符号 JackpotDisplay_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：gpio、clear_count、data_pins；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void JackpotDisplay_Init(void)
{
    GPIO_InitTypeDef gpio = {0};
    uint8_t clear_count;
    uint16_t data_pins;

    data_pins = JACKPOT_DISPLAY_LATCH_PIN | JACKPOT_DISPLAY_DATA_PIN;
    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    HAL_GPIO_WritePin(JACKPOT_DISPLAY_CLK_PORT,
                      JACKPOT_DISPLAY_CLK_PIN, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(GPIOD, data_pins, GPIO_PIN_RESET);

    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    gpio.Pin = JACKPOT_DISPLAY_CLK_PIN;
    HAL_GPIO_Init(JACKPOT_DISPLAY_CLK_PORT, &gpio);
    gpio.Pin = data_pins;
    HAL_GPIO_Init(GPIOD, &gpio);

    for (clear_count = 0U;
         clear_count < JACKPOT_DISPLAY_CLEAR_FRAMES;
         clear_count++)
    {
        JackpotDisplay_SendFrame(s_blank_frame);
        delay_ms(JACKPOT_DISPLAY_CLEAR_GAP_MS);
    }

    s_value = 0xFFFFU;
    s_dirty = 0U;
    s_last_refresh_ms = Gtim_NowMs();
    s_blink_enabled = 0U;
    s_demo_active = 0U;
    s_demo_blink = 0U;
    s_demo_value = 0U;
    s_blink_visible = 1U;
    s_blink_tick_ms = s_last_refresh_ms;
    s_ready = 1U;
}

/**
 * @brief 彩金显示板通信：设置（对应符号 JackpotDisplay_SetValue）。
 * @调用关系 直接调用者：app_main.c:App_Init、app_main.c:App_Loop、standby.c:Standby_Stop。
 * @局部状态 主要临时变量：limited_value；均只在本次调用中有效。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void JackpotDisplay_SetValue(uint16_t value)
{
    uint16_t limited_value;

    limited_value = (value > JACKPOT_DISPLAY_MAX_VALUE) ?
                    JACKPOT_DISPLAY_MAX_VALUE : value;

    if (limited_value == s_value)
    {
        return;
    }

    JackpotDisplay_BuildFrame(s_frame, limited_value);
    s_value = limited_value;
    s_dirty = 1U;
}

/**
 * @brief 彩金显示板通信：设置（对应符号 JackpotDisplay_SetBlink）。
 * @调用关系 直接调用者：game.c:Game_StartJackpotLinePresentation、game.c:Game_StopJackpotLinePresentation、game.c:Game_Init、game.c:Game_Task。
 * @局部状态 主要临时变量：requested；均只在本次调用中有效。
 * @param enable 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void JackpotDisplay_SetBlink(uint8_t enable)
{
    uint8_t requested;

    requested = (enable != 0U) ? 1U : 0U;
    if (requested == s_blink_enabled)
    {
        return;
    }

    s_blink_enabled = requested;
    s_blink_visible = 1U;
    s_blink_tick_ms = Gtim_NowMs();
    s_dirty = 1U;
}

/**
 * @brief 彩金显示板通信：周期处理（对应符号 JackpotDisplay_Task）。
 * @调用关系 直接调用者：app_main.c:App_Init、app_main.c:App_Loop、jackpot_display.c:JackpotDisplay_EndDemo。
 * @局部状态 主要临时变量：now_ms、output_frame、blink_enabled；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void JackpotDisplay_Task(void)
{
    uint32_t now_ms;
    const uint8_t *output_frame;
    uint8_t blink_enabled;

    if (s_ready == 0U)
    {
        return;
    }

    now_ms = Gtim_NowMs();
    blink_enabled = (s_demo_active != 0U) ? s_demo_blink : s_blink_enabled;
    if ((blink_enabled != 0U) &&
        ((uint32_t)(now_ms - s_blink_tick_ms) >=
         JACKPOT_DISPLAY_BLINK_MS))
    {
        s_blink_tick_ms = now_ms;
        s_blink_visible = (s_blink_visible == 0U) ? 1U : 0U;
        s_dirty = 1U;
    }

    if ((s_dirty == 0U) &&
        ((uint32_t)(now_ms - s_last_refresh_ms) <
         JACKPOT_DISPLAY_REFRESH_MS))
    {
        return;
    }

    output_frame = ((blink_enabled != 0U) &&
                    (s_blink_visible == 0U)) ?
                   s_blank_frame : s_frame;
    if ((s_demo_active != 0U) &&
        ((blink_enabled == 0U) || (s_blink_visible != 0U)))
    {
        output_frame = s_demo_frame;
    }
    JackpotDisplay_SendFrame(output_frame);
    s_dirty = 0U;
    s_last_refresh_ms = now_ms;
}

/**
 * @brief 彩金显示板通信：设置（对应符号 JackpotDisplay_SetDemo）。
 * @调用关系 直接调用者：standby.c:Standby_BeginPart、standby.c:Standby_BeginLineAward、standby.c:Standby_BeginJackpotAward、standby.c:Standby_Task。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @param blink 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void JackpotDisplay_SetDemo(uint16_t value, uint8_t blink)
{
    value = (value > JACKPOT_DISPLAY_MAX_VALUE) ?
             JACKPOT_DISPLAY_MAX_VALUE : value;
    blink = (blink != 0U) ? 1U : 0U;
    if ((s_demo_active != 0U) &&
        (s_demo_value == value) && (s_demo_blink == blink))
    {
        return;
    }
    if ((s_demo_active == 0U) || (s_demo_blink != blink))
    {
        s_blink_visible = 1U;
        s_blink_tick_ms = Gtim_NowMs();
    }
    s_demo_active = 1U;
    s_demo_value = value;
    s_demo_blink = blink;
    JackpotDisplay_BuildFrame(s_demo_frame, value);
    s_dirty = 1U;
}

/**
 * @brief 彩金显示板通信：执行内部处理（对应符号 JackpotDisplay_EndDemo）。
 * @调用关系 直接调用者：standby.c:Standby_Stop。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void JackpotDisplay_EndDemo(void)
{
    if (s_demo_active == 0U)
    {
        return;
    }
    s_demo_active = 0U;
    s_blink_visible = 1U;
    s_blink_tick_ms = Gtim_NowMs();
    s_dirty = 1U;
    /* Reveal the latest real value, including changes received during demo. */
    JackpotDisplay_Task();
}

/**
 * @brief 彩金显示板通信：清除（对应符号 JackpotDisplay_Clear）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void JackpotDisplay_Clear(void)
{
    uint8_t i;

    for (i = 0U; i < JACKPOT_DISPLAY_4094_COUNT; i++)
    {
        s_frame[i] = 0U;
    }

    s_value = 0xFFFFU;
    s_dirty = 1U;
}
