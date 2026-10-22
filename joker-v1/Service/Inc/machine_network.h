/* AUTO-FILE-DOC
 * 文件职责：RS485 主从机网络。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef MACHINE_NETWORK_H
#define MACHINE_NETWORK_H

#include <stdint.h>

#include "./MODBUS/modbus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MACHINE_NETWORK_MASTER_ID      0U
#define MACHINE_NETWORK_FIRST_SLAVE_ID 1U
#define MACHINE_NETWORK_LAST_SLAVE_ID  7U
#define MACHINE_NETWORK_NODE_COUNT     8U

typedef enum
{
    MACHINE_NETWORK_ROLE_DISABLED = 0U,
    MACHINE_NETWORK_ROLE_MASTER,
    MACHINE_NETWORK_ROLE_SLAVE
} MachineNetwork_Role;

void MachineNetwork_Init(void);
void MachineNetwork_Task(void);

uint8_t MachineNetwork_GetNodeId(void);
MachineNetwork_Role MachineNetwork_GetRole(void);

uint8_t MachineNetwork_MasterRead(uint8_t slave_id,
                                  uint16_t start_register,
                                  uint16_t quantity);
uint8_t MachineNetwork_MasterWrite(uint8_t slave_id,
                                   uint16_t register_address,
                                   uint16_t value);
uint8_t MachineNetwork_MasterWriteMultiple(uint8_t slave_id,
                                           uint16_t start_register,
                                           const uint16_t *values,
                                           uint16_t quantity);
uint8_t MachineNetwork_TakeReadResult(uint8_t *slave_id,
                                      uint16_t *start_register,
                                      uint16_t *values,
                                      uint16_t value_capacity,
                                      uint16_t *value_count);
Modbus_Status MachineNetwork_GetMasterStatus(void);

uint16_t MachineNetwork_GetLocalRegister(uint16_t address);
void MachineNetwork_SetLocalRegister(uint16_t address, uint16_t value);

#ifdef __cplusplus
}
#endif

#endif
