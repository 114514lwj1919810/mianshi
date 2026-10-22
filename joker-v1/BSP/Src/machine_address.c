/* AUTO-FILE-DOC
 * 文件职责：机器地址读取。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "machine_address.h"

#include "board_pinout.h"
#include "eeprom.h"

#include <stddef.h>
#include <string.h>

#define MACHINE_ADDRESS_SLOT0_ADDR       0x7080U
#define MACHINE_ADDRESS_SLOT1_ADDR       0x70C0U
#define MACHINE_ADDRESS_MAGIC            0x31524441UL
#define MACHINE_ADDRESS_VERSION          2U
#define MACHINE_ADDRESS_LEGACY_VERSION   1U

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint16_t length;
    uint32_t sequence;
    uint8_t network_enabled;
    uint8_t node_id;
    uint8_t reserved[14];
    uint32_t checksum;
} MachineAddress_Record;

typedef char MachineAddress_RecordSizeMustBe32[
    (sizeof(MachineAddress_Record) == 32U) ? 1 : -1];

static uint8_t s_network_enabled = 0U;
static uint8_t s_node_id = MACHINE_ADDRESS_DEFAULT_ID;
static uint8_t s_active_slot = 0U;
static uint8_t s_have_record = 0U;
static uint32_t s_sequence = 0U;

/**
 * @brief 机器地址读取：检查（对应符号 MachineAddress_Checksum）。
 * @调用关系 直接调用者：machine_address.c:MachineAddress_RecordValid、machine_address.c:MachineAddress_SetModeValue。
 * @局部状态 主要临时变量：copy、bytes、value、index；均只在本次调用中有效。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t MachineAddress_Checksum(const MachineAddress_Record *record)
{
    MachineAddress_Record copy;
    const uint8_t *bytes;
    uint32_t value;
    uint16_t index;

    copy = *record;
    copy.checksum = 0U;
    bytes = (const uint8_t *)&copy;
    value = 2166136261UL;
    for (index = 0U; index < (uint16_t)sizeof(copy); index++)
    {
        value ^= bytes[index];
        value *= 16777619UL;
    }
    return value;
}

/**
 * @brief 机器地址读取：校验（对应符号 MachineAddress_RecordValid）。
 * @调用关系 直接调用者：machine_address.c:MachineAddress_ReadSlot。
 * @param record 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t MachineAddress_RecordValid(const MachineAddress_Record *record)
{
    if ((record->magic != MACHINE_ADDRESS_MAGIC) ||
        (record->length != (uint16_t)sizeof(*record)))
    {
        return 0U;
    }

    if (record->version == MACHINE_ADDRESS_LEGACY_VERSION)
    {
        /* v1 的第一个字节是 0~7 软件地址或 8 拨码模式。 */
        if (record->network_enabled > 8U)
        {
            return 0U;
        }
    }
    else if ((record->version != MACHINE_ADDRESS_VERSION) ||
             (record->network_enabled > 1U) ||
             (record->node_id > MACHINE_ADDRESS_MAX_ID))
    {
        return 0U;
    }
    return (record->checksum == MachineAddress_Checksum(record)) ? 1U : 0U;
}

/**
 * @brief 机器地址读取：读取（对应符号 MachineAddress_ReadSlot）。
 * @调用关系 直接调用者：machine_address.c:MachineAddress_Load、machine_address.c:MachineAddress_SetModeValue。
 * @param address 按值传入；函数内部修改不会回写调用方变量。
 * @param record 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t MachineAddress_ReadSlot(uint16_t address,
                                       MachineAddress_Record *record)
{
    if (Eeprom_Read(address,
                    (uint8_t *)record,
                    (uint16_t)sizeof(*record)) == 0U)
    {
        return 0U;
    }
    return MachineAddress_RecordValid(record);
}

/**
 * @brief 机器地址读取：初始化（对应符号 MachineAddress_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init、machine_network.c:MachineNetwork_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void MachineAddress_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    __HAL_RCC_GPIOC_CLK_ENABLE();

    gpio.Pin = BOARD_ADDR_BIT0_PIN |
               BOARD_ADDR_BIT1_PIN |
               BOARD_ADDR_BIT2_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &gpio);

    s_network_enabled = 0U;
    s_node_id = MACHINE_ADDRESS_DEFAULT_ID;
    s_active_slot = 0U;
    s_have_record = 0U;
    s_sequence = 0U;
}

/**
 * @brief 机器地址读取：读取（对应符号 MachineAddress_ReadDip）。
 * @调用关系 直接调用者：machine_address.c:MachineAddress_Read。
 * @局部状态 主要临时变量：address；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineAddress_ReadDip(void)
{
    uint8_t address = 0U;

    /* SW2 ON connects the pin to GND, therefore RESET means logical 1. */
    if (HAL_GPIO_ReadPin(BOARD_ADDR_BIT0_PORT,
                        BOARD_ADDR_BIT0_PIN) == GPIO_PIN_RESET)
    {
        address |= 0x01U;
    }
    if (HAL_GPIO_ReadPin(BOARD_ADDR_BIT1_PORT,
                        BOARD_ADDR_BIT1_PIN) == GPIO_PIN_RESET)
    {
        address |= 0x02U;
    }
    if (HAL_GPIO_ReadPin(BOARD_ADDR_BIT2_PORT,
                        BOARD_ADDR_BIT2_PIN) == GPIO_PIN_RESET)
    {
        address |= 0x04U;
    }
    return address;
}

/**
 * @brief 机器地址读取：执行内部处理（对应符号 MachineAddress_Load）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：slot0、slot1、selected、valid0、valid1；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void MachineAddress_Load(void)
{
    MachineAddress_Record slot0;
    MachineAddress_Record slot1;
    const MachineAddress_Record *selected;
    uint8_t valid0;
    uint8_t valid1;

    valid0 = MachineAddress_ReadSlot(MACHINE_ADDRESS_SLOT0_ADDR, &slot0);
    valid1 = MachineAddress_ReadSlot(MACHINE_ADDRESS_SLOT1_ADDR, &slot1);
    selected = (const MachineAddress_Record *)0;

    if ((valid0 != 0U) && (valid1 != 0U))
    {
        if ((int32_t)(slot1.sequence - slot0.sequence) > 0)
        {
            selected = &slot1;
            s_active_slot = 1U;
        }
        else
        {
            selected = &slot0;
            s_active_slot = 0U;
        }
    }
    else if (valid0 != 0U)
    {
        selected = &slot0;
        s_active_slot = 0U;
    }
    else if (valid1 != 0U)
    {
        selected = &slot1;
        s_active_slot = 1U;
    }

    if (selected != (const MachineAddress_Record *)0)
    {
        s_sequence = selected->sequence;
        s_have_record = 1U;

        if (selected->version == MACHINE_ADDRESS_VERSION)
        {
            s_network_enabled = selected->network_enabled;
            s_node_id = selected->node_id;
        }
        else
        {
            /* 旧固件配置自动迁移：原软件地址继续联网，原拨码模式读取一次拨码。 */
            s_network_enabled = 1U;
            s_node_id = (selected->network_enabled <= MACHINE_ADDRESS_MAX_ID) ?
                        selected->network_enabled : MachineAddress_ReadDip();
            (void)MachineAddress_SetConfig(s_network_enabled, s_node_id);
        }
    }
    else
    {
        /* 新板首次启动默认单机、0号；拨码开关不再参与运行配置。 */
        s_network_enabled = 0U;
        s_node_id = MACHINE_ADDRESS_DEFAULT_ID;
        s_sequence = 0U;
        s_have_record = 0U;
        (void)MachineAddress_SetConfig(s_network_enabled, s_node_id);
    }
}

/**
 * @brief 机器地址读取：读取（对应符号 MachineAddress_Read）。
 * @调用关系 直接调用者：app_main.c:App_Init、machine_network.c:MachineNetwork_Init。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineAddress_Read(void)
{
    return s_node_id;
}

uint8_t MachineAddress_IsNetworkEnabled(void)
{
    return s_network_enabled;
}

uint8_t MachineAddress_GetNodeId(void)
{
    return s_node_id;
}

/**
 * @brief 机器地址读取：读取（对应符号 MachineAddress_GetModeValue）。
 * @调用关系 直接调用者：setting.c:Setting_CopyRecordToRam、setting.c:Setting_Init、setting.c:Setting_RestoreDefault、setting.c:Setting_SetValue。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineAddress_GetModeValue(void)
{
    return s_node_id;
}

/**
 * @brief 机器地址读取：设置（对应符号 MachineAddress_SetModeValue）。
 * @调用关系 直接调用者：setting.c:Setting_SetValue。
 * @局部状态 主要临时变量：record、verify、target_slot、address；均只在本次调用中有效。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t MachineAddress_SetModeValue(uint8_t value)
{
    return MachineAddress_SetConfig(s_network_enabled, value);
}

uint8_t MachineAddress_SetConfig(uint8_t network_enabled, uint8_t node_id)
{
    MachineAddress_Record record;
    MachineAddress_Record verify;
    uint8_t target_slot;
    uint16_t address;

    network_enabled = (network_enabled != 0U) ? 1U : 0U;
    if (node_id > MACHINE_ADDRESS_MAX_ID)
    {
        return 0U;
    }
    if ((s_have_record != 0U) &&
        (network_enabled == s_network_enabled) &&
        (node_id == s_node_id))
    {
        return 1U;
    }

    memset(&record, 0, sizeof(record));
    record.magic = MACHINE_ADDRESS_MAGIC;
    record.version = MACHINE_ADDRESS_VERSION;
    record.length = (uint16_t)sizeof(record);
    record.sequence = s_sequence + 1UL;
    record.network_enabled = network_enabled;
    record.node_id = node_id;
    record.checksum = MachineAddress_Checksum(&record);

    target_slot = (s_have_record != 0U) ?
                  (uint8_t)(s_active_slot ^ 1U) : 0U;
    address = (target_slot == 0U) ?
              MACHINE_ADDRESS_SLOT0_ADDR : MACHINE_ADDRESS_SLOT1_ADDR;
    if ((Eeprom_Write(address,
                      (const uint8_t *)&record,
                      (uint16_t)sizeof(record)) == 0U) ||
        (MachineAddress_ReadSlot(address, &verify) == 0U) ||
        (verify.sequence != record.sequence))
    {
        return 0U;
    }

    s_network_enabled = network_enabled;
    s_node_id = node_id;
    s_sequence = record.sequence;
    s_active_slot = target_slot;
    s_have_record = 1U;
    return 1U;
}
