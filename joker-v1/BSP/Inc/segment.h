/* AUTO-FILE-DOC
 * 文件职责：数码屏和灯扫描。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef SEGMENT_H
#define SEGMENT_H

#include "gd32f407_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SEGMENT_4094_COUNT              14U
#define SEGMENT_BIT_DELAY_US            5U
#define SEGMENT_POWER_CLEAR_FRAMES      6U
#define SEGMENT_POWER_CLEAR_GAP_MS      10U

#define SEGMENT_LOTTERY_ROW_COUNT       4U
#define SEGMENT_LOTTERY_SLOT_COL_COUNT  3U
#define SEGMENT_LOTTERY_TOTAL_COL_COUNT 4U

/* Eye-board LED 4094: CN2-H15/H16/H17 = PG4/PG5/PG6. */
#define EYE_LED_4094_COUNT              2U

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
#define SEGMENT_CLK_PORT                GPIOG
#define SEGMENT_CLK_PIN                 GPIO_PIN_7
#define SEGMENT_LATCH_PORT              GPIOG
#define SEGMENT_LATCH_PIN               GPIO_PIN_8
#define SEGMENT_DATA_PORT               GPIOC
#define SEGMENT_DATA_PIN                GPIO_PIN_6

/* Eye-board LED 4094 control lines. */
#define EYE_LED_CLK_PORT                GPIOG
#define EYE_LED_CLK_PIN                 GPIO_PIN_4
#define EYE_LED_LATCH_PORT              GPIOG
#define EYE_LED_LATCH_PIN               GPIO_PIN_5
#define EYE_LED_DATA_PORT               GPIOG
#define EYE_LED_DATA_PIN                GPIO_PIN_6

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_EarlySafeInit(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_Init(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_Task(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_ClearAll(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_SetLeftNumber(uint16_t value);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_SetMiddleNumber(uint16_t value);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_SetRightNumber(uint16_t value);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_SetLeftFixed4(uint16_t value);
void Segment_SetMiddleFixed4(uint16_t value);
/** 中屏显示固定三位编号（例如001），最左一位留空。 */
void Segment_SetMiddleFixed3(uint16_t value);
void Segment_SetRightFixed4(uint16_t value);

/** Use the middle and right displays together for one signed value. */
void Segment_SetMiddleRightSigned(int64_t value);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_ClearLeft(void);
void Segment_ClearMiddle(void);
void Segment_ClearRight(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_SetLeftPNumber(uint8_t index);
void Segment_SetLeftCNumber(uint8_t index);
void Segment_SetLeftANumber(uint8_t index);
void Segment_SetLeftHNumber(uint8_t index);
void Segment_SetLeftFNumber(uint8_t index);
void Segment_SetLeftUNumber(uint8_t index);
/** @brief 只在左屏显示E和两位编号，不改中屏、右屏。 */
void Segment_SetLeftENumber(uint8_t index);
/** @brief 只在中屏显示E和两位编号，不改左屏、右屏。 */
void Segment_SetMiddleENumber(uint8_t index);
void Segment_SetMiddleRightDigits(const uint8_t digits[8], int8_t blank_index);
void Segment_SetFaultCode(uint8_t code);

/**
 * @brief Clear all 16 outputs of the two eye-board LED 4094 chips.
 *
 * This only clears the software frame. Segment_Task() sends the new frame
 * to PG4/PG5/PG6 on the next 20 ms display refresh.
 */
void Segment_EyeLedClearAll(void);

/**
 * @brief Turn on the eye-board LEDs corresponding to the 9-channel eye mask.
 *
 * eye_channel_mask bit0..bit8 correspond to channel 1..9. The observed
 * 4094 physical mapping is: CH1->bit3, CH2->bit2, CH3->bit1, CH4->bit0,
 * CH5->bit4, CH6->bit12, CH7->bit13, CH8->bit14, CH9->bit15.
 *
 * Existing lit LEDs are retained; this function only turns on the channels
 * that appear in eye_channel_mask.
 */
void Segment_EyeLedSetChannelMask(uint16_t eye_channel_mask);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_EyeLedClearChannelMask(uint16_t eye_channel_mask);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_LotteryLampClearAll(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_LotteryLampShow(uint8_t col1_row,
                             uint8_t col2_row,
                             uint8_t col3_row,
                             uint8_t prize_row,
                             uint8_t show_prize);


/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_Test1(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_Test2(void);

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
void Segment_Test3(void);

#ifdef __cplusplus
}
#endif

#endif
