/* AUTO-FILE-DOC
 * 文件职责：手持无线业务协议。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef REMOTE_CONFIG_H
#define REMOTE_CONFIG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void RemoteConfig_Init(void);
void RemoteConfig_Task(uint8_t command_enable);
void RemoteConfig_StatusLedTask(void);
uint8_t RemoteConfig_IsSelected(void);
/* Game_Task consumes this one-shot event; it never clears awards or lock state. */
uint8_t RemoteConfig_TakeExitRequest(void);
uint8_t RemoteConfig_TakeAccountPageRequest(void);
uint8_t RemoteConfig_TakeFaultClearRequest(void);

#ifdef __cplusplus
}
#endif

#endif
