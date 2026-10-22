/* AUTO-FILE-DOC
 * 文件职责：I2C 总线驱动。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef IIC_H
#define IIC_H

#include "gd32f407_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define IIC1_SCL_PORT                  GPIOB
#define IIC1_SCL_PIN                   GPIO_PIN_6
#define IIC1_SDA_PORT                  GPIOB
#define IIC1_SDA_PIN                   GPIO_PIN_7
#define IIC1_GPIO_AF                   GPIO_AF4_I2C1

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define IIC1_PERIPH                    I2C0
/*
 * This value is intentionally conservative. With I2C1 clocked from
 * D2PCLK1=120MHz it yields a much slower, more tolerant bus than the old
 * 0xF0422227 setting. It matches the separately verified working RTC test.
 * The old macro name is retained so iic.c needs no other change.
 */
#define IIC1_TIMING_100KHZ             0xF0433F7DU
#define IIC_DEFAULT_TIMEOUT_MS         50U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Iic_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Iic_IsInited(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Iic_IsDeviceReady(uint16_t dev_addr_hal, uint32_t trials, uint32_t timeout_ms);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Iic_MasterWrite(uint16_t dev_addr_hal,
                        const uint8_t *data,
                        uint16_t len,
                        uint32_t timeout_ms);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Iic_MasterRead(uint16_t dev_addr_hal,
                       uint8_t *data,
                       uint16_t len,
                       uint32_t timeout_ms);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Iic_MemWrite(uint16_t dev_addr_hal,
                     uint16_t mem_addr,
                     uint16_t mem_addr_size,
                     const uint8_t *data,
                     uint16_t len,
                     uint32_t timeout_ms);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Iic_MemRead(uint16_t dev_addr_hal,
                    uint16_t mem_addr,
                    uint16_t mem_addr_size,
                    uint8_t *data,
                    uint16_t len,
                    uint32_t timeout_ms);


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Iic_Test1(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Iic_Test2(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Iic_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
