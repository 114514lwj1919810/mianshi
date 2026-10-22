/* AUTO-FILE-DOC
 * 文件职责：运行限时与解码锁。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef MACHINE_LOCK_H
#define MACHINE_LOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void MachineLock_Init(void);

/*
 * Call once per game loop after the 10 ms key scan.  post_done keeps all
 * operator/code screens disabled during power-on self test.  safe_to_stop
 * must only be set while no motor, ticket, lottery or settlement is active.
 * The return value is 1 while normal game input/state processing must stop.
 */
uint8_t MachineLock_Task(uint8_t post_done, uint8_t safe_to_stop);
uint8_t MachineLock_IsBlocking(void);
uint8_t MachineLock_IsLocked(void);
/** P9恢复默认时只重置A0工作人员密码，不改变锁定状态、A1和运行累计。 */
void MachineLock_ResetStaffPassword(void);

/* Public for the PC decoder utility and deterministic firmware tests. */
uint32_t MachineLock_CalculateCode(uint32_t runtime_minutes,
                                   uint32_t coin_in,
                                   uint32_t coin_out,
                                   uint32_t check_code,
                                   int32_t profit,
                                   uint32_t lock_count);

#ifdef __cplusplus
}
#endif

#endif
