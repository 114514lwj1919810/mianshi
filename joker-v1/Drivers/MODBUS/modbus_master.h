#ifndef MODBUS_MASTER_H
#define MODBUS_MASTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_MASTER_MAX_READ_REGISTERS  16U
#define MODBUS_MASTER_MAX_WRITE_REGISTERS 16U

typedef enum
{
    MODBUS_MASTER_STATUS_IDLE = 0U,
    MODBUS_MASTER_STATUS_BUSY,
    MODBUS_MASTER_STATUS_SUCCESS,
    MODBUS_MASTER_STATUS_TIMEOUT,
    MODBUS_MASTER_STATUS_CRC_ERROR,
    MODBUS_MASTER_STATUS_RESPONSE_ERROR,
    MODBUS_MASTER_STATUS_EXCEPTION
} ModbusMaster_Status;

void modbus_master_init(void);
void modbus_master_poll(void);

uint8_t modbus_master_read_holding(uint8_t slave_address,
                                   uint16_t start_address,
                                   uint16_t quantity);
uint8_t modbus_master_write_single(uint8_t slave_address,
                                   uint16_t register_address,
                                   uint16_t value);
uint8_t modbus_master_write_multiple(uint8_t slave_address,
                                     uint16_t start_address,
                                     const uint16_t *values,
                                     uint16_t quantity);

uint8_t modbus_master_take_read_result(uint8_t *slave_address,
                                       uint16_t *start_address,
                                       uint16_t *values,
                                       uint16_t value_capacity,
                                       uint16_t *value_count);
ModbusMaster_Status modbus_master_get_status(void);
uint8_t modbus_master_get_exception(void);
uint8_t modbus_master_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif
