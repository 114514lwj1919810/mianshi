/* AUTO-FILE-DOC
 * 文件职责：手持设备页面状态机与协议调度。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "app_main.h"

#include "app_watchdog.h"
#include "motor.h"
#include "solenoid.h"
#include "ticket.h"

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 手持设备页面状态机与协议调度：执行内部处理（对应符号 main）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
int main(void)
{
    /* Clamp both motor direction pairs before enabling interrupts or App_Init. */
    Motor_EarlySafeInit();
    __enable_irq();
    AppWatchdog_Feed();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Solenoid_BootSafeInit();
    Ticket_BootSafeInit();

    App_Init();

    while (1)
    {
        App_Loop();
    }
}
