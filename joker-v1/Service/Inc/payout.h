/* AUTO-FILE-DOC
 * 文件职责：奖励下发流程。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef PAYOUT_H
#define PAYOUT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Payout_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Payout_RequestExchange(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Payout_RequestTicket(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Payout_StartScore(uint16_t payout_count);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Payout_AddPrize(uint16_t payout_count);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Payout_Task(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint16_t Payout_TakePaidCount(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint16_t Payout_GetRemaining(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Payout_IsBusy(void);

/* 长按消币：暂停不清余数，松手恢复；跳过量由游戏层直接记账。 */
void Payout_Pause(void);
void Payout_Resume(void);
uint8_t Payout_IsPaused(void);
uint16_t Payout_SkipRemaining(void);

#ifdef __cplusplus
}
#endif

#endif
