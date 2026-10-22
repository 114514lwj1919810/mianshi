/* AUTO-FILE-DOC
 * 文件职责：上电自检。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "self_test.h"

#include "gtim.h"
#include "segment.h"
#include "solenoid.h"

#define SELF_TEST_STABLE_SHOW_MS        2000U
#define SELF_TEST_CLEAR_GAP_MS          200U
#define SELF_TEST_SOLENOID_MS           500U
#define SELF_TEST_FIRST_DIGIT           1U
#define SELF_TEST_LAST_DIGIT            9U
#define SELF_TEST_STEP_MS               500U

typedef enum
{
    SELF_TEST_PHASE_STABLE_123 = 0U,
    SELF_TEST_PHASE_CLEAR_BEFORE_SOLENOID,
    SELF_TEST_PHASE_SOLENOID_HIGH_HOLD,
    SELF_TEST_PHASE_COUNTING,
    SELF_TEST_PHASE_DONE
} SelfTest_Phase;

static SelfTest_Phase s_phase = SELF_TEST_PHASE_DONE;
static uint8_t s_digit = SELF_TEST_FIRST_DIGIT;
static uint32_t s_phase_start_ms = 0U;

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 上电自检：刷新显示（对应符号 SelfTest_ShowStable123）。
 * @调用关系 直接调用者：self_test.c:SelfTest_Init、self_test.c:SelfTest_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void SelfTest_ShowStable123(void)
{
    Segment_ClearAll();
    Segment_SetMiddleNumber(123U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 上电自检：刷新显示（对应符号 SelfTest_ShowDigit）。
 * @调用关系 直接调用者：self_test.c:SelfTest_Task、self_test.c:SelfTest_Task。
 * @局部状态 主要临时变量：value；均只在本次调用中有效。
 * @param digit 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void SelfTest_ShowDigit(uint8_t digit)
{
    uint16_t value;

    value = (uint16_t)((uint16_t)digit * 1111U);
    Segment_SetLeftNumber(value);
    Segment_SetMiddleNumber(value);
    Segment_SetRightNumber(value);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 上电自检：刷新显示（对应符号 SelfTest_ShowIdleZero）。
 * @调用关系 直接调用者：self_test.c:SelfTest_Task、self_test.c:SelfTest_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void SelfTest_ShowIdleZero(void)
{
    Segment_SetLeftNumber(0U);
    Segment_SetMiddleNumber(0U);
    Segment_SetRightNumber(0U);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 上电自检：初始化（对应符号 SelfTest_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void SelfTest_Init(void)
{
    s_phase = SELF_TEST_PHASE_STABLE_123;
    s_digit = SELF_TEST_FIRST_DIGIT;
    s_phase_start_ms = Gtim_NowMs();

    SelfTest_ShowStable123();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 上电自检：周期处理（对应符号 SelfTest_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void SelfTest_Task(void)
{
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    switch (s_phase)
    {
        case SELF_TEST_PHASE_STABLE_123:
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            if (Gtim_IsElapsed(s_phase_start_ms, SELF_TEST_STABLE_SHOW_MS) != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Segment_ClearAll();

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                s_phase = SELF_TEST_PHASE_CLEAR_BEFORE_SOLENOID;
                s_phase_start_ms = Gtim_NowMs();
            }
            break;

        case SELF_TEST_PHASE_CLEAR_BEFORE_SOLENOID:
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            if (Gtim_IsElapsed(s_phase_start_ms, SELF_TEST_CLEAR_GAP_MS) != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Solenoid_SetHigh();

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                s_phase = SELF_TEST_PHASE_SOLENOID_HIGH_HOLD;
                s_phase_start_ms = Gtim_NowMs();
            }
            break;

        case SELF_TEST_PHASE_SOLENOID_HIGH_HOLD:
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            if (Gtim_IsElapsed(s_phase_start_ms, SELF_TEST_SOLENOID_MS) != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Solenoid_SetLow();

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                s_digit = SELF_TEST_FIRST_DIGIT;
                SelfTest_ShowDigit(s_digit);

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                s_phase = SELF_TEST_PHASE_COUNTING;
                s_phase_start_ms = Gtim_NowMs();
            }
            break;

        case SELF_TEST_PHASE_COUNTING:
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            if (Gtim_IsElapsed(s_phase_start_ms, SELF_TEST_STEP_MS) == 0U)
            {
                break;
            }

            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            if (s_digit < SELF_TEST_LAST_DIGIT)
            {
                s_digit++;
                SelfTest_ShowDigit(s_digit);

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                s_phase_start_ms = Gtim_NowMs();
            }
            else
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                SelfTest_ShowIdleZero();

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                s_phase = SELF_TEST_PHASE_DONE;
            }
            break;

        case SELF_TEST_PHASE_DONE:
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            break;

        default:
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            Solenoid_SetLow();
            s_phase = SELF_TEST_PHASE_DONE;
            break;
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 上电自检：执行内部处理（对应符号 SelfTest_IsDone）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t SelfTest_IsDone(void)
{
    return (s_phase == SELF_TEST_PHASE_DONE) ? 1U : 0U;
}
