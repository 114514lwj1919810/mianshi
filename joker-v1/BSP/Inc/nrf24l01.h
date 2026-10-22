/* AUTO-FILE-DOC
 * 文件职责：nRF24L01 无线收发。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

#ifndef __NRF24L01_H
#define __NRF24L01_H

#include <stdint.h>
#include "nrf24l01_define.h"

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
typedef enum {
    MODE_UNINIT = 0,
    MODE_MASTER,
    MODE_SLAVE
} RunMode;

typedef enum {
    SLAVE_IDLE = 0,
    SLAVE_LISTEN_BROADCAST,
    SLAVE_WAIT_CONNECT,
    SLAVE_CONNECTED
} SlaveState;

typedef enum {
    NRF_ASYNC_TX_IDLE = 0,
    NRF_ASYNC_TX_BUSY,
    NRF_ASYNC_TX_OK,
    NRF_ASYNC_TX_FAILED
} NrfAsyncTxResult;

typedef struct {
    uint8_t id[DEVICE_ID_LEN];
    uint8_t addr[5];
    uint8_t channel;
    uint8_t index;
    uint8_t isActive;
} DeviceInfo;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
extern RunMode g_RunMode;
extern SlaveState g_SlaveState;
extern uint8_t g_MyID[DEVICE_ID_LEN];
extern uint8_t g_SlaveAddr[5];
extern uint8_t g_SlaveChannel;
extern uint32_t g_SlaveLastActivity;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
extern volatile uint8_t g_NRF24L01_IrqPending;
extern volatile uint8_t g_NrfInitResult;
extern volatile uint8_t g_NrfDiscoverTxOk;
extern volatile uint32_t g_NrfRxPacketCount;
extern volatile uint32_t g_NrfWrongPacketCount;
extern volatile uint32_t g_NrfRxQueueDropCount;
extern volatile uint32_t g_NrfTxSuccessCount;
extern volatile uint32_t g_NrfTxFailCount;
extern volatile uint32_t g_NrfTxTimeoutCount;
extern volatile uint8_t g_NrfLastPacketType;
extern volatile uint8_t g_NrfLastStatus;
extern volatile uint8_t g_NrfLastFifoStatus;
extern volatile uint8_t g_NrfLastRawPacket[PACKET_SIZE];
extern volatile uint8_t g_NrfLastAppData[PACKET_SIZE - 6U];
extern volatile uint8_t g_NrfLastAppLength;

extern volatile uint8_t g_NrfMasterId[DEVICE_ID_LEN];
extern volatile uint8_t g_NrfMasterAddrAdvertised[5];
extern volatile uint8_t g_NrfMasterChannelAdvertised;
extern volatile uint8_t g_NrfMasterAddrDerived[5];
extern volatile uint8_t g_NrfMasterChannelDerived;
extern volatile uint8_t g_NrfMasterLinkValid;
extern volatile uint8_t g_NrfMasterLinkMismatch;
extern volatile uint8_t g_NrfResponseTargetAddr[5];
extern volatile uint8_t g_NrfResponseTargetChannel;

extern volatile uint8_t g_NrfRxAddrExpected[5];
extern volatile uint8_t g_NrfRxAddrReadback[5];
extern volatile uint8_t g_NrfRxChannelExpected;
extern volatile uint8_t g_NrfRxChannelReadback;
extern volatile uint8_t g_NrfRxConfigReadback;
extern volatile uint8_t g_NrfRxEnAaReadback;
extern volatile uint8_t g_NrfRxEnRxAddrReadback;
extern volatile uint8_t g_NrfRxPayloadWidthReadback;
extern volatile uint8_t g_NrfRxConfigOk;
extern volatile uint32_t g_NrfRxConfigRepairCount;

extern volatile uint32_t g_NrfServiceCallCount;
extern volatile uint32_t g_NrfIrqNotifyCount;
extern volatile uint32_t g_NrfRxServiceCallCount;
extern volatile uint32_t g_NrfRxForcedPollCount;
extern volatile uint32_t g_NrfRxFifoDrainCount;
extern volatile uint32_t g_NrfRxQueuedCount;
extern volatile uint32_t g_NrfRxEmptyIrqCount;
extern volatile uint32_t g_NrfConnectedTimeoutCount;
extern volatile uint8_t  g_NrfLastDriverState;
extern volatile uint8_t  g_NrfLastFifoBeforeDrain;
extern volatile uint8_t  g_NrfLastFifoAfterDrain;
extern volatile uint8_t  g_NrfLastDrainCount;
extern volatile uint8_t  g_NrfRxQueueLevel;

extern volatile uint8_t  g_NrfRadioState;
extern volatile uint32_t g_NrfRadioStateChangeCount;
extern volatile uint32_t g_NrfPollCount;
extern volatile uint32_t g_NrfRxValidDataCount;
extern volatile uint32_t g_NrfRxInvalidDataCount;
extern volatile uint32_t g_NrfAppTxStartCount;
extern volatile uint32_t g_NrfAppTxCompleteCount;
extern volatile uint32_t g_NrfAppTxRejectCount;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void NRF24L01_EXTI_Notify(void);
uint8_t NRF24L01_IRQ_Pending(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void NRF24L01_Init(RunMode mode);
void NRF24L01_Service(void);
void NRF24L01_Run(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t NRF24L01_IsReady(void);
uint8_t NRF24L01_SlaveRecv(uint8_t *data, uint8_t capacity, uint8_t *len);

uint8_t NRF24L01_SlaveSendStart(const uint8_t *data, uint8_t len);
NrfAsyncTxResult NRF24L01_SlaveSendGetResult(void);
uint8_t NRF24L01_SlaveSendBusy(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
const char *NRF24L01_GetStateString(void);

#endif
