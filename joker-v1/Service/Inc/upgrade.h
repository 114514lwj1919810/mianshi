/* AUTO-FILE-DOC
 * 文件职责：升级兼容入口。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef UPGRADE_H
#define UPGRADE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

/* POST 自检结束后调用：0=无需处理，1=已清理且需要复位，2=EEPROM故障。 */
uint8_t Upgrade_PrepareTrialRelease(void);
uint8_t Upgrade_ResetOnNewRelease(void);

#ifdef __cplusplus
}
#endif

#endif
