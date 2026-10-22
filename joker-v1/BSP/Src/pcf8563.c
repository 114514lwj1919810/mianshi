/* AUTO-FILE-DOC
 * 文件职责：实时时钟驱动。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "pcf8563.h"

#include "iic.h"

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
static uint8_t s_pcf8563_ready = 0U;
static uint16_t s_pcf8563_last_error_code = PCF8563_ERROR_NONE;

#define PCF8563_DEFAULT_YEAR            2026U
#define PCF8563_DEFAULT_MONTH           7U
#define PCF8563_DEFAULT_DAY             7U
#define PCF8563_DEFAULT_WEEKDAY         2U
#define PCF8563_DEFAULT_HOUR            10U
#define PCF8563_DEFAULT_MINUTE          30U
#define PCF8563_DEFAULT_SECOND          0U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define PCF8563_SET_DEFAULT_TIME_ON_BOOT 0U

/**
 * @brief 实时时钟驱动：执行内部处理（对应符号 Pcf8563_BcdToDec）。
 * @调用关系 直接调用者：pcf8563.c:Pcf8563_ReadTime。
 * @param bcd 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Pcf8563_BcdToDec(uint8_t bcd)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    return (uint8_t)(((bcd >> 4U) * 10U) + (bcd & 0x0FU));
}

/**
 * @brief 实时时钟驱动：执行内部处理（对应符号 Pcf8563_DecToBcd）。
 * @调用关系 直接调用者：pcf8563.c:Pcf8563_Init、pcf8563.c:Pcf8563_SetTime。
 * @param dec 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Pcf8563_DecToBcd(uint8_t dec)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    return (uint8_t)(((dec / 10U) << 4U) | (dec % 10U));
}

/**
 * @brief 实时时钟驱动：写入（对应符号 Pcf8563_WriteReg）。
 * @调用关系 直接调用者：pcf8563.c:Pcf8563_ClearVoltageLowFlag、pcf8563.c:Pcf8563_Init、pcf8563.c:Pcf8563_SetTime。
 * @param reg 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Pcf8563_WriteReg(uint8_t reg, uint8_t value)
{
    return Iic_MemWrite(PCF8563_IIC_ADDR_HAL,
                        reg,
                        I2C_MEMADD_SIZE_8BIT,
                        &value,
                        1U,
                        PCF8563_IIC_TIMEOUT_MS);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#if (PCF8563_SET_DEFAULT_TIME_ON_BOOT == 0U)
/**
 * @brief 实时时钟驱动：清除（对应符号 Pcf8563_ClearVoltageLowFlag）。
 * @调用关系 直接调用者：pcf8563.c:Pcf8563_Init。
 * @局部状态 主要临时变量：second_raw；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Pcf8563_ClearVoltageLowFlag(void)
{
    uint8_t second_raw;

    if (Iic_MemRead(PCF8563_IIC_ADDR_HAL,
                    PCF8563_REG_SECONDS,
                    I2C_MEMADD_SIZE_8BIT,
                    &second_raw,
                    1U,
                    PCF8563_IIC_TIMEOUT_MS) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_READ_FAIL;
        return 0U;
    }

    second_raw &= 0x7FU;
    if (Pcf8563_WriteReg(PCF8563_REG_SECONDS, second_raw) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_TIME_WRITE;
        return 0U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Iic_MemRead(PCF8563_IIC_ADDR_HAL,
                    PCF8563_REG_SECONDS,
                    I2C_MEMADD_SIZE_8BIT,
                    &second_raw,
                    1U,
                    PCF8563_IIC_TIMEOUT_MS) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_READ_FAIL;
        return 0U;
    }

    if ((second_raw & 0x80U) != 0U)
    {
        s_pcf8563_last_error_code = PCF8563_ERROR_TIME_VL_STUCK;
        return 0U;
    }

    s_pcf8563_ready = 1U;
    return 1U;
}
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#if (PCF8563_SET_DEFAULT_TIME_ON_BOOT == 0U)
/**
 * @brief 实时时钟驱动：执行内部处理（对应符号 Pcf8563_FillDefaultTime）。
 * @调用关系 直接调用者：pcf8563.c:Pcf8563_Init。
 * @param time 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Pcf8563_FillDefaultTime(Pcf8563_Time *time)
{
    time->year = PCF8563_DEFAULT_YEAR;
    time->month = PCF8563_DEFAULT_MONTH;
    time->day = PCF8563_DEFAULT_DAY;
    time->weekday = PCF8563_DEFAULT_WEEKDAY;
    time->hour = PCF8563_DEFAULT_HOUR;
    time->minute = PCF8563_DEFAULT_MINUTE;
    time->second = PCF8563_DEFAULT_SECOND;
    time->voltage_low = 0U;
}
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 实时时钟驱动：读取（对应符号 Pcf8563_GetInvalidReason）。
 * @调用关系 直接调用者：pcf8563.c:Pcf8563_IsTimeValidValue、pcf8563.c:Pcf8563_Init、pcf8563.c:Pcf8563_ReadTime。
 * @param time 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Pcf8563_GetInvalidReason(const Pcf8563_Time *time)
{
    if (time == 0)
    {
        return PCF8563_ERROR_READ_PARAM;
    }

    if (time->voltage_low != 0U)
    {
        if (s_pcf8563_last_error_code == PCF8563_ERROR_TIME_VL_STUCK)
        {
            return PCF8563_ERROR_TIME_VL_STUCK;
        }
        return PCF8563_ERROR_TIME_VL;
    }

    if ((time->year == 2000U) && (time->month == 1U) && (time->day == 1U))
    {
        return PCF8563_ERROR_TIME_DEFAULT;
    }

    if ((time->year < 2024U) || (time->year > 2099U) ||
        (time->month < 1U) || (time->month > 12U) ||
        (time->day < 1U) || (time->day > 31U) ||
        (time->hour > 23U) ||
        (time->minute > 59U) ||
        (time->second > 59U))
    {
        return PCF8563_ERROR_TIME_RANGE;
    }

    return PCF8563_ERROR_NONE;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 实时时钟驱动：校验（对应符号 Pcf8563_IsTimeValidValue）。
 * @调用关系 直接调用者：game.c:Game_IsPcfTimeUsable。
 * @局部状态 主要临时变量：invalid_reason；均只在本次调用中有效。
 * @param time 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Pcf8563_IsTimeValidValue(const Pcf8563_Time *time)
{
    uint16_t invalid_reason;

    invalid_reason = Pcf8563_GetInvalidReason(time);
    if (invalid_reason != PCF8563_ERROR_NONE)
    {
        s_pcf8563_last_error_code = invalid_reason;
        return 0U;
    }

    return 1U;
}

/**
 * @brief 实时时钟驱动：初始化（对应符号 Pcf8563_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：buf、ctrl、time、invalid_reason；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Pcf8563_Init(void)
{
    uint8_t buf[PCF8563_TIME_REG_COUNT];
#if (PCF8563_SET_DEFAULT_TIME_ON_BOOT != 0U)
    uint8_t ctrl;
#endif
#if (PCF8563_SET_DEFAULT_TIME_ON_BOOT == 0U)
    Pcf8563_Time time;
#endif

    if (Pcf8563_Check() == 0U)
    {
        s_pcf8563_last_error_code = PCF8563_ERROR_CHECK_FAIL;
        return 0U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    buf[0] = 0x00U;
    buf[1] = 0x00U;
    if (Iic_MemWrite(PCF8563_IIC_ADDR_HAL,
                     PCF8563_REG_CONTROL_STATUS1,
                     I2C_MEMADD_SIZE_8BIT,
                     buf,
                     2U,
                     PCF8563_IIC_TIMEOUT_MS) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_CTRL1_WRITE;
        return 0U;
    }

    s_pcf8563_ready = 1U;

#if (PCF8563_SET_DEFAULT_TIME_ON_BOOT != 0U)
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    ctrl = 0x20U;
    if (Pcf8563_WriteReg(PCF8563_REG_CONTROL_STATUS1, ctrl) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_STOP_WRITE;
        return 0U;
    }

    buf[0] = Pcf8563_DecToBcd((uint8_t)PCF8563_DEFAULT_SECOND);
    buf[1] = Pcf8563_DecToBcd((uint8_t)PCF8563_DEFAULT_MINUTE);
    buf[2] = Pcf8563_DecToBcd((uint8_t)PCF8563_DEFAULT_HOUR);
    buf[3] = Pcf8563_DecToBcd((uint8_t)PCF8563_DEFAULT_DAY);
    buf[4] = (uint8_t)(PCF8563_DEFAULT_WEEKDAY & 0x07U);
    buf[5] = Pcf8563_DecToBcd((uint8_t)PCF8563_DEFAULT_MONTH);
    buf[6] = Pcf8563_DecToBcd((uint8_t)(PCF8563_DEFAULT_YEAR % 100U));

    if (Iic_MemWrite(PCF8563_IIC_ADDR_HAL,
                     PCF8563_REG_SECONDS,
                     I2C_MEMADD_SIZE_8BIT,
                     buf,
                     PCF8563_TIME_REG_COUNT,
                     PCF8563_IIC_TIMEOUT_MS) == 0U)
    {
        (void)Pcf8563_WriteReg(PCF8563_REG_CONTROL_STATUS1, 0x00U);
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_TIME_WRITE;
        return 0U;
    }

    ctrl = 0x00U;
    if (Pcf8563_WriteReg(PCF8563_REG_CONTROL_STATUS1, ctrl) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_RUN_WRITE;
        return 0U;
    }

    if (Iic_MemRead(PCF8563_IIC_ADDR_HAL,
                    PCF8563_REG_SECONDS,
                    I2C_MEMADD_SIZE_8BIT,
                    buf,
                    PCF8563_TIME_REG_COUNT,
                    PCF8563_IIC_TIMEOUT_MS) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_READBACK;
        return 0U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (((buf[0] & 0x80U) != 0U) ||
        ((buf[1] & 0x7FU) != Pcf8563_DecToBcd((uint8_t)PCF8563_DEFAULT_MINUTE)) ||
        ((buf[2] & 0x3FU) != Pcf8563_DecToBcd((uint8_t)PCF8563_DEFAULT_HOUR)) ||
        ((buf[3] & 0x3FU) != Pcf8563_DecToBcd((uint8_t)PCF8563_DEFAULT_DAY)) ||
        ((buf[5] & 0x1FU) != Pcf8563_DecToBcd((uint8_t)PCF8563_DEFAULT_MONTH)) ||
        (buf[6] != Pcf8563_DecToBcd((uint8_t)(PCF8563_DEFAULT_YEAR % 100U))))
    {
        s_pcf8563_ready = 1U;
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_READBACK_BAD;
        return 1U;
    }

    s_pcf8563_ready = 1U;
    s_pcf8563_last_error_code = PCF8563_ERROR_NONE;
    return 1U;
#else

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Pcf8563_ReadTime(&time) != 0U)
    {
        uint16_t invalid_reason = Pcf8563_GetInvalidReason(&time);
        if (invalid_reason != PCF8563_ERROR_NONE)
        {
            s_pcf8563_last_error_code = invalid_reason;
            Pcf8563_FillDefaultTime(&time);
            if (Pcf8563_SetTime(&time) == 0U)
            {
                return 1U;
            }

            if (Pcf8563_ReadTime(&time) == 0U)
            {
                s_pcf8563_last_error_code = PCF8563_ERROR_SET_READBACK;
                return 1U;
            }

            invalid_reason = Pcf8563_GetInvalidReason(&time);
            if (invalid_reason == PCF8563_ERROR_TIME_VL)
            {
                if (Pcf8563_ClearVoltageLowFlag() != 0U)
                {
                    if (Pcf8563_ReadTime(&time) != 0U)
                    {
                        invalid_reason = Pcf8563_GetInvalidReason(&time);
                    }
                }
            }

            if (invalid_reason != PCF8563_ERROR_NONE)
            {
                s_pcf8563_last_error_code = invalid_reason;
                return 1U;
            }
        }
    }

    s_pcf8563_last_error_code = PCF8563_ERROR_NONE;
    return 1U;
#endif
}

/**
 * @brief 实时时钟驱动：读取（对应符号 Pcf8563_IsReady）。
 * @调用关系 直接调用者：app_main.c:App_Init、game.c:Game_PrintRtcToUart。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Pcf8563_IsReady(void)
{
    return s_pcf8563_ready;
}

/**
 * @brief 实时时钟驱动：读取（对应符号 Pcf8563_GetLastErrorCode）。
 * @调用关系 直接调用者：app_main.c:App_Init、pcf8563.c:Pcf8563_Test3、game.c:Game_PrintRtcToUart、game.c:Game_ShowAccountPage。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Pcf8563_GetLastErrorCode(void)
{
    return s_pcf8563_last_error_code;
}

/**
 * @brief 实时时钟驱动：检查（对应符号 Pcf8563_Check）。
 * @调用关系 直接调用者：pcf8563.c:Pcf8563_Init、pcf8563.c:Pcf8563_Test1。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Pcf8563_Check(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Iic_IsDeviceReady(PCF8563_IIC_ADDR_HAL,
                          PCF8563_READY_TRIALS,
                          PCF8563_READY_TIMEOUT_MS) != 0U)
    {
        s_pcf8563_ready = 1U;
        return 1U;
    }

    s_pcf8563_ready = 0U;
    return 0U;
}

/**
 * @brief 实时时钟驱动：读取（对应符号 Pcf8563_ReadTime）。
 * @调用关系 直接调用者：pcf8563.c:Pcf8563_Init、pcf8563.c:Pcf8563_Test2、game.c:Game_PrintRtcToUart、game.c:Game_LoadAccountTimeForDisplay、game.c:Game_RestoreRtcFromAccount、game.c:Game_RtcBackupTask。
 * @局部状态 主要临时变量：raw、month_raw、year_raw；均只在本次调用中有效。
 * @param time 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Pcf8563_ReadTime(Pcf8563_Time *time)
{
    uint8_t raw[PCF8563_TIME_REG_COUNT];
    uint8_t month_raw;
    uint8_t year_raw;
    if (time == 0)
    {
        s_pcf8563_last_error_code = PCF8563_ERROR_READ_PARAM;
        return 0U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Iic_MemRead(PCF8563_IIC_ADDR_HAL,
                    PCF8563_REG_SECONDS,
                    I2C_MEMADD_SIZE_8BIT,
                    raw,
                    PCF8563_TIME_REG_COUNT,
                    PCF8563_IIC_TIMEOUT_MS) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_READ_FAIL;
        return 0U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    time->voltage_low = ((raw[0] & 0x80U) != 0U) ? 1U : 0U;
    time->second = Pcf8563_BcdToDec((uint8_t)(raw[0] & 0x7FU));
    time->minute = Pcf8563_BcdToDec((uint8_t)(raw[1] & 0x7FU));
    time->hour = Pcf8563_BcdToDec((uint8_t)(raw[2] & 0x3FU));
    time->day = Pcf8563_BcdToDec((uint8_t)(raw[3] & 0x3FU));
    time->weekday = (uint8_t)(raw[4] & 0x07U);

    month_raw = raw[5];
    time->month = Pcf8563_BcdToDec((uint8_t)(month_raw & 0x1FU));

    year_raw = Pcf8563_BcdToDec(raw[6]);
    if ((month_raw & 0x80U) != 0U)
    {
        time->year = (uint16_t)(1900U + year_raw);
    }
    else
    {
        time->year = (uint16_t)(2000U + year_raw);
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_pcf8563_ready = 1U;
    s_pcf8563_last_error_code = Pcf8563_GetInvalidReason(time);
    return 1U;
}

/**
 * @brief 实时时钟驱动：设置（对应符号 Pcf8563_SetTime）。
 * @调用关系 直接调用者：pcf8563.c:Pcf8563_Init、game.c:Game_RestoreRtcFromAccount、game.c:Game_RtcBackupTask、game.c:Game_AdjustAccountTime、remote_config.c:Remote_SetTime。
 * @局部状态 主要临时变量：raw、year_low、century_bit、control1；均只在本次调用中有效。
 * @param time 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Pcf8563_SetTime(const Pcf8563_Time *time)
{
    uint8_t raw[PCF8563_TIME_REG_COUNT];
    uint8_t year_low;
    uint8_t century_bit;
    uint8_t control1;

    if (time == 0)
    {
        return 0U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if ((time->year < 1900U) || (time->year > 2099U) ||
        (time->month < 1U) || (time->month > 12U) ||
        (time->day < 1U) || (time->day > 31U) ||
        (time->weekday > 6U) ||
        (time->hour > 23U) ||
        (time->minute > 59U) ||
        (time->second > 59U))
    {
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_RANGE;
        return 0U;
    }

    if (time->year < 2000U)
    {
        year_low = (uint8_t)(time->year - 1900U);
        century_bit = 0x80U;
    }
    else
    {
        year_low = (uint8_t)(time->year - 2000U);
        century_bit = 0x00U;
    }

    raw[0] = Pcf8563_DecToBcd(time->second);
    raw[1] = Pcf8563_DecToBcd(time->minute);
    raw[2] = Pcf8563_DecToBcd(time->hour);
    raw[3] = Pcf8563_DecToBcd(time->day);
    raw[4] = (uint8_t)(time->weekday & 0x07U);
    raw[5] = (uint8_t)(century_bit | Pcf8563_DecToBcd(time->month));
    raw[6] = Pcf8563_DecToBcd(year_low);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    control1 = 0x20U;
    if (Pcf8563_WriteReg(PCF8563_REG_CONTROL_STATUS1, control1) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_STOP_WRITE;
        return 0U;
    }

    if (Iic_MemWrite(PCF8563_IIC_ADDR_HAL,
                     PCF8563_REG_SECONDS,
                     I2C_MEMADD_SIZE_8BIT,
                     raw,
                     PCF8563_TIME_REG_COUNT,
                     PCF8563_IIC_TIMEOUT_MS) == 0U)
    {
        (void)Pcf8563_WriteReg(PCF8563_REG_CONTROL_STATUS1, 0x00U);
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_TIME_WRITE;
        return 0U;
    }

    control1 = 0x00U;
    if (Pcf8563_WriteReg(PCF8563_REG_CONTROL_STATUS1, control1) == 0U)
    {
        s_pcf8563_ready = 0U;
        s_pcf8563_last_error_code = PCF8563_ERROR_SET_RUN_WRITE;
        return 0U;
    }

    s_pcf8563_ready = 1U;
    s_pcf8563_last_error_code = PCF8563_ERROR_NONE;
    return 1U;
}

/**
 * @brief 实时时钟驱动：执行内部处理（对应符号 Pcf8563_Test1）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Pcf8563_Test1(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Pcf8563_Check();
}

/**
 * @brief 实时时钟驱动：执行内部处理（对应符号 Pcf8563_Test2）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：now；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Pcf8563_Test2(void)
{
    Pcf8563_Time now;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Pcf8563_ReadTime(&now);
}

/**
 * @brief 实时时钟驱动：执行内部处理（对应符号 Pcf8563_Test3）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Pcf8563_Test3(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    (void)Pcf8563_GetLastErrorCode();
}
