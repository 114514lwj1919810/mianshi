/* AUTO-FILE-DOC
 * 文件职责：按键扫描与事件队列。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef KEY_H
#define KEY_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define KEY_EXCHANGE_PORT              GPIOE
#define KEY_EXCHANGE_PIN               GPIO_PIN_7
#define KEY_TICKET_PORT                GPIOG
#define KEY_TICKET_PIN                 GPIO_PIN_1
#define KEY_GAME_PORT                  GPIOG
#define KEY_GAME_PIN                   GPIO_PIN_0
#define KEY_SET_PORT                   GPIOF
#define KEY_SET_PIN                    GPIO_PIN_14
#define KEY_ACCOUNT_PORT               GPIOF
#define KEY_ACCOUNT_PIN                GPIO_PIN_13
#define KEY_UP_PORT                    GPIOF
#define KEY_UP_PIN                     GPIO_PIN_12
#define KEY_DOWN_PORT                  GPIOF
#define KEY_DOWN_PIN                   GPIO_PIN_11
#define KEY_LEFT_PORT                  GPIOB
#define KEY_LEFT_PIN                   GPIO_PIN_2
#define KEY_RIGHT_PORT                 GPIOB
#define KEY_RIGHT_PIN                  GPIO_PIN_1
#define KEY_CLEAR_PORT                 GPIOE
#define KEY_CLEAR_PIN                  GPIO_PIN_2

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define KEY_LAMP_EXCHANGE_PORT         GPIOD
#define KEY_LAMP_EXCHANGE_PIN          GPIO_PIN_11
#define KEY_LAMP_TICKET_PORT           GPIOD
#define KEY_LAMP_TICKET_PIN            GPIO_PIN_12
#define KEY_LAMP_GAME_PORT             GPIOD
#define KEY_LAMP_GAME_PIN              GPIO_PIN_13
#define KEY_LAMP_LOTTERY_PORT          GPIOD
#define KEY_LAMP_LOTTERY_PIN           GPIO_PIN_14
#define KEY_LAMP_ON_LEVEL              GPIO_PIN_SET
#define KEY_LAMP_OFF_LEVEL             GPIO_PIN_RESET
#define KEY_LAMP_BLINK_HALF_MS         500U

#define KEY_DEBOUNCE_TICKS             3U

typedef enum
{
    KEY_EXCHANGE = 0U,
    KEY_TICKET,
    KEY_GAME,
    KEY_SET,
    KEY_ACCOUNT,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_CLEAR,
    KEY_COUNT
} Key_Id;

typedef enum
{
    KEY_LAMP_EXCHANGE = 0U,
    KEY_LAMP_TICKET,
    KEY_LAMP_GAME,
    KEY_LAMP_LOTTERY,
    KEY_LAMP_COUNT
} Key_Lamp;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Key_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Key_Scan10ms(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Key_IsPressed(Key_Id key);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Key_TakePress(Key_Id key);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Key_ClearEvents(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Key_SetLamp(Key_Lamp lamp, uint8_t on);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Key_UpdateLampBlink(uint8_t game_blink_enable,
                         uint8_t ticket_blink_enable,
                         uint8_t lottery_blink_enable);


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Key_Test1(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Key_Test2(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Key_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
