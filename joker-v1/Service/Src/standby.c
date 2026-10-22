/* AUTO-FILE-DOC
 * 文件职责：待机演示。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "standby.h"
#include "audio_manager.h"
#include "award_lamp.h"
#include "gift_light.h"
#include "gtim.h"
#include "jackpot.h"
#include "jackpot_display.h"
#include "key.h"
#include "lottery.h"
#include "payout.h"
#include "score.h"
#include "segment.h"
#include "setting.h"
#include "ticket.h"

#define STANDBY_IDLE_DELAY_MS       5000U
#define STANDBY_STEP_MS             500U
#define STANDBY_EYE_CHANNEL_MASK    0x01FFU
#define STANDBY_LINE_STEP_MS        1000U
#define STANDBY_SLOT_TOTAL_MS       700U
#define STANDBY_SLOT_STEP_MS        100U
#define STANDBY_GIFT_RUN_MS         1100U
#define STANDBY_GIFT_STEP_MS        100U
#define STANDBY_GIFT_CONFIRM_MS     1000U
#define STANDBY_GIFT_GAP_MS         1000U
#define STANDBY_SILENT_HOLD_MS      1000U
#define STANDBY_PAYOUT_DEMO_MS      1000U
#define STANDBY_RESET_HOLD_MS       2000U
#define STANDBY_AUDIO_TIMEOUT_MS    9000U

typedef enum
{
    STANDBY_PART_LINES = 0U,
    STANDBY_PART_GIFTS
} Standby_Part;

typedef enum
{
    STANDBY_PHASE_LINE_STEP = 0U,
    STANDBY_PHASE_SLOT,
    STANDBY_PHASE_GIFT_AUDIO,
    STANDBY_PHASE_GIFT_RUN,
    STANDBY_PHASE_GIFT_CONFIRM,
    STANDBY_PHASE_GIFT_GAP,
    STANDBY_PHASE_LINE_AUDIO,
    STANDBY_PHASE_BASE_COUNTDOWN,
    STANDBY_PHASE_JACKPOT_AUDIO,
    STANDBY_PHASE_BONUS_COUNTDOWN,
    STANDBY_PHASE_RESET_HOLD
} Standby_Phase;

/* Presentation-only RAM: no real payout, account, gift or network requests. */
static uint8_t s_active;
static uint8_t s_step;
static uint8_t s_sound_enabled;
static uint8_t s_audio_waiting;
static Standby_Part s_part;
static Standby_Phase s_phase;
static uint32_t s_last_step_ms;
static uint32_t s_phase_start_ms;
static uint32_t s_random_state;
static uint16_t s_eye_mask;
static uint8_t s_lit_count;
static uint8_t s_tier;
static uint16_t s_line_prize;
static uint16_t s_jackpot_snapshot;
static uint16_t s_jackpot_award;
static uint8_t s_gift_count;
static uint8_t s_gift_index;
static uint8_t s_gift_target[3];

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_Random）。
 * @调用关系 直接调用者：standby.c:Standby_BeginPart、standby.c:Standby_PlanGifts。
 * @局部状态 主要临时变量：value；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Standby_Random(void)
{
    uint32_t value = s_random_state;
    value ^= value << 13U;
    value ^= value >> 17U;
    value ^= value << 5U;
    s_random_state = (value != 0U) ? value : 1U;
    return s_random_state;
}

/**
 * @brief 待机演示：进入状态（对应符号 Standby_EnterPhase）。
 * @调用关系 直接调用者：standby.c:Standby_BeginPart、standby.c:Standby_BeginLineAward、standby.c:Standby_BeginJackpotAward、standby.c:Standby_BeginGiftRun、standby.c:Standby_Task。
 * @param phase 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_EnterPhase(Standby_Phase phase)
{
    s_phase = phase;
    s_phase_start_ms = Gtim_NowMs();
}

/**
 * @brief 待机演示：刷新显示（对应符号 Standby_ShowEyes）。
 * @调用关系 直接调用者：standby.c:Standby_BeginPart、standby.c:Standby_Task。
 * @param mask 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_ShowEyes(uint16_t mask)
{
    Segment_EyeLedClearAll();
    Segment_EyeLedSetChannelMask(mask);
}

/* Keep the old diagonal four-column animation throughout part 1. */
/**
 * @brief 待机演示：刷新显示（对应符号 Standby_ShowDiagonal）。
 * @调用关系 直接调用者：standby.c:Standby_BeginPart、standby.c:Standby_Task。
 * @局部状态 主要临时变量：row；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_ShowDiagonal(void)
{
    uint8_t row = (uint8_t)((s_step % 9U) % SEGMENT_LOTTERY_ROW_COUNT);
    Segment_LotteryLampShow(row,
        (uint8_t)((row + 1U) % SEGMENT_LOTTERY_ROW_COUNT),
        (uint8_t)((row + 2U) % SEGMENT_LOTTERY_ROW_COUNT),
        (uint8_t)((row + 3U) % SEGMENT_LOTTERY_ROW_COUNT), 1U);
}

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_PlayEffect）。
 * @调用关系 直接调用者：standby.c:Standby_Task。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_PlayEffect(uint16_t index)
{
    if (s_sound_enabled != 0U)
        Audio_PlayIndexOn(AUDIO_CHIP_1, index);
}

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_PlayLoop）。
 * @调用关系 直接调用者：standby.c:Standby_BeginGiftRun、standby.c:Standby_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_PlayLoop(void)
{
    if (s_sound_enabled != 0U)
        Audio_LoopIndexOn(AUDIO_CHIP_1, 13U);
}

/**
 * @brief 待机演示：停止（对应符号 Standby_StopEffect）。
 * @调用关系 直接调用者：standby.c:Standby_AnnouncementDone、standby.c:Standby_Start、standby.c:Standby_Stop、standby.c:Standby_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_StopEffect(void)
{
    Audio_StopOn(AUDIO_CHIP_1);
}

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_PlayAnnouncement）。
 * @调用关系 直接调用者：standby.c:Standby_BeginLineAward、standby.c:Standby_BeginJackpotAward、standby.c:Standby_Task。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param prelude 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_PlayAnnouncement(uint16_t index, uint8_t prelude)
{
    s_audio_waiting = 0U;
    if (s_sound_enabled != 0U)
        s_audio_waiting = Audio_PlayLoudIndexOn(AUDIO_CHIP_1, index, prelude);
}

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_AnnouncementDone）。
 * @调用关系 直接调用者：standby.c:Standby_Task。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Standby_AnnouncementDone(void)
{
    if (s_audio_waiting == 0U)
        return Gtim_IsElapsed(s_phase_start_ms, STANDBY_SILENT_HOLD_MS);
    if (Audio_IsLoudSequenceBusy() == 0U)
    {
        s_audio_waiting = 0U;
        return 1U;
    }
    if (Gtim_IsElapsed(s_phase_start_ms, STANDBY_AUDIO_TIMEOUT_MS) != 0U)
    {
        Standby_StopEffect();
        s_audio_waiting = 0U;
        return 1U;
    }
    return 0U;
}

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_BeginPart）。
 * @调用关系 直接调用者：standby.c:Standby_Start、standby.c:Standby_Task。
 * @param part 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_BeginPart(Standby_Part part)
{
    s_part = part;
    s_tier = 0U;
    s_eye_mask = 0U;
    s_lit_count = 0U;
    s_gift_count = 0U;
    s_gift_index = 0U;
    Segment_SetLeftNumber(0U);
    Segment_SetMiddleNumber(0U);
    Segment_SetRightNumber(0U);
    JackpotDisplay_SetDemo(Jackpot_GetCurrent(), 0U);
    if (part == STANDBY_PART_LINES)
    {
        Standby_EnterPhase(STANDBY_PHASE_LINE_STEP);
        Standby_ShowDiagonal();
    }
    else
    {
        /* One seed lamp: never demonstrate a gift on an empty board. */
        s_eye_mask = (uint16_t)(1U << (1U + Standby_Random() % 7U));
        s_lit_count = 1U;
        Standby_EnterPhase(STANDBY_PHASE_GIFT_GAP);
        Segment_LotteryLampShow(0U, 1U, 2U, 3U, 1U);
    }
    Standby_ShowEyes(s_eye_mask);
}

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_BeginLineAward）。
 * @调用关系 直接调用者：standby.c:Standby_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_BeginLineAward(void)
{
    s_tier = s_lit_count;
    s_line_prize = (s_tier == 5U) ? SCORE_AWARD_LINE_5 :
                   ((s_tier == 6U) ? SCORE_AWARD_LINE_6 : SCORE_AWARD_LINE_7);
    s_jackpot_snapshot = Jackpot_GetCurrent();
    Segment_SetLeftNumber(0U);
    JackpotDisplay_SetDemo(s_jackpot_snapshot, 1U);
    Standby_EnterPhase(STANDBY_PHASE_LINE_AUDIO);
    /* Same prelude 23 and boosted 17/18/19 as real play. */
    Standby_PlayAnnouncement((uint16_t)(17U + s_tier - 5U), 1U);
}

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_BeginJackpotAward）。
 * @调用关系 直接调用者：standby.c:Standby_Task。
 * @局部状态 主要临时变量：percent；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_BeginJackpotAward(void)
{
    uint16_t percent = (s_tier == 5U) ? 25U : ((s_tier == 6U) ? 50U : 100U);
    s_jackpot_award = (uint16_t)((uint32_t)s_jackpot_snapshot * percent / 100U);
    /* The zero/remaining display is temporary. No real pool floor is changed. */
    JackpotDisplay_SetDemo(s_jackpot_snapshot, 0U);
    Segment_SetMiddleNumber(0U);
    Standby_EnterPhase(STANDBY_PHASE_JACKPOT_AUDIO);
    Standby_PlayAnnouncement((uint16_t)(25U + s_tier - 5U), 0U);
}

/* Independent PRNG; random counts and left/right growth without skipping 5/6/7. */
/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_PlanGifts）。
 * @调用关系 直接调用者：standby.c:Standby_Task。
 * @局部状态 主要临时变量：low、high、index、remaining；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_PlanGifts(void)
{
    uint8_t low = 1U;
    uint8_t high = 7U;
    uint8_t index;
    uint8_t remaining = (s_lit_count < 5U) ? (uint8_t)(5U - s_lit_count) : 1U;
    while ((s_eye_mask & (uint16_t)(1U << low)) == 0U)
        low++;
    while ((s_eye_mask & (uint16_t)(1U << high)) == 0U)
        high--;
    s_gift_count = (uint8_t)(1U + Standby_Random() % 3U);
    if (s_gift_count > remaining)
        s_gift_count = remaining;
    s_gift_index = 0U;
    for (index = 0U; index < s_gift_count; index++)
    {
        if ((low > 1U) && ((high == 7U) || ((Standby_Random() & 1U) != 0U)))
            s_gift_target[index] = --low;
        else
            s_gift_target[index] = ++high;
    }
}

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_BeginGiftRun）。
 * @调用关系 直接调用者：standby.c:Standby_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Standby_BeginGiftRun(void)
{
    Standby_EnterPhase(STANDBY_PHASE_GIFT_RUN);
    Standby_PlayLoop();
}

/**
 * @brief 待机演示：执行内部处理（对应符号 Standby_Countdown）。
 * @调用关系 直接调用者：standby.c:Standby_Task。
 * @局部状态 主要临时变量：elapsed；均只在本次调用中有效。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Standby_Countdown(uint16_t value)
{
    uint32_t elapsed = Gtim_NowMs() - s_phase_start_ms;
    (void)value; /* 保留演示时序，三屏不再模拟得奖/出票数字。 */
    if (elapsed >= STANDBY_PAYOUT_DEMO_MS)
    {
        Segment_SetMiddleNumber(0U);
        return 1U;
    }
    Segment_SetMiddleNumber(0U);
    return 0U;
}

/**
 * @brief 待机演示：初始化（对应符号 Standby_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Standby_Init(void)
{
    s_active = 0U;
    s_step = 0U;
    s_last_step_ms = 0U;
    s_audio_waiting = 0U;
    s_random_state = 0x563A8217UL;
}

/**
 * @brief 待机演示：启动（对应符号 Standby_CanStart）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param idle_start_ms 按值传入；函数内部修改不会回写调用方变量。
 * @param game_coin 按值传入；函数内部修改不会回写调用方变量。
 * @param active_eye_mask 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Standby_CanStart(uint32_t idle_start_ms,
                         uint16_t game_coin,
                         uint16_t active_eye_mask)
{
    if ((game_coin != 0U) ||
        ((active_eye_mask & STANDBY_EYE_CHANNEL_MASK) != 0U) ||
        (Gtim_IsElapsed(idle_start_ms, STANDBY_IDLE_DELAY_MS) == 0U) ||
        (Lottery_IsBusy() != 0U) ||
        (GiftLight_IsBusy() != 0U) ||
        (Payout_IsBusy() != 0U) ||
        (Ticket_IsBusy() != 0U) ||
        (Audio_IsLoudSequenceBusy() != 0U) ||
        (AwardLamp_IsBlinking() != 0U))
        return 0U;
    return 1U;
}

/**
 * @brief 待机演示：启动（对应符号 Standby_Start）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Standby_Start(void)
{
    s_active = 1U;
    s_step = 0U;
    s_last_step_ms = Gtim_NowMs();
    s_random_state ^= s_last_step_ms;
    s_sound_enabled = Setting_IsStandbySoundEnabled();
    s_audio_waiting = 0U;
    Standby_StopEffect();
    Segment_ClearAll();
    Standby_BeginPart(STANDBY_PART_LINES);
}

/**
 * @brief 待机演示：停止（对应符号 Standby_Stop）。
 * @调用关系 直接调用者：game.c:Game_EnterAccountPage、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Standby_Stop(void)
{
    if (s_active == 0U)
        return;
    s_active = 0U;
    s_audio_waiting = 0U;
    Standby_StopEffect();
    JackpotDisplay_SetValue(Jackpot_GetCurrent());
    JackpotDisplay_EndDemo();
    Segment_SetLeftNumber(0U);
    Segment_SetMiddleNumber(0U);
    Segment_SetRightNumber(0U);
    Segment_LotteryLampClearAll();
    Segment_EyeLedClearAll();
    Key_SetLamp(KEY_LAMP_EXCHANGE, 0U);
    Key_SetLamp(KEY_LAMP_TICKET, 0U);
    Key_SetLamp(KEY_LAMP_GAME, 0U);
    Key_SetLamp(KEY_LAMP_LOTTERY, 0U);
}

/**
 * @brief 待机演示：周期处理（对应符号 Standby_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：elapsed、row、target、run_channel、sound_enabled；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Standby_Task(void)
{
    uint32_t elapsed;
    uint8_t row;
    uint8_t target;
    uint8_t run_channel;
    uint8_t sound_enabled;
    if (s_active == 0U)
        return;
    sound_enabled = Setting_IsStandbySoundEnabled();
    if (sound_enabled != s_sound_enabled)
    {
        s_sound_enabled = sound_enabled;
        if (sound_enabled == 0U)
        {
            Standby_StopEffect();
            s_audio_waiting = 0U;
        }
    }
    if (Gtim_IsElapsed(s_last_step_ms, STANDBY_STEP_MS) != 0U)
    {
        s_last_step_ms = Gtim_NowMs();
        s_step = (uint8_t)((s_step + 1U) % 18U);
        if (s_part == STANDBY_PART_LINES)
            Standby_ShowDiagonal();
    }
    elapsed = Gtim_NowMs() - s_phase_start_ms;
    switch (s_phase)
    {
        case STANDBY_PHASE_LINE_STEP:
            if (elapsed < STANDBY_LINE_STEP_MS)
                break;
            s_lit_count++;
            s_eye_mask |= (uint16_t)(1U << s_lit_count);
            Standby_ShowEyes(s_eye_mask);
            Segment_SetLeftNumber(0U);
            if (s_lit_count >= 5U)
                Standby_BeginLineAward();
            else
                Standby_EnterPhase(STANDBY_PHASE_LINE_STEP);
            break;

        case STANDBY_PHASE_GIFT_GAP:
            if (elapsed >= STANDBY_GIFT_GAP_MS)
            {
                Standby_PlanGifts();
                Standby_EnterPhase(STANDBY_PHASE_SLOT);
                Standby_PlayLoop();
            }
            break;

        case STANDBY_PHASE_SLOT:
            row = (uint8_t)((elapsed / STANDBY_SLOT_STEP_MS) % 4U);
            Segment_LotteryLampShow(
                (elapsed >= 200U) ? 0U : row,
                (elapsed >= 400U) ? 0U : (uint8_t)((row + 1U) % 4U),
                (elapsed >= STANDBY_SLOT_TOTAL_MS) ? 0U : (uint8_t)((row + 2U) % 4U),
                0U, 1U);
            if (elapsed >= STANDBY_SLOT_TOTAL_MS)
            {
                Standby_EnterPhase(STANDBY_PHASE_GIFT_AUDIO);
                Standby_PlayAnnouncement((s_gift_count == 3U) ? 24U :
                                         (uint16_t)(19U + s_gift_count), 0U);
            }
            break;

        case STANDBY_PHASE_GIFT_AUDIO:
            if (Standby_AnnouncementDone() != 0U)
                Standby_BeginGiftRun();
            break;

        case STANDBY_PHASE_GIFT_RUN:
            target = s_gift_target[s_gift_index];
            if (elapsed >= STANDBY_GIFT_RUN_MS)
            {
                Standby_StopEffect();
                Standby_ShowEyes((uint16_t)(s_eye_mask | (uint16_t)(1U << target)));
                Standby_PlayEffect(14U);
                Standby_EnterPhase(STANDBY_PHASE_GIFT_CONFIRM);
            }
            else
            {
                /* Last 100ms step lands on the target, no final jump. */
                run_channel = (uint8_t)(1U + ((target - 1U + 14U -
                    ((STANDBY_GIFT_RUN_MS / STANDBY_GIFT_STEP_MS - 1U) -
                     elapsed / STANDBY_GIFT_STEP_MS)) % 7U));
                Standby_ShowEyes((uint16_t)(s_eye_mask | (uint16_t)(1U << run_channel)));
            }
            break;

        case STANDBY_PHASE_GIFT_CONFIRM:
            target = s_gift_target[s_gift_index];
            Standby_ShowEyes((uint16_t)(s_eye_mask |
                (((elapsed / STANDBY_GIFT_STEP_MS) & 1U) == 0U ?
                 (uint16_t)(1U << target) : 0U)));
            if (elapsed >= STANDBY_GIFT_CONFIRM_MS)
            {
                s_eye_mask |= (uint16_t)(1U << target);
                s_lit_count++;
                s_gift_index++;
                Standby_ShowEyes(s_eye_mask);
                Segment_SetLeftNumber(0U);
                if (s_gift_index < s_gift_count)
                    Standby_BeginGiftRun();
                else if (s_lit_count >= 5U)
                    Standby_BeginLineAward();
                else
                    Standby_EnterPhase(STANDBY_PHASE_GIFT_GAP);
            }
            break;

        case STANDBY_PHASE_LINE_AUDIO:
            if (Standby_AnnouncementDone() != 0U)
            {
                Segment_SetLeftNumber(0U);
                Segment_SetMiddleNumber(0U);
                Standby_EnterPhase(STANDBY_PHASE_BASE_COUNTDOWN);
            }
            break;

        case STANDBY_PHASE_BASE_COUNTDOWN:
            if (Standby_Countdown(s_line_prize) != 0U)
                Standby_BeginJackpotAward();
            break;

        case STANDBY_PHASE_JACKPOT_AUDIO:
            if (Standby_AnnouncementDone() != 0U)
                Standby_EnterPhase(STANDBY_PHASE_BONUS_COUNTDOWN);
            break;

        case STANDBY_PHASE_BONUS_COUNTDOWN:
            if (Standby_Countdown(s_jackpot_award) != 0U)
                Standby_EnterPhase(STANDBY_PHASE_RESET_HOLD);
            break;

        case STANDBY_PHASE_RESET_HOLD:
            if (elapsed >= STANDBY_RESET_HOLD_MS)
            {
                JackpotDisplay_SetDemo(Jackpot_GetCurrent(), 0U);
                Segment_SetLeftNumber(0U);
                if (s_tier == 7U)
                    Standby_BeginPart((s_part == STANDBY_PART_LINES) ?
                                      STANDBY_PART_GIFTS : STANDBY_PART_LINES);
                else
                    Standby_EnterPhase((s_part == STANDBY_PART_LINES) ?
                                      STANDBY_PHASE_LINE_STEP : STANDBY_PHASE_GIFT_GAP);
            }
            break;

        default:
            Standby_BeginPart(STANDBY_PART_LINES);
            break;
    }
}

/**
 * @brief 待机演示：更新（对应符号 Standby_UpdateKeyLamps）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：lamp_index；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Standby_UpdateKeyLamps(void)
{
    uint8_t lamp_index = (uint8_t)(s_step % (uint8_t)KEY_LAMP_COUNT);
    if (s_active == 0U)
        return;
    Key_SetLamp(KEY_LAMP_EXCHANGE, (lamp_index == (uint8_t)KEY_LAMP_EXCHANGE) ? 1U : 0U);
    Key_SetLamp(KEY_LAMP_TICKET, (lamp_index == (uint8_t)KEY_LAMP_TICKET) ? 1U : 0U);
    Key_SetLamp(KEY_LAMP_GAME, (lamp_index == (uint8_t)KEY_LAMP_GAME) ? 1U : 0U);
    Key_SetLamp(KEY_LAMP_LOTTERY, (lamp_index == (uint8_t)KEY_LAMP_LOTTERY) ? 1U : 0U);
}
