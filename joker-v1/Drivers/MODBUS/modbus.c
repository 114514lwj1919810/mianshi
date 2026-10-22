#include "./MODBUS/modbus.h"

#include "./RS485/rs485.h"
#include "./SYSTEM/sys/sys.h"

#include <stddef.h>
#include <string.h>

#define MODBUS_BROADCAST_ADDRESS          0U
#define MODBUS_MIN_SLAVE_ADDRESS          1U
#define MODBUS_MAX_SLAVE_ADDRESS          247U
#define MODBUS_MAX_ADU_SIZE               256U
#define MODBUS_RX_CHUNK_SIZE              64U
#define MODBUS_FRAME_GAP_MS               4U
#define MODBUS_RESPONSE_TIMEOUT_MS        60U

#define MODBUS_FUNCTION_READ_HOLDING      0x03U
#define MODBUS_FUNCTION_WRITE_SINGLE      0x06U
#define MODBUS_FUNCTION_WRITE_MULTIPLE    0x10U

#define MODBUS_EXCEPTION_ILLEGAL_FUNCTION 0x01U
#define MODBUS_EXCEPTION_ILLEGAL_ADDRESS  0x02U
#define MODBUS_EXCEPTION_ILLEGAL_VALUE    0x03U

typedef enum
{
    MODBUS_MASTER_IDLE = 0U,
    MODBUS_MASTER_WAIT_RESPONSE
} Modbus_MasterPhase;

typedef struct
{
    uint8_t frame[MODBUS_MAX_ADU_SIZE];
    uint16_t length;
    uint32_t last_byte_ms;
    uint8_t overflow;
} Modbus_RxContext;

typedef struct
{
    Modbus_MasterPhase phase;
    Modbus_Status status;
    uint8_t slave_id;
    uint8_t function;
    uint8_t request_echo[6];
    uint16_t start_register;
    uint16_t quantity;
    uint32_t request_ms;
    uint16_t values[MODBUS_MAX_READ_REGISTERS];
    uint16_t value_count;
    uint8_t result_pending;
} Modbus_MasterContext;

static uint8_t s_node_id;
static Modbus_Role s_role;
static uint16_t s_holding_registers[MODBUS_HOLDING_REGISTER_COUNT];
static Modbus_RxContext s_rx;
static Modbus_MasterContext s_master;

static uint8_t Modbus_IsSlaveIdValid(uint8_t slave_id)
{
    return ((slave_id >= MODBUS_MIN_SLAVE_ADDRESS) &&
            (slave_id <= MODBUS_MAX_SLAVE_ADDRESS)) ? 1U : 0U;
}

static uint16_t Modbus_Crc16(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFFU;
    uint16_t index;
    uint8_t bit;

    for (index = 0U; index < length; index++)
    {
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

static uint16_t Modbus_AppendCrc(uint8_t *frame, uint16_t length)
{
    uint16_t crc = Modbus_Crc16(frame, length);

    frame[length] = (uint8_t)crc;
    frame[length + 1U] = (uint8_t)(crc >> 8U);
    return (uint16_t)(length + 2U);
}

static void Modbus_ResetRx(void)
{
    s_rx.length = 0U;
    s_rx.last_byte_ms = HAL_GetTick();
    s_rx.overflow = 0U;
}

static void Modbus_CollectRx(void)
{
    uint8_t data[MODBUS_RX_CHUNK_SIZE];
    uint16_t length;
    uint16_t free_length;

    length = Rs485_Read(data, sizeof(data));
    if (length == 0U)
    {
        return;
    }

    if (s_rx.overflow == 0U)
    {
        free_length = (uint16_t)(MODBUS_MAX_ADU_SIZE - s_rx.length);
        if (length <= free_length)
        {
            memcpy(&s_rx.frame[s_rx.length], data, length);
            s_rx.length = (uint16_t)(s_rx.length + length);
        }
        else
        {
            s_rx.length = 0U;
            s_rx.overflow = 1U;
        }
    }

    s_rx.last_byte_ms = HAL_GetTick();
}

static uint8_t Modbus_IsFrameReady(uint32_t now)
{
    if ((s_rx.length == 0U) && (s_rx.overflow == 0U))
    {
        return 0U;
    }

    return ((uint32_t)(now - s_rx.last_byte_ms) >=
            MODBUS_FRAME_GAP_MS) ? 1U : 0U;
}

static uint8_t Modbus_IsRangeValid(uint16_t start, uint16_t quantity)
{
    if ((quantity == 0U) || (start >= MODBUS_HOLDING_REGISTER_COUNT))
    {
        return 0U;
    }

    return (quantity <= (MODBUS_HOLDING_REGISTER_COUNT - start)) ? 1U : 0U;
}

static uint16_t Modbus_BuildException(uint8_t address,
                                      uint8_t function,
                                      uint8_t exception,
                                      uint8_t *response,
                                      uint16_t capacity)
{
    if (capacity < 5U)
    {
        return 0U;
    }

    response[0] = address;
    response[1] = (uint8_t)(function | 0x80U);
    response[2] = exception;
    return Modbus_AppendCrc(response, 3U);
}

static uint16_t Modbus_ProcessSlaveRequest(const uint8_t *request,
                                           uint16_t request_length,
                                           uint8_t *response,
                                           uint16_t capacity)
{
    uint8_t address;
    uint8_t function;
    uint8_t broadcast;
    uint16_t received_crc;
    uint16_t start;
    uint16_t quantity;
    uint16_t index;

    if ((request == NULL) || (response == NULL) || (request_length < 4U))
    {
        return 0U;
    }

    received_crc = (uint16_t)request[request_length - 2U] |
                   ((uint16_t)request[request_length - 1U] << 8U);
    if (received_crc != Modbus_Crc16(request,
                                     (uint16_t)(request_length - 2U)))
    {
        return 0U;
    }

    address = request[0];
    if ((address != s_node_id) && (address != MODBUS_BROADCAST_ADDRESS))
    {
        return 0U;
    }

    function = request[1];
    broadcast = (address == MODBUS_BROADCAST_ADDRESS) ? 1U : 0U;

    if (function == MODBUS_FUNCTION_READ_HOLDING)
    {
        uint16_t response_length;

        if ((broadcast != 0U) || (request_length != 8U))
        {
            return (broadcast != 0U) ? 0U :
                   Modbus_BuildException(address, function,
                                         MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                         response, capacity);
        }

        start = ((uint16_t)request[2] << 8U) | request[3];
        quantity = ((uint16_t)request[4] << 8U) | request[5];
        if ((quantity == 0U) || (quantity > 125U))
        {
            return Modbus_BuildException(address, function,
                                         MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                         response, capacity);
        }
        if (Modbus_IsRangeValid(start, quantity) == 0U)
        {
            return Modbus_BuildException(address, function,
                                         MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                         response, capacity);
        }

        response_length = (uint16_t)(5U + (quantity * 2U));
        if (capacity < response_length)
        {
            return 0U;
        }

        response[0] = address;
        response[1] = function;
        response[2] = (uint8_t)(quantity * 2U);
        for (index = 0U; index < quantity; index++)
        {
            response[3U + (index * 2U)] =
                (uint8_t)(s_holding_registers[start + index] >> 8U);
            response[4U + (index * 2U)] =
                (uint8_t)s_holding_registers[start + index];
        }
        return Modbus_AppendCrc(response,
                                (uint16_t)(response_length - 2U));
    }

    if (function == MODBUS_FUNCTION_WRITE_SINGLE)
    {
        uint16_t value;

        if (request_length != 8U)
        {
            return (broadcast != 0U) ? 0U :
                   Modbus_BuildException(address, function,
                                         MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                         response, capacity);
        }

        start = ((uint16_t)request[2] << 8U) | request[3];
        value = ((uint16_t)request[4] << 8U) | request[5];
        if (start >= MODBUS_HOLDING_REGISTER_COUNT)
        {
            return (broadcast != 0U) ? 0U :
                   Modbus_BuildException(address, function,
                                         MODBUS_EXCEPTION_ILLEGAL_ADDRESS,
                                         response, capacity);
        }

        s_holding_registers[start] = value;
        if ((broadcast != 0U) || (capacity < request_length))
        {
            return 0U;
        }

        memcpy(response, request, request_length);
        return request_length;
    }

    if (function == MODBUS_FUNCTION_WRITE_MULTIPLE)
    {
        uint8_t byte_count;

        if (request_length < 9U)
        {
            return (broadcast != 0U) ? 0U :
                   Modbus_BuildException(address, function,
                                         MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                         response, capacity);
        }

        start = ((uint16_t)request[2] << 8U) | request[3];
        quantity = ((uint16_t)request[4] << 8U) | request[5];
        byte_count = request[6];
        if ((quantity > 123U) ||
            (byte_count != (uint8_t)(quantity * 2U)) ||
            (request_length != (uint16_t)(9U + byte_count)) ||
            (Modbus_IsRangeValid(start, quantity) == 0U))
        {
            return (broadcast != 0U) ? 0U :
                   Modbus_BuildException(address, function,
                                         MODBUS_EXCEPTION_ILLEGAL_VALUE,
                                         response, capacity);
        }

        for (index = 0U; index < quantity; index++)
        {
            s_holding_registers[start + index] =
                ((uint16_t)request[7U + (index * 2U)] << 8U) |
                request[8U + (index * 2U)];
        }

        if ((broadcast != 0U) || (capacity < 8U))
        {
            return 0U;
        }

        response[0] = address;
        response[1] = function;
        memcpy(&response[2], &request[2], 4U);
        return Modbus_AppendCrc(response, 6U);
    }

    return (broadcast != 0U) ? 0U :
           Modbus_BuildException(address, function,
                                 MODBUS_EXCEPTION_ILLEGAL_FUNCTION,
                                 response, capacity);
}

static void Modbus_MasterFinish(Modbus_Status status)
{
    s_master.status = status;
    s_master.phase = MODBUS_MASTER_IDLE;
    Modbus_ResetRx();
}

static void Modbus_ProcessMasterResponse(void)
{
    uint16_t received_crc;
    uint16_t expected_length;
    uint16_t index;

    if (s_rx.length < 5U)
    {
        Modbus_MasterFinish(MODBUS_STATUS_RESPONSE_ERROR);
        return;
    }

    received_crc = (uint16_t)s_rx.frame[s_rx.length - 2U] |
                   ((uint16_t)s_rx.frame[s_rx.length - 1U] << 8U);
    if (received_crc != Modbus_Crc16(s_rx.frame,
                                     (uint16_t)(s_rx.length - 2U)))
    {
        Modbus_MasterFinish(MODBUS_STATUS_CRC_ERROR);
        return;
    }

    if (s_rx.frame[0] != s_master.slave_id)
    {
        Modbus_MasterFinish(MODBUS_STATUS_RESPONSE_ERROR);
        return;
    }

    if (s_rx.frame[1] == (uint8_t)(s_master.function | 0x80U))
    {
        Modbus_MasterFinish((s_rx.length == 5U) ?
                            MODBUS_STATUS_EXCEPTION :
                            MODBUS_STATUS_RESPONSE_ERROR);
        return;
    }

    if (s_rx.frame[1] != s_master.function)
    {
        Modbus_MasterFinish(MODBUS_STATUS_RESPONSE_ERROR);
        return;
    }

    if (s_master.function == MODBUS_FUNCTION_READ_HOLDING)
    {
        expected_length = (uint16_t)(5U + (s_master.quantity * 2U));
        if ((s_rx.length != expected_length) ||
            (s_rx.frame[2] != (uint8_t)(s_master.quantity * 2U)))
        {
            Modbus_MasterFinish(MODBUS_STATUS_RESPONSE_ERROR);
            return;
        }

        for (index = 0U; index < s_master.quantity; index++)
        {
            s_master.values[index] =
                ((uint16_t)s_rx.frame[3U + (index * 2U)] << 8U) |
                s_rx.frame[4U + (index * 2U)];
        }
        s_master.value_count = s_master.quantity;
        s_master.result_pending = 1U;
        Modbus_MasterFinish(MODBUS_STATUS_SUCCESS);
        return;
    }

    if ((s_master.function == MODBUS_FUNCTION_WRITE_SINGLE) &&
        (s_rx.length == 8U) &&
        (memcmp(s_rx.frame, s_master.request_echo,
                sizeof(s_master.request_echo)) == 0))
    {
        Modbus_MasterFinish(MODBUS_STATUS_SUCCESS);
        return;
    }

    Modbus_MasterFinish(MODBUS_STATUS_RESPONSE_ERROR);
}

static uint8_t Modbus_MasterBegin(const uint8_t *frame,
                                  uint16_t length,
                                  uint8_t slave_id,
                                  uint8_t function,
                                  uint16_t start_register,
                                  uint16_t quantity)
{
    if ((s_role != MODBUS_ROLE_MASTER) || (frame == NULL) ||
        (length == 0U) || (s_master.phase != MODBUS_MASTER_IDLE) ||
        (Rs485_IsTxBusy() != 0U))
    {
        return 0U;
    }

    Rs485_ClearRx();
    Modbus_ResetRx();
    s_master.slave_id = slave_id;
    s_master.function = function;
    memcpy(s_master.request_echo, frame, sizeof(s_master.request_echo));
    s_master.start_register = start_register;
    s_master.quantity = quantity;
    s_master.value_count = 0U;
    s_master.result_pending = 0U;

    if (Rs485_Send(frame, length) == 0U)
    {
        return 0U;
    }

    s_master.request_ms = HAL_GetTick();
    s_master.status = MODBUS_STATUS_BUSY;
    s_master.phase = MODBUS_MASTER_WAIT_RESPONSE;
    return 1U;
}

void Modbus_Init(uint8_t node_id)
{
    memset(s_holding_registers, 0, sizeof(s_holding_registers));
    memset(&s_master, 0, sizeof(s_master));
    s_node_id = node_id;
    s_master.phase = MODBUS_MASTER_IDLE;
    s_master.status = MODBUS_STATUS_IDLE;

    if (node_id == MODBUS_BROADCAST_ADDRESS)
    {
        s_role = MODBUS_ROLE_MASTER;
    }
    else if (Modbus_IsSlaveIdValid(node_id) != 0U)
    {
        s_role = MODBUS_ROLE_SLAVE;
    }
    else
    {
        s_role = MODBUS_ROLE_DISABLED;
    }

    Rs485_ClearRx();
    Modbus_ResetRx();
}

void Modbus_Task(void)
{
    uint8_t response[MODBUS_MAX_ADU_SIZE];
    uint16_t response_length;
    uint32_t now;

    if (s_role == MODBUS_ROLE_DISABLED)
    {
        return;
    }

    if ((s_role == MODBUS_ROLE_MASTER) &&
        (s_master.phase != MODBUS_MASTER_WAIT_RESPONSE))
    {
        return;
    }

    Modbus_CollectRx();
    now = HAL_GetTick();

    if (Modbus_IsFrameReady(now) != 0U)
    {
        if (s_rx.overflow != 0U)
        {
            if (s_role == MODBUS_ROLE_MASTER)
            {
                Modbus_MasterFinish(MODBUS_STATUS_RESPONSE_ERROR);
            }
            else
            {
                Modbus_ResetRx();
            }
            return;
        }

        if (s_role == MODBUS_ROLE_MASTER)
        {
            Modbus_ProcessMasterResponse();
        }
        else
        {
            response_length = Modbus_ProcessSlaveRequest(
                s_rx.frame, s_rx.length, response, sizeof(response));
            Modbus_ResetRx();
            if (response_length != 0U)
            {
                (void)Rs485_Send(response, response_length);
            }
        }
        return;
    }

    if ((s_role == MODBUS_ROLE_MASTER) &&
        ((uint32_t)(now - s_master.request_ms) >=
         MODBUS_RESPONSE_TIMEOUT_MS))
    {
        Modbus_MasterFinish(MODBUS_STATUS_TIMEOUT);
    }
}

Modbus_Role Modbus_GetRole(void)
{
    return s_role;
}

uint8_t Modbus_MasterRead(uint8_t slave_id,
                          uint16_t start_register,
                          uint16_t quantity)
{
    uint8_t frame[8];

    if ((Modbus_IsSlaveIdValid(slave_id) == 0U) ||
        (quantity == 0U) || (quantity > MODBUS_MAX_READ_REGISTERS))
    {
        return 0U;
    }

    frame[0] = slave_id;
    frame[1] = MODBUS_FUNCTION_READ_HOLDING;
    frame[2] = (uint8_t)(start_register >> 8U);
    frame[3] = (uint8_t)start_register;
    frame[4] = (uint8_t)(quantity >> 8U);
    frame[5] = (uint8_t)quantity;
    (void)Modbus_AppendCrc(frame, 6U);
    return Modbus_MasterBegin(frame, sizeof(frame), slave_id,
                              MODBUS_FUNCTION_READ_HOLDING,
                              start_register, quantity);
}

uint8_t Modbus_MasterWrite(uint8_t slave_id,
                           uint16_t register_address,
                           uint16_t value)
{
    uint8_t frame[8];

    if (Modbus_IsSlaveIdValid(slave_id) == 0U)
    {
        return 0U;
    }

    frame[0] = slave_id;
    frame[1] = MODBUS_FUNCTION_WRITE_SINGLE;
    frame[2] = (uint8_t)(register_address >> 8U);
    frame[3] = (uint8_t)register_address;
    frame[4] = (uint8_t)(value >> 8U);
    frame[5] = (uint8_t)value;
    (void)Modbus_AppendCrc(frame, 6U);
    return Modbus_MasterBegin(frame, sizeof(frame), slave_id,
                              MODBUS_FUNCTION_WRITE_SINGLE,
                              register_address, 1U);
}

uint8_t Modbus_MasterWriteMultiple(uint8_t slave_id,
                                   uint16_t start_register,
                                   const uint16_t *values,
                                   uint16_t quantity)
{
    uint8_t frame[9U + (MODBUS_MAX_READ_REGISTERS * 2U)];
    uint16_t index;
    uint16_t length;

    if ((Modbus_IsSlaveIdValid(slave_id) == 0U) || (values == NULL) ||
        (quantity == 0U) || (quantity > MODBUS_MAX_READ_REGISTERS))
    {
        return 0U;
    }

    frame[0] = slave_id;
    frame[1] = MODBUS_FUNCTION_WRITE_MULTIPLE;
    frame[2] = (uint8_t)(start_register >> 8U);
    frame[3] = (uint8_t)start_register;
    frame[4] = (uint8_t)(quantity >> 8U);
    frame[5] = (uint8_t)quantity;
    frame[6] = (uint8_t)(quantity * 2U);
    for (index = 0U; index < quantity; index++)
    {
        frame[7U + (index * 2U)] = (uint8_t)(values[index] >> 8U);
        frame[8U + (index * 2U)] = (uint8_t)values[index];
    }

    length = Modbus_AppendCrc(frame, (uint16_t)(7U + (quantity * 2U)));
    return Modbus_MasterBegin(frame, length, slave_id,
                              MODBUS_FUNCTION_WRITE_MULTIPLE,
                              start_register, quantity);
}

uint8_t Modbus_MasterBroadcastWrite(uint16_t register_address,
                                    uint16_t value)
{
    uint8_t frame[8];

    if ((s_role != MODBUS_ROLE_MASTER) ||
        (s_master.phase != MODBUS_MASTER_IDLE) ||
        (Rs485_IsTxBusy() != 0U))
    {
        return 0U;
    }

    frame[0] = MODBUS_BROADCAST_ADDRESS;
    frame[1] = MODBUS_FUNCTION_WRITE_SINGLE;
    frame[2] = (uint8_t)(register_address >> 8U);
    frame[3] = (uint8_t)register_address;
    frame[4] = (uint8_t)(value >> 8U);
    frame[5] = (uint8_t)value;
    (void)Modbus_AppendCrc(frame, 6U);

    Rs485_ClearRx();
    Modbus_ResetRx();
    if (Rs485_Send(frame, sizeof(frame)) == 0U)
    {
        return 0U;
    }

    s_master.status = MODBUS_STATUS_SUCCESS;
    return 1U;
}

uint8_t Modbus_MasterTakeReadResult(uint8_t *slave_id,
                                    uint16_t *start_register,
                                    uint16_t *values,
                                    uint16_t value_capacity,
                                    uint16_t *value_count)
{
    if ((s_master.result_pending == 0U) || (values == NULL) ||
        (value_count == NULL) ||
        (value_capacity < s_master.value_count))
    {
        return 0U;
    }

    memcpy(values, s_master.values,
           (size_t)s_master.value_count * sizeof(values[0]));
    if (slave_id != NULL)
    {
        *slave_id = s_master.slave_id;
    }
    if (start_register != NULL)
    {
        *start_register = s_master.start_register;
    }
    *value_count = s_master.value_count;
    s_master.result_pending = 0U;
    return 1U;
}

Modbus_Status Modbus_MasterGetStatus(void)
{
    return s_master.status;
}

uint16_t Modbus_GetRegister(uint16_t address)
{
    return (address < MODBUS_HOLDING_REGISTER_COUNT) ?
           s_holding_registers[address] : 0U;
}

void Modbus_SetRegister(uint16_t address, uint16_t value)
{
    if (address < MODBUS_HOLDING_REGISTER_COUNT)
    {
        s_holding_registers[address] = value;
    }
}
