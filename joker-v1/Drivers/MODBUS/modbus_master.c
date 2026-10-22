#include "./MODBUS/modbus_master.h"

#include "./SYSTEM/sys/sys.h"
#include "./SYSTEM/usart/usart.h"

#include <stddef.h>
#include <string.h>

#define MODBUS_MASTER_MIN_SLAVE_ADDRESS  1U
#define MODBUS_MASTER_MAX_SLAVE_ADDRESS  247U
#define MODBUS_MASTER_MAX_ADU_SIZE       256U
#define MODBUS_MASTER_RX_CHUNK_SIZE      64U
#define MODBUS_MASTER_FRAME_GAP_MS       4U
#define MODBUS_MASTER_RESPONSE_TIMEOUT_MS 20U

#define MODBUS_FUNCTION_READ_HOLDING     0x03U
#define MODBUS_FUNCTION_WRITE_SINGLE     0x06U
#define MODBUS_FUNCTION_WRITE_MULTIPLE   0x10U

typedef enum
{
    MODBUS_MASTER_PHASE_IDLE = 0U,
    MODBUS_MASTER_PHASE_WAIT_RESPONSE
} ModbusMaster_Phase;

typedef struct
{
    ModbusMaster_Phase phase;
    ModbusMaster_Status status;
    uint8_t slave_address;
    uint8_t function;
    uint8_t request_echo[6];
    uint16_t start_address;
    uint16_t quantity;
    uint32_t request_tick;
    uint32_t last_byte_tick;
    uint8_t rx_frame[MODBUS_MASTER_MAX_ADU_SIZE];
    uint16_t rx_length;
    uint16_t read_values[MODBUS_MASTER_MAX_READ_REGISTERS];
    uint16_t read_count;
    uint8_t read_pending;
    uint8_t exception_code;
} ModbusMaster_Context;

static ModbusMaster_Context s_master;

static uint16_t modbus_master_crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;

    for (index = 0U; index < length; index++)
    {
        uint8_t bit;

        crc ^= data[index];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 0x0001U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

static uint16_t modbus_master_append_crc(uint8_t *frame, uint16_t length)
{
    uint16_t crc = modbus_master_crc16(frame, length);

    frame[length] = (uint8_t)(crc & 0xFFU);
    frame[length + 1U] = (uint8_t)(crc >> 8U);
    return (uint16_t)(length + 2U);
}

static uint8_t modbus_master_slave_is_valid(uint8_t slave_address)
{
    return ((slave_address >= MODBUS_MASTER_MIN_SLAVE_ADDRESS) &&
            (slave_address <= MODBUS_MASTER_MAX_SLAVE_ADDRESS)) ? 1U : 0U;
}

static uint8_t modbus_master_begin_request(const uint8_t *frame,
                                            uint16_t length,
                                            uint8_t slave_address,
                                            uint8_t function,
                                            uint16_t start_address,
                                            uint16_t quantity)
{
    if ((frame == NULL) || (length == 0U) ||
        (s_master.phase != MODBUS_MASTER_PHASE_IDLE))
    {
        return 0U;
    }

    usart_clear_rx();
    s_master.slave_address = slave_address;
    s_master.function = function;
    s_master.request_echo[0] = frame[0];
    s_master.request_echo[1] = frame[1];
    s_master.request_echo[2] = frame[2];
    s_master.request_echo[3] = frame[3];
    s_master.request_echo[4] = frame[4];
    s_master.request_echo[5] = frame[5];
    s_master.start_address = start_address;
    s_master.quantity = quantity;
    s_master.rx_length = 0U;
    s_master.read_pending = 0U;
    s_master.read_count = 0U;
    s_master.exception_code = 0U;
    s_master.request_tick = HAL_GetTick();
    s_master.last_byte_tick = s_master.request_tick;
    s_master.status = MODBUS_MASTER_STATUS_BUSY;
    s_master.phase = MODBUS_MASTER_PHASE_WAIT_RESPONSE;
    usart_send_data(frame, length);
    return 1U;
}

static void modbus_master_finish(ModbusMaster_Status status)
{
    s_master.status = status;
    s_master.phase = MODBUS_MASTER_PHASE_IDLE;
    s_master.rx_length = 0U;
}

static void modbus_master_process_response(void)
{
    uint16_t received_crc;
    uint16_t calculated_crc;

    if (s_master.rx_length < 5U)
    {
        modbus_master_finish(MODBUS_MASTER_STATUS_RESPONSE_ERROR);
        return;
    }

    received_crc = (uint16_t)s_master.rx_frame[s_master.rx_length - 2U] |
                   ((uint16_t)s_master.rx_frame[s_master.rx_length - 1U] << 8U);
    calculated_crc = modbus_master_crc16(s_master.rx_frame,
                                         (uint16_t)(s_master.rx_length - 2U));
    if (received_crc != calculated_crc)
    {
        modbus_master_finish(MODBUS_MASTER_STATUS_CRC_ERROR);
        return;
    }

    if (s_master.rx_frame[0] != s_master.slave_address)
    {
        modbus_master_finish(MODBUS_MASTER_STATUS_RESPONSE_ERROR);
        return;
    }

    if (s_master.rx_frame[1] == (uint8_t)(s_master.function | 0x80U))
    {
        if (s_master.rx_length != 5U)
        {
            modbus_master_finish(MODBUS_MASTER_STATUS_RESPONSE_ERROR);
            return;
        }

        s_master.exception_code = s_master.rx_frame[2];
        modbus_master_finish(MODBUS_MASTER_STATUS_EXCEPTION);
        return;
    }

    if (s_master.rx_frame[1] != s_master.function)
    {
        modbus_master_finish(MODBUS_MASTER_STATUS_RESPONSE_ERROR);
        return;
    }

    if (s_master.function == MODBUS_FUNCTION_READ_HOLDING)
    {
        uint16_t index;
        uint16_t expected_length = (uint16_t)(5U + (s_master.quantity * 2U));

        if ((s_master.rx_length != expected_length) ||
            (s_master.rx_frame[2] != (uint8_t)(s_master.quantity * 2U)))
        {
            modbus_master_finish(MODBUS_MASTER_STATUS_RESPONSE_ERROR);
            return;
        }

        for (index = 0U; index < s_master.quantity; index++)
        {
            s_master.read_values[index] =
                ((uint16_t)s_master.rx_frame[3U + (index * 2U)] << 8U) |
                s_master.rx_frame[4U + (index * 2U)];
        }
        s_master.read_count = s_master.quantity;
        s_master.read_pending = 1U;
        modbus_master_finish(MODBUS_MASTER_STATUS_SUCCESS);
        return;
    }

    if ((s_master.function == MODBUS_FUNCTION_WRITE_SINGLE) ||
        (s_master.function == MODBUS_FUNCTION_WRITE_MULTIPLE))
    {
        if ((s_master.rx_length != 8U) ||
            (memcmp(s_master.rx_frame, s_master.request_echo,
                    sizeof(s_master.request_echo)) != 0))
        {
            modbus_master_finish(MODBUS_MASTER_STATUS_RESPONSE_ERROR);
            return;
        }

        modbus_master_finish(MODBUS_MASTER_STATUS_SUCCESS);
        return;
    }

    modbus_master_finish(MODBUS_MASTER_STATUS_RESPONSE_ERROR);
}

void modbus_master_init(void)
{
    memset(&s_master, 0, sizeof(s_master));
    s_master.phase = MODBUS_MASTER_PHASE_IDLE;
    s_master.status = MODBUS_MASTER_STATUS_IDLE;
    usart_clear_rx();
}

void modbus_master_poll(void)
{
    uint8_t chunk[MODBUS_MASTER_RX_CHUNK_SIZE];
    uint16_t length;
    uint32_t now;

    if (s_master.phase != MODBUS_MASTER_PHASE_WAIT_RESPONSE)
    {
        return;
    }

    length = usart_read(chunk, sizeof(chunk));
    now = HAL_GetTick();
    if (length != 0U)
    {
        uint16_t free_length =
            (uint16_t)(MODBUS_MASTER_MAX_ADU_SIZE - s_master.rx_length);

        if (length > free_length)
        {
            modbus_master_finish(MODBUS_MASTER_STATUS_RESPONSE_ERROR);
            return;
        }

        memcpy(&s_master.rx_frame[s_master.rx_length], chunk, length);
        s_master.rx_length = (uint16_t)(s_master.rx_length + length);
        s_master.last_byte_tick = now;
    }

    if ((s_master.rx_length != 0U) &&
        ((uint32_t)(now - s_master.last_byte_tick) >= MODBUS_MASTER_FRAME_GAP_MS))
    {
        modbus_master_process_response();
        return;
    }

    if ((uint32_t)(now - s_master.request_tick) >= MODBUS_MASTER_RESPONSE_TIMEOUT_MS)
    {
        modbus_master_finish(MODBUS_MASTER_STATUS_TIMEOUT);
    }
}

uint8_t modbus_master_read_holding(uint8_t slave_address,
                                   uint16_t start_address,
                                   uint16_t quantity)
{
    uint8_t frame[8];

    if ((modbus_master_slave_is_valid(slave_address) == 0U) ||
        (quantity == 0U) ||
        (quantity > MODBUS_MASTER_MAX_READ_REGISTERS))
    {
        return 0U;
    }

    frame[0] = slave_address;
    frame[1] = MODBUS_FUNCTION_READ_HOLDING;
    frame[2] = (uint8_t)(start_address >> 8U);
    frame[3] = (uint8_t)start_address;
    frame[4] = (uint8_t)(quantity >> 8U);
    frame[5] = (uint8_t)quantity;
    (void)modbus_master_append_crc(frame, 6U);
    return modbus_master_begin_request(frame, sizeof(frame), slave_address,
                                       MODBUS_FUNCTION_READ_HOLDING,
                                       start_address, quantity);
}

uint8_t modbus_master_write_single(uint8_t slave_address,
                                   uint16_t register_address,
                                   uint16_t value)
{
    uint8_t frame[8];

    if (modbus_master_slave_is_valid(slave_address) == 0U)
    {
        return 0U;
    }

    frame[0] = slave_address;
    frame[1] = MODBUS_FUNCTION_WRITE_SINGLE;
    frame[2] = (uint8_t)(register_address >> 8U);
    frame[3] = (uint8_t)register_address;
    frame[4] = (uint8_t)(value >> 8U);
    frame[5] = (uint8_t)value;
    (void)modbus_master_append_crc(frame, 6U);
    return modbus_master_begin_request(frame, sizeof(frame), slave_address,
                                       MODBUS_FUNCTION_WRITE_SINGLE,
                                       register_address, 1U);
}

uint8_t modbus_master_write_multiple(uint8_t slave_address,
                                     uint16_t start_address,
                                     const uint16_t *values,
                                     uint16_t quantity)
{
    uint8_t frame[9U + (MODBUS_MASTER_MAX_WRITE_REGISTERS * 2U)];
    uint16_t index;
    uint16_t length;

    if ((modbus_master_slave_is_valid(slave_address) == 0U) ||
        (values == NULL) || (quantity == 0U) ||
        (quantity > MODBUS_MASTER_MAX_WRITE_REGISTERS))
    {
        return 0U;
    }

    frame[0] = slave_address;
    frame[1] = MODBUS_FUNCTION_WRITE_MULTIPLE;
    frame[2] = (uint8_t)(start_address >> 8U);
    frame[3] = (uint8_t)start_address;
    frame[4] = (uint8_t)(quantity >> 8U);
    frame[5] = (uint8_t)quantity;
    frame[6] = (uint8_t)(quantity * 2U);
    for (index = 0U; index < quantity; index++)
    {
        frame[7U + (index * 2U)] = (uint8_t)(values[index] >> 8U);
        frame[8U + (index * 2U)] = (uint8_t)values[index];
    }

    length = modbus_master_append_crc(frame,
                                      (uint16_t)(7U + (quantity * 2U)));
    return modbus_master_begin_request(frame, length, slave_address,
                                       MODBUS_FUNCTION_WRITE_MULTIPLE,
                                       start_address, quantity);
}

uint8_t modbus_master_take_read_result(uint8_t *slave_address,
                                       uint16_t *start_address,
                                       uint16_t *values,
                                       uint16_t value_capacity,
                                       uint16_t *value_count)
{
    uint16_t index;

    if ((s_master.read_pending == 0U) || (values == NULL) ||
        (value_count == NULL) || (value_capacity < s_master.read_count))
    {
        return 0U;
    }

    for (index = 0U; index < s_master.read_count; index++)
    {
        values[index] = s_master.read_values[index];
    }
    if (slave_address != NULL)
    {
        *slave_address = s_master.slave_address;
    }
    if (start_address != NULL)
    {
        *start_address = s_master.start_address;
    }
    *value_count = s_master.read_count;
    s_master.read_pending = 0U;
    return 1U;
}

ModbusMaster_Status modbus_master_get_status(void)
{
    return s_master.status;
}

uint8_t modbus_master_get_exception(void)
{
    return s_master.exception_code;
}

uint8_t modbus_master_is_busy(void)
{
    return (s_master.phase == MODBUS_MASTER_PHASE_WAIT_RESPONSE) ? 1U : 0U;
}
