/* AUTO-FILE-DOC
 * 文件职责：device_label_wire 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef DEVICE_LABEL_WIRE_H
#define DEVICE_LABEL_WIRE_H
#include <stdint.h>

/* Backward-compatible extension to the 32-byte DISCOVER RESPONSE.
 * Bytes 0..10 remain unchanged: type, UID-derived ID, radio address, channel.
 * [11] marker 'N', [12] version 1, [13..19] seven ASCII characters NNN-NNN.
 * This is a display label, never a radio identity or authentication credential.
 */
#define DEVICE_LABEL_LENGTH 7U
#define DEVICE_LABEL_MARKER 0x4EU
#define DEVICE_LABEL_VERSION 1U

/**
 * @brief device_label_wire 模块：校验（对应符号 DeviceLabel_IsValid）。
 * @调用关系 直接调用者：device_label_wire.h:DeviceLabel_Encode、device_label_wire.h:DeviceLabel_Decode、device_label_wire.h:DeviceLabel_Format。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param label 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static __inline uint8_t DeviceLabel_IsValid(const char *label)
{
    uint8_t i;
    if (label == 0) return 0U;
    for (i = 0U; i < DEVICE_LABEL_LENGTH; ++i) {
        if (i == 3U) {
            if (label[i] != '-') return 0U;
        } else if (label[i] < '0' || label[i] > '9') return 0U;
    }
    return label[DEVICE_LABEL_LENGTH] == '\0' ? 1U : 0U;
}

/**
 * @brief device_label_wire 模块：执行内部处理（对应符号 DeviceLabel_Encode）。
 * @调用关系 直接调用者：nrf24l01.c:NRF24L01_HandleDiscover。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param packet 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param label 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static __inline void DeviceLabel_Encode(uint8_t packet[32], const char *label)
{
    uint8_t i;
    for (i = 11U; i < 20U; ++i) packet[i] = 0U;
    if (DeviceLabel_IsValid(label) == 0U) return;
    packet[11] = DEVICE_LABEL_MARKER;
    packet[12] = DEVICE_LABEL_VERSION;
    for (i = 0U; i < DEVICE_LABEL_LENGTH; ++i) packet[13U + i] = (uint8_t)label[i];
}

/**
 * @brief device_label_wire 模块：执行内部处理（对应符号 DeviceLabel_Decode）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param label 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param packet 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static __inline void DeviceLabel_Decode(char label[8], const uint8_t packet[32])
{
    uint8_t i;
    /* Always clear old scan data, including when receiving a legacy STM board. */
    for (i = 0U; i <= DEVICE_LABEL_LENGTH; ++i) label[i] = '\0';
    if (packet[0] != 0x02U || packet[11] != DEVICE_LABEL_MARKER ||
        packet[12] != DEVICE_LABEL_VERSION) return;
    for (i = 0U; i < DEVICE_LABEL_LENGTH; ++i) label[i] = (char)packet[13U + i];
    if (DeviceLabel_IsValid(label) == 0U) label[0] = '\0';
}

/**
 * @brief device_label_wire 模块：执行内部处理（对应符号 DeviceLabel_Format）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param text 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param label 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param id 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static __inline void DeviceLabel_Format(char text[9], const char *label, const uint8_t id[4])
{
    static const char hex[] = "0123456789ABCDEF";
    uint8_t i;
    if (DeviceLabel_IsValid(label) != 0U) {
        for (i = 0U; i <= DEVICE_LABEL_LENGTH; ++i) text[i] = label[i];
    } else {
        for (i = 0U; i < 4U; ++i) {
            text[i * 2U] = hex[id[i] >> 4U];
            text[i * 2U + 1U] = hex[id[i] & 0x0FU];
        }
        text[8] = '\0';
    }
}
#endif
