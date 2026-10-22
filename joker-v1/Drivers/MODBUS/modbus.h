#ifndef MODBUS_H
#define MODBUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MODBUS_HOLDING_REGISTER_COUNT  16U
#define MODBUS_MAX_READ_REGISTERS      16U

typedef enum
{
    MODBUS_ROLE_DISABLED = 0U,
    MODBUS_ROLE_MASTER,
    MODBUS_ROLE_SLAVE
} Modbus_Role;

typedef enum
{
    MODBUS_STATUS_IDLE = 0U,
    MODBUS_STATUS_BUSY,
    MODBUS_STATUS_SUCCESS,
    MODBUS_STATUS_TIMEOUT,
    MODBUS_STATUS_CRC_ERROR,
    MODBUS_STATUS_RESPONSE_ERROR,
    MODBUS_STATUS_EXCEPTION
} Modbus_Status;

void Modbus_Init(uint8_t node_id);
void Modbus_Task(void);

Modbus_Role Modbus_GetRole(void);

uint8_t Modbus_MasterRead(uint8_t slave_id,
                          uint16_t start_register,
                          uint16_t quantity);
uint8_t Modbus_MasterWrite(uint8_t slave_id,
                           uint16_t register_address,
                           uint16_t value);
uint8_t Modbus_MasterWriteMultiple(uint8_t slave_id,
                                   uint16_t start_register,
                                   const uint16_t *values,
                                   uint16_t quantity);
uint8_t Modbus_MasterBroadcastWrite(uint16_t register_address,
                                    uint16_t value);
uint8_t Modbus_MasterTakeReadResult(uint8_t *slave_id,
                                    uint16_t *start_register,
                                    uint16_t *values,
                                    uint16_t value_capacity,
                                    uint16_t *value_count);
Modbus_Status Modbus_MasterGetStatus(void);

uint16_t Modbus_GetRegister(uint16_t address);
void Modbus_SetRegister(uint16_t address, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
