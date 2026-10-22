/* AUTO-FILE-DOC
 * 文件职责：语音调度。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "audio_manager.h"

#include "./SYSTEM/usart/usart.h"

#include <string.h>

/*
 * 本文件按已经过实机验证的 joker-music 音频驱动工作：
 * - 发送 KT404C 兼容的 8 字节无校验帧，并接收音效板的播放完成回包。
 * - 第一条普通音效使用根目录物理序号0x03，后续音效使用ADVERT1的0x25插播。
 * - KT404C只支持一层插播；插播期间的新音效进入FIFO，当前插播结束后再发送。
 * - 单曲循环使用 0x08，停止使用 0x16。
 * - 两条命令之间固定间隔 120ms，保持与 joker-music 一致。
 */
#define AUDIO_UART_TIMEOUT_MS           20U
#define AUDIO_COMMAND_GAP_MS            120U
#define AUDIO_STARTUP_DELAY_MS          1500U
#define AUDIO_STARTUP_VOLUME_REPEAT     2U
#define AUDIO_FRAME_LEN                 8U
#define AUDIO_TX_BUF_LEN                32U
#define AUDIO_QUEUE_DEPTH               8U
#define AUDIO_INIT_VOLUME               15U
#define AUDIO_MODULE_MAX_VOLUME         30U
#define AUDIO_EFFECT_ADVERT_FOLDER      1U
#define AUDIO_EFFECT_RX_FRAME_LEN       10U
#define AUDIO_EFFECT_COMPLETE_MIN_MS    120U
#define AUDIO_EFFECT_COMPLETE_MARGIN_MS 120U
#define AUDIO_EFFECT_DURATION_GUARD_MS  250U
#define AUDIO_EFFECT_DEFAULT_MS         5000U
#define AUDIO_EFFECT_AUDIBLE_GUARD_MS   20U
#define AUDIO_LOUD_VOLUME_BOOST         10U
#define AUDIO_JACKPOT_VOLUME_BOOST      15U
#define AUDIO_LOUD_PRELUDE_INDEX        23U
#define AUDIO_LOUD_SEQUENCE_MAX_TRACKS  5U
#define AUDIO_LOUD_COMPLETE_MIN_MS      120U
#define AUDIO_LOUD_PRELUDE_AUDIBLE_MS   1670U

typedef struct
{
    Audio_State state;

    uint8_t queue_data[AUDIO_QUEUE_DEPTH][AUDIO_TX_BUF_LEN];
    uint16_t queue_length[AUDIO_QUEUE_DEPTH];
    uint8_t queue_head;
    uint8_t queue_tail;
    uint8_t queue_count;

    uint8_t target_volume;
    uint8_t startup_volume_frames;
    uint32_t state_tick;
} Audio_Context;

typedef struct
{
    uint16_t index;
    uint8_t loop;
} Audio_EffectEvent;

typedef struct
{
    Audio_EffectEvent pending_event;
    uint8_t pending_valid;

    uint8_t protected_loop_active;
    uint16_t protected_loop_index;
    uint8_t protected_resume_needed;

    uint8_t transient_active;
    uint16_t transient_index;
    uint32_t transient_tick;
    uint32_t transient_audible_ms;

    uint8_t base_active;
    uint8_t base_loop;
    uint8_t advert_active;
    uint16_t base_index;
    uint16_t advert_index;
    uint32_t base_remaining_ms;
    uint32_t base_tick;
    uint32_t advert_tick;
    uint32_t advert_min_complete_ms;
    uint32_t advert_timeout_ms;

    uint8_t rx_frame[AUDIO_EFFECT_RX_FRAME_LEN];
    uint8_t rx_length;
} Audio_EffectContext;

typedef struct
{
    uint16_t track[AUDIO_LOUD_SEQUENCE_MAX_TRACKS];
    uint8_t track_count;
    uint8_t track_pos;
    uint8_t prelude_count;
    uint8_t active;
    uint8_t stop_sent;
    uint8_t volume_sent;
    uint8_t waiting_complete;
    uint32_t track_tick;
} Audio_LoudSequenceContext;

/*
 * 根目录和ADVERT1中1~19号音效为实测时长。20~23先作为播放完成回包
 * 丢失时的保护值；正常情况下庆祝序列由KT404C完成回包推进，不依赖这些值。
 * 24~27号只放在根目录，由庆祝序列使用0x03物理序号播放。24号是送3灯，
 * 25~27号依次对应25%、50%、全部彩金播报；28号是“请解码”。
 */
static const uint16_t s_audio_effect_duration_ms[] =
{
    2196U, 1565U, 932U, 524U,
    1231U, 1103U, 860U, 2744U,
    266U, 1325U, 13282U, 3989U,
    322U, 1051U, 1061U, 322U,
    2280U, 3552U, 3744U,
    2500U, 2500U, 2000U, 1700U,
    1776U, 1776U, 1776U, 1776U,
    3000U
};

/*
 * ADVERT1文件从开始到“最后可听声音”的实测时间。部分MP3尾部有0.3~1.9秒
 * 静音，若等待完整文件结束，持续抽奖音效013听起来就会恢复过慢。
 */
static const uint16_t s_audio_effect_audible_end_ms[] =
{
    2107U, 1240U, 841U, 524U,
    1128U, 641U, 860U, 2523U,
    266U, 1267U, 11413U, 3989U,
    322U, 1051U, 1061U, 322U,
    1412U, 2704U, 2891U,
    2500U, 2500U, 2000U, 1670U,
    1327U, 1508U, 1462U, 1250U
};

static Audio_Context s_audio_ctx[AUDIO_CHIP_COUNT];
static Audio_EffectContext s_effect_ctx;
static Audio_LoudSequenceContext s_loud_ctx;
static uint8_t s_audio_initialized;
static uint8_t s_exclusive_alarm_active;

static uint32_t Audio_EffectGetMinCompleteMs(uint16_t index);

/* 0：完全屏蔽背景音乐板；1：音效板和背景音乐板同时工作。 */
uint8_t g_background_music_enable = 1U;

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_IsElapsed）。
 * @调用关系 直接调用者：audio_manager.c:Audio_TaskOne。
 * @param start 按值传入；函数内部修改不会回写调用方变量。
 * @param period 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_IsElapsed(uint32_t start, uint32_t period)
{
    return ((uint32_t)(HAL_GetTick() - start) >= period) ? 1U : 0U;
}

/**
 * @brief 语音调度：校验（对应符号 Audio_IsValidChip）。
 * @调用关系 直接调用者：audio_manager.c:Audio_SendNow、audio_manager.c:Audio_RequestSend、audio_manager.c:Audio_TaskOne、audio_manager.c:Audio_PlayIndexOn、audio_manager.c:Audio_LoopIndexOn、audio_manager.c:Audio_CancelLoopResumeOn、audio_manager.c:Audio_StopOn、audio_manager.c:Audio_VolumeUpOn。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_IsValidChip(Audio_Chip chip)
{
    return (chip < AUDIO_CHIP_COUNT) ? 1U : 0U;
}

/**
 * @brief 语音调度：读取（对应符号 Audio_GetBoostedVolume）。
 * @调用关系 直接调用者：audio_manager.c:Audio_LoudSequenceTask。
 * @局部状态 主要临时变量：boosted_volume、final_index、boost；均只在本次调用中有效。
 * @param current_volume 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_GetBoostedVolume(uint8_t current_volume)
{
    uint16_t boosted_volume;
    uint16_t final_index;
    uint8_t boost = AUDIO_LOUD_VOLUME_BOOST;

    if ((s_loud_ctx.active != 0U) &&
        (s_loud_ctx.track_count != 0U))
    {
        final_index = s_loud_ctx.track[s_loud_ctx.track_count - 1U];

        if (final_index == 28U)
        {
            /* 打码提示音不跟随P13，固定使用模块最大音量30。 */
            return AUDIO_MODULE_MAX_VOLUME;
        }
        if ((final_index >= 25U) && (final_index <= 27U))
        {
            boost = AUDIO_JACKPOT_VOLUME_BOOST;
        }
    }

    boosted_volume = (uint16_t)current_volume +
                     boost;
    if (boosted_volume > AUDIO_MODULE_MAX_VOLUME)
    {
        boosted_volume = AUDIO_MODULE_MAX_VOLUME;
    }
    return (uint8_t)boosted_volume;
}

/**
 * @brief 语音调度：读取（对应符号 Audio_GetHandle）。
 * @调用关系 直接调用者：audio_manager.c:Audio_SendNow。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static UART_HandleTypeDef *Audio_GetHandle(Audio_Chip chip)
{
    return (chip == AUDIO_CHIP_2) ?
           &g_audio_uart2_handle : &g_audio_uart1_handle;
}

/* 构造与 joker-music 完全一致的 KT404C 8 字节无校验控制帧。 */
/**
 * @brief 语音调度：构造（对应符号 Audio_BuildFrame）。
 * @调用关系 直接调用者：audio_manager.c:Audio_LoudSequenceTask、audio_manager.c:Audio_TaskOne、audio_manager.c:Audio_EffectTryDispatch、audio_manager.c:Audio_PlayIndexOn、audio_manager.c:Audio_LoopIndexOn、audio_manager.c:Audio_StopOn、audio_manager.c:Audio_StartExclusiveAlarm、audio_manager.c:Audio_StopExclusiveAlarm。
 * @param frame 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param command 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_BuildFrame(uint8_t *frame, uint8_t command, uint16_t value)
{
    frame[0] = 0x7EU;
    frame[1] = 0xFFU;
    frame[2] = 0x06U;
    frame[3] = command;
    frame[4] = 0x00U;
    frame[5] = (uint8_t)(value >> 8U);
    frame[6] = (uint8_t)value;
    frame[7] = 0xEFU;
}

/**
 * @brief 语音调度：清除（对应符号 Audio_QueueClear）。
 * @调用关系 直接调用者：audio_manager.c:Audio_PlayLoudIndexOn、audio_manager.c:Audio_StopOn、audio_manager.c:Audio_StartExclusiveAlarm、audio_manager.c:Audio_StopExclusiveAlarm。
 * @param ctx 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_QueueClear(Audio_Context *ctx)
{
    if (ctx == 0)
    {
        return;
    }

    memset(ctx->queue_data, 0, sizeof(ctx->queue_data));
    memset(ctx->queue_length, 0, sizeof(ctx->queue_length));
    ctx->queue_head = 0U;
    ctx->queue_tail = 0U;
    ctx->queue_count = 0U;
}

/* 队列满时沿用 joker-music 的处理：丢弃最旧命令，保留刚发生的新命令。 */
/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_QueuePush）。
 * @调用关系 直接调用者：audio_manager.c:Audio_RequestSend、audio_manager.c:Audio_TaskOne、audio_manager.c:Audio_StopOn、audio_manager.c:Audio_StartExclusiveAlarm。
 * @局部状态 主要临时变量：tail；均只在本次调用中有效。
 * @param ctx 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param length 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_QueuePush(Audio_Context *ctx,
                            const uint8_t *data,
                            uint16_t length)
{
    uint8_t tail;

    if ((ctx == 0) || (data == 0) || (length == 0U) ||
        (length > AUDIO_TX_BUF_LEN))
    {
        return;
    }

    if (ctx->queue_count >= AUDIO_QUEUE_DEPTH)
    {
        ctx->queue_head++;
        if (ctx->queue_head >= AUDIO_QUEUE_DEPTH)
        {
            ctx->queue_head = 0U;
        }
        ctx->queue_count--;
    }

    tail = ctx->queue_tail;
    memcpy(ctx->queue_data[tail], data, length);
    ctx->queue_length[tail] = length;
    ctx->queue_tail++;
    if (ctx->queue_tail >= AUDIO_QUEUE_DEPTH)
    {
        ctx->queue_tail = 0U;
    }
    ctx->queue_count++;
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_QueuePop）。
 * @调用关系 直接调用者：audio_manager.c:Audio_TaskOne。
 * @局部状态 主要临时变量：head；均只在本次调用中有效。
 * @param ctx 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param length 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_QueuePop(Audio_Context *ctx,
                              uint8_t *data,
                              uint16_t *length)
{
    uint8_t head;

    if ((ctx == 0) || (data == 0) || (length == 0) ||
        (ctx->queue_count == 0U))
    {
        return 0U;
    }

    head = ctx->queue_head;
    *length = ctx->queue_length[head];
    memcpy(data, ctx->queue_data[head], *length);
    ctx->queue_length[head] = 0U;

    ctx->queue_head++;
    if (ctx->queue_head >= AUDIO_QUEUE_DEPTH)
    {
        ctx->queue_head = 0U;
    }
    ctx->queue_count--;
    return 1U;
}

/**
 * @brief 语音调度：设置（对应符号 Audio_EffectReset）。
 * @调用关系 直接调用者：audio_manager.c:Audio_LoudSequenceTask、audio_manager.c:Audio_Init、audio_manager.c:Audio_PlayLoudIndexOn、audio_manager.c:Audio_StopOn、audio_manager.c:Audio_StartExclusiveAlarm、audio_manager.c:Audio_StopExclusiveAlarm。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectReset(void)
{
    memset(&s_effect_ctx, 0, sizeof(s_effect_ctx));
    audio_usart_clear_rx(AUDIO_UART_CHANNEL_1);
}

/**
 * @brief 语音调度：设置（对应符号 Audio_LoudSequenceReset）。
 * @调用关系 直接调用者：audio_manager.c:Audio_LoudSequenceTask、audio_manager.c:Audio_Init、audio_manager.c:Audio_PlayLoudIndexOn、audio_manager.c:Audio_StopOn、audio_manager.c:Audio_StartExclusiveAlarm、audio_manager.c:Audio_StopExclusiveAlarm。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_LoudSequenceReset(void)
{
    memset(&s_loud_ctx, 0, sizeof(s_loud_ctx));
}

/*
 * 庆祝序列直接按KT404C播放完成回包推进。120ms内的完成帧可能属于刚被
 * 覆盖的旧音效，因此只吞掉、不推进当前23或17~22。
 */
/**
 * @brief 语音调度：处理状态或事件（对应符号 Audio_LoudSequenceHandleComplete）。
 * @调用关系 直接调用者：audio_manager.c:Audio_ProcessRxByte。
 * @局部状态 主要临时变量：index；均只在本次调用中有效。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_LoudSequenceHandleComplete(uint32_t now)
{
    uint16_t index;

    if ((s_loud_ctx.active == 0U) ||
        (s_loud_ctx.waiting_complete == 0U))
    {
        return 0U;
    }

    /* 23号前奏按实测1670ms有效声音推进，不等待末尾静音。 */
    if (s_loud_ctx.track_pos < s_loud_ctx.prelude_count)
    {
        return 1U;
    }

    index = s_loud_ctx.track[s_loud_ctx.track_pos];
    if ((index >= 25U) &&
        ((uint32_t)(now - s_loud_ctx.track_tick) <
         Audio_EffectGetMinCompleteMs(index)))
    {
        /* 25~27 must reach their measured audible ending.  A delayed
         * completion frame from the sound stopped before this sequence must
         * not advance the sequence and let the payout sound cut off "彩金". */
        return 1U;
    }
    if ((index < 25U) &&
        ((uint32_t)(now - s_loud_ctx.track_tick) <
         AUDIO_LOUD_COMPLETE_MIN_MS))
    {
        return 1U;
    }

    s_loud_ctx.waiting_complete = 0U;
    if (s_loud_ctx.track_pos < s_loud_ctx.track_count)
    {
        s_loud_ctx.track_pos++;
    }
    return 1U;
}

/*
 * 触发音效只保留最新请求。玩家连续投币或按键时，新事件直接覆盖尚未发送的
 * 旧事件，停止操作后不会再补播一长串历史声音。
 */
/**
 * @brief 语音调度：设置（对应符号 Audio_EffectSetPending）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectRetryAdvertAsBase、audio_manager.c:Audio_PlayIndexOn、audio_manager.c:Audio_LoopIndexOn。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param loop 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectSetPending(uint16_t index, uint8_t loop)
{
    if (index == 0U)
    {
        return;
    }

    s_effect_ctx.pending_event.index = index;
    s_effect_ctx.pending_event.loop = (loop != 0U) ? 1U : 0U;
    s_effect_ctx.pending_valid = 1U;
}

/**
 * @brief 语音调度：读取（对应符号 Audio_EffectGetPending）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectTryDispatch。
 * @param event 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_EffectGetPending(Audio_EffectEvent *event)
{
    if ((event == 0) || (s_effect_ctx.pending_valid == 0U))
    {
        return 0U;
    }

    *event = s_effect_ctx.pending_event;
    return 1U;
}

/**
 * @brief 语音调度：清除（对应符号 Audio_EffectClearPending）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectTryDispatch、audio_manager.c:Audio_CancelLoopResumeOn。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectClearPending(void)
{
    memset(&s_effect_ctx.pending_event, 0,
           sizeof(s_effect_ctx.pending_event));
    s_effect_ctx.pending_valid = 0U;
}

/**
 * @brief 语音调度：读取（对应符号 Audio_EffectGetRawDurationMs）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectGetDurationMs、audio_manager.c:Audio_EffectGetAudibleMs、audio_manager.c:Audio_EffectGetMinCompleteMs。
 * @局部状态 主要临时变量：count；均只在本次调用中有效。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Audio_EffectGetRawDurationMs(uint16_t index)
{
    uint16_t count;

    count = (uint16_t)(sizeof(s_audio_effect_duration_ms) /
                       sizeof(s_audio_effect_duration_ms[0]));
    if ((index >= 1U) && (index <= count))
    {
        return (uint32_t)s_audio_effect_duration_ms[index - 1U];
    }
    return AUDIO_EFFECT_DEFAULT_MS;
}

/**
 * @brief 语音调度：读取（对应符号 Audio_EffectGetDurationMs）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectStartBase、audio_manager.c:Audio_EffectStartAdvert、audio_manager.c:Audio_LoudSequenceTask。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Audio_EffectGetDurationMs(uint16_t index)
{
    return Audio_EffectGetRawDurationMs(index) +
           AUDIO_EFFECT_DURATION_GUARD_MS;
}

/**
 * @brief 语音调度：读取（对应符号 Audio_EffectGetAudibleMs）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectStartTransient。
 * @局部状态 主要临时变量：count；均只在本次调用中有效。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Audio_EffectGetAudibleMs(uint16_t index)
{
    uint16_t count;

    count = (uint16_t)(sizeof(s_audio_effect_audible_end_ms) /
                       sizeof(s_audio_effect_audible_end_ms[0]));
    if ((index >= 1U) && (index <= count))
    {
        return (uint32_t)s_audio_effect_audible_end_ms[index - 1U] +
               AUDIO_EFFECT_AUDIBLE_GUARD_MS;
    }
    return Audio_EffectGetRawDurationMs(index);
}

/*
 * 完成回包不带可直接用于识别ADVERT1逻辑序号的信息。A暂停后迟到的完成回包
 * 可能出现在B已经开始播放之后，因此不能只用固定120ms判断B已结束。这里按TF卡
 * 中每个B的实测时长建立最早完成门限，并预留120ms解码误差。
 */
/**
 * @brief 语音调度：读取（对应符号 Audio_EffectGetMinCompleteMs）。
 * @调用关系 直接调用者：audio_manager.c:Audio_LoudSequenceHandleComplete、audio_manager.c:Audio_EffectStartAdvert。
 * @局部状态 主要临时变量：duration；均只在本次调用中有效。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Audio_EffectGetMinCompleteMs(uint16_t index)
{
    uint32_t duration;

    duration = Audio_EffectGetRawDurationMs(index);
    if (duration > (AUDIO_EFFECT_COMPLETE_MIN_MS +
                    AUDIO_EFFECT_COMPLETE_MARGIN_MS))
    {
        return duration - AUDIO_EFFECT_COMPLETE_MARGIN_MS;
    }
    return AUDIO_EFFECT_COMPLETE_MIN_MS;
}

/**
 * @brief 语音调度：清除（对应符号 Audio_EffectClearBase）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectPauseBase、audio_manager.c:Audio_EffectHandleComplete、audio_manager.c:Audio_EffectTimeoutTask。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectClearBase(void)
{
    s_effect_ctx.base_active = 0U;
    s_effect_ctx.base_loop = 0U;
    s_effect_ctx.base_index = 0U;
    s_effect_ctx.base_remaining_ms = 0U;
    s_effect_ctx.base_tick = 0U;
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_EffectPauseBase）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectStartAdvert。
 * @局部状态 主要临时变量：elapsed；均只在本次调用中有效。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectPauseBase(uint32_t now)
{
    uint32_t elapsed;

    if ((s_effect_ctx.base_active == 0U) ||
        (s_effect_ctx.base_loop != 0U) ||
        (s_effect_ctx.advert_active != 0U))
    {
        return;
    }

    elapsed = (uint32_t)(now - s_effect_ctx.base_tick);
    if (elapsed >= s_effect_ctx.base_remaining_ms)
    {
        Audio_EffectClearBase();
        return;
    }
    s_effect_ctx.base_remaining_ms -= elapsed;
    s_effect_ctx.base_tick = now;
}

/**
 * @brief 语音调度：启动（对应符号 Audio_EffectStartBase）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectTryDispatch。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param loop 按值传入；函数内部修改不会回写调用方变量。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectStartBase(uint16_t index, uint8_t loop, uint32_t now)
{
    s_effect_ctx.base_active = 1U;
    s_effect_ctx.base_loop = (loop != 0U) ? 1U : 0U;
    s_effect_ctx.base_index = index;
    s_effect_ctx.base_remaining_ms = Audio_EffectGetDurationMs(index);
    s_effect_ctx.base_tick = now;
    s_effect_ctx.advert_active = 0U;
    s_effect_ctx.advert_index = 0U;
    s_effect_ctx.advert_min_complete_ms = 0U;
    s_effect_ctx.advert_timeout_ms = 0U;
}

/**
 * @brief 语音调度：启动（对应符号 Audio_EffectStartAdvert）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectTryDispatch。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectStartAdvert(uint16_t index, uint32_t now)
{
    Audio_EffectPauseBase(now);
    s_effect_ctx.advert_active = 1U;
    s_effect_ctx.advert_index = index;
    s_effect_ctx.advert_tick = now;
    s_effect_ctx.advert_min_complete_ms =
        Audio_EffectGetMinCompleteMs(index);
    s_effect_ctx.advert_timeout_ms = Audio_EffectGetDurationMs(index);
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_EffectFinishAdvert）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectHandleComplete。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectFinishAdvert(uint32_t now)
{
    (void)now;
    s_effect_ctx.advert_active = 0U;
    s_effect_ctx.advert_index = 0U;
    s_effect_ctx.advert_tick = 0U;
    s_effect_ctx.advert_min_complete_ms = 0U;
    s_effect_ctx.advert_timeout_ms = 0U;
    s_effect_ctx.transient_active = 0U;
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_EffectRetryAdvertAsBase）。
 * @调用关系 直接调用者：audio_manager.c:Audio_ProcessRxByte。
 * @局部状态 主要临时变量：index；均只在本次调用中有效。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectRetryAdvertAsBase(uint32_t now)
{
    uint16_t index;

    (void)now;
    index = s_effect_ctx.advert_index;
    s_effect_ctx.advert_active = 0U;
    s_effect_ctx.advert_index = 0U;
    s_effect_ctx.advert_tick = 0U;
    s_effect_ctx.advert_min_complete_ms = 0U;
    s_effect_ctx.advert_timeout_ms = 0U;
    s_effect_ctx.transient_active = 0U;

    /* 已有更新请求时保留最新请求；否则把插播失败的B改为普通根目录播放。 */
    if (s_effect_ctx.pending_valid == 0U)
    {
        Audio_EffectSetPending(index, 0U);
    }
}

/**
 * @brief 语音调度：处理状态或事件（对应符号 Audio_EffectHandleComplete）。
 * @调用关系 直接调用者：audio_manager.c:Audio_ProcessRxByte。
 * @局部状态 主要临时变量：elapsed；均只在本次调用中有效。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectHandleComplete(uint32_t now)
{
    uint32_t elapsed;

    if (s_exclusive_alarm_active != 0U)
    {
        return;
    }

    if (s_effect_ctx.advert_active != 0U)
    {
        elapsed = (uint32_t)(now - s_effect_ctx.advert_tick);
        if (elapsed < s_effect_ctx.advert_min_complete_ms)
        {
            /* 这是基础音效A或更早音效的迟到回包，B仍在播放。 */
            return;
        }
        Audio_EffectFinishAdvert(now);
        return;
    }

    if (s_effect_ctx.transient_active != 0U)
    {
        elapsed = (uint32_t)(now - s_effect_ctx.transient_tick);
        if (elapsed < s_effect_ctx.transient_audible_ms)
        {
            return;
        }
        s_effect_ctx.transient_active = 0U;
        if ((s_effect_ctx.base_active != 0U) &&
            (s_effect_ctx.base_loop == 0U))
        {
            Audio_EffectClearBase();
        }
    }
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_ProcessRxByte）。
 * @调用关系 直接调用者：audio_manager.c:Audio_ProcessRx。
 * @局部状态 主要临时变量：command；均只在本次调用中有效。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_ProcessRxByte(uint8_t value)
{
    uint8_t command;

    if (s_effect_ctx.rx_length == 0U)
    {
        if (value == 0x7EU)
        {
            s_effect_ctx.rx_frame[0] = value;
            s_effect_ctx.rx_length = 1U;
        }
        return;
    }

    if (s_effect_ctx.rx_length >= AUDIO_EFFECT_RX_FRAME_LEN)
    {
        s_effect_ctx.rx_length = 0U;
        return;
    }

    s_effect_ctx.rx_frame[s_effect_ctx.rx_length++] = value;
    if (s_effect_ctx.rx_length < AUDIO_EFFECT_RX_FRAME_LEN)
    {
        return;
    }

    if ((s_effect_ctx.rx_frame[0] == 0x7EU) &&
        (s_effect_ctx.rx_frame[2] == 0x06U) &&
        (s_effect_ctx.rx_frame[AUDIO_EFFECT_RX_FRAME_LEN - 1U] == 0xEFU))
    {
        command = s_effect_ctx.rx_frame[3];
        if ((command == 0x3CU) || (command == 0x3DU) ||
            (command == 0x3EU))
        {
            if (Audio_LoudSequenceHandleComplete(HAL_GetTick()) == 0U)
            {
                Audio_EffectHandleComplete(HAL_GetTick());
            }
        }
        else if ((command == 0x40U) &&
                 (s_effect_ctx.rx_frame[6] == 0x07U) &&
                 (s_effect_ctx.advert_active != 0U))
        {
            Audio_EffectRetryAdvertAsBase(HAL_GetTick());
        }
    }
    s_effect_ctx.rx_length = 0U;
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_ProcessRx）。
 * @调用关系 直接调用者：audio_manager.c:Audio_Task。
 * @局部状态 主要临时变量：data、count、index；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_ProcessRx(void)
{
    uint8_t data[16];
    uint16_t count;
    uint16_t index;

    do
    {
        count = audio_usart_read(AUDIO_UART_CHANNEL_1,
                                 data,
                                 (uint16_t)sizeof(data));
        for (index = 0U; index < count; index++)
        {
            Audio_ProcessRxByte(data[index]);
        }
    } while (count == (uint16_t)sizeof(data));
}

/*
 * 和 joker-music 一样使用阻塞发送。8 字节在 9600 波特率下约占 8.3ms，
 * 发送结束后才启动 120ms 命令保护间隔，避免模块内部上一条命令被覆盖。
 */
/**
 * @brief 语音调度：编码并发送（对应符号 Audio_SendNow）。
 * @调用关系 直接调用者：audio_manager.c:Audio_RequestSend、audio_manager.c:Audio_LoudSequenceTask、audio_manager.c:Audio_TaskOne、audio_manager.c:Audio_EffectTryDispatch、audio_manager.c:Audio_StopOn、audio_manager.c:Audio_StartExclusiveAlarm、audio_manager.c:Audio_StopExclusiveAlarm。
 * @局部状态 主要临时变量：ctx、status；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param length 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_SendNow(Audio_Chip chip,
                             const uint8_t *data,
                             uint16_t length)
{
    Audio_Context *ctx;
    HAL_StatusTypeDef status;

    if ((Audio_IsValidChip(chip) == 0U) || (data == 0) ||
        (length == 0U) || (length > AUDIO_TX_BUF_LEN))
    {
        return 0U;
    }

    ctx = &s_audio_ctx[chip];
    status = HAL_UART_Transmit(Audio_GetHandle(chip),
                               (uint8_t *)data,
                               length,
                               AUDIO_UART_TIMEOUT_MS);
    if (status != HAL_OK)
    {
        return 0U;
    }
    ctx->state = AUDIO_STATE_WAIT;
    ctx->state_tick = HAL_GetTick();
    return 1U;
}

/**
 * @brief 语音调度：编码并发送（对应符号 Audio_RequestSend）。
 * @调用关系 直接调用者：audio_manager.c:Audio_PlayIndexOn、audio_manager.c:Audio_LoopIndexOn、audio_manager.c:Audio_VolumeUpOn、audio_manager.c:Audio_VolumeDownOn、audio_manager.c:Audio_SetVolumeLevelOn、audio_manager.c:Audio_SendHexOn、audio_manager.c:Audio_SendHexStringOn。
 * @局部状态 主要临时变量：ctx；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param length 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_RequestSend(Audio_Chip chip,
                              const uint8_t *data,
                              uint16_t length)
{
    Audio_Context *ctx;

    if ((s_audio_initialized == 0U) ||
        (Audio_IsValidChip(chip) == 0U) || (data == 0) ||
        (length == 0U) || (length > AUDIO_TX_BUF_LEN))
    {
        return;
    }

    /* E15期间两块板只接受专用报警流程，普通按键、投币和背景命令全部丢弃。 */
    if (s_exclusive_alarm_active != 0U)
    {
        return;
    }

    if ((chip == AUDIO_CHIP_2) && (g_background_music_enable == 0U))
    {
        return;
    }

    ctx = &s_audio_ctx[chip];
    if ((ctx->state == AUDIO_STATE_IDLE) &&
        (ctx->startup_volume_frames == 0U) &&
        (ctx->queue_count == 0U))
    {
        if (Audio_SendNow(chip, data, length) != 0U)
        {
            return;
        }
    }

    Audio_QueuePush(ctx, data, length);
}

/*
 * 非阻塞推进高音量庆祝序列：先停止旧音效 -> 音量30 -> 23号若干次 ->
 * 最终音效 -> 恢复P13右屏音效音量。23号只播放到实测有效声音末端；
 * 最终音效仍按KT404C完成回包和超时保护推进。
 */
/**
 * @brief 语音调度：周期处理（对应符号 Audio_LoudSequenceTask）。
 * @调用关系 直接调用者：audio_manager.c:Audio_Task、audio_manager.c:Audio_PlayLoudIndexOn。
 * @局部状态 主要临时变量：ctx、frame、index、timeout_ms；均只在本次调用中有效。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_LoudSequenceTask(uint32_t now)
{
    Audio_Context *ctx;
    uint8_t frame[AUDIO_FRAME_LEN];
    uint16_t index;
    uint32_t timeout_ms;

    if ((s_loud_ctx.active == 0U) ||
        (s_exclusive_alarm_active != 0U))
    {
        return;
    }

    ctx = &s_audio_ctx[AUDIO_CHIP_1];
    if (s_loud_ctx.waiting_complete != 0U)
    {
        if (s_loud_ctx.track_pos < s_loud_ctx.prelude_count)
        {
            if ((uint32_t)(now - s_loud_ctx.track_tick) <
                AUDIO_LOUD_PRELUDE_AUDIBLE_MS)
            {
                return;
            }
        }
        else
        {
            index = s_loud_ctx.track[s_loud_ctx.track_pos];
            timeout_ms = Audio_EffectGetDurationMs(index);
            if ((uint32_t)(now - s_loud_ctx.track_tick) < timeout_ms)
            {
                return;
            }
        }

        /* 播放完成回包丢失时按保护时长继续，不能永久卡住游戏。 */
        s_loud_ctx.waiting_complete = 0U;
        if (s_loud_ctx.track_pos < s_loud_ctx.track_count)
        {
            s_loud_ctx.track_pos++;
        }
    }

    if ((ctx->state != AUDIO_STATE_IDLE) ||
        (ctx->startup_volume_frames != 0U) ||
        (ctx->queue_count != 0U))
    {
        return;
    }

    /* 必须先停013等旧音效，再提高音量，避免旧文件尾音被一起放大。 */
    if (s_loud_ctx.stop_sent == 0U)
    {
        Audio_BuildFrame(frame, 0x16U, 0U);
        if (Audio_SendNow(AUDIO_CHIP_1, frame, sizeof(frame)) != 0U)
        {
            s_loud_ctx.stop_sent = 1U;
        }
        return;
    }

    if (s_loud_ctx.volume_sent == 0U)
    {
        Audio_BuildFrame(frame,
                         0x06U,
                         Audio_GetBoostedVolume(ctx->target_volume));
        if (Audio_SendNow(AUDIO_CHIP_1, frame, sizeof(frame)) != 0U)
        {
            s_loud_ctx.volume_sent = 1U;
        }
        return;
    }

    if (s_loud_ctx.track_pos < s_loud_ctx.track_count)
    {
        index = s_loud_ctx.track[s_loud_ctx.track_pos];
        Audio_BuildFrame(frame, 0x03U, index);
        if (Audio_SendNow(AUDIO_CHIP_1, frame, sizeof(frame)) != 0U)
        {
            s_loud_ctx.waiting_complete = 1U;
            s_loud_ctx.track_tick = now;
        }
        return;
    }

    Audio_BuildFrame(frame, 0x06U, ctx->target_volume);
    if (Audio_SendNow(AUDIO_CHIP_1, frame, sizeof(frame)) != 0U)
    {
        Audio_LoudSequenceReset();
        Audio_EffectReset();
    }
}

/**
 * @brief 语音调度：初始化（对应符号 Audio_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：chip；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_Init(void)
{
    uint8_t chip;

    s_audio_initialized = 0U;
    s_exclusive_alarm_active = 0U;
    memset(s_audio_ctx, 0, sizeof(s_audio_ctx));
    Audio_EffectReset();
    Audio_LoudSequenceReset();

    for (chip = 0U; chip < (uint8_t)AUDIO_CHIP_COUNT; chip++)
    {
        s_audio_ctx[chip].state = AUDIO_STATE_STARTUP_WAIT;
        s_audio_ctx[chip].target_volume = AUDIO_INIT_VOLUME;
        s_audio_ctx[chip].startup_volume_frames =
            AUDIO_STARTUP_VOLUME_REPEAT;
        s_audio_ctx[chip].state_tick = HAL_GetTick();
    }

    s_audio_initialized = 1U;
}

/**
 * @brief 语音调度：周期处理（对应符号 Audio_TaskOne）。
 * @调用关系 直接调用者：audio_manager.c:Audio_Task。
 * @局部状态 主要临时变量：ctx、data、length；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_TaskOne(Audio_Chip chip)
{
    Audio_Context *ctx;
    uint8_t data[AUDIO_TX_BUF_LEN];
    uint16_t length;

    if (Audio_IsValidChip(chip) == 0U)
    {
        return;
    }

    if ((chip == AUDIO_CHIP_2) &&
        ((g_background_music_enable == 0U) ||
         (s_exclusive_alarm_active != 0U)))
    {
        return;
    }

    ctx = &s_audio_ctx[chip];
    if (ctx->state == AUDIO_STATE_STARTUP_WAIT)
    {
        if (Audio_IsElapsed(ctx->state_tick, AUDIO_STARTUP_DELAY_MS) == 0U)
        {
            return;
        }
        ctx->state = AUDIO_STATE_IDLE;
    }

    if (ctx->state == AUDIO_STATE_WAIT)
    {
        if (Audio_IsElapsed(ctx->state_tick, AUDIO_COMMAND_GAP_MS) == 0U)
        {
            return;
        }
        ctx->state = AUDIO_STATE_IDLE;
    }

    if (ctx->state != AUDIO_STATE_IDLE)
    {
        ctx->state = AUDIO_STATE_ERROR;
        return;
    }

    /*
     * Setting_Init()在启动等待期间把EEPROM中的P13写到target_volume。
     * KT404C初始化完成后重复写两次，避免上电先按最大音量播放。
     */
    if (ctx->startup_volume_frames != 0U)
    {
        Audio_BuildFrame(data, 0x06U, ctx->target_volume);
        ctx->startup_volume_frames--;
        Audio_SendNow(chip, data, AUDIO_FRAME_LEN);
        return;
    }

    if (Audio_QueuePop(ctx, data, &length) != 0U)
    {
        if (Audio_SendNow(chip, data, length) == 0U)
        {
            Audio_QueuePush(ctx, data, length);
        }
    }
}

/**
 * @brief 语音调度：周期处理（对应符号 Audio_EffectTimeoutTask）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectTask。
 * @局部状态 主要临时变量：elapsed；均只在本次调用中有效。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectTimeoutTask(uint32_t now)
{
    uint32_t elapsed;

    if (s_effect_ctx.transient_active != 0U)
    {
        elapsed = (uint32_t)(now - s_effect_ctx.transient_tick);
        if (elapsed >= s_effect_ctx.transient_audible_ms)
        {
            /*
             * 到达实际有声结尾就允许恢复013，不等待MP3尾部静音。
             * 若此时已有更新音效，更新音效优先，013暂不插回去。
             */
            s_effect_ctx.transient_active = 0U;
            s_effect_ctx.advert_active = 0U;
            s_effect_ctx.advert_index = 0U;
            s_effect_ctx.advert_tick = 0U;
            s_effect_ctx.advert_min_complete_ms = 0U;
            s_effect_ctx.advert_timeout_ms = 0U;
        }
    }

    if ((s_effect_ctx.base_active != 0U) &&
        (s_effect_ctx.base_loop == 0U) &&
        (s_effect_ctx.transient_active == 0U) &&
        (s_effect_ctx.protected_loop_active == 0U))
    {
        elapsed = (uint32_t)(now - s_effect_ctx.base_tick);
        if (elapsed >= s_effect_ctx.base_remaining_ms)
        {
            Audio_EffectClearBase();
        }
    }
}

/**
 * @brief 语音调度：读取（对应符号 Audio_EffectChannelReady）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectTryDispatch。
 * @局部状态 主要临时变量：ctx；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_EffectChannelReady(void)
{
    Audio_Context *ctx;

    ctx = &s_audio_ctx[AUDIO_CHIP_1];
    if ((ctx->state != AUDIO_STATE_IDLE) ||
        (ctx->startup_volume_frames != 0U) ||
        (ctx->queue_count != 0U))
    {
        return 0U;
    }
    return 1U;
}

/**
 * @brief 语音调度：启动（对应符号 Audio_EffectStartTransient）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectTryDispatch。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectStartTransient(uint16_t index, uint32_t now)
{
    s_effect_ctx.transient_active = 1U;
    s_effect_ctx.transient_index = index;
    s_effect_ctx.transient_tick = now;
    s_effect_ctx.transient_audible_ms = Audio_EffectGetAudibleMs(index);
    if (s_effect_ctx.protected_loop_active != 0U)
    {
        s_effect_ctx.protected_resume_needed = 1U;
    }
}

/* 空闲时立即发送；120ms硬件命令间隔内只保留最后一次请求。 */
/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_EffectTryDispatch）。
 * @调用关系 直接调用者：audio_manager.c:Audio_EffectTask、audio_manager.c:Audio_PlayIndexOn、audio_manager.c:Audio_LoopIndexOn。
 * @局部状态 主要临时变量：event、frame、advert_file、use_advert；均只在本次调用中有效。
 * @param now 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectTryDispatch(uint32_t now)
{
    Audio_EffectEvent event;
    uint8_t frame[AUDIO_FRAME_LEN];
    uint16_t advert_file;
    uint8_t use_advert;

    if ((Audio_EffectChannelReady() == 0U) ||
        (s_exclusive_alarm_active != 0U))
    {
        return;
    }

    if (Audio_EffectGetPending(&event) != 0U)
    {
        if (event.loop != 0U)
        {
            Audio_BuildFrame(frame, 0x08U, event.index);
            if (Audio_SendNow(AUDIO_CHIP_1, frame, sizeof(frame)) != 0U)
            {
                Audio_EffectClearPending();
                s_effect_ctx.protected_loop_active = 1U;
                s_effect_ctx.protected_loop_index = event.index;
                s_effect_ctx.protected_resume_needed = 0U;
                s_effect_ctx.transient_active = 0U;
                Audio_EffectStartBase(event.index, 1U, now);
            }
            return;
        }

        use_advert = 0U;
        if ((s_effect_ctx.protected_loop_active != 0U) &&
            (s_effect_ctx.base_active != 0U) &&
            (s_effect_ctx.base_loop != 0U) &&
            (s_effect_ctx.base_index == s_effect_ctx.protected_loop_index) &&
            (s_effect_ctx.transient_active == 0U) &&
            (event.index <= 255U))
        {
            use_advert = 1U;
        }

        if (use_advert != 0U)
        {
            advert_file =
                (uint16_t)(((uint16_t)AUDIO_EFFECT_ADVERT_FOLDER << 8U) |
                           (event.index & 0x00FFU));
            Audio_BuildFrame(frame, 0x25U, advert_file);
        }
        else
        {
            Audio_BuildFrame(frame, 0x03U, event.index);
        }

        if (Audio_SendNow(AUDIO_CHIP_1, frame, sizeof(frame)) != 0U)
        {
            Audio_EffectClearPending();
            if (use_advert != 0U)
            {
                Audio_EffectStartAdvert(event.index, now);
            }
            else
            {
                Audio_EffectStartBase(event.index, 0U, now);
            }
            Audio_EffectStartTransient(event.index, now);
        }
        return;
    }

    if ((s_effect_ctx.protected_loop_active != 0U) &&
        (s_effect_ctx.protected_resume_needed != 0U) &&
        (s_effect_ctx.transient_active == 0U))
    {
        Audio_BuildFrame(frame, 0x08U,
                         s_effect_ctx.protected_loop_index);
        if (Audio_SendNow(AUDIO_CHIP_1, frame, sizeof(frame)) != 0U)
        {
            s_effect_ctx.protected_resume_needed = 0U;
            Audio_EffectStartBase(s_effect_ctx.protected_loop_index,
                                  1U, now);
        }
    }
}

/**
 * @brief 语音调度：周期处理（对应符号 Audio_EffectTask）。
 * @调用关系 直接调用者：audio_manager.c:Audio_Task。
 * @局部状态 主要临时变量：now；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Audio_EffectTask(void)
{
    uint32_t now;

    if ((s_audio_initialized == 0U) ||
        (s_exclusive_alarm_active != 0U))
    {
        return;
    }

    now = HAL_GetTick();
    Audio_EffectTimeoutTask(now);
    Audio_EffectTryDispatch(now);
}

/**
 * @brief 语音调度：周期处理（对应符号 Audio_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_Task(void)
{
    Audio_ProcessRx();
    Audio_TaskOne(AUDIO_CHIP_1);
    Audio_TaskOne(AUDIO_CHIP_2);
    if (s_loud_ctx.active != 0U)
    {
        Audio_LoudSequenceTask(HAL_GetTick());
    }
    else
    {
        Audio_EffectTask();
    }
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_PlayLoudIndexOn）。
 * @调用关系 直接调用者：audio_manager.c:Audio_PlayIndexOn、game.c:Game_PlayJackpotAwardAudio、game.c:Game_Task、lottery.c:Lottery_Task、standby.c:Standby_PlayAnnouncement。
 * @局部状态 主要临时变量：pos；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param prelude_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Audio_PlayLoudIndexOn(Audio_Chip chip,
                              uint16_t index,
                              uint8_t prelude_count)
{
    uint8_t pos;

    if ((chip != AUDIO_CHIP_1) ||
        (s_audio_initialized == 0U) ||
        (s_exclusive_alarm_active != 0U) ||
        (index < 17U) || (index > 28U) ||
        (prelude_count >= AUDIO_LOUD_SEQUENCE_MAX_TRACKS))
    {
        return 0U;
    }

    Audio_QueueClear(&s_audio_ctx[AUDIO_CHIP_1]);
    Audio_EffectReset();
    Audio_LoudSequenceReset();

    for (pos = 0U; pos < prelude_count; pos++)
    {
        s_loud_ctx.track[pos] = AUDIO_LOUD_PRELUDE_INDEX;
    }
    s_loud_ctx.track[prelude_count] = index;
    s_loud_ctx.track_count = (uint8_t)(prelude_count + 1U);
    s_loud_ctx.prelude_count = prelude_count;
    s_loud_ctx.active = 1U;

    Audio_LoudSequenceTask(HAL_GetTick());
    return 1U;
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_IsLoudSequenceBusy）。
 * @调用关系 直接调用者：game.c:Game_Task、lottery.c:Lottery_Task、standby.c:Standby_AnnouncementDone、standby.c:Standby_CanStart。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Audio_IsLoudSequenceBusy(void)
{
    return s_loud_ctx.active;
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_PlayIndexOn）。
 * @调用关系 直接调用者：game.c:Game_StartCardPayout、game.c:Game_FinishTicketSkipHold、game.c:Game_ProcessCoinCount、game.c:Game_EnterSettingMode、game.c:Game_Task、gift_light.c:GiftLight_Task、lottery.c:Lottery_Task、standby.c:Standby_PlayEffect。
 * @局部状态 主要临时变量：frame；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_PlayIndexOn(Audio_Chip chip, uint16_t index)
{
    uint8_t frame[AUDIO_FRAME_LEN];

    if ((Audio_IsValidChip(chip) == 0U) || (index == 0U))
    {
        return;
    }

    /* 17~27无论从哪个业务入口触发，都统一执行根目录临时高音量播放。 */
    if ((chip == AUDIO_CHIP_1) && (index >= 17U) && (index <= 27U))
    {
        (void)Audio_PlayLoudIndexOn(chip, index, 0U);
        return;
    }

    if (chip == AUDIO_CHIP_1)
    {
        if ((s_audio_initialized != 0U) &&
            (s_exclusive_alarm_active == 0U) &&
            (s_loud_ctx.active == 0U))
        {
            Audio_EffectSetPending(index, 0U);
            Audio_EffectTryDispatch(HAL_GetTick());
        }
        return;
    }

    Audio_BuildFrame(frame, 0x03U, index);
    Audio_RequestSend(chip, frame, sizeof(frame));
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_LoopIndexOn）。
 * @调用关系 直接调用者：game.c:Game_AudioSetBack、gift_light.c:GiftLight_StartRunPhase、lottery.c:Lottery_StartOneRequest、standby.c:Standby_PlayLoop。
 * @局部状态 主要临时变量：frame；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_LoopIndexOn(Audio_Chip chip, uint16_t index)
{
    uint8_t frame[AUDIO_FRAME_LEN];

    if ((Audio_IsValidChip(chip) == 0U) || (index == 0U))
    {
        return;
    }

    if (chip == AUDIO_CHIP_1)
    {
        if ((s_audio_initialized != 0U) &&
            (s_exclusive_alarm_active == 0U) &&
            (s_loud_ctx.active == 0U))
        {
            s_effect_ctx.protected_loop_active = 1U;
            s_effect_ctx.protected_loop_index = index;
            s_effect_ctx.protected_resume_needed = 0U;
            Audio_EffectSetPending(index, 1U);
            Audio_EffectTryDispatch(HAL_GetTick());
        }
        return;
    }

    Audio_BuildFrame(frame, 0x08U, index);
    Audio_RequestSend(chip, frame, sizeof(frame));
}

/*
 * 只取消音效板循环音的自动恢复，不额外发送停止命令。抽奖结果音效紧接着会
 * 立即覆盖013，因此这里发送0x16反而会多制造一次120ms命令等待。
 */
/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_CancelLoopResumeOn）。
 * @调用关系 直接调用者：gift_light.c:GiftLight_Task、lottery.c:Lottery_Task。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_CancelLoopResumeOn(Audio_Chip chip)
{
    if ((Audio_IsValidChip(chip) == 0U) ||
        (chip != AUDIO_CHIP_1))
    {
        return;
    }

    s_effect_ctx.protected_loop_active = 0U;
    s_effect_ctx.protected_loop_index = 0U;
    s_effect_ctx.protected_resume_needed = 0U;
    if ((s_effect_ctx.pending_valid != 0U) &&
        (s_effect_ctx.pending_event.loop != 0U))
    {
        Audio_EffectClearPending();
    }
}

/* 清掉指定板的旧命令并立即发送最高优先级0x16，不等待普通队列。 */
/**
 * @brief 语音调度：停止（对应符号 Audio_StopOn）。
 * @调用关系 直接调用者：standby.c:Standby_StopEffect。
 * @局部状态 主要临时变量：frame、restore_volume；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_StopOn(Audio_Chip chip)
{
    uint8_t frame[AUDIO_FRAME_LEN];
    uint8_t restore_volume;

    if ((s_audio_initialized == 0U) ||
        (Audio_IsValidChip(chip) == 0U) ||
        (s_exclusive_alarm_active != 0U))
    {
        return;
    }

    restore_volume = (chip == AUDIO_CHIP_1) ? 1U : 0U;
    if (chip == AUDIO_CHIP_1)
    {
        Audio_LoudSequenceReset();
        Audio_EffectReset();
    }
    Audio_QueueClear(&s_audio_ctx[chip]);
    Audio_BuildFrame(frame, 0x16U, 0U);
    Audio_SendNow(chip, frame, sizeof(frame));
    if (restore_volume != 0U)
    {
        /* Also restore after repeated stops: a previous volume restore may
         * still have been queued when the newer stop cleared the queue. */
        Audio_BuildFrame(frame, 0x06U, s_audio_ctx[chip].target_volume);
        Audio_QueuePush(&s_audio_ctx[chip], frame, sizeof(frame));
    }
}

/*
 * E15专用入口：两块板的旧命令全部作废，先立即停止两块板，再让音效板
 * 播放指定报警音并把当前曲目设为单曲循环。普通音效和背景命令继续被拒绝。
 *
 * 这里不再只发0x08。根据KT404C V1.6手册，0x03是已经实机验证的指定曲目
 * 播放，0x19=0是在播放状态下开启当前曲目循环；两条命令分开发送，可以确保
 * 即使某些模块固件对0x08兼容性不好，报警音也至少会立即开始播放。
 */
/**
 * @brief 语音调度：启动（对应符号 Audio_StartExclusiveAlarm）。
 * @调用关系 直接调用者：fault.c:Fault_Report。
 * @局部状态 主要临时变量：frame；均只在本次调用中有效。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_StartExclusiveAlarm(uint16_t index)
{
    uint8_t frame[AUDIO_FRAME_LEN];

    if ((s_audio_initialized == 0U) || (index == 0U))
    {
        return;
    }

    s_exclusive_alarm_active = 1U;
    Audio_LoudSequenceReset();
    Audio_EffectReset();
    Audio_QueueClear(&s_audio_ctx[AUDIO_CHIP_1]);
    Audio_QueueClear(&s_audio_ctx[AUDIO_CHIP_2]);

    Audio_BuildFrame(frame, 0x16U, 0U);
    Audio_SendNow(AUDIO_CHIP_1, frame, sizeof(frame));
    Audio_SendNow(AUDIO_CHIP_2, frame, sizeof(frame));

    /* 若报警抢占了庆祝序列，先恢复P13右屏音量，避免报警后一直停在30。 */
    Audio_BuildFrame(frame, 0x06U,
                     s_audio_ctx[AUDIO_CHIP_1].target_volume);
    Audio_QueuePush(&s_audio_ctx[AUDIO_CHIP_1], frame, sizeof(frame));

    Audio_BuildFrame(frame, 0x03U, index);
    Audio_QueuePush(&s_audio_ctx[AUDIO_CHIP_1], frame, sizeof(frame));

    Audio_BuildFrame(frame, 0x19U, 0U);
    Audio_QueuePush(&s_audio_ctx[AUDIO_CHIP_1], frame, sizeof(frame));
}

/* E15解除时立即停止09循环；背景音乐由Game状态机重新下发。 */
/**
 * @brief 语音调度：停止（对应符号 Audio_StopExclusiveAlarm）。
 * @调用关系 直接调用者：fault.c:Fault_Clear。
 * @局部状态 主要临时变量：frame；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_StopExclusiveAlarm(void)
{
    uint8_t frame[AUDIO_FRAME_LEN];

    if (s_audio_initialized == 0U)
    {
        return;
    }

    Audio_QueueClear(&s_audio_ctx[AUDIO_CHIP_1]);
    Audio_LoudSequenceReset();
    Audio_EffectReset();
    Audio_BuildFrame(frame, 0x16U, 0U);
    Audio_SendNow(AUDIO_CHIP_1, frame, sizeof(frame));
    s_exclusive_alarm_active = 0U;
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_VolumeUpOn）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：frame；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_VolumeUpOn(Audio_Chip chip)
{
    uint8_t frame[AUDIO_FRAME_LEN];

    if (Audio_IsValidChip(chip) == 0U)
    {
        return;
    }
    if ((chip == AUDIO_CHIP_1) && (s_loud_ctx.active != 0U))
    {
        return;
    }

    Audio_BuildFrame(frame, 0x04U, 0U);
    Audio_RequestSend(chip, frame, sizeof(frame));
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_VolumeDownOn）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：frame；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_VolumeDownOn(Audio_Chip chip)
{
    uint8_t frame[AUDIO_FRAME_LEN];

    if (Audio_IsValidChip(chip) == 0U)
    {
        return;
    }
    if ((chip == AUDIO_CHIP_1) && (s_loud_ctx.active != 0U))
    {
        return;
    }

    Audio_BuildFrame(frame, 0x05U, 0U);
    Audio_RequestSend(chip, frame, sizeof(frame));
}

/**
 * @brief 语音调度：设置（对应符号 Audio_SetVolumeLevelOn）。
 * @调用关系 直接调用者：audio_manager.c:Audio_SetVolumeLevel、setting.c:Setting_ApplyVolume。
 * @局部状态 主要临时变量：ctx、frame；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @param level 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_SetVolumeLevelOn(Audio_Chip chip, uint8_t level)
{
    Audio_Context *ctx;
    uint8_t frame[AUDIO_FRAME_LEN];

    if (Audio_IsValidChip(chip) == 0U)
    {
        return;
    }

    if (level > AUDIO_MODULE_MAX_VOLUME)
    {
        level = AUDIO_MODULE_MAX_VOLUME;
    }

    ctx = &s_audio_ctx[chip];
    ctx->target_volume = level;

    if ((s_audio_initialized == 0U) ||
        (ctx->state == AUDIO_STATE_STARTUP_WAIT) ||
        (ctx->startup_volume_frames != 0U) ||
        (s_exclusive_alarm_active != 0U) ||
        ((chip == AUDIO_CHIP_1) && (s_loud_ctx.active != 0U)))
    {
        return;
    }

    Audio_BuildFrame(frame, 0x06U, level);
    Audio_RequestSend(chip, frame, sizeof(frame));
}

/**
 * @brief 语音调度：设置（对应符号 Audio_SetVolumeLevel）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param level 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_SetVolumeLevel(uint8_t level)
{
    Audio_SetVolumeLevelOn(AUDIO_CHIP_1, level);
    Audio_SetVolumeLevelOn(AUDIO_CHIP_2, level);
}

/**
 * @brief 语音调度：编码并发送（对应符号 Audio_SendHexOn）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @param hex 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Audio_SendHexOn(Audio_Chip chip, const uint8_t *hex, uint16_t len)
{
    if ((Audio_IsValidChip(chip) == 0U) || (hex == 0) ||
        (len == 0U) || (len > AUDIO_TX_BUF_LEN))
    {
        return;
    }

    Audio_RequestSend(chip, hex, len);
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_HexCharToValue）。
 * @调用关系 直接调用者：audio_manager.c:Audio_ParseHexString。
 * @param ch 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static int8_t Audio_HexCharToValue(char ch)
{
    if ((ch >= '0') && (ch <= '9'))
    {
        return (int8_t)(ch - '0');
    }
    if ((ch >= 'a') && (ch <= 'f'))
    {
        return (int8_t)(ch - 'a' + 10);
    }
    if ((ch >= 'A') && (ch <= 'F'))
    {
        return (int8_t)(ch - 'A' + 10);
    }
    return -1;
}

/**
 * @brief 语音调度：执行内部处理（对应符号 Audio_IsSeparator）。
 * @调用关系 直接调用者：audio_manager.c:Audio_ParseHexString。
 * @param ch 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_IsSeparator(char ch)
{
    return ((ch == ' ') || (ch == '\t') || (ch == '\r') ||
            (ch == '\n') || (ch == ',') || (ch == ':') ||
            (ch == '-') || (ch == '_')) ? 1U : 0U;
}

/**
 * @brief 语音调度：解析并校验（对应符号 Audio_ParseHexString）。
 * @调用关系 直接调用者：audio_manager.c:Audio_SendHexStringOn。
 * @局部状态 主要临时变量：count、high、low；均只在本次调用中有效。
 * @param str 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param out 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param out_max 按值传入；函数内部修改不会回写调用方变量。
 * @param out_len 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Audio_ParseHexString(const char *str,
                                    uint8_t *out,
                                    uint16_t out_max,
                                    uint16_t *out_len)
{
    uint16_t count;
    int8_t high;
    int8_t low;

    if ((str == 0) || (out == 0) || (out_len == 0) || (out_max == 0U))
    {
        return 0U;
    }

    count = 0U;
    while (*str != '\0')
    {
        while (Audio_IsSeparator(*str) != 0U)
        {
            str++;
        }
        if (*str == '\0')
        {
            break;
        }
        if ((str[0] == '0') && ((str[1] == 'x') || (str[1] == 'X')))
        {
            str += 2;
        }

        high = Audio_HexCharToValue(*str++);
        low = Audio_HexCharToValue(*str++);
        if ((high < 0) || (low < 0) || (count >= out_max))
        {
            return 0U;
        }
        out[count++] = (uint8_t)(((uint8_t)high << 4U) | (uint8_t)low);
    }

    if (count == 0U)
    {
        return 0U;
    }
    *out_len = count;
    return 1U;
}

/**
 * @brief 语音调度：编码并发送（对应符号 Audio_SendHexStringOn）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @局部状态 主要临时变量：data、length；均只在本次调用中有效。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @param hex_string 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Audio_SendHexStringOn(Audio_Chip chip, const char *hex_string)
{
    uint8_t data[AUDIO_TX_BUF_LEN];
    uint16_t length;

    if (Audio_IsValidChip(chip) == 0U)
    {
        return 0U;
    }

    length = 0U;
    if (Audio_ParseHexString(hex_string, data, sizeof(data), &length) == 0U)
    {
        return 0U;
    }

    Audio_RequestSend(chip, data, length);
    return 1U;
}

/**
 * @brief 语音调度：读取（对应符号 Audio_GetStateOn）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @param chip 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
Audio_State Audio_GetStateOn(Audio_Chip chip)
{
    if (Audio_IsValidChip(chip) == 0U)
    {
        return AUDIO_STATE_ERROR;
    }
    return s_audio_ctx[chip].state;
}
