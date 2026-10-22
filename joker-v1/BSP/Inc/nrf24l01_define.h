/* AUTO-FILE-DOC
 * 文件职责：nrf24l01_define 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

#ifndef __NRF24L01_DEFINE_H
#define __NRF24L01_DEFINE_H


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24L01_CONFIG             0x00U
#define NRF24L01_EN_AA              0x01U
#define NRF24L01_EN_RXADDR          0x02U
#define NRF24L01_SETUP_AW           0x03U
#define NRF24L01_SETUP_RETR         0x04U
#define NRF24L01_RF_CH              0x05U
#define NRF24L01_RF_SETUP           0x06U
#define NRF24L01_STATUS             0x07U
#define NRF24L01_RX_ADDR_P0         0x0AU
#define NRF24L01_TX_ADDR            0x10U
#define NRF24L01_RX_PW_P0           0x11U
#define NRF24L01_FIFO_STATUS        0x17U
#define NRF24L01_DYNPD              0x1CU
#define NRF24L01_FEATURE            0x1DU


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24L01_R_REGISTER         0x00U
#define NRF24L01_W_REGISTER         0x20U
#define NRF24L01_R_RX_PAYLOAD       0x61U
#define NRF24L01_W_TX_PAYLOAD       0xA0U
#define NRF24L01_FLUSH_TX           0xE1U
#define NRF24L01_FLUSH_RX           0xE2U
#define NRF24L01_NOP                0xFFU


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define PACKET_TYPE_DISCOVER        0x01U
#define PACKET_TYPE_RESPONSE        0x02U
#define PACKET_TYPE_CONNECT         0x03U
#define PACKET_TYPE_DATA            0x04U
#define PACKET_TYPE_CONNECT_ACK     0x06U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define PACKET_SIZE                 32U
#define DEVICE_ID_LEN               4U
#define MAX_DEVICES                 8U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define BROADCAST_ADDR_0            0xE7U
#define BROADCAST_ADDR_1            0xE7U
#define BROADCAST_ADDR_2            0xE7U
#define BROADCAST_ADDR_3            0xE7U
#define BROADCAST_ADDR_4            0xE7U
#define BROADCAST_CHANNEL           0x4CU

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define HEARTBEAT_TIMEOUT_MS        15000U
#define CONNECT_TIMEOUT_MS          15000U
#define DATA_TIMEOUT_MS             200U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define WAIT_CONNECT_BROADCAST_WINDOW_MS   60U


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_POWER_ON_DELAY_MS     100U
#define NRF24_MODE_SETTLE_MS        2U
#define NRF24_RX_FALLBACK_MS        5U
#define NRF24_DISCOVER_COPIES       3U
#define NRF24_DISCOVER_GAP_MS       10U

#endif
