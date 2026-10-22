/* AUTO-FILE-DOC
 * 文件职责：gd32f407_hal 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "gd32f407_hal.h"

static volatile uint32_t s_hal_tick;
static uint32_t s_nvic_group = NVIC_PRIGROUP_PRE2_SUB2;
static uint8_t s_nvic_preemption[128];
static uint8_t s_nvic_subpriority[128];

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 gpio_port_value）。
 * @调用关系 直接调用者：gd32f407_hal.c:gpio_exti_source、gd32f407_hal.c:HAL_GPIO_Init、gd32f407_hal.c:HAL_GPIO_WritePin、gd32f407_hal.c:HAL_GPIO_ReadPin、gd32f407_hal.c:HAL_GPIO_TogglePin。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t gpio_port_value(GPIO_TypeDef *port)
{
    return (uint32_t)port;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 gpio_exti_source）。
 * @调用关系 直接调用者：gd32f407_hal.c:HAL_GPIO_Init。
 * @局部状态 主要临时变量：value；均只在本次调用中有效。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t gpio_exti_source(GPIO_TypeDef *port)
{
    uint32_t value = gpio_port_value(port);
    return (uint8_t)((value - GPIO_BASE) / 0x400U);
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 pin_index）。
 * @调用关系 直接调用者：gd32f407_hal.c:HAL_GPIO_Init。
 * @局部状态 主要临时变量：index；均只在本次调用中有效。
 * @param pin 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t pin_index(uint32_t pin)
{
    uint8_t index = 0U;
    while (((pin >> index) & 1U) == 0U) {
        index++;
    }
    return index;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 elapsed）。
 * @调用关系 直接调用者：gd32f407_hal.c:HAL_UART_Transmit、gd32f407_hal.c:HAL_SPI_TransmitReceive。
 * @param start 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t elapsed(uint32_t start, uint32_t timeout_ms)
{
    if (timeout_ms == HAL_MAX_DELAY) {
        return 0U;
    }
    return ((uint32_t)(HAL_GetTick() - start) >= timeout_ms) ? 1U : 0U;
}

/**
 * @brief gd32f407_hal 模块：初始化（对应符号 HAL_Init）。
 * @调用关系 直接调用者：gd32f407.c:GD32F407_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_Init(void)
{
    s_hal_tick = 0U;
    SystemCoreClockUpdate();
    (void)SysTick_Config(SystemCoreClock / 1000U);
    NVIC_SetPriority(SysTick_IRQn, 0x0FU);
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 HAL_IncTick）。
 * @调用关系 直接调用者：gd32f4xx_it.c:SysTick_Handler。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_IncTick(void)
{
    s_hal_tick++;
}

/**
 * @brief gd32f407_hal 模块：读取（对应符号 HAL_GetTick）。
 * @调用关系 直接调用者：app_main.c:App_Loop、coin.c:Coin_MarkExtiFromIrq、gd32f407_hal.c:elapsed、gd32f407_hal.c:HAL_Delay、gd32f407_hal.c:HAL_UART_Transmit、gd32f407_hal.c:HAL_SPI_TransmitReceive、nrf24l01.c:Delay_DeadlineReached、nrf24l01.c:NRF24L01_ConfigureRx。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint32_t HAL_GetTick(void)
{
    return s_hal_tick;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 HAL_Delay）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：start；均只在本次调用中有效。
 * @param delay_ms 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_Delay(uint32_t delay_ms)
{
    uint32_t start = HAL_GetTick();
    while ((uint32_t)(HAL_GetTick() - start) < delay_ms) {
    }
}

/**
 * @brief gd32f407_hal 模块：初始化（对应符号 HAL_GPIO_Init）。
 * @调用关系 直接调用者：segment.c:Segment_EarlySafeInit、segment.c:Segment_Init、award_lamp.c:AwardLamp_Init、coin.c:Coin_Init、eye.c:Eye_InitInput、eye.c:Eye_InitExti、eye.c:Eye_InitExtiBothEdges、gd32f407.c:GD32F407_Init。
 * @局部状态 主要临时变量：gd_port、pin、mode、output_type、index；均只在本次调用中有效。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param init 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *init)
{
    uint32_t gd_port;
    uint32_t pin;
    uint32_t mode;
    uint32_t output_type = GPIO_OTYPE_PP;

    if ((port == 0) || (init == 0) || (init->Pin == 0U)) {
        return;
    }

    gd_port = gpio_port_value(port);
    mode = GPIO_MODE_INPUT;
    if ((init->Mode == GPIO_MODE_OUTPUT_PP) || (init->Mode == GPIO_MODE_OUTPUT_OD)) {
        mode = GPIO_MODE_OUTPUT;
    } else if ((init->Mode == GPIO_MODE_AF_PP) || (init->Mode == GPIO_MODE_AF_OD)) {
        mode = GPIO_MODE_AF;
        gpio_af_set(gd_port, (uint32_t)init->Alternate, init->Pin);
    }

    if ((init->Mode == GPIO_MODE_OUTPUT_OD) || (init->Mode == GPIO_MODE_AF_OD)) {
        output_type = GPIO_OTYPE_OD;
    }

    gpio_mode_set(gd_port, mode, init->Pull, init->Pin);
    if ((mode == GPIO_MODE_OUTPUT) || (mode == GPIO_MODE_AF)) {
        gpio_output_options_set(gd_port, output_type, init->Speed, init->Pin);
    }

    if ((init->Mode == GPIO_MODE_IT_FALLING) ||
        (init->Mode == GPIO_MODE_IT_RISING_FALLING)) {
        rcu_periph_clock_enable(RCU_SYSCFG);
        for (pin = GPIO_PIN_0; pin <= GPIO_PIN_15; pin <<= 1U) {
            if ((init->Pin & pin) != 0U) {
                uint8_t index = pin_index(pin);
                syscfg_exti_line_config(gpio_exti_source(port), index);
                exti_init((exti_line_enum)pin, EXTI_INTERRUPT,
                          (init->Mode == GPIO_MODE_IT_FALLING) ?
                          EXTI_TRIG_FALLING : EXTI_TRIG_BOTH);
                exti_interrupt_flag_clear((exti_line_enum)pin);
            }
        }
    }
}

/**
 * @brief gd32f407_hal 模块：写入（对应符号 HAL_GPIO_WritePin）。
 * @调用关系 直接调用者：segment.c:Segment_EarlySafeInit、segment.c:Segment_LineLow、segment.c:Segment_LineHigh、segment.c:Segment_Init、award_lamp.c:AwardLamp_Write、gd32f407.c:GD32F407_Init、gd32f407.c:GD32F407_HeartbeatSet、iic.c:Iic_RecoverBusGpio。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param pins 按值传入；函数内部修改不会回写调用方变量。
 * @param state 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pins, GPIO_PinState state)
{
    if (state == GPIO_PIN_SET) {
        gpio_bit_set(gpio_port_value(port), pins);
    } else {
        gpio_bit_reset(gpio_port_value(port), pins);
    }
}

/**
 * @brief gd32f407_hal 模块：读取（对应符号 HAL_GPIO_ReadPin）。
 * @调用关系 直接调用者：coin.c:Coin_IsActive、eye.c:Eye_ReadChannel、eye.c:Eye_MarkCounterFromIrq、eye.c:Eye_IsCounterActive、key.c:Key_ReadRaw、machine_address.c:MachineAddress_ReadDip、nrf24l01.c:NRF24L01_R_IRQ、shake.c:Shake_IsActive。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param pin 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
    return (gpio_input_bit_get(gpio_port_value(port), pin) == SET) ?
           GPIO_PIN_SET : GPIO_PIN_RESET;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 HAL_GPIO_TogglePin）。
 * @调用关系 直接调用者：gd32f407.c:GD32F407_HeartbeatToggle。
 * @param port 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param pins 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pins)
{
    gpio_bit_toggle(gpio_port_value(port), pins);
}

/**
 * @brief gd32f407_hal 模块：设置（对应符号 HAL_NVIC_SetPriorityGrouping）。
 * @调用关系 直接调用者：interrupt.c:Interrupt_Init。
 * @param group 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_NVIC_SetPriorityGrouping(uint32_t group)
{
    s_nvic_group = group;
    nvic_priority_group_set(group);
}

/**
 * @brief gd32f407_hal 模块：设置（对应符号 HAL_NVIC_SetPriority）。
 * @调用关系 直接调用者：interrupt.c:Interrupt_Init。
 * @局部状态 主要临时变量：index；均只在本次调用中有效。
 * @param irqn 按值传入；函数内部修改不会回写调用方变量。
 * @param preemption 按值传入；函数内部修改不会回写调用方变量。
 * @param subpriority 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_NVIC_SetPriority(IRQn_Type irqn, uint32_t preemption, uint32_t subpriority)
{
    uint32_t index = (uint32_t)irqn;
    if (index < sizeof(s_nvic_preemption)) {
        s_nvic_preemption[index] = (uint8_t)preemption;
        s_nvic_subpriority[index] = (uint8_t)subpriority;
    }
    (void)s_nvic_group;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 HAL_NVIC_EnableIRQ）。
 * @调用关系 直接调用者：interrupt.c:Interrupt_Init。
 * @局部状态 主要临时变量：index、preemption、subpriority；均只在本次调用中有效。
 * @param irqn 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_NVIC_EnableIRQ(IRQn_Type irqn)
{
    uint32_t index = (uint32_t)irqn;
    uint8_t preemption = 0U;
    uint8_t subpriority = 0U;
    if (index < sizeof(s_nvic_preemption)) {
        preemption = s_nvic_preemption[index];
        subpriority = s_nvic_subpriority[index];
    }
    nvic_irq_enable(irqn, preemption, subpriority);
}

/**
 * @brief gd32f407_hal 模块：清除（对应符号 HAL_NVIC_ClearPendingIRQ）。
 * @调用关系 直接调用者：eye.c:Eye_ClearCounter。
 * @param irqn 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irqn)
{
    NVIC_ClearPendingIRQ(irqn);
}

/**
 * @brief gd32f407_hal 模块：初始化（对应符号 HAL_TIM_Base_Init）。
 * @调用关系 直接调用者：gtim.c:Gtim_Init。
 * @param htim 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim)
{
    timer_parameter_struct cfg;
    if (htim == 0) {
        return HAL_ERROR;
    }
    timer_deinit(htim->Instance);
    timer_struct_para_init(&cfg);
    cfg.prescaler = (uint16_t)htim->Init.Prescaler;
    cfg.alignedmode = TIMER_COUNTER_EDGE;
    cfg.counterdirection = htim->Init.CounterMode;
    cfg.period = htim->Init.Period;
    cfg.clockdivision = htim->Init.ClockDivision;
    cfg.repetitioncounter = 0U;
    timer_init(htim->Instance, &cfg);
    timer_interrupt_flag_clear(htim->Instance, TIMER_INT_FLAG_UP);
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：启动（对应符号 HAL_TIM_Base_Start_IT）。
 * @调用关系 直接调用者：gtim.c:Gtim_Start。
 * @param htim 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim)
{
    if (htim == 0) {
        return HAL_ERROR;
    }
    timer_interrupt_flag_clear(htim->Instance, TIMER_INT_FLAG_UP);
    timer_interrupt_enable(htim->Instance, TIMER_INT_UP);
    timer_enable(htim->Instance);
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：停止（对应符号 HAL_TIM_Base_Stop_IT）。
 * @调用关系 直接调用者：gtim.c:Gtim_Stop。
 * @param htim 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim)
{
    if (htim == 0) {
        return HAL_ERROR;
    }
    timer_interrupt_disable(htim->Instance, TIMER_INT_UP);
    timer_disable(htim->Instance);
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：初始化（对应符号 HAL_UART_Init）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param huart 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart)
{
    if (huart == 0) {
        return HAL_ERROR;
    }
    HAL_UART_MspInit(huart);
    usart_deinit(huart->Instance);
    usart_baudrate_set(huart->Instance, huart->Init.BaudRate);
    usart_word_length_set(huart->Instance, huart->Init.WordLength);
    usart_stop_bit_set(huart->Instance, huart->Init.StopBits);
    usart_parity_config(huart->Instance, huart->Init.Parity);
    usart_hardware_flow_rts_config(huart->Instance, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(huart->Instance, USART_CTS_DISABLE);
    usart_receive_config(huart->Instance, USART_RECEIVE_ENABLE);
    usart_transmit_config(huart->Instance, USART_TRANSMIT_ENABLE);
    usart_enable(huart->Instance);
    huart->rx_data = 0;
    huart->tx_data = 0;
    huart->tx_busy = 0U;
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 HAL_UART_Transmit）。
 * @调用关系 直接调用者：audio_manager.c:Audio_SendNow。
 * @局部状态 主要临时变量：i、start；均只在本次调用中有效。
 * @param huart 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *data,
                                    uint16_t size, uint32_t timeout_ms)
{
    uint16_t i;
    uint32_t start;
    if ((huart == 0) || ((data == 0) && (size != 0U))) {
        return HAL_ERROR;
    }
    start = HAL_GetTick();
    for (i = 0U; i < size; i++) {
        while (usart_flag_get(huart->Instance, USART_FLAG_TBE) == RESET) {
            if (elapsed(start, timeout_ms) != 0U) {
                return HAL_TIMEOUT;
            }
        }
        usart_data_transmit(huart->Instance, data[i]);
    }
    while (usart_flag_get(huart->Instance, USART_FLAG_TC) == RESET) {
        if (elapsed(start, timeout_ms) != 0U) {
            return HAL_TIMEOUT;
        }
    }
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 HAL_UART_Transmit_IT）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param huart 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       const uint8_t *data, uint16_t size)
{
    if ((huart == 0) || (data == 0) || (size == 0U) || (huart->tx_busy != 0U)) {
        return HAL_BUSY;
    }
    huart->tx_data = data;
    huart->tx_size = size;
    huart->tx_count = 0U;
    huart->tx_busy = 1U;
    usart_interrupt_flag_clear(huart->Instance, USART_INT_FLAG_TC);
    usart_interrupt_enable(huart->Instance, USART_INT_TBE);
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：接收（对应符号 HAL_UART_Receive_IT）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param huart 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart,
                                      uint8_t *data, uint16_t size)
{
    if ((huart == 0) || (data == 0) || (size == 0U)) {
        return HAL_ERROR;
    }
    huart->rx_data = data;
    huart->rx_size = size;
    huart->rx_count = 0U;
    usart_interrupt_enable(huart->Instance, USART_INT_RBNE);
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：处理状态或事件（对应符号 HAL_UART_IRQHandler）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：instance；均只在本次调用中有效。
 * @param huart 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void HAL_UART_IRQHandler(UART_HandleTypeDef *huart)
{
    uint32_t instance;
    if (huart == 0) {
        return;
    }
    instance = huart->Instance;

    if ((usart_flag_get(instance, USART_FLAG_ORERR) == SET) ||
        (usart_flag_get(instance, USART_FLAG_NERR) == SET) ||
        (usart_flag_get(instance, USART_FLAG_FERR) == SET) ||
        (usart_flag_get(instance, USART_FLAG_PERR) == SET)) {
        (void)usart_data_receive(instance);
        HAL_UART_ErrorCallback(huart);
    }

    if ((huart->rx_data != 0) &&
        (usart_interrupt_flag_get(instance, USART_INT_FLAG_RBNE) == SET)) {
        huart->rx_data[huart->rx_count++] = (uint8_t)usart_data_receive(instance);
        if (huart->rx_count >= huart->rx_size) {
            usart_interrupt_disable(instance, USART_INT_RBNE);
            huart->rx_data = 0;
            HAL_UART_RxCpltCallback(huart);
        }
    }

    if ((huart->tx_busy != 0U) &&
        (usart_interrupt_flag_get(instance, USART_INT_FLAG_TBE) == SET)) {
        if (huart->tx_count < huart->tx_size) {
            usart_data_transmit(instance, huart->tx_data[huart->tx_count++]);
        } else {
            usart_interrupt_disable(instance, USART_INT_TBE);
            usart_interrupt_enable(instance, USART_INT_TC);
        }
    }

    if ((huart->tx_busy != 0U) &&
        (usart_interrupt_flag_get(instance, USART_INT_FLAG_TC) == SET)) {
        usart_interrupt_disable(instance, USART_INT_TC);
        usart_interrupt_flag_clear(instance, USART_INT_FLAG_TC);
        huart->tx_busy = 0U;
        huart->tx_data = 0;
        HAL_UART_TxCpltCallback(huart);
    }
}

/* I2C polling helpers, adapted from the verified GD32F407ZGT6 reference. */
#define I2C_WAIT_LIMIT 1000000UL

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 i2c_error）。
 * @调用关系 直接调用者：gd32f407_hal.c:i2c_wait。
 * @param i2c 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static int i2c_error(uint32_t i2c)
{
    return ((i2c_flag_get(i2c, I2C_FLAG_AERR) == SET) ||
            (i2c_flag_get(i2c, I2C_FLAG_BERR) == SET) ||
            (i2c_flag_get(i2c, I2C_FLAG_LOSTARB) == SET) ||
            (i2c_flag_get(i2c, I2C_FLAG_OUERR) == SET)) ? 1 : 0;
}

/**
 * @brief gd32f407_hal 模块：清除（对应符号 i2c_clear_errors）。
 * @调用关系 直接调用者：gd32f407_hal.c:i2c_prepare、gd32f407_hal.c:i2c_finish、gd32f407_hal.c:HAL_I2C_IsDeviceReady。
 * @param i2c 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void i2c_clear_errors(uint32_t i2c)
{
    i2c_flag_clear(i2c, I2C_FLAG_AERR);
    i2c_flag_clear(i2c, I2C_FLAG_BERR);
    i2c_flag_clear(i2c, I2C_FLAG_LOSTARB);
    i2c_flag_clear(i2c, I2C_FLAG_OUERR);
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 i2c_wait）。
 * @调用关系 直接调用者：gd32f407_hal.c:i2c_start_address、gd32f407_hal.c:i2c_transmit、gd32f407_hal.c:i2c_receive。
 * @局部状态 主要临时变量：limit；均只在本次调用中有效。
 * @param i2c 按值传入；函数内部修改不会回写调用方变量。
 * @param flag 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static int i2c_wait(uint32_t i2c, i2c_flag_enum flag)
{
    uint32_t limit = I2C_WAIT_LIMIT;
    while (i2c_flag_get(i2c, flag) == RESET) {
        if ((i2c_error(i2c) != 0) || (limit-- == 0U)) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 i2c_prepare）。
 * @调用关系 直接调用者：gd32f407_hal.c:HAL_I2C_IsDeviceReady、gd32f407_hal.c:HAL_I2C_Master_Transmit、gd32f407_hal.c:HAL_I2C_Master_Receive、gd32f407_hal.c:HAL_I2C_Mem_Write、gd32f407_hal.c:HAL_I2C_Mem_Read。
 * @局部状态 主要临时变量：limit；均只在本次调用中有效。
 * @param i2c 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static int i2c_prepare(uint32_t i2c)
{
    uint32_t limit = I2C_WAIT_LIMIT;
    i2c_ackpos_config(i2c, I2C_ACKPOS_CURRENT);
    i2c_ack_config(i2c, I2C_ACK_ENABLE);
    i2c_clear_errors(i2c);
    while (i2c_flag_get(i2c, I2C_FLAG_I2CBSY) == SET) {
        if (limit-- == 0U) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief gd32f407_hal 模块：启动（对应符号 i2c_start_address）。
 * @调用关系 直接调用者：gd32f407_hal.c:HAL_I2C_IsDeviceReady、gd32f407_hal.c:HAL_I2C_Master_Transmit、gd32f407_hal.c:HAL_I2C_Master_Receive、gd32f407_hal.c:HAL_I2C_Mem_Write、gd32f407_hal.c:HAL_I2C_Mem_Read。
 * @param i2c 按值传入；函数内部修改不会回写调用方变量。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param direction 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static int i2c_start_address(uint32_t i2c, uint8_t address, uint32_t direction)
{
    i2c_start_on_bus(i2c);
    if (i2c_wait(i2c, I2C_FLAG_SBSEND) != 0) {
        return -1;
    }
    i2c_master_addressing(i2c, (uint32_t)address << 1U, direction);
    return i2c_wait(i2c, I2C_FLAG_ADDSEND);
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 i2c_transmit）。
 * @调用关系 直接调用者：gd32f407_hal.c:HAL_I2C_Master_Transmit、gd32f407_hal.c:HAL_I2C_Mem_Write、gd32f407_hal.c:HAL_I2C_Mem_Read。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param i2c 按值传入；函数内部修改不会回写调用方变量。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static int i2c_transmit(uint32_t i2c, const uint8_t *data, uint16_t size)
{
    uint16_t i;
    i2c_flag_clear(i2c, I2C_FLAG_ADDSEND);
    for (i = 0U; i < size; i++) {
        if (i2c_wait(i2c, I2C_FLAG_TBE) != 0) {
            return -1;
        }
        i2c_data_transmit(i2c, data[i]);
        if (i2c_wait(i2c, I2C_FLAG_BTC) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief gd32f407_hal 模块：接收（对应符号 i2c_receive）。
 * @调用关系 直接调用者：gd32f407_hal.c:HAL_I2C_Master_Receive、gd32f407_hal.c:HAL_I2C_Mem_Read。
 * @局部状态 主要临时变量：index、remaining；均只在本次调用中有效。
 * @param i2c 按值传入；函数内部修改不会回写调用方变量。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static int i2c_receive(uint32_t i2c, uint8_t *data, uint16_t size)
{
    uint16_t index = 0U;
    uint16_t remaining = size;
    if (size == 1U) {
        i2c_ack_config(i2c, I2C_ACK_DISABLE);
        i2c_flag_clear(i2c, I2C_FLAG_ADDSEND);
        i2c_stop_on_bus(i2c);
        if (i2c_wait(i2c, I2C_FLAG_RBNE) != 0) {
            return -1;
        }
        data[0] = i2c_data_receive(i2c);
        return 0;
    }
    if (size == 2U) {
        i2c_ackpos_config(i2c, I2C_ACKPOS_NEXT);
        i2c_ack_config(i2c, I2C_ACK_DISABLE);
        i2c_flag_clear(i2c, I2C_FLAG_ADDSEND);
        if (i2c_wait(i2c, I2C_FLAG_BTC) != 0) {
            return -1;
        }
        i2c_stop_on_bus(i2c);
        data[0] = i2c_data_receive(i2c);
        data[1] = i2c_data_receive(i2c);
        return 0;
    }
    i2c_flag_clear(i2c, I2C_FLAG_ADDSEND);
    while (remaining > 3U) {
        if (i2c_wait(i2c, I2C_FLAG_RBNE) != 0) {
            return -1;
        }
        data[index++] = i2c_data_receive(i2c);
        remaining--;
    }
    if (i2c_wait(i2c, I2C_FLAG_BTC) != 0) {
        return -1;
    }
    i2c_ack_config(i2c, I2C_ACK_DISABLE);
    data[index++] = i2c_data_receive(i2c);
    if (i2c_wait(i2c, I2C_FLAG_BTC) != 0) {
        return -1;
    }
    i2c_stop_on_bus(i2c);
    data[index++] = i2c_data_receive(i2c);
    data[index] = i2c_data_receive(i2c);
    return 0;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 i2c_finish）。
 * @调用关系 直接调用者：gd32f407_hal.c:HAL_I2C_IsDeviceReady、gd32f407_hal.c:HAL_I2C_Master_Transmit、gd32f407_hal.c:HAL_I2C_Master_Receive、gd32f407_hal.c:HAL_I2C_Mem_Write、gd32f407_hal.c:HAL_I2C_Mem_Read。
 * @局部状态 主要临时变量：limit；均只在本次调用中有效。
 * @param i2c 按值传入；函数内部修改不会回写调用方变量。
 * @param status 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static HAL_StatusTypeDef i2c_finish(uint32_t i2c, int status)
{
    uint32_t limit = I2C_WAIT_LIMIT;
    if (status == 0) {
        while ((I2C_CTL0(i2c) & I2C_CTL0_STOP) != 0U) {
            if (limit-- == 0U) {
                status = -1;
                break;
            }
        }
    }
    i2c_ackpos_config(i2c, I2C_ACKPOS_CURRENT);
    i2c_ack_config(i2c, I2C_ACK_ENABLE);
    if (status != 0) {
        if (i2c_flag_get(i2c, I2C_FLAG_MASTER) == SET) {
            i2c_stop_on_bus(i2c);
        }
        i2c_clear_errors(i2c);
        return HAL_ERROR;
    }
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：初始化（对应符号 HAL_I2C_Init）。
 * @调用关系 直接调用者：iic.c:Iic_Init。
 * @param hi2c 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c)
{
    if (hi2c == 0) {
        return HAL_ERROR;
    }
    i2c_deinit(hi2c->Instance);
    i2c_clock_config(hi2c->Instance, 100000U, I2C_DTCY_2);
    i2c_mode_addr_config(hi2c->Instance, I2C_I2CMODE_ENABLE,
                         I2C_ADDFORMAT_7BITS, 0U);
    i2c_enable(hi2c->Instance);
    i2c_ackpos_config(hi2c->Instance, I2C_ACKPOS_CURRENT);
    i2c_ack_config(hi2c->Instance, I2C_ACK_ENABLE);
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 HAL_I2CEx_ConfigAnalogFilter）。
 * @调用关系 直接调用者：iic.c:Iic_Init。
 * @param hi2c 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param enable 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *hi2c, uint32_t enable)
{
    (void)hi2c;
    (void)enable;
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 HAL_I2CEx_ConfigDigitalFilter）。
 * @调用关系 直接调用者：iic.c:Iic_Init。
 * @param hi2c 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param filter 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *hi2c, uint32_t filter)
{
    (void)hi2c;
    (void)filter;
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：读取（对应符号 HAL_I2C_IsDeviceReady）。
 * @调用关系 直接调用者：iic.c:Iic_IsDeviceReady。
 * @局部状态 主要临时变量：status；均只在本次调用中有效。
 * @param hi2c 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param trials 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t address,
                                        uint32_t trials, uint32_t timeout_ms)
{
    int status = -1;
    (void)timeout_ms;
    if (hi2c == 0) {
        return HAL_ERROR;
    }
    while (trials-- != 0U) {
        status = i2c_prepare(hi2c->Instance);
        if (status == 0) {
            status = i2c_start_address(hi2c->Instance, (uint8_t)(address >> 1U), I2C_TRANSMITTER);
        }
        if (status == 0) {
            i2c_flag_clear(hi2c->Instance, I2C_FLAG_ADDSEND);
            i2c_stop_on_bus(hi2c->Instance);
            return i2c_finish(hi2c->Instance, 0);
        }
        i2c_stop_on_bus(hi2c->Instance);
        i2c_clear_errors(hi2c->Instance);
    }
    return HAL_ERROR;
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 HAL_I2C_Master_Transmit）。
 * @调用关系 直接调用者：iic.c:Iic_MasterWrite。
 * @局部状态 主要临时变量：status；均只在本次调用中有效。
 * @param hi2c 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t address,
                                          uint8_t *data, uint16_t size, uint32_t timeout_ms)
{
    int status;
    (void)timeout_ms;
    if ((hi2c == 0) || (data == 0) || (size == 0U)) {
        return HAL_ERROR;
    }
    status = i2c_prepare(hi2c->Instance);
    if (status == 0) status = i2c_start_address(hi2c->Instance, (uint8_t)(address >> 1U), I2C_TRANSMITTER);
    if (status == 0) status = i2c_transmit(hi2c->Instance, data, size);
    if (status == 0) i2c_stop_on_bus(hi2c->Instance);
    return i2c_finish(hi2c->Instance, status);
}

/**
 * @brief gd32f407_hal 模块：接收（对应符号 HAL_I2C_Master_Receive）。
 * @调用关系 直接调用者：iic.c:Iic_MasterRead。
 * @局部状态 主要临时变量：status；均只在本次调用中有效。
 * @param hi2c 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t address,
                                         uint8_t *data, uint16_t size, uint32_t timeout_ms)
{
    int status;
    (void)timeout_ms;
    if ((hi2c == 0) || (data == 0) || (size == 0U)) {
        return HAL_ERROR;
    }
    status = i2c_prepare(hi2c->Instance);
    if (status == 0) status = i2c_start_address(hi2c->Instance, (uint8_t)(address >> 1U), I2C_RECEIVER);
    if (status == 0) status = i2c_receive(hi2c->Instance, data, size);
    return i2c_finish(hi2c->Instance, status);
}

/**
 * @brief gd32f407_hal 模块：执行内部处理（对应符号 mem_prefix）。
 * @调用关系 直接调用者：gd32f407_hal.c:HAL_I2C_Mem_Write、gd32f407_hal.c:HAL_I2C_Mem_Read。
 * @param prefix 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t mem_prefix(uint8_t *prefix, uint16_t address, uint16_t size)
{
    if (size == I2C_MEMADD_SIZE_16BIT) {
        prefix[0] = (uint8_t)(address >> 8U);
        prefix[1] = (uint8_t)address;
        return 2U;
    }
    prefix[0] = (uint8_t)address;
    return 1U;
}

/**
 * @brief gd32f407_hal 模块：写入（对应符号 HAL_I2C_Mem_Write）。
 * @调用关系 直接调用者：iic.c:Iic_MemWrite。
 * @局部状态 主要临时变量：prefix、prefix_size、status；均只在本次调用中有效。
 * @param hi2c 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param mem_address 按值传入；函数内部修改不会回写调用方变量。
 * @param mem_address_size 按值传入；函数内部修改不会回写调用方变量。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t address,
                                    uint16_t mem_address, uint16_t mem_address_size,
                                    uint8_t *data, uint16_t size, uint32_t timeout_ms)
{
    uint8_t prefix[2];
    uint16_t prefix_size;
    int status;
    (void)timeout_ms;
    if ((hi2c == 0) || (data == 0) || (size == 0U)) return HAL_ERROR;
    prefix_size = mem_prefix(prefix, mem_address, mem_address_size);
    status = i2c_prepare(hi2c->Instance);
    if (status == 0) status = i2c_start_address(hi2c->Instance, (uint8_t)(address >> 1U), I2C_TRANSMITTER);
    if (status == 0) status = i2c_transmit(hi2c->Instance, prefix, prefix_size);
    if (status == 0) status = i2c_transmit(hi2c->Instance, data, size);
    if (status == 0) i2c_stop_on_bus(hi2c->Instance);
    return i2c_finish(hi2c->Instance, status);
}

/**
 * @brief gd32f407_hal 模块：读取（对应符号 HAL_I2C_Mem_Read）。
 * @调用关系 直接调用者：iic.c:Iic_MemRead。
 * @局部状态 主要临时变量：prefix、prefix_size、status；均只在本次调用中有效。
 * @param hi2c 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param mem_address 按值传入；函数内部修改不会回写调用方变量。
 * @param mem_address_size 按值传入；函数内部修改不会回写调用方变量。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t address,
                                   uint16_t mem_address, uint16_t mem_address_size,
                                   uint8_t *data, uint16_t size, uint32_t timeout_ms)
{
    uint8_t prefix[2];
    uint16_t prefix_size;
    int status;
    (void)timeout_ms;
    if ((hi2c == 0) || (data == 0) || (size == 0U)) return HAL_ERROR;
    prefix_size = mem_prefix(prefix, mem_address, mem_address_size);
    status = i2c_prepare(hi2c->Instance);
    if (status == 0) status = i2c_start_address(hi2c->Instance, (uint8_t)(address >> 1U), I2C_TRANSMITTER);
    if (status == 0) status = i2c_transmit(hi2c->Instance, prefix, prefix_size);
    if (status == 0) status = i2c_start_address(hi2c->Instance, (uint8_t)(address >> 1U), I2C_RECEIVER);
    if (status == 0) status = i2c_receive(hi2c->Instance, data, size);
    return i2c_finish(hi2c->Instance, status);
}

/**
 * @brief gd32f407_hal 模块：初始化（对应符号 HAL_SPI_Init）。
 * @调用关系 直接调用者：spi.c:SPI1_Init。
 * @param hspi 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi)
{
    spi_parameter_struct cfg;
    if (hspi == 0) {
        return HAL_ERROR;
    }
    spi_i2s_deinit(hspi->Instance);
    spi_struct_para_init(&cfg);
    cfg.device_mode = SPI_MASTER;
    cfg.trans_mode = SPI_TRANSMODE_FULLDUPLEX;
    cfg.frame_size = SPI_FRAMESIZE_8BIT;
    cfg.nss = SPI_NSS_SOFT;
    cfg.endian = SPI_ENDIAN_MSB;
    cfg.clock_polarity_phase = SPI_CK_PL_LOW_PH_1EDGE;
    cfg.prescale = hspi->Init.BaudRatePrescaler;
    spi_init(hspi->Instance, &cfg);
    spi_enable(hspi->Instance);
    return HAL_OK;
}

/**
 * @brief gd32f407_hal 模块：接收（对应符号 HAL_SPI_TransmitReceive）。
 * @调用关系 直接调用者：spi.c:SPI1_TransferByte。
 * @局部状态 主要临时变量：i、start；均只在本次调用中有效。
 * @param hspi 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param tx 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param rx 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param size 按值传入；函数内部修改不会回写调用方变量。
 * @param timeout_ms 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi,
                                          uint8_t *tx, uint8_t *rx,
                                          uint16_t size, uint32_t timeout_ms)
{
    uint16_t i;
    uint32_t start;
    if ((hspi == 0) || (tx == 0) || (rx == 0)) {
        return HAL_ERROR;
    }
    start = HAL_GetTick();
    for (i = 0U; i < size; i++) {
        while (spi_i2s_flag_get(hspi->Instance, SPI_FLAG_TBE) == RESET) {
            if (elapsed(start, timeout_ms) != 0U) return HAL_TIMEOUT;
        }
        spi_i2s_data_transmit(hspi->Instance, tx[i]);
        while (spi_i2s_flag_get(hspi->Instance, SPI_FLAG_RBNE) == RESET) {
            if (elapsed(start, timeout_ms) != 0U) return HAL_TIMEOUT;
        }
        rx[i] = (uint8_t)spi_i2s_data_receive(hspi->Instance);
    }
    while (spi_i2s_flag_get(hspi->Instance, SPI_FLAG_TRANS) == SET) {
        if (elapsed(start, timeout_ms) != 0U) return HAL_TIMEOUT;
    }
    return HAL_OK;
}
