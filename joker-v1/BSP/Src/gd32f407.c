/* AUTO-FILE-DOC
 * 文件职责：gd32f407 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "gd32f407.h"

#include "jackpot_display.h"
#include "motor.h"
#include "segment.h"
#include "solenoid.h"
#include "wiper_motor.h"
#include "./SYSTEM/delay/delay.h"

/**
 * @brief gd32f407 模块：初始化（对应符号 GD32F407_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void GD32F407_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    HAL_Init();
    Solenoid_EarlySafeInit();
    Motor_EarlySafeInit();
    WiperMotor_EarlySafeInit();
    Segment_EarlySafeInit();
    JackpotDisplay_EarlySafeInit();

    delay_init((uint16_t)(SystemCoreClock / 1000000U));

    __HAL_RCC_GPIOC_CLK_ENABLE();
    HAL_GPIO_WritePin(GD32F407_HEARTBEAT_PORT,
                      GD32F407_HEARTBEAT_PIN,
                      GPIO_PIN_RESET);
    gpio.Pin = GD32F407_HEARTBEAT_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GD32F407_HEARTBEAT_PORT, &gpio);
}

/**
 * @brief gd32f407 模块：设置（对应符号 GD32F407_HeartbeatSet）。
 * @调用关系 直接调用者：gd32f407.c:GD32F407_Test1、gd32f407.c:GD32F407_Test2、remote_config.c:RemoteConfig_StatusLedTask。
 * @param high_level 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void GD32F407_HeartbeatSet(uint8_t high_level)
{
    HAL_GPIO_WritePin(GD32F407_HEARTBEAT_PORT, GD32F407_HEARTBEAT_PIN,
                      (high_level != 0U) ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/**
 * @brief gd32f407 模块：执行内部处理（对应符号 GD32F407_HeartbeatToggle）。
 * @调用关系 直接调用者：gd32f407.c:GD32F407_Test3、remote_config.c:RemoteConfig_StatusLedTask。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void GD32F407_HeartbeatToggle(void)
{
    HAL_GPIO_TogglePin(GD32F407_HEARTBEAT_PORT, GD32F407_HEARTBEAT_PIN);
}

/**
 * @brief gd32f407 模块：执行内部处理（对应符号 GD32F407_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void GD32F407_Test1(void) { GD32F407_HeartbeatSet(1U); }
/**
 * @brief gd32f407 模块：执行内部处理（对应符号 GD32F407_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void GD32F407_Test2(void) { GD32F407_HeartbeatSet(0U); }
/**
 * @brief gd32f407 模块：执行内部处理（对应符号 GD32F407_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void GD32F407_Test3(void) { GD32F407_HeartbeatToggle(); }
