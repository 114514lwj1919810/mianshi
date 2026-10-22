/* AUTO-FILE-DOC
 * 文件职责：语音调度。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#ifndef __AUDIO_MANAGER_H
#define __AUDIO_MANAGER_H

#include <stdint.h>

/*
 * 双KT404C连接：
 * - 音效板：UART4_TX/PA0发送，UART4_RX/PA1接收播放完成回包。
 * - 第一条触发音效播放根目录物理序号，后续音效从ADVERT1执行单层插播。
 * - 背景板：USART3_TX/PB10，循环播放背景音乐。
 * - 两路均为9600波特率、8位数据、无校验、1位停止位。
 * - 插播期间的新音效进入FIFO，当前插播结束后继续发送，不做嵌套插播。
 */

typedef enum
{
    AUDIO_CHIP_1 = 0,
    AUDIO_CHIP_2,
    AUDIO_CHIP_COUNT
} Audio_Chip;

typedef enum
{
    AUDIO_STATE_IDLE = 0,
    AUDIO_STATE_WAIT,
    AUDIO_STATE_STARTUP_WAIT,
    AUDIO_STATE_ERROR
} Audio_State;

/* 背景音乐板开关：0=完全屏蔽背景板，1=背景音乐和触发音效同时启用。 */
extern uint8_t g_background_music_enable;

void Audio_Init(void);
void Audio_Task(void);

void Audio_PlayIndexOn(Audio_Chip chip, uint16_t index);
void Audio_LoopIndexOn(Audio_Chip chip, uint16_t index);
void Audio_CancelLoopResumeOn(Audio_Chip chip);
void Audio_StopOn(Audio_Chip chip);

/*
 * 音效板高音量庆祝序列：17~24号临时把AUDIO_CHIP_1当前P13音量提高10级，
 * 25~27号彩金播报提高15级（均最高30级），先停止旧音效，再按根目录物理序号播放
 * prelude_count次23号前置音及17~27号index，完成后恢复P13右屏音效音量。
 * 序列期间普通音效不会覆盖庆祝音；E15独占报警仍可立即抢占。
 */
uint8_t Audio_PlayLoudIndexOn(Audio_Chip chip,
                              uint16_t index,
                              uint8_t prelude_count);
uint8_t Audio_IsLoudSequenceBusy(void);

/* E15专用独占报警，开始时停两块板并循环index，结束时停止报警。 */
void Audio_StartExclusiveAlarm(uint16_t index);
void Audio_StopExclusiveAlarm(void);

void Audio_VolumeUpOn(Audio_Chip chip);
void Audio_VolumeDownOn(Audio_Chip chip);
void Audio_SetVolumeLevelOn(Audio_Chip chip, uint8_t level);
void Audio_SetVolumeLevel(uint8_t level);

void Audio_SendHexOn(Audio_Chip chip, const uint8_t *hex, uint16_t len);
uint8_t Audio_SendHexStringOn(Audio_Chip chip, const char *hex_string);

Audio_State Audio_GetStateOn(Audio_Chip chip);

#endif
