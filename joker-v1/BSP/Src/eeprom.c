/* AUTO-FILE-DOC
 * 文件职责：EEPROM 读写。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "eeprom.h"

#include "iic.h"
#include <string.h>

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
static uint8_t s_eeprom_ready = 0U;
static uint8_t s_eeprom_fault;
#define EEPROM_OPERATION_ATTEMPTS 3U
volatile uint32_t g_eeprom_read_retry_count;
volatile uint32_t g_eeprom_write_retry_count;
volatile uint16_t g_eeprom_last_failed_address;

uint8_t Eeprom_GetFault(void) { return s_eeprom_fault; }

void Eeprom_ProtectData(uint16_t address)
{
    if (s_eeprom_fault == 0U) s_eeprom_fault = 4U;
    g_eeprom_last_failed_address = address;
    s_eeprom_ready = 0U;
}

/**
 * @brief EEPROM 读写：校验（对应符号 Eeprom_IsRangeValid）。
 * @调用关系 直接调用者：eeprom.c:Eeprom_Read、eeprom.c:Eeprom_Write。
 * @局部状态 主要临时变量：end_addr；均只在本次调用中有效。
 * @param addr 按值传入；函数内部修改不会回写调用方变量。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Eeprom_IsRangeValid(uint16_t addr, uint16_t len)
{
    uint32_t end_addr;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (len == 0U)
    {
        return 1U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (addr >= EEPROM_SIZE_BYTES)
    {
        return 0U;
    }

    end_addr = (uint32_t)addr + (uint32_t)len - 1U;
    if (end_addr >= EEPROM_SIZE_BYTES)
    {
        return 0U;
    }

    return 1U;
}

/**
 * @brief EEPROM 读写：初始化（对应符号 Eeprom_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Eeprom_Init(void)
{
    uint8_t attempt;
    s_eeprom_fault = 0U;
    g_eeprom_read_retry_count = 0U;
    g_eeprom_write_retry_count = 0U;
    g_eeprom_last_failed_address = 0U;
    for (attempt = 0U; attempt < EEPROM_OPERATION_ATTEMPTS; attempt++)
    {
        if (Eeprom_Check() != 0U) return 1U;
        if (attempt + 1U < EEPROM_OPERATION_ATTEMPTS) (void)Iic_Init();
    }
    s_eeprom_fault = 1U;
    return 0U;
}

/**
 * @brief EEPROM 读写：读取（对应符号 Eeprom_IsReady）。
 * @调用关系 直接调用者：account.c:Account_Init、account.c:Account_SaveNow、gift_algorithm.c:GiftAlgorithm_LoadState、gift_algorithm.c:GiftAlgorithm_SaveState、jackpot.c:Jackpot_SaveNow、jackpot.c:Jackpot_Init、setting.c:Setting_Init、setting.c:Setting_SaveNow。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Eeprom_IsReady(void)
{
    return s_eeprom_ready;
}

/**
 * @brief EEPROM 读写：检查（对应符号 Eeprom_Check）。
 * @调用关系 直接调用者：eeprom.c:Eeprom_Init、eeprom.c:Eeprom_Test1、account.c:Account_Init、account.c:Account_SaveNow、jackpot.c:Jackpot_SaveNow、jackpot.c:Jackpot_Init、setting.c:Setting_Init、setting.c:Setting_SaveNow。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Eeprom_Check(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Iic_IsDeviceReady(EEPROM_IIC_ADDR_HAL,
                          EEPROM_WRITE_READY_TRIALS,
                          EEPROM_WRITE_READY_TIMEOUT_MS) != 0U)
    {
        s_eeprom_ready = 1U;
        return 1U;
    }

    s_eeprom_ready = 0U;
    return 0U;
}

/**
 * @brief EEPROM 读写：读取（对应符号 Eeprom_Read）。
 * @调用关系 直接调用者：eeprom.c:Eeprom_Test2、eeprom.c:Eeprom_Test3、machine_address.c:MachineAddress_ReadSlot、account.c:Account_ReadGiftSlot、account.c:Account_ReadOutputSlot、account.c:Account_ReadTicketV2Slot、account.c:Account_ReadTicketLegacySlot、account.c:Account_ReadSlot。
 * @param addr 按值传入；函数内部修改不会回写调用方变量。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Eeprom_Read(uint16_t addr, uint8_t *data, uint16_t len)
{
    uint8_t attempt;
    if ((data == 0) && (len != 0U))
    {
        return 0U;
    }

    if (Eeprom_IsRangeValid(addr, len) == 0U)
    {
        return 0U;
    }

    if (len == 0U)
    {
        return 1U;
    }

    for (attempt = 0U; attempt < EEPROM_OPERATION_ATTEMPTS; attempt++)
    {
        if (Iic_MemRead(EEPROM_IIC_ADDR_HAL,
                    addr,
                    I2C_MEMADD_SIZE_16BIT,
                    data,
                    len,
                    EEPROM_IIC_TIMEOUT_MS) != 0U)
        {
            s_eeprom_ready = 1U;
            return 1U;
        }
        g_eeprom_read_retry_count++;
        if (attempt + 1U < EEPROM_OPERATION_ATTEMPTS) (void)Iic_Init();
    }
    /* A later successful ACK must not authorize overwriting default RAM data. */
    {
        s_eeprom_ready = 0U;
        if (s_eeprom_fault == 0U) s_eeprom_fault = 2U;
        g_eeprom_last_failed_address = addr;
        return 0U;
    }
}

/**
 * @brief EEPROM 读写：写入（对应符号 Eeprom_Write）。
 * @调用关系 直接调用者：machine_address.c:MachineAddress_SetModeValue、account.c:Account_SaveGiftData、account.c:Account_SaveTicketData、account.c:Account_SaveNow、gift_algorithm.c:GiftAlgorithm_SaveState、jackpot.c:Jackpot_SaveNow、machine_lock.c:MachineLock_Save、setting.c:Setting_SaveNow。
 * @局部状态 主要临时变量：remain、write_addr、page_offset、page_space、write_len、write_ptr；均只在本次调用中有效。
 * @param addr 按值传入；函数内部修改不会回写调用方变量。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Eeprom_Write(uint16_t addr, const uint8_t *data, uint16_t len)
{
    uint16_t remain;
    uint16_t write_addr;
    uint16_t page_offset;
    uint16_t page_space;
    uint16_t write_len;
    const uint8_t *write_ptr;
    uint8_t verify_page[EEPROM_PAGE_SIZE_BYTES];
    uint8_t attempt;
    uint8_t page_verified;

    if (s_eeprom_fault != 0U) return 0U;

    if ((data == 0) && (len != 0U))
    {
        return 0U;
    }

    if (Eeprom_IsRangeValid(addr, len) == 0U)
    {
        return 0U;
    }

    if (len == 0U)
    {
        return 1U;
    }

    remain = len;
    write_addr = addr;
    write_ptr = data;

    while (remain != 0U)
    {
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        page_offset = (uint16_t)(write_addr % EEPROM_PAGE_SIZE_BYTES);
        page_space = (uint16_t)(EEPROM_PAGE_SIZE_BYTES - page_offset);

        if (remain > page_space)
        {
            write_len = page_space;
        }
        else
        {
            write_len = remain;
        }

        page_verified = 0U;
        for (attempt = 0U; attempt < EEPROM_OPERATION_ATTEMPTS; attempt++)
        {
            if ((Iic_MemWrite(EEPROM_IIC_ADDR_HAL,
                         write_addr,
                         I2C_MEMADD_SIZE_16BIT,
                         write_ptr,
                         write_len,
                         EEPROM_IIC_TIMEOUT_MS) != 0U) &&
                (Iic_IsDeviceReady(EEPROM_IIC_ADDR_HAL,
                              EEPROM_WRITE_READY_TRIALS,
                              EEPROM_WRITE_READY_TIMEOUT_MS) != 0U) &&
                (Iic_MemRead(EEPROM_IIC_ADDR_HAL, write_addr,
                             I2C_MEMADD_SIZE_16BIT, verify_page, write_len,
                             EEPROM_IIC_TIMEOUT_MS) != 0U) &&
                (memcmp(verify_page, write_ptr, write_len) == 0))
            {
                page_verified = 1U;
                break;
            }
            g_eeprom_write_retry_count++;
            if (attempt + 1U < EEPROM_OPERATION_ATTEMPTS) (void)Iic_Init();
        }
        if (page_verified == 0U)
        {
            s_eeprom_ready = 0U;
            s_eeprom_fault = 3U;
            g_eeprom_last_failed_address = write_addr;
            return 0U;
        }

        write_addr = (uint16_t)(write_addr + write_len);
        write_ptr += write_len;
        remain = (uint16_t)(remain - write_len);
    }

    s_eeprom_ready = 1U;
    return 1U;
}

/**
 * @brief EEPROM 读写：执行内部处理（对应符号 Eeprom_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eeprom_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Eeprom_Check();
}

/**
 * @brief EEPROM 读写：执行内部处理（对应符号 Eeprom_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：value；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eeprom_Test2(void)
{
    uint8_t value = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Eeprom_Read(0U, &value, 1U);
}

/**
 * @brief EEPROM 读写：执行内部处理（对应符号 Eeprom_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：value；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Eeprom_Test3(void)
{
    uint8_t value = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Eeprom_Read(EEPROM_LAST_ADDR, &value, 1U);
}
