/* AUTO-FILE-DOC
 * 文件职责：gd32f407_hal 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef GD32F407_HAL_H
#define GD32F407_HAL_H

#include "gd32f4xx.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Small STM32-HAL-shaped adapter used by the retained Joker BSP.
 * The implementation is entirely backed by the GD32F4xx standard peripheral
 * library; no STM32 device header or binary library is used by this project.
 */
typedef enum {
    HAL_OK = 0U,
    HAL_ERROR = 1U,
    HAL_BUSY = 2U,
    HAL_TIMEOUT = 3U
} HAL_StatusTypeDef;

typedef enum {
    GPIO_PIN_RESET = 0U,
    GPIO_PIN_SET = 1U
} GPIO_PinState;

typedef uint32_t GPIO_TypeDef;

#undef GPIOA
#undef GPIOB
#undef GPIOC
#undef GPIOD
#undef GPIOE
#undef GPIOF
#undef GPIOG
#undef GPIOH
#undef GPIOI
#define GPIOA ((GPIO_TypeDef *)(GPIO_BASE + 0x00000000U))
#define GPIOB ((GPIO_TypeDef *)(GPIO_BASE + 0x00000400U))
#define GPIOC ((GPIO_TypeDef *)(GPIO_BASE + 0x00000800U))
#define GPIOD ((GPIO_TypeDef *)(GPIO_BASE + 0x00000C00U))
#define GPIOE ((GPIO_TypeDef *)(GPIO_BASE + 0x00001000U))
#define GPIOF ((GPIO_TypeDef *)(GPIO_BASE + 0x00001400U))
#define GPIOG ((GPIO_TypeDef *)(GPIO_BASE + 0x00001800U))
#define GPIOH ((GPIO_TypeDef *)(GPIO_BASE + 0x00001C00U))
#define GPIOI ((GPIO_TypeDef *)(GPIO_BASE + 0x00002000U))

typedef struct {
    uint32_t Pin;
    uint32_t Mode;
    uint32_t Pull;
    uint32_t Speed;
    uint32_t Alternate;
} GPIO_InitTypeDef;

#undef GPIO_MODE_INPUT
#define GPIO_MODE_INPUT              0x00U
#define GPIO_MODE_OUTPUT_PP          0x01U
#define GPIO_MODE_OUTPUT_OD          0x02U
#define GPIO_MODE_AF_PP              0x03U
#define GPIO_MODE_AF_OD              0x04U
#define GPIO_MODE_IT_FALLING         0x05U
#define GPIO_MODE_IT_RISING_FALLING  0x06U
#define GPIO_NOPULL                  GPIO_PUPD_NONE
#define GPIO_PULLUP                  GPIO_PUPD_PULLUP
#define GPIO_PULLDOWN                GPIO_PUPD_PULLDOWN
#define GPIO_SPEED_FREQ_LOW          GPIO_OSPEED_2MHZ
#define GPIO_SPEED_FREQ_HIGH         GPIO_OSPEED_50MHZ
#define GPIO_SPEED_FREQ_VERY_HIGH    GPIO_OSPEED_MAX
#define GPIO_AF4_I2C1                GPIO_AF_4
#define GPIO_AF5_SPI1                GPIO_AF_5
#define GPIO_AF7_USART1              GPIO_AF_7
#define GPIO_AF7_USART3              GPIO_AF_7
#define GPIO_AF8_UART4               GPIO_AF_8
#define GPIO_AF8_UART5               GPIO_AF_8

typedef struct {
    uint32_t BaudRate;
    uint32_t WordLength;
    uint32_t StopBits;
    uint32_t Parity;
    uint32_t HwFlowCtl;
    uint32_t Mode;
} UART_InitTypeDef;

typedef struct {
    uint32_t Instance;
    UART_InitTypeDef Init;
    uint8_t *rx_data;
    uint16_t rx_size;
    uint16_t rx_count;
    const uint8_t *tx_data;
    uint16_t tx_size;
    uint16_t tx_count;
    volatile uint8_t tx_busy;
} UART_HandleTypeDef;

#define UART_WORDLENGTH_8B USART_WL_8BIT
#define UART_STOPBITS_1    USART_STB_1BIT
#define UART_PARITY_NONE   USART_PM_NONE
#define UART_HWCONTROL_NONE 0U
#define UART_MODE_TX_RX    0x03U

typedef struct {
    uint32_t Timing;
    uint32_t OwnAddress1;
    uint32_t AddressingMode;
    uint32_t DualAddressMode;
    uint32_t OwnAddress2;
    uint32_t OwnAddress2Masks;
    uint32_t GeneralCallMode;
    uint32_t NoStretchMode;
} I2C_InitTypeDef;

typedef struct {
    uint32_t Instance;
    I2C_InitTypeDef Init;
} I2C_HandleTypeDef;

#define I2C_ADDRESSINGMODE_7BIT      0U
#define I2C_DUALADDRESS_DISABLE     0U
#define I2C_OA2_NOMASK              0U
#define I2C_GENERALCALL_DISABLE     0U
#define I2C_NOSTRETCH_DISABLE       0U
#define I2C_ANALOGFILTER_ENABLE     1U
#define I2C_MEMADD_SIZE_8BIT        1U
#define I2C_MEMADD_SIZE_16BIT       2U

typedef struct {
    uint32_t Mode;
    uint32_t Direction;
    uint32_t DataSize;
    uint32_t CLKPolarity;
    uint32_t CLKPhase;
    uint32_t NSS;
    uint32_t BaudRatePrescaler;
    uint32_t FirstBit;
    uint32_t TIMode;
    uint32_t CRCCalculation;
    uint32_t CRCPolynomial;
    uint32_t NSSPMode;
    uint32_t NSSPolarity;
    uint32_t FifoThreshold;
    uint32_t TxCRCInitializationPattern;
    uint32_t RxCRCInitializationPattern;
    uint32_t MasterSSIdleness;
    uint32_t MasterInterDataIdleness;
    uint32_t MasterReceiverAutoSusp;
    uint32_t MasterKeepIOState;
    uint32_t IOSwap;
} SPI_InitTypeDef;

typedef struct {
    uint32_t Instance;
    SPI_InitTypeDef Init;
} SPI_HandleTypeDef;

#define SPI_MODE_MASTER                         SPI_MASTER
#define SPI_DIRECTION_2LINES                    SPI_TRANSMODE_FULLDUPLEX
#define SPI_DATASIZE_8BIT                       SPI_FRAMESIZE_8BIT
#define SPI_POLARITY_LOW                        SPI_CK_PL_LOW_PH_1EDGE
#define SPI_PHASE_1EDGE                         0U
#define SPI_BAUDRATEPRESCALER_128               SPI_PSC_128
#define SPI_FIRSTBIT_MSB                        SPI_ENDIAN_MSB
#define SPI_TIMODE_DISABLE                      0U
#define SPI_CRCCALCULATION_DISABLE              0U
#define SPI_NSS_PULSE_DISABLE                   0U
#define SPI_NSS_POLARITY_LOW                    0U
#define SPI_FIFO_THRESHOLD_01DATA               0U
#define SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN 0U
#define SPI_MASTER_SS_IDLENESS_00CYCLE          0U
#define SPI_MASTER_INTERDATA_IDLENESS_00CYCLE   0U
#define SPI_MASTER_RX_AUTOSUSP_DISABLE          0U
#define SPI_MASTER_KEEP_IO_STATE_ENABLE         1U
#define SPI_IO_SWAP_DISABLE                     0U

typedef struct {
    uint32_t Prescaler;
    uint32_t CounterMode;
    uint32_t Period;
    uint32_t ClockDivision;
    uint32_t AutoReloadPreload;
} TIM_Base_InitTypeDef;

typedef struct {
    uint32_t Instance;
    TIM_Base_InitTypeDef Init;
} TIM_HandleTypeDef;

#define TIM_COUNTERMODE_UP              TIMER_COUNTER_UP
#define TIM_CLOCKDIVISION_DIV1          TIMER_CKDIV_DIV1
#define TIM_AUTORELOAD_PRELOAD_DISABLE  0U
#define TIM_FLAG_UPDATE                 TIMER_INT_FLAG_UP
#define TIM_IT_UPDATE                   TIMER_INT_UP
#define TIM6                            TIMER5
#define TIM6_DAC_IRQn                   TIMER5_DAC_IRQn
#define TIM6_DAC_IRQHandler             TIMER5_DAC_IRQHandler

#define EXTI9_5_IRQn                    EXTI5_9_IRQn
#define EXTI9_5_IRQHandler              EXTI5_9_IRQHandler
#define EXTI15_10_IRQn                  EXTI10_15_IRQn
#define EXTI15_10_IRQHandler            EXTI10_15_IRQHandler
#define USART1_IRQHandler               USART0_IRQHandler

#define NVIC_PRIORITYGROUP_2            NVIC_PRIGROUP_PRE2_SUB2
#define HAL_MAX_DELAY                   0xFFFFFFFFUL
#define UID_BASE                        0x1FFF7A10UL

#define __HAL_RCC_GPIOA_CLK_ENABLE()    rcu_periph_clock_enable(RCU_GPIOA)
#define __HAL_RCC_GPIOB_CLK_ENABLE()    rcu_periph_clock_enable(RCU_GPIOB)
#define __HAL_RCC_GPIOC_CLK_ENABLE()    rcu_periph_clock_enable(RCU_GPIOC)
#define __HAL_RCC_GPIOD_CLK_ENABLE()    rcu_periph_clock_enable(RCU_GPIOD)
#define __HAL_RCC_GPIOE_CLK_ENABLE()    rcu_periph_clock_enable(RCU_GPIOE)
#define __HAL_RCC_GPIOF_CLK_ENABLE()    rcu_periph_clock_enable(RCU_GPIOF)
#define __HAL_RCC_GPIOG_CLK_ENABLE()    rcu_periph_clock_enable(RCU_GPIOG)
#define __HAL_RCC_SYSCFG_CLK_ENABLE()   rcu_periph_clock_enable(RCU_SYSCFG)
#define __HAL_RCC_USART1_CLK_ENABLE()   rcu_periph_clock_enable(RCU_USART0)
#define __HAL_RCC_UART4_CLK_ENABLE()    rcu_periph_clock_enable(RCU_UART3)
#define __HAL_RCC_UART5_CLK_ENABLE()    rcu_periph_clock_enable(RCU_UART4)
#define __HAL_RCC_USART3_CLK_ENABLE()   rcu_periph_clock_enable(RCU_USART2)
#define __HAL_RCC_I2C1_CLK_ENABLE()     rcu_periph_clock_enable(RCU_I2C0)
#define __HAL_RCC_I2C1_FORCE_RESET()    rcu_periph_reset_enable(RCU_I2C0RST)
#define __HAL_RCC_I2C1_RELEASE_RESET()  rcu_periph_reset_disable(RCU_I2C0RST)
#define __HAL_RCC_SPI1_CLK_ENABLE()     rcu_periph_clock_enable(RCU_SPI0)
#define __HAL_RCC_TIM6_CLK_ENABLE()     rcu_periph_clock_enable(RCU_TIMER5)

#define __HAL_GPIO_EXTI_GET_IT(pin)     exti_interrupt_flag_get((exti_line_enum)(pin))
#define __HAL_GPIO_EXTI_CLEAR_IT(pin)   exti_interrupt_flag_clear((exti_line_enum)(pin))
#define __HAL_TIM_GET_FLAG(htim, flag)  timer_interrupt_flag_get((htim)->Instance, (flag))
#define __HAL_TIM_GET_IT_SOURCE(htim, it) timer_interrupt_flag_get((htim)->Instance, TIMER_INT_FLAG_UP)
#define __HAL_TIM_CLEAR_IT(htim, it)    timer_interrupt_flag_clear((htim)->Instance, TIMER_INT_FLAG_UP)

void HAL_Init(void);
void HAL_IncTick(void);
uint32_t HAL_GetTick(void);
void HAL_Delay(uint32_t delay_ms);

void HAL_GPIO_Init(GPIO_TypeDef *port, const GPIO_InitTypeDef *init);
void HAL_GPIO_WritePin(GPIO_TypeDef *port, uint16_t pins, GPIO_PinState state);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin);
void HAL_GPIO_TogglePin(GPIO_TypeDef *port, uint16_t pins);

void HAL_NVIC_SetPriorityGrouping(uint32_t group);
void HAL_NVIC_SetPriority(IRQn_Type irqn, uint32_t preemption, uint32_t subpriority);
void HAL_NVIC_EnableIRQ(IRQn_Type irqn);
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irqn);

HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *htim);
HAL_StatusTypeDef HAL_TIM_Base_Stop_IT(TIM_HandleTypeDef *htim);

HAL_StatusTypeDef HAL_UART_Init(UART_HandleTypeDef *huart);
HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, const uint8_t *data,
                                    uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef HAL_UART_Transmit_IT(UART_HandleTypeDef *huart,
                                       const uint8_t *data, uint16_t size);
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *huart,
                                      uint8_t *data, uint16_t size);
void HAL_UART_IRQHandler(UART_HandleTypeDef *huart);
void HAL_UART_MspInit(UART_HandleTypeDef *huart);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *hi2c, uint32_t enable);
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *hi2c, uint32_t filter);
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *hi2c, uint16_t address,
                                        uint32_t trials, uint32_t timeout_ms);
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *hi2c, uint16_t address,
                                          uint8_t *data, uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef HAL_I2C_Master_Receive(I2C_HandleTypeDef *hi2c, uint16_t address,
                                         uint8_t *data, uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef HAL_I2C_Mem_Write(I2C_HandleTypeDef *hi2c, uint16_t address,
                                    uint16_t mem_address, uint16_t mem_address_size,
                                    uint8_t *data, uint16_t size, uint32_t timeout_ms);
HAL_StatusTypeDef HAL_I2C_Mem_Read(I2C_HandleTypeDef *hi2c, uint16_t address,
                                   uint16_t mem_address, uint16_t mem_address_size,
                                   uint8_t *data, uint16_t size, uint32_t timeout_ms);

HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *hspi);
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *hspi,
                                          uint8_t *tx, uint8_t *rx,
                                          uint16_t size, uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
