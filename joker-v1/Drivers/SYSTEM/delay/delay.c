#include "./SYSTEM/delay/delay.h"

static uint32_t s_ticks_per_us;

void delay_init(uint16_t sysclk_mhz)
{
    s_ticks_per_us = sysclk_mhz;
}

void delay_us(uint32_t microseconds)
{
    uint32_t reload = SysTick->LOAD + 1U;
    uint32_t previous = SysTick->VAL;
    uint64_t target = (uint64_t)microseconds * s_ticks_per_us;
    uint64_t elapsed_ticks = 0U;

    while (elapsed_ticks < target) {
        uint32_t current = SysTick->VAL;
        if (current <= previous) {
            elapsed_ticks += (uint64_t)(previous - current);
        } else {
            elapsed_ticks += (uint64_t)(previous + reload - current);
        }
        previous = current;
    }
}

void delay_ms(uint16_t milliseconds)
{
    HAL_Delay(milliseconds);
}
