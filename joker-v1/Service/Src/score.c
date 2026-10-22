/* AUTO-FILE-DOC
 * 文件职责：老虎机得分计算。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "score.h"

#define SCORE_RUN_TABLE_SIZE             SCORE_MIDDLE_CHANNEL_COUNT

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 老虎机得分计算：读取（对应符号 Score_GetMiddleMask）。
 * @调用关系 直接调用者：score.c:Score_CalcLineScore、score.c:Score_GetJackpotTier。
 * @param eye_channel_mask 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Score_GetMiddleMask(uint16_t eye_channel_mask)
{
    return (uint8_t)((eye_channel_mask & SCORE_MIDDLE_EYE_MASK) >> SCORE_MIDDLE_FIRST_CHANNEL);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 老虎机得分计算：构造（对应符号 Score_BuildRunTable）。
 * @调用关系 直接调用者：score.c:Score_CalcLineScore、score.c:Score_GetJackpotTier。
 * @局部状态 主要临时变量：bit_index、run_count、current_run；均只在本次调用中有效。
 * @param middle_mask 按值传入；函数内部修改不会回写调用方变量。
 * @param run_table 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Score_BuildRunTable(uint8_t middle_mask, uint8_t *run_table)
{
    uint8_t bit_index;
    uint8_t run_count = 0U;
    uint8_t current_run = 0U;

    for (bit_index = 0U; bit_index < SCORE_MIDDLE_CHANNEL_COUNT; bit_index++)
    {
        if ((middle_mask & (uint8_t)(1U << bit_index)) != 0U)
        {
            current_run++;
        }
        else
        {
            if (current_run != 0U)
            {
                run_table[run_count] = current_run;
                run_count++;
                current_run = 0U;
            }
        }
    }

    if (current_run != 0U)
    {
        run_table[run_count] = current_run;
        run_count++;
    }

    return run_count;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 老虎机得分计算：执行内部处理（对应符号 Score_HasRunAtLeast）。
 * @调用关系 直接调用者：score.c:Score_CalcLineScore、score.c:Score_GetJackpotTier。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @param run_table 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param run_count 按值传入；函数内部修改不会回写调用方变量。
 * @param need_len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Score_HasRunAtLeast(const uint8_t *run_table, uint8_t run_count, uint8_t need_len)
{
    uint8_t i;

    for (i = 0U; i < run_count; i++)
    {
        if (run_table[i] >= need_len)
        {
            return 1U;
        }
    }

    return 0U;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 老虎机得分计算：执行内部处理（对应符号 Score_HasTwoGroups）。
 * @调用关系 直接调用者：score.c:Score_CalcLineScore。
 * @局部状态 主要临时变量：i、j；均只在本次调用中有效。
 * @param run_table 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param run_count 按值传入；函数内部修改不会回写调用方变量。
 * @param first_len 按值传入；函数内部修改不会回写调用方变量。
 * @param second_len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Score_HasTwoGroups(const uint8_t *run_table,
                                  uint8_t run_count,
                                  uint8_t first_len,
                                  uint8_t second_len)
{
    uint8_t i;
    uint8_t j;

    for (i = 0U; i < run_count; i++)
    {
        if (run_table[i] < first_len)
        {
            continue;
        }

        for (j = 0U; j < run_count; j++)
        {
            if (i == j)
            {
                continue;
            }

            if (run_table[j] >= second_len)
            {
                return 1U;
            }
        }
    }

    return 0U;
}

/**
 * @brief 老虎机得分计算：初始化（对应符号 Score_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Score_Init(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
}

/**
 * @brief 老虎机得分计算：执行内部处理（对应符号 Score_HasMiddleChannel）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param eye_channel_mask 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Score_HasMiddleChannel(uint16_t eye_channel_mask)
{
    return ((eye_channel_mask & SCORE_MIDDLE_EYE_MASK) != 0U) ? 1U : 0U;
}

/**
 * @brief 老虎机得分计算：执行内部处理（对应符号 Score_CalcLineScore）。
 * @调用关系 直接调用者：game.c:Game_Task、gift_algorithm.c:GiftAlgorithm_CalcFutureRisk、gift_algorithm.c:GiftAlgorithm_CalcCandidateCost、gift_algorithm.c:GiftAlgorithm_ChoosePairFirst、gift_algorithm.c:GiftAlgorithm_BuildCandidates、gift_algorithm.c:GiftAlgorithm_ChooseManualRevealOrder、gift_algorithm.c:GiftAlgorithm_EnumerateCandidates、standby.c:Standby_Task。
 * @局部状态 主要临时变量：middle_mask、run_table、run_count；均只在本次调用中有效。
 * @param eye_channel_mask 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Score_CalcLineScore(uint16_t eye_channel_mask)
{
    uint8_t middle_mask;
    uint8_t run_table[SCORE_RUN_TABLE_SIZE] = {0U};
    uint8_t run_count;

    middle_mask = Score_GetMiddleMask(eye_channel_mask);
    run_count = Score_BuildRunTable(middle_mask, run_table);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Score_HasRunAtLeast(run_table, run_count, 7U) != 0U)
    {
        return SCORE_AWARD_LINE_7;
    }

    if (Score_HasRunAtLeast(run_table, run_count, 6U) != 0U)
    {
        return SCORE_AWARD_LINE_6;
    }

    if (Score_HasRunAtLeast(run_table, run_count, 5U) != 0U)
    {
        return SCORE_AWARD_LINE_5;
    }

    if (Score_HasTwoGroups(run_table, run_count, 2U, 4U) != 0U)
    {
        return SCORE_AWARD_LINE_2_PLUS_4;
    }

    if (Score_HasRunAtLeast(run_table, run_count, 4U) != 0U)
    {
        return SCORE_AWARD_LINE_4;
    }

    if (Score_HasTwoGroups(run_table, run_count, 3U, 3U) != 0U)
    {
        return SCORE_AWARD_TWO_LINE_3;
    }

    if (Score_HasTwoGroups(run_table, run_count, 2U, 3U) != 0U)
    {
        return SCORE_AWARD_LINE_2_PLUS_3;
    }

    if (Score_HasTwoGroups(run_table, run_count, 2U, 2U) != 0U)
    {
        return SCORE_AWARD_TWO_LINE_2;
    }

    if (Score_HasRunAtLeast(run_table, run_count, 3U) != 0U)
    {
        return SCORE_AWARD_LINE_3;
    }

    if (Score_HasRunAtLeast(run_table, run_count, 2U) != 0U)
    {
        return SCORE_AWARD_LINE_2;
    }

    return 0U;
}

/**
 * @brief 老虎机得分计算：读取（对应符号 Score_GetJackpotTier）。
 * @调用关系 直接调用者：game.c:Game_BeginLineSettlement、gift_algorithm.c:GiftAlgorithm_GetLineCount、gift_algorithm.c:GiftAlgorithm_ScoreManualCandidate。
 * @局部状态 主要临时变量：middle_mask、run_table、run_count；均只在本次调用中有效。
 * @param eye_channel_mask 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Score_GetJackpotTier(uint16_t eye_channel_mask)
{
    uint8_t middle_mask;
    uint8_t run_table[SCORE_RUN_TABLE_SIZE] = {0U};
    uint8_t run_count;

    middle_mask = Score_GetMiddleMask(eye_channel_mask);
    run_count = Score_BuildRunTable(middle_mask, run_table);

    if (Score_HasRunAtLeast(run_table, run_count, 7U) != 0U)
    {
        return 7U;
    }
    if (Score_HasRunAtLeast(run_table, run_count, 6U) != 0U)
    {
        return 6U;
    }
    if (Score_HasRunAtLeast(run_table, run_count, 5U) != 0U)
    {
        return 5U;
    }

    return 0U;
}
