/* AUTO-FILE-DOC
 * 文件职责：nRF24L01 无线收发。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#include "gd32f407_hal.h"
#include "nrf24l01.h"
#include "device_identity.h"
#include "device_label_wire.h"
#include "nrf24l01_define.h"
#include "nrf24l01_port.h"
#include "delay.h"
#include "spi.h"

#ifndef NULL
#define NULL ((void *)0)
#endif

/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 Delay_DeadlineReached）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @param deadline 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Delay_DeadlineReached(uint32_t deadline)
{
    return ((int32_t)(HAL_GetTick() - deadline) >= 0) ? 1U : 0U;
}

#define NRF24_CONFIG_TX              0x0EU    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_CONFIG_RX              0x0FU    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */


#define NRF24_STATUS_RX_DR           0x40U    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_STATUS_TX_DS           0x20U    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_STATUS_MAX_RT          0x10U    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_STATUS_ALL             0x70U    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

#define NRF24_FIFO_RX_EMPTY          0x01U    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

#define NRF24_DATA_HEAD_LEN          6U    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_USER_DATA_MAX          (PACKET_SIZE - NRF24_DATA_HEAD_LEN)    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_APP_RX_QUEUE_DEPTH     16U    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define NRF24_RX_POLL_PERIOD_MS      1U
#define NRF24_RX_DRAIN_BUDGET        8U    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

typedef enum {
    RADIO_BOOT_WAIT = 0,
    RADIO_BROADCAST_RX_SETTLE,
    RADIO_BROADCAST_RX,
    RADIO_DISCOVER_DELAY,
    RADIO_DISCOVER_TX_SETTLE,
    RADIO_DISCOVER_TX_WAIT,
    RADIO_DISCOVER_GAP,
    RADIO_PRIVATE_RX_SETTLE,
    RADIO_PRIVATE_RX,
    RADIO_WAIT_CONNECT_BROADCAST_SETTLE,
    RADIO_WAIT_CONNECT_BROADCAST_RX,
    RADIO_APP_TX_SETTLE,
    RADIO_APP_TX_WAIT,
    RADIO_ERROR
} RadioState;

typedef struct {
    uint8_t len;
    uint8_t data[NRF24_USER_DATA_MAX];
} NrfAppRxItem;

RunMode g_RunMode = MODE_UNINIT;
SlaveState g_SlaveState = SLAVE_IDLE;
uint8_t g_MyID[DEVICE_ID_LEN];
uint8_t g_SlaveAddr[5];
uint8_t g_SlaveChannel = BROADCAST_CHANNEL;
uint32_t g_SlaveLastActivity;

volatile uint8_t g_NRF24L01_IrqPending;
volatile uint8_t g_NrfInitResult = 0xFFU;
volatile uint8_t g_NrfDiscoverTxOk;
volatile uint32_t g_NrfRxPacketCount;
volatile uint32_t g_NrfWrongPacketCount;
volatile uint32_t g_NrfRxQueueDropCount;
volatile uint32_t g_NrfTxSuccessCount;
volatile uint32_t g_NrfTxFailCount;
volatile uint32_t g_NrfTxTimeoutCount;
volatile uint8_t g_NrfLastPacketType;
volatile uint8_t g_NrfLastStatus;
volatile uint8_t g_NrfLastFifoStatus;
volatile uint8_t g_NrfLastRawPacket[PACKET_SIZE];
volatile uint8_t g_NrfLastAppData[NRF24_USER_DATA_MAX];
volatile uint8_t g_NrfLastAppLength;
volatile uint8_t g_NrfMasterId[DEVICE_ID_LEN];
volatile uint8_t g_NrfMasterAddrAdvertised[5];
volatile uint8_t g_NrfMasterChannelAdvertised;
volatile uint8_t g_NrfMasterAddrDerived[5];
volatile uint8_t g_NrfMasterChannelDerived;
volatile uint8_t g_NrfMasterLinkValid;
volatile uint8_t g_NrfMasterLinkMismatch;
volatile uint8_t g_NrfResponseTargetAddr[5];
volatile uint8_t g_NrfResponseTargetChannel;
volatile uint32_t g_NrfIrqNotifyCount;
volatile uint32_t g_NrfPollCount;
volatile uint32_t g_NrfServiceCallCount;
volatile uint32_t g_NrfRxServiceCallCount;
volatile uint32_t g_NrfRxForcedPollCount;
volatile uint32_t g_NrfRxFifoDrainCount;
volatile uint32_t g_NrfRxEmptyIrqCount;
volatile uint32_t g_NrfRxValidDataCount;
volatile uint32_t g_NrfRxInvalidDataCount;
volatile uint32_t g_NrfRxQueuedCount;
volatile uint32_t g_NrfAppTxStartCount;
volatile uint32_t g_NrfAppTxRejectCount;
volatile uint32_t g_NrfAppTxCompleteCount;
volatile uint32_t g_NrfConnectedTimeoutCount;
volatile uint32_t g_NrfRadioStateChangeCount;
volatile uint8_t g_NrfRadioState;
volatile uint8_t g_NrfLastDriverState;
volatile uint8_t g_NrfLastDrainCount;
volatile uint8_t g_NrfLastFifoBeforeDrain;
volatile uint8_t g_NrfLastFifoAfterDrain;
volatile uint8_t g_NrfRxQueueLevel;
volatile uint8_t g_NrfRxConfigReadback;
volatile uint8_t g_NrfRxEnAaReadback;
volatile uint8_t g_NrfRxEnRxAddrReadback;
volatile uint8_t g_NrfRxPayloadWidthReadback;
volatile uint8_t g_NrfRxChannelReadback;
volatile uint8_t g_NrfRxAddrReadback[5];
volatile uint8_t g_NrfRxChannelExpected;
volatile uint8_t g_NrfRxAddrExpected[5];
volatile uint8_t g_NrfRxConfigOk;
volatile uint32_t g_NrfRxConfigRepairCount;

static const uint8_t s_BroadcastAddr[5] = {
    BROADCAST_ADDR_0,
    BROADCAST_ADDR_1,
    BROADCAST_ADDR_2,
    BROADCAST_ADDR_3,
    BROADCAST_ADDR_4
};
static uint8_t s_RemoteMasterAddr[5];
static uint8_t s_RemoteMasterChannel = BROADCAST_CHANNEL;
static uint8_t s_TxPacket[PACKET_SIZE];
static uint8_t s_DiscoverResponse[PACKET_SIZE];
static uint8_t s_DiscoverCopiesRemaining;
static uint8_t s_DiscoverAnySuccess;
static uint8_t s_DiscoverGapSeed;
static uint8_t s_ControlTxActive;
static uint8_t s_RxQueueHead;
static uint8_t s_RxQueueTail;
static uint8_t s_RxQueueCount;
static NrfAppRxItem s_RxQueue[NRF24_APP_RX_QUEUE_DEPTH];
static RadioState s_RadioState = RADIO_BOOT_WAIT;
static uint32_t s_Deadline;
static uint32_t s_NextPoll;
static uint32_t s_ConnectDeadline;
static uint32_t s_WaitConnectWindowDeadline;
static NrfAsyncTxResult s_AppTxResult = NRF_ASYNC_TX_IDLE;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：设置（对应符号 NRF24L01_SetState）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_EnterBroadcastRx、nrf24l01.c:NRF24L01_EnterWaitConnectBroadcastRx、nrf24l01.c:NRF24L01_ReturnPrivateRxWaitConnect、nrf24l01.c:NRF24L01_EnterPrivateRxConnected、nrf24l01.c:NRF24L01_ReturnPrivateRx、nrf24l01.c:NRF24L01_HandleDiscover、nrf24l01.c:NRF24L01_StartConnectAck、nrf24l01.c:NRF24L01_StartDiscoverTx。
 * @param state 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_SetState(RadioState state)
{
    if (s_RadioState != state) {
        s_RadioState = state;
        g_NrfRadioState = (uint8_t)state;
        g_NrfLastDriverState = (uint8_t)state;
        g_NrfRadioStateChangeCount++;
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_CsnDelay）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_W_CSN。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_CsnDelay(void)
{
    volatile uint16_t i;

    for (i = 0U; i < 80U; i++) {
        __NOP();
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_W_CE）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_ConfigureRx、nrf24l01.c:NRF24L01_ConfigureTx、nrf24l01.c:NRF24L01_HandleDiscover、nrf24l01.c:NRF24L01_StartConnectAck、nrf24l01.c:NRF24L01_FinishTx、nrf24l01.c:NRF24L01_Init、nrf24l01.c:NRF24L01_Service、nrf24l01.c:NRF24L01_SlaveSendStart。
 * @param high 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_W_CE(uint8_t high)
{
HAL_GPIO_WritePin(NRF24_CE_GPIO_PORT,
                      NRF24_CE_GPIO_PIN,
                      high != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_W_CSN）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_WriteReg、nrf24l01.c:NRF24L01_ReadReg、nrf24l01.c:NRF24L01_WriteRegs、nrf24l01.c:NRF24L01_ReadRegs、nrf24l01.c:NRF24L01_Command、nrf24l01.c:NRF24L01_WritePayload、nrf24l01.c:NRF24L01_ReadPayload、nrf24l01.c:NRF24L01_ReadStatus。
 * @param high 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_W_CSN(uint8_t high)
{
HAL_GPIO_WritePin(NRF24_CSN_GPIO_PORT,
                      NRF24_CSN_GPIO_PIN,
                      high != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET);
    NRF24L01_CsnDelay();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_R_IRQ）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_IRQ_Pending、nrf24l01.c:NRF24L01_ClearIrqFlags、nrf24l01.c:NRF24L01_DrainRx。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_R_IRQ(void)
{
    return (HAL_GPIO_ReadPin(NRF24_IRQ_GPIO_PORT,
                             NRF24_IRQ_GPIO_PIN) == GPIO_PIN_SET) ?
           1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_EXTI_Notify）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void NRF24L01_EXTI_Notify(void)
{
g_NRF24L01_IrqPending = 1U;
    g_NrfIrqNotifyCount++;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_IRQ_Pending）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_ShouldPoll。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t NRF24L01_IRQ_Pending(void)
{
    if (g_NRF24L01_IrqPending != 0U ||
        NRF24L01_R_IRQ() == 0U) {
        return 1U;
    }

    return 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：初始化（对应符号 NRF24L01_GPIO_Init）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    NRF24_GPIO_CLOCK_ENABLE();

    HAL_GPIO_WritePin(NRF24_CSN_GPIO_PORT,
                      NRF24_CSN_GPIO_PIN,
                      GPIO_PIN_SET);
    HAL_GPIO_WritePin(NRF24_CE_GPIO_PORT,
                      NRF24_CE_GPIO_PIN,
                      GPIO_PIN_RESET);

    gpio.Pin = NRF24_CSN_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(NRF24_CSN_GPIO_PORT, &gpio);

    gpio.Pin = NRF24_CE_GPIO_PIN;
    HAL_GPIO_Init(NRF24_CE_GPIO_PORT, &gpio);

    gpio.Pin = NRF24_IRQ_GPIO_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLUP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(NRF24_IRQ_GPIO_PORT, &gpio);

    /* PE9 shares EXTI9 with PF9 ticket eye, so 7.9 polls the IRQ pin instead of mapping EXTI. */

    g_NRF24L01_IrqPending = 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：写入（对应符号 NRF24L01_WriteReg）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_ClearIrqFlags、nrf24l01.c:NRF24L01_WriteCommonRegs、nrf24l01.c:NRF24L01_ConfigureRx、nrf24l01.c:NRF24L01_ConfigureTx。
 * @param reg 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_WriteReg(uint8_t reg, uint8_t value)
{
NRF24L01_W_CSN(0U);
    (void)SPI1_WriteByte((uint8_t)(NRF24L01_W_REGISTER | reg));
    (void)SPI1_WriteByte(value);
    NRF24L01_W_CSN(1U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：读取（对应符号 NRF24L01_ReadReg）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_CaptureRxConfig、nrf24l01.c:NRF24L01_DrainRx。
 * @局部状态 主要临时变量：value；均只在本次调用中有效。
 * @param reg 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_ReadReg(uint8_t reg)
{
uint8_t value;

    NRF24L01_W_CSN(0U);
    (void)SPI1_WriteByte((uint8_t)(NRF24L01_R_REGISTER | reg));
    value = SPI1_WriteByte(NRF24L01_NOP);
    NRF24L01_W_CSN(1U);

    return value;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：写入（对应符号 NRF24L01_WriteRegs）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_ConfigureRx、nrf24l01.c:NRF24L01_ConfigureTx、nrf24l01.c:NRF24L01_RegisterCheck。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param reg 按值传入；函数内部修改不会回写调用方变量。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_WriteRegs(uint8_t reg,
                               const uint8_t *data,
                               uint8_t count)
{
uint8_t i;

    NRF24L01_W_CSN(0U);
    (void)SPI1_WriteByte((uint8_t)(NRF24L01_W_REGISTER | reg));

    for (i = 0U; i < count; i++) {
        (void)SPI1_WriteByte(data[i]);
    }

    NRF24L01_W_CSN(1U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：读取（对应符号 NRF24L01_ReadRegs）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_CaptureRxConfig、nrf24l01.c:NRF24L01_RegisterCheck。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param reg 按值传入；函数内部修改不会回写调用方变量。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_ReadRegs(uint8_t reg,
                              uint8_t *data,
                              uint8_t count)
{
uint8_t i;

    NRF24L01_W_CSN(0U);
    (void)SPI1_WriteByte((uint8_t)(NRF24L01_R_REGISTER | reg));

    for (i = 0U; i < count; i++) {
        data[i] = SPI1_WriteByte(NRF24L01_NOP);
    }

    NRF24L01_W_CSN(1U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_Command）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_ConfigureRx、nrf24l01.c:NRF24L01_ConfigureTx、nrf24l01.c:NRF24L01_FinishTx。
 * @param command 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_Command(uint8_t command)
{
NRF24L01_W_CSN(0U);
    (void)SPI1_WriteByte(command);
    NRF24L01_W_CSN(1U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：写入（对应符号 NRF24L01_WritePayload）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_ConfigureTx。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_WritePayload(const uint8_t *data)
{
uint8_t i;

    NRF24L01_W_CSN(0U);
    (void)SPI1_WriteByte(NRF24L01_W_TX_PAYLOAD);

    for (i = 0U; i < PACKET_SIZE; i++) {
        (void)SPI1_WriteByte(data[i]);
    }

    NRF24L01_W_CSN(1U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：读取（对应符号 NRF24L01_ReadPayload）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_DrainRx。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_ReadPayload(uint8_t *data)
{
uint8_t i;

    NRF24L01_W_CSN(0U);
    (void)SPI1_WriteByte(NRF24L01_R_RX_PAYLOAD);

    for (i = 0U; i < PACKET_SIZE; i++) {
        data[i] = SPI1_WriteByte(NRF24L01_NOP);
    }

    NRF24L01_W_CSN(1U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：读取（对应符号 NRF24L01_ReadStatus）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_DrainRx、nrf24l01.c:NRF24L01_ReadTxOutcome。
 * @局部状态 主要临时变量：status；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_ReadStatus(void)
{
    uint8_t status;

    NRF24L01_W_CSN(0U);
    status = SPI1_WriteByte(NRF24L01_NOP);
    NRF24L01_W_CSN(1U);

    return status;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：清除（对应符号 NRF24L01_ClearIrqFlags）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_ConfigureRx、nrf24l01.c:NRF24L01_ConfigureTx、nrf24l01.c:NRF24L01_DrainRx、nrf24l01.c:NRF24L01_ReadTxOutcome。
 * @param flags 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_ClearIrqFlags(uint8_t flags)
{
NRF24L01_WriteReg(NRF24L01_STATUS,
                      (uint8_t)(flags & NRF24_STATUS_ALL));

    g_NRF24L01_IrqPending = 0U;

    if (NRF24L01_R_IRQ() == 0U) {
        g_NRF24L01_IrqPending = 1U;
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：清除（对应符号 NRF24L01_ClearPacket）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_PrepareDataPacket、nrf24l01.c:NRF24L01_HandleDiscover、nrf24l01.c:NRF24L01_StartConnectAck。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param packet 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_ClearPacket(uint8_t *packet)
{
    uint8_t i;

    for (i = 0U; i < PACKET_SIZE; i++) {
        packet[i] = 0U;
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_ArrayEqual）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_UpdateMasterInfo、nrf24l01.c:NRF24L01_CaptureRxConfig、nrf24l01.c:NRF24L01_QueueDataPacket、nrf24l01.c:NRF24L01_HandleConnect。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param a 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param b 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_ArrayEqual(const uint8_t *a,
                                   const uint8_t *b,
                                   uint8_t count)
{
    uint8_t i;

    for (i = 0U; i < count; i++) {
        if (a[i] != b[i]) {
            return 0U;
        }
    }

    return 1U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_GenerateDeviceID）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @局部状态 主要临时变量：uid0、uid1、uid2；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_GenerateDeviceID(void)
{
uint32_t uid0 = *(uint32_t *)(UID_BASE + 0U);
    uint32_t uid1 = *(uint32_t *)(UID_BASE + 4U);
    uint32_t uid2 = *(uint32_t *)(UID_BASE + 8U);

    g_MyID[0] = (uint8_t)uid0;
    g_MyID[1] = (uint8_t)(uid1 >> 8U);
    g_MyID[2] = (uint8_t)(uid2 >> 16U);
    g_MyID[3] = (uint8_t)((uid0 >> 24U) ^ uid1 ^ uid2);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_MakeChannelFromID）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_UpdateMasterInfo、nrf24l01.c:NRF24L01_Service。
 * @局部状态 主要临时变量：sum、channel；均只在本次调用中有效。
 * @param id 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param offset 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_MakeChannelFromID(const uint8_t *id,
                                          uint8_t offset)
{
uint16_t sum;
    uint8_t channel;

    sum = (uint16_t)id[0] + id[1] + id[2] + id[3] + offset;
    channel = (uint8_t)(2U + (sum % 100U));

    if (channel == BROADCAST_CHANNEL) {
        channel++;
    }

    return channel;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_MakeAddress）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_UpdateMasterInfo、nrf24l01.c:NRF24L01_Service。
 * @param addr 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param id 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param prefix 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_MakeAddress(uint8_t *addr,
                                 const uint8_t *id,
                                 uint8_t prefix)
{
addr[0] = prefix;
    addr[1] = (uint8_t)(id[0] ^ 0xA5U);
    addr[2] = (uint8_t)(id[1] ^ 0x5AU);
    addr[3] = (uint8_t)(id[2] ^ 0x3CU);
    addr[4] = (uint8_t)(id[3] ^ 0xC3U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：更新（对应符号 NRF24L01_UpdateMasterInfo）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_HandleDiscover。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param masterId 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param advertisedAddr 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param advertisedChannel 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_UpdateMasterInfo(const uint8_t *masterId,
                                      const uint8_t *advertisedAddr,
                                      uint8_t advertisedChannel)
{
uint8_t i;

    for (i = 0U; i < DEVICE_ID_LEN; i++) {
        g_NrfMasterId[i] = masterId[i];
    }

    for (i = 0U; i < 5U; i++) {
        g_NrfMasterAddrAdvertised[i] = advertisedAddr[i];
        s_RemoteMasterAddr[i] = advertisedAddr[i];
    }

    g_NrfMasterChannelAdvertised = advertisedChannel;
    s_RemoteMasterChannel = advertisedChannel;
    g_NrfMasterLinkValid = 1U;

    NRF24L01_MakeAddress((uint8_t *)g_NrfMasterAddrDerived,
                         masterId,
                         0xA7U);
    g_NrfMasterChannelDerived =
        NRF24L01_MakeChannelFromID(masterId, 17U);

    g_NrfMasterLinkMismatch =
        (NRF24L01_ArrayEqual(
             (const uint8_t *)g_NrfMasterAddrAdvertised,
             (const uint8_t *)g_NrfMasterAddrDerived,
             5U) == 0U ||
         g_NrfMasterChannelAdvertised !=
             g_NrfMasterChannelDerived) ? 1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：写入（对应符号 NRF24L01_WriteCommonRegs）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_ConfigureRx、nrf24l01.c:NRF24L01_ConfigureTx。
 * @param autoAck 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_WriteCommonRegs(uint8_t autoAck)
{
NRF24L01_WriteReg(NRF24L01_EN_AA, autoAck != 0U ? 0x01U : 0x00U);
    NRF24L01_WriteReg(NRF24L01_EN_RXADDR, 0x01U);
    NRF24L01_WriteReg(NRF24L01_SETUP_AW, 0x03U);
    NRF24L01_WriteReg(NRF24L01_SETUP_RETR, 0x2FU);
    NRF24L01_WriteReg(NRF24L01_RF_SETUP, 0x06U);
    NRF24L01_WriteReg(NRF24L01_RX_PW_P0, PACKET_SIZE);
    NRF24L01_WriteReg(NRF24L01_DYNPD, 0x00U);
    NRF24L01_WriteReg(NRF24L01_FEATURE, 0x00U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_CaptureRxConfig）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_ConfigureRx。
 * @局部状态 主要临时变量：i、expectedAa；均只在本次调用中有效。
 * @param addr 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param channel 按值传入；函数内部修改不会回写调用方变量。
 * @param autoAck 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_CaptureRxConfig(const uint8_t *addr,
                                     uint8_t channel,
                                     uint8_t autoAck)
{
    uint8_t i;
    uint8_t expectedAa;

    for (i = 0U; i < 5U; i++) {
        g_NrfRxAddrExpected[i] = addr[i];
    }

    NRF24L01_ReadRegs(NRF24L01_RX_ADDR_P0,
                      (uint8_t *)g_NrfRxAddrReadback,
                      5U);

    g_NrfRxChannelExpected = channel;
    g_NrfRxChannelReadback = NRF24L01_ReadReg(NRF24L01_RF_CH);
    g_NrfRxConfigReadback = NRF24L01_ReadReg(NRF24L01_CONFIG);
    g_NrfRxEnAaReadback = NRF24L01_ReadReg(NRF24L01_EN_AA);
    g_NrfRxEnRxAddrReadback = NRF24L01_ReadReg(NRF24L01_EN_RXADDR);
    g_NrfRxPayloadWidthReadback = NRF24L01_ReadReg(NRF24L01_RX_PW_P0);

    expectedAa = autoAck != 0U ? 0x01U : 0x00U;

    g_NrfRxConfigOk =
        (NRF24L01_ArrayEqual(
             (const uint8_t *)g_NrfRxAddrExpected,
             (const uint8_t *)g_NrfRxAddrReadback,
             5U) != 0U &&
         g_NrfRxChannelReadback == channel &&
         g_NrfRxConfigReadback == NRF24_CONFIG_RX &&
         g_NrfRxEnAaReadback == expectedAa &&
         g_NrfRxEnRxAddrReadback == 0x01U &&
         g_NrfRxPayloadWidthReadback == PACKET_SIZE) ? 1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_ConfigureRx）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_EnterBroadcastRx、nrf24l01.c:NRF24L01_EnterWaitConnectBroadcastRx、nrf24l01.c:NRF24L01_ReturnPrivateRxWaitConnect、nrf24l01.c:NRF24L01_EnterPrivateRxConnected、nrf24l01.c:NRF24L01_ReturnPrivateRx。
 * @局部状态 主要临时变量：attempt；均只在本次调用中有效。
 * @param addr 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param channel 按值传入；函数内部修改不会回写调用方变量。
 * @param autoAck 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_ConfigureRx(const uint8_t *addr,
                                 uint8_t channel,
                                 uint8_t autoAck)
{
uint8_t attempt;

    NRF24L01_W_CE(0U);

    for (attempt = 0U; attempt < 2U; attempt++) {
        NRF24L01_WriteCommonRegs(autoAck);
        NRF24L01_WriteReg(NRF24L01_RF_CH, channel);
        NRF24L01_WriteRegs(NRF24L01_RX_ADDR_P0, addr, 5U);
        NRF24L01_WriteRegs(NRF24L01_TX_ADDR, addr, 5U);
        NRF24L01_ClearIrqFlags(NRF24_STATUS_ALL);
        NRF24L01_Command(NRF24L01_FLUSH_RX);
        NRF24L01_Command(NRF24L01_FLUSH_TX);
        NRF24L01_WriteReg(NRF24L01_CONFIG, NRF24_CONFIG_RX);

        NRF24L01_CaptureRxConfig(addr, channel, autoAck);

        if (g_NrfRxConfigOk != 0U) {
            break;
        }

        g_NrfRxConfigRepairCount++;
    }

    s_Deadline = HAL_GetTick() + NRF24_MODE_SETTLE_MS;
    s_NextPoll = s_Deadline;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_ConfigureTx）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_StartConnectAck、nrf24l01.c:NRF24L01_StartDiscoverTx、nrf24l01.c:NRF24L01_SlaveSendStart。
 * @param addr 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param channel 按值传入；函数内部修改不会回写调用方变量。
 * @param autoAck 按值传入；函数内部修改不会回写调用方变量。
 * @param packet 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_ConfigureTx(const uint8_t *addr,
                                 uint8_t channel,
                                 uint8_t autoAck,
                                 const uint8_t *packet)
{
NRF24L01_W_CE(0U);
    NRF24L01_WriteCommonRegs(autoAck);
    NRF24L01_WriteReg(NRF24L01_RF_CH, channel);
    NRF24L01_WriteRegs(NRF24L01_TX_ADDR, addr, 5U);
    NRF24L01_WriteRegs(NRF24L01_RX_ADDR_P0, addr, 5U);
    NRF24L01_ClearIrqFlags(NRF24_STATUS_ALL);
    NRF24L01_Command(NRF24L01_FLUSH_TX);
    NRF24L01_WriteReg(NRF24L01_CONFIG, NRF24_CONFIG_TX);
    NRF24L01_WritePayload(packet);

    s_Deadline = HAL_GetTick() + NRF24_MODE_SETTLE_MS;
    s_NextPoll = s_Deadline;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：检查（对应符号 NRF24L01_RegisterCheck）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @局部状态 主要临时变量：test、txRead、rxRead、i；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_RegisterCheck(void)
{
const uint8_t test[5] = {
        0xA5U, 0x5AU, 0x3CU, 0xC3U, 0x96U
    };
    uint8_t txRead[5];
    uint8_t rxRead[5];
    uint8_t i;

    NRF24L01_WriteRegs(NRF24L01_TX_ADDR, test, 5U);
    NRF24L01_WriteRegs(NRF24L01_RX_ADDR_P0, test, 5U);
    NRF24L01_ReadRegs(NRF24L01_TX_ADDR, txRead, 5U);
    NRF24L01_ReadRegs(NRF24L01_RX_ADDR_P0, rxRead, 5U);

    for (i = 0U; i < 5U; i++) {
        if (txRead[i] != test[i] ||
            rxRead[i] != test[i]) {
            return 1U;
        }
    }

    return 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：清除（对应符号 NRF24L01_ClearSoftwareQueue）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_EnterBroadcastRx、nrf24l01.c:NRF24L01_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_ClearSoftwareQueue(void)
{
s_RxQueueHead = 0U;
    s_RxQueueTail = 0U;
    s_RxQueueCount = 0U;
    g_NrfRxQueueLevel = 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：进入状态（对应符号 NRF24L01_EnterBroadcastRx）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_EnterBroadcastRx(void)
{
    uint8_t i;

    g_SlaveState = SLAVE_LISTEN_BROADCAST;
    g_NrfMasterLinkValid = 0U;
    g_NrfMasterLinkMismatch = 0U;

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_AppTxResult = NRF_ASYNC_TX_IDLE;
    s_ControlTxActive = 0U;

    for (i = 0U; i < DEVICE_ID_LEN; i++) {
        g_NrfMasterId[i] = 0U;
    }

    NRF24L01_ClearSoftwareQueue();
    NRF24L01_ConfigureRx(s_BroadcastAddr,
                         BROADCAST_CHANNEL,
                         0U);
    NRF24L01_SetState(RADIO_BROADCAST_RX_SETTLE);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：进入状态（对应符号 NRF24L01_EnterWaitConnectBroadcastRx）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_EnterWaitConnectBroadcastRx(void)
{
    g_SlaveState = SLAVE_WAIT_CONNECT;
    NRF24L01_ConfigureRx(s_BroadcastAddr,
                         BROADCAST_CHANNEL,
                         0U);
    s_WaitConnectWindowDeadline =
        HAL_GetTick() + WAIT_CONNECT_BROADCAST_WINDOW_MS;
    NRF24L01_SetState(RADIO_WAIT_CONNECT_BROADCAST_SETTLE);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_ReturnPrivateRxWaitConnect）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_ReturnPrivateRxWaitConnect(void)
{
    g_SlaveState = SLAVE_WAIT_CONNECT;
    NRF24L01_ConfigureRx(g_SlaveAddr,
                         g_SlaveChannel,
                         1U);
    NRF24L01_SetState(RADIO_PRIVATE_RX_SETTLE);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：进入状态（对应符号 NRF24L01_EnterPrivateRxConnected）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_HandleConnect。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_EnterPrivateRxConnected(void)
{
g_SlaveState = SLAVE_CONNECTED;
    g_SlaveLastActivity = HAL_GetTick();

    NRF24L01_ConfigureRx(g_SlaveAddr,
                         g_SlaveChannel,
                         1U);
    NRF24L01_SetState(RADIO_PRIVATE_RX_SETTLE);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_ReturnPrivateRx）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_ReturnPrivateRx(void)
{
    NRF24L01_ConfigureRx(g_SlaveAddr,
                         g_SlaveChannel,
                         1U);
    NRF24L01_SetState(RADIO_PRIVATE_RX_SETTLE);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_PrepareDataPacket）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_SlaveSendStart。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param packet 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_PrepareDataPacket(uint8_t *packet,
                                       const uint8_t *data,
                                       uint8_t len)
{
uint8_t i;

    if (len > NRF24_USER_DATA_MAX) {
        len = NRF24_USER_DATA_MAX;
    }

    NRF24L01_ClearPacket(packet);
    packet[0] = PACKET_TYPE_DATA;
    packet[1] = len;

    for (i = 0U; i < DEVICE_ID_LEN; i++) {
        packet[2U + i] = g_MyID[i];
    }

    for (i = 0U; i < len; i++) {
        packet[NRF24_DATA_HEAD_LEN + i] = data[i];
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_QueueDataPacket）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_DrainRx。
 * @局部状态 主要临时变量：len、i、item；均只在本次调用中有效。
 * @param packet 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_QueueDataPacket(const uint8_t *packet)
{
uint8_t len;
    uint8_t i;
    NrfAppRxItem *item;

    if (packet[0] != PACKET_TYPE_DATA) {
        g_NrfWrongPacketCount++;
        g_NrfRxInvalidDataCount++;
        return 0U;
    }

    len = packet[1];

    if (len == 0U || len > NRF24_USER_DATA_MAX) {
        g_NrfWrongPacketCount++;
        g_NrfRxInvalidDataCount++;
        return 0U;
    }

    if (s_RxQueueCount >= NRF24_APP_RX_QUEUE_DEPTH) {
        g_NrfRxQueueDropCount++;
        return 0U;
    }

    
    if (g_SlaveState != SLAVE_CONNECTED ||
        NRF24L01_ArrayEqual(&packet[2U],
                            (const uint8_t *)g_NrfMasterId,
                            DEVICE_ID_LEN) == 0U) {
        g_NrfWrongPacketCount++;
        g_NrfRxInvalidDataCount++;
        return 0U;
    }

    item = &s_RxQueue[s_RxQueueTail];
    item->len = len;

    for (i = 0U; i < len; i++) {
        item->data[i] = packet[NRF24_DATA_HEAD_LEN + i];
        g_NrfLastAppData[i] = item->data[i];
    }

    for (; i < NRF24_USER_DATA_MAX; i++) {
        item->data[i] = 0U;
        g_NrfLastAppData[i] = 0U;
    }

    g_NrfLastAppLength = len;
    s_RxQueueTail =
        (uint8_t)((s_RxQueueTail + 1U) %
                  NRF24_APP_RX_QUEUE_DEPTH);
    s_RxQueueCount++;
    g_NrfRxQueueLevel = s_RxQueueCount;
    g_NrfRxQueuedCount++;
    g_NrfRxValidDataCount++;

    return 1U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：处理状态或事件（对应符号 NRF24L01_HandleDiscover）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_DrainRx。
 * @局部状态 主要临时变量：i、randomDelay；均只在本次调用中有效。
 * @param packet 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_HandleDiscover(const uint8_t *packet)
{
uint8_t i;
    uint8_t randomDelay;

    if (packet[0] != PACKET_TYPE_DISCOVER) {
        g_NrfWrongPacketCount++;
        return;
    }

    NRF24L01_UpdateMasterInfo(&packet[1],
                              &packet[5],
                              packet[10]);

    NRF24L01_ClearPacket(s_DiscoverResponse);
    s_DiscoverResponse[0] = PACKET_TYPE_RESPONSE;

    for (i = 0U; i < DEVICE_ID_LEN; i++) {
        s_DiscoverResponse[1U + i] = g_MyID[i];
    }

    for (i = 0U; i < 5U; i++) {
        s_DiscoverResponse[5U + i] = g_SlaveAddr[i];
    }

    s_DiscoverResponse[10] = g_SlaveChannel;
    DeviceLabel_Encode(s_DiscoverResponse, DeviceIdentity_GetDisplayNumber());

    randomDelay =
        (uint8_t)(5U +
        ((g_MyID[0] ^ g_MyID[1] ^
          g_MyID[2] ^ g_MyID[3]) % 40U));

    NRF24L01_W_CE(0U);
    s_DiscoverCopiesRemaining = NRF24_DISCOVER_COPIES;
    s_DiscoverAnySuccess = 0U;
    g_NrfDiscoverTxOk = 0U;
    g_SlaveState = SLAVE_WAIT_CONNECT;
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_ConnectDeadline = HAL_GetTick() + CONNECT_TIMEOUT_MS;
    s_DiscoverGapSeed = (uint8_t)(randomDelay ^ packet[1] ^ packet[2]);
    s_Deadline = HAL_GetTick() + randomDelay;
    NRF24L01_SetState(RADIO_DISCOVER_DELAY);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：启动（对应符号 NRF24L01_StartConnectAck）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_HandleConnect。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_StartConnectAck(void)
{
uint8_t i;

    NRF24L01_ClearPacket(s_TxPacket);
    s_TxPacket[0] = PACKET_TYPE_CONNECT_ACK;

    
    for (i = 0U; i < DEVICE_ID_LEN; i++) {
        s_TxPacket[1U + i] = g_MyID[i];
        s_TxPacket[5U + i] = g_NrfMasterId[i];
    }
    s_TxPacket[9U] = 0U;

    s_ControlTxActive = 1U;
    NRF24L01_W_CE(0U);
    NRF24L01_ConfigureTx(s_RemoteMasterAddr,
                         s_RemoteMasterChannel,
                         1U,
                         s_TxPacket);
    NRF24L01_SetState(RADIO_APP_TX_SETTLE);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：处理状态或事件（对应符号 NRF24L01_HandleConnect）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_DrainRx。
 * @param packet 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_HandleConnect(const uint8_t *packet)
{
    if (packet[0] != PACKET_TYPE_CONNECT) {
        g_NrfWrongPacketCount++;
        return 0U;
    }

    if (NRF24L01_ArrayEqual(&packet[1U],
                            (const uint8_t *)g_NrfMasterId,
                            DEVICE_ID_LEN) == 0U ||
        NRF24L01_ArrayEqual(&packet[5U],
                            g_MyID,
                            DEVICE_ID_LEN) == 0U ||
        NRF24L01_ArrayEqual(&packet[9U],
                            (const uint8_t *)g_NrfMasterAddrAdvertised,
                            5U) == 0U ||
        packet[14U] != g_NrfMasterChannelAdvertised) {
        g_NrfWrongPacketCount++;
        return 0U;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    NRF24L01_EnterPrivateRxConnected();
    NRF24L01_StartConnectAck();
    return 1U;
}
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_ShouldPoll）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_DrainRx、nrf24l01.c:NRF24L01_ReadTxOutcome。
 * @局部状态 主要临时变量：now；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_ShouldPoll(void)
{
uint32_t now;

    if (NRF24L01_IRQ_Pending() != 0U) {
        return 1U;
    }

    now = HAL_GetTick();

    if ((int32_t)(now - s_NextPoll) >= 0) {
        s_NextPoll = now + NRF24_RX_POLL_PERIOD_MS;
        g_NrfRxForcedPollCount++;
        return 1U;
    }

    return 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_DrainRx）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @局部状态 主要临时变量：packet、status、fifo、drained、i；均只在本次调用中有效。
 * @param broadcastMode 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_DrainRx(uint8_t broadcastMode)
{
uint8_t packet[PACKET_SIZE];
    uint8_t status;
    uint8_t fifo;
    uint8_t drained;
    uint8_t i;

    if (NRF24L01_ShouldPoll() == 0U) {
        return 0U;
    }

    g_NrfPollCount++;
    g_NrfRxServiceCallCount++;

    status = NRF24L01_ReadStatus();
    fifo = NRF24L01_ReadReg(NRF24L01_FIFO_STATUS);

    g_NrfLastStatus = status;
    g_NrfLastFifoStatus = fifo;
    g_NrfLastFifoBeforeDrain = fifo;

    if (status == 0xFFU || fifo == 0xFFU) {
        return 0U;
    }

    drained = 0U;

    while (drained < NRF24_RX_DRAIN_BUDGET &&
           (fifo & NRF24_FIFO_RX_EMPTY) == 0U) {
        NRF24L01_ReadPayload(packet);
        drained++;
        g_NrfRxPacketCount++;
        g_NrfRxFifoDrainCount++;

        for (i = 0U; i < PACKET_SIZE; i++) {
            g_NrfLastRawPacket[i] = packet[i];
        }

        g_NrfLastPacketType = packet[0];

        if (broadcastMode != 0U) {
            NRF24L01_HandleDiscover(packet);
            break;
        }

        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        if (packet[0] == PACKET_TYPE_CONNECT) {
            if (g_SlaveState == SLAVE_WAIT_CONNECT ||
                g_SlaveState == SLAVE_CONNECTED) {
                (void)NRF24L01_HandleConnect(packet);
            } else {
                g_NrfWrongPacketCount++;
            }
            break;
        }

        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        if (g_SlaveState == SLAVE_WAIT_CONNECT) {
            g_NrfWrongPacketCount++;
            break;
        }

        if (NRF24L01_QueueDataPacket(packet) != 0U) {
            g_SlaveLastActivity = HAL_GetTick();
        }

        fifo = NRF24L01_ReadReg(NRF24L01_FIFO_STATUS);

        if (fifo == 0xFFU) {
            break;
        }
    }

    status = NRF24L01_ReadStatus();

    if (status != 0xFFU) {
        NRF24L01_ClearIrqFlags(status);
    }

    fifo = NRF24L01_ReadReg(NRF24L01_FIFO_STATUS);
    g_NrfLastFifoAfterDrain = fifo;
    g_NrfLastDrainCount = drained;

    if (drained == 0U &&
        ((status & NRF24_STATUS_RX_DR) != 0U ||
         NRF24L01_R_IRQ() == 0U)) {
        g_NrfRxEmptyIrqCount++;
    }

    if (fifo != 0xFFU &&
        (fifo & NRF24_FIFO_RX_EMPTY) == 0U) {
        g_NRF24L01_IrqPending = 1U;
    }

    return drained;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：读取（对应符号 NRF24L01_ReadTxOutcome）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @局部状态 主要临时变量：status；均只在本次调用中有效。
 * @param success 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t NRF24L01_ReadTxOutcome(uint8_t *success)
{
uint8_t status;

    if (success == NULL) {
        return 0U;
    }

    if (NRF24L01_ShouldPoll() == 0U) {
        return 0U;
    }

    status = NRF24L01_ReadStatus();
    g_NrfLastStatus = status;

    if (status == 0xFFU) {
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        *success = 0U;
        return 1U;
    }

    if ((status & NRF24_STATUS_TX_DS) != 0U) {
        NRF24L01_ClearIrqFlags(status);
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        *success = 1U;
        return 1U;
    }

    if ((status & NRF24_STATUS_MAX_RT) != 0U) {
        NRF24L01_ClearIrqFlags(status);
        *success = 0U;
        return 1U;
    }

    return 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_FinishTx）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_FinishTx(void)
{
    NRF24L01_W_CE(0U);
    NRF24L01_Command(NRF24L01_FLUSH_TX);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：启动（对应符号 NRF24L01_StartDiscoverTx）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Service。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void NRF24L01_StartDiscoverTx(void)
{
    NRF24L01_ConfigureTx(s_BroadcastAddr,
                         BROADCAST_CHANNEL,
                         0U,
                         s_DiscoverResponse);
    NRF24L01_SetState(RADIO_DISCOVER_TX_SETTLE);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：初始化（对应符号 NRF24L01_Init）。
 * @调用关系 直接调用者：remote_config.c:RemoteConfig_Init。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param mode 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void NRF24L01_Init(RunMode mode)
{
uint8_t i;

    SPI1_Init();
    NRF24L01_GPIO_Init();
    NRF24L01_W_CE(0U);
    NRF24L01_W_CSN(1U);

    for (i = 0U; i < DEVICE_ID_LEN; i++) {
        g_MyID[i] = 0U;
        g_NrfMasterId[i] = 0U;
    }

    for (i = 0U; i < 5U; i++) {
        g_SlaveAddr[i] = 0U;
        s_RemoteMasterAddr[i] = 0U;
        g_NrfMasterAddrAdvertised[i] = 0U;
        g_NrfMasterAddrDerived[i] = 0U;
        g_NrfResponseTargetAddr[i] = 0U;
    }

    NRF24L01_ClearSoftwareQueue();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_AppTxResult = NRF_ASYNC_TX_IDLE;
    s_ControlTxActive = 0U;
    g_NrfMasterLinkValid = 0U;
    g_NrfMasterLinkMismatch = 0U;

    g_RunMode = mode;
    g_SlaveState = SLAVE_IDLE;
    g_NrfInitResult = 0xFFU;

    if (mode != MODE_SLAVE) {
        g_NrfInitResult = 2U;
        NRF24L01_SetState(RADIO_ERROR);
        return;
    }

    s_Deadline = HAL_GetTick() + NRF24_POWER_ON_DELAY_MS;
    s_NextPoll = s_Deadline;
    NRF24L01_SetState(RADIO_BOOT_WAIT);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：读取（对应符号 NRF24L01_IsReady）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t NRF24L01_IsReady(void)
{
return g_NrfInitResult == 0U ? 1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_Service）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Run、remote_config.c:Remote_RadioService。
 * @局部状态 主要临时变量：txSuccess；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void NRF24L01_Service(void)
{
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    uint8_t txSuccess = 0U;

    g_NrfServiceCallCount++;
    g_NrfRadioState = (uint8_t)s_RadioState;
    g_NrfLastDriverState = (uint8_t)s_RadioState;

    switch (s_RadioState) {
    case RADIO_BOOT_WAIT:
        if (Delay_DeadlineReached(s_Deadline) == 0U) {
            break;
        }

        NRF24L01_GenerateDeviceID();
        NRF24L01_MakeAddress(g_SlaveAddr, g_MyID, 0xC3U);
        g_SlaveChannel =
            NRF24L01_MakeChannelFromID(g_MyID, 53U);

        g_NrfInitResult = NRF24L01_RegisterCheck();

        if (g_NrfInitResult != 0U) {
            NRF24L01_SetState(RADIO_ERROR);
            break;
        }

        NRF24L01_EnterBroadcastRx();
        break;

    case RADIO_BROADCAST_RX_SETTLE:
        if (Delay_DeadlineReached(s_Deadline) != 0U) {
            NRF24L01_W_CE(1U);
            s_NextPoll = HAL_GetTick();
            NRF24L01_SetState(RADIO_BROADCAST_RX);
        }
        break;

    case RADIO_BROADCAST_RX:
        (void)NRF24L01_DrainRx(1U);
        break;

    case RADIO_DISCOVER_DELAY:
        if (Delay_DeadlineReached(s_Deadline) != 0U) {
            NRF24L01_StartDiscoverTx();
        }
        break;

    case RADIO_DISCOVER_TX_SETTLE:
        if (Delay_DeadlineReached(s_Deadline) != 0U) {
            NRF24L01_W_CE(1U);
            s_Deadline = HAL_GetTick() + DATA_TIMEOUT_MS;
            s_NextPoll = HAL_GetTick();
            NRF24L01_SetState(RADIO_DISCOVER_TX_WAIT);
        }
        break;

    case RADIO_DISCOVER_TX_WAIT:
        if (NRF24L01_ReadTxOutcome(&txSuccess) != 0U) {
            NRF24L01_FinishTx();

            if (txSuccess != 0U) {
                s_DiscoverAnySuccess = 1U;
                g_NrfDiscoverTxOk++;
                g_NrfTxSuccessCount++;
            } else {
                g_NrfTxFailCount++;
            }

            if (s_DiscoverCopiesRemaining > 0U) {
                s_DiscoverCopiesRemaining--;
            }

            if (s_DiscoverCopiesRemaining == 0U) {
                if (s_DiscoverAnySuccess != 0U) {
                    NRF24L01_EnterWaitConnectBroadcastRx();
                } else {
                    NRF24L01_EnterBroadcastRx();
                }
            } else {
                s_DiscoverGapSeed =
                    (uint8_t)(s_DiscoverGapSeed + 17U);
                s_Deadline =
                    HAL_GetTick() + NRF24_DISCOVER_GAP_MS +
                    (uint32_t)(s_DiscoverGapSeed % 11U);
                NRF24L01_SetState(RADIO_DISCOVER_GAP);
            }
        } else if (Delay_DeadlineReached(s_Deadline) != 0U) {
            NRF24L01_FinishTx();
            g_NrfTxTimeoutCount++;

            if (s_DiscoverCopiesRemaining > 0U) {
                s_DiscoverCopiesRemaining--;
            }

            if (s_DiscoverCopiesRemaining == 0U) {
                if (s_DiscoverAnySuccess != 0U) {
                    NRF24L01_EnterWaitConnectBroadcastRx();
                } else {
                    NRF24L01_EnterBroadcastRx();
                }
            } else {
                s_DiscoverGapSeed =
                    (uint8_t)(s_DiscoverGapSeed + 17U);
                s_Deadline =
                    HAL_GetTick() + NRF24_DISCOVER_GAP_MS +
                    (uint32_t)(s_DiscoverGapSeed % 11U);
                NRF24L01_SetState(RADIO_DISCOVER_GAP);
            }
        }
        break;

    case RADIO_DISCOVER_GAP:
        if (Delay_DeadlineReached(s_Deadline) != 0U) {
            NRF24L01_StartDiscoverTx();
        }
        break;

    case RADIO_PRIVATE_RX_SETTLE:
        if (Delay_DeadlineReached(s_Deadline) != 0U) {
            NRF24L01_W_CE(1U);
            s_NextPoll = HAL_GetTick();
            NRF24L01_SetState(RADIO_PRIVATE_RX);
        }
        break;

    case RADIO_PRIVATE_RX:
        (void)NRF24L01_DrainRx(0U);

        if (g_SlaveState == SLAVE_WAIT_CONNECT) {
            if (Delay_DeadlineReached(s_ConnectDeadline) != 0U) {
                g_NrfConnectedTimeoutCount++;
                NRF24L01_EnterBroadcastRx();
            }
        } else if ((uint32_t)(HAL_GetTick() -
                              g_SlaveLastActivity) >
                   HEARTBEAT_TIMEOUT_MS) {
            g_NrfConnectedTimeoutCount++;
            NRF24L01_EnterBroadcastRx();
        }
        break;

    case RADIO_WAIT_CONNECT_BROADCAST_SETTLE:
        if (Delay_DeadlineReached(s_Deadline) != 0U) {
            NRF24L01_W_CE(1U);
            s_NextPoll = HAL_GetTick();
            NRF24L01_SetState(RADIO_WAIT_CONNECT_BROADCAST_RX);
        }
        break;

    case RADIO_WAIT_CONNECT_BROADCAST_RX:
        (void)NRF24L01_DrainRx(1U);

        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        if (s_RadioState == RADIO_WAIT_CONNECT_BROADCAST_RX) {
            if (Delay_DeadlineReached(s_ConnectDeadline) != 0U) {
                g_NrfConnectedTimeoutCount++;
                NRF24L01_EnterBroadcastRx();
            } else if (Delay_DeadlineReached(
                           s_WaitConnectWindowDeadline) != 0U) {
                NRF24L01_ReturnPrivateRxWaitConnect();
            }
        }
        break;

    case RADIO_APP_TX_SETTLE:
        if (Delay_DeadlineReached(s_Deadline) != 0U) {
            NRF24L01_W_CE(1U);
            s_Deadline = HAL_GetTick() + DATA_TIMEOUT_MS;
            s_NextPoll = HAL_GetTick();
            NRF24L01_SetState(RADIO_APP_TX_WAIT);
        }
        break;

    case RADIO_APP_TX_WAIT:
        if (NRF24L01_ReadTxOutcome(&txSuccess) != 0U) {
            NRF24L01_FinishTx();

            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            if (s_ControlTxActive != 0U) {
                s_ControlTxActive = 0U;
                if (txSuccess != 0U) {
                    g_NrfTxSuccessCount++;
                    g_SlaveLastActivity = HAL_GetTick();
                    NRF24L01_ReturnPrivateRx();
                } else {
                    g_NrfTxFailCount++;
                    NRF24L01_ReturnPrivateRx();
                }
                break;
            }

            if (txSuccess != 0U) {
                s_AppTxResult = NRF_ASYNC_TX_OK;
                g_NrfTxSuccessCount++;
                g_NrfAppTxCompleteCount++;
                g_SlaveLastActivity = HAL_GetTick();
            } else {
                s_AppTxResult = NRF_ASYNC_TX_FAILED;
                g_NrfTxFailCount++;
            }

            NRF24L01_ReturnPrivateRx();
        } else if (Delay_DeadlineReached(s_Deadline) != 0U) {
            NRF24L01_FinishTx();
            g_NrfTxTimeoutCount++;
            if (s_ControlTxActive != 0U) {
                s_ControlTxActive = 0U;
                NRF24L01_ReturnPrivateRx();
            } else {
                s_AppTxResult = NRF_ASYNC_TX_FAILED;
                NRF24L01_ReturnPrivateRx();
            }
        }
        break;

    case RADIO_ERROR:
    default:
        break;
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：执行内部处理（对应符号 NRF24L01_Run）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void NRF24L01_Run(void)
{
    NRF24L01_Service();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：接收（对应符号 NRF24L01_SlaveRecv）。
 * @调用关系 直接调用者：remote_config.c:Remote_RadioService。
 * @局部状态 主要临时变量：i、item；均只在本次调用中有效。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param capacity 按值传入；函数内部修改不会回写调用方变量。
 * @param len 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t NRF24L01_SlaveRecv(uint8_t *data, uint8_t capacity, uint8_t *len)
{
uint8_t i;
    NrfAppRxItem *item;

    if (data == NULL ||
        len == NULL ||
        capacity == 0U ||
        s_RxQueueCount == 0U) {
        return 0U;
    }

    item = &s_RxQueue[s_RxQueueHead];

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (item->len > capacity) {
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    *len = item->len;
        return 0U;
    }

    *len = item->len;

    for (i = 0U; i < item->len; i++) {
        data[i] = item->data[i];
    }

    s_RxQueueHead =
        (uint8_t)((s_RxQueueHead + 1U) %
                  NRF24_APP_RX_QUEUE_DEPTH);
    s_RxQueueCount--;
    g_NrfRxQueueLevel = s_RxQueueCount;

    return 1U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：编码并发送（对应符号 NRF24L01_SlaveSendStart）。
 * @调用关系 直接调用者：remote_config.c:Remote_TxService。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t NRF24L01_SlaveSendStart(const uint8_t *data, uint8_t len)
{
uint8_t i;

    if (data == NULL ||
        len == 0U ||
        len > NRF24_USER_DATA_MAX ||
        s_RadioState != RADIO_PRIVATE_RX ||
        g_SlaveState != SLAVE_CONNECTED ||
        s_AppTxResult != NRF_ASYNC_TX_IDLE ||
        g_NrfMasterLinkValid == 0U) {
        g_NrfAppTxRejectCount++;
        return 0U;
    }

    NRF24L01_PrepareDataPacket(s_TxPacket, data, len);

    for (i = 0U; i < 5U; i++) {
        g_NrfResponseTargetAddr[i] =
            s_RemoteMasterAddr[i];
    }

    g_NrfResponseTargetChannel =
        s_RemoteMasterChannel;

    s_AppTxResult = NRF_ASYNC_TX_BUSY;
    g_NrfAppTxStartCount++;

    NRF24L01_W_CE(0U);
    NRF24L01_ConfigureTx(s_RemoteMasterAddr,
                         s_RemoteMasterChannel,
                         1U,
                         s_TxPacket);
    NRF24L01_SetState(RADIO_APP_TX_SETTLE);

    return 1U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：读取（对应符号 NRF24L01_SlaveSendGetResult）。
 * @调用关系 直接调用者：remote_config.c:Remote_TxService。
 * @局部状态 主要临时变量：result；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
NrfAsyncTxResult NRF24L01_SlaveSendGetResult(void)
{
    NrfAsyncTxResult result;

    result = s_AppTxResult;

    if (result == NRF_ASYNC_TX_OK ||
        result == NRF_ASYNC_TX_FAILED) {
        s_AppTxResult = NRF_ASYNC_TX_IDLE;
    }

    return result;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：编码并发送（对应符号 NRF24L01_SlaveSendBusy）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t NRF24L01_SlaveSendBusy(void)
{
return s_AppTxResult == NRF_ASYNC_TX_BUSY ?
           1U : 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief nRF24L01 无线收发：读取（对应符号 NRF24L01_GetStateString）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
const char *NRF24L01_GetStateString(void)
{
    switch (s_RadioState) {
    case RADIO_BOOT_WAIT:
        return "BOOT";
    case RADIO_BROADCAST_RX_SETTLE:
        return "BCAST_RX_SETTLE";
    case RADIO_BROADCAST_RX:
        return "BCAST_RX";
    case RADIO_DISCOVER_DELAY:
        return "DISC_DELAY";
    case RADIO_DISCOVER_TX_SETTLE:
        return "DISC_TX_SETTLE";
    case RADIO_DISCOVER_TX_WAIT:
        return "DISC_TX_WAIT";
    case RADIO_DISCOVER_GAP:
        return "DISC_GAP";
    case RADIO_PRIVATE_RX_SETTLE:
        return "PRIVATE_RX_SETTLE";
    case RADIO_PRIVATE_RX:
        return "PRIVATE_RX";
    case RADIO_APP_TX_SETTLE:
        return "APP_TX_SETTLE";
    case RADIO_APP_TX_WAIT:
        return "APP_TX_WAIT";
    case RADIO_ERROR:
    default:
        return "ERROR";
    }
}
