/* AUTO-FILE-DOC
 * 文件职责：应用看门狗。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef APP_WATCHDOG_H
#define APP_WATCHDOG_H

#ifdef __cplusplus
extern "C" {
#endif

void AppWatchdog_Init(void);
void AppWatchdog_Feed(void);

#ifdef __cplusplus
}
#endif

#endif
