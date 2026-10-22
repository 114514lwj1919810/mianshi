/* AUTO-FILE-DOC
 * 文件职责：IAP 分区信息。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "app_slot.h"

#include "gd32f4xx.h"

/**
 * @brief IAP 分区信息：设置（对应符号 AppSlot_SetVectorTable）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：app_base；均只在本次调用中有效。
 * @param slot 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t AppSlot_SetVectorTable(char slot)
{
    uint32_t app_base;

    if ((slot == 'A') || (slot == 'a'))
    {
        app_base = APP_SLOT_A_BASE_ADDR;
    }
    else if ((slot == 'B') || (slot == 'b'))
    {
        app_base = APP_SLOT_B_BASE_ADDR;
    }
    else
    {
        return 0U;
    }

    SCB->VTOR = app_base;
    __DSB();
    __ISB();
    return 1U;
}
