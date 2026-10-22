/* AUTO-FILE-DOC
 * 文件职责：EEPROM 读写。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef EEPROM_H
#define EEPROM_H

#include "gd32f407_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define EEPROM_IIC_ADDR_7BIT           0x50U
#define EEPROM_IIC_ADDR_HAL            ((uint16_t)(EEPROM_IIC_ADDR_7BIT << 1U))

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define EEPROM_SIZE_BYTES              32768U
#define EEPROM_PAGE_SIZE_BYTES         64U
#define EEPROM_LAST_ADDR               ((uint16_t)(EEPROM_SIZE_BYTES - 1U))

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define EEPROM_IIC_TIMEOUT_MS          50U
#define EEPROM_WRITE_READY_TRIALS      100U
#define EEPROM_WRITE_READY_TIMEOUT_MS  2U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Eeprom_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Eeprom_IsReady(void);

/* Latched storage fault: 0=healthy, 1=offline, 2=read, 3=write/verify,
 * 4=nonblank records could not be validated. Only Eeprom_Init clears it.
 * While latched all writes are blocked, preserving old records for recovery. */
uint8_t Eeprom_GetFault(void);
void Eeprom_ProtectData(uint16_t address);
extern volatile uint32_t g_eeprom_read_retry_count;
extern volatile uint32_t g_eeprom_write_retry_count;
extern volatile uint16_t g_eeprom_last_failed_address;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Eeprom_Check(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Eeprom_Read(uint16_t addr, uint8_t *data, uint16_t len);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Eeprom_Write(uint16_t addr, const uint8_t *data, uint16_t len);


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Eeprom_Test1(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Eeprom_Test2(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Eeprom_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
