/* AUTO-FILE-DOC
 * 文件职责：board_pinout 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef BOARD_PINOUT_H
#define BOARD_PINOUT_H

#include "gd32f407_hal.h"

/*
 * GD32F407ZG board mapping transcribed from gd32引脚小丑.pdf (8 pages).
 * The PDF still shows an STM32H743ZIT6 symbol; these definitions follow the
 * connector net names and package pin positions, as requested, not the label.
 */

/* Inputs IN1..IN28 (IN27 is not present in the schematic). */
#define BOARD_IN1_PORT   GPIOE
#define BOARD_IN1_PIN    GPIO_PIN_7
#define BOARD_IN2_PORT   GPIOG
#define BOARD_IN2_PIN    GPIO_PIN_1
#define BOARD_IN3_PORT   GPIOG
#define BOARD_IN3_PIN    GPIO_PIN_0
#define BOARD_IN4_PORT   GPIOF
#define BOARD_IN4_PIN    GPIO_PIN_15
#define BOARD_IN5_PORT   GPIOF
#define BOARD_IN5_PIN    GPIO_PIN_14
#define BOARD_IN6_PORT   GPIOF
#define BOARD_IN6_PIN    GPIO_PIN_13
#define BOARD_IN7_PORT   GPIOF
#define BOARD_IN7_PIN    GPIO_PIN_12
#define BOARD_IN8_PORT   GPIOF
#define BOARD_IN8_PIN    GPIO_PIN_11
#define BOARD_IN9_PORT   GPIOB
#define BOARD_IN9_PIN    GPIO_PIN_2
#define BOARD_IN10_PORT  GPIOB
#define BOARD_IN10_PIN   GPIO_PIN_1
#define BOARD_IN11_PORT  GPIOB
#define BOARD_IN11_PIN   GPIO_PIN_0
#define BOARD_IN12_PORT  GPIOF
#define BOARD_IN12_PIN   GPIO_PIN_10
#define BOARD_IN13_PORT  GPIOF
#define BOARD_IN13_PIN   GPIO_PIN_9
#define BOARD_IN14_PORT  GPIOF
#define BOARD_IN14_PIN   GPIO_PIN_8
#define BOARD_IN15_PORT  GPIOF
#define BOARD_IN15_PIN   GPIO_PIN_7
#define BOARD_IN16_PORT  GPIOF
#define BOARD_IN16_PIN   GPIO_PIN_6
#define BOARD_IN17_PORT  GPIOF
#define BOARD_IN17_PIN   GPIO_PIN_5
#define BOARD_IN18_PORT  GPIOF
#define BOARD_IN18_PIN   GPIO_PIN_4
#define BOARD_IN19_PORT  GPIOF
#define BOARD_IN19_PIN   GPIO_PIN_3
#define BOARD_IN20_PORT  GPIOF
#define BOARD_IN20_PIN   GPIO_PIN_2
#define BOARD_IN21_PORT  GPIOF
#define BOARD_IN21_PIN   GPIO_PIN_1
#define BOARD_IN22_PORT  GPIOF
#define BOARD_IN22_PIN   GPIO_PIN_0
#define BOARD_IN23_PORT  GPIOE
#define BOARD_IN23_PIN   GPIO_PIN_6
#define BOARD_IN24_PORT  GPIOE
#define BOARD_IN24_PIN   GPIO_PIN_5
#define BOARD_IN25_PORT  GPIOE
#define BOARD_IN25_PIN   GPIO_PIN_4
#define BOARD_IN26_PORT  GPIOE
#define BOARD_IN26_PIN   GPIO_PIN_3
#define BOARD_IN28_PORT  GPIOE
#define BOARD_IN28_PIN   GPIO_PIN_2
#define BOARD_IN30_PORT  GPIOD
#define BOARD_IN30_PIN   GPIO_PIN_3
#define BOARD_IN31_PORT  GPIOD
#define BOARD_IN31_PIN   GPIO_PIN_4
#define BOARD_IN32_PORT  GPIOD
#define BOARD_IN32_PIN   GPIO_PIN_5

/* Open-collector output bank H1..H20. */
#define BOARD_H1_PORT    GPIOB
#define BOARD_H1_PIN     GPIO_PIN_12
#define BOARD_H2_PORT    GPIOB
#define BOARD_H2_PIN     GPIO_PIN_13
#define BOARD_H3_PORT    GPIOB
#define BOARD_H3_PIN     GPIO_PIN_14
#define BOARD_H4_PORT    GPIOB
#define BOARD_H4_PIN     GPIO_PIN_15
#define BOARD_H5_PORT    GPIOD
#define BOARD_H5_PIN     GPIO_PIN_8
#define BOARD_H6_PORT    GPIOD
#define BOARD_H6_PIN     GPIO_PIN_9
#define BOARD_H7_PORT    GPIOD
#define BOARD_H7_PIN     GPIO_PIN_10
#define BOARD_H8_PORT    GPIOD
#define BOARD_H8_PIN     GPIO_PIN_11
#define BOARD_H9_PORT    GPIOD
#define BOARD_H9_PIN     GPIO_PIN_12
#define BOARD_H10_PORT   GPIOD
#define BOARD_H10_PIN    GPIO_PIN_13
#define BOARD_H11_PORT   GPIOD
#define BOARD_H11_PIN    GPIO_PIN_14
#define BOARD_H12_PORT   GPIOD
#define BOARD_H12_PIN    GPIO_PIN_15
#define BOARD_H13_PORT   GPIOG
#define BOARD_H13_PIN    GPIO_PIN_2
#define BOARD_H14_PORT   GPIOG
#define BOARD_H14_PIN    GPIO_PIN_3
#define BOARD_H15_PORT   GPIOG
#define BOARD_H15_PIN    GPIO_PIN_4
#define BOARD_H16_PORT   GPIOG
#define BOARD_H16_PIN    GPIO_PIN_5
#define BOARD_H17_PORT   GPIOG
#define BOARD_H17_PIN    GPIO_PIN_6
#define BOARD_H18_PORT   GPIOG
#define BOARD_H18_PIN    GPIO_PIN_7
#define BOARD_H19_PORT   GPIOG
#define BOARD_H19_PIN    GPIO_PIN_8
#define BOARD_H20_PORT   GPIOC
#define BOARD_H20_PIN    GPIO_PIN_6

/* High-current output bank A1..A6. */
#define BOARD_A1_PORT    GPIOE
#define BOARD_A1_PIN     GPIO_PIN_10
#define BOARD_A2_PORT    GPIOE
#define BOARD_A2_PIN     GPIO_PIN_11
#define BOARD_A3_PORT    GPIOE
#define BOARD_A3_PIN     GPIO_PIN_12
#define BOARD_A4_PORT    GPIOE
#define BOARD_A4_PIN     GPIO_PIN_13
#define BOARD_A5_PORT    GPIOE
#define BOARD_A5_PIN     GPIO_PIN_14
#define BOARD_A6_PORT    GPIOE
#define BOARD_A6_PIN     GPIO_PIN_15

/* RZ7889 motor bridge control. */
#define BOARD_MA_PORT    GPIOG
#define BOARD_MA_PIN     GPIO_PIN_13
#define BOARD_MC_PORT    GPIOG
#define BOARD_MC_PIN     GPIO_PIN_12
#define BOARD_MB_PORT    GPIOG
#define BOARD_MB_PIN     GPIO_PIN_11
#define BOARD_MD_PORT    GPIOG
#define BOARD_MD_PIN     GPIO_PIN_10

/* SPI0: TF card and nRF24L01 share PA5/PA6/PA7. */
#define BOARD_NRF_CE_PORT   GPIOA
#define BOARD_NRF_CE_PIN    GPIO_PIN_2
#define BOARD_NRF_CSN_PORT  GPIOA
#define BOARD_NRF_CSN_PIN   GPIO_PIN_3
#define BOARD_NRF_IRQ_PORT  GPIOE
#define BOARD_NRF_IRQ_PIN   GPIO_PIN_9
#define BOARD_TF_CS_PORT    GPIOA
#define BOARD_TF_CS_PIN     GPIO_PIN_4
#define BOARD_SPI_SCK_PORT  GPIOA
#define BOARD_SPI_SCK_PIN   GPIO_PIN_5
#define BOARD_SPI_MISO_PORT GPIOA
#define BOARD_SPI_MISO_PIN  GPIO_PIN_6
#define BOARD_SPI_MOSI_PORT GPIOA
#define BOARD_SPI_MOSI_PIN  GPIO_PIN_7

/* I2C0 shared by AT24C256 and PCF8563. */
#define BOARD_I2C_SCL_PORT  GPIOB
#define BOARD_I2C_SCL_PIN   GPIO_PIN_6
#define BOARD_I2C_SDA_PORT  GPIOB
#define BOARD_I2C_SDA_PIN   GPIO_PIN_7

/* Serial and status nets. */
#define BOARD_FX1_TX_PORT   GPIOA
#define BOARD_FX1_TX_PIN    GPIO_PIN_0
#define BOARD_FX1_RX_PORT   GPIOA
#define BOARD_FX1_RX_PIN    GPIO_PIN_1
#define BOARD_FX2_TX_PORT   GPIOB
#define BOARD_FX2_TX_PIN    GPIO_PIN_10
#define BOARD_FX2_RX_PORT   GPIOB
#define BOARD_FX2_RX_PIN    GPIO_PIN_11
#define BOARD_TX1_PORT      GPIOA
#define BOARD_TX1_PIN       GPIO_PIN_9
#define BOARD_RX1_PORT      GPIOA
#define BOARD_RX1_PIN       GPIO_PIN_10
#define BOARD_RS485_TX_PORT GPIOC
#define BOARD_RS485_TX_PIN  GPIO_PIN_12
#define BOARD_RS485_RX_PORT GPIOD
#define BOARD_RS485_RX_PIN  GPIO_PIN_2
/*
 * The populated board uses the same automatic-direction principle as
 * pin_design.pdf: CMT8021 isolates UART TX/RX and Q1 derives MAX485 RE/DE
 * from the transmit data.  There is no MCU RS485 direction GPIO.
 */

/* SW2 address selector: switch ON shorts the corresponding input to GND. */
#define BOARD_ADDR_BIT0_PORT GPIOC
#define BOARD_ADDR_BIT0_PIN  GPIO_PIN_0
#define BOARD_ADDR_BIT1_PORT GPIOC
#define BOARD_ADDR_BIT1_PIN  GPIO_PIN_1
#define BOARD_ADDR_BIT2_PORT GPIOC
#define BOARD_ADDR_BIT2_PIN  GPIO_PIN_2
#define BOARD_FX_BUSY_PORT  GPIOC
#define BOARD_FX_BUSY_PIN   GPIO_PIN_10
#define BOARD_BGM_BUSY_PORT GPIOA
#define BOARD_BGM_BUSY_PIN  GPIO_PIN_15

/* P2/CN6 4094-style clock/strobe/data outputs. */
#define BOARD_OUT_OC_PORT   GPIOC
#define BOARD_OUT_OC_PIN    GPIO_PIN_11
#define BOARD_OUT_OS_PORT   GPIOD
#define BOARD_OUT_OS_PIN    GPIO_PIN_0
#define BOARD_OUT_OD_PORT   GPIOD
#define BOARD_OUT_OD_PIN    GPIO_PIN_1

#endif
