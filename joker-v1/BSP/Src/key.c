/* AUTO-FILE-DOC
 * 文件职责：按键扫描与事件队列。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "key.h"

#include "gtim.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} Key_Pin;

typedef struct
{
    uint8_t sample_pressed;
    uint8_t stable_pressed;
    uint8_t stable_ticks;
    uint8_t press_event;
} Key_Runtime;

static const Key_Pin s_pin[KEY_COUNT] =
{
    {KEY_EXCHANGE_PORT, KEY_EXCHANGE_PIN},
    {KEY_TICKET_PORT, KEY_TICKET_PIN},
    {KEY_GAME_PORT, KEY_GAME_PIN},
    {KEY_SET_PORT, KEY_SET_PIN},
    {KEY_ACCOUNT_PORT, KEY_ACCOUNT_PIN},
    {KEY_UP_PORT, KEY_UP_PIN},
    {KEY_DOWN_PORT, KEY_DOWN_PIN},
    {KEY_LEFT_PORT, KEY_LEFT_PIN},
    {KEY_RIGHT_PORT, KEY_RIGHT_PIN},
    {KEY_CLEAR_PORT, KEY_CLEAR_PIN}
};

static const Key_Pin s_lamp_pin[KEY_LAMP_COUNT] =
{
    {KEY_LAMP_EXCHANGE_PORT, KEY_LAMP_EXCHANGE_PIN},
    {KEY_LAMP_TICKET_PORT, KEY_LAMP_TICKET_PIN},
    {KEY_LAMP_GAME_PORT, KEY_LAMP_GAME_PIN},
    {KEY_LAMP_LOTTERY_PORT, KEY_LAMP_LOTTERY_PIN}
};

static Key_Runtime s_key[KEY_COUNT];

/**
 * @brief 按键扫描与事件队列：执行内部处理（对应符号 Key_UsesReleaseEvent）。
 * @调用关系 直接调用者：key.c:Key_Scan10ms。
 * @param key 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Key_UsesReleaseEvent(Key_Id key)
{
    return ((key == KEY_SET) ||
            (key == KEY_ACCOUNT) ||
            (key == KEY_CLEAR)) ? 1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按键扫描与事件队列：读取（对应符号 Key_ReadRaw）。
 * @调用关系 直接调用者：key.c:Key_Init、key.c:Key_Scan10ms。
 * @param key 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Key_ReadRaw(Key_Id key)
{
    if (key >= KEY_COUNT)
    {
        return 0U;
    }

    return (HAL_GPIO_ReadPin(s_pin[key].port, s_pin[key].pin) == GPIO_PIN_RESET) ? 1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按键扫描与事件队列：初始化（对应符号 Key_InitPort）。
 * @调用关系 直接调用者：key.c:Key_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param pins 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Key_InitPort(GPIO_TypeDef *port, uint16_t pins)
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
 * @brief 按键扫描与事件队列：初始化（对应符号 Key_InitLampOutputs）。
 * @调用关系 直接调用者：key.c:Key_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Key_InitLampOutputs(void)
{
    GPIO_InitTypeDef gpio = {0};

    HAL_GPIO_WritePin(GPIOD,
                      KEY_LAMP_EXCHANGE_PIN | KEY_LAMP_TICKET_PIN |
                      KEY_LAMP_GAME_PIN | KEY_LAMP_LOTTERY_PIN,
                      KEY_LAMP_OFF_LEVEL);

    gpio.Pin = KEY_LAMP_EXCHANGE_PIN | KEY_LAMP_TICKET_PIN |
               KEY_LAMP_GAME_PIN | KEY_LAMP_LOTTERY_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_PULLDOWN;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOD, &gpio);

    HAL_GPIO_WritePin(GPIOD,
                      KEY_LAMP_EXCHANGE_PIN | KEY_LAMP_TICKET_PIN |
                      KEY_LAMP_GAME_PIN | KEY_LAMP_LOTTERY_PIN,
                      KEY_LAMP_OFF_LEVEL);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按键扫描与事件队列：初始化（对应符号 Key_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：i、pressed；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Key_Init(void)
{
    uint8_t i;
    uint8_t pressed;

    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_GPIOD_CLK_ENABLE();
    __HAL_RCC_GPIOE_CLK_ENABLE();
    __HAL_RCC_GPIOF_CLK_ENABLE();
    __HAL_RCC_GPIOG_CLK_ENABLE();

    Key_InitLampOutputs();

    Key_InitPort(GPIOE, GPIO_PIN_2 | GPIO_PIN_7);
    Key_InitPort(GPIOF, GPIO_PIN_11 | GPIO_PIN_12 | GPIO_PIN_13 | GPIO_PIN_14);
    Key_InitPort(GPIOB, GPIO_PIN_1 | GPIO_PIN_2);
    Key_InitPort(GPIOG, GPIO_PIN_0 | GPIO_PIN_1);

    for (i = 0U; i < (uint8_t)KEY_COUNT; i++)
    {
        pressed = Key_ReadRaw((Key_Id)i);
        s_key[i].sample_pressed = pressed;
        s_key[i].stable_pressed = pressed;
        s_key[i].stable_ticks = KEY_DEBOUNCE_TICKS;
        s_key[i].press_event = 0U;
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按键扫描与事件队列：执行内部处理（对应符号 Key_Scan10ms）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：i、raw_pressed；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Key_Scan10ms(void)
{
    uint8_t i;
    uint8_t raw_pressed;

    for (i = 0U; i < (uint8_t)KEY_COUNT; i++)
    {
        raw_pressed = Key_ReadRaw((Key_Id)i);

        if (raw_pressed != s_key[i].sample_pressed)
        {
            s_key[i].sample_pressed = raw_pressed;
            s_key[i].stable_ticks = 0U;
            continue;
        }

        if (s_key[i].stable_ticks < KEY_DEBOUNCE_TICKS)
        {
            s_key[i].stable_ticks++;
        }

        if ((s_key[i].stable_ticks >= KEY_DEBOUNCE_TICKS) &&
            (s_key[i].stable_pressed != raw_pressed))
        {
            s_key[i].stable_pressed = raw_pressed;
            if (((Key_UsesReleaseEvent((Key_Id)i) == 0U) &&
                 (raw_pressed != 0U)) ||
                ((Key_UsesReleaseEvent((Key_Id)i) != 0U) &&
                 (raw_pressed == 0U)))
            {
                s_key[i].press_event = 1U;
            }
        }
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按键扫描与事件队列：执行内部处理（对应符号 Key_IsPressed）。
 * @调用关系 直接调用者：machine_lock.c:MachineLock_UpdateHold、machine_lock.c:MachineLock_UpdateDualHold、machine_lock.c:MachineLock_Task。
 * @param key 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Key_IsPressed(Key_Id key)
{
    if (key >= KEY_COUNT)
    {
        return 0U;
    }

    return s_key[key].stable_pressed;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按键扫描与事件队列：读取并清除一次性事件（对应符号 Key_TakePress）。
 * @调用关系 直接调用者：game.c:Game_UpdateLine7Settlement、game.c:Game_DiscardMenuPlayInputs、game.c:Game_TaskSettingMode、game.c:Game_TaskAccountMode、game.c:Game_Task、machine_lock.c:MachineLock_UpdateHold、machine_lock.c:MachineLock_UpdateDualHold、machine_lock.c:MachineLock_AdjustDigit。
 * @局部状态 主要临时变量：event、primask；均只在本次调用中有效。
 * @param key 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Key_TakePress(Key_Id key)
{
    uint8_t event;
    uint32_t primask;

    if (key >= KEY_COUNT)
    {
        return 0U;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    event = s_key[key].press_event;
    s_key[key].press_event = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }

    return event;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按键扫描与事件队列：清除（对应符号 Key_ClearEvents）。
 * @调用关系 直接调用者：key.c:Key_Test1、game.c:Game_Task。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Key_ClearEvents(void)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)KEY_COUNT; i++)
    {
        s_key[i].press_event = 0U;
    }
}


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按键扫描与事件队列：设置（对应符号 Key_SetLamp）。
 * @调用关系 直接调用者：key.c:Key_UpdateLampBlink、key.c:Key_Test2、standby.c:Standby_Stop、standby.c:Standby_UpdateKeyLamps。
 * @param lamp 按值传入；函数内部修改不会回写调用方变量。
 * @param on 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Key_SetLamp(Key_Lamp lamp, uint8_t on)
{
    if (lamp >= KEY_LAMP_COUNT)
    {
        return;
    }

    HAL_GPIO_WritePin(s_lamp_pin[lamp].port,
                      s_lamp_pin[lamp].pin,
                      (on != 0U) ? KEY_LAMP_ON_LEVEL : KEY_LAMP_OFF_LEVEL);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按键扫描与事件队列：更新（对应符号 Key_UpdateLampBlink）。
 * @调用关系 直接调用者：key.c:Key_Test3、game.c:Game_Task。
 * @局部状态 主要临时变量：blink_on；均只在本次调用中有效。
 * @param game_blink_enable 按值传入；函数内部修改不会回写调用方变量。
 * @param ticket_blink_enable 按值传入；函数内部修改不会回写调用方变量。
 * @param lottery_blink_enable 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Key_UpdateLampBlink(uint8_t game_blink_enable,
                         uint8_t ticket_blink_enable,
                         uint8_t lottery_blink_enable)
{
    uint8_t blink_on;

    blink_on = (((Gtim_NowMs() / KEY_LAMP_BLINK_HALF_MS) & 1U) == 0U) ? 1U : 0U;

    Key_SetLamp(KEY_LAMP_GAME, ((game_blink_enable != 0U) && (blink_on != 0U)) ? 1U : 0U);
    Key_SetLamp(KEY_LAMP_TICKET, ((ticket_blink_enable != 0U) && (blink_on != 0U)) ? 1U : 0U);
    Key_SetLamp(KEY_LAMP_LOTTERY, ((lottery_blink_enable != 0U) && (blink_on != 0U)) ? 1U : 0U);
}

/**
 * @brief 按键扫描与事件队列：执行内部处理（对应符号 Key_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Key_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Key_ClearEvents();
}

/**
 * @brief 按键扫描与事件队列：执行内部处理（对应符号 Key_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Key_Test2(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Key_SetLamp(KEY_LAMP_GAME, 1U);
    Key_SetLamp(KEY_LAMP_TICKET, 0U);
}

/**
 * @brief 按键扫描与事件队列：执行内部处理（对应符号 Key_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Key_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Key_UpdateLampBlink(1U, 0U, 0U);
}
