#include "./RS485/rs485.h"

#include "./SYSTEM/usart/usart.h"

#include <stddef.h>
#include <string.h>

static uint8_t s_tx_buffer[RS485_MAX_FRAME_SIZE];
static volatile uint8_t s_tx_busy;

void Rs485_Init(uint32_t baudrate)
{
    s_tx_busy = 0U;
    usart_init(baudrate);
}

uint8_t Rs485_Send(const uint8_t *data, uint16_t length)
{
    if ((data == NULL) || (length == 0U) ||
        (length > RS485_MAX_FRAME_SIZE) || (s_tx_busy != 0U))
    {
        return 0U;
    }

    memcpy(s_tx_buffer, data, length);
    s_tx_busy = 1U;

    /* Q1 derives MAX485 RE/DE from 485DI, so no MCU direction GPIO is needed. */
    if (HAL_UART_Transmit_IT(&g_uart1_handle, s_tx_buffer, length) != HAL_OK)
    {
        s_tx_busy = 0U;
        return 0U;
    }

    return 1U;
}

uint16_t Rs485_Read(uint8_t *data, uint16_t capacity)
{
    return usart_read(data, capacity);
}

void Rs485_ClearRx(void)
{
    usart_clear_rx();
}

uint8_t Rs485_IsTxBusy(void)
{
    return s_tx_busy;
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &g_uart1_handle)
    {
        s_tx_busy = 0U;
        /* Drop any local echo and arm RX before the peer's next frame. */
        usart_restart_rx();
    }
}
