#ifndef STM32H7XX_HAL_H
#define STM32H7XX_HAL_H

#include <stdint.h>

typedef struct
{
    uint32_t unused;
} GPIO_TypeDef;

typedef enum
{
    GPIO_PIN_RESET = 0,
    GPIO_PIN_SET
} GPIO_PinState;

typedef struct
{
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
} GPIO_InitTypeDef;

extern GPIO_TypeDef g_test_gpiod;

#define GPIOD                   (&g_test_gpiod)
#define GPIO_PIN_0              0x0001U
#define GPIO_PIN_1              0x0002U
#define GPIO_PIN_2              0x0004U
#define GPIO_MODE_INPUT         0U
#define GPIO_MODE_OUTPUT_PP     1U
#define GPIO_NOPULL             0U
#define GPIO_PULLUP             1U
#define GPIO_PULLDOWN           2U
#define GPIO_SPEED_FREQ_LOW     0U
#define GPIO_SPEED_FREQ_HIGH    3U
#define __HAL_RCC_GPIOD_CLK_ENABLE() ((void)0)

void HAL_GPIO_WritePin(GPIO_TypeDef *port,
                       uint16_t pin,
                       GPIO_PinState state);
void HAL_GPIO_Init(GPIO_TypeDef *port, GPIO_InitTypeDef *gpio);
uint32_t HAL_GetTick(void);

#endif
