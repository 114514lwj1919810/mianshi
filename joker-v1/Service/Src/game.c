/* AUTO-FILE-DOC
 * 文件职责：游戏主状态机。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "game.h"

#include "account.h"
#include "award_lamp.h"
#include "audio_manager.h"
#include "coin.h"
#include "eye.h"
#include "fault.h"
#include "gift_light.h"
#include "gift_algorithm.h"
#include "gtim.h"
#include "key.h"
#include "jackpot.h"
#include "jackpot_display.h"
#include "meter.h"
#include "lottery.h"
#include "machine_address.h"
#include "./MODBUS/modbus.h"
#include "machine_lock.h"
#include "product_variant.h"
#include "motor.h"
#include "payout.h"
#include "pcf8563.h"
#include "segment.h"
#include "score.h"
#include "remote_config.h"
#include "self_test.h"
#ifdef BOOT_APP_BASE
#include "boot_contract.h"
#endif
#include "setting.h"
#include "shake.h"
#include "solenoid.h"
#include "standby.h"
#include "ticket.h"
#include "upgrade.h"
#include "wiper_motor.h"

#define GAME_LUCKY_CHANNEL_MASK       ((uint16_t)((1U << EYE_CHANNEL_1) | (1U << EYE_CHANNEL_9)))
#define GAME_SCORE_CHANNEL_MASK       SCORE_MIDDLE_EYE_MASK
#define GAME_SOLENOID_PULSE_MS        500U
#define GAME_DROP_CHANNEL_JAM_MS      15000U
#define GAME_TICKET_SHOW_HOLD_MS      1200U
#define GAME_TICKET_CLEAR_HOLD_MS     5000U
#define GAME_CREDIT_TRANSFER_STEP_MS   250U
#define GAME_LINE7_AUTO_PAYOUT_MS  60000U
#define GAME_JACKPOT_REPLY_TIMEOUT_MS 10000U
#define GAME_FAIL_BLINK_TOTAL_MS       800U
#define GAME_FAIL_BLINK_STEP_MS       200U
#define GAME_MENU_BLINK_MS            500U
#define GAME_SETTING_ACTION_ACK_MS   3000U
#define GAME_E14_BLINK_PERIOD_MS     2000U
#define GAME_E14_VISIBLE_MS          1000U
#define GAME_SETTING_ACTION_ACK_NONE 0xFFU
#define GAME_SETTING_VALUE_MAX        9999U
#define GAME_SETTING_PAGE_COUNT        17U
#define GAME_ACCOUNT_PAGE_COUNT        56U  /* C51~56为预算及一次性发奖审计；原页不移位。 */
/* 日期页已经从查账菜单删除；保留RTC日期只用于芯片正常走时。 */
#define GAME_ACCOUNT_DATE_PAGE_INDEX   0xFFU
/* C13 已改为彩金出奖分，旧时钟页不能再占用它。 */
#define GAME_ACCOUNT_CLOCK_PAGE_INDEX  0xFFU
#define GAME_ACCOUNT_TIME_READ_MS      1000U
#define GAME_RTC_UART_DEBUG_ENABLE     0U
#define GAME_RTC_UART_PRINT_MS         1000U
#define GAME_RTC_BACKUP_CHECK_MS       1000U
#define GAME_ACCOUNT_TIME_FIELD_COUNT  3U
#define GAME_RTC_DEFAULT_YEAR          2026U
#define GAME_RTC_DEFAULT_MONTH         7U
#define GAME_RTC_DEFAULT_DAY           7U
#define GAME_RTC_DEFAULT_HOUR          10U
#define GAME_RTC_DEFAULT_MINUTE        30U
#define GAME_RTC_DEFAULT_SECOND        0U

#define GAME_AUDIO_BACK_IDLE           1U
#define GAME_AUDIO_BACK_LINE7       2U

#define GAME_AUDIO_TRIG_EXCHANGE       1U
#define GAME_AUDIO_TRIG_DROP           2U
#define GAME_AUDIO_TRIG_HIT_CHANNEL    3U
#define GAME_AUDIO_TRIG_COIN_IN        4U
#define GAME_AUDIO_TRIG_ENTER_SETTING  5U
#define GAME_AUDIO_TRIG_FAIL           6U
#define GAME_AUDIO_TRIG_NO_COIN        10U
#define GAME_AUDIO_TRIG_LINE_WIN       11U
#define GAME_AUDIO_TRIG_LINE5          17U
#define GAME_AUDIO_TRIG_LINE6          18U
#define GAME_AUDIO_TRIG_LINE7          19U
#define GAME_AUDIO_TRIG_JACKPOT_QUARTER 25U
#define GAME_AUDIO_TRIG_JACKPOT_HALF    26U
#define GAME_AUDIO_TRIG_JACKPOT_ALL     27U

typedef enum
{
    GAME_FAIL_SEQUENCE_NONE = 0U,
    GAME_FAIL_SEQUENCE_BLINK,
    GAME_FAIL_SEQUENCE_SOLENOID
} Game_FailSequence;

typedef enum
{
    GAME_JACKPOT_SETTLEMENT_NONE = 0U,
    GAME_JACKPOT_SETTLEMENT_BASE_PAYOUT,
    GAME_JACKPOT_SETTLEMENT_AWARD_WAIT,
    GAME_JACKPOT_SETTLEMENT_BONUS_PAYOUT
} Game_JackpotSettlementPhase;

static volatile Game_Mode s_game_mode = GAME_MODE_POST;
/*
 * 主状态与故障恢复：
 * s_game_mode 是 Game_Task 每轮 switch 的唯一主状态；进入故障前把原状态和
 * 各执行器运行情况保存到 s_fault_*，清故障后才能按原业务继续或安全回待机。
 */
static volatile Game_Mode s_fault_resume_mode = GAME_MODE_IDLE;
static volatile uint8_t s_fault_game_motor_was_running = 0U;
static volatile uint8_t s_fault_payout_motor_was_running = 0U;
static volatile uint8_t s_fault_ticket_was_running = 0U;
static volatile uint8_t s_fault_wiper_was_running = 0U;
static volatile uint16_t s_game_score = 0U;
static volatile uint16_t s_game_prize = 0U;
static volatile uint16_t s_game_coin = 0U;
static volatile uint16_t s_coins_in_channel_mask = 0U;
static volatile uint32_t s_state_start_ms = 0U;
#ifdef BOOT_APP_BASE
static uint8_t s_upgrade_post_checked = 0U;
#endif

/*
 * 奖励事务必须跨循环保存，不能用局部变量：
 * line_payout_pending 是尚未并入出票/吐卡的线奖；
 * ticket/card *_remaining 是断电恢复与分批确认所依据的欠付量；
 * line7_* 和 jackpot_settlement_phase 防止彩金请求、扣款、下发被重复执行。
 */
static volatile uint16_t s_line_payout_pending = 0U;
static volatile uint32_t s_ticket_display_remaining = 0U;
/* 出票时按住故障清除键会立即暂停；持续满 5 秒才把剩余票作消票处理。 */
static volatile uint32_t s_ticket_clear_hold_start_ms = 0U;
static volatile uint8_t s_ticket_clear_hold_active = 0U;
static volatile uint8_t s_ticket_clear_wait_release = 0U;
static uint32_t s_credit_transfer_ms;

static void Game_EnterIdleOrReady(void);

/* P14=0只改变展示和流向，不驱动出奖设备。中屏和右屏保存于同一账目记录。 */
static uint8_t Game_BeginCreditTransfer(uint16_t count)
{
    if ((s_game_prize != 0U) || (Payout_IsBusy() != 0U) ||
        (Ticket_IsBusy() != 0U) ||
        ((uint32_t)s_game_coin + count > 9999U))
    {
        return 0U;
    }
    Account_SetCreditTransfer(1U);
    Game_SetPrize(count);
    s_credit_transfer_ms = Gtim_NowMs();
    return 1U;
}

static void Game_UpdateCreditTransfer(void)
{
    /* 掉电恢复或结算边界可能留下空的转移标志，不能把0减成65535。 */
    if ((Account_IsCreditTransfer() != 0U) && (s_game_prize == 0U))
    {
        Account_SetCreditTransfer(0U);
        return;
    }
    if ((Account_IsCreditTransfer() == 0U) ||
        /*
         * 中屏转右屏是后台记分动画，开始下一局后仍应继续。
         * 只在会占用数码管的自检/待机/菜单/故障页面，以及实际
         * 出奖保持页面暂停；START/ONLINE/OVER/彩金结算均不暂停。
         */
        (s_game_mode == GAME_MODE_POST) ||
        (s_game_mode == GAME_MODE_STANDBY) ||
        (s_game_mode == GAME_MODE_SETTING) ||
        (s_game_mode == GAME_MODE_ACCOUNT) ||
        (s_game_mode == GAME_MODE_FAULT) ||
        (s_game_mode == GAME_MODE_TICKET_HOLD) ||
        (s_game_mode == GAME_MODE_TICKET_SKIP_HOLD) ||
        (Gtim_IsElapsed(s_credit_transfer_ms,
                        GAME_CREDIT_TRANSFER_STEP_MS) == 0U))
    {
        return;
    }
    /* 投币可能在转移期间把右屏补满；保留中屏余额，等玩家消耗出空位再继续。 */
    if (s_game_coin >= 9999U)
    {
        return;
    }
    s_credit_transfer_ms = Gtim_NowMs();
    /* 固定每250ms转移1个：中屏逐个减少、右屏逐个增加，不按大奖大小加速。 */
    Game_SetPrize((uint16_t)(s_game_prize - 1U));
    Game_SetCoin((uint16_t)(s_game_coin + 1U));
    /* 奖励在开奖时已计C2；转屏只是余额位置变化。C1只按下一次实际开始押分增加。 */
    /* 循环只移动玩家余额；C1在下一次按开始时记押分，C2已在中奖时记奖。 */
    if (s_game_prize == 0U)
    {
        Account_SetCreditTransfer(0U);
        /*
         * 空闲时转完要进入READY；若玩家已经启动下一局，则必须
         * 保持START等当前状态，不能在电机运行中强行切回READY。
         */
        if ((s_game_mode == GAME_MODE_IDLE) ||
            (s_game_mode == GAME_MODE_READY))
        {
            Game_EnterIdleOrReady();
        }
    }
}
static volatile uint32_t s_card_payout_remaining = 0U;
static volatile uint16_t s_card_payout_x = 0U;
static volatile uint16_t s_card_payout_y = 0U;
static volatile uint8_t s_card_payout_requested = 0U;
/* P14=0兑奖右屏游戏币时置1，避免把已经记过的连线奖再次计入C账。 */
static volatile uint8_t s_redeeming_credits = 0U;
/* 使用业务队列而不是只看四位显示，避免欠票超过9999或卡已换算后漏计。
 * 左屏由算法按本次实际灯掩码另加；这里绝不改账/发奖/启动电机。 */
uint64_t Game_GetPendingReturnValueScaled(void)
{
    uint8_t coin = Setting_IsCoinPayoutEnabled();
    uint8_t ticket = Setting_IsTicketPayoutEnabled();
    uint8_t card = Setting_IsCardPayoutEnabled();
    uint8_t multiplier = (uint8_t)((coin != 0U) + (ticket != 0U) + (card != 0U));
    uint32_t remaining;
    uint16_t ratio;
    uint16_t percent = Setting_GetValue(SETTING_PARAM_LOTTERY_PAYOUT_PERCENT);
    uint64_t reserved;
    if (multiplier == 0U) multiplier = 1U;
    if (percent > 100U) percent = 100U;
    reserved = (uint64_t)s_game_coin * multiplier * ACCOUNT_PAYOUT_VALUE_SCALE;
    if (Account_IsCreditTransfer() != 0U)
    {
        /* 循环时还将同时增加C1/C2；预留其(100-P3)%差额，转屏不凭空产生预算。 */
        return reserved + (uint64_t)s_game_prize *
            ((uint64_t)multiplier * ACCOUNT_PAYOUT_VALUE_SCALE +
             (100U - percent) * (ACCOUNT_PAYOUT_VALUE_SCALE / 100U));
    }
    if (coin != 0U)
    {
        remaining = (uint32_t)Payout_GetRemaining() + s_line_payout_pending;
        if (remaining < s_game_prize) remaining = s_game_prize;
        reserved += (uint64_t)remaining * ACCOUNT_PAYOUT_VALUE_SCALE;
    }
    if (ticket != 0U)
    {
        remaining = Ticket_GetRemaining();
        if (remaining < s_ticket_display_remaining) remaining = s_ticket_display_remaining;
        if ((coin == 0U) && (remaining < s_game_prize)) remaining = s_game_prize;
        ratio = Setting_GetTicketsPerCoin();
        if (ratio == 0U) ratio = 1U;
        reserved += ((uint64_t)remaining * ACCOUNT_PAYOUT_VALUE_SCALE + ratio - 1U) / ratio;
    }
    if (card != 0U)
    {
        reserved += (uint64_t)s_game_prize * ACCOUNT_PAYOUT_VALUE_SCALE;
        if (s_card_payout_y != 0U)
            reserved += ((uint64_t)s_card_payout_remaining * s_card_payout_x *
                         ACCOUNT_PAYOUT_VALUE_SCALE + s_card_payout_y - 1U) / s_card_payout_y;
    }
    return reserved;
}
static volatile uint32_t s_line_payout_hold_start_ms = 0U;
static volatile uint8_t s_line7_lock_active = 0U;
static volatile uint8_t s_line7_settlement_started = 0U;
static volatile uint32_t s_line7_hold_start_ms = 0U;
static volatile uint16_t s_fail_eye_blink_mask = 0U;
static volatile uint8_t s_fail_eye_blink_visible = 0U;
static volatile uint32_t s_fail_eye_blink_start_ms = 0U;
static volatile uint32_t s_fail_eye_blink_last_ms = 0U;
static volatile uint8_t s_drop_result_waiting = 0U;
static volatile uint32_t s_drop_result_wait_start_ms = 0U;
static volatile uint16_t s_drop_start_active_mask = 0U;

/* 设置/查账页光标与闪烁状态。远程退出只调用正常离开菜单路径，不改游戏事务。 */
static volatile uint8_t s_setting_page_index = 0U;
static volatile uint8_t s_setting_value_selected = 0U;
/*
 * P6/P7 是一次性命令：后台执行完成后，Setting 会立即把保存值恢复为 0。
 * 为避免操作人员只看见“1”闪一下，下面两个变量只负责保留 3 秒确认显示；
 * 它们不参与清账/恢复默认的执行判断，因此不会造成命令重复执行。
 */
static volatile uint8_t s_setting_action_ack_param = GAME_SETTING_ACTION_ACK_NONE;
static volatile uint32_t s_setting_action_ack_start_ms = 0U;
static volatile uint8_t s_account_page_index = 0U;
static Pcf8563_Time s_account_time_cache = {0U};
static uint8_t s_account_time_valid = 0U;
static uint32_t s_account_time_last_read_ms = 0U;
static volatile uint8_t s_account_time_edit_active = 0U;
static volatile uint8_t s_account_time_field_index = 0U;
static volatile uint16_t s_coin_input_remainder = 0U;
static uint8_t s_audio_back_index = 0U;
static uint8_t s_e15_background_stopped = 0U;
static uint8_t s_e14_blink_active = 0U;
static uint32_t s_e14_blink_start_ms = 0U;
static uint8_t s_award_lamp_payout_started = 0U;
static uint8_t s_lottery_award_output_active = 0U;
static Game_FailSequence s_fail_sequence = GAME_FAIL_SEQUENCE_NONE;
static uint8_t s_round_has_gift_light = 0U;
static Game_JackpotSettlementPhase s_jackpot_settlement_phase =
    GAME_JACKPOT_SETTLEMENT_NONE;
static uint8_t s_jackpot_tier_pending = 0U;
static uint8_t s_jackpot_round_had_gift = 0U;
static uint8_t s_jackpot_recovery_tier = 0U;
/* Hold conversion/settlement until the balance + phase checkpoint is saved. */
static uint8_t s_settlement_checkpoint_pending;
#if (GAME_RTC_UART_DEBUG_ENABLE != 0U)
static uint32_t s_rtc_uart_last_print_ms = 0U;
#endif
static uint32_t s_rtc_backup_last_check_ms = 0U;
static uint32_t s_rtc_backup_last_minute_stamp = 0xFFFFFFFFUL;

/*
 * 现场P页只显示产品说明书列出的项目。右侧值是原EEPROM参数下标，
 * 因而隐藏页面后不会改变已有参数的存储位置，也不会破坏旧机升级数据。
 */
static const uint8_t s_setting_page_to_param[GAME_SETTING_PAGE_COUNT] =
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
    SETTING_PARAM_JACKPOT_COINS_PER_POINT,
    SETTING_PARAM_RS485_ADDRESS,
    SETTING_PARAM_IMMEDIATE_AWARD,
    SETTING_PARAM_RECOVERY_DROP,
    SETTING_PARAM_MAJOR_DRAW_COINS,
    SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL
};

/* 仅调整显示顺序，不改变旧账字段或EEPROM地址。值为旧C页的0基下标；
 * 48/49/50是新增的3/5/10币“实际入奖分”，不是算法选奖次数×金额。 */
static const uint8_t s_account_page_to_legacy[GAME_ACCOUNT_PAGE_COUNT] =
{
    0U, 1U, 2U, 3U, 4U,             /* C1~5 实体投/吐、总押/总中、实际差 */
    11U, 42U, 6U, 10U,              /* C6~9 返奖率、精细率、连线分、老虎机币奖总分 */
    48U, 49U, 50U, 13U, 16U,        /* C10~14 3/5/10分、彩金分、送灯大奖分 */
    5U, 8U, 18U, 19U, 17U, 7U,     /* C15~20 票/卡、承诺、兑现值、扣币、局数 */
    20U, 21U, 22U,                  /* C21~23 A-B、普通预算、大奖池 */
    23U, 24U, 25U, 26U, 27U, 28U, 29U, 30U, 31U, 32U,
    33U, 34U, 35U, 36U, 37U, 38U, 39U, 40U, 41U, /* C24~42 原诊断 */
    14U, 15U, 9U, 43U, 44U, 47U,   /* C43~48 当前彩金、送灯、抽奖、孤立、混合、版本 */
    45U, 46U,                      /* C49~50 旧押分/中奖历史 */
    51U, 52U, 53U, 54U, 55U, 56U    /* C51~56 发奖审计 */
};

static uint32_t Game_CalcTicketPulseCount(uint16_t prize_count);
static uint8_t Game_StartCardPayout(void);
static void Game_HandleTicketKey(void);
static uint8_t Game_TransferScoreToCredits(void);
static uint8_t Game_RedeemCreditsToAward(void);
static uint8_t Game_AddLotteryPrizeAndPayout(uint16_t prize_count);
static uint8_t Game_AddLinePrizeAndHold(uint16_t line_prize,
                                        uint16_t jackpot_prize);
static uint8_t Game_BeginLineSettlement(void);
static uint8_t Game_CompleteLineSettlement(uint16_t line_prize,
                                           uint16_t jackpot_prize);
static void Game_UpdateJackpotSettlement(void);
static uint8_t Game_AbortOrphanJackpotWait(void);
static uint8_t Game_TrySkipTicketOutput(void);
static void Game_UpdateTicketClearHold(void);
static void Game_StartJackpotLinePresentation(uint16_t line_prize);
static void Game_StopJackpotLinePresentation(void);
static void Game_FinishLineRound(void);
static void Game_StartFailEyeBlink(uint16_t eye_mask);
static void Game_UpdateE14Display(void);
static void Game_UpdateFailEyeBlink(void);
static void Game_PlayJackpotAwardAudio(uint8_t tier);
static void Game_AudioSetBack(uint8_t index);
static void Game_AudioUpdateBack(void);
static void Game_EnterIdleOrReady(void);
static void Game_ProcessCoinCount(uint32_t coin_count);
static void Game_ResetDropResultWait(void);
static void Game_StartLine7Settlement(void);
static void Game_UpdateLine7Settlement(void);
static uint8_t Game_MenuBlinkVisible(void);
static void Game_DiscardMenuPlayInputs(void);
static void Game_RestoreGameDisplay(void);
static void Game_RestoreOutputsAfterFault(void);
static void Game_EnterSettingMode(void);
static void Game_EnterAccountMode(void);
static void Game_EnterAccountPage(uint8_t page_number);
static void Game_LeaveMenuMode(void);
static void Game_ShowValueOnMiddleRight(uint32_t value);
static void Game_ShowActualCoinDifference(void);
static uint8_t Game_GetSettingParamIndex(void);
/**
 * @brief 读取归并后的查账页数值，C48 返回 MMDDNNNN 固件版本。
 * @param page_index C1~C50对应的0基下标。
 * @retval 数字页数值；C5/C21/C22的符号由显示层处理。
 */
static uint32_t Game_GetAccountPageValue(uint8_t page_index);
static uint16_t Game_CalcCreditFromCoinPulse(uint32_t coin_count);
static void Game_ProcessClearAccountRequest(void);
static uint8_t Game_CountMaskBits(uint16_t mask);
static void Game_ApplySettingExitActions(void);
static void Game_UpdateSettingActionAcknowledge(uint8_t param_index);
static void Game_ShowSettingPage(void);
static void Game_ShowAccountPage(void);
static void Game_TaskSettingMode(void);
static void Game_TaskAccountMode(void);
#if (GAME_RTC_UART_DEBUG_ENABLE != 0U)
static void Game_PrintRtcToUart(void);
#endif

static uint8_t Game_IsAccountTimePage(void);
static uint8_t Game_IsPcfTimeUsable(const Pcf8563_Time *time);
static void Game_FillDefaultRtcTime(Pcf8563_Time *time);
static void Game_AccountTimeToPcf(const Account_RtcTime *saved_time, Pcf8563_Time *rtc_time);
static void Game_SaveRtcTimeToAccount(const Pcf8563_Time *time);
static uint8_t Game_LoadAccountTimeForDisplay(void);
static void Game_RestoreRtcFromAccount(void);
static void Game_RtcBackupTask(void);
static uint8_t Game_DaysInMonth(uint16_t year, uint8_t month);
static void Game_ClampAccountTimeCacheDay(void);
static void Game_AdjustAccountTime(uint8_t increase);

#if (GAME_RTC_UART_DEBUG_ENABLE != 0U)
/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_PrintRtcToUart）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：now；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_PrintRtcToUart(void)
{
    Pcf8563_Time now;

    if (Gtim_IsElapsed(s_rtc_uart_last_print_ms, GAME_RTC_UART_PRINT_MS) == 0U)
    {
        return;
    }

    s_rtc_uart_last_print_ms = Gtim_NowMs();

    if (Pcf8563_ReadTime(&now) != 0U)
    {
        printf("RTC=PCF8563 %04u-%02u-%02u %02u:%02u:%02u VL=%u err=%u ready=%u\r\n",
               (unsigned int)now.year,
               (unsigned int)now.month,
               (unsigned int)now.day,
               (unsigned int)now.hour,
               (unsigned int)now.minute,
               (unsigned int)now.second,
               (unsigned int)now.voltage_low,
               (unsigned int)Pcf8563_GetLastErrorCode(),
               (unsigned int)Pcf8563_IsReady());
    }
    else
    {
        printf("RTC=PCF8563 read_fail err=%u ready=%u\r\n",
               (unsigned int)Pcf8563_GetLastErrorCode(),
               (unsigned int)Pcf8563_IsReady());
    }
}
#endif

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_IsAccountTimePage）。
 * @调用关系 直接调用者：game.c:Game_ShowAccountPage、game.c:Game_TaskAccountMode。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_IsAccountTimePage(void)
{
    return ((s_account_page_index == GAME_ACCOUNT_DATE_PAGE_INDEX) ||
            (s_account_page_index == GAME_ACCOUNT_CLOCK_PAGE_INDEX)) ? 1U : 0U;
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_IsPcfTimeUsable）。
 * @调用关系 直接调用者：game.c:Game_SaveRtcTimeToAccount、game.c:Game_LoadAccountTimeForDisplay、game.c:Game_RestoreRtcFromAccount、game.c:Game_RtcBackupTask。
 * @param time 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_IsPcfTimeUsable(const Pcf8563_Time *time)
{
    return Pcf8563_IsTimeValidValue(time);
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_FillDefaultRtcTime）。
 * @调用关系 直接调用者：game.c:Game_LoadAccountTimeForDisplay。
 * @param time 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_FillDefaultRtcTime(Pcf8563_Time *time)
{
    if (time == 0)
    {
        return;
    }

    time->year = GAME_RTC_DEFAULT_YEAR;
    time->month = GAME_RTC_DEFAULT_MONTH;
    time->day = GAME_RTC_DEFAULT_DAY;
    time->weekday = 0U;
    time->hour = GAME_RTC_DEFAULT_HOUR;
    time->minute = GAME_RTC_DEFAULT_MINUTE;
    time->second = GAME_RTC_DEFAULT_SECOND;
    time->voltage_low = 0U;
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_AccountTimeToPcf）。
 * @调用关系 直接调用者：game.c:Game_LoadAccountTimeForDisplay、game.c:Game_RestoreRtcFromAccount、game.c:Game_RtcBackupTask。
 * @param saved_time 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @param rtc_time 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_AccountTimeToPcf(const Account_RtcTime *saved_time, Pcf8563_Time *rtc_time)
{
    if ((saved_time == 0) || (rtc_time == 0))
    {
        return;
    }

    rtc_time->year = saved_time->year;
    rtc_time->month = saved_time->month;
    rtc_time->day = saved_time->day;
    rtc_time->weekday = 0U;
    rtc_time->hour = saved_time->hour;
    rtc_time->minute = saved_time->minute;
    rtc_time->second = saved_time->second;
    rtc_time->voltage_low = 0U;
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_SaveRtcTimeToAccount）。
 * @调用关系 直接调用者：game.c:Game_RestoreRtcFromAccount、game.c:Game_RtcBackupTask、game.c:Game_AdjustAccountTime。
 * @局部状态 主要临时变量：saved_time；均只在本次调用中有效。
 * @param time 只读输入指针；调用方必须保证地址有效且容量满足接口约定。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_SaveRtcTimeToAccount(const Pcf8563_Time *time)
{
    Account_RtcTime saved_time;

    if (Game_IsPcfTimeUsable(time) == 0U)
    {
        return;
    }

    saved_time.year = time->year;
    saved_time.month = time->month;
    saved_time.day = time->day;
    saved_time.hour = time->hour;
    saved_time.minute = time->minute;
    saved_time.second = time->second;
    saved_time.valid = 1U;
    Account_SetRtcTime(&saved_time);
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_LoadAccountTimeForDisplay）。
 * @调用关系 直接调用者：game.c:Game_AdjustAccountTime、game.c:Game_ShowAccountPage。
 * @局部状态 主要临时变量：saved_time；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_LoadAccountTimeForDisplay(void)
{
    Account_RtcTime saved_time;

    if ((s_account_time_valid != 0U) &&
        (Game_IsPcfTimeUsable(&s_account_time_cache) != 0U))
    {
        return 1U;
    }

    if (Pcf8563_ReadTime(&s_account_time_cache) != 0U)
    {
        if (Game_IsPcfTimeUsable(&s_account_time_cache) != 0U)
        {
            s_account_time_valid = 1U;
            return 1U;
        }
    }

    Account_GetRtcTime(&saved_time);
    if (saved_time.valid != 0U)
    {
        Game_AccountTimeToPcf(&saved_time, &s_account_time_cache);
        s_account_time_valid = 1U;
        return 1U;
    }

    Game_FillDefaultRtcTime(&s_account_time_cache);
    s_account_time_valid = 1U;
    return 1U;
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_RestoreRtcFromAccount）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @局部状态 主要临时变量：saved_time、rtc_time；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_RestoreRtcFromAccount(void)
{
    Account_RtcTime saved_time;
    Pcf8563_Time rtc_time;

    Account_GetRtcTime(&saved_time);
    if (saved_time.valid == 0U)
    {
        if (Pcf8563_ReadTime(&rtc_time) != 0U)
        {
            if (Game_IsPcfTimeUsable(&rtc_time) != 0U)
            {
                Game_SaveRtcTimeToAccount(&rtc_time);
            }
        }
        return;
    }

    Game_AccountTimeToPcf(&saved_time, &rtc_time);
    if (Pcf8563_SetTime(&rtc_time) != 0U)
    {
        s_account_time_cache = rtc_time;
        s_account_time_valid = 1U;
        s_account_time_last_read_ms = Gtim_NowMs();
        s_rtc_backup_last_minute_stamp = 0xFFFFFFFFUL;
    }
}

/**
 * @brief 游戏主状态机：周期处理（对应符号 Game_RtcBackupTask）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：saved_time、now、minute_stamp；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_RtcBackupTask(void)
{
    Account_RtcTime saved_time;
    Pcf8563_Time now;
    uint32_t minute_stamp;

    if (Gtim_IsElapsed(s_rtc_backup_last_check_ms, GAME_RTC_BACKUP_CHECK_MS) == 0U)
    {
        return;
    }
    s_rtc_backup_last_check_ms = Gtim_NowMs();

    if (Pcf8563_ReadTime(&now) == 0U)
    {
        return;
    }

    if (Game_IsPcfTimeUsable(&now) != 0U)
    {
        minute_stamp = ((uint32_t)now.year * 100000000UL) +
                       ((uint32_t)now.month * 1000000UL) +
                       ((uint32_t)now.day * 10000UL) +
                       ((uint32_t)now.hour * 100UL) +
                       (uint32_t)now.minute;

        if (minute_stamp != s_rtc_backup_last_minute_stamp)
        {
            s_rtc_backup_last_minute_stamp = minute_stamp;
            Game_SaveRtcTimeToAccount(&now);
        }
        return;
    }

    Account_GetRtcTime(&saved_time);
    if (saved_time.valid != 0U)
    {
        Game_AccountTimeToPcf(&saved_time, &now);
        (void)Pcf8563_SetTime(&now);
        s_account_time_cache = now;
        s_account_time_valid = 1U;
    }
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_DaysInMonth）。
 * @调用关系 直接调用者：game.c:Game_ClampAccountTimeCacheDay、game.c:Game_AdjustAccountTime。
 * @param year 按值传入；函数内部修改不会回写调用方变量。
 * @param month 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_DaysInMonth(uint16_t year, uint8_t month)
{
    if ((month == 4U) || (month == 6U) || (month == 9U) || (month == 11U))
    {
        return 30U;
    }

    if (month == 2U)
    {
        if (((year % 400U) == 0U) ||
            (((year % 4U) == 0U) && ((year % 100U) != 0U)))
        {
            return 29U;
        }
        return 28U;
    }

    return 31U;
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_ClampAccountTimeCacheDay）。
 * @调用关系 直接调用者：game.c:Game_AdjustAccountTime。
 * @局部状态 主要临时变量：max_day；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_ClampAccountTimeCacheDay(void)
{
    uint8_t max_day;

    max_day = Game_DaysInMonth(s_account_time_cache.year, s_account_time_cache.month);
    if (s_account_time_cache.day > max_day)
    {
        s_account_time_cache.day = max_day;
    }
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_AdjustAccountTime）。
 * @调用关系 直接调用者：game.c:Game_TaskAccountMode。
 * @param increase 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_AdjustAccountTime(uint8_t increase)
{
    if (Game_LoadAccountTimeForDisplay() == 0U)
    {
        return;
    }

    if (s_account_page_index == GAME_ACCOUNT_DATE_PAGE_INDEX)
    {
        if (s_account_time_field_index == 0U)
        {
            if (increase != 0U)
            {
                s_account_time_cache.year = (s_account_time_cache.year >= 2099U) ? 2024U : (uint16_t)(s_account_time_cache.year + 1U);
            }
            else
            {
                s_account_time_cache.year = (s_account_time_cache.year <= 2024U) ? 2099U : (uint16_t)(s_account_time_cache.year - 1U);
            }
            Game_ClampAccountTimeCacheDay();
        }
        else if (s_account_time_field_index == 1U)
        {
            if (increase != 0U)
            {
                s_account_time_cache.month = (s_account_time_cache.month >= 12U) ? 1U : (uint8_t)(s_account_time_cache.month + 1U);
            }
            else
            {
                s_account_time_cache.month = (s_account_time_cache.month <= 1U) ? 12U : (uint8_t)(s_account_time_cache.month - 1U);
            }
            Game_ClampAccountTimeCacheDay();
        }
        else
        {
            if (increase != 0U)
            {
                s_account_time_cache.day++;
                if (s_account_time_cache.day > Game_DaysInMonth(s_account_time_cache.year, s_account_time_cache.month))
                {
                    s_account_time_cache.day = 1U;
                }
            }
            else
            {
                if (s_account_time_cache.day <= 1U)
                {
                    s_account_time_cache.day = Game_DaysInMonth(s_account_time_cache.year, s_account_time_cache.month);
                }
                else
                {
                    s_account_time_cache.day--;
                }
            }
        }
    }
    else
    {
        if (s_account_time_field_index == 0U)
        {
            if (increase != 0U)
            {
                s_account_time_cache.hour = (s_account_time_cache.hour >= 23U) ? 0U : (uint8_t)(s_account_time_cache.hour + 1U);
            }
            else
            {
                s_account_time_cache.hour = (s_account_time_cache.hour == 0U) ? 23U : (uint8_t)(s_account_time_cache.hour - 1U);
            }
        }
        else if (s_account_time_field_index == 1U)
        {
            if (increase != 0U)
            {
                s_account_time_cache.minute = (s_account_time_cache.minute >= 59U) ? 0U : (uint8_t)(s_account_time_cache.minute + 1U);
            }
            else
            {
                s_account_time_cache.minute = (s_account_time_cache.minute == 0U) ? 59U : (uint8_t)(s_account_time_cache.minute - 1U);
            }
        }
        else
        {
            if (increase != 0U)
            {
                s_account_time_cache.second = (s_account_time_cache.second >= 59U) ? 0U : (uint8_t)(s_account_time_cache.second + 1U);
            }
            else
            {
                s_account_time_cache.second = (s_account_time_cache.second == 0U) ? 59U : (uint8_t)(s_account_time_cache.second - 1U);
            }
        }
    }

    s_account_time_cache.weekday = 0U;
    s_account_time_cache.voltage_low = 0U;
    if (Pcf8563_SetTime(&s_account_time_cache) != 0U)
    {
        Game_SaveRtcTimeToAccount(&s_account_time_cache);
        (void)Account_SaveNow();
        s_account_time_valid = 1U;
        s_account_time_last_read_ms = Gtim_NowMs();
        s_rtc_backup_last_minute_stamp = 0xFFFFFFFFUL;
    }
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_CalcTicketPulseCount）。
 * @调用关系 直接调用者：game.c:Game_AddLotteryPrizeAndPayout、game.c:Game_AddLinePrizeAndHold、game.c:Game_Task。
 * @局部状态 主要临时变量：ticket_count；均只在本次调用中有效。
 * @param prize_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint32_t Game_CalcTicketPulseCount(uint16_t prize_count)
{
    uint32_t ticket_count;

    if (prize_count == 0U)
    {
        return 0U;
    }

    ticket_count = (uint32_t)prize_count * (uint32_t)Setting_GetTicketsPerCoin();

    if (ticket_count > TICKET_PENDING_MAX)
    {
        ticket_count = TICKET_PENDING_MAX;
    }

    return ticket_count;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_RecordReturnCommitment）。
 * @调用关系 直接调用者：game.c:Game_AddLotteryPrizeAndPayout、game.c:Game_AddLinePrizeAndHold。
 * @局部状态 主要临时变量：output_count；均只在本次调用中有效。
 * @param prize_count 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_RecordReturnCommitment(uint16_t prize_count)
{
    uint32_t output_count;

    /* 暂存奖励生成时已经计入承诺；右屏兑奖不是再次中奖，不能重复计入。 */
    if (s_redeeming_credits != 0U) return;

    output_count = 0U;
    if (Setting_IsCoinPayoutEnabled() != 0U)
    {
        output_count++;
    }
    if (Setting_IsTicketPayoutEnabled() != 0U)
    {
        output_count++;
    }
    if (Setting_IsCardPayoutEnabled() != 0U)
    {
        output_count++;
    }

    Account_AddTotalPayout(prize_count);
    Account_AddReturnCommitment((uint32_t)prize_count * output_count);
}

/** 鎶?/5甯佽€佽檸鏈哄鍚屾椂浜ょ粰宸插惎鐢ㄧ殑杈撳嚭璁惧骞跺啓鍏8/杩斿鎵胯銆?*/
/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_AddLotteryPrizeAndPayout）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：ticket_count、display_count、next_prize；均只在本次调用中有效。
 * @param prize_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_AddLotteryPrizeAndPayout(uint16_t prize_count)
{
    uint32_t ticket_count;
    uint32_t display_count;
    uint32_t next_prize;

    if ((prize_count == 0U) ||
        (GiftAlgorithm_CoinAwardAuthorized(prize_count) == 0U))
    {
        return 0U;
    }

    if (Setting_IsImmediateAwardEnabled() == 0U)
    {
        /* P14=0仍记返奖承诺，通过中屏逐步转入右屏，不启动电机。 */
        if (Game_BeginCreditTransfer(prize_count) == 0U)
        {
            return 0U;
        }
        Account_AddLotteryPayout(prize_count);
        Game_RecordReturnCommitment(prize_count);
        GiftAlgorithm_CommitCoinAward(prize_count);
        return 1U;
    }

    if (Setting_IsTicketPayoutEnabled() != 0U)
    {
        ticket_count = Game_CalcTicketPulseCount(prize_count);
        if ((ticket_count == 0U) || (Ticket_Add(ticket_count) == 0U))
        {
            return 0U;
        }
    }
    if ((Setting_IsCoinPayoutEnabled() != 0U) &&
        (Payout_AddPrize(prize_count) == 0U))
    {
        return 0U;
    }

    Account_AddLotteryPayout(prize_count);
    Game_RecordReturnCommitment(prize_count);
    GiftAlgorithm_CommitCoinAward(prize_count);
    display_count = prize_count;
    if ((Setting_IsCoinPayoutEnabled() == 0U) &&
        (Setting_IsTicketPayoutEnabled() != 0U) &&
        (Setting_IsCardPayoutEnabled() == 0U))
    {
        display_count = Game_CalcTicketPulseCount(prize_count);
        s_ticket_display_remaining += display_count;
        if (s_ticket_display_remaining > 65535U)
        {
            s_ticket_display_remaining = 65535U;
        }
    }
    next_prize = (uint32_t)s_game_prize + display_count;
    if (next_prize > 65535U)
    {
        next_prize = 65535U;
    }
    Game_SetPrize((uint16_t)next_prize);
    if (Setting_IsCardPayoutEnabled() != 0U)
    {
        (void)Game_StartCardPayout();
    }
    return 1U;
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_AddLinePrizeAndHold）。
 * @调用关系 直接调用者：game.c:Game_CompleteLineSettlement、game.c:Game_Task。
 * @局部状态 主要临时变量：next_pending、ticket_count、next_prize、total_prize、prize_count；均只在本次调用中有效。
 * @param line_prize 按值传入；函数内部修改不会回写调用方变量。
 * @param jackpot_prize 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_AddLinePrizeAndHold(uint16_t line_prize,
                                        uint16_t jackpot_prize)
{
    uint32_t next_pending;
    uint32_t ticket_count;
    uint32_t next_prize;
    uint32_t total_prize;
    uint16_t prize_count;

    total_prize = (uint32_t)line_prize + jackpot_prize;
    if (total_prize > 65535U)
    {
        total_prize = 65535U;
        if (line_prize >= 65535U)
        {
            jackpot_prize = 0U;
        }
        else
        {
            jackpot_prize = (uint16_t)(65535U - line_prize);
        }
    }
    prize_count = (uint16_t)total_prize;

    if (prize_count == 0U)
    {
        return 0U;
    }

    if ((Setting_IsImmediateAwardEnabled() == 0U) &&
        (s_redeeming_credits == 0U))
    {
        /*
         * 奖励暂存：结算仍然完成账目及彩金扣减，但不启动任何出奖设备，
         * 奖值先放中屏，再逐步转入右屏；本批转完之后才申请下一笔彩金。
         */
        if (Game_BeginCreditTransfer(prize_count) == 0U)
        {
            return 0U;
        }
        Account_AddLineTicket(line_prize);
        Jackpot_AddLocalPayout(jackpot_prize);
        Game_RecordReturnCommitment(prize_count);
        s_line_payout_pending = 0U;
        Game_EnterIdleOrReady();
        return 1U;
    }

    if (Setting_IsCardPayoutEnabled() != 0U)
    {
        if (s_redeeming_credits == 0U)
        {
            Account_AddLineTicket(line_prize);
            Jackpot_AddLocalPayout(jackpot_prize);
        }
        Game_RecordReturnCommitment(prize_count);

        next_prize = (uint32_t)s_game_prize + prize_count;
        if (next_prize > 65535U)
        {
            next_prize = 65535U;
        }
        Game_SetPrize((uint16_t)next_prize);
        s_line_payout_pending = 0U;
        s_ticket_display_remaining = 0U;
        Game_EnterIdleOrReady();
        return 1U;
    }

    ticket_count = 0U;
    if (Setting_IsTicketPayoutEnabled() != 0U)
    {
        ticket_count = Game_CalcTicketPulseCount(prize_count);
        if ((ticket_count == 0U) || (Ticket_Add(ticket_count) == 0U))
        {
            return 0U;
        }
    }

    if (s_redeeming_credits == 0U)
    {
        Account_AddLineTicket(line_prize);
        Jackpot_AddLocalPayout(jackpot_prize);
    }
    Game_RecordReturnCommitment(prize_count);

    if (Setting_IsCoinPayoutEnabled() == 0U)
    {
        next_prize = (uint32_t)s_game_prize + ticket_count;
        if (next_prize > 65535U)
        {
            next_prize = 65535U;
        }
        s_ticket_display_remaining += ticket_count;
        if (s_ticket_display_remaining > 65535U)
        {
            s_ticket_display_remaining = 65535U;
        }
        Game_SetPrize((uint16_t)next_prize);
        s_line_payout_pending = 0U;
        s_line_payout_hold_start_ms = Gtim_NowMs();
        Game_EnterIdleOrReady();
        return 1U;
    }

    next_prize = (uint32_t)s_game_prize + prize_count;
    if (next_prize > 65535U)
    {
        next_prize = 65535U;
    }
    Game_SetPrize((uint16_t)next_prize);
    s_ticket_display_remaining = 0U;

    next_pending = (uint32_t)s_line_payout_pending + prize_count;
    if (next_pending > 65535U)
    {
        next_pending = 65535U;
    }

    s_line_payout_pending = (uint16_t)next_pending;
    s_line_payout_hold_start_ms = Gtim_NowMs();
    if (s_line7_lock_active != 0U)
    {
        s_line_payout_hold_start_ms -= GAME_TICKET_SHOW_HOLD_MS;
    }
    s_state_start_ms = s_line_payout_hold_start_ms;
    s_game_mode = GAME_MODE_TICKET_HOLD;

    return 1U;
}
/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_FinishLineRound）。
 * @调用关系 直接调用者：game.c:Game_CompleteLineSettlement。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_FinishLineRound(void)
{
    Solenoid_Pulse(GAME_SOLENOID_PULSE_MS);
    Account_AddGameRound(1U);
    Game_SetScore(0U);
    s_coins_in_channel_mask = 0U;
    s_round_has_gift_light = 0U;
    Segment_EyeLedClearAll();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 游戏主状态机：启动（对应符号 Game_StartCardPayout）。
 * @调用关系 直接调用者：game.c:Game_AddLotteryPrizeAndPayout、game.c:Game_HandleTicketKey、game.c:Game_CompleteLineSettlement、game.c:Game_Task。
 * @局部状态 主要临时变量：coins_per_batch、cards_per_batch、batch_count、card_count、score_remainder；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_StartCardPayout(void)
{
    uint16_t coins_per_batch;
    uint16_t cards_per_batch;
    uint32_t batch_count;
    uint32_t card_count;
    uint16_t score_remainder;

    if ((Account_IsCreditTransfer() != 0U) ||
        (Setting_IsCardPayoutEnabled() == 0U) ||
        (s_card_payout_remaining != 0U))
    {
        return 0U;
    }

    coins_per_batch = Setting_GetCardCoinsPerBatch();
    cards_per_batch = Setting_GetCardsPerBatch();
    if ((coins_per_batch == 0U) || (cards_per_batch == 0U) ||
        (s_game_prize < coins_per_batch))
    {
        return 0U;
    }

    batch_count = (uint32_t)s_game_prize / coins_per_batch;
    card_count = batch_count * cards_per_batch;
    score_remainder = (uint16_t)((uint32_t)s_game_prize % coins_per_batch);
    if ((card_count == 0U) || (Ticket_Add(card_count) == 0U))
    {
        return 0U;
    }

    s_card_payout_remaining = card_count;
    s_card_payout_x = coins_per_batch;
    s_card_payout_y = cards_per_batch;

    Game_SetPrize(score_remainder);
    Account_SetCardPayoutPending(card_count, coins_per_batch, cards_per_batch);
    (void)Account_SaveNow();
    Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_LINE_WIN);
    return 1U;
}
/** @brief 缂佺喍绔存径鍕倞IDLE/READY閻樿埖鈧胶娈戦崙铏广偍闁款噯绱濋崠鍛P1=4閻ㄥ嫬鐡ㄩ崚鍡楁嫲閸欐垵宕遍妴?*/
/**
 * @brief 游戏主状态机：处理状态或事件（对应符号 Game_HandleTicketKey）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_HandleTicketKey(void)
{
    /* 转移期间不能把已经转入的右屏再次兑出，避免两个批次混在中屏。 */
    if (Account_IsCreditTransfer() != 0U) return;
    if (Setting_IsImmediateAwardEnabled() == 0U)
    {
        /*
         * 循环模式先处理左屏，经中屏逐个转到右屏；再次按出票，
         * 且左、中屏均清空后，才允许兑出右屏余额。
         */
        if (s_game_score != 0U)
        {
            (void)Game_TransferScoreToCredits();
        }
        else if ((s_game_prize == 0U) &&
                 (s_line_payout_pending == 0U) &&
                 (s_game_coin != 0U))
        {
            (void)Game_RedeemCreditsToAward();
        }
        return;
    }

    /* 即玩即吐也先结算左屏；只有左、中屏都没有待发奖励，
     * 才把右屏剩余游戏币作为退币处理。 */
    if ((s_game_score == 0U) &&
        (s_game_prize == 0U) &&
        (s_line_payout_pending == 0U) &&
        (s_game_coin != 0U))
    {
        (void)Game_RedeemCreditsToAward();
        return;
    }

    if (Setting_IsCardPayoutEnabled() != 0U)
    {
        s_card_payout_requested = 1U;
        if ((s_game_score != 0U) && (Setting_IsScoreExchangeEnabled() != 0U))
        {
            if (Game_BeginLineSettlement() == 0U)
            {
                s_card_payout_requested = 0U;
            }
        }
        else
        {
            s_card_payout_requested = 0U;
            (void)Game_StartCardPayout();
        }
        return;
    }

    if ((s_game_score != 0U) && (Setting_IsScoreExchangeEnabled() != 0U))
    {
        (void)Game_BeginLineSettlement();
    }
}

static uint8_t Game_TransferScoreToCredits(void)
{
    if ((s_game_score == 0U) ||
        (Setting_IsScoreExchangeEnabled() == 0U))
    {
        return 0U;
    }

    /* 仍走统一连线/彩金结算，5/6/7连线的彩金请求不会因为暂存模式丢失。 */
    return Game_BeginLineSettlement();
}

static uint8_t Game_RedeemCreditsToAward(void)
{
    uint16_t redeem_count;

    if ((s_game_coin == 0U) ||
        (s_game_score != 0U) ||
        (s_game_prize != 0U) ||
        (Account_IsCreditTransfer() != 0U) ||
        (s_line_payout_pending != 0U))
    {
        return 0U;
    }

    redeem_count = s_game_coin;
    s_redeeming_credits = 1U;
    if (Game_AddLinePrizeAndHold(redeem_count, 0U) == 0U)
    {
        s_redeeming_credits = 0U;
        return 0U;
    }
    s_redeeming_credits = 0U;

    /* 只有出奖任务成功入队后才清右屏，防止设备队列满时吞掉玩家游戏币。 */
    Game_SetCoin(0U);
    if (Setting_IsCardPayoutEnabled() != 0U)
    {
        (void)Game_StartCardPayout();
    }
    if (s_game_mode != GAME_MODE_TICKET_HOLD)
    {
        Game_EnterIdleOrReady();
    }
    return 1U;
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_CompleteLineSettlement）。
 * @调用关系 直接调用者：game.c:Game_BeginLineSettlement。
 * @局部状态 主要临时变量：total_prize；均只在本次调用中有效。
 * @param line_prize 按值传入；函数内部修改不会回写调用方变量。
 * @param jackpot_prize 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_CompleteLineSettlement(uint16_t line_prize,
                                           uint16_t jackpot_prize)
{
    uint32_t total_prize;

    if (Game_AddLinePrizeAndHold(line_prize, jackpot_prize) == 0U)
    {
        return 0U;
    }

    if (s_round_has_gift_light != 0U)
    {
        total_prize = (uint32_t)line_prize + jackpot_prize;
        Account_AddGiftWinValue(total_prize);
    }

    Game_FinishLineRound();
    if ((s_card_payout_requested != 0U) &&
        (Setting_IsCardPayoutEnabled() != 0U))
    {
        s_card_payout_requested = 0U;
        (void)Game_StartCardPayout();
    }
    return 1U;
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_BeginLineSettlement）。
 * @调用关系 直接调用者：game.c:Game_HandleTicketKey、game.c:Game_StartLine7Settlement。
 * @局部状态 主要临时变量：tier；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_BeginLineSettlement(void)
{
#if (JACKPOT_LINE_BONUS_ENABLED == 0U)
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    return Game_CompleteLineSettlement(s_game_score, 0U);
#else
    uint8_t tier;

    tier = Score_GetJackpotTier(s_coins_in_channel_mask);
    if (tier == 0U)
    {
        return Game_CompleteLineSettlement(s_game_score, 0U);
    }

    if (s_jackpot_settlement_phase != GAME_JACKPOT_SETTLEMENT_NONE)
    {
        return 0U;
    }

    /*
     * Jackpot line awards are paid in two physical batches.  The pool is not
     * claimed here: first queue only the fixed 30/55/90 line prize.  Once
     * that batch has really reached zero, Game_UpdateJackpotSettlement()
     * requests and deducts the jackpot share, then queues the second batch.
     */
    s_jackpot_tier_pending = tier;
    s_jackpot_round_had_gift = s_round_has_gift_light;
    s_jackpot_settlement_phase = GAME_JACKPOT_SETTLEMENT_BASE_PAYOUT;
    if (Game_CompleteLineSettlement(s_game_score, 0U) == 0U)
    {
        s_jackpot_settlement_phase = GAME_JACKPOT_SETTLEMENT_NONE;
        s_jackpot_tier_pending = 0U;
        s_jackpot_round_had_gift = 0U;
        return 0U;
    }

    /* Score has been cleared and the base award queued: persist these together.
     * No claim may be published until this checkpoint has reached EEPROM. */
    Account_SetJackpotSettlement((uint8_t)s_jackpot_settlement_phase,
                                s_jackpot_tier_pending, s_jackpot_round_had_gift);
    s_settlement_checkpoint_pending = 1U;
    return 1U;
#endif
}

/**
 * @brief 游戏主状态机：启动（对应符号 Game_StartJackpotLinePresentation）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param line_prize 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_StartJackpotLinePresentation(uint16_t line_prize)
{
#if (JACKPOT_LINE_BONUS_ENABLED != 0U)
    if ((line_prize != SCORE_AWARD_LINE_5) &&
        (line_prize != SCORE_AWARD_LINE_6) &&
        (line_prize != SCORE_AWARD_LINE_7))
    {
        return;
    }

    JackpotDisplay_SetBlink(1U);
#else
    (void)line_prize;
#endif
}

/**
 * @brief 游戏主状态机：停止（对应符号 Game_StopJackpotLinePresentation）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_StopJackpotLinePresentation(void)
{
    JackpotDisplay_SetBlink(0U);
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_IsJackpotOutputComplete）。
 * @调用关系 直接调用者：game.c:Game_UpdateJackpotSettlement。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_IsJackpotOutputComplete(void)
{
    /* 消掉基础奖后必须先松开清除键，彩金才进入第二个独立批次。 */
    if (s_ticket_clear_wait_release != 0U) return 0U;
    if (Account_IsCreditTransfer() != 0U) return 0U;
    if ((s_line_payout_pending != 0U) ||
        (Payout_IsBusy() != 0U) ||
        (Ticket_IsBusy() != 0U) ||
        (s_card_payout_remaining != 0U))
    {
        return 0U;
    }

    if ((Setting_IsCardPayoutEnabled() == 0U) &&
        (s_game_prize != 0U) &&
        (Ticket_HasFault() == 0U))
    {
        return 0U;
    }

    return 1U;
}

/*
 * Skip only the ticket batch which is physically running now. Confirmed PF9
 * pulses have already gone through the normal accounting path; the remainder
 * is booked directly into the ticket-out account and is deliberately not
 * queued to the slow external ticket meter. 消票结束后直接恢复正常游戏界面。
 */
/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_TrySkipTicketOutput）。
 * @调用关系 直接调用者：game.c:Game_UpdateTicketClearHold。
 * @局部状态 主要临时变量：skipped_count；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_TrySkipTicketOutput(void)
{
    uint32_t skipped_count;

    /* 同批次币/票均暂停并结清；不提前取走下一笔尚未到达的彩金。 */
    uint32_t skipped_coin = Payout_SkipRemaining();
    uint16_t real_coin = Payout_TakePaidCount();
    skipped_coin += s_line_payout_pending;
    s_line_payout_pending = 0U;
    if ((skipped_coin != 0U) || (real_coin != 0U))
    {
        if (real_coin != 0U)
        {
            Meter_AddPulse(METER_PAYOUT_OUT, real_coin);
            Account_AddPhysicalCoinOut(real_coin);
        }
        Game_SetPrize(0U);
    }

    if ((Setting_IsTicketPayoutEnabled() == 0U) ||
        (Setting_IsCardPayoutEnabled() != 0U) ||
        (Ticket_GetRemaining() == 0U))
    {
        Game_EnterIdleOrReady();
        return (skipped_coin != 0U) ? 1U : 0U;
    }

    skipped_count = Ticket_SkipRemaining();
    if (skipped_count == 0U)
    {
        return 0U;
    }

    Account_AddTicketOut(skipped_count, Setting_GetTicketsPerCoin());
    s_ticket_display_remaining = 0U;
    Game_SetPrize(0U);

    Game_EnterIdleOrReady();
    return 1U;
}

/**
 * @brief 处理出奖期间“故障清除键长按 5 秒消票/消币”。
 *
 * 按键刚按下便调用 Ticket_Pause()，所以计时过程中出票口不会继续动作；
 * 未满 5 秒松开则调用 Ticket_Resume() 继续原批次；满 5 秒后将尚未吐出的
 * 数量直接计入出票账目并清空中屏，随后直接恢复正常游戏状态。
 */
static void Game_UpdateTicketClearHold(void)
{
    uint8_t ticket_batch_active;

    /* 一次长按只允许消掉一个批次；必须松手后才能对后续彩金批次再次长按。 */
    if (s_ticket_clear_wait_release != 0U)
    {
        if (Key_IsPressed(KEY_CLEAR) == 0U)
        {
            s_ticket_clear_wait_release = 0U;
            (void)Key_TakePress(KEY_CLEAR);
        }
        return;
    }

    ticket_batch_active =
        (((s_game_mode == GAME_MODE_IDLE) ||
          (s_game_mode == GAME_MODE_READY) ||
          (s_game_mode == GAME_MODE_JACKPOT_WAIT) ||
          (s_game_mode == GAME_MODE_TICKET_HOLD)) &&
         (Setting_IsCardPayoutEnabled() == 0U) &&
         (Account_IsCreditTransfer() == 0U) &&
         ((Ticket_GetRemaining() != 0U) || (Payout_GetRemaining() != 0U) ||
          (s_line_payout_pending != 0U))) ? 1U : 0U;

    if (ticket_batch_active == 0U)
    {
        if (s_ticket_clear_hold_active != 0U)
        {
            Ticket_Resume();
            Payout_Resume();
        }
        s_ticket_clear_hold_active = 0U;
        s_ticket_clear_hold_start_ms = 0U;
        return;
    }

    if (Key_IsPressed(KEY_CLEAR) != 0U)
    {
        if (s_ticket_clear_hold_active == 0U)
        {
            s_ticket_clear_hold_active = 1U;
            s_ticket_clear_hold_start_ms = Gtim_NowMs();
            Ticket_Pause();
            Payout_Pause();
        }
        else if (Gtim_IsElapsed(s_ticket_clear_hold_start_ms,
                                GAME_TICKET_CLEAR_HOLD_MS) != 0U)
        {
            s_ticket_clear_hold_active = 0U;
            s_ticket_clear_hold_start_ms = 0U;
            s_ticket_clear_wait_release = 1U;
            (void)Key_TakePress(KEY_CLEAR);
            (void)Game_TrySkipTicketOutput();
        }
        return;
    }

    if (s_ticket_clear_hold_active != 0U)
    {
        /* 未按满 5 秒：取消消票，下一轮 Ticket_Task 从剩余数量继续出票。 */
        s_ticket_clear_hold_active = 0U;
        s_ticket_clear_hold_start_ms = 0U;
        (void)Key_TakePress(KEY_CLEAR);
        Ticket_Resume();
        Payout_Resume();
    }
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_UpdateJackpotSettlement）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_SettlementCheckpointReady(void)
{
    if (s_settlement_checkpoint_pending == 0U) return 1U;
    if (Account_SaveNow() == 0U) return 0U;
    /* BONUS in EEPROM proves the amount has been accepted exactly once.
     * After a reset we may safely retry the ACK, without adding it again. */
    if (((s_jackpot_settlement_phase == GAME_JACKPOT_SETTLEMENT_BONUS_PAYOUT) ||
         (s_jackpot_settlement_phase == GAME_JACKPOT_SETTLEMENT_NONE)) &&
        (Jackpot_ConfirmAward() == 0U)) return 0U;
    s_settlement_checkpoint_pending = 0U;
    return 1U;
}

/*
 * 放弃一笔“主机始终没有下发”的彩金申请。
 *
 * 先把档位写入C42并清掉主账WAIT，再清Jackpot申请，两个EEPROM日志按此
 * 顺序提交。任一步写失败都继续保留E17，避免只清一半后再次形成死锁。
 * 固定连线奖在进入WAIT前已经结算；这里只放弃金额未知、从未收到的彩金。
 */
static uint8_t Game_AbortOrphanJackpotWait(void)
{
    uint8_t tier = s_jackpot_tier_pending;
    uint8_t claim_tier = Jackpot_GetPendingTier();

    if (s_jackpot_settlement_phase != GAME_JACKPOT_SETTLEMENT_AWARD_WAIT)
    {
        return 0U;
    }
    if ((tier < 5U) || (tier > 7U)) tier = claim_tier;
    if ((tier < 5U) || (tier > 7U)) return 0U;

    Account_ArchiveOrphanJackpot(tier);
    if ((claim_tier >= 5U) && (claim_tier <= 7U) && (claim_tier != tier))
    {
        Account_ArchiveOrphanJackpot(claim_tier);
    }
    if (Account_SaveNow() == 0U) return 0U;
    if (Jackpot_ConfirmAward() == 0U) return 0U;

    s_jackpot_settlement_phase = GAME_JACKPOT_SETTLEMENT_NONE;
    s_jackpot_tier_pending = 0U;
    s_jackpot_round_had_gift = 0U;
    s_jackpot_recovery_tier = 0U;
    s_settlement_checkpoint_pending = 0U;
    s_line7_lock_active = 0U;
    s_line7_settlement_started = 0U;
    s_line7_hold_start_ms = 0U;
    Game_StopJackpotLinePresentation();
    s_fault_resume_mode = (s_game_coin != 0U) ? GAME_MODE_READY : GAME_MODE_IDLE;
    return 1U;
}

/* Called only after POST. Old images without a journal still use legacy
 * pending-claim recovery below; an old lost BASE tier cannot be reconstructed. */
static uint8_t Game_RestoreJackpotSettlement(void)
{
    uint8_t tier;
    uint8_t had_gift;
    uint8_t phase = Account_GetJackpotSettlement(&tier, &had_gift);
    uint8_t claim_tier = Jackpot_GetPendingTier();
    /* P13切换后，不能把旧联机WAIT交给已关闭485的单机无限等待。
     * 兼容两类板：旧版本已删除申请但主账还在WAIT；以及新版本保留着申请。
     * BONUS已经到账，仍按原流程吐完；BASE无已发申请时仍可正常本地结算。 */
    if (((Modbus_GetRole() != MODBUS_ROLE_SLAVE) &&
         ((phase == GAME_JACKPOT_SETTLEMENT_AWARD_WAIT) ||
          ((phase != GAME_JACKPOT_SETTLEMENT_BONUS_PAYOUT) &&
           (claim_tier >= 5U) && (claim_tier <= 7U)))) ||
        /* 隔离主账已保存、清申请未保存时掉电：即使又切回分机，也只补做清申请。 */
        ((phase == 0U) && (claim_tier >= 5U) && (claim_tier <= 7U) &&
         ((Account_GetOrphanJackpotMask() & (1U << (claim_tier - 5U))) != 0U)))
    {
        if (phase == 0U) tier = claim_tier;
        Account_ArchiveOrphanJackpot(tier);
        if ((claim_tier >= 5U) && (claim_tier <= 7U) && (claim_tier != tier))
            Account_ArchiveOrphanJackpot(claim_tier);
        s_jackpot_settlement_phase = GAME_JACKPOT_SETTLEMENT_NONE;
        s_jackpot_tier_pending = 0U;
        s_jackpot_round_had_gift = 0U;
        s_jackpot_recovery_tier = 0U;
        s_line7_lock_active = 0U;
        s_line7_settlement_started = 0U;
        s_settlement_checkpoint_pending = 1U;
        Game_StopJackpotLinePresentation();
        s_game_mode = GAME_MODE_JACKPOT_WAIT;
        if (Game_SettlementCheckpointReady() != 0U) Game_EnterIdleOrReady();
        return 1U;
    }
    if (phase == 0U) return 0U;
    s_jackpot_tier_pending = tier;
    s_jackpot_round_had_gift = had_gift;
    s_jackpot_recovery_tier = 0U;
    if ((phase == GAME_JACKPOT_SETTLEMENT_BASE_PAYOUT) &&
        (Jackpot_GetPendingTier() == tier))
    {
        /* Claim was saved after the base reached zero, just before reset. */
        phase = GAME_JACKPOT_SETTLEMENT_AWARD_WAIT;
    }
    s_jackpot_settlement_phase = (Game_JackpotSettlementPhase)phase;
    Account_SetJackpotSettlement(phase, tier, had_gift);
    s_settlement_checkpoint_pending = 1U;
    s_line7_lock_active = (tier == 7U) ? 1U : 0U;
    s_line7_settlement_started = s_line7_lock_active;
    JackpotDisplay_SetBlink((phase != GAME_JACKPOT_SETTLEMENT_BONUS_PAYOUT) ? 1U : 0U);
    s_game_mode = GAME_MODE_JACKPOT_WAIT;
    s_state_start_ms = Gtim_NowMs();
    return 1U;
}

static void Game_UpdateJackpotSettlement(void)
{
    /* 菜单退出后再推进结算，避免基础奖刚吐完就把工作人员强行踢出菜单。 */
    if ((s_game_mode == GAME_MODE_SETTING) || (s_game_mode == GAME_MODE_ACCOUNT)) return;
    if (s_settlement_checkpoint_pending != 0U) return;
    if (s_jackpot_settlement_phase ==
        GAME_JACKPOT_SETTLEMENT_BASE_PAYOUT)
    {
        if (Game_IsJackpotOutputComplete() == 0U)
        {
            return;
        }

        /* Commit zero remaining BASE before a request can cause a deduction. */
        if (Account_SaveNow() == 0U) return;
        if (Jackpot_RequestAward(s_jackpot_tier_pending) != 0U)
        {
            s_jackpot_settlement_phase =
                GAME_JACKPOT_SETTLEMENT_AWARD_WAIT;
            Account_SetJackpotSettlement((uint8_t)s_jackpot_settlement_phase,
                                        s_jackpot_tier_pending, s_jackpot_round_had_gift);
            s_settlement_checkpoint_pending = 1U;
            s_state_start_ms = Gtim_NowMs();
            s_game_mode = GAME_MODE_JACKPOT_WAIT;
        }
        return;
    }

    if (s_jackpot_settlement_phase ==
        GAME_JACKPOT_SETTLEMENT_BONUS_PAYOUT)
    {
        if (Game_IsJackpotOutputComplete() == 0U)
        {
            return;
        }

        s_jackpot_settlement_phase = GAME_JACKPOT_SETTLEMENT_NONE;
        s_jackpot_tier_pending = 0U;
        s_jackpot_round_had_gift = 0U;
        Account_SetJackpotSettlement(0U, 0U, 0U);
        s_settlement_checkpoint_pending = 1U;
        Game_EnterIdleOrReady();
    }
}

/**
 * @brief 游戏主状态机：启动（对应符号 Game_StartFailEyeBlink）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param eye_mask 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_StartFailEyeBlink(uint16_t eye_mask)
{
    eye_mask &= GAME_SCORE_CHANNEL_MASK;
    if (eye_mask == 0U)
    {
        return;
    }

    s_fail_eye_blink_mask = eye_mask;
    s_fail_eye_blink_visible = 1U;
    s_fail_eye_blink_start_ms = Gtim_NowMs();
    s_fail_eye_blink_last_ms = s_fail_eye_blink_start_ms;

    Segment_EyeLedClearAll();
    Segment_EyeLedSetChannelMask(s_fail_eye_blink_mask);
}

/**
 * @brief 游戏主状态机：更新（对应符号 Game_UpdateFailEyeBlink）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_UpdateFailEyeBlink(void)
{
    if (s_fail_eye_blink_mask == 0U)
    {
        return;
    }

    if (Gtim_IsElapsed(s_fail_eye_blink_start_ms, GAME_FAIL_BLINK_TOTAL_MS) != 0U)
    {
        Segment_EyeLedClearChannelMask(s_fail_eye_blink_mask);
        s_fail_eye_blink_mask = 0U;
        s_fail_eye_blink_visible = 0U;
        return;
    }

    if (Gtim_IsElapsed(s_fail_eye_blink_last_ms, GAME_FAIL_BLINK_STEP_MS) == 0U)
    {
        return;
    }

    s_fail_eye_blink_last_ms = Gtim_NowMs();
    if (s_fail_eye_blink_visible != 0U)
    {
        Segment_EyeLedClearChannelMask(s_fail_eye_blink_mask);
        s_fail_eye_blink_visible = 0U;
    }
    else
    {
        Segment_EyeLedSetChannelMask(s_fail_eye_blink_mask);
        s_fail_eye_blink_visible = 1U;
    }
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_PlayJackpotAwardAudio）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：audio_index；均只在本次调用中有效。
 * @param tier 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_PlayJackpotAwardAudio(uint8_t tier)
{
    uint8_t audio_index = 0U;

    if (tier == 5U)
    {
        audio_index = GAME_AUDIO_TRIG_JACKPOT_QUARTER;
    }
    else if (tier == 6U)
    {
        audio_index = GAME_AUDIO_TRIG_JACKPOT_HALF;
    }
    else if (tier == 7U)
    {
        audio_index = GAME_AUDIO_TRIG_JACKPOT_ALL;
    }

    if (audio_index != 0U)
    {
        (void)Audio_PlayLoudIndexOn(AUDIO_CHIP_1, audio_index, 0U);
    }
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_AudioSetBack）。
 * @调用关系 直接调用者：game.c:Game_AudioUpdateBack、game.c:Game_Task。
 * @param index 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_AudioSetBack(uint8_t index)
{
    if ((index == 0U) || (s_audio_back_index == index))
    {
        return;
    }

    s_audio_back_index = index;
    Audio_LoopIndexOn(AUDIO_CHIP_2, index);
}

/**
 * @brief 游戏主状态机：更新（对应符号 Game_AudioUpdateBack）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_AudioUpdateBack(void)
{
    if ((s_game_mode == GAME_MODE_POST) ||
        (s_game_mode == GAME_MODE_FAULT))
    {
        return;
    }

    if (s_line7_lock_active != 0U)
    {
        Game_AudioSetBack(GAME_AUDIO_BACK_LINE7);
    }
    else
    {
        Game_AudioSetBack(GAME_AUDIO_BACK_IDLE);
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 按尚未完成的业务和右屏余额选择下一游戏状态。
 * @note 彩金结算优先进入等待；抽奖或送灯未结束时进入LUCKY；其余按右屏余额进入READY或IDLE。
 */
static void Game_EnterIdleOrReady(void)
{
    s_state_start_ms = Gtim_NowMs();
    if (s_jackpot_settlement_phase != GAME_JACKPOT_SETTLEMENT_NONE)
    {
        s_game_mode = GAME_MODE_JACKPOT_WAIT;
    }
    else if ((Lottery_IsBusy() != 0U) || (GiftLight_IsBusy() != 0U))
    {
        s_game_mode = GAME_MODE_LUCKY;
    }
    else
    {
        s_game_mode = (s_game_coin != 0U) ? GAME_MODE_READY : GAME_MODE_IDLE;
    }
}

/**
 * @brief 缂佺喍绔存径鍕倞闂勩倛顔曠純顔兼嫲閺佸懘娈伴悩鑸碘偓浣割樆閺€璺哄煂閻ㄥ嫭婀侀弫鍫熷鐢浇鍓﹂崘灞傗偓? */
/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_ProcessCoinCount）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：next_coin、credit_count；均只在本次调用中有效。
 * @param coin_count 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_ProcessCoinCount(uint32_t coin_count)
{
    uint32_t next_coin;
    uint16_t credit_count;

    if (coin_count == 0U)
    {
        return;
    }

    Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_COIN_IN);
    credit_count = Game_CalcCreditFromCoinPulse(coin_count);
    if (credit_count != 0U)
    {
        next_coin = (uint32_t)s_game_coin + credit_count;
        if (next_coin > 9999U)
        {
            next_coin = 9999U;
        }
        Game_SetCoin((uint16_t)next_coin);
    }

    Meter_AddPulse(METER_COIN_IN, coin_count);
    Account_AddPhysicalCoinIn(coin_count);
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_ResetDropResultWait）。
 * @调用关系 直接调用者：game.c:Game_Init、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_ResetDropResultWait(void)
{
    s_drop_result_waiting = 0U;
    s_drop_result_wait_start_ms = 0U;
    s_drop_start_active_mask = 0U;
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_StartLine7Settlement）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_StartLine7Settlement(void)
{
    if ((s_line7_lock_active == 0U) ||
        (s_line7_settlement_started != 0U))
    {
        return;
    }

    if (Setting_IsCardPayoutEnabled() != 0U)
    {
        s_card_payout_requested = 1U;
    }

    s_line7_settlement_started = 1U;
    if (Game_BeginLineSettlement() == 0U)
    {
        s_line7_settlement_started = 0U;
        s_card_payout_requested = 0U;
    }
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_UpdateLine7Settlement）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_UpdateLine7Settlement(void)
{
    if ((s_game_mode == GAME_MODE_SETTING) || (s_game_mode == GAME_MODE_ACCOUNT)) return;
    if ((s_line7_lock_active == 0U) ||
        /*
         * 正常路径由Game_StartLine7Settlement置位started；同时允许修复旧路径留下的
         * started=0但分数已经结清的状态。只要左屏仍是7连固定奖，就绝不能提前解锁。
         */
        ((s_line7_settlement_started == 0U) && (s_game_score != 0U)) ||
        (s_jackpot_settlement_phase != GAME_JACKPOT_SETTLEMENT_NONE) ||
        (Account_IsCreditTransfer() != 0U) ||
        (s_line_payout_pending != 0U) ||
        (Payout_IsBusy() != 0U) ||
        (Ticket_IsBusy() != 0U) ||
        (s_card_payout_remaining != 0U) ||
        ((s_game_prize != 0U) && (Ticket_HasFault() == 0U)))
    {
        return;
    }

    s_line7_lock_active = 0U;
    s_line7_settlement_started = 0U;
    s_line7_hold_start_ms = 0U;
    (void)Key_TakePress(KEY_GAME);
    Game_EnterIdleOrReady();
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_MenuBlinkVisible）。
 * @调用关系 直接调用者：game.c:Game_ShowSettingPage、game.c:Game_ShowAccountPage。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_MenuBlinkVisible(void)
{
    return (((Gtim_NowMs() / GAME_MENU_BLINK_MS) & 1U) == 0U) ? 1U : 0U;
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_DiscardMenuPlayInputs）。
 * @调用关系 直接调用者：game.c:Game_EnterSettingMode、game.c:Game_EnterAccountPage、game.c:Game_LeaveMenuMode、game.c:Game_TaskSettingMode、game.c:Game_TaskAccountMode。
 * @局部状态 主要临时变量：counter_index；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_DiscardMenuPlayInputs(void)
{
    uint8_t counter_index;

    (void)Key_TakePress(KEY_EXCHANGE);
    (void)Key_TakePress(KEY_TICKET);
    (void)Key_TakePress(KEY_GAME);
    (void)Key_TakePress(KEY_CLEAR);
    (void)Eye_TakeChannelTriggerMask();
    (void)Eye_TakeCounterEventMask();
    for (counter_index = 0U; counter_index < (uint8_t)EYE_COUNTER_COUNT; counter_index++)
    {
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        if ((counter_index != (uint8_t)EYE_COUNTER_TICKET_OUT) ||
            (s_card_payout_remaining == 0U))
        {
            (void)Eye_TakeCounter((Eye_Counter)counter_index);
        }
    }
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_RestoreGameDisplay）。
 * @调用关系 直接调用者：game.c:Game_LeaveMenuMode、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_RestoreGameDisplay(void)
{
    Segment_ClearAll();
    Segment_SetLeftNumber(s_game_score);
    Segment_SetMiddleNumber(s_game_prize);
    Segment_SetRightNumber(s_game_coin);
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Segment_EyeLedClearAll();
    Segment_EyeLedSetChannelMask(s_coins_in_channel_mask);
}

/**
 * @brief 缺票时只让中屏在“欠票数/E14”之间交替，完全不改变游戏状态。
 * @note 一个完整周期2秒：前1秒显示E14，后1秒恢复真实中屏欠票数。
 */
static void Game_UpdateE14Display(void)
{
    uint32_t elapsed_ms;

    if (Fault_GetCode() != FAULT_CODE_E14)
    {
        if (s_e14_blink_active != 0U)
        {
            s_e14_blink_active = 0U;
            s_e14_blink_start_ms = 0U;
            if ((s_game_mode != GAME_MODE_POST) &&
                (s_game_mode != GAME_MODE_SETTING) &&
                (s_game_mode != GAME_MODE_ACCOUNT) &&
                (s_game_mode != GAME_MODE_FAULT))
            {
                Segment_SetMiddleNumber(s_game_prize);
            }
        }
        return;
    }

    /* 设置、查账等专用界面保持自身显示；回到游戏画面后继续E14提示。 */
    if ((s_game_mode == GAME_MODE_POST) ||
        (s_game_mode == GAME_MODE_SETTING) ||
        (s_game_mode == GAME_MODE_ACCOUNT) ||
        (s_game_mode == GAME_MODE_FAULT))
    {
        return;
    }

    if (s_e14_blink_active == 0U)
    {
        s_e14_blink_active = 1U;
        s_e14_blink_start_ms = Gtim_NowMs();
    }

    elapsed_ms = Gtim_NowMs() - s_e14_blink_start_ms;
    if ((elapsed_ms % GAME_E14_BLINK_PERIOD_MS) < GAME_E14_VISIBLE_MS)
    {
        Segment_SetMiddleENumber((uint8_t)FAULT_CODE_E14);
    }
    else
    {
        Segment_SetMiddleNumber(s_game_prize);
    }
}


/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_RestoreOutputsAfterFault）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_RestoreOutputsAfterFault(void)
{
    if (s_fault_wiper_was_running != 0U)
    {
        WiperMotor_Run();
    }

    if (s_fault_game_motor_was_running != 0U)
    {
        Motor_RunForward(MOTOR_GAME_COIN);
    }

    if (s_fault_payout_motor_was_running != 0U)
    {
        Motor_RunForward(MOTOR_WIN_PRIZE);
    }

    if (s_fault_ticket_was_running != 0U)
    {
        Ticket_Resume();
    }

    s_fault_game_motor_was_running = 0U;
    s_fault_payout_motor_was_running = 0U;
    s_fault_ticket_was_running = 0U;
    s_fault_wiper_was_running = 0U;
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_EnterSettingMode）。
 * @调用关系 直接调用者：game.c:Game_TaskAccountMode、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_EnterSettingMode(void)
{
    Game_DiscardMenuPlayInputs();
    s_setting_page_index = 0U;
    s_setting_value_selected = 0U;
    s_state_start_ms = Gtim_NowMs();
    s_game_mode = GAME_MODE_SETTING;
    Segment_ClearAll();
    Game_ShowSettingPage();
    Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_ENTER_SETTING);
}

/**
 * @brief 游戏主状态机：进入状态（对应符号 Game_EnterAccountMode）。
 * @调用关系 直接调用者：game.c:Game_TaskSettingMode、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_EnterAccountMode(void)
{
    Game_EnterAccountPage(1U);
}

/**
 * @brief 游戏主状态机：进入状态（对应符号 Game_EnterAccountPage）。
 * @调用关系 直接调用者：game.c:Game_EnterAccountMode、game.c:Game_Task。
 * @param page_number 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_EnterAccountPage(uint8_t page_number)
{
    if (s_game_mode == GAME_MODE_STANDBY)
    {
        Standby_Stop();
    }

    if ((page_number == 0U) || (page_number > GAME_ACCOUNT_PAGE_COUNT))
    {
        page_number = 1U;
    }

    Game_DiscardMenuPlayInputs();
    s_account_page_index = (uint8_t)(page_number - 1U);
    s_account_time_valid = 0U;
    s_account_time_edit_active = 0U;
    s_account_time_field_index = 0U;
    s_account_time_last_read_ms = 0U;
    s_state_start_ms = Gtim_NowMs();
    s_game_mode = GAME_MODE_ACCOUNT;
    Segment_ClearAll();
    Game_ShowAccountPage();
}

/**
 * @brief 游戏主状态机：退出状态（对应符号 Game_LeaveMenuMode）。
 * @调用关系 直接调用者：game.c:Game_TaskSettingMode、game.c:Game_TaskAccountMode、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_LeaveMenuMode(void)
{
    Game_DiscardMenuPlayInputs();
    s_account_time_edit_active = 0U;
    Game_RestoreGameDisplay();
    Game_EnterIdleOrReady();
}

/**
 * @brief 游戏主状态机：刷新显示（对应符号 Game_ShowValueOnMiddleRight）。
 * @调用关系 直接调用者：game.c:Game_ShowAccountPage。
 * @局部状态 主要临时变量：high、low；均只在本次调用中有效。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_ShowValueOnMiddleRight(uint32_t value)
{
    uint32_t high;
    uint16_t low;

    if (value > 99999999UL)
    {
        value = 99999999UL;
    }

    high = value / 10000UL;
    low = (uint16_t)(value % 10000UL);

    if (high == 0U)
    {
        Segment_ClearMiddle();
        Segment_SetRightNumber(low);
    }
    else
    {
        Segment_SetMiddleNumber((uint16_t)high);
        Segment_SetRightFixed4(low);
    }
}

/**
 * @brief 显示 C5“累计实际币差”。
 * @调用关系 直接调用者：game.c:Game_ShowAccountPage。
 * @note C5只按“实体投币－实体吐币”计算；为负时仅占用一个负号位。
 * @note 打码利润仍由 machine_lock.c 按全部出奖计算，两者不是同一个统计量。
 */
static void Game_ShowActualCoinDifference(void)
{
    Account_Data data;
    int64_t difference;

    Account_GetData(&data);
    if (data.physical_coin_in_total >= data.physical_coin_out_total)
    {
        difference = (int64_t)(data.physical_coin_in_total - data.physical_coin_out_total);
    }
    else
    {
        difference = -(int64_t)(data.physical_coin_out_total - data.physical_coin_in_total);
    }
    /* Segment接口只放一个负号，其余七位显示绝对值。 */
    Segment_SetMiddleRightSigned(difference);
}

static uint8_t Game_GetSettingParamIndex(void)
{
    if (s_setting_page_index >= GAME_SETTING_PAGE_COUNT)
    {
        return SETTING_PARAM_AWARD_MODE;
    }
    return s_setting_page_to_param[s_setting_page_index];
}

/**
 * @brief 读取C1~C50；通过映射复用旧字段，升级时不移动EEPROM数据。
 * @param page_index 查账页0基下标，范围0~49。
 * @retval 页面值；C48版本采用MMDDNNNN规则。
 * @note 由 Game_ShowAccountPage 调用；固件版本不存入账本，不受清账影响。
 */
static uint32_t Game_GetAccountPageValue(uint8_t page_index)
{
    Account_Data data;                            // 当前账本快照，无量纲
    uint64_t payout_rate;                         // 精细返奖率，单位：0.01%
    GiftAlgorithm_Diagnostics diagnostics;        // 新返奖算法诊断，无量纲
    uint64_t value = 0U;                          // 页面显示值，无量纲
    uint8_t i;                                    // 奖型索引，数组下标

    if (page_index >= GAME_ACCOUNT_PAGE_COUNT) return 0U;
    if (page_index >= 50U)
    {
        GiftAlgorithm_GetDiagnostics(s_coins_in_channel_mask, &diagnostics);
        switch (page_index)
        {
            case 50U: return diagnostics.coin_budget_before_cents;
            case 51U: return diagnostics.coin_granted;
            case 52U: return diagnostics.recovery_stakes;
            case 53U: return diagnostics.recovery_funded_cents;
            case 54U: return diagnostics.recovery_paid;
            case 55U: return diagnostics.rejected_grants;
            default: return 0U;
        }
    }
    page_index = s_account_page_to_legacy[page_index];
    if (page_index == 47U) return GAME_FIRMWARE_VERSION;

    Account_GetData(&data);
    if (page_index == 48U) return data.lottery_coin3_value_total;
    if (page_index == 49U) return data.lottery_coin5_value_total;
    if (page_index == 50U) return data.lottery_coin10_value_total;

    /* 以下仍按旧页下标读取字段。显示顺序由顶部映射表统一调整。
     * 四本新账固定C1~C4；旧主账原始值只在C49/C50历史页展示。 */
    if (page_index == 0U) return data.physical_coin_in_total;
    if (page_index == 1U) return data.physical_coin_out_total;
    if (page_index == 2U) return Account_GetStakeSinceEpoch(&data);
    if (page_index == 3U) return Account_GetAwardSinceEpoch(&data);
    if (page_index == 45U) return data.coin_in_total;
    if (page_index == 46U) return data.payout_total;
    page_index = (uint8_t)(page_index - 2U);

    if (page_index >= 15U)
    {
        GiftAlgorithm_GetDiagnostics(s_coins_in_channel_mask, &diagnostics);
        switch (page_index + 1U)
        {
            case 16U: value = data.game_use_total; break;
            case 17U: value = data.return_commitment_total; break;
            case 18U: value = Account_CalcPayoutEquivalent(&data); break;
            case 19U: case 20U: return 0U; /* 有符号余额与预算由显示层处理 */
            case 21U: value = diagnostics.reserved; break;
            case 22U: value = diagnostics.recovery; break;
            case 23U: value = diagnostics.effective_percent; break;
            case 24U: value = diagnostics.award_count[GIFT_ALGORITHM_AWARD_COIN_3]; break;
            case 25U: value = diagnostics.award_count[GIFT_ALGORITHM_AWARD_COIN_5]; break;
            case 26U: value = diagnostics.award_count[GIFT_ALGORITHM_AWARD_COIN_10]; break;
            case 27U: value = diagnostics.award_count[GIFT_ALGORITHM_AWARD_GIFT_1]; break;
            case 28U: value = diagnostics.award_count[GIFT_ALGORITHM_AWARD_GIFT_2]; break;
            case 29U:
                value = diagnostics.award_count[GIFT_ALGORITHM_AWARD_GIFT_3] +
                        diagnostics.award_count[GIFT_ALGORITHM_AWARD_GIFT_4] +
                        diagnostics.award_count[GIFT_ALGORITHM_AWARD_GIFT_5] +
                        diagnostics.award_count[GIFT_ALGORITHM_AWARD_GIFT_6] +
                        diagnostics.award_count[GIFT_ALGORITHM_AWARD_GIFT_7];
                break;
            case 30U: value = diagnostics.award_count[GIFT_ALGORITHM_AWARD_NONE]; break;
            case 31U: value = diagnostics.guarantee_count; break;
            case 32U: value = diagnostics.limited_count; break;
            case 33U:
                for (i = 0U; i < GIFT_ALGORITHM_AWARD_TYPE_COUNT; i++)
                    value += diagnostics.award_count[i];
                break;
            case 34U: value = diagnostics.last_reason; break;
            case 35U: value = diagnostics.last_drawn; break;
            case 36U: value = diagnostics.miss_streak; break;
            case 37U: value = diagnostics.guarantee_threshold; break;
            case 38U:
                value = Setting_GetValue(SETTING_PARAM_LOTTERY_PAYOUT_PERCENT);
                break;
            case 39U:
                value = Setting_GetValue(SETTING_PARAM_RECOVERY_DROP);
                break;
            case 40U: value = 15U; break; /* E池收支与一次性发奖校验 */
            case 41U: /* 原精细C41现为C43，9653表示96.53%，仅显示不参与控奖 */
                if (Account_GetStakeSinceEpoch(&data) != 0U)
                    value = (Account_CalcPayoutValueScaled(&data) / 100U) /
                            Account_GetStakeSinceEpoch(&data);
                break;
            case 42U: /* 待核对旧联机彩金的档位：0、5、6、7、56、57、67、567。 */
                for (i = 0U; i < 3U; i++)
                    if ((Account_GetOrphanJackpotMask() & (1U << i)) != 0U)
                        value = value * 10U + 5U + i;
                break;
            case 43U: value = Account_IsMixedLedger(); break;
            default: break;
        }
        return (value > 99999999ULL) ? 99999999U : (uint32_t)value;
    }

    switch (page_index)
    {
        case 2U:
            return 0U; /* 旧C3现在是C5，有符号值由显示层处理。 */

        case 3U:
            return data.ticket_out_total;

        case 4U:
            return data.line_ticket_total;

        case 5U:
            return data.game_round_total;

        case 6U:
            return data.card_out_total;

        case 7U:
            return data.lottery_count_total;

        case 8U:
            return data.lottery_payout_total;

        case 9U:
            if (Account_GetStakeSinceEpoch(&data) == 0U)
            {
                return 0U;
            }
            payout_rate = Account_CalcPayoutRatePercent(&data);
            if (payout_rate > 99999999ULL)
            {
                payout_rate = 99999999ULL;
            }
            return (uint32_t)payout_rate;

        case 11U:
            return Jackpot_GetLocalPayoutTotal();

        case 12U:
            return Jackpot_GetCurrent();

        case 13U:
            return data.gift_light_total;

        case 14U:
            return data.gift_win_value_total;

        default:
            return 0U;
    }
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_CalcCreditFromCoinPulse）。
 * @调用关系 直接调用者：game.c:Game_ProcessCoinCount。
 * @局部状态 主要临时变量：coin_to_point、coins_per_point、total_coin、credit_count；均只在本次调用中有效。
 * @param coin_count 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Game_CalcCreditFromCoinPulse(uint32_t coin_count)
{
    uint16_t coin_to_point;
    uint16_t coins_per_point;
    uint32_t total_coin;
    uint32_t credit_count;

    if (coin_count == 0U)
    {
        return 0U;
    }

    coin_to_point = Setting_GetCoinToPoint();
    coins_per_point = Setting_GetCoinsPerPoint();

    if (coin_to_point > 1U)
    {
        s_coin_input_remainder = 0U;
        credit_count = coin_count * (uint32_t)coin_to_point;
    }
    else if (coins_per_point > 1U)
    {
        total_coin = (uint32_t)s_coin_input_remainder + coin_count;
        credit_count = total_coin / (uint32_t)coins_per_point;
        s_coin_input_remainder = (uint16_t)(total_coin % (uint32_t)coins_per_point);
    }
    else
    {
        s_coin_input_remainder = 0U;
        credit_count = coin_count;
    }

    return (credit_count > 9999U) ? 9999U : (uint16_t)credit_count;
}

/**
 * @brief 游戏主状态机：执行内部处理（对应符号 Game_CountMaskBits）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：count；均只在本次调用中有效。
 * @param mask 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Game_CountMaskBits(uint16_t mask)
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
 * @brief 游戏主状态机：清除（对应符号 Game_ProcessClearAccountRequest）。
 * @调用关系 直接调用者：game.c:Game_ApplySettingExitActions、game.c:Game_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_ProcessClearAccountRequest(void)
{
    uint8_t clear_requested;
    uint8_t factory_requested;

    clear_requested = (Setting_GetValue(SETTING_PARAM_CLEAR_ACCOUNT) != 0U) ? 1U : 0U;
    factory_requested = (Setting_GetValue(SETTING_PARAM_FACTORY_RESET) != 0U) ? 1U : 0U;
    if ((clear_requested == 0U) && (factory_requested == 0U))
    {
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if ((s_game_mode == GAME_MODE_POST) ||
        (s_game_mode == GAME_MODE_FAULT) ||
        (Lottery_IsBusy() != 0U) ||
        (GiftLight_IsBusy() != 0U) ||
        (Payout_IsBusy() != 0U) ||
        (Ticket_IsBusy() != 0U) ||
        (s_line_payout_pending != 0U) ||
        (s_jackpot_settlement_phase != GAME_JACKPOT_SETTLEMENT_NONE) ||
        (s_ticket_display_remaining != 0U) ||
        (s_card_payout_requested != 0U) ||
        (s_card_payout_remaining != 0U) ||
        (s_game_prize != 0U) ||
        (s_line7_lock_active != 0U) ||
        (s_line7_settlement_started != 0U))
    {
        return;
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (Lottery_ResetForAccountClear() == 0U)
    {
        return;
    }
    Account_ClearBookData();
    if (Account_IsReady() == 0U)
    {
        return;
    }

    if (factory_requested != 0U)
    {
        /*
         * 原P9（隐藏页归并后为现场P7）只恢复普通P参数、清查账和
         * 恢复A0默认密码。
         * MachineLock记录中的锁定状态、A1、总运行/周期时间、打码次数均不动；
         * Setting_RestoreDefault也会保留P29的单机/联机配置与机台号。
         */
        Setting_RestoreDefault();
#if (MACHINE_LOCK_FEATURE_ENABLE != 0U)
        MachineLock_ResetStaffPassword();
#endif
    }
    else
    {
        Setting_SetValue(SETTING_PARAM_CLEAR_ACCOUNT, 0U);
    }
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_ApplySettingExitActions）。
 * @调用关系 直接调用者：game.c:Game_TaskSettingMode、game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_ApplySettingExitActions(void)
{
    Game_ProcessClearAccountRequest();

    /* 离开设置菜单后无需继续保留 P6/P7 的界面确认状态。 */
    s_setting_action_ack_param = GAME_SETTING_ACTION_ACK_NONE;
    s_coin_input_remainder = 0U;
}

/**
 * @brief 在本机设置菜单中记录 P6/P7 的“已置 1”确认显示。
 * @param param_index 当前调整的 Setting 参数下标。
 * @note 清账或恢复默认仍由 Game_ProcessClearAccountRequest 立即执行；
 *       本函数只改变界面显示状态，不改变业务数据和 EEPROM 执行时序。
 */
static void Game_UpdateSettingActionAcknowledge(uint8_t param_index)
{
    if ((param_index != SETTING_PARAM_CLEAR_ACCOUNT) &&
        (param_index != SETTING_PARAM_FACTORY_RESET))
    {
        return;
    }

    if (Setting_GetValue(param_index) != 0U)
    {
        s_setting_action_ack_param = param_index;
        s_setting_action_ack_start_ms = Gtim_NowMs();
    }
    else if (s_setting_action_ack_param == param_index)
    {
        /* 操作人员主动把 1 改回 0 时，不再保留确认显示。 */
        s_setting_action_ack_param = GAME_SETTING_ACTION_ACK_NONE;
    }
}

/**
 * @brief 游戏主状态机：设置（对应符号 Game_ShowSettingPage）。
 * @调用关系 直接调用者：game.c:Game_EnterSettingMode、game.c:Game_TaskSettingMode、game.c:Game_Task。
 * @局部状态 主要临时变量：blink_visible；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_ShowSettingPage(void)
{
    uint8_t blink_visible;
    uint8_t param_index;
    uint8_t action_ack_visible;

    blink_visible = Game_MenuBlinkVisible();
    param_index = Game_GetSettingParamIndex();
    action_ack_visible = 0U;

    if (s_setting_action_ack_param != GAME_SETTING_ACTION_ACK_NONE)
    {
        if (Gtim_IsElapsed(s_setting_action_ack_start_ms,
                           GAME_SETTING_ACTION_ACK_MS) != 0U)
        {
            s_setting_action_ack_param = GAME_SETTING_ACTION_ACK_NONE;
        }
        else if (s_setting_action_ack_param == param_index)
        {
            action_ack_visible = 1U;
        }
    }

    if ((s_setting_value_selected == 0U) && (blink_visible == 0U))
    {
        Segment_ClearLeft();
    }
    else
    {
        Segment_SetLeftPNumber((uint8_t)(s_setting_page_index + 1U));
    }

    if (param_index == SETTING_PARAM_VOLUME)
    {
        if ((s_setting_value_selected == 1U) && (blink_visible == 0U))
        {
            Segment_ClearMiddle();
        }
        else
        {
            Segment_SetMiddleNumber(Setting_GetBackgroundVolume());
        }

        if ((s_setting_value_selected == 2U) && (blink_visible == 0U))
        {
            Segment_ClearRight();
        }
        else
        {
            Segment_SetRightNumber(Setting_GetEffectVolume());
        }
        return;
    }

    if (param_index == SETTING_PARAM_CARD_RATIO)
    {
        if ((s_setting_value_selected == 1U) && (blink_visible == 0U))
        {
            Segment_ClearMiddle();
        }
        else
        {
            Segment_SetMiddleNumber(Setting_GetCardCoinsPerBatch());
        }

        if ((s_setting_value_selected == 2U) && (blink_visible == 0U))
        {
            Segment_ClearRight();
        }
        else
        {
            Segment_SetRightNumber(Setting_GetCardsPerBatch());
        }
        return;
    }

    if (param_index == SETTING_PARAM_RS485_ADDRESS)
    {
        if ((s_setting_value_selected == 1U) && (blink_visible == 0U))
        {
            Segment_ClearMiddle();
        }
        else
        {
            Segment_SetMiddleNumber(Setting_IsNetworkEnabled());
        }

        if ((s_setting_value_selected == 2U) && (blink_visible == 0U))
        {
            Segment_ClearRight();
        }
        else
        {
            Segment_SetRightNumber(Setting_GetMachineId());
        }
        return;
    }

    Segment_ClearMiddle();

    if (action_ack_visible != 0U)
    {
        /* 即使后台已经把一次性参数清零，这 3 秒仍稳定显示 1。 */
        Segment_SetRightNumber(1U);
    }
    else if ((s_setting_value_selected != 0U) && (blink_visible == 0U))
    {
        Segment_ClearRight();
    }
    else
    {
        Segment_SetRightNumber(Setting_GetValue(param_index));
    }
}

/**
 * @brief 游戏主状态机：刷新显示（对应符号 Game_ShowAccountPage）。
 * @调用关系 直接调用者：game.c:Game_EnterAccountPage、game.c:Game_TaskAccountMode、game.c:Game_Task。
 * @局部状态 主要临时变量：blink_visible、show_middle、show_right、month_day、hour_minute；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_ShowAccountPage(void)
{
    uint8_t legacy_index;
    uint8_t blink_visible;
    uint8_t show_middle;
    uint8_t show_right;
    uint16_t month_day;
    uint16_t hour_minute;

    legacy_index = s_account_page_to_legacy[s_account_page_index];
    blink_visible = Game_MenuBlinkVisible();
    show_middle = 1U;
    show_right = 1U;

    if ((s_account_time_edit_active == 0U) && (blink_visible == 0U))
    {
        Segment_ClearLeft();
    }
    else
    {
        Segment_SetLeftCNumber((uint8_t)(s_account_page_index + 1U));
    }

    if (Game_IsAccountTimePage() != 0U)
    {
        if ((s_account_time_valid == 0U) ||
            ((s_account_time_edit_active == 0U) &&
             (Gtim_IsElapsed(s_account_time_last_read_ms, GAME_ACCOUNT_TIME_READ_MS) != 0U)))
        {
            s_account_time_last_read_ms = Gtim_NowMs();
            (void)Game_LoadAccountTimeForDisplay();
        }

        if (s_account_time_valid == 0U)
        {
            Segment_SetMiddleFixed4(0U);
            Segment_SetRightFixed4(Pcf8563_GetLastErrorCode());
            return;
        }

        if ((s_account_time_edit_active != 0U) && (blink_visible == 0U))
        {
            if (s_account_page_index == GAME_ACCOUNT_DATE_PAGE_INDEX)
            {
                if (s_account_time_field_index == 0U)
                {
                    show_middle = 0U;
                }
                else
                {
                    show_right = 0U;
                }
            }
            else
            {
                if (s_account_time_field_index == 2U)
                {
                    show_right = 0U;
                }
                else
                {
                    show_middle = 0U;
                }
            }
        }

        if (s_account_page_index == GAME_ACCOUNT_DATE_PAGE_INDEX)
        {
            month_day = (uint16_t)(((uint16_t)s_account_time_cache.month * 100U) +
                                   s_account_time_cache.day);
            if (show_middle != 0U)
            {
                Segment_SetMiddleFixed4(s_account_time_cache.year);
            }
            else
            {
                Segment_ClearMiddle();
            }

            if (show_right != 0U)
            {
                Segment_SetRightFixed4(month_day);
            }
            else
            {
                Segment_ClearRight();
            }
        }
        else
        {
            hour_minute = (uint16_t)(((uint16_t)s_account_time_cache.hour * 100U) +
                                     s_account_time_cache.minute);
            if (show_middle != 0U)
            {
                Segment_SetMiddleFixed4(hour_minute);
            }
            else
            {
                Segment_ClearMiddle();
            }

            if (show_right != 0U)
            {
                Segment_SetRightFixed4((uint16_t)s_account_time_cache.second);
            }
            else
            {
                Segment_ClearRight();
            }
        }
        return;
    }

    if (legacy_index == 4U)
    {
        Game_ShowActualCoinDifference();
        return;
    }

    if ((legacy_index == 20U) || (legacy_index == 21U))
    {
        GiftAlgorithm_Diagnostics diagnostics;
        GiftAlgorithm_GetDiagnostics(s_coins_in_channel_mask, &diagnostics);
        Segment_SetMiddleRightSigned((legacy_index == 20U) ?
            diagnostics.ledger_gap : diagnostics.available);
        return;
    }
    Game_ShowValueOnMiddleRight(Game_GetAccountPageValue(s_account_page_index));
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 游戏主状态机：周期处理（对应符号 Game_TaskSettingMode）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_TaskSettingMode(void)
{
    uint8_t param_index;

    Game_DiscardMenuPlayInputs();
    param_index = Game_GetSettingParamIndex();

    if (Key_TakePress(KEY_SET) != 0U)
    {
        Game_ApplySettingExitActions();
        Game_LeaveMenuMode();
        return;
    }

    if (Key_TakePress(KEY_ACCOUNT) != 0U)
    {
        Game_ApplySettingExitActions();
        Game_EnterAccountMode();
        return;
    }

    if (Key_TakePress(KEY_LEFT) != 0U)
    {
        if ((param_index == SETTING_PARAM_VOLUME) ||
            (param_index == SETTING_PARAM_CARD_RATIO) ||
            (param_index == SETTING_PARAM_RS485_ADDRESS))
        {
            /* P13/P15瀹革箓鏁崷銊⑩偓娣х紓鏍у娇閳ユ繂鎷版稉顓炵潌鐎涙顔屾稊瀣？閸掑洦宕查柅澶嬪閵?*/
            s_setting_value_selected =
                (s_setting_value_selected == 1U) ? 0U : 1U;
        }
        else
        {
            s_setting_value_selected = 0U;
        }
    }

    if (Key_TakePress(KEY_RIGHT) != 0U)
    {
        if ((param_index == SETTING_PARAM_VOLUME) ||
            (param_index == SETTING_PARAM_CARD_RATIO) ||
            (param_index == SETTING_PARAM_RS485_ADDRESS))
        {
            /* P13/P15閸欐娊鏁崷銊⑩偓娣х紓鏍у娇閳ユ繂鎷伴崣鍐茬潌鐎涙顔屾稊瀣？閸掑洦宕查柅澶嬪閵?*/
            s_setting_value_selected =
                (s_setting_value_selected == 2U) ? 0U : 2U;
        }
        else
        {
            s_setting_value_selected = 1U;
        }
    }

    if (Key_TakePress(KEY_UP) != 0U)
    {
        if (s_setting_value_selected == 0U)
        {
            if (s_setting_page_index == 0U)
            {
                s_setting_page_index = (uint8_t)(GAME_SETTING_PAGE_COUNT - 1U);
            }
            else
            {
                s_setting_page_index--;
            }
        }
        else
        {
            if (param_index == SETTING_PARAM_VOLUME)
            {
                if (s_setting_value_selected == 1U)
                {
                    Setting_AdjustBackgroundVolume(0U);
                }
                else
                {
                    (void)Setting_AdjustValue(SETTING_PARAM_VOLUME, 0U);
                }
            }
            else if (param_index == SETTING_PARAM_CARD_RATIO)
            {
                Setting_AdjustCardRatio(
                    (s_setting_value_selected == 2U) ? 1U : 0U,
                    0U);
            }
            else if (param_index == SETTING_PARAM_RS485_ADDRESS)
            {
                if (s_setting_value_selected == 1U)
                {
                    Setting_SetNetworkConfig(
                        (Setting_IsNetworkEnabled() == 0U) ? 1U : 0U,
                        Setting_GetMachineId());
                }
                else
                {
                    Setting_SetNetworkConfig(
                        Setting_IsNetworkEnabled(),
                        (Setting_GetMachineId() == 0U) ?
                        MACHINE_ADDRESS_MAX_ID :
                        (uint8_t)(Setting_GetMachineId() - 1U));
                }
            }
            else if (((param_index != SETTING_PARAM_JACKPOT_BASE) &&
                      (param_index != SETTING_PARAM_JACKPOT_COINS_PER_POINT)) ||
                (Jackpot_IsBaseEditable() != 0U))
            {
                (void)Setting_AdjustValue(param_index, 0U);
                Game_UpdateSettingActionAcknowledge(param_index);
            }
        }
    }

    if (Key_TakePress(KEY_DOWN) != 0U)
    {
        if (s_setting_value_selected == 0U)
        {
            s_setting_page_index++;
            if (s_setting_page_index >= GAME_SETTING_PAGE_COUNT)
            {
                s_setting_page_index = 0U;
            }
        }
        else
        {
            if (param_index == SETTING_PARAM_VOLUME)
            {
                if (s_setting_value_selected == 1U)
                {
                    Setting_AdjustBackgroundVolume(1U);
                }
                else
                {
                    (void)Setting_AdjustValue(SETTING_PARAM_VOLUME, 1U);
                }
            }
            else if (param_index == SETTING_PARAM_CARD_RATIO)
            {
                Setting_AdjustCardRatio(
                    (s_setting_value_selected == 2U) ? 1U : 0U,
                    1U);
            }
            else if (param_index == SETTING_PARAM_RS485_ADDRESS)
            {
                if (s_setting_value_selected == 1U)
                {
                    Setting_SetNetworkConfig(
                        (Setting_IsNetworkEnabled() == 0U) ? 1U : 0U,
                        Setting_GetMachineId());
                }
                else
                {
                    Setting_SetNetworkConfig(
                        Setting_IsNetworkEnabled(),
                        (Setting_GetMachineId() >= MACHINE_ADDRESS_MAX_ID) ?
                        0U : (uint8_t)(Setting_GetMachineId() + 1U));
                }
            }
            else if (((param_index != SETTING_PARAM_JACKPOT_BASE) &&
                      (param_index != SETTING_PARAM_JACKPOT_COINS_PER_POINT)) ||
                (Jackpot_IsBaseEditable() != 0U))
            {
                (void)Setting_AdjustValue(param_index, 1U);
                Game_UpdateSettingActionAcknowledge(param_index);
            }
        }
    }

    Game_ShowSettingPage();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 游戏主状态机：周期处理（对应符号 Game_TaskAccountMode）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：time_page；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Game_TaskAccountMode(void)
{
    uint8_t time_page;

    Game_DiscardMenuPlayInputs();
    time_page = Game_IsAccountTimePage();

    if (Key_TakePress(KEY_ACCOUNT) != 0U)
    {
        s_account_time_edit_active = 0U;
        Game_LeaveMenuMode();
        return;
    }

    if (Key_TakePress(KEY_SET) != 0U)
    {
        s_account_time_edit_active = 0U;
        Game_EnterSettingMode();
        return;
    }

    if (time_page != 0U)
    {
        if (Key_TakePress(KEY_LEFT) != 0U)
        {
            if (s_account_time_edit_active == 0U)
            {
                s_account_time_edit_active = 1U;
                s_account_time_field_index = 0U;
            }
            else if (s_account_time_field_index == 0U)
            {
                s_account_time_edit_active = 0U;
            }
            else
            {
                s_account_time_field_index--;
            }
        }

        if (Key_TakePress(KEY_RIGHT) != 0U)
        {
            if (s_account_time_edit_active == 0U)
            {
                s_account_time_edit_active = 1U;
                s_account_time_field_index = 0U;
            }
            else
            {
                s_account_time_field_index++;
                if (s_account_time_field_index >= GAME_ACCOUNT_TIME_FIELD_COUNT)
                {
                    s_account_time_edit_active = 0U;
                    s_account_time_field_index = 0U;
                }
            }
        }
    }
    else
    {
        (void)Key_TakePress(KEY_LEFT);
        (void)Key_TakePress(KEY_RIGHT);
        s_account_time_edit_active = 0U;
    }

    if (Key_TakePress(KEY_UP) != 0U)
    {
        if ((time_page != 0U) && (s_account_time_edit_active != 0U))
        {
            Game_AdjustAccountTime(0U);
        }
        else
        {
            if (s_account_page_index == 0U)
            {
                s_account_page_index = (uint8_t)(GAME_ACCOUNT_PAGE_COUNT - 1U);
            }
            else
            {
                s_account_page_index--;
            }
            s_account_time_valid = 0U;
            s_account_time_edit_active = 0U;
            s_account_time_field_index = 0U;
        }
    }

    if (Key_TakePress(KEY_DOWN) != 0U)
    {
        if ((time_page != 0U) && (s_account_time_edit_active != 0U))
        {
            Game_AdjustAccountTime(1U);
        }
        else
        {
            s_account_page_index++;
            if (s_account_page_index >= GAME_ACCOUNT_PAGE_COUNT)
            {
                s_account_page_index = 0U;
            }
            s_account_time_valid = 0U;
            s_account_time_edit_active = 0U;
            s_account_time_field_index = 0U;
        }
    }

    Game_ShowAccountPage();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 游戏主状态机：初始化（对应符号 Game_Init）。
 * @调用关系 直接调用者：app_main.c:App_Init。
 * @局部状态 主要临时变量：saved_score、saved_prize、saved_coin、saved_card_count、saved_card_x、saved_card_y；均只在本次调用中有效。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Game_Init(void)
{
    uint16_t saved_score;
    uint16_t saved_prize;
    uint16_t saved_coin;
    uint32_t saved_card_count;
    uint16_t saved_card_x;
    uint16_t saved_card_y;

    Account_Init();
    Setting_Init();
    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    Account_SetTicketConversionP6(Setting_GetTicketsPerCoin());
    Jackpot_Init();
#if (MACHINE_LOCK_FEATURE_ENABLE != 0U)
    MachineLock_Init();
#endif
    s_jackpot_recovery_tier = Jackpot_GetPendingTier();
    Game_RestoreRtcFromAccount();
    Account_GetGameState(&saved_score, &saved_prize, &saved_coin);
    Account_GetCardPayoutPending(&saved_card_count,
                                 &saved_card_x,
                                 &saved_card_y);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    s_game_score = saved_score;
    s_game_prize = saved_prize;
    s_game_coin = saved_coin;
    s_card_payout_remaining = saved_card_count;
    s_card_payout_x = saved_card_x;
    s_card_payout_y = saved_card_y;
    WiperMotor_Stop();
    Ticket_Init();
    Score_Init();
    Lottery_Init();
    GiftLight_Init();
    Payout_Init();
    Fault_Init();
    s_line_payout_pending = 0U;
    s_ticket_display_remaining = 0U;
    s_ticket_clear_hold_start_ms = 0U;
    s_ticket_clear_hold_active = 0U;
    s_ticket_clear_wait_release = 0U;
    s_card_payout_requested = 0U;
    s_line_payout_hold_start_ms = 0U;
    s_line7_lock_active = 0U;
    s_line7_settlement_started = 0U;
    s_line7_hold_start_ms = 0U;
    s_setting_action_ack_param = GAME_SETTING_ACTION_ACK_NONE;
    s_setting_action_ack_start_ms = 0U;
    s_fail_eye_blink_mask = 0U;
    s_fail_eye_blink_visible = 0U;
    s_fail_eye_blink_start_ms = 0U;
    s_fail_eye_blink_last_ms = 0U;
    Game_ResetDropResultWait();
    s_coins_in_channel_mask = 0U;
    s_state_start_ms = Gtim_NowMs();
    s_game_mode = GAME_MODE_POST;
#ifdef BOOT_APP_BASE
    s_upgrade_post_checked = 0U;
#endif
    s_fault_resume_mode = GAME_MODE_IDLE;
    s_fault_game_motor_was_running = 0U;
    s_fault_payout_motor_was_running = 0U;
    s_fault_ticket_was_running = 0U;
    s_fault_wiper_was_running = 0U;
    s_audio_back_index = 0U;
    s_e15_background_stopped = 0U;
    s_award_lamp_payout_started = 0U;
    s_lottery_award_output_active = 0U;
    s_fail_sequence = GAME_FAIL_SEQUENCE_NONE;
    s_round_has_gift_light = 0U;
    s_jackpot_settlement_phase = GAME_JACKPOT_SETTLEMENT_NONE;
    s_jackpot_tier_pending = 0U;
    s_jackpot_round_had_gift = 0U;
    s_settlement_checkpoint_pending = 0U;
    JackpotDisplay_SetBlink(0U);
    Standby_Init();

    Segment_ClearAll();
    Segment_EyeLedClearAll();
    SelfTest_Init();
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 执行一次游戏主循环并推进全部业务状态机。
 * @note 送灯确认事件与实体落币共用本局灯位和连线计算；LUCKY期间丢弃游戏操作键。
 */
void Game_Task(void)
{
    uint8_t counter_index;                        // 计数光眼索引，数组下标
    uint32_t coin_count;                          // 本轮投币数量，单位：币
    uint32_t motor_count;                         // 掉币电机光眼计数，单位：次
    uint16_t drop_mask;                           // 本次落入通道，寄存器位掩码
    uint16_t active_drop_mask;                    // 当前有效落币通道，寄存器位掩码
    uint16_t middle_drop_mask;                    // 2~8号落币通道，寄存器位掩码
    uint16_t line_score;                          // 当前连线固定奖，单位：币
    uint8_t gift_count;                           // 本次送灯数量，单位：盏
    uint16_t gift_target_mask;                    // 全部送灯目标，寄存器位掩码
    uint16_t gift_first_mask;                     // 第一盏送灯目标，寄存器位掩码
    uint16_t gift_second_mask;                    // 第二盏送灯目标，寄存器位掩码
    uint16_t lottery_coin_award;                  // 抽奖直接币奖，单位：币
    uint16_t gift_confirmed_mask;                 // 本轮确认送灯，寄存器位掩码
    uint16_t jackpot_award;                       // 待领取彩金，单位：币
    uint16_t paid_count;                          // 已确认吐币数量，单位：币
    uint32_t ticket_count;                        // 已确认出票数量，单位：张
    uint32_t motor_start_primask;                 // 启动电机前中断状态，寄存器值
    uint8_t remote_command_enable;                // 遥控命令允许标志，无量纲
    uint8_t remote_account_page;                  // 遥控查账页码，范围：0~47
    uint8_t remote_fault_clear_request;           // 遥控清故障请求，无量纲
#if (MACHINE_LOCK_FEATURE_ENABLE != 0U)
    uint8_t machine_lock_safe;                    // 允许切换锁机状态标志，无量纲
    uint8_t machine_lock_was_blocking;            // 上轮锁机标志，无量纲
    uint8_t machine_lock_blocking;                // 当前锁机标志，无量纲
#endif

    Gtim_Task();
    Segment_Task();
    Audio_Task();
    AwardLamp_Task();
#if (GAME_RTC_UART_DEBUG_ENABLE != 0U)
    Game_PrintRtcToUart();
#endif
    if (s_game_mode != GAME_MODE_START)
        Game_RtcBackupTask();
    remote_command_enable =
        ((s_game_mode == GAME_MODE_IDLE) ||
         (s_game_mode == GAME_MODE_STANDBY) ||
         (s_game_mode == GAME_MODE_READY) ||
         (s_game_mode == GAME_MODE_SETTING) ||
         (s_game_mode == GAME_MODE_ACCOUNT)) ? 1U : 0U;
    RemoteConfig_Task(remote_command_enable);
    /* Only leave menu modes. Do not reset POST, payout, jackpot wait or faults.
     * MachineLock still runs below and retains control when the machine is locked. */
    if (RemoteConfig_TakeExitRequest() != 0U)
    {
        if (s_game_mode == GAME_MODE_SETTING)
        {
            Game_ApplySettingExitActions();
            Game_LeaveMenuMode();
        }
        else if (s_game_mode == GAME_MODE_ACCOUNT)
        {
            Game_LeaveMenuMode();
        }
        else if ((s_game_mode == GAME_MODE_IDLE) ||
                 (s_game_mode == GAME_MODE_READY))
        {
            Game_RestoreGameDisplay();
        }
    }
    remote_account_page = RemoteConfig_TakeAccountPageRequest();
    remote_fault_clear_request = 0U;
    if (remote_account_page != 0U)
    {
        Game_EnterAccountPage(remote_account_page);
    }

    if (Gtim_Take2msFlag() != 0U)
    {
        Eye_ScanChannels2ms();
    }

    if (Gtim_Take10msFlag() != 0U)
    {
        Key_Scan10ms();
    }

    Coin_Task();
    Eye_ExtiTask();
    Shake_Task();
    Solenoid_Task();
    Motor_Task();
    WiperMotor_Task();
    Meter_Task();
    Ticket_Task();
    /* 閺佸懘娈伴張鐔兼？閸愯崵绮ㄩ幎钘夘殯闂婅櫕鏅ラ悩鑸碘偓渚婄礉闁灝鍘ら弲顕€鈧岸鐓堕弫鍫ｎ洬閻?E15 閻ㄥ嫬鎯婇悳顖涙閸斻劍濮ょ拃锕傜叾閵?*/
    if (s_game_mode != GAME_MODE_FAULT)
    {
        Lottery_Task();
        GiftLight_Task();
    }

    ticket_count = Ticket_TakeDispensedCount();
    if (ticket_count != 0U)
    {
        Meter_AddPulse(METER_TICKET_OUT, ticket_count);
    }

    if ((ticket_count != 0U) && (s_card_payout_remaining != 0U))
    {
        uint32_t card_confirmed;                  // 已确认卡票数量，单位：张

        card_confirmed = ticket_count;
        if (card_confirmed > s_card_payout_remaining)
        {
            card_confirmed = s_card_payout_remaining;
        }

        Account_AddCardOut(card_confirmed, s_card_payout_x, s_card_payout_y);
        s_card_payout_remaining -= card_confirmed;
        if (s_card_payout_remaining == 0U)
        {
            s_card_payout_x = 0U;
            s_card_payout_y = 0U;
        }
        Account_SetCardPayoutPending(s_card_payout_remaining,
                                     s_card_payout_x,
                                     s_card_payout_y);

        ticket_count = 0U;
    }

    if ((ticket_count != 0U) &&
        (Setting_IsTicketPayoutEnabled() != 0U))
    {
        Account_AddTicketOut(ticket_count, Setting_GetTicketsPerCoin());
    }

    if ((ticket_count != 0U) && (s_ticket_display_remaining != 0U))
    {
        if (ticket_count > s_ticket_display_remaining)
        {
            ticket_count = s_ticket_display_remaining;
        }
        s_ticket_display_remaining -= ticket_count;

        if (ticket_count >= s_game_prize)
        {
            Game_SetPrize(0U);
        }
        else
        {
            Game_SetPrize((uint16_t)(s_game_prize - ticket_count));
        }
    }
    gift_confirmed_mask = (uint16_t)(
        GiftLight_TakeConfirmedMask() & GAME_SCORE_CHANNEL_MASK);
    if (gift_confirmed_mask != 0U)
    {
        s_coins_in_channel_mask |= gift_confirmed_mask;
        Account_AddGiftLightCount(Game_CountMaskBits(gift_confirmed_mask));
        s_round_has_gift_light = 1U;
        line_score = Score_CalcLineScore(s_coins_in_channel_mask);
        if (line_score != s_game_score)
        {
            if (line_score == SCORE_AWARD_LINE_5)
            {
                Audio_PlayLoudIndexOn(AUDIO_CHIP_1,
                                      GAME_AUDIO_TRIG_LINE5,
                                      1U);
            }
            else if (line_score == SCORE_AWARD_LINE_6)
            {
                Audio_PlayLoudIndexOn(AUDIO_CHIP_1,
                                      GAME_AUDIO_TRIG_LINE6,
                                      1U);
            }
            else if (line_score == SCORE_AWARD_LINE_7)
            {
                Audio_PlayLoudIndexOn(AUDIO_CHIP_1,
                                      GAME_AUDIO_TRIG_LINE7,
                                      1U);
            }
            Game_StartJackpotLinePresentation(line_score);
        }
        Game_SetScore(line_score);

        if ((line_score == SCORE_AWARD_LINE_7) && (s_line7_lock_active == 0U))
        {
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            s_line7_lock_active = 1U;
            s_line7_settlement_started = 0U;
            s_line7_hold_start_ms = Gtim_NowMs();
            s_game_mode = GAME_MODE_LINE7_HOLD;
        }
    }

    lottery_coin_award = Lottery_TakeCoinAward();
    if (lottery_coin_award != 0U)
    {
        if (Game_AddLotteryPrizeAndPayout(lottery_coin_award) != 0U)
        {
            s_lottery_award_output_active = 1U;
        }
        else
        {
            Lottery_ReturnCoinAward(lottery_coin_award);
        }
    }

    gift_target_mask = 0U;
    gift_first_mask = 0U;
    gift_second_mask = 0U;
    gift_count = Lottery_TakeGiftResult(&gift_target_mask,
                                        &gift_first_mask,
                                        &gift_second_mask);
    if (gift_count != 0U)
    {
        if (GiftLight_Start(gift_target_mask,
                            gift_first_mask,
                            gift_second_mask,
                            s_coins_in_channel_mask) == 0U)
        {
            Lottery_NotifyAwardComplete();
        }
    }

    if (GiftLight_TakeCompleted() != 0U)
    {
        Lottery_NotifyAwardComplete();
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if ((Payout_IsBusy() != 0U) &&
        (s_award_lamp_payout_started == 0U))
    {
        s_award_lamp_payout_started = 1U;
        if (Setting_IsAmbientLedFlashEnabled(s_game_prize) != 0U)
        {
            AwardLamp_StartBlinkContinuous();
        }
    }

    Payout_Task();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    paid_count = Payout_TakePaidCount();
    if (paid_count != 0U)
    {
        if (paid_count >= s_game_prize)
        {
            Game_SetPrize(0U);
        }
        else
        {
            Game_SetPrize((uint16_t)(s_game_prize - paid_count));
        }

        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
        Meter_AddPulse(METER_PAYOUT_OUT, paid_count);
        Account_AddPhysicalCoinOut(paid_count);
    }

    if (Payout_IsBusy() == 0U)
    {
        if (s_award_lamp_payout_started != 0U)
        {
            AwardLamp_Stop();
        }
        s_award_lamp_payout_started = 0U;
    }

    if ((s_lottery_award_output_active != 0U) &&
        (Payout_IsBusy() == 0U) &&
        (Ticket_IsBusy() == 0U) &&
        (s_card_payout_remaining == 0U) &&
        ((s_game_prize == 0U) ||
         (Setting_IsCardPayoutEnabled() != 0U) ||
         (Ticket_HasFault() != 0U)))
    {
        s_lottery_award_output_active = 0U;
        Lottery_NotifyAwardComplete();
    }

    if ((s_game_mode != GAME_MODE_START) &&
        (SelfTest_IsDone() != 0U) && (Game_SettlementCheckpointReady() != 0U))
    {
        Game_UpdateCreditTransfer();
        Game_UpdateJackpotSettlement();
        Game_UpdateLine7Settlement();
    }

    Game_ProcessClearAccountRequest();
    if (s_game_mode != GAME_MODE_START)
    {
        GiftAlgorithm_Task();
        Account_Task();
    }
    Game_UpdateFailEyeBlink();

#if (MACHINE_LOCK_FEATURE_ENABLE != 0U)
    machine_lock_safe =
        (((s_game_mode == GAME_MODE_IDLE) ||
          (s_game_mode == GAME_MODE_STANDBY) ||
          (s_game_mode == GAME_MODE_READY) ||
          (s_game_mode == GAME_MODE_SETTING) ||
          (s_game_mode == GAME_MODE_ACCOUNT)) &&
         (Payout_IsBusy() == 0U) &&
         (Ticket_IsBusy() == 0U) &&
         (Lottery_IsBusy() == 0U) &&
         (GiftLight_IsBusy() == 0U) &&
         (s_card_payout_remaining == 0U) &&
         (Account_IsCreditTransfer() == 0U) &&
         (s_ticket_clear_wait_release == 0U) &&
         (s_line7_lock_active == 0U) &&
         (s_jackpot_settlement_phase == GAME_JACKPOT_SETTLEMENT_NONE)) ? 1U : 0U;
    machine_lock_was_blocking =
        (SelfTest_IsDone() != 0U) ? MachineLock_IsBlocking() : 0U;
    machine_lock_blocking = MachineLock_Task(SelfTest_IsDone(),
                                             machine_lock_safe);

    if ((machine_lock_was_blocking == 0U) &&
        (machine_lock_blocking != 0U))
    {
        if (s_game_mode == GAME_MODE_STANDBY)
        {
            Standby_Stop();
        }
        if ((s_game_mode == GAME_MODE_STANDBY) ||
            (s_game_mode == GAME_MODE_SETTING) ||
            (s_game_mode == GAME_MODE_ACCOUNT))
        {
            s_account_time_edit_active = 0U;
            Game_EnterIdleOrReady();
        }
    }
    else if ((machine_lock_was_blocking != 0U) &&
             (machine_lock_blocking == 0U))
    {
        if (s_game_mode == GAME_MODE_SETTING)
        {
            Game_ShowSettingPage();
        }
        else if (s_game_mode == GAME_MODE_ACCOUNT)
        {
            Game_ShowAccountPage();
        }
        else
        {
            Game_RestoreGameDisplay();
        }
        s_state_start_ms = Gtim_NowMs();
    }

    if (machine_lock_blocking != 0U)
    {
        (void)Coin_TakeCount();
        Motor_StopAll();
        Ticket_Pause();
        WiperMotor_Stop();
        Solenoid_Release();
        Key_UpdateLampBlink(0U, 0U, 0U);
        return;
    }
#endif

    if (s_game_mode != GAME_MODE_POST)
    {
        if (s_game_mode == GAME_MODE_FAULT)
        {
            remote_fault_clear_request =
                RemoteConfig_TakeFaultClearRequest();
            /* E17期间主机重新上线并把彩金送到后，自动回到原WAIT继续下发；
             * 若工作人员已把P13切成单机，则隔离旧请求后自动恢复。 */
            if (Fault_GetCode() == FAULT_CODE_E17)
            {
                if (Jackpot_PeekAward(&jackpot_award) != 0U)
                {
                    Fault_Clear();
                }
                else if ((Setting_IsNetworkEnabled() == 0U) &&
                         (Game_AbortOrphanJackpotWait() != 0U))
                {
                    Fault_Clear();
                }
            }
        }
        else if (Fault_IsActive() == 0U)
        {
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            (void)RemoteConfig_TakeFaultClearRequest();
        }

        if ((s_game_mode == GAME_MODE_FAULT) &&
            ((Key_TakePress(KEY_CLEAR) != 0U) ||
             (remote_fault_clear_request != 0U)))
        {
            if (Fault_GetCode() == FAULT_CODE_E14)
            {
                Ticket_ClearFault();
            }
            else if ((Fault_GetCode() == FAULT_CODE_E11) ||
                     (Fault_GetCode() == FAULT_CODE_E12) ||
                     (Fault_GetCode() == FAULT_CODE_E16))
            {
                if (Fault_GetCode() == FAULT_CODE_E16)
                {
                    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                    s_drop_result_waiting = 1U;
                    s_drop_result_wait_start_ms = Gtim_NowMs();
                }
                Fault_Clear();
            }
            else if (Fault_GetCode() == FAULT_CODE_E17)
            {
                /* 短按故障清除＝确认放弃这笔从未收到金额的彩金。
                 * C42留下5/6/7档位，方便之后人工核对；固定连线奖不回退。 */
                if ((Jackpot_PeekAward(&jackpot_award) != 0U) ||
                    (Game_AbortOrphanJackpotWait() != 0U))
                {
                    Fault_Clear();
                }
            }
            else if (remote_fault_clear_request != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Fault_Clear();
            }
        }

        Fault_Task();
        if ((Fault_IsActive() != 0U) &&
            (Fault_GetCode() != FAULT_CODE_E14))
        {
            if (Fault_GetCode() == FAULT_CODE_E15)
            {
                s_e15_background_stopped = 1U;
            }

            if (s_game_mode != GAME_MODE_FAULT)
            {
                s_fault_resume_mode = s_game_mode;
                s_fault_game_motor_was_running = (Motor_GetDirection(MOTOR_GAME_COIN) != MOTOR_DIR_STOP) ? 1U : 0U;
                s_fault_payout_motor_was_running = ((Payout_IsBusy() != 0U) && (Payout_GetRemaining() != 0U) &&
                                                     (Motor_GetDirection(MOTOR_WIN_PRIZE) != MOTOR_DIR_STOP)) ? 1U : 0U;
                s_fault_ticket_was_running = Ticket_IsBusy();
                s_fault_wiper_was_running = WiperMotor_IsRunning();
                if (s_game_mode == GAME_MODE_STANDBY)
                {
                    Standby_Stop();
                }
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                (void)Key_TakePress(KEY_CLEAR);
            }
            s_game_mode = GAME_MODE_FAULT;
        }
        else if (s_game_mode == GAME_MODE_FAULT)
        {
            Game_RestoreGameDisplay();
            Game_RestoreOutputsAfterFault();
            s_state_start_ms = Gtim_NowMs();
            if ((s_fault_resume_mode == GAME_MODE_POST) || (s_fault_resume_mode == GAME_MODE_FAULT))
            {
                Game_EnterIdleOrReady();
            }
            else if (s_fault_resume_mode == GAME_MODE_STANDBY)
            {
                Standby_Start();
                s_game_mode = GAME_MODE_STANDBY;
            }
            else
            {
                s_game_mode = s_fault_resume_mode;
            }

            if (s_e15_background_stopped != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                s_audio_back_index = 0U;
                s_e15_background_stopped = 0U;
            }
        }

        /*
         * E14缺票不再进入GAME_MODE_FAULT。补票后按故障清除键（或遥控清除）
         * 会恢复彩票器，先前未吐和后来新增的票数都还在Ticket队列中。
         */
        if ((Fault_GetCode() == FAULT_CODE_E14) &&
            ((Key_TakePress(KEY_CLEAR) != 0U) ||
             (RemoteConfig_TakeFaultClearRequest() != 0U)))
        {
            Ticket_ClearFault();
            Fault_Clear();
            Game_RestoreGameDisplay();
        }
        if ((Fault_GetCode() == FAULT_CODE_E14) &&
            (s_game_mode == GAME_MODE_STANDBY))
        {
            Standby_Stop();
            Game_EnterIdleOrReady();
        }
    }

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    coin_count = Coin_TakeCount();
    if (coin_count != 0U)
    {
        if ((s_game_mode != GAME_MODE_POST) &&
            (s_game_mode != GAME_MODE_SETTING) &&
            (s_game_mode != GAME_MODE_FAULT))
        {
            if (s_game_mode == GAME_MODE_STANDBY)
            {
                Standby_Stop();
            }

            Game_ProcessCoinCount(coin_count);

            if (s_game_mode == GAME_MODE_STANDBY)
            {
                Game_RestoreGameDisplay();
                Game_EnterIdleOrReady();
            }
            else if (s_game_mode == GAME_MODE_ACCOUNT)
            {
                s_account_time_edit_active = 0U;
                Game_RestoreGameDisplay();
                Game_EnterIdleOrReady();
            }
            else if (s_game_mode == GAME_MODE_IDLE)
            {
                s_state_start_ms = Gtim_NowMs();
                if (s_game_coin != 0U)
                {
                    s_game_mode = GAME_MODE_READY;
                }
            }
        }
    }

    Game_UpdateTicketClearHold();

    switch (s_game_mode)
    {
        case GAME_MODE_POST:
            SelfTest_Task();
            if (SelfTest_IsDone() != 0U)
            {
#ifdef BOOT_APP_BASE
                /* SelfTest_Task() finishes POST in this very iteration.  Run
                 * the TF reset before the state transition below; checking
                 * POST at the top of the next loop can never succeed. */
                if (s_upgrade_post_checked == 0U)
                {
                    uint8_t upgrade_reset;
                    if (Upgrade_PrepareTrialRelease() != 0U) return;
                    upgrade_reset = Upgrade_ResetOnNewRelease();
                    if (upgrade_reset == 2U) return;
                    BootApp_Confirm();
                    if (upgrade_reset == 1U) NVIC_SystemReset();
                    s_upgrade_post_checked = 1U;
                }
#endif
                /*
                 * POST is the common gate for coin accounting and shared
                 * jackpot activity.  Publish/handle claims only after the
                 * running-light self-test has completely finished.
                 */
                Jackpot_SetOperational(1U);
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Key_ClearEvents();
                (void)Eye_TakeChannelTriggerMask();
                (void)Eye_TakeCounterEventMask();
                Shake_Clear();
                for (counter_index = 0U; counter_index < (uint8_t)EYE_COUNTER_COUNT; counter_index++)
                {
                    (void)Eye_TakeCounter((Eye_Counter)counter_index);
                }

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Game_SetScore(s_game_score);
                Game_SetPrize(s_game_prize);
                Game_SetCoin(s_game_coin);

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                if (s_card_payout_remaining != 0U)
                {
                    (void)Ticket_Add(s_card_payout_remaining);
                }
                else if ((Account_IsCreditTransfer() == 0U) &&
                         (s_game_prize != 0U) &&
                         (Setting_IsCardPayoutEnabled() == 0U))
                {
                    if (Setting_IsCoinPayoutEnabled() != 0U)
                    {
                        (void)Payout_AddPrize(s_game_prize);
                    }

                    if (Setting_IsTicketPayoutEnabled() != 0U)
                    {
                        ticket_count = (Setting_IsCoinPayoutEnabled() != 0U) ?
                                       Game_CalcTicketPulseCount(s_game_prize) : (uint32_t)s_game_prize;
                        if (Setting_IsCoinPayoutEnabled() == 0U)
                        {
                            s_ticket_display_remaining = s_game_prize;
                        }
                        (void)Ticket_Add(ticket_count);
                    }
                }
                if (Game_RestoreJackpotSettlement() != 0U)
                {
                    /* The journal already selected BASE, WAIT or BONUS. */
                }
                else if ((s_jackpot_recovery_tier >= 5U) &&
                    (s_jackpot_recovery_tier <= 7U))
                {
                    /*
                     * The fixed 30/55/90 batch was completed before the
                     * persistent claim was created.  After a reset, resume
                     * directly at the shared-jackpot wait so a retried
                     * register-3 award is consumed and paid instead of being
                     * left in Jackpot's RAM with no game state waiting for it.
                     */
                    s_jackpot_tier_pending = s_jackpot_recovery_tier;
                    s_jackpot_recovery_tier = 0U;
                    s_jackpot_settlement_phase =
                        GAME_JACKPOT_SETTLEMENT_AWARD_WAIT;
                    s_jackpot_round_had_gift = 0U;
                    Account_SetJackpotSettlement((uint8_t)s_jackpot_settlement_phase,
                                                s_jackpot_tier_pending, 0U);
                    s_settlement_checkpoint_pending = 1U;
                    s_state_start_ms = Gtim_NowMs();
                    JackpotDisplay_SetBlink(1U);
                    s_game_mode = GAME_MODE_JACKPOT_WAIT;
                }
                else
                {
                    Game_EnterIdleOrReady();
                }
            }
            break;

        case GAME_MODE_IDLE:

            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            (void)Eye_TakeChannelTriggerMask();

            /*
             * SET and ACCOUNT are operator controls, so service them before
             * checking asynchronous lottery/gift output activity.  Those
             * background tasks continue to run above the state switch and
             * must not swallow either menu key.
             */
            if (Key_TakePress(KEY_SET) != 0U)
            {
                Game_EnterSettingMode();
                break;
            }

            if (Key_TakePress(KEY_ACCOUNT) != 0U)
            {
                Game_EnterAccountMode();
                break;
            }

            if (Lottery_BlocksGameKeys() != 0U)
            {
                (void)Key_TakePress(KEY_TICKET);
                (void)Key_TakePress(KEY_GAME);
                (void)Key_TakePress(KEY_EXCHANGE);
                break;
            }
            if (Lottery_IsBusy() != 0U)
            {
                /* 异常状态切换兜底：抽奖未结束时清掉结算键，避免延迟触发。 */
                (void)Key_TakePress(KEY_TICKET);
                (void)Key_TakePress(KEY_EXCHANGE);
            }

            if (Key_TakePress(KEY_TICKET) != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Game_HandleTicketKey();
                break;
            }

            if (Key_TakePress(KEY_GAME) != 0U)
            {
                if ((s_line7_lock_active != 0U) ||
                    (Lottery_BlocksGameKeys() != 0U))
                {
                    break;
                }

                Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_NO_COIN);
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            }

            if (Key_TakePress(KEY_EXCHANGE) != 0U)
            {
                Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_EXCHANGE);
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Payout_RequestExchange();
            }

            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            if (Standby_CanStart(s_state_start_ms,
                                 s_game_coin,
                                 s_coins_in_channel_mask) != 0U)
            {
                Standby_Start();
                s_game_mode = GAME_MODE_STANDBY;
            }

            break;

        case GAME_MODE_STANDBY:
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            (void)Eye_TakeChannelTriggerMask();

            if (Key_TakePress(KEY_SET) != 0U)
            {
                Standby_Stop();
                Game_EnterSettingMode();
                break;
            }

            if (Key_TakePress(KEY_ACCOUNT) != 0U)
            {
                Standby_Stop();
                Game_EnterAccountMode();
                break;
            }

            if (Key_TakePress(KEY_TICKET) != 0U)
            {
                Standby_Stop();
                Game_RestoreGameDisplay();
                Game_HandleTicketKey();
                if (s_game_mode == GAME_MODE_STANDBY)
                {
                    Game_EnterIdleOrReady();
                }
                break;
            }

            if (Key_TakePress(KEY_GAME) != 0U)
            {
                if (Setting_IsStandbySoundEnabled() != 0U)
                {
                    Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_NO_COIN);
                }
            }

            if (Key_TakePress(KEY_EXCHANGE) != 0U)
            {
                if (Setting_IsStandbySoundEnabled() != 0U)
                {
                    Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_EXCHANGE);
                }
                Payout_RequestExchange();
            }

            Standby_Task();
            break;

        case GAME_MODE_READY:

            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            (void)Eye_TakeChannelTriggerMask();

            /* Menu keys remain responsive while awards finish in parallel. */
            if (Key_TakePress(KEY_SET) != 0U)
            {
                Game_EnterSettingMode();
                break;
            }

            if (Key_TakePress(KEY_ACCOUNT) != 0U)
            {
                Game_EnterAccountMode();
                break;
            }

            if (Lottery_BlocksGameKeys() != 0U)
            {
                (void)Key_TakePress(KEY_TICKET);
                (void)Key_TakePress(KEY_GAME);
                (void)Key_TakePress(KEY_EXCHANGE);
                break;
            }
            if (Lottery_IsBusy() != 0U)
            {
                (void)Key_TakePress(KEY_TICKET);
                (void)Key_TakePress(KEY_EXCHANGE);
            }

            if (Key_TakePress(KEY_TICKET) != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Game_HandleTicketKey();
                break;
            }

            if (Key_TakePress(KEY_EXCHANGE) != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Payout_RequestExchange();
            }

            if (Key_TakePress(KEY_GAME) != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                if (s_line7_lock_active != 0U)
                {
                    break;
                }

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                if (s_game_coin == 0U)
                {
                    Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_NO_COIN);
                    Game_EnterIdleOrReady();
                    break;
                }

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Game_SetCoin((uint16_t)(s_game_coin - 1U));
                Account_AddGameUse(1U);
                Account_AddCoinIn(Setting_GetCoinsPerPoint());
                GiftAlgorithm_OnStake();
                /* 彩金只按本次实际押分增加。投币后直接退币、奖励循环
                 * 到右屏而不玩，都不能给彩金增加贡献。P4=2时一次
                 * 开始消耗1游戏分，对应2个实体币的押分。 */
                Jackpot_AddLocalCoins(Setting_GetCoinsPerPoint());

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                (void)Eye_TakeChannelTriggerMask();

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Game_ResetDropResultWait();
                s_drop_start_active_mask = Eye_GetChannelActiveMask();

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                motor_start_primask = __get_PRIMASK();
                __disable_irq();
                Eye_ClearCounter(EYE_COUNTER_GAME_MOTOR);
                Jackpot_SetStorageDeferred(1U);
                Motor_RunForward(MOTOR_GAME_COIN);
                if (motor_start_primask == 0U)
                {
                    __enable_irq();
                }
                Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_DROP);
                Game_AudioSetBack(GAME_AUDIO_BACK_IDLE);

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                s_state_start_ms = Gtim_NowMs();
                s_game_mode = GAME_MODE_START;
            }
            break;

        case GAME_MODE_START:
            /*
             * START keeps the game motor stop source on PB0 only. Result
             * eyes 1~9 are consumed as the drop result after PB0 has stopped
             * the motor; they no longer stop the motor early while it runs.
             */
            s_drop_start_active_mask &= Eye_GetChannelActiveMask();
            drop_mask = 0U;

            motor_count = Eye_TakeCounter(EYE_COUNTER_GAME_MOTOR);
            if ((motor_count != 0U) && (s_drop_result_waiting == 0U))
            {
                /* PB0 中断已在下降沿停机；这里仅登记后续落点等待。 */
                Motor_Stop(MOTOR_GAME_COIN);
                s_drop_result_waiting = 1U;
                s_drop_result_wait_start_ms = Gtim_NowMs();
            }

            if (Motor_GetDirection(MOTOR_GAME_COIN) != MOTOR_DIR_STOP)
            {
                (void)Key_TakePress(KEY_GAME);
                break;
            }

            drop_mask = Eye_TakeChannelTriggerMask();
            if (drop_mask == 0U)
            {
                /*
                 * The edge event can be absent when a result eye had not
                 * re-armed between two close coins. Recover only from a newly
                 * active level after the PB0-confirmed motor stop.
                 */
                active_drop_mask =
                    (uint16_t)(Eye_GetChannelActiveMask() &
                               (uint16_t)(~s_drop_start_active_mask));
                if (active_drop_mask != 0U)
                {
                    drop_mask = active_drop_mask;
                }
                else
                {
                    if ((s_drop_result_waiting != 0U) &&
                        (Gtim_IsElapsed(s_drop_result_wait_start_ms,
                                        GAME_DROP_CHANNEL_JAM_MS) != 0U))
                    {
                        Motor_Stop(MOTOR_GAME_COIN);
                        Jackpot_SetStorageDeferred(0U);
                        (void)Key_TakePress(KEY_GAME);
                        Fault_Report(FAULT_CODE_E16);
                    }
                    (void)Key_TakePress(KEY_GAME);
                    break;
                }
            }
            (void)Key_TakePress(KEY_GAME);

            Game_ResetDropResultWait();
            Jackpot_SetStorageDeferred(0U);
            Segment_EyeLedSetChannelMask(drop_mask);

            middle_drop_mask = (uint16_t)(drop_mask & GAME_SCORE_CHANNEL_MASK);

            if ((drop_mask & GAME_LUCKY_CHANNEL_MASK) != 0U)
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Lottery_Request(
                    (uint16_t)(drop_mask & GAME_LUCKY_CHANNEL_MASK),
                    (uint16_t)(s_coins_in_channel_mask |
                               middle_drop_mask));
            }

            if (middle_drop_mask != 0U)
            {
                if ((middle_drop_mask & s_coins_in_channel_mask) != 0U)
                {
                    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                    Game_StartFailEyeBlink((uint16_t)(middle_drop_mask & s_coins_in_channel_mask));
                    Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_FAIL);

                    s_fail_sequence = GAME_FAIL_SEQUENCE_BLINK;
                    s_state_start_ms = Gtim_NowMs();
                    s_game_mode = GAME_MODE_OVER;
                    break;
                }

                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                s_coins_in_channel_mask |= middle_drop_mask;
                /* 同一次检测若含多个通道，把实体灯位同步进送灯动画底图。 */
                GiftLight_IncludeOccupiedMask(s_coins_in_channel_mask);
                line_score = Score_CalcLineScore(s_coins_in_channel_mask);
                if (line_score != s_game_score)
                {
                    if (line_score == SCORE_AWARD_LINE_5)
                    {
                        Audio_PlayLoudIndexOn(AUDIO_CHIP_1,
                                              GAME_AUDIO_TRIG_LINE5,
                                              1U);
                    }
                    else if (line_score == SCORE_AWARD_LINE_6)
                    {
                        Audio_PlayLoudIndexOn(AUDIO_CHIP_1,
                                              GAME_AUDIO_TRIG_LINE6,
                                              1U);
                    }
                    else if (line_score == SCORE_AWARD_LINE_7)
                    {
                        Audio_PlayLoudIndexOn(AUDIO_CHIP_1,
                                              GAME_AUDIO_TRIG_LINE7,
                                              1U);
                    }
                    Game_StartJackpotLinePresentation(line_score);
                }
                Game_SetScore(line_score);
                if (line_score == 0U)
                {
                    Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_HIT_CHANNEL);
                }
                s_state_start_ms = Gtim_NowMs();
                if (line_score == SCORE_AWARD_LINE_7)
                {
                    s_line7_lock_active = 1U;
                    s_line7_settlement_started = 0U;
                    s_line7_hold_start_ms = s_state_start_ms;
                    s_game_mode = GAME_MODE_LINE7_HOLD;
                }
                else
                {
                    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                    Game_EnterIdleOrReady();
                }
            }
            else
            {
                /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
                Game_EnterIdleOrReady();
            }
            break;

        case GAME_MODE_LUCKY:
            (void)Key_TakePress(KEY_GAME);
            (void)Key_TakePress(KEY_TICKET);
            (void)Key_TakePress(KEY_EXCHANGE);
            if ((Lottery_IsBusy() == 0U) && (GiftLight_IsBusy() == 0U))
            {
                Game_EnterIdleOrReady();
            }
            break;

        case GAME_MODE_ONLINE:
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            (void)Key_TakePress(KEY_GAME);
            Game_EnterIdleOrReady();
            break;

        case GAME_MODE_LINE7_HOLD:
            (void)Key_TakePress(KEY_GAME);
            (void)Key_TakePress(KEY_EXCHANGE);
            if (Key_TakePress(KEY_TICKET) != 0U)
            {
                /*
                 * 7连线必须统一经过这个入口设置settlement_started。
                 * P14=0时Game_BeginLineSettlement内部仍会走“中屏转右屏”，但不能再
                 * 绕过7连锁的启动标志，否则奖励/彩金转完后开始键会永久被锁住。
                 */
                Game_StartLine7Settlement();
            }
            else if ((Setting_IsImmediateAwardEnabled() != 0U) &&
                     (Gtim_IsElapsed(s_line7_hold_start_ms,
                                     GAME_LINE7_AUTO_PAYOUT_MS) != 0U))
            {
                Game_StartLine7Settlement();
            }
            break;

        case GAME_MODE_JACKPOT_WAIT:
            /* Do not leave queued play presses to fire after payout completes. */
            (void)Key_TakePress(KEY_GAME);
            (void)Key_TakePress(KEY_TICKET);
            (void)Key_TakePress(KEY_EXCHANGE);
            /* 等待通信/持久化也必须允许工作人员进入菜单，退出后再恢复原阶段。 */
            if (Key_TakePress(KEY_SET) != 0U)
            {
                Game_EnterSettingMode();
                break;
            }
            if (Key_TakePress(KEY_ACCOUNT) != 0U)
            {
                Game_EnterAccountMode();
                break;
            }
            if (s_settlement_checkpoint_pending != 0U)
            {
                /* EEPROM checkpoint 一直失败时也不能无限静默等待。E17 只提示
                 * 人工检查；未落盘的申请仍保留，不能擅自清掉或重复发奖。 */
                if ((s_jackpot_settlement_phase ==
                     GAME_JACKPOT_SETTLEMENT_AWARD_WAIT) &&
                    (Gtim_IsElapsed(s_state_start_ms,
                                    GAME_JACKPOT_REPLY_TIMEOUT_MS) != 0U))
                {
                    Fault_Report(FAULT_CODE_E17);
                }
                break;
            }
            if (s_jackpot_settlement_phase == GAME_JACKPOT_SETTLEMENT_NONE)
            {
                Game_EnterIdleOrReady();
                break;
            }
            /* P13在本次开机中从联机改为单机时，底层485角色要重启才改变，
             * 但业务不能继续等旧主机。立即隔离未收到的彩金申请并恢复游戏。 */
            if ((s_jackpot_settlement_phase ==
                 GAME_JACKPOT_SETTLEMENT_AWARD_WAIT) &&
                (Setting_IsNetworkEnabled() == 0U) &&
                /* 下发已经到达时仍优先兑现，不能因刚切单机把已收到的金额丢掉。 */
                (Jackpot_PeekAward(&jackpot_award) == 0U))
            {
                if (Game_AbortOrphanJackpotWait() != 0U)
                {
                    Game_EnterIdleOrReady();
                }
                break;
            }
            if ((s_jackpot_settlement_phase ==
                 GAME_JACKPOT_SETTLEMENT_AWARD_WAIT) &&
                (Jackpot_PeekAward(&jackpot_award) != 0U))
            {
                /*
                 * The fixed line-prize batch has reached zero.  The master
                 * has now deducted the pool (with the P14 floor applied), so
                 * stop flashing and show the new steady jackpot value.
                 */
                Game_StopJackpotLinePresentation();

                if (jackpot_award == 0U)
                {
                    s_jackpot_settlement_phase =
                        GAME_JACKPOT_SETTLEMENT_NONE;
                    s_jackpot_tier_pending = 0U;
                    s_jackpot_round_had_gift = 0U;
                    Account_SetJackpotSettlement(0U, 0U, 0U);
                    s_settlement_checkpoint_pending = 1U;
                    /* Save completion before clearing even a zero-value claim. */
                    (void)Game_SettlementCheckpointReady();
                    Game_EnterIdleOrReady();
                    break;
                }

                if (Game_AddLinePrizeAndHold(0U, jackpot_award) != 0U)
                {
                    /* The jackpot amount is now visible on the middle
                     * display.  Announce its tier before the second coin
                     * batch starts. */
                    Game_PlayJackpotAwardAudio(s_jackpot_tier_pending);

                    if (s_jackpot_round_had_gift != 0U)
                    {
                        Account_AddGiftWinValue(jackpot_award);
                    }

                    if ((Setting_IsCardPayoutEnabled() != 0U) &&
                        (Account_IsCreditTransfer() == 0U))
                    {
                        (void)Game_StartCardPayout();
                    }

                    /* Keep the second coin batch on screen briefly so the
                     * 25/26/27 announcement starts before the payout motor. */
                    if (s_game_mode == GAME_MODE_TICKET_HOLD)
                    {
                        s_line_payout_hold_start_ms = Gtim_NowMs();
                        s_state_start_ms = s_line_payout_hold_start_ms;
                    }
                    s_jackpot_settlement_phase =
                        GAME_JACKPOT_SETTLEMENT_BONUS_PAYOUT;
                    Account_SetJackpotSettlement((uint8_t)s_jackpot_settlement_phase,
                                                s_jackpot_tier_pending,
                                                s_jackpot_round_had_gift);
                    s_settlement_checkpoint_pending = 1U;
                    (void)Game_SettlementCheckpointReady();
                }
            }
            else if ((s_jackpot_settlement_phase ==
                      GAME_JACKPOT_SETTLEMENT_AWARD_WAIT) &&
                     (Gtim_IsElapsed(s_state_start_ms,
                                     GAME_JACKPOT_REPLY_TIMEOUT_MS) != 0U))
            {
                /* E17不擅自丢弃请求：主机恢复会自动继续；按故障清除才隔离。 */
                Fault_Report(FAULT_CODE_E17);
            }
            break;

        case GAME_MODE_TICKET_HOLD:
            if (s_ticket_clear_hold_active != 0U) break;
            /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
            if (Gtim_IsElapsed(s_line_payout_hold_start_ms, GAME_TICKET_SHOW_HOLD_MS) == 0U)
            {
                break;
            }

            /* For the jackpot second batch, tracks 25/26/27 own the effect
             * channel until their complete audible phrase and volume restore
             * have finished.  Only then may payout start and request track
             * 11, otherwise that sound cuts off the final word "彩金". */
            if ((s_jackpot_settlement_phase ==
                 GAME_JACKPOT_SETTLEMENT_BONUS_PAYOUT) &&
                (Audio_IsLoudSequenceBusy() != 0U))
            {
                break;
            }

            if (s_line_payout_pending != 0U)
            {
                if (Setting_IsCoinPayoutEnabled() != 0U)
                {
                    if (Payout_AddPrize(s_line_payout_pending) != 0U)
                    {
                        Audio_PlayIndexOn(AUDIO_CHIP_1, GAME_AUDIO_TRIG_LINE_WIN);
                    }
                }
                s_line_payout_pending = 0U;
            }

            Game_EnterIdleOrReady();
            break;

        case GAME_MODE_OVER:
            (void)Key_TakePress(KEY_GAME);

            if (s_fail_sequence == GAME_FAIL_SEQUENCE_BLINK)
            {
                if (s_fail_eye_blink_mask != 0U)
                {
                    break;
                }

                Solenoid_Pulse(GAME_SOLENOID_PULSE_MS);
                s_fail_sequence = GAME_FAIL_SEQUENCE_SOLENOID;
                break;
            }

            if ((s_fail_sequence == GAME_FAIL_SEQUENCE_SOLENOID) &&
                (Solenoid_IsBusy() != 0U))
            {
                break;
            }

            s_fail_sequence = GAME_FAIL_SEQUENCE_NONE;
            Account_AddGameRound(1U);
            s_coins_in_channel_mask = 0U;
            s_round_has_gift_light = 0U;
            Game_SetScore(0U);
            Game_StopJackpotLinePresentation();
            Segment_EyeLedClearAll();

            /* 抽奖或送灯尚未结束时进入LUCKY，全部确认完成后才开放下一局。 */
            Game_EnterIdleOrReady();
            break;

        case GAME_MODE_SETTING:
            Game_TaskSettingMode();
            break;

        case GAME_MODE_ACCOUNT:
            Game_TaskAccountMode();
            break;

        case GAME_MODE_FAULT:
        default:
            Motor_StopAll();
            Ticket_Pause();
            WiperMotor_Stop();
            Solenoid_Release();
            Segment_SetFaultCode((uint8_t)Fault_GetCode());
            break;
    }

    Game_UpdateE14Display();
    if (s_game_mode != GAME_MODE_START)
        Jackpot_SetStorageDeferred(0U);
    Game_AudioUpdateBack();

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (s_game_mode == GAME_MODE_FAULT)
    {
        Key_UpdateLampBlink(0U, 0U, 0U);
    }
    else if (s_game_mode == GAME_MODE_STANDBY)
    {
        Standby_UpdateKeyLamps();
    }
    else if ((s_game_mode == GAME_MODE_SETTING) ||
             (s_game_mode == GAME_MODE_ACCOUNT))
    {
        Key_UpdateLampBlink(0U, 0U, Lottery_IsBusy());
    }
    else if (s_line7_lock_active != 0U)
    {
        Key_UpdateLampBlink(0U,
                            (s_line7_settlement_started == 0U) ? 1U : 0U,
                            Lottery_IsBusy());
    }
    else
    {
        /*
         * Normal game flow uses the right-display credit as the game-key
         * lamp source. H10/PD13 therefore shares the same 500 ms blink phase
         * as the ticket-key lamp whenever playable credit is non-zero.
         */
        Key_UpdateLampBlink((s_game_coin != 0U) ? 1U : 0U,
                            ((s_game_score != 0U) ||
                             ((s_game_coin != 0U) &&
                              (Account_IsCreditTransfer() == 0U)) ||
                             ((Setting_IsCardPayoutEnabled() != 0U) &&
                              (s_game_prize >= Setting_GetCardCoinsPerBatch()))) ? 1U : 0U,
                            Lottery_IsBusy());
    }
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 游戏主状态机：读取（对应符号 Game_GetMode）。
 * @调用关系 直接调用者：fault.c:Fault_CheckGameMotorNoCount。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
Game_Mode Game_GetMode(void)
{
    return s_game_mode;
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 游戏主状态机：设置（对应符号 Game_SetScore）。
 * @调用关系 直接调用者：game.c:Game_FinishLineRound、game.c:Game_Task。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Game_SetScore(uint16_t value)
{
    s_game_score = value;
    Segment_SetLeftNumber(s_game_score);
    Account_SetGameState(s_game_score, s_game_prize, s_game_coin);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 游戏主状态机：设置（对应符号 Game_SetPrize）。
 * @调用关系 直接调用者：奖励加入、消票完成和主状态机。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Game_SetPrize(uint16_t value)
{
    s_game_prize = value;
    Segment_SetMiddleNumber(s_game_prize);
    Account_SetGameState(s_game_score, s_game_prize, s_game_coin);
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
/**
 * @brief 游戏主状态机：设置（对应符号 Game_SetCoin）。
 * @调用关系 直接调用者：game.c:Game_ProcessCoinCount、game.c:Game_Task。
 * @局部状态 主要临时变量：old_coin；均只在本次调用中有效。
 * @param value 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Game_SetCoin(uint16_t value)
{
    uint16_t old_coin;

    old_coin = s_game_coin;
    s_game_coin = value;
    Segment_SetRightNumber(s_game_coin);
    Account_SetGameState(s_game_score, s_game_prize, s_game_coin);

    /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    if (s_game_coin != 0U)
    {
        WiperMotor_Run();
    }
    else if (old_coin != 0U)
    {
        WiperMotor_StopAfter(WIPER_MOTOR_ZERO_DELAY_MS);
    }
    else
    {
        /* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
    }
}
