/**
 * @file usart.c
 * @brief USART1 printf 输出和接收辅助函数实现。
 */
#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"


/* 如果使用os,则包括下面的头文件即可. */
#if SYS_SUPPORT_OS
#include "os.h"   /* os 使用 */
#endif

/******************************************************************************************/
/* 加入以下代码, 支持printf函数, 而不需要选择use MicroLIB */

#if 1
#if (__ARMCC_VERSION >= 6010050)            /* 使用AC6编译器时 */
__asm(".global __use_no_semihosting\n\t");  /* 声明不使用半主机模式 */
__asm(".global __ARM_use_no_argv \n\t");    /* AC6下需要声明main函数为无参数格式，否则部分例程可能出现半主机模式 */

#else
/* 使用AC5编译器时, 要在这里定义__FILE 和 不使用半主机模式 */
#pragma import(__use_no_semihosting)

struct __FILE
{
    int handle;
    /* Whatever you require here. If the only file you are using is */
    /* standard output using printf() for debugging, no file handling */
    /* is required. */
};

#endif

/* 不使用半主机模式，至少需要重定义_ttywrch\_sys_exit\_sys_command_string函数,以同时兼容AC6和AC5模式 */
int _ttywrch(int ch)
{
    ch = ch;
    return ch;
}

/* 定义_sys_exit()以避免使用半主机模式 */
void _sys_exit(int x)
{
    x = x;
}

char *_sys_command_string(char *cmd, int len)
{
    return NULL;
}

/* FILE 在 stdio.h里面定义 */
FILE __stdout;

/* 重定义fputc函数, printf函数最终会通过调用fputc输出字符串到串口 */
int fputc(int ch, FILE *f)
{
    while (usart_flag_get(USART_UX, USART_FLAG_TBE) == RESET) {
    }
    usart_data_transmit(USART_UX, (uint8_t)ch);
    return ch;
}
#endif

/******************************************************************************************/

#if USART_EN_RX     /* 如果使能了接收 */

/* 接收缓冲, 最大USART_REC_LEN个字节. */
uint8_t g_usart_rx_buf[USART_REC_LEN];

/*  接收状态
 *  bit15，      接收完成标志
 *  bit14，      接收到0x0d
 *  bit13~0，    接收到的有效字节数目
*/
uint16_t g_usart_rx_sta = 0;

static uint8_t g_usart_rx_fifo[USART_RX_FIFO_SIZE];
uint8_t g_rx_buffer[RXBUFFERSIZE];
volatile uint16_t g_usart_rx_head = 0U;
volatile uint16_t g_usart_rx_tail = 0U;          /* HAL库使用的串口接收缓冲 */

UART_HandleTypeDef g_uart1_handle;          /* UART句柄 */
UART_HandleTypeDef g_audio_uart1_handle;
UART_HandleTypeDef g_audio_uart2_handle;

static uint8_t s_audio_uart_rx_fifo[AUDIO_UART_CHANNEL_COUNT][AUDIO_UART_RX_FIFO_SIZE];
static uint8_t s_audio_uart_rx_byte[AUDIO_UART_CHANNEL_COUNT];
static volatile uint16_t s_audio_uart_rx_head[AUDIO_UART_CHANNEL_COUNT];
static volatile uint16_t s_audio_uart_rx_tail[AUDIO_UART_CHANNEL_COUNT];

static void audio_usart_rx_push(uint8_t channel, uint8_t data)
{
    uint16_t next_head;

    if (channel >= AUDIO_UART_CHANNEL_COUNT)
    {
        return;
    }

    next_head = (uint16_t)((s_audio_uart_rx_head[channel] + 1U) %
                           AUDIO_UART_RX_FIFO_SIZE);
    if (next_head == s_audio_uart_rx_tail[channel])
    {
        /* 回包只占10字节；满时丢最旧字节，优先保留最新完整帧。 */
        s_audio_uart_rx_tail[channel] =
            (uint16_t)((s_audio_uart_rx_tail[channel] + 1U) %
                       AUDIO_UART_RX_FIFO_SIZE);
    }

    s_audio_uart_rx_fifo[channel][s_audio_uart_rx_head[channel]] = data;
    s_audio_uart_rx_head[channel] = next_head;
}


/**
 * @brief       串口X初始化函数
 * @param       baudrate: 波特率, 根据自己需要设置波特率值
 * @note        注意: 必须设置正确的时钟源, 否则串口波特率就会设置异常.
 *              这里的USART的时钟源在sys_stm32_clock_init()函数中已经设置过了.
 * @retval      无
 */
static void audio_usart_config_handle(UART_HandleTypeDef *handle,
                                      uint32_t instance,
                                      uint32_t baudrate)
{
    handle->Instance = instance;
    handle->Init.BaudRate = baudrate;
    handle->Init.WordLength = UART_WORDLENGTH_8B;
    handle->Init.StopBits = UART_STOPBITS_1;
    handle->Init.Parity = UART_PARITY_NONE;
    handle->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    handle->Init.Mode = UART_MODE_TX_RX;
    (void)HAL_UART_Init(handle);
}

void audio_usart_init(uint32_t baudrate)
{
    audio_usart_config_handle(&g_audio_uart1_handle, AUDIO_UART, baudrate);
    audio_usart_config_handle(&g_audio_uart2_handle, AUDIO2_UART, baudrate);

    /* 音效板需要接收KT404C播放完成帧，用于推进ADVERT插播队列。 */
    audio_usart_clear_rx(AUDIO_UART_CHANNEL_1);
    audio_usart_clear_rx(AUDIO_UART_CHANNEL_2);
    (void)HAL_UART_Receive_IT(&g_audio_uart1_handle,
                              &s_audio_uart_rx_byte[AUDIO_UART_CHANNEL_1],
                              1U);
}

uint16_t audio_usart_read(uint8_t channel, uint8_t *data, uint16_t len)
{
    uint16_t count;

    if ((channel >= AUDIO_UART_CHANNEL_COUNT) ||
        (data == NULL) || (len == 0U))
    {
        return 0U;
    }

    count = 0U;
    while ((count < len) &&
           (s_audio_uart_rx_tail[channel] != s_audio_uart_rx_head[channel]))
    {
        data[count++] =
            s_audio_uart_rx_fifo[channel][s_audio_uart_rx_tail[channel]];
        s_audio_uart_rx_tail[channel] =
            (uint16_t)((s_audio_uart_rx_tail[channel] + 1U) %
                       AUDIO_UART_RX_FIFO_SIZE);
    }

    return count;
}

void audio_usart_clear_rx(uint8_t channel)
{
    uint32_t primask;

    if (channel >= AUDIO_UART_CHANNEL_COUNT)
    {
        return;
    }

    primask = __get_PRIMASK();
    __disable_irq();
    s_audio_uart_rx_head[channel] = 0U;
    s_audio_uart_rx_tail[channel] = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void usart_init(uint32_t baudrate)
{
    g_uart1_handle.Instance = USART_UX;                    /* USART1 */
    g_uart1_handle.Init.BaudRate = baudrate;               /* 波特率 */
    g_uart1_handle.Init.WordLength = UART_WORDLENGTH_8B;   /* 字长为8位数据格式 */
    g_uart1_handle.Init.StopBits = UART_STOPBITS_1;        /* 一个停止位 */
    g_uart1_handle.Init.Parity = UART_PARITY_NONE;         /* 无奇偶校验位 */
    g_uart1_handle.Init.HwFlowCtl = UART_HWCONTROL_NONE;   /* 无硬件流控 */
    g_uart1_handle.Init.Mode = UART_MODE_TX_RX;            /* 收发模式 */
    HAL_UART_Init(&g_uart1_handle);                        /* HAL_UART_Init()会使能UART1 */
    
    /* 该函数会开启接收中断：标志位UART_IT_RXNE，并且设置接收缓冲以及接收缓冲接收最大数据量 */
    usart_clear_rx();
    HAL_UART_Receive_IT(&g_uart1_handle, (uint8_t *)g_rx_buffer, RXBUFFERSIZE);
}

/**
 * @brief       UART底层初始化函数
 * @param       huart: UART句柄类型指针
 * @note        此函数会被HAL_UART_Init()调用
 *              完成时钟使能，引脚配置，中断配置
 * @retval      无
 */
void HAL_UART_MspInit(UART_HandleTypeDef *huart)
{
    GPIO_InitTypeDef gpio_init_struct;
    if (huart->Instance == USART_UX)                                /* 如果是串口1，进行串口1 MSP初始化 */
    {
        USART_UX_CLK_ENABLE();                                      /* USART1 时钟使能 */
        USART_TX_GPIO_CLK_ENABLE();                                 /* 发送引脚时钟使能 */
        USART_RX_GPIO_CLK_ENABLE();                                 /* 接收引脚时钟使能 */

        gpio_init_struct.Pin = USART_TX_GPIO_PIN;                   /* TX引脚 */
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;                    /* 复用推挽 */
        gpio_init_struct.Pull = GPIO_PULLUP;                        /* 上拉 */
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;              /* 高速 */
        gpio_init_struct.Alternate = USART_TX_GPIO_AF;              /* 复用为USART1 */
        HAL_GPIO_Init(USART_TX_GPIO_PORT, &gpio_init_struct);       /* 初始化发送引脚 */

        gpio_init_struct.Pin = USART_RX_GPIO_PIN;                   /* RX引脚 */
        gpio_init_struct.Alternate = USART_RX_GPIO_AF;              /* 复用为USART1 */
        HAL_GPIO_Init(USART_RX_GPIO_PORT, &gpio_init_struct);       /* 初始化接收引脚 */

#if USART_EN_RX
        HAL_NVIC_EnableIRQ(USART_UX_IRQn);                          /* 使能USART1中断通道 */
        HAL_NVIC_SetPriority(USART_UX_IRQn, 0U, 1U);               /* RS485字节接收优先于游戏输入 */
#endif
    }
    else if (huart->Instance == AUDIO_UART)
    {
        AUDIO_UART_CLK_ENABLE();
        AUDIO_UART_GPIO_CLK_ENABLE();

        gpio_init_struct.Pin = AUDIO_UART_TX_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;
        gpio_init_struct.Pull = GPIO_PULLUP;
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
        gpio_init_struct.Alternate = AUDIO_UART_TX_GPIO_AF;
        HAL_GPIO_Init(AUDIO_UART_TX_GPIO_PORT, &gpio_init_struct);

        gpio_init_struct.Pin = AUDIO_UART_RX_GPIO_PIN;
        gpio_init_struct.Alternate = AUDIO_UART_RX_GPIO_AF;
        HAL_GPIO_Init(AUDIO_UART_RX_GPIO_PORT, &gpio_init_struct);

        HAL_NVIC_SetPriority(AUDIO_UART_IRQn, 3U, 3U);
        HAL_NVIC_EnableIRQ(AUDIO_UART_IRQn);
    }
    else if (huart->Instance == AUDIO2_UART)
    {
        AUDIO2_UART_CLK_ENABLE();
        AUDIO2_UART_GPIO_CLK_ENABLE();

        gpio_init_struct.Pin = AUDIO2_UART_TX_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_AF_PP;
        gpio_init_struct.Pull = GPIO_PULLUP;
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;
        gpio_init_struct.Alternate = AUDIO2_UART_TX_GPIO_AF;
        HAL_GPIO_Init(AUDIO2_UART_TX_GPIO_PORT, &gpio_init_struct);

        gpio_init_struct.Pin = AUDIO2_UART_RX_GPIO_PIN;
        gpio_init_struct.Alternate = AUDIO2_UART_RX_GPIO_AF;
        HAL_GPIO_Init(AUDIO2_UART_RX_GPIO_PORT, &gpio_init_struct);

        /* 背景板同样只发送，不开启USART3接收中断。 */
    }

}

/**
 * @brief       Rx传输回调函数
 * @param       huart: UART句柄类型指针
 * @retval      无
 */
void usart_send_data(const uint8_t *data, uint16_t len)
{
    if ((data == NULL) || (len == 0U))
    {
        return;
    }

    (void)HAL_UART_Transmit(&g_uart1_handle, (uint8_t *)data, len, HAL_MAX_DELAY);
}

uint16_t usart_available(void)
{
    uint16_t head = g_usart_rx_head;
    uint16_t tail = g_usart_rx_tail;

    if (head >= tail)
    {
        return (uint16_t)(head - tail);
    }

    return (uint16_t)(USART_RX_FIFO_SIZE - tail + head);
}

uint16_t usart_read(uint8_t *data, uint16_t len)
{
    uint16_t count = 0U;

    if ((data == NULL) || (len == 0U))
    {
        return 0U;
    }

    while ((count < len) && (g_usart_rx_tail != g_usart_rx_head))
    {
        data[count++] = g_usart_rx_fifo[g_usart_rx_tail];
        g_usart_rx_tail = (uint16_t)((g_usart_rx_tail + 1U) % USART_RX_FIFO_SIZE);
    }

    return count;
}

void usart_clear_rx(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    g_usart_rx_head = 0U;
    g_usart_rx_tail = 0U;
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void usart_restart_rx(void)
{
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    usart_interrupt_disable(USART_UX, USART_INT_RBNE);
    while (usart_flag_get(USART_UX, USART_FLAG_RBNE) != RESET)
    {
        (void)usart_data_receive(USART_UX);
    }
    g_usart_rx_head = 0U;
    g_usart_rx_tail = 0U;
    (void)HAL_UART_Receive_IT(&g_uart1_handle,
                              g_rx_buffer, RXBUFFERSIZE);
    if (primask == 0U)
    {
        __enable_irq();
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &g_uart1_handle)
    {
        uint16_t next_head = (uint16_t)((g_usart_rx_head + 1U) % USART_RX_FIFO_SIZE);

        if (next_head != g_usart_rx_tail)
        {
            g_usart_rx_fifo[g_usart_rx_head] = g_rx_buffer[0];
            g_usart_rx_head = next_head;
        }

        (void)HAL_UART_Receive_IT(&g_uart1_handle, g_rx_buffer, RXBUFFERSIZE);
    }
    else if (huart == &g_audio_uart1_handle)
    {
        audio_usart_rx_push(AUDIO_UART_CHANNEL_1,
                            s_audio_uart_rx_byte[AUDIO_UART_CHANNEL_1]);
        (void)HAL_UART_Receive_IT(&g_audio_uart1_handle,
                                  &s_audio_uart_rx_byte[AUDIO_UART_CHANNEL_1],
                                  1U);
    }
    else if (huart == &g_audio_uart2_handle)
    {
        audio_usart_rx_push(AUDIO_UART_CHANNEL_2,
                            s_audio_uart_rx_byte[AUDIO_UART_CHANNEL_2]);
        (void)HAL_UART_Receive_IT(&g_audio_uart2_handle,
                                  &s_audio_uart_rx_byte[AUDIO_UART_CHANNEL_2],
                                  1U);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart == &g_uart1_handle)
    {
        /* Recover Modbus RX after overrun, noise or framing errors. */
        usart_restart_rx();
    }
    else if (huart == &g_audio_uart1_handle)
    {
        (void)HAL_UART_Receive_IT(&g_audio_uart1_handle,
                                  &s_audio_uart_rx_byte[AUDIO_UART_CHANNEL_1],
                                  1U);
    }
    else if (huart == &g_audio_uart2_handle)
    {
        (void)HAL_UART_Receive_IT(&g_audio_uart2_handle,
                                  &s_audio_uart_rx_byte[AUDIO_UART_CHANNEL_2],
                                  1U);
    }
}

/**
 * @brief       串口1中断服务函数
 * @param       无
 * @retval      无
 */
void USART_UX_IRQHandler(void)
{ 
#if SYS_SUPPORT_OS                        /* 使用OS */
    OSIntEnter();    
#endif

    HAL_UART_IRQHandler(&g_uart1_handle); /* 调用HAL库中断处理公用函数 */

#if SYS_SUPPORT_OS                      /* 使用OS */
    OSIntExit();
#endif

}

void UART3_IRQHandler(void)
{
    HAL_UART_IRQHandler(&g_audio_uart1_handle);
}

void USART2_IRQHandler(void)
{
    HAL_UART_IRQHandler(&g_audio_uart2_handle);
}

#endif


 

 



 

 
