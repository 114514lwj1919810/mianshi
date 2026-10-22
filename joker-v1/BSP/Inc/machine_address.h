/* AUTO-FILE-DOC
 * 文件职责：机器地址读取。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef MACHINE_ADDRESS_H
#define MACHINE_ADDRESS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MACHINE_ADDRESS_MIN_ID       0U
#define MACHINE_ADDRESS_MAX_ID       7U
#define MACHINE_ADDRESS_DEFAULT_ID   0U

/*
 * SW2 uses PC0/PC1/PC2 as bit0/bit1/bit2.  Each switch closes to GND, so an
 * ON switch is read as a logical 1.  000 selects master; 001..111 select
 * slave addresses 1..7.  The selector is sampled once during App_Init().
 */
void MachineAddress_Init(void);
void MachineAddress_Load(void);
uint8_t MachineAddress_Read(void);
uint8_t MachineAddress_ReadDip(void);
uint8_t MachineAddress_IsNetworkEnabled(void);
uint8_t MachineAddress_GetNodeId(void);
uint8_t MachineAddress_SetConfig(uint8_t network_enabled, uint8_t node_id);

/* 兼容旧调用名：现在 mode value 只代表 0~7 机台号，不再代表拨码模式。 */
uint8_t MachineAddress_GetModeValue(void);
uint8_t MachineAddress_SetModeValue(uint8_t value);

#ifdef __cplusplus
}
#endif

#endif
