/* AUTO-FILE-DOC
 * 文件职责：出票器状态机。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef TICKET_H
#define TICKET_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * 彩票器/卡片机接口：
 * - PD10 / CN2-H7 / IN：主板输出，高电平启动设备，低电平停止设备。
 * - PF9  / P1-K13 / OUT：设备反馈输入，每吐出一张票或一张卡产生一个低有效脉冲。
 *
 * PB13 不是彩票器电机控制脚，它只输出“出票码表”脉冲。只有 PF9 确认真实
 * 吐出后，Game 层才会给 PB13 补同样数量的码表脉冲。
 */
#define TICKET_IN_PORT                  GPIOD
#define TICKET_IN_PIN                   GPIO_PIN_10
#define TICKET_RUN_LEVEL                GPIO_PIN_SET
#define TICKET_STOP_LEVEL               GPIO_PIN_RESET

/* 连续 5 秒收不到 PF9 新反馈时停止 PD10，避免缺票、卡票或断线时电机一直运行。 */
#define TICKET_NO_FEEDBACK_TIMEOUT_MS   5000U
#define TICKET_PENDING_MAX              60000UL

/** @brief 在 HAL 初始化之前尽早把 PD10 固定为停止低电平。 */
void Ticket_BootSafeInit(void);

/** @brief 初始化彩票器控制输出，并清空内部任务。 */
void Ticket_Init(void);

/**
 * @brief 推进一次非阻塞出票任务。
 *
 * 每轮主循环调用。函数读取 PF9 已确认的反馈数量，每收到一张就减少一个剩余目标；
 * 剩余数为 0 时立即关闭 PD10。函数内部没有 HAL_Delay() 或等待循环。
 */
void Ticket_Task(void);

/**
 * @brief 追加需要实际吐出的彩票或卡片数量。
 * @return 1 表示已加入任务；0 表示参数为 0 或累计数量超过安全上限。
 */
uint8_t Ticket_Add(uint32_t ticket_count);

/** @brief 取走从上次读取以来 PF9 实际确认吐出的数量。 */
uint32_t Ticket_TakeDispensedCount(void);

/** @brief 返回尚未吐出的剩余数量。 */
uint32_t Ticket_GetRemaining(void);

/**
 * @brief 立即停止当前出票批次，并取走尚未实际吐出的数量。
 *
 * 已经由 PF9 确认的票仍由 Ticket_TakeDispensedCount() 正常上账；本函数只返回
 * 尚未确认的剩余票数，供 Game 层直接计入账目，不再驱动出票器和出票码表。
 */
uint32_t Ticket_SkipRemaining(void);

/** @brief 返回是否存在运行中或暂停中的未完成出票批次。 */
uint8_t Ticket_IsBusy(void);

/** @brief 返回是否因为 5 秒没有 PF9 反馈而停机。 */
uint8_t Ticket_HasFault(void);

/** @brief 清除无反馈故障，保留剩余数量并在下一轮继续。 */
void Ticket_ClearFault(void);

/** @brief 暂停设备，保留剩余数量，供整机故障状态使用。 */
void Ticket_Pause(void);

/** @brief 恢复被 Ticket_Pause() 暂停的任务。 */
void Ticket_Resume(void);

/** @brief 停止设备并清空全部待出票/卡数量。 */
void Ticket_Stop(void);

/** @brief 板级测试：追加 1 张，仍由 PF9 反馈闭环停止。 */
void Ticket_Test1(void);

#ifdef __cplusplus
}
#endif

#endif
