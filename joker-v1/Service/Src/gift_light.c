/* AUTO-FILE-DOC
 * 文件职责：gift_light 模块。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "gift_light.h"

#include "audio_manager.h"
#include "gtim.h"
#include "segment.h"

#define GIFT_LIGHT_RUN_TOTAL_MS          1100U
#define GIFT_LIGHT_RUN_STEP_MS           100U
#define GIFT_LIGHT_CONFIRM_TOTAL_MS      1000U
#define GIFT_LIGHT_CONFIRM_BLINK_MS      100U

#define GIFT_LIGHT_AUDIO_ROLLING         13U
#define GIFT_LIGHT_AUDIO_RESULT          14U
#define GIFT_LIGHT_AUDIO_AWARD           12U

typedef enum
{
    GIFT_LIGHT_PHASE_IDLE = 0U,
    GIFT_LIGHT_PHASE_RUNNING,
    GIFT_LIGHT_PHASE_CONFIRM
} GiftLight_Phase;

static GiftLight_Phase s_phase = GIFT_LIGHT_PHASE_IDLE;
static uint8_t s_busy;
static uint8_t s_target_count;
static uint8_t s_target_index;
static uint8_t s_run_channel;
static uint8_t s_confirm_visible;
static uint16_t s_target_mask[GIFT_LIGHT_MAX_PER_REQUEST];
static uint16_t s_base_mask;
static uint16_t s_confirmed_event_mask;
static uint8_t s_completed_event;
static uint16_t s_random_state = 1U;
static uint32_t s_phase_start_ms;
static uint32_t s_last_step_ms;

/**
 * @brief gift_light 模块：执行内部处理（对应符号 GiftLight_Random16）。
 * @调用关系 直接调用者：gift_light.c:GiftLight_StartRunPhase。
 * @局部状态 主要临时变量：value；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t GiftLight_Random16(void)
{
    uint16_t value;

    value = s_random_state;
    value ^= (uint16_t)(value << 7U);
    value ^= (uint16_t)(value >> 9U);
    value ^= (uint16_t)(value << 8U);
    if (value == 0U)
    {
        value = 1U;
    }
    s_random_state = value;
    return value;
}

/**
 * @brief gift_light 模块：执行内部处理（对应符号 GiftLight_CountBits）。
 * @调用关系 直接调用者：gift_light.c:GiftLight_Start。
 * @局部状态 主要临时变量：count；均只在本次调用中有效。
 * @param mask 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t GiftLight_CountBits(uint16_t mask)
{
    uint8_t count = 0U;

    while (mask != 0U)
    {
        count = (uint8_t)(count + (uint8_t)(mask & 1U));
        mask >>= 1U;
    }
    return count;
}

/**
 * @brief gift_light 模块：执行内部处理（对应符号 GiftLight_MaskToChannel）。
 * @调用关系 直接调用者：gift_light.c:GiftLight_FindNextFreeChannel、gift_light.c:GiftLight_Task。
 * @局部状态 主要临时变量：channel；均只在本次调用中有效。
 * @param mask 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t GiftLight_MaskToChannel(uint16_t mask)
{
    uint8_t channel;

    for (channel = 1U; channel <= 7U; channel++)
    {
        if ((mask & (uint16_t)(1U << channel)) != 0U)
        {
            return channel;
        }
    }
    return 1U;
}

/**
 * @brief gift_light 模块：执行内部处理（对应符号 GiftLight_FindNextFreeChannel）。
 * @调用关系 直接调用者：gift_light.c:GiftLight_StartRunPhase、gift_light.c:GiftLight_Task。
 * @局部状态 主要临时变量：step、channel、unavailable_mask；均只在本次调用中有效。
 * @param current_channel 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t GiftLight_FindNextFreeChannel(uint8_t current_channel)
{
    uint8_t step;
    uint8_t channel;
    uint16_t unavailable_mask;

    unavailable_mask = s_base_mask;
    for (step = 0U; step < GIFT_LIGHT_MAX_PER_REQUEST; step++)
    {
        if (step >= s_target_index)
        {
            break;
        }
        unavailable_mask |= s_target_mask[step];
    }

    for (step = 0U; step < 7U; step++)
    {
        channel = (uint8_t)(1U + ((current_channel + step) % 7U));
        if ((unavailable_mask & (uint16_t)(1U << channel)) == 0U)
        {
            return channel;
        }
    }

    return GiftLight_MaskToChannel(s_target_mask[s_target_index]);
}

/**
 * @brief gift_light 模块：刷新显示（对应符号 GiftLight_ShowRunFrame）。
 * @调用关系 直接调用者：gift_light.c:GiftLight_StartRunPhase、gift_light.c:GiftLight_Task。
 * @局部状态 主要临时变量：display_mask；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void GiftLight_ShowRunFrame(void)
{
    uint16_t display_mask;

    display_mask = (uint16_t)(s_base_mask |
                              (uint16_t)(1U << s_run_channel));
    Segment_EyeLedClearAll();
    Segment_EyeLedSetChannelMask(display_mask);
}

/**
 * @brief gift_light 模块：刷新显示（对应符号 GiftLight_ShowConfirmFrame）。
 * @调用关系 直接调用者：gift_light.c:GiftLight_Task。
 * @局部状态 主要临时变量：display_mask；均只在本次调用中有效。
 * @param visible 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void GiftLight_ShowConfirmFrame(uint8_t visible)
{
    uint16_t display_mask = s_base_mask;

    if (visible != 0U)
    {
        display_mask |= s_target_mask[s_target_index];
    }

    Segment_EyeLedClearAll();
    Segment_EyeLedSetChannelMask(display_mask);
}

/**
 * @brief gift_light 模块：启动（对应符号 GiftLight_StartRunPhase）。
 * @调用关系 直接调用者：gift_light.c:GiftLight_Start、gift_light.c:GiftLight_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void GiftLight_StartRunPhase(void)
{
    s_phase = GIFT_LIGHT_PHASE_RUNNING;
    s_phase_start_ms = Gtim_NowMs();
    s_last_step_ms = s_phase_start_ms;
    s_run_channel = GiftLight_FindNextFreeChannel(
        (uint8_t)(GiftLight_Random16() % 7U));
    GiftLight_ShowRunFrame();
    Audio_LoopIndexOn(AUDIO_CHIP_1, GIFT_LIGHT_AUDIO_ROLLING);
}

/**
 * @brief gift_light 模块：初始化（对应符号 GiftLight_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @局部状态 主要临时变量：i；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void GiftLight_Init(void)
{
    uint8_t i;

    s_phase = GIFT_LIGHT_PHASE_IDLE;
    s_busy = 0U;
    s_target_count = 0U;
    s_target_index = 0U;
    s_run_channel = 1U;
    s_confirm_visible = 0U;
    for (i = 0U; i < GIFT_LIGHT_MAX_PER_REQUEST; i++)
    {
        s_target_mask[i] = 0U;
    }
    s_base_mask = 0U;
    s_confirmed_event_mask = 0U;
    s_completed_event = 0U;
    s_random_state = 1U;
    s_phase_start_ms = 0U;
    s_last_step_ms = 0U;
}

/**
 * @brief 启动一次逐盏送灯动画。
 * @param target_mask 本次全部目标灯位图，范围为2~8号通道。
 * @param first_target_mask 第一盏确认灯位图，只能包含一位。
 * @param second_target_mask 第二盏确认灯位图；只送一盏时为0。
 * @param occupied_mask 本局动画开始前已经点亮的灯位图。
 * @return 实际接受的送灯数量，范围1~6；参数无效或忙时返回0。
 * @note 这里只建立动画目标；每盏灯完成确认后才产生逻辑入灯事件。
 */
uint8_t GiftLight_Start(uint16_t target_mask,
                        uint16_t first_target_mask,
                        uint16_t second_target_mask,
                        uint16_t occupied_mask)
{
    uint16_t remaining_mask;                      // 尚未安排的目标灯，寄存器位掩码
    uint8_t requested_count;                      // 请求送灯数，单位：盏
    uint8_t target_index;                         // 目标数组索引，数组下标
    uint8_t channel;                              // 灯通道，范围：1~7

    if (s_busy != 0U)
    {
        return 0U;
    }

    s_base_mask = (uint16_t)(occupied_mask & GIFT_LIGHT_MIDDLE_MASK);
    target_mask &= GIFT_LIGHT_MIDDLE_MASK;
    requested_count = GiftLight_CountBits(target_mask);
    if ((requested_count == 0U) ||
        (requested_count > GIFT_LIGHT_MAX_PER_REQUEST) ||
        ((target_mask & s_base_mask) != 0U))
    {
        return 0U;
    }

    first_target_mask &= target_mask;
    second_target_mask &= target_mask;
    if (GiftLight_CountBits(first_target_mask) != 1U)
    {
        return 0U;
    }
    if (requested_count == 1U)
    {
        if ((first_target_mask != target_mask) ||
            (second_target_mask != 0U))
        {
            return 0U;
        }
    }
    else if ((GiftLight_CountBits(second_target_mask) != 1U) ||
             ((first_target_mask & second_target_mask) != 0U))
    {
        return 0U;
    }

    s_random_state = (uint16_t)(Gtim_NowMs() | 1U);
    for (target_index = 0U;
         target_index < GIFT_LIGHT_MAX_PER_REQUEST;
         target_index++)
    {
        s_target_mask[target_index] = 0U;
    }
    s_target_mask[0] = first_target_mask;
    s_target_mask[1] = (requested_count >= 2U) ? second_target_mask : 0U;
    remaining_mask = (uint16_t)(target_mask & ~first_target_mask &
                                ~second_target_mask);
    target_index = 2U;
    for (channel = 1U;
         (channel <= 7U) && (target_index < requested_count);
         channel++)
    {
        uint16_t bit = (uint16_t)(1U << channel); // 当前灯位，寄存器位掩码

        if ((remaining_mask & bit) != 0U)
        {
            s_target_mask[target_index] = bit;
            target_index++;
        }
    }
    if ((requested_count == 2U) &&
        ((uint16_t)(first_target_mask | second_target_mask) != target_mask))
    {
        return 0U;
    }
    if ((requested_count >= 3U) && (target_index != requested_count))
    {
        return 0U;
    }

    s_target_count = requested_count;
    s_target_index = 0U;
    /* 逻辑入灯必须与逐盏确认同步，不能在动画开始时一次性提前入账。 */
    s_confirmed_event_mask = 0U;
    s_completed_event = 0U;
    s_busy = 1U;
    GiftLight_StartRunPhase();
    return s_target_count;
}

/**
 * @brief 把同一检测周期内已经落入的实体灯位合并到显示底图。
 * @param occupied_mask 本局已经点亮的2~8号通道位图。
 */
void GiftLight_IncludeOccupiedMask(uint16_t occupied_mask)
{
    s_base_mask |= (uint16_t)(occupied_mask & GIFT_LIGHT_MIDDLE_MASK);
}

/**
 * @brief 推进跑灯和逐盏闪烁确认状态机。
 * @note 每盏灯确认结束时产生一次逻辑入灯事件，全部完成后产生结束事件。
 */
void GiftLight_Task(void)
{
    if (s_busy == 0U)
    {
        return;
    }

    if (s_phase == GIFT_LIGHT_PHASE_RUNNING)
    {
        if (Gtim_IsElapsed(s_phase_start_ms,
                           GIFT_LIGHT_RUN_TOTAL_MS) != 0U)
        {
            s_run_channel =
                GiftLight_MaskToChannel(s_target_mask[s_target_index]);
            GiftLight_ShowRunFrame();
            Audio_CancelLoopResumeOn(AUDIO_CHIP_1);
            Audio_PlayIndexOn(AUDIO_CHIP_1, GIFT_LIGHT_AUDIO_RESULT);
            Audio_PlayIndexOn(AUDIO_CHIP_1, GIFT_LIGHT_AUDIO_AWARD);
            s_phase = GIFT_LIGHT_PHASE_CONFIRM;
            s_phase_start_ms = Gtim_NowMs();
            s_last_step_ms = s_phase_start_ms;
            s_confirm_visible = 1U;
            GiftLight_ShowConfirmFrame(1U);
            return;
        }

        if (Gtim_IsElapsed(s_last_step_ms,
                           GIFT_LIGHT_RUN_STEP_MS) != 0U)
        {
            s_last_step_ms = Gtim_NowMs();
            s_run_channel =
                GiftLight_FindNextFreeChannel(s_run_channel);
            GiftLight_ShowRunFrame();
        }
        return;
    }

    if (s_phase == GIFT_LIGHT_PHASE_CONFIRM)
    {
        if (Gtim_IsElapsed(s_phase_start_ms,
                           GIFT_LIGHT_CONFIRM_TOTAL_MS) != 0U)
        {
            s_base_mask |= s_target_mask[s_target_index];
            s_confirmed_event_mask |= s_target_mask[s_target_index];
            Segment_EyeLedClearAll();
            Segment_EyeLedSetChannelMask(s_base_mask);

            s_target_index++;
            if (s_target_index < s_target_count)
            {
                GiftLight_StartRunPhase();
            }
            else
            {
                s_phase = GIFT_LIGHT_PHASE_IDLE;
                s_busy = 0U;
                s_completed_event = 1U;
            }
            return;
        }

        if (Gtim_IsElapsed(s_last_step_ms,
                           GIFT_LIGHT_CONFIRM_BLINK_MS) != 0U)
        {
            s_last_step_ms = Gtim_NowMs();
            s_confirm_visible = (s_confirm_visible != 0U) ? 0U : 1U;
            GiftLight_ShowConfirmFrame(s_confirm_visible);
        }
    }
}

/**
 * @brief gift_light 模块：读取并清除一次性事件（对应符号 GiftLight_TakeConfirmedMask）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：result；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t GiftLight_TakeConfirmedMask(void)
{
    uint16_t result = s_confirmed_event_mask;

    s_confirmed_event_mask = 0U;
    return result;
}

/**
 * @brief gift_light 模块：读取并清除一次性事件（对应符号 GiftLight_TakeCompleted）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：result；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t GiftLight_TakeCompleted(void)
{
    uint8_t result = s_completed_event;

    s_completed_event = 0U;
    return result;
}

/**
 * @brief gift_light 模块：执行内部处理（对应符号 GiftLight_IsBusy）。
 * @调用关系 直接调用者：game.c:Game_ProcessClearAccountRequest、game.c:Game_Task、standby.c:Standby_CanStart。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t GiftLight_IsBusy(void)
{
    return s_busy;
}
