/* AUTO-FILE-DOC
 * 文件职责：转盘抽奖。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "lottery.h"

#include "account.h"
#include "audio_manager.h"
#include "gift_algorithm.h"
#include "gtim.h"
#include "segment.h"

#define LOTTERY_LUCKY_MASK                      ((uint16_t)0x0101U)
#define LOTTERY_MIDDLE_MASK                     ((uint16_t)0x00FEU)
#define LOTTERY_ROLL_STEP_MS                    100U
#define LOTTERY_COL1_STOP_MS                    200U
#define LOTTERY_COL2_STOP_MS                    400U
#define LOTTERY_COL3_STOP_MS                    700U
#define LOTTERY_RESULT_HOLD_MS                  2000U
#define LOTTERY_WIN_BLINK_STEP_MS               200U

#define LOTTERY_AUDIO_ROLLING                   13U
#define LOTTERY_AUDIO_RESULT                    14U
#define LOTTERY_AUDIO_NO_AWARD                  15U
#define LOTTERY_AUDIO_GIFT_1                    20U
#define LOTTERY_AUDIO_GIFT_2                    21U
#define LOTTERY_AUDIO_GIFT_3                    24U
#define LOTTERY_AUDIO_COIN_WIN                  22U

#define LOTTERY_ROW_GIFT                       0U
#define LOTTERY_ROW_COIN_10                    1U
#define LOTTERY_ROW_COIN_5                     2U
#define LOTTERY_ROW_COIN_3                     3U

typedef enum
{
    LOTTERY_PHASE_IDLE = 0U,
    LOTTERY_PHASE_ROLLING,
    LOTTERY_PHASE_AWARD_AUDIO_WAIT,
    LOTTERY_PHASE_AWARD_PAYOUT_WAIT,
    LOTTERY_PHASE_RESULT_HOLD
} Lottery_Phase;

static volatile Lottery_Phase s_phase = LOTTERY_PHASE_IDLE;
static volatile uint8_t s_busy = 0U;
static volatile uint8_t s_request_count = 0U;
static volatile uint16_t s_request_mask = 0U;
static volatile uint16_t s_request_occupied_mask = 0U;
static volatile uint16_t s_current_lucky_mask = 0U;
static volatile uint16_t s_award_pending = 0U;
static volatile uint16_t s_gift_target_pending = 0U;
static volatile uint16_t s_gift_first_pending = 0U;
static volatile uint16_t s_gift_second_pending = 0U;

static uint32_t s_phase_start_ms = 0U;
static uint32_t s_last_roll_ms = 0U;
static uint16_t s_random_state = 1U;
static uint8_t s_col_row[SEGMENT_LOTTERY_SLOT_COL_COUNT] = {0U, 1U, 2U};
static uint8_t s_result_row[SEGMENT_LOTTERY_SLOT_COL_COUNT] = {0U, 1U, 2U};
static uint8_t s_result_prize_row = 0U;
static uint16_t s_result_award = 0U;
static uint8_t s_result_released = 0U; /* 本次结果只能交给Game一次 */
static uint16_t s_result_gift_target_mask = 0U;
static uint16_t s_result_gift_first_mask = 0U;
static uint16_t s_result_gift_second_mask = 0U;
static GiftAlgorithm_MissStyle s_result_miss_style =
    GIFT_ALGORITHM_MISS_TEASE;
static volatile uint8_t s_award_payout_complete = 0U;
static uint8_t s_win_blink_visible = 0U;
static uint32_t s_win_blink_last_ms = 0U;

/**
 * @brief 生成一个 16 位伪随机数。
 *
 * @return 伪随机数。
 *
 * 逻辑说明：
 * - 本项目没有专门的硬件随机数需求，这里只用于老虎机动画和概率选择。
 * - s_random_state 保存上一次随机状态。
 * - 乘 37、加 17、再混入当前毫秒数，可以让每次落币启动时的结果不完全固定。
 * - 这个随机数不是加密随机数，只用于游戏表现和概率分支。
 */
/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_NextRandom16）。
 * @调用关系 直接调用者：lottery.c:Lottery_NextRow。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Lottery_NextRandom16(void)
{
    s_random_state = (uint16_t)((s_random_state * 37U) + 17U + (uint16_t)Gtim_NowMs());
    return s_random_state;
}

/**
 * @brief 生成老虎机某一列当前亮灯行。
 *
 * @return 0~3，对应第 1~4 行。
 *
 * 逻辑说明：
 * - 4×4 跑灯板每列只有 4 行，所以随机数最终只保留 2 个 bit。
 * - 第 1 行是 30 币，第 2 行是 10 币，第 3 行是 5 币，第 4 行是 3 币。
 * - 本函数只用于初始位置和未中奖最终位置，不再用于滚动动画。
 */
/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_NextRow）。
 * @调用关系 直接调用者：lottery.c:Lottery_PrepareResultRows、lottery.c:Lottery_StartOneRequest。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Lottery_NextRow(void)
{
    return (uint8_t)((Lottery_NextRandom16() >> 3U) & 0x03U);
}

/**
 * @brief 让某一列按固定方向滚到下一行。
 *
 * @param current_row 当前这一列亮着的行，0~3 对应第 1~4 行。
 *
 * @return 下一次应该亮的行。
 *
 * 逻辑说明：
 * - 原来滚动时每一步都调用 Lottery_NextRow() 随机选行，视觉上像某一列在闪烁。
 * - 现在改成 0 -> 1 -> 2 -> 3 -> 0 的固定顺序，玩家看到的是灯点沿列方向移动。
 * - 每列仍然只亮 1 个 LED，符合 4×4 跑灯板的老虎机显示规则。
 */
/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_NextRollRow）。
 * @调用关系 直接调用者：lottery.c:Lottery_Task。
 * @param current_row 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Lottery_NextRollRow(uint8_t current_row)
{
    current_row++;
    if (current_row >= SEGMENT_LOTTERY_ROW_COUNT)
    {
        current_row = 0U;
    }

    return current_row;
}

/**
 * @brief  判断奖型是否为送灯
 * @param  award_type GiftAlgorithm_AwardType 奖型值
 * @retval 1 为 1~6 灯奖；0 为其他奖型
 */
static uint8_t Lottery_IsGiftAward(uint16_t award_type)
{
    return ((award_type == GIFT_ALGORITHM_AWARD_GIFT_1) ||
            (award_type == GIFT_ALGORITHM_AWARD_GIFT_2) ||
            (award_type == GIFT_ALGORITHM_AWARD_GIFT_3) ||
            (award_type == GIFT_ALGORITHM_AWARD_GIFT_4) ||
            (award_type == GIFT_ALGORITHM_AWARD_GIFT_5) ||
            (award_type == GIFT_ALGORITHM_AWARD_GIFT_6)) ? 1U : 0U;
}

/**
 * @brief  把送灯奖型转换为灯数
 * @param  award_type GiftAlgorithm_AwardType 奖型值
 * @retval 1~6 为送灯数；0 表示不是送灯奖
 */
static uint8_t Lottery_GiftCount(uint16_t award_type)
{
    if (award_type == GIFT_ALGORITHM_AWARD_GIFT_1) return 1U;
    if (award_type == GIFT_ALGORITHM_AWARD_GIFT_2) return 2U;
    if (award_type == GIFT_ALGORITHM_AWARD_GIFT_3) return 3U;
    if (award_type == GIFT_ALGORITHM_AWARD_GIFT_4) return 4U;
    if (award_type == GIFT_ALGORITHM_AWARD_GIFT_5) return 5U;
    if (award_type == GIFT_ALGORITHM_AWARD_GIFT_6) return 6U;
    return 0U;
}

/**
 * @brief 根据奖型找到 4×4 跑灯板第四列对应的奖项行。
 *
 * @param award_type 送灯1~6或直接返币10/5/3的奖型。
 *
 * @return 奖项行，0~3 对应送灯/10/5/3币。
 */
/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_AwardToRow）。
 * @调用关系 直接调用者：lottery.c:Lottery_PrepareResultRows。
 * @param award_type 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint8_t Lottery_AwardToRow(uint16_t award_type)
{
    if (Lottery_IsGiftAward(award_type) != 0U)
    {
        return LOTTERY_ROW_GIFT;
    }
    if (award_type == GIFT_ALGORITHM_AWARD_COIN_10)
    {
        return LOTTERY_ROW_COIN_10;
    }
    if (award_type == GIFT_ALGORITHM_AWARD_COIN_5)
    {
        return LOTTERY_ROW_COIN_5;
    }

    return LOTTERY_ROW_COIN_3;
}

/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_AwardToAudio）。
 * @调用关系 直接调用者：lottery.c:Lottery_Task。
 * @param award_type 按值传入；函数内部修改不会回写调用方变量。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Lottery_AwardToAudio(uint16_t award_type)
{
    if (award_type == GIFT_ALGORITHM_AWARD_GIFT_1)
    {
        return LOTTERY_AUDIO_GIFT_1;
    }
    if (award_type == GIFT_ALGORITHM_AWARD_GIFT_2)
    {
        return LOTTERY_AUDIO_GIFT_2;
    }
    if ((award_type == GIFT_ALGORITHM_AWARD_GIFT_3) ||
        (award_type == GIFT_ALGORITHM_AWARD_GIFT_4) ||
        (award_type == GIFT_ALGORITHM_AWARD_GIFT_5) ||
        (award_type == GIFT_ALGORITHM_AWARD_GIFT_6))
    {
        return LOTTERY_AUDIO_GIFT_3;
    }
    return LOTTERY_AUDIO_COIN_WIN;
}

/**
 * @brief 根据返奖预算选择本次老虎机奖励。
 *
 * @return GiftAlgorithm_AwardType枚举，不是直接返回中奖币数。
 * 正账按C3乘现场返奖率后全额扣除C4和D，负账池只累计进入负账后的新押币。
 * 普通币奖在预算内按10、5、3币顺序选择；正账每满P17押分从预算转1币入D。
 * D池入池后的实时余额达到P16即开始抽5/6/7连线，分别按90/175、
 * 55/175、30/175执行；灯型不成立或D池不足时不派高连线，也不扣D。
 * 本层只接收已确定的结果并播放动画；真正入账/发奖仍由Game处理。
 */
/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_SelectAward）。
 * @调用关系 直接调用者：lottery.c:Lottery_StartOneRequest。
 * @局部状态 主要临时变量：result；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static uint16_t Lottery_SelectAward(void)
{
    GiftAlgorithm_Result result;

    GiftAlgorithm_Select(s_request_occupied_mask, &result);
    s_result_gift_target_mask = result.target_mask;
    s_result_gift_first_mask = result.first_target_mask;
    s_result_gift_second_mask = result.second_target_mask;
    s_result_miss_style = result.miss_style;
    return (uint16_t)result.type;
}

/**
 * @brief 准备本次老虎机最终停灯位置。
 *
 * 逻辑说明：
 * - 如果 s_result_award 不为 0，三列最终停在同一行，并点亮第四列奖项灯。
 * - 空奖1让前两列停在同一行，第三列一定错位。
 * - 空奖2让前两列先错位，第三列仍不能形成三列同排。
 * - 两种未中奖组合都不会让三列停在同一行，第四列奖项灯也不会点亮。
 */
/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_PrepareResultRows）。
 * @调用关系 直接调用者：lottery.c:Lottery_StartOneRequest。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Lottery_PrepareResultRows(void)
{
    if (s_result_award != 0U)
    {
        s_result_prize_row = Lottery_AwardToRow(s_result_award);
        s_result_row[0] = s_result_prize_row;
        s_result_row[1] = s_result_prize_row;
        s_result_row[2] = s_result_prize_row;
        return;
    }

    s_result_prize_row = 0U;
    s_result_row[0] = Lottery_NextRow();
    if (s_result_miss_style == GIFT_ALGORITHM_MISS_TEASE)
    {
        s_result_row[1] = s_result_row[0];
        s_result_row[2] = (uint8_t)((s_result_row[0] + 1U +
                                    (Lottery_NextRow() & 0x01U)) & 0x03U);
    }
    else /* GIFT_ALGORITHM_MISS_EARLY_MISMATCH */
    {
        s_result_row[1] = (uint8_t)((s_result_row[0] + 1U +
                                    (Lottery_NextRow() & 0x01U)) & 0x03U);
        s_result_row[2] = (uint8_t)((s_result_row[1] + 1U +
                                    (Lottery_NextRow() & 0x01U)) & 0x03U);
    }
}

/**
 * @brief 把本次奖型放入唯一的待领取槽。
 *
 * @param award_type 奖型枚举，不是币数。
 *
 * 逻辑说明：
 * - Game_Task通过Lottery_TakeCoinAward/TakeGiftResult领取。
 * - 槽未空时不能覆盖或累加，当前轮必须完成后才允许下一轮。
 */
/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_AddPendingAward）。
 * @调用关系 直接调用者：lottery.c:Lottery_ReleaseResultAward。
 * @param award_type 按值传入；同一轮结果只能发布一次。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Lottery_AddPendingAward(uint16_t award_type)
{
    /* 这里存的是奖型枚举，不是币数，绝不能相加。
     * 例如 COIN_5=3，相加两次会变成COIN_10=6。 */
    if ((award_type == GIFT_ALGORITHM_AWARD_NONE) ||
        (s_award_pending != GIFT_ALGORITHM_AWARD_NONE))
    {
        return;
    }
    s_award_pending = award_type;
}

/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_ReleaseResultAward）。
 * @调用关系 直接调用者：lottery.c:Lottery_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Lottery_ReleaseResultAward(void)
{
    if (s_result_released != 0U) return;
    s_result_released = 1U;
    Lottery_AddPendingAward(s_result_award);
    s_gift_target_pending = s_result_gift_target_mask;
    s_gift_first_pending = s_result_gift_first_mask;
    s_gift_second_pending = s_result_gift_second_mask;
    s_award_payout_complete = 0U;
}

/**
 * @brief 转盘抽奖：更新（对应符号 Lottery_UpdateWinBlink）。
 * @调用关系 直接调用者：lottery.c:Lottery_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Lottery_UpdateWinBlink(void)
{
    if (Gtim_IsElapsed(s_win_blink_last_ms,
                       LOTTERY_WIN_BLINK_STEP_MS) == 0U)
    {
        return;
    }

    s_win_blink_last_ms = Gtim_NowMs();
    if (s_win_blink_visible != 0U)
    {
        Segment_LotteryLampClearAll();
        s_win_blink_visible = 0U;
    }
    else
    {
        Segment_LotteryLampShow(s_result_row[0],
                                s_result_row[1],
                                s_result_row[2],
                                s_result_prize_row,
                                1U);
        s_win_blink_visible = 1U;
    }
}

/**
 * @brief 从等待队列里启动一次幸运抽奖。
 *
 * 逻辑说明：
 * - s_request_count 是等待抽奖次数，1/9 两侧返奖口每触发一次就加 1。
 * - 每次启动时先决定本次结果，但灯板动画仍然按列滚动，最后才展示结果。
 * - 决定结果时读取 Account 账目，用当前返奖预算控制是否中奖以及中多少。
 */
/**
 * @brief 转盘抽奖：启动（对应符号 Lottery_StartOneRequest）。
 * @调用关系 直接调用者：lottery.c:Lottery_Request、lottery.c:Lottery_Task。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
static void Lottery_StartOneRequest(void)
{
    if (s_request_count == 0U)
    {
        s_busy = 0U;
        s_phase = LOTTERY_PHASE_IDLE;
        Segment_LotteryLampClearAll();
        return;
    }

    s_request_count--;
    s_current_lucky_mask = (uint16_t)(s_request_mask & LOTTERY_LUCKY_MASK);
    s_request_mask = 0U;
    s_random_state = (uint16_t)(Gtim_NowMs() | 1U);
    s_phase_start_ms = Gtim_NowMs();
    s_last_roll_ms = s_phase_start_ms;

    s_col_row[0] = Lottery_NextRow();
    s_col_row[1] = Lottery_NextRow();
    s_col_row[2] = Lottery_NextRow();

    s_result_released = 0U;
    s_result_award = Lottery_SelectAward();
    s_request_occupied_mask = 0U;
    Lottery_PrepareResultRows();

    /*
     * 1号或9号光眼一触发就立刻显示第一帧并播放013滚动音。
     * 不再先等待两侧通道灯保持2秒；三列按2声“哒”在约0.7秒内依次停下，
     * 013循环音也在第三列停下时同步结束。
     */
    Segment_LotteryLampShow(s_col_row[0],
                            s_col_row[1],
                            s_col_row[2],
                            s_result_prize_row,
                            0U);
    Audio_LoopIndexOn(AUDIO_CHIP_1, LOTTERY_AUDIO_ROLLING);
    s_phase = LOTTERY_PHASE_ROLLING;
    s_busy = 1U;
}

/**
 * @brief 转盘抽奖：初始化（对应符号 Lottery_Init）。
 * @调用关系 直接调用者：game.c:Game_Init。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Lottery_Init(void)
{
    s_phase = LOTTERY_PHASE_IDLE;
    s_busy = 0U;
    s_request_count = 0U;
    s_request_mask = 0U;
    s_request_occupied_mask = 0U;
    s_current_lucky_mask = 0U;
    s_award_pending = 0U;
    s_gift_target_pending = 0U;
    s_gift_first_pending = 0U;
    s_gift_second_pending = 0U;
    s_phase_start_ms = 0U;
    s_last_roll_ms = 0U;
    s_random_state = 1U;
    s_col_row[0] = 0U;
    s_col_row[1] = 1U;
    s_col_row[2] = 2U;
    s_result_row[0] = 0U;
    s_result_row[1] = 1U;
    s_result_row[2] = 2U;
    s_result_prize_row = 0U;
    s_result_award = 0U;
    s_result_released = 0U;
    s_result_gift_target_mask = 0U;
    s_result_gift_first_mask = 0U;
    s_result_gift_second_mask = 0U;
    s_result_miss_style = GIFT_ALGORITHM_MISS_TEASE;
    s_award_payout_complete = 0U;
    s_win_blink_visible = 0U;
    s_win_blink_last_ms = 0U;
    GiftAlgorithm_Init();
    Segment_LotteryLampClearAll();
}

/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_Request）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param lucky_channel_mask 按值传入；函数内部修改不会回写调用方变量。
 * @param occupied_middle_mask 按值传入；函数内部修改不会回写调用方变量。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Lottery_Request(uint16_t lucky_channel_mask,
                     uint16_t occupied_middle_mask)
{
    lucky_channel_mask &= LOTTERY_LUCKY_MASK;
    if (lucky_channel_mask == 0U)
    {
        return;
    }

    /*
     * 记录这次是 1 号或 9 号返奖口触发。
     * s_request_mask 只用于两侧灯保持 2 秒后熄灭对应灯。
     * 真正抽奖次数由 s_request_count 记录，连续落入两侧口时会排队执行。
     * Account_AddLotteryCount() 对应查账 C7：触发老虎机抽奖的次数。
     */
    Account_AddLotteryCount(1U);
    s_request_mask |= lucky_channel_mask;
    s_request_occupied_mask |=
        (uint16_t)(occupied_middle_mask & LOTTERY_MIDDLE_MASK);
    if (s_request_count < 255U)
    {
        s_request_count++;
    }

    if (s_busy == 0U)
    {
        Lottery_StartOneRequest();
    }
}

/**
 * @brief 转盘抽奖：周期处理（对应符号 Lottery_Task）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：elapsed_ms；均只在本次调用中有效。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Lottery_Task(void)
{
    uint32_t elapsed_ms;

    if (s_busy == 0U)
    {
        if (s_request_count != 0U)
        {
            Lottery_StartOneRequest();
        }
        return;
    }

    switch (s_phase)
    {
        case LOTTERY_PHASE_ROLLING:
            /*
             * 前三列从光眼触发时立即滚动，总共播放约2声“哒”。
             * 第1列0.2秒停，第2列0.4秒停，第3列约0.7秒停。
             */
            elapsed_ms = (uint32_t)(Gtim_NowMs() - s_phase_start_ms);

            if (Gtim_IsElapsed(s_last_roll_ms, LOTTERY_ROLL_STEP_MS) != 0U)
            {
                s_last_roll_ms = Gtim_NowMs();

                if (elapsed_ms < LOTTERY_COL1_STOP_MS)
                {
                    s_col_row[0] = Lottery_NextRollRow(s_col_row[0]);
                }
                else
                {
                    s_col_row[0] = s_result_row[0];
                }

                if (elapsed_ms < LOTTERY_COL2_STOP_MS)
                {
                    s_col_row[1] = Lottery_NextRollRow(s_col_row[1]);
                }
                else
                {
                    s_col_row[1] = s_result_row[1];
                }

                if (elapsed_ms < LOTTERY_COL3_STOP_MS)
                {
                    s_col_row[2] = Lottery_NextRollRow(s_col_row[2]);
                    Segment_LotteryLampShow(s_col_row[0],
                                            s_col_row[1],
                                            s_col_row[2],
                                            s_result_prize_row,
                                            0U);
                }
                else
                {
                    s_col_row[2] = s_result_row[2];
                    Segment_EyeLedClearChannelMask(s_current_lucky_mask);

                    /*
                     * 第 3 列停住后才显示最终结果。
                     * 中奖：前三列同一行，第四列奖项灯点亮。
                     * 未中：前三列不同组合，第四列奖项灯不亮。
                     */
                    Segment_LotteryLampShow(s_result_row[0],
                                            s_result_row[1],
                                            s_result_row[2],
                                            s_result_prize_row,
                                            (s_result_award != 0U) ? 1U : 0U);

                    /* 结果揭晓立即提交奖项；语音与出奖并行，不作为按键互锁。 */
                    Audio_CancelLoopResumeOn(AUDIO_CHIP_1);

                    s_phase_start_ms = Gtim_NowMs();
                    if (s_result_award != 0U)
                    {
                        s_win_blink_visible = 1U;
                        s_win_blink_last_ms = s_phase_start_ms;
                        (void)Audio_PlayLoudIndexOn(AUDIO_CHIP_1,
                            Lottery_AwardToAudio(s_result_award), 0U);
                        Lottery_ReleaseResultAward();
                        s_phase = LOTTERY_PHASE_AWARD_PAYOUT_WAIT;
                    }
                    else
                    {
                        Audio_PlayIndexOn(AUDIO_CHIP_1,
                                          LOTTERY_AUDIO_RESULT);
                        Audio_PlayIndexOn(AUDIO_CHIP_1,
                                          LOTTERY_AUDIO_NO_AWARD);
                        s_phase = LOTTERY_PHASE_RESULT_HOLD;
                    }
                }
            }
            break;

        case LOTTERY_PHASE_AWARD_AUDIO_WAIT:
            Lottery_UpdateWinBlink();
            if (Audio_IsLoudSequenceBusy() == 0U)
            {
                Lottery_ReleaseResultAward();
                s_phase = LOTTERY_PHASE_AWARD_PAYOUT_WAIT;
            }
            break;

        case LOTTERY_PHASE_AWARD_PAYOUT_WAIT:
            if (s_award_payout_complete != 0U)
            {
                s_award_payout_complete = 0U;
                s_win_blink_visible = 0U;
                Segment_LotteryLampClearAll();
                if (s_request_count != 0U)
                {
                    Lottery_StartOneRequest();
                }
                else
                {
                    s_current_lucky_mask = 0U;
                    s_busy = 0U;
                    s_phase = LOTTERY_PHASE_IDLE;
                }
                break;
            }

            Lottery_UpdateWinBlink();
            break;

        case LOTTERY_PHASE_RESULT_HOLD:
            /*
             * 第三阶段：最终结果保持 2 秒。
             * 保持结束后清掉 4×4 跑灯板。
             * 如果等待队列里还有下一次 1/9 返奖口触发，就立即进入下一轮抽奖。
             */
            if (Gtim_IsElapsed(s_phase_start_ms, LOTTERY_RESULT_HOLD_MS) != 0U)
            {
                Segment_LotteryLampClearAll();
                if (s_request_count != 0U)
                {
                    Lottery_StartOneRequest();
                }
                else
                {
                    s_current_lucky_mask = 0U;
                    s_busy = 0U;
                    s_phase = LOTTERY_PHASE_IDLE;
                }
            }
            break;

        case LOTTERY_PHASE_IDLE:
        default:
            s_busy = 0U;
            s_phase = LOTTERY_PHASE_IDLE;
            break;
    }
}

/**
 * @brief 转盘抽奖：读取并清除一次性事件（对应符号 Lottery_TakeGiftResult）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：result、primask；均只在本次调用中有效。
 * @param target_mask 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param first_target_mask 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @param second_target_mask 输入/输出指针（函数可能通过它回写结果）；调用方必须保证地址有效且容量满足接口约定。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Lottery_TakeGiftResult(uint16_t *target_mask,
                               uint16_t *first_target_mask,
                               uint16_t *second_target_mask)
{
    uint16_t result;                              // 待领取奖型，无量纲
    uint32_t primask;                             // 中断屏蔽寄存器值，寄存器位掩码
    uint8_t gift_count;                           // 送灯数量，单位：盏

    /*
     * s_award_pending 可能在 Lottery_Task() 里变化。
     * 这里关中断只是为了让“读取并清零”成为一个完整动作。
     */
    primask = __get_PRIMASK();
    __disable_irq();
    result = s_award_pending;
    gift_count = Lottery_GiftCount(result);
    if (gift_count != 0U)
    {
        s_award_pending = 0U;
        if (target_mask != 0)
        {
            *target_mask = s_gift_target_pending;
        }
        if (first_target_mask != 0)
        {
            *first_target_mask = s_gift_first_pending;
        }
        if (second_target_mask != 0)
        {
            *second_target_mask = s_gift_second_pending;
        }
        s_gift_target_pending = 0U;
        s_gift_first_pending = 0U;
        s_gift_second_pending = 0U;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }

    return gift_count;
}

/**
 * @brief 转盘抽奖：读取并清除一次性事件（对应符号 Lottery_TakeCoinAward）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @局部状态 主要临时变量：result、coins、primask；均只在本次调用中有效。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint16_t Lottery_TakeCoinAward(void)
{
    uint16_t result;
    uint16_t coins = 0U;
    uint32_t primask = __get_PRIMASK();

    __disable_irq();
    result = s_award_pending;
    if (result == GIFT_ALGORITHM_AWARD_COIN_10)
    {
        coins = 10U;
        s_award_pending = 0U;
    }
    else if (result == GIFT_ALGORITHM_AWARD_COIN_5)
    {
        coins = 5U;
        s_award_pending = 0U;
    }
    else if (result == GIFT_ALGORITHM_AWARD_COIN_3)
    {
        coins = 3U;
        s_award_pending = 0U;
    }
    if (primask == 0U)
    {
        __enable_irq();
    }
    if ((coins != 0U) && (GiftAlgorithm_CoinAwardAuthorized(coins) == 0U))
    {
        /* 异常/重复奖不进入Game、不反复退回队列，也不让幸运状态卡住。 */
        coins = 0U;
        Lottery_NotifyAwardComplete();
    }
    return coins;
}

/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_ReturnCoinAward）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @param award_coins 按值传入；函数内部修改不会回写调用方变量。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Lottery_ReturnCoinAward(uint16_t award_coins)
{
    if (award_coins == 10U)
    {
        s_award_pending = GIFT_ALGORITHM_AWARD_COIN_10;
    }
    else if (award_coins == 5U)
    {
        s_award_pending = GIFT_ALGORITHM_AWARD_COIN_5;
    }
    else if (award_coins == 3U)
    {
        s_award_pending = GIFT_ALGORITHM_AWARD_COIN_3;
    }
}

/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_NotifyAwardComplete）。
 * @调用关系 直接调用者：game.c:Game_Task。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void Lottery_NotifyAwardComplete(void)
{
    if (s_phase == LOTTERY_PHASE_AWARD_PAYOUT_WAIT)
    {
        s_award_payout_complete = 1U;
    }
}

/**
 * @brief 转盘抽奖：设置（对应符号 Lottery_ResetForAccountClear）。
 * @调用关系 直接调用者：game.c:Game_ProcessClearAccountRequest。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Lottery_ResetForAccountClear(void)
{
    if (Lottery_IsBusy() != 0U)
    {
        return 0U;
    }
    return GiftAlgorithm_ResetForAccountClear();
}

/**
 * @brief 转盘抽奖：执行内部处理（对应符号 Lottery_IsBusy）。
 * @调用关系 直接调用者：game.c:Game_ProcessClearAccountRequest、game.c:Game_Task、lottery.c:Lottery_ResetForAccountClear、standby.c:Standby_CanStart。
 * @return 返回值含义由声明类型和调用处分支共同约定；0/1 通常表示失败/成功或否/是。
 * @note 位移及按位与/或用于寄存器、掩码或小端字段拼装，不是逻辑与/或。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
uint8_t Lottery_IsBusy(void)
{
    return ((s_busy != 0U) || (s_request_count != 0U)) ? 1U : 0U;
}

/**
 * @brief 查询幸运抽奖是否应锁住游戏操作键。
 * @return 抽奖请求、结果保持、送灯或币奖输出未完成时返回1，否则返回0。
 * @note 正常流程由GAME_MODE_LUCKY负责互锁；本接口防止异常状态切换后提前放行。
 */
uint8_t Lottery_BlocksGameKeys(void)
{
    return Lottery_IsBusy();
}
