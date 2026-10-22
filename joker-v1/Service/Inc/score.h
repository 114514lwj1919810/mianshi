/* AUTO-FILE-DOC
 * 文件职责：老虎机得分计算。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef SCORE_H
#define SCORE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

#define SCORE_MIDDLE_FIRST_CHANNEL       1U
#define SCORE_MIDDLE_CHANNEL_COUNT       7U
#define SCORE_MIDDLE_EYE_MASK            ((uint16_t)0x00FEU)

/*
 * 连线固定奖的唯一来源。游戏状态机、送灯预算和演示都引用这些宏，
 * 避免只改显示分数却遗漏7连锁、语音或算法预算。
 */
#define SCORE_AWARD_LINE_2               4U
#define SCORE_AWARD_LINE_3               6U
#define SCORE_AWARD_TWO_LINE_2           8U
#define SCORE_AWARD_LINE_2_PLUS_3        10U
#define SCORE_AWARD_LINE_4               12U
#define SCORE_AWARD_TWO_LINE_3           12U
#define SCORE_AWARD_LINE_2_PLUS_4        16U
#define SCORE_AWARD_LINE_5               30U
#define SCORE_AWARD_LINE_6               55U
#define SCORE_AWARD_LINE_7               90U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Score_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Score_HasMiddleChannel(uint16_t eye_channel_mask);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint16_t Score_CalcLineScore(uint16_t eye_channel_mask);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
uint8_t Score_GetJackpotTier(uint16_t eye_channel_mask);

#ifdef __cplusplus
}
#endif

#endif
