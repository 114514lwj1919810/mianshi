/* AUTO-FILE-DOC
 * 文件职责：SPI 总线驱动。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#include "spi.h"

SPI_HandleTypeDef hspi1;

volatile uint32_t g_spi1_error_count;
volatile HAL_StatusTypeDef g_spi1_last_status = HAL_OK;

static uint8_t s_spi1_initialized;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief SPI 总线驱动：初始化（对应符号 SPI1_Init）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_Init、remote_config.c:RemoteConfig_Init。
 * @局部状态 主要临时变量：gpio；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void SPI1_Init(void)
{
    GPIO_InitTypeDef gpio = {0};

    if (s_spi1_initialized != 0U) {
        return;
    }

    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_SPI1_CLK_ENABLE();
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

    
    gpio.Pin = GPIO_PIN_5 | GPIO_PIN_7;
    gpio.Mode = GPIO_MODE_AF_PP;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    gpio.Alternate = GPIO_AF5_SPI1;    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_Init(GPIOA, &gpio);

    
    gpio.Pin = GPIO_PIN_6;
    gpio.Pull = GPIO_PULLUP;    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    HAL_GPIO_Init(GPIOA, &gpio);

    hspi1.Instance = SPI0;
    hspi1.Init.Mode = SPI_MODE_MASTER;
    hspi1.Init.Direction = SPI_DIRECTION_2LINES;
    hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi1.Init.NSS = SPI_NSS_SOFT;

    



    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_128;    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi1.Init.CRCPolynomial = 7;
    hspi1.Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    hspi1.Init.TxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi1.Init.RxCRCInitializationPattern =
        SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
    hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    hspi1.Init.MasterInterDataIdleness =
        SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;
    hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;

    if (HAL_SPI_Init(&hspi1) != HAL_OK) {
        g_spi1_error_count++;
        return;
    }

    



    s_spi1_initialized = 1U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief SPI 总线驱动：执行内部处理（对应符号 SPI1_TransferByte）。
 * @调用关系 直接调用者：spi.c:SPI1_WriteByte。
 * @局部状态 主要临时变量：dummy、status；均只在本次调用中有效。
 * @param tx 按值传入；函数内部修改不会回写调用方变量。
 * @param rx 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
HAL_StatusTypeDef SPI1_TransferByte(uint8_t tx, uint8_t *rx)
{
uint8_t dummy;
    HAL_StatusTypeDef status;

    if (rx == 0) {
        rx = &dummy;
    }

    status = HAL_SPI_TransmitReceive(&hspi1, &tx, rx, 1U, 2U);    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    g_spi1_last_status = status;

    if (status != HAL_OK) {
        g_spi1_error_count++;
    }

    return status;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief SPI 总线驱动：写入（对应符号 SPI1_WriteByte）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_WriteReg、nrf24l01.c:NRF24L01_ReadReg、nrf24l01.c:NRF24L01_WriteRegs、nrf24l01.c:NRF24L01_ReadRegs、nrf24l01.c:NRF24L01_Command、nrf24l01.c:NRF24L01_WritePayload、nrf24l01.c:NRF24L01_ReadPayload、nrf24l01.c:NRF24L01_ReadStatus。
 * @局部状态 主要临时变量：rx；均只在本次调用中有效。
 * @param byte 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t SPI1_WriteByte(uint8_t byte)
{
uint8_t rx = 0xFFU;

    if (SPI1_TransferByte(byte, &rx) != HAL_OK) {
        return 0xFFU;
    }

    return rx;
}
