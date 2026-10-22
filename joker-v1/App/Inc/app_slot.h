/* AUTO-FILE-DOC
 * 文件职责：IAP 分区信息。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef APP_SLOT_H
#define APP_SLOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* GD32 layout. Legacy VTOR helper only; NOT a bootloader or a slot jump. */
#define APP_SLOT_A_BASE_ADDR 0x08020000UL
#define APP_SLOT_B_BASE_ADDR 0x08060000UL

uint8_t AppSlot_SetVectorTable(char slot);

#ifdef __cplusplus
}
#endif

#endif
