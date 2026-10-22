/* AUTO-FILE-DOC
 * 文件职责：应用看门狗。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "app_watchdog.h"

#include "gd32f4xx.h"

#define APP_FWDGT_RELOAD_MAX 0x0FFFU

/**
 * @brief 应用看门狗：初始化（对应符号 AppWatchdog_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void AppWatchdog_Init(void)
{
    fwdgt_config(APP_FWDGT_RELOAD_MAX, FWDGT_PSC_DIV256);
    fwdgt_enable();
    fwdgt_counter_reload();
}

/**
 * @brief 应用看门狗：执行内部处理（对应符号 AppWatchdog_Feed）。
 * @调用关系 直接调用者：app_main.c:App_Loop、main.c:main。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void AppWatchdog_Feed(void)
{
    fwdgt_counter_reload();
}
