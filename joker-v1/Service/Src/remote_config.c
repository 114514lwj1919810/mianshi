/* AUTO-FILE-DOC
 * 文件职责：手持无线业务协议。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "remote_config.h"

#include "account.h"
#include "app_protocol.h"
#include "fault.h"
#include "gtim.h"
#include "jackpot.h"
#include "nrf24l01.h"
#include "pcf8563.h"
#include "segment.h"
#include "setting.h"
#include "spi.h"
#include "gd32f407.h"
#include "gd32f407_hal.h"

#define REMOTE_REPLY_GUARD_MS          5U
#define REMOTE_REPLY_GAP_MS            10U
#define REMOTE_REPLY_QUEUE_DEPTH       8U
#define REMOTE_DISPLAY_HOLD_MS         5000U
#define REMOTE_LED_CONNECTING_PERIOD_MS 500U
#define REMOTE_LED_CONNECTED_PERIOD_MS  1000U
#define REMOTE_STATUS_LED_ON_LEVEL     0U
#define REMOTE_IAP_VERIFY_BLINK_MS      500U

#define REMOTE_DISPLAY_NONE            0U
#define REMOTE_DISPLAY_PARAM           1U
#define REMOTE_DISPLAY_ACCOUNT         2U

typedef struct
{
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
} RemoteTimeValue;

typedef struct
{
    uint8_t frame[APP_FRAME_MAX_LEN];
    uint8_t len;
} RemoteTxItem;

/*
 * 接收侧状态：
 * - s_rx_buf/s_rx_len 保存一条已经通过 CRC 的应用 payload；
 * - s_command_pending 是“生产者 Remote_RadioService / 消费者 Remote_DataService”
 *   之间的一次性标志，消费后立即清零；
 * - s_current_seq 来自请求帧，所有响应必须带回同一个 seq，手持端据此排除旧包。
 *
 * 页面请求状态：
 * - selected 只表示当前机器是否被手持端选中；
 * - account/fault/exit 是跨模块的一次性请求，由 Game_Task 的 Take 接口消费；
 * - exit 只释放远程页面，绝不能清余额、清奖励、绕过故障或解码锁。
 */
static uint8_t s_rx_buf[PACKET_SIZE];
static uint8_t s_rx_len;
static uint8_t s_command_pending;
static uint8_t s_current_seq;
static uint8_t s_selected_flag;
static uint8_t s_account_page_request;
static uint8_t s_exit_request; /* Consumed once by Game_Task after radio processing. */
static uint8_t s_fault_clear_request;

/*
 * 应答队列采用固定长度环形缓冲区。head 指向下一条待发项，tail 指向下一个
 * 可写位置，count 是有效项数；s_tx_active 防止上一包尚未得到无线结果时重入。
 * 所有数组下标都必须先对 REMOTE_REPLY_QUEUE_DEPTH 取模。
 */
static RemoteTxItem s_tx_queue[REMOTE_REPLY_QUEUE_DEPTH];
static uint8_t s_tx_queue_head;
static uint8_t s_tx_queue_tail;
static uint8_t s_tx_queue_count;
static uint8_t s_tx_active;
static uint32_t s_tx_next_start_ms;

/*
 * 最近一次有副作用命令的幂等缓存。无线层可能因 ACK 丢失而重发同一个 seq；
 * 命令内容和 seq 都相同就只重发旧结果，不再次写参数或清故障。
 */
static uint8_t s_last_set_seq_valid;
static uint8_t s_last_set_seq;
static uint8_t s_last_set_index;
static uint8_t s_last_set_status;
static uint16_t s_last_set_middle_value;
static uint16_t s_last_set_right_value;
static uint8_t s_last_clear_seq_valid;
static uint8_t s_last_clear_seq;
static uint8_t s_last_clear_status;

/* 远程覆盖显示只保留 REMOTE_DISPLAY_HOLD_MS，到期后由本机游戏界面继续刷新。 */
static uint8_t s_display_type;
static uint8_t s_display_index;
static uint32_t s_display_start_ms;
static uint32_t s_status_led_last_toggle_ms;
#if !defined(BOOT_APP_BASE)
static uint8_t s_status_led_last_slave_state = 0xFFU;
#endif

/* 无线P1~P12与本机归并后的前12页完全一致。 */
static const uint8_t s_remote_param_to_setting[APP_PARAM_COUNT] =
{
    SETTING_PARAM_AWARD_MODE,
    SETTING_PARAM_COINS_PER_POINT,
    SETTING_PARAM_LOTTERY_PAYOUT_PERCENT,
    SETTING_PARAM_TICKETS_PER_COIN,
    SETTING_PARAM_LOTTERY_DIFFICULTY,
    SETTING_PARAM_CLEAR_ACCOUNT,
    SETTING_PARAM_FACTORY_RESET,
    SETTING_PARAM_VOLUME,
    SETTING_PARAM_JACKPOT_BASE,
    SETTING_PARAM_CARD_RATIO,
    SETTING_PARAM_STANDBY_SOUND_ENABLE,
    SETTING_PARAM_JACKPOT_COINS_PER_POINT
};

static void Remote_FillAccountValues(uint32_t *values);

volatile uint32_t g_remote_rx_total;
volatile uint32_t g_remote_tx_total;
volatile uint32_t g_remote_tx_fail_total;
volatile uint32_t g_remote_param_write_total;
volatile uint32_t g_remote_fault_clear_total;
volatile uint32_t g_remote_unknown_command_total;
volatile uint8_t g_remote_last_command;
volatile uint8_t g_remote_last_response_status;
volatile uint8_t g_remote_selected_flag;
volatile uint8_t g_remote_last_tx_result;
volatile uint8_t g_remote_last_fault_code;
volatile uint32_t g_remote_time_set_total;
volatile uint16_t g_remote_last_time_year;
volatile uint8_t g_remote_last_time_month;
volatile uint8_t g_remote_last_time_day;
volatile uint8_t g_remote_last_time_hour;
volatile uint8_t g_remote_last_time_minute;
volatile uint8_t g_remote_last_time_second;

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_DeadlineReached）。
 * @调用关系 直接调用者：remote_config.c:Remote_TxService。
 * @param deadline 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_DeadlineReached(uint32_t deadline)
{
    return ((int32_t)(HAL_GetTick() - deadline) >= 0) ? 1U : 0U;
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_ParamValueAllowed）。
 * @调用关系 直接调用者：remote_config.c:Remote_ParamPairAllowed。
 * @局部状态 主要临时变量：coins、cards；均只在本次调用中有效。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_ParamValueAllowed(uint8_t index, uint16_t value)
{
    switch (index)
    {
        case SETTING_PARAM_AWARD_MODE:
            return ((value >= 1U) && (value <= 4U)) ? 1U : 0U;

        case SETTING_PARAM_SCORE_EXCHANGE_ENABLE:
        case SETTING_PARAM_CLEAR_ACCOUNT:
        case SETTING_PARAM_FACTORY_RESET:
        case SETTING_PARAM_AMBIENT_LED_FLASH:
        case SETTING_PARAM_STANDBY_SOUND_ENABLE:
            return (value <= 1U) ? 1U : 0U;

        case SETTING_PARAM_COIN_TO_POINT:
            return ((value >= 1U) && (value <= 10U)) ? 1U : 0U;

        case SETTING_PARAM_COINS_PER_POINT:
            return ((value >= 1U) && (value <= 5U)) ? 1U : 0U;

        case SETTING_PARAM_LOTTERY_DIFFICULTY:
            return ((value >= SETTING_LOTTERY_DIFFICULTY_MIN) &&
                    (value <= SETTING_LOTTERY_DIFFICULTY_MAX)) ? 1U : 0U;

        case SETTING_PARAM_LOTTERY_PAYOUT_PERCENT:
            return ((value >= 85U) && (value <= 98U)) ? 1U : 0U;

        case SETTING_PARAM_TICKETS_PER_COIN:
            return (((value >= 1U) && (value <= 10U)) ||
                    ((value >= 24U) && (value <= 50U))) ? 1U : 0U;

        case SETTING_PARAM_SOLENOID_EYE_CHECK:
            return (value <= 2U) ? 1U : 0U;

        case SETTING_PARAM_DROP_INTERVAL_TENTH_SECOND:
            return ((value >= 10U) && (value <= 30U)) ? 1U : 0U;

        case SETTING_PARAM_VOLUME:
            return (value <= 30U) ? 1U : 0U;

        case SETTING_PARAM_JACKPOT_BASE:
            return (value <= 999U) ? 1U : 0U;

        case SETTING_PARAM_RECOVERY_DROP:
            return (value <= SETTING_RECOVERY_DROP_MAX) ? 1U : 0U;

        case SETTING_PARAM_JACKPOT_COINS_PER_POINT:
            return ((value >= SETTING_JACKPOT_COINS_PER_POINT_MIN) &&
                    (value <= SETTING_JACKPOT_COINS_PER_POINT_MAX) &&
                    ((value % SETTING_JACKPOT_COINS_PER_POINT_STEP) == 0U)) ? 1U : 0U;

        case SETTING_PARAM_CARD_RATIO:
        {
            uint16_t coins = value / SETTING_CARD_RATIO_FACTOR;
            uint16_t cards = value % SETTING_CARD_RATIO_FACTOR;

            return ((coins >= SETTING_CARD_RATIO_MIN) &&
                    (coins <= SETTING_CARD_RATIO_MAX) &&
                    (cards >= SETTING_CARD_RATIO_MIN) &&
                    (cards <= SETTING_CARD_RATIO_MAX)) ? 1U : 0U;
        }

        default:
            return 0U;
    }
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_ParamPairAllowed）。
 * @调用关系 直接调用者：remote_config.c:Remote_MapSetStatus。
 * @局部状态 主要临时变量：award_mode；均只在本次调用中有效。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param middle_value 按值传入；函数内部修改不会回写调用方变量。
 * @param right_value 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_ParamPairAllowed(uint8_t index,
                                       uint16_t middle_value,
                                       uint16_t right_value)
{
    uint16_t award_mode;

    if (index >= SETTING_PARAM_COUNT)
    {
        return 0U;
    }

    if (index == SETTING_PARAM_VOLUME)
    {
        return ((middle_value <= 30U) && (right_value <= 30U)) ? 1U : 0U;
    }

    if (index == SETTING_PARAM_CARD_RATIO)
    {
        return ((middle_value >= SETTING_CARD_RATIO_MIN) &&
                (middle_value <= SETTING_CARD_RATIO_MAX) &&
                (right_value >= SETTING_CARD_RATIO_MIN) &&
                (right_value <= SETTING_CARD_RATIO_MAX)) ? 1U : 0U;
    }

    if (middle_value != APP_PARAM_VALUE_UNUSED)
    {
        return 0U;
    }

    if (Remote_ParamValueAllowed(index, right_value) == 0U)
    {
        return 0U;
    }

    if (index == SETTING_PARAM_TICKETS_PER_COIN)
    {
        award_mode = Setting_GetValue(SETTING_PARAM_AWARD_MODE);
        if ((award_mode == SETTING_AWARD_MODE_TICKET) &&
            ((right_value < SETTING_TICKET_ONLY_TICKETS_MIN) ||
             (right_value > SETTING_TICKET_ONLY_TICKETS_MAX)))
        {
            return 0U;
        }
        if ((award_mode == SETTING_AWARD_MODE_COIN_AND_TICKET) &&
            ((right_value < SETTING_COMBINED_TICKETS_MIN) ||
             (right_value > SETTING_COMBINED_TICKETS_MAX)))
        {
            return 0U;
        }
    }

    return 1U;
}

/**
 * @brief 手持无线业务协议：读取（对应符号 Remote_GetParamPair）。
 * @调用关系 直接调用者：remote_config.c:Remote_SendParameterSnapshot。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param middle_value 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param right_value 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_GetParamPair(uint8_t index,
                                uint16_t *middle_value,
                                uint16_t *right_value)
{
    uint8_t param_index;

    *middle_value = APP_PARAM_VALUE_UNUSED;
    *right_value = 0U;

    if (index >= APP_PARAM_COUNT)
    {
        return;
    }
    param_index = s_remote_param_to_setting[index];

    if (param_index == SETTING_PARAM_VOLUME)
    {
        *middle_value = Setting_GetBackgroundVolume();
        *right_value = Setting_GetEffectVolume();
    }
    else if (param_index == SETTING_PARAM_CARD_RATIO)
    {
        *middle_value = Setting_GetCardCoinsPerBatch();
        *right_value = Setting_GetCardsPerBatch();
    }
    else
    {
        *right_value = Setting_GetValue(param_index);
    }
}

/**
 * @brief 手持无线业务协议：刷新显示（对应符号 Remote_ShowParam）。
 * @调用关系 直接调用者：remote_config.c:Remote_DataService。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_ShowParam(uint8_t index)
{
    if (index >= APP_PARAM_COUNT)
    {
        return;
    }

    s_display_type = REMOTE_DISPLAY_PARAM;
    s_display_index = index;
    s_display_start_ms = Gtim_NowMs();
}

/**
 * @brief 手持无线业务协议：刷新显示（对应符号 Remote_ShowAccount）。
 * @调用关系 直接调用者：remote_config.c:Remote_DataService。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_ShowAccount(uint8_t index)
{
    if (index >= APP_ACCOUNT_COUNT)
    {
        return;
    }

    s_display_type = REMOTE_DISPLAY_ACCOUNT;
    s_display_index = index;
    s_display_start_ms = Gtim_NowMs();
}

/**
 * @brief 手持无线业务协议：刷新显示（对应符号 Remote_ShowRight3）。
 * @调用关系 直接调用者：remote_config.c:Remote_DisplayTask。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_ShowRight3(uint32_t value)
{
    if (value > 999UL)
    {
        value = 999UL;
    }

    Segment_ClearMiddle();
    Segment_SetRightNumber((uint16_t)value);
}

/**
 * @brief 手持无线业务协议：周期处理（对应符号 Remote_DisplayTask）。
 * @调用关系 直接调用者：remote_config.c:RemoteConfig_Task。
 * @局部状态 主要临时变量：account_values；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_DisplayTask(void)
{
    uint32_t account_values[APP_ACCOUNT_COUNT];
    uint8_t param_index;

    if (s_display_type == REMOTE_DISPLAY_NONE)
    {
        return;
    }

    if (Gtim_IsElapsed(s_display_start_ms, REMOTE_DISPLAY_HOLD_MS) != 0U)
    {
        s_display_type = REMOTE_DISPLAY_NONE;
        return;
    }

    Segment_ClearLeft();
    Segment_ClearMiddle();
    Segment_ClearRight();
    if (s_display_type == REMOTE_DISPLAY_PARAM)
    {
        param_index = s_remote_param_to_setting[s_display_index];
        Segment_SetLeftPNumber((uint8_t)(s_display_index + 1U));
        if (param_index == SETTING_PARAM_CARD_RATIO)
        {
            Segment_SetMiddleNumber(Setting_GetCardCoinsPerBatch());
            Segment_SetRightNumber(Setting_GetCardsPerBatch());
        }
        else if (param_index == SETTING_PARAM_VOLUME)
        {
            Segment_SetMiddleNumber(Setting_GetBackgroundVolume());
            Segment_SetRightNumber(Setting_GetEffectVolume());
        }
        else
        {
            Segment_ClearMiddle();
            Remote_ShowRight3(Setting_GetValue(param_index));
        }
    }
    else
    {
        Segment_SetLeftCNumber((uint8_t)(s_display_index + 1U));
        Remote_FillAccountValues(account_values);
        Remote_ShowRight3(account_values[s_display_index]);
    }
}

/**
 * @brief 手持无线业务协议：周期处理（对应符号 RemoteConfig_StatusLedTask）。
 * @调用关系 直接调用者：remote_config.c:RemoteConfig_Task。
 * @局部状态 主要临时变量：now、period、slave_state；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void RemoteConfig_StatusLedTask(void)
{
    uint32_t now;
#if !defined(BOOT_APP_BASE)
    uint32_t period;
    uint8_t slave_state;
#endif

    now = Gtim_NowMs();

#if defined(BOOT_APP_BASE) && defined(MACHINE_LOCK_FEATURE_ENABLE) && \
    (MACHINE_LOCK_FEATURE_ENABLE == 0)
    /*
     * NO_FUAH IAP 版本识别标志：PC13 为低电平点亮，因此持续输出
     * REMOTE_STATUS_LED_ON_LEVEL(0) 即为常亮。这里每次任务循环都重申
     * 点亮状态，避免无线状态机或前一分区遗留电平把指示灯改回闪烁。
     * FUAH IAP 版本不会进入本分支，仍保留下方原有的 1 秒闪烁标志。
     */
    (void)now;
    (void)s_status_led_last_toggle_ms;
    GD32F407_HeartbeatSet(REMOTE_STATUS_LED_ON_LEVEL);
    return;
#elif defined(BOOT_APP_BASE)
    /*
     * TF 卡升级验证标志：仅 IAP 分区版应用会进入这里。
     * PC13 每 500 ms 翻转一次，完整亮灭周期为 1 秒，并且不受
     * NRF24L01 的连接状态影响。普通（非 IAP）工程继续使用下方
     * 原有的无线状态指示逻辑，因此本测试不会改变普通烧录版本。
     */
    if ((uint32_t)(now - s_status_led_last_toggle_ms) >= REMOTE_IAP_VERIFY_BLINK_MS)
    {
        s_status_led_last_toggle_ms = now;
        GD32F407_HeartbeatToggle();
    }
    return;
#else
    slave_state = (uint8_t)g_SlaveState;

    if (slave_state != s_status_led_last_slave_state)
    {
        s_status_led_last_slave_state = slave_state;
        s_status_led_last_toggle_ms = now;
        GD32F407_HeartbeatSet(REMOTE_STATUS_LED_ON_LEVEL);
    }

    if (g_SlaveState == SLAVE_WAIT_CONNECT)
    {
        period = REMOTE_LED_CONNECTING_PERIOD_MS;
    }
    else if (g_SlaveState == SLAVE_CONNECTED)
    {
        period = REMOTE_LED_CONNECTED_PERIOD_MS;
    }
    else
    {
        GD32F407_HeartbeatSet(REMOTE_STATUS_LED_ON_LEVEL);
        s_status_led_last_toggle_ms = now;
        return;
    }

    if ((uint32_t)(now - s_status_led_last_toggle_ms) >= period)
    {
        s_status_led_last_toggle_ms = now;
        GD32F407_HeartbeatToggle();
    }
#endif
}

/**
 * @brief 手持无线业务协议：计算 CRC（对应符号 Remote_Crc16）。
 * @调用关系 直接调用者：remote_config.c:Remote_BuildFrame、remote_config.c:Remote_ParseFrame。
 * @局部状态 主要临时变量：crc、i、bit；均只在本次调用中有效。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Remote_Crc16(const uint8_t *data, uint8_t len)
{
    uint16_t crc = 0xFFFFU;
    uint8_t i;
    uint8_t bit;

    for (i = 0U; i < len; i++)
    {
        crc ^= data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            if ((crc & 1U) != 0U)
            {
                crc = (uint16_t)((crc >> 1U) ^ 0xA001U);
            }
            else
            {
                crc >>= 1U;
            }
        }
    }

    return crc;
}

/**
 * @brief 手持无线业务协议：构造（对应符号 Remote_BuildFrame）。
 * @调用关系 直接调用者：remote_config.c:Remote_SendReliable。
 * @局部状态 主要临时变量：crc、i；均只在本次调用中有效。
 * @param type 按值传入；函数内部修改不会回写调用方变量。
 * @param seq 按值传入；函数内部修改不会回写调用方变量。
 * @param payload 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param payload_len 按值传入；函数内部修改不会回写调用方变量。
 * @param frame 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param frame_len 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_BuildFrame(uint8_t type,
                                 uint8_t seq,
                                 const uint8_t *payload,
                                 uint8_t payload_len,
                                 uint8_t *frame,
                                 uint8_t *frame_len)
{
    uint16_t crc;
    uint8_t i;

    if ((payload == 0) || (frame == 0) || (frame_len == 0) ||
        (payload_len == 0U) || (payload_len > APP_FRAME_MAX_PAYLOAD))
    {
        return 0U;
    }

    frame[0] = APP_FRAME_SOF;
    frame[1] = type;
    frame[2] = seq;
    frame[3] = payload_len;

    for (i = 0U; i < payload_len; i++)
    {
        frame[4U + i] = payload[i];
    }

    crc = Remote_Crc16(frame, (uint8_t)(4U + payload_len));
    frame[4U + payload_len] = (uint8_t)crc;
    frame[5U + payload_len] = (uint8_t)(crc >> 8U);
    *frame_len = (uint8_t)(APP_FRAME_OVERHEAD + payload_len);

    return 1U;
}

/**
 * @brief 手持无线业务协议：解析并校验（对应符号 Remote_ParseFrame）。
 * @调用关系 直接调用者：remote_config.c:Remote_RadioService。
 * @局部状态 主要临时变量：len、i、crc_expected、crc_actual；均只在本次调用中有效。
 * @param frame 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param frame_len 按值传入；函数内部修改不会回写调用方变量。
 * @param payload 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param payload_len 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param seq 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_ParseFrame(const uint8_t *frame,
                                 uint8_t frame_len,
                                 uint8_t *payload,
                                 uint8_t *payload_len,
                                 uint8_t *seq)
{
    uint8_t len;
    uint8_t i;
    uint16_t crc_expected;
    uint16_t crc_actual;

    if ((frame == 0) || (payload == 0) || (payload_len == 0) || (seq == 0) ||
        (frame_len < APP_FRAME_OVERHEAD) || (frame[0] != APP_FRAME_SOF))
    {
        return 0U;
    }

    len = frame[3];
    if ((len == 0U) || (len > APP_FRAME_MAX_PAYLOAD) ||
        (frame_len != (uint8_t)(APP_FRAME_OVERHEAD + len)))
    {
        return 0U;
    }

    crc_expected = (uint16_t)frame[4U + len] |
                   ((uint16_t)frame[5U + len] << 8U);
    crc_actual = Remote_Crc16(frame, (uint8_t)(4U + len));
    if ((crc_expected != crc_actual) || (frame[1] != frame[4]))
    {
        return 0U;
    }

    for (i = 0U; i < len; i++)
    {
        payload[i] = frame[4U + i];
    }

    *payload_len = len;
    *seq = frame[2];
    return 1U;
}

/**
 * @brief 手持无线业务协议：清除（对应符号 Remote_TxQueueClear）。
 * @调用关系 直接调用者：remote_config.c:Remote_TxService、remote_config.c:RemoteConfig_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_TxQueueClear(void)
{
    s_tx_queue_head = 0U;
    s_tx_queue_tail = 0U;
    s_tx_queue_count = 0U;
    s_tx_active = 0U;
    s_tx_next_start_ms = HAL_GetTick();
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_TxQueuePop）。
 * @调用关系 直接调用者：remote_config.c:Remote_TxService。
 * @param success 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_TxQueuePop(uint8_t success)
{
    if (s_tx_queue_count == 0U)
    {
        s_tx_active = 0U;
        return;
    }

    s_tx_queue_head = (uint8_t)((s_tx_queue_head + 1U) %
                                REMOTE_REPLY_QUEUE_DEPTH);
    s_tx_queue_count--;
    s_tx_active = 0U;
    s_tx_next_start_ms = HAL_GetTick() + REMOTE_REPLY_GAP_MS;

    if (success != 0U)
    {
        g_remote_tx_total++;
    }
    else
    {
        g_remote_tx_fail_total++;
    }
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_TxService）。
 * @调用关系 直接调用者：remote_config.c:RemoteConfig_Task。
 * @局部状态 主要临时变量：item、result；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_TxService(void)
{
    RemoteTxItem *item;
    NrfAsyncTxResult result;

    if (g_SlaveState != SLAVE_CONNECTED)
    {
        if ((s_tx_queue_count != 0U) || (s_tx_active != 0U))
        {
            Remote_TxQueueClear();
        }
        return;
    }

    if (s_tx_active != 0U)
    {
        result = NRF24L01_SlaveSendGetResult();
        g_remote_last_tx_result = (uint8_t)result;
        if (result == NRF_ASYNC_TX_OK)
        {
            Remote_TxQueuePop(1U);
        }
        else if (result == NRF_ASYNC_TX_FAILED)
        {
            Remote_TxQueuePop(0U);
        }
        return;
    }

    if ((s_tx_queue_count == 0U) ||
        (Remote_DeadlineReached(s_tx_next_start_ms) == 0U))
    {
        return;
    }

    item = &s_tx_queue[s_tx_queue_head];
    if (NRF24L01_SlaveSendStart(item->frame, item->len) != 0U)
    {
        s_tx_active = 1U;
        g_remote_last_tx_result = (uint8_t)NRF_ASYNC_TX_BUSY;
    }
}

/**
 * @brief 手持无线业务协议：编码并发送（对应符号 Remote_SendReliable）。
 * @调用关系 直接调用者：remote_config.c:Remote_SendParameterSnapshot、remote_config.c:Remote_SendSetParameterResponse、remote_config.c:Remote_SendStatus2、remote_config.c:Remote_SendFaultResponse、remote_config.c:Remote_SendAccountSnapshot。
 * @局部状态 主要临时变量：item、was_empty；均只在本次调用中有效。
 * @param data 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_SendReliable(uint8_t *data, uint8_t len)
{
    RemoteTxItem *item;
    uint8_t was_empty;

    if ((data == 0) || (len == 0U) ||
        (len > APP_FRAME_MAX_PAYLOAD) ||
        (s_tx_queue_count >= REMOTE_REPLY_QUEUE_DEPTH))
    {
        g_remote_tx_fail_total++;
        return 0U;
    }

    was_empty = (s_tx_queue_count == 0U) ? 1U : 0U;
    item = &s_tx_queue[s_tx_queue_tail];
    if (Remote_BuildFrame(data[0], s_current_seq, data, len,
                          item->frame, &item->len) == 0U)
    {
        g_remote_tx_fail_total++;
        return 0U;
    }

    s_tx_queue_tail = (uint8_t)((s_tx_queue_tail + 1U) %
                                REMOTE_REPLY_QUEUE_DEPTH);
    s_tx_queue_count++;

    if ((was_empty != 0U) && (s_tx_active == 0U))
    {
        s_tx_next_start_ms = HAL_GetTick() + REMOTE_REPLY_GUARD_MS;
    }

    return 1U;
}

/**
 * @brief 手持无线业务协议：编码并发送（对应符号 Remote_SendParameterSnapshot）。
 * @调用关系 直接调用者：remote_config.c:Remote_DataService。
 * @局部状态 主要临时变量：packet、part、start、count、i、packet_len、all_ok、middle_value、right_value；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_SendParameterSnapshot(void)
{
    uint8_t packet[APP_PARAM_PACKET_LEN];
    uint8_t part;
    uint8_t start;
    uint8_t count;
    uint8_t i;
    uint8_t packet_len;
    uint8_t all_ok = 1U;
    uint16_t middle_value;
    uint16_t right_value;

    for (part = 0U; part < APP_PARAM_PART_COUNT; part++)
    {
        start = (uint8_t)(part * APP_PARAMS_PER_PACKET);
        count = (uint8_t)(APP_PARAM_COUNT - start);
        if (count > APP_PARAMS_PER_PACKET)
        {
            count = APP_PARAMS_PER_PACKET;
        }

        packet[0] = APP_RSP_PARAMS;
        packet[1] = part;
        packet[2] = start;
        packet[3] = count;

        for (i = 0U; i < count; i++)
        {
            Remote_GetParamPair((uint8_t)(start + i),
                                &middle_value,
                                &right_value);
            packet[4U + i * APP_PARAM_ITEM_LEN] = (uint8_t)middle_value;
            packet[5U + i * APP_PARAM_ITEM_LEN] = (uint8_t)(middle_value >> 8U);
            packet[6U + i * APP_PARAM_ITEM_LEN] = (uint8_t)right_value;
            packet[7U + i * APP_PARAM_ITEM_LEN] = (uint8_t)(right_value >> 8U);
        }

        packet_len = (uint8_t)(4U + count * APP_PARAM_ITEM_LEN);
        if (Remote_SendReliable(packet, packet_len) == 0U)
        {
            all_ok = 0U;
        }
    }

    return all_ok;
}

/**
 * @brief 手持无线业务协议：设置（对应符号 Remote_SendSetParameterResponse）。
 * @调用关系 直接调用者：remote_config.c:Remote_DataService。
 * @局部状态 主要临时变量：response；均只在本次调用中有效。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param status 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_SendSetParameterResponse(uint8_t index, uint8_t status)
{
    uint8_t response[3];

    response[0] = APP_RSP_SET_PARAM;
    response[1] = index;
    response[2] = status;
    return Remote_SendReliable(response, (uint8_t)sizeof(response));
}

/**
 * @brief 手持无线业务协议：编码并发送（对应符号 Remote_SendStatus2）。
 * @调用关系 直接调用者：remote_config.c:Remote_DataService。
 * @局部状态 主要临时变量：response；均只在本次调用中有效。
 * @param type 按值传入；函数内部修改不会回写调用方变量。
 * @param status 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_SendStatus2(uint8_t type, uint8_t status)
{
    uint8_t response[2];

    response[0] = type;
    response[1] = status;
    return Remote_SendReliable(response, (uint8_t)sizeof(response));
}

/**
 * @brief 手持无线业务协议：编码并发送（对应符号 Remote_SendFaultResponse）。
 * @调用关系 直接调用者：remote_config.c:Remote_DataService。
 * @局部状态 主要临时变量：response；均只在本次调用中有效。
 * @param status 按值传入；函数内部修改不会回写调用方变量。
 * @param fault_code 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_SendFaultResponse(uint8_t status, uint8_t fault_code)
{
    uint8_t response[3];

    response[0] = APP_RSP_FAULT;
    response[1] = status;
    response[2] = fault_code;
    return Remote_SendReliable(response, (uint8_t)sizeof(response));
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_IsLeapYear）。
 * @调用关系 直接调用者：remote_config.c:Remote_DaysInMonth。
 * @param year 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_IsLeapYear(uint16_t year)
{
    return ((((year % 4U) == 0U) && ((year % 100U) != 0U)) ||
            ((year % 400U) == 0U)) ? 1U : 0U;
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_DaysInMonth）。
 * @调用关系 直接调用者：remote_config.c:Remote_SetTime。
 * @param year 按值传入；函数内部修改不会回写调用方变量。
 * @param month 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_DaysInMonth(uint16_t year, uint8_t month)
{
    if (month == 2U)
    {
        return (Remote_IsLeapYear(year) != 0U) ? 29U : 28U;
    }

    if ((month == 4U) || (month == 6U) ||
        (month == 9U) || (month == 11U))
    {
        return 30U;
    }

    if ((month >= 1U) && (month <= 12U))
    {
        return 31U;
    }

    return 0U;
}
/**
 * @brief 手持无线业务协议：设置（对应符号 Remote_SetTime）。
 * @调用关系 直接调用者：remote_config.c:Remote_DataService。
 * @局部状态 主要临时变量：remote_time、rtc_time、account_time、max_day；均只在本次调用中有效。
 * @param data 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param len 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_SetTime(const uint8_t *data, uint8_t len)
{
    RemoteTimeValue remote_time;
    Pcf8563_Time rtc_time;
    Account_RtcTime account_time;
    uint8_t max_day;

    if ((data == 0) || (len != APP_TIME_PAYLOAD_LEN))
    {
        return 0U;
    }

    remote_time.year = (uint16_t)data[1] | ((uint16_t)data[2] << 8U);
    remote_time.month = data[3];
    remote_time.day = data[4];
    remote_time.hour = data[5];
    remote_time.minute = data[6];
    remote_time.second = data[7];

    if ((remote_time.year < 2000U) || (remote_time.year > 2099U) ||
        (remote_time.month < 1U) || (remote_time.month > 12U) ||
        (remote_time.hour > 23U) || (remote_time.minute > 59U) ||
        (remote_time.second > 59U))
    {
        return 0U;
    }

    max_day = Remote_DaysInMonth(remote_time.year, remote_time.month);
    if ((remote_time.day < 1U) || (remote_time.day > max_day))
    {
        return 0U;
    }

    rtc_time.year = remote_time.year;
    rtc_time.month = remote_time.month;
    rtc_time.day = remote_time.day;
    rtc_time.weekday = 0U;
    rtc_time.hour = remote_time.hour;
    rtc_time.minute = remote_time.minute;
    rtc_time.second = remote_time.second;
    rtc_time.voltage_low = 0U;

    if (Pcf8563_SetTime(&rtc_time) == 0U)
    {
        return 0U;
    }

    account_time.year = remote_time.year;
    account_time.month = remote_time.month;
    account_time.day = remote_time.day;
    account_time.hour = remote_time.hour;
    account_time.minute = remote_time.minute;
    account_time.second = remote_time.second;
    account_time.valid = 1U;
    Account_SetRtcTime(&account_time);

    g_remote_last_time_year = remote_time.year;
    g_remote_last_time_month = remote_time.month;
    g_remote_last_time_day = remote_time.day;
    g_remote_last_time_hour = remote_time.hour;
    g_remote_last_time_minute = remote_time.minute;
    g_remote_last_time_second = remote_time.second;
    g_remote_time_set_total++;
    return 1U;
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_FillAccountValues）。
 * @调用关系 直接调用者：remote_config.c:Remote_DisplayTask、remote_config.c:Remote_SendAccountSnapshot。
 * @局部状态 主要临时变量：account、total_award、payout_rate；均只在本次调用中有效。
 * @param values 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_FillAccountValues(uint32_t *values)
{
    Account_Data account;
    uint64_t payout_rate;
    int64_t difference;

    Account_GetData(&account);
    difference = (int64_t)account.coin_in_total -
                 (int64_t)account.payout_total;
    if (Account_GetStakeSinceEpoch(&account) == 0U)
    {
        payout_rate = 0U;
    }
    else
    {
        payout_rate = Account_CalcPayoutRatePercent(&account);
    }

    values[0] = account.coin_in_total;
    values[1] = account.payout_total;
    values[2] = (uint32_t)(int32_t)difference;
    values[3] = account.ticket_out_total;
    values[4] = account.line_ticket_total;
    values[5] = account.game_round_total;
    values[6] = account.card_out_total;
    values[7] = account.lottery_count_total;
    values[8] = account.lottery_payout_total;
    values[9] = (payout_rate > 99999999ULL) ? 99999999UL : (uint32_t)payout_rate;
    /* C11时间由SET_TIME单独处理；index10~13依次对应C12~C15。 */
    values[10] = Jackpot_GetLocalPayoutTotal();
    values[11] = Jackpot_GetCurrent();
    values[12] = account.gift_light_total;
    values[13] = account.gift_win_value_total;
}

/**
 * @brief 手持无线业务协议：编码并发送（对应符号 Remote_SendAccountSnapshot）。
 * @调用关系 直接调用者：remote_config.c:Remote_DataService。
 * @局部状态 主要临时变量：packet、values、part、start、count、i、pos、packet_len、all_ok、value；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_SendAccountSnapshot(void)
{
    uint8_t packet[24];
    uint32_t values[APP_ACCOUNT_COUNT];
    uint8_t part;
    uint8_t start;
    uint8_t count;
    uint8_t i;
    uint8_t pos;
    uint8_t packet_len;
    uint8_t all_ok = 1U;
    uint32_t value;

    Remote_FillAccountValues(values);

    for (part = 0U; part < APP_ACCOUNT_PART_COUNT; part++)
    {
        start = (uint8_t)(part * APP_ACCOUNT_PER_PACKET);
        count = (uint8_t)(APP_ACCOUNT_COUNT - start);
        if (count > APP_ACCOUNT_PER_PACKET)
        {
            count = APP_ACCOUNT_PER_PACKET;
        }

        packet[0] = APP_RSP_ACCOUNT;
        packet[1] = part;
        packet[2] = start;
        packet[3] = count;

        for (i = 0U; i < count; i++)
        {
            value = values[start + i];
            pos = (uint8_t)(4U + i * 4U);
            packet[pos] = (uint8_t)value;
            packet[pos + 1U] = (uint8_t)(value >> 8U);
            packet[pos + 2U] = (uint8_t)(value >> 16U);
            packet[pos + 3U] = (uint8_t)(value >> 24U);
        }

        packet_len = (uint8_t)(4U + count * 4U);
        if (Remote_SendReliable(packet, packet_len) == 0U)
        {
            all_ok = 0U;
        }
    }

    return all_ok;
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_RadioService）。
 * @调用关系 直接调用者：remote_config.c:RemoteConfig_Task。
 * @局部状态 主要临时变量：raw_buf、raw_len、seq；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_RadioService(void)
{
    uint8_t raw_buf[PACKET_SIZE];
    uint8_t raw_len = 0U;
    uint8_t seq = 0U;

    NRF24L01_Service();

    if (g_SlaveState != SLAVE_CONNECTED)
    {
        s_selected_flag = 0U;
        g_remote_selected_flag = 0U;
        return;
    }

    if (s_command_pending != 0U)
    {
        return;
    }

    if (NRF24L01_SlaveRecv(raw_buf, sizeof(raw_buf), &raw_len) != 0U)
    {
        s_rx_len = 0U;
        if (Remote_ParseFrame(raw_buf, raw_len, s_rx_buf, &s_rx_len, &seq) == 0U)
        {
            g_remote_unknown_command_total++;
            return;
        }

        s_current_seq = seq;
        g_remote_rx_total++;
        g_remote_last_command = (s_rx_len > 0U) ? s_rx_buf[0] : 0U;
        s_command_pending = 1U;
    }
}

/**
 * @brief 手持无线业务协议：设置（对应符号 Remote_MapSetStatus）。
 * @调用关系 直接调用者：remote_config.c:Remote_DataService。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @param middle_value 按值传入；函数内部修改不会回写调用方变量。
 * @param right_value 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Remote_MapSetStatus(uint8_t index,
                                   uint16_t middle_value,
                                   uint16_t right_value)
{
    uint8_t param_index;

    if (index >= APP_PARAM_COUNT)
    {
        return APP_STATUS_BAD_PARAM;
    }

    param_index = s_remote_param_to_setting[index];
    if (Remote_ParamPairAllowed(param_index, middle_value, right_value) == 0U)
    {
        return APP_STATUS_BAD_VALUE;
    }

    if (((param_index == SETTING_PARAM_JACKPOT_BASE) ||
         (param_index == SETTING_PARAM_JACKPOT_COINS_PER_POINT)) &&
        (Jackpot_IsBaseEditable() == 0U))
    {
        return APP_STATUS_FAILED;
    }

    if (param_index == SETTING_PARAM_VOLUME)
    {
        Setting_SetVolumePair(middle_value, right_value);
    }
    else if (param_index == SETTING_PARAM_CARD_RATIO)
    {
        Setting_SetCardRatioPair(middle_value, right_value);
    }
    else
    {
        Setting_SetValue(param_index, right_value);
    }

    /*
     * P8 只在这里保存“清账请求”。Game 主循环会等所有抽奖、吐币和吐卡
     * 完全空闲后，统一执行“换新池 -> 清账 -> P8回0”，避免远程清账切断
     * 正在落盘的奖项事务。
     */
    if (Setting_IsReady() == 0U)
    {
        return APP_STATUS_FAILED;
    }

    g_remote_param_write_total++;
    return APP_STATUS_OK;
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 Remote_DataService）。
 * @调用关系 直接调用者：remote_config.c:RemoteConfig_Task。
 * @局部状态 主要临时变量：index、status、middle_value、right_value、fault_code；均只在本次调用中有效。
 * @param command_enable 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Remote_DataService(uint8_t command_enable)
{
    uint8_t index;
    uint8_t status;
    uint16_t middle_value;
    uint16_t right_value;
    uint8_t fault_code;

    if (s_command_pending == 0U)
    {
        return;
    }

    s_command_pending = 0U;
    g_remote_last_response_status = APP_STATUS_OK;

    if (s_rx_len == 0U)
    {
        return;
    }

    /* Exit is allowed even while busy/locked: release the UI, not the lock/game.
     * Repeating the same request is harmless, so an ACK loss can be retried. */
    if (s_rx_buf[0] == APP_CMD_EXIT)
    {
        if (s_rx_len != 1U)
        {
            (void)Remote_SendStatus2(APP_RSP_EXIT, APP_STATUS_BAD_PARAM);
            return;
        }
        s_selected_flag = 0U;
        g_remote_selected_flag = 0U;
        s_display_type = REMOTE_DISPLAY_NONE;
        s_account_page_request = 0U;
        s_exit_request = 1U;
        (void)Remote_SendStatus2(APP_RSP_EXIT, APP_STATUS_OK);
        return;
    }

    if (s_rx_buf[0] == APP_CMD_HEARTBEAT)
    {
        if (s_rx_len != 2U)
        {
            g_remote_unknown_command_total++;
            return;
        }

        s_selected_flag = (s_rx_buf[1] != 0U) ? 1U : 0U;
        g_remote_selected_flag = s_selected_flag;
        return;
    }

    if ((command_enable == 0U) &&
        ((s_rx_buf[0] == APP_CMD_SET_PARAM) ||
         (s_rx_buf[0] == APP_CMD_SET_TIME)))
    {
        g_remote_last_response_status = APP_STATUS_FAILED;
        if ((s_rx_buf[0] == APP_CMD_SET_PARAM) && (s_rx_len >= 2U))
        {
            (void)Remote_SendSetParameterResponse(s_rx_buf[1],
                                                  APP_STATUS_FAILED);
        }
        else if (s_rx_buf[0] == APP_CMD_SET_TIME)
        {
            (void)Remote_SendStatus2(APP_RSP_SET_TIME,
                                     APP_STATUS_FAILED);
        }
        return;
    }

    if (s_rx_buf[0] == APP_CMD_GET_PARAMS)
    {
        if (s_rx_len != 1U)
        {
            g_remote_unknown_command_total++;
            return;
        }

        s_selected_flag = 1U;
        g_remote_selected_flag = 1U;
        Remote_ShowParam(0U);
        if (Remote_SendParameterSnapshot() == 0U)
        {
            g_remote_last_response_status = APP_STATUS_FAILED;
        }
        return;
    }

    if (s_rx_buf[0] == APP_CMD_SET_PARAM)
    {
        s_selected_flag = 1U;
        g_remote_selected_flag = 1U;

        if (s_rx_len != APP_SET_PARAM_PAYLOAD_LEN)
        {
            g_remote_unknown_command_total++;
            if (s_rx_len >= 2U)
            {
                (void)Remote_SendSetParameterResponse(s_rx_buf[1], APP_STATUS_BAD_PARAM);
            }
            return;
        }

        index = s_rx_buf[1];
        middle_value = (uint16_t)s_rx_buf[2] |
                       ((uint16_t)s_rx_buf[3] << 8U);
        right_value = (uint16_t)s_rx_buf[4] |
                      ((uint16_t)s_rx_buf[5] << 8U);

        if ((s_last_set_seq_valid != 0U) &&
            (s_current_seq == s_last_set_seq) &&
            (index == s_last_set_index) &&
            (middle_value == s_last_set_middle_value) &&
            (right_value == s_last_set_right_value))
        {
            Remote_ShowParam(index);
            (void)Remote_SendSetParameterResponse(index, s_last_set_status);
            return;
        }

        status = Remote_MapSetStatus(index, middle_value, right_value);
        s_last_set_seq_valid = 1U;
        s_last_set_seq = s_current_seq;
        s_last_set_index = index;
        s_last_set_status = status;
        s_last_set_middle_value = middle_value;
        s_last_set_right_value = right_value;
        g_remote_last_response_status = status;
        Remote_ShowParam(index);
        (void)Remote_SendSetParameterResponse(index, status);
        return;
    }

    if (s_rx_buf[0] == APP_CMD_GET_FAULT)
    {
        if (s_rx_len != 1U)
        {
            g_remote_unknown_command_total++;
            return;
        }

        s_selected_flag = 1U;
        g_remote_selected_flag = 1U;
        fault_code = (uint8_t)Fault_GetCode();
        g_remote_last_fault_code = fault_code;
        (void)Remote_SendFaultResponse(APP_STATUS_OK, fault_code);
        return;
    }

    if (s_rx_buf[0] == APP_CMD_SET_TIME)
    {
        s_selected_flag = 1U;
        g_remote_selected_flag = 1U;
        status = (Remote_SetTime(s_rx_buf, s_rx_len) != 0U) ?
                 APP_STATUS_OK : APP_STATUS_BAD_VALUE;
        g_remote_last_response_status = status;
        /* 时钟不再占用本机C页；SET_TIME只更新时间，不强制跳转到C13彩金页。 */
        (void)Remote_SendStatus2(APP_RSP_SET_TIME, status);
        return;
    }

    if (s_rx_buf[0] == APP_CMD_CLEAR_FAULT)
    {
        if (s_rx_len != 1U)
        {
            g_remote_unknown_command_total++;
            return;
        }

        s_selected_flag = 1U;
        g_remote_selected_flag = 1U;

        if ((s_last_clear_seq_valid != 0U) &&
            (s_current_seq == s_last_clear_seq))
        {
            (void)Remote_SendStatus2(APP_RSP_CLEAR_FAULT, s_last_clear_status);
            return;
        }

        /*
         * 不在协议层直接清Fault。E16需要重置Game层15秒等待计时，
         * E14也需要先清Ticket故障；统一交给Game_Task按本机清除键同一路径处理。
         */
        if (Fault_IsActive() != 0U)
        {
            s_fault_clear_request = 1U;
        }
        status = APP_STATUS_OK;
        s_last_clear_seq_valid = 1U;
        s_last_clear_seq = s_current_seq;
        s_last_clear_status = status;
        g_remote_fault_clear_total++;
        g_remote_last_response_status = status;
        (void)Remote_SendStatus2(APP_RSP_CLEAR_FAULT, status);
        return;
    }

    if (s_rx_buf[0] == APP_CMD_GET_ACCOUNT)
    {
        if (s_rx_len != 1U)
        {
            g_remote_unknown_command_total++;
            return;
        }

        s_selected_flag = 1U;
        g_remote_selected_flag = 1U;
        Remote_ShowAccount(0U);
        if (Remote_SendAccountSnapshot() == 0U)
        {
            g_remote_last_response_status = APP_STATUS_FAILED;
        }
        return;
    }

    g_remote_unknown_command_total++;
}

/**
 * @brief 手持无线业务协议：初始化（对应符号 RemoteConfig_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void RemoteConfig_Init(void)
{
    s_exit_request = 0U;
    s_account_page_request = 0U;
    s_fault_clear_request = 0U;
    Remote_TxQueueClear();
    SPI1_Init();
    NRF24L01_Init(MODE_SLAVE);
}

/**
 * @brief 手持无线业务协议：周期处理（对应符号 RemoteConfig_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param command_enable 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void RemoteConfig_Task(uint8_t command_enable)
{
    Remote_RadioService();
    Remote_TxService();
    Remote_DataService(command_enable);
    Remote_RadioService();
    Remote_TxService();
    RemoteConfig_StatusLedTask();
    if (command_enable != 0U)
    {
        Remote_DisplayTask();
    }
}

/**
 * @brief 手持无线业务协议：读取并清除一次性事件（对应符号 RemoteConfig_TakeExitRequest）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：requested；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t RemoteConfig_TakeExitRequest(void)
{
    uint8_t requested = s_exit_request;
    s_exit_request = 0U;
    return requested;
}

/**
 * @brief 手持无线业务协议：执行内部处理（对应符号 RemoteConfig_IsSelected）。
 * @调用关系 直接调用者：未发现普通 C 直接调用；可能由启动文件、中断向量、回调表或外部模块入口调用。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t RemoteConfig_IsSelected(void)
{
    return s_selected_flag;
}

/**
 * @brief 手持无线业务协议：读取并清除一次性事件（对应符号 RemoteConfig_TakeAccountPageRequest）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：page_number；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t RemoteConfig_TakeAccountPageRequest(void)
{
    uint8_t page_number;

    page_number = s_account_page_request;
    s_account_page_request = 0U;
    return page_number;
}

/**
 * @brief 手持无线业务协议：读取并清除一次性事件（对应符号 RemoteConfig_TakeFaultClearRequest）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：requested；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t RemoteConfig_TakeFaultClearRequest(void)
{
    uint8_t requested;

    requested = s_fault_clear_request;
    s_fault_clear_request = 0U;
    return requested;
}
