/* AUTO-FILE-DOC
 * 文件职责：彩金计算与联机事件。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef JACKPOT_H
#define JACKPOT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JACKPOT_REGISTER_CONTRIBUTION_HIGH  0U
#define JACKPOT_REGISTER_CONTRIBUTION_LOW   1U
#define JACKPOT_REGISTER_EVENT              2U
#define JACKPOT_REGISTER_DOWNLINK           3U
#define JACKPOT_REGISTER_CONTRIBUTION_DIVISOR 4U

/*
 * 开启5/6/7连线附带的20%/50%/100%彩金。
 * 彩金与30/55/90基础奖励分批进入当前出币/彩票/卡片结算流程。
 */
#define JACKPOT_LINE_BONUS_ENABLED          1U

void Jackpot_Init(void);
/* 升级时恢复 P14 默认基值并清除待处理的联机彩金事务。 */
uint8_t Jackpot_ResetForUpgrade(void);
void Jackpot_Task(void);
/* START落点采集期间暂停后台EEPROM保存，离开START后恢复。 */
void Jackpot_SetStorageDeferred(uint8_t deferred);
void Jackpot_SetOperational(uint8_t enabled);

void Jackpot_AddLocalCoins(uint32_t count);
void Jackpot_AddLocalPayout(uint32_t count);
uint32_t Jackpot_GetLocalPayoutTotal(void);
void Jackpot_ClearLocalPayoutTotal(void);
uint8_t Jackpot_RequestAward(uint8_t line_count);
uint8_t Jackpot_TakeAward(uint16_t *award);
/* Inspect without ACK; confirm only after game balances are durable. */
uint8_t Jackpot_PeekAward(uint16_t *award);
uint8_t Jackpot_ConfirmAward(void);
uint8_t Jackpot_GetPendingTier(void);

uint16_t Jackpot_GetCurrent(void);
/* EEPROM 保存的异常累计次数；节点 0~7 为贡献跳变，0xFF 为本机值异常。 */
uint16_t Jackpot_GetAnomalyCount(void);
uint8_t Jackpot_GetLastAnomalyNode(void);
uint8_t Jackpot_IsBaseEditable(void);

#ifdef __cplusplus
}
#endif

#endif
