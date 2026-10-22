/* AUTO-FILE-DOC
 * 文件职责：实时时钟驱动。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef PCF8563_H
#define PCF8563_H

#include "gd32f407_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define PCF8563_IIC_ADDR_7BIT          0x51U
#define PCF8563_IIC_ADDR_HAL           ((uint16_t)(PCF8563_IIC_ADDR_7BIT << 1U))

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define PCF8563_REG_CONTROL_STATUS1    0x00U
#define PCF8563_REG_CONTROL_STATUS2    0x01U
#define PCF8563_REG_SECONDS            0x02U
#define PCF8563_REG_MINUTES            0x03U
#define PCF8563_REG_HOURS              0x04U
#define PCF8563_REG_DAYS               0x05U
#define PCF8563_REG_WEEKDAYS           0x06U
#define PCF8563_REG_CENTURY_MONTHS     0x07U
#define PCF8563_REG_YEARS              0x08U

#define PCF8563_TIME_REG_COUNT         7U
#define PCF8563_IIC_TIMEOUT_MS         50U
#define PCF8563_READY_TRIALS           3U
#define PCF8563_READY_TIMEOUT_MS       5U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define PCF8563_ERROR_NONE             0U
#define PCF8563_ERROR_CHECK_FAIL       1001U
#define PCF8563_ERROR_CTRL1_READ       1101U
#define PCF8563_ERROR_CTRL1_WRITE      1102U
#define PCF8563_ERROR_CTRL2_WRITE      1103U
#define PCF8563_ERROR_READ_PARAM       2001U
#define PCF8563_ERROR_READ_FAIL        2101U
#define PCF8563_ERROR_TIME_INVALID     2301U
#define PCF8563_ERROR_TIME_VL          2302U
#define PCF8563_ERROR_TIME_RANGE       2303U
#define PCF8563_ERROR_TIME_DEFAULT     2304U
#define PCF8563_ERROR_TIME_VL_STUCK    2312U
#define PCF8563_ERROR_SET_PARAM        3001U
#define PCF8563_ERROR_SET_RANGE        3002U
#define PCF8563_ERROR_SET_CTRL1_READ   3101U
#define PCF8563_ERROR_SET_STOP_WRITE   3102U
#define PCF8563_ERROR_SET_TIME_WRITE   3103U
#define PCF8563_ERROR_SET_RUN_WRITE    3104U
#define PCF8563_ERROR_SET_READBACK     3201U
#define PCF8563_ERROR_SET_READBACK_BAD 3202U

typedef struct
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    uint16_t year;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    uint8_t month;
    uint8_t day;
    uint8_t weekday;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    uint8_t voltage_low;
} Pcf8563_Time;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Pcf8563_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Pcf8563_IsReady(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Pcf8563_Check(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Pcf8563_ReadTime(Pcf8563_Time *time);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Pcf8563_IsTimeValidValue(const Pcf8563_Time *time);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint16_t Pcf8563_GetLastErrorCode(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Pcf8563_SetTime(const Pcf8563_Time *time);



/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Pcf8563_Test1(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Pcf8563_Test2(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Pcf8563_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
