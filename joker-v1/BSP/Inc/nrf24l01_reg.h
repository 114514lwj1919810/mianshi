/* AUTO-FILE-DOC
 * 文件职责：nrf24l01_reg 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

#ifndef __NRF24L01_REG_H
#define __NRF24L01_REG_H

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_REG_CONFIG       0x00U
#define NRF24_REG_EN_AA        0x01U
#define NRF24_REG_RF_CH        0x05U
#define NRF24_REG_RF_SETUP     0x06U
#define NRF24_REG_STATUS       0x07U
#define NRF24_REG_TX_ADDR      0x10U
#define NRF24_REG_FIFO_STATUS  0x17U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_CMD_R_REGISTER   0x00U
#define NRF24_CMD_W_REGISTER   0x20U
#define NRF24_CMD_FLUSH_TX     0xE1U
#define NRF24_CMD_FLUSH_RX     0xE2U
#define NRF24_CMD_NOP          0xFFU

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_STATUS_CLEAR_ALL 0x70U

#endif
