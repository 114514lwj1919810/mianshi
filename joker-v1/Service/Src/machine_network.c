/* AUTO-FILE-DOC
 * 文件职责：RS485 主从机网络。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "machine_network.h"

#include "machine_address.h"
#include "./MODBUS/modbus.h"

static uint8_t s_node_id = MACHINE_ADDRESS_DEFAULT_ID;
static MachineNetwork_Role s_role = MACHINE_NETWORK_ROLE_DISABLED;

/**
 * @brief RS485 主从机网络：校验（对应符号 MachineNetwork_IsSlaveIdValid）。
 * @调用关系 直接调用者：machine_network.c:MachineNetwork_Init、machine_network.c:MachineNetwork_MasterRead、machine_network.c:MachineNetwork_MasterWrite、machine_network.c:MachineNetwork_MasterWriteMultiple。
 * @param slave_id 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t MachineNetwork_IsSlaveIdValid(uint8_t slave_id)
{
    return ((slave_id >= MACHINE_NETWORK_FIRST_SLAVE_ID) &&
            (slave_id <= MACHINE_NETWORK_LAST_SLAVE_ID)) ? 1U : 0U;
}

/**
 * @brief RS485 主从机网络：初始化（对应符号 MachineNetwork_Init）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void MachineNetwork_Init(void)
{
    MachineAddress_Init();
    s_node_id = MachineAddress_Read();

    Modbus_Init(s_node_id);

    if (s_node_id == MACHINE_NETWORK_MASTER_ID)
    {
        s_role = MACHINE_NETWORK_ROLE_MASTER;
    }
    else if (MachineNetwork_IsSlaveIdValid(s_node_id) != 0U)
    {
        s_role = MACHINE_NETWORK_ROLE_SLAVE;
    }
    else
    {
        s_role = MACHINE_NETWORK_ROLE_DISABLED;
    }
}

/**
 * @brief RS485 主从机网络：周期处理（对应符号 MachineNetwork_Task）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void MachineNetwork_Task(void)
{
    if (s_role != MACHINE_NETWORK_ROLE_DISABLED)
    {
        Modbus_Task();
    }
}

/**
 * @brief RS485 主从机网络：读取（对应符号 MachineNetwork_GetNodeId）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineNetwork_GetNodeId(void)
{
    return s_node_id;
}

/**
 * @brief RS485 主从机网络：读取（对应符号 MachineNetwork_GetRole）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
MachineNetwork_Role MachineNetwork_GetRole(void)
{
    return s_role;
}

/**
 * @brief RS485 主从机网络：读取（对应符号 MachineNetwork_MasterRead）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param slave_id 按值传入；函数内部修改不会回写调用方变量。
 * @param start_register 按值传入；函数内部修改不会回写调用方变量。
 * @param quantity 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineNetwork_MasterRead(uint8_t slave_id,
                                  uint16_t start_register,
                                  uint16_t quantity)
{
    if ((s_role != MACHINE_NETWORK_ROLE_MASTER) ||
        (MachineNetwork_IsSlaveIdValid(slave_id) == 0U))
    {
        return 0U;
    }

    return Modbus_MasterRead(slave_id, start_register, quantity);
}

/**
 * @brief RS485 主从机网络：写入（对应符号 MachineNetwork_MasterWrite）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param slave_id 按值传入；函数内部修改不会回写调用方变量。
 * @param register_address 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineNetwork_MasterWrite(uint8_t slave_id,
                                   uint16_t register_address,
                                   uint16_t value)
{
    if ((s_role != MACHINE_NETWORK_ROLE_MASTER) ||
        (MachineNetwork_IsSlaveIdValid(slave_id) == 0U))
    {
        return 0U;
    }

    return Modbus_MasterWrite(slave_id, register_address, value);
}

/**
 * @brief RS485 主从机网络：写入（对应符号 MachineNetwork_MasterWriteMultiple）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param slave_id 按值传入；函数内部修改不会回写调用方变量。
 * @param start_register 按值传入；函数内部修改不会回写调用方变量。
 * @param values 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param quantity 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineNetwork_MasterWriteMultiple(uint8_t slave_id,
                                           uint16_t start_register,
                                           const uint16_t *values,
                                           uint16_t quantity)
{
    if ((s_role != MACHINE_NETWORK_ROLE_MASTER) ||
        (MachineNetwork_IsSlaveIdValid(slave_id) == 0U))
    {
        return 0U;
    }

    return Modbus_MasterWriteMultiple(slave_id, start_register,
                                      values, quantity);
}

/**
 * @brief RS485 主从机网络：读取并清除一次性事件（对应符号 MachineNetwork_TakeReadResult）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param slave_id 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param start_register 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param values 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param value_capacity 按值传入；函数内部修改不会回写调用方变量。
 * @param value_count 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineNetwork_TakeReadResult(uint8_t *slave_id,
                                      uint16_t *start_register,
                                      uint16_t *values,
                                      uint16_t value_capacity,
                                      uint16_t *value_count)
{
    if (s_role != MACHINE_NETWORK_ROLE_MASTER)
    {
        return 0U;
    }

    return Modbus_MasterTakeReadResult(slave_id, start_register,
                                       values, value_capacity,
                                       value_count);
}

/**
 * @brief RS485 主从机网络：读取（对应符号 MachineNetwork_GetMasterStatus）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
Modbus_Status MachineNetwork_GetMasterStatus(void)
{
    if (s_role != MACHINE_NETWORK_ROLE_MASTER)
    {
        return MODBUS_STATUS_IDLE;
    }

    return Modbus_MasterGetStatus();
}

/**
 * @brief RS485 主从机网络：读取（对应符号 MachineNetwork_GetLocalRegister）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t MachineNetwork_GetLocalRegister(uint16_t address)
{
    return Modbus_GetRegister(address);
}

/**
 * @brief RS485 主从机网络：设置（对应符号 MachineNetwork_SetLocalRegister）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void MachineNetwork_SetLocalRegister(uint16_t address, uint16_t value)
{
    Modbus_SetRegister(address, value);
}
