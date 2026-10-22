/* AUTO-FILE-DOC
 * 文件职责：故障管理。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef FAULT_H
#define FAULT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    FAULT_CODE_NONE = 0U,
    FAULT_CODE_E01 = 1U,
    FAULT_CODE_E02,
    FAULT_CODE_E03,
    FAULT_CODE_E04,
    FAULT_CODE_E05,
    FAULT_CODE_E06,
    FAULT_CODE_E07,
    FAULT_CODE_E08,
    FAULT_CODE_E09,
    FAULT_CODE_E10,
    FAULT_CODE_E11,
    FAULT_CODE_E12,
    FAULT_CODE_E13,
    FAULT_CODE_E14,
    FAULT_CODE_E15,
    /* 游戏马达计数已确认，但15秒内1~9路落币光眼均无结果。 */
    FAULT_CODE_E16,
    /* 分机已请求5/6/7连线彩金，但30秒内未收到主机下发。 */
    FAULT_CODE_E17
} Fault_Code;

void Fault_Init(void);
void Fault_Task(void);
uint8_t Fault_IsActive(void);
Fault_Code Fault_GetCode(void);
void Fault_Report(Fault_Code code);
void Fault_Clear(void);

#ifdef __cplusplus
}
#endif

#endif
