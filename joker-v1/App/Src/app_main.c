/* AUTO-FILE-DOC
 * 文件职责：应用初始化与主循环。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "app_main.h"
#ifdef BOOT_APP_BASE
#include "boot_contract.h"
#endif

#define APP_UART_DEBUG_ENABLE 0U

#if (APP_UART_DEBUG_ENABLE != 0U)
#include <stdio.h>
#endif

#include "audio_manager.h"
#include "app_watchdog.h"
#include "award_lamp.h"
#include "coin.h"
#include "eeprom.h"
#include "eye.h"
#include "game.h"
#include "gtim.h"
#include "iic.h"
#include "interrupt.h"
#include "jackpot_display.h"
#include "key.h"
#include "jackpot.h"
#include "machine_address.h"
#include "meter.h"
#include "motor.h"
#include "pcf8563.h"
#include "remote_config.h"
#include "segment.h"
#include "shake.h"
#include "solenoid.h"
#include "gd32f407.h"
#include "wiper_motor.h"
#include "ticket.h"
#include "./MODBUS/modbus.h"
#include "./RS485/rs485.h"
#include "./SYSTEM/usart/usart.h"

#define APP_RS485_BAUDRATE       19200U

#ifdef EEPROM_RESCUE_BUILD
/*
 * Dedicated destructive recovery image.
 *
 * This build deliberately erases only the external AT24C256 EEPROM.  The
 * secure bootloader, A/B metadata and per-product identity/key live in MCU
 * internal flash and are therefore not touched.  Keeping this code behind a
 * separate target prevents a normal game firmware from ever entering the
 * erase path by accident.
 */
#define EEPROM_RESCUE_CODE_WORKING 90U
#define EEPROM_RESCUE_CODE_SUCCESS 88U
#define EEPROM_RESCUE_CODE_FAILURE 91U
#define EEPROM_RESCUE_LED_STEP_MS   250U

static uint8_t s_eeprom_rescue_result = 0U;
static uint32_t s_eeprom_rescue_led_tick = 0U;

static uint8_t App_EraseAndVerifyExternalEeprom(void)
{
    uint8_t erase_page[EEPROM_PAGE_SIZE_BYTES];
    uint8_t verify_page[EEPROM_PAGE_SIZE_BYTES];
    uint16_t address;
    uint16_t i;

    for (i = 0U; i < EEPROM_PAGE_SIZE_BYTES; i++)
    {
        erase_page[i] = 0xFFU;
    }

    for (address = 0U;
         address < EEPROM_SIZE_BYTES;
         address = (uint16_t)(address + EEPROM_PAGE_SIZE_BYTES))
    {
        AppWatchdog_Feed();
        if (Eeprom_Write(address, erase_page, EEPROM_PAGE_SIZE_BYTES) == 0U)
        {
            return 0U;
        }
        if (Eeprom_Read(address, verify_page, EEPROM_PAGE_SIZE_BYTES) == 0U)
        {
            return 0U;
        }
        for (i = 0U; i < EEPROM_PAGE_SIZE_BYTES; i++)
        {
            if (verify_page[i] != 0xFFU)
            {
                return 0U;
            }
        }
    }

    return 1U;
}
#endif

/**
 * @brief 应用层总初始化入口。
 *
 * 上电后只调用一次。先初始化核心、串口和显示硬件，再初始化输入输出、
 * 中断与游戏状态机。彩金板在 Game_Init() 加载彩池记录后立即显示当前值。
 */
/**
 * @brief 应用初始化与主循环：初始化（对应符号 App_Init）。
 * @调用关系 直接调用者：main.c:main。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void App_Init(void)
{
#ifdef EEPROM_RESCUE_BUILD
    /* Keep every actuator in the boot-safe state established by main(). */
    GD32F407_Init();
    AppWatchdog_Init();
    Gtim_Init();
    Segment_ClearAll();
    Segment_Init();
    Segment_ClearAll();
    Iic_Init();
    Interrupt_Init();
    Gtim_Start();

    Segment_SetFaultCode(EEPROM_RESCUE_CODE_WORKING);
    GD32F407_HeartbeatSet(1U);

    if ((Eeprom_Init() != 0U) &&
        (App_EraseAndVerifyExternalEeprom() != 0U))
    {
        /*
         * Confirm only after every byte has been read back as 0xFF.  This
         * makes the rescue slot bootable until the user installs the newer
         * official package and avoids bootloader trial rollback.
         */
#ifdef BOOT_APP_BASE
        BootApp_Confirm();
#endif
        s_eeprom_rescue_result = 1U;
        Segment_SetFaultCode(EEPROM_RESCUE_CODE_SUCCESS);
    }
    else
    {
        s_eeprom_rescue_result = 2U;
        Segment_SetFaultCode(EEPROM_RESCUE_CODE_FAILURE);
        GD32F407_HeartbeatSet(1U);
    }
    s_eeprom_rescue_led_tick = HAL_GetTick();
    return;
#else
    GD32F407_Init();
    AppWatchdog_Init();
    audio_usart_init(9600U);
    Audio_Init();
    Rs485_Init(APP_RS485_BAUDRATE);
    MachineAddress_Init();

    /*
     * UART5_TX=PC12、UART5_RX=PD2、硬件自动收发方向、19200。
     * UART5 当前承载 Modbus RTU，因此默认关闭 printf 调试，避免污染总线。
     */
#if (APP_UART_DEBUG_ENABLE != 0U)
    printf("\r\nJoker main start\r\n");
#endif

    Gtim_Init();

    Segment_ClearAll();
    Segment_Init();
    Segment_ClearAll();
    JackpotDisplay_Init();

    Iic_Init();
    Pcf8563_Init();
#if (APP_UART_DEBUG_ENABLE != 0U)
    printf("RTC init ready=%u err=%u\r\n",
           (unsigned int)Pcf8563_IsReady(),
           (unsigned int)Pcf8563_GetLastErrorCode());
#endif
    Eeprom_Init();
    MachineAddress_Load();
    /* P29中屏=0时彻底关闭485角色；中屏=1时才按右屏0~7加入总线。 */
    Modbus_Init((MachineAddress_IsNetworkEnabled() != 0U) ?
                MachineAddress_Read() : 0xFFU);

    Solenoid_Init();
    Motor_Init();
    WiperMotor_Init();
    AwardLamp_Init();
    Meter_Init();
    Key_Init();
    Coin_Init();
    Eye_Init();
    Shake_Init();
    RemoteConfig_Init();

    Interrupt_Init();
    Gtim_Start();

    Game_Init();
    JackpotDisplay_SetValue(Jackpot_GetCurrent());
    JackpotDisplay_Task();
#endif
}

/**
 * @brief 应用层主循环单次入口。
 *
 * 先处理 Modbus 和共享彩池，使从机收到主机新彩金后在同一轮循环更新
 * 独立彩金板；随后再运行游戏状态机。
 */
/**
 * @brief 应用初始化与主循环：执行内部处理（对应符号 App_Loop）。
 * @调用关系 直接调用者：main.c:main。
 * @note AUTO-DOC：调用关系由本工程源码静态检索生成；硬件向量和函数指针调用需结合工程文件确认。
 */
void App_Loop(void)
{
#ifdef EEPROM_RESCUE_BUILD
    uint32_t now_ms;

    AppWatchdog_Feed();
    now_ms = HAL_GetTick();
    if ((s_eeprom_rescue_result == 1U) &&
        ((uint32_t)(now_ms - s_eeprom_rescue_led_tick) >= EEPROM_RESCUE_LED_STEP_MS))
    {
        s_eeprom_rescue_led_tick = now_ms;
        GD32F407_HeartbeatToggle();
    }
#ifdef BOOT_APP_BASE
    /* Failure remains unconfirmed and is allowed to follow normal rollback. */
    if (s_eeprom_rescue_result != 1U)
    {
        BootApp_CheckDeadline(now_ms);
    }
#endif
    return;
#else
    AppWatchdog_Feed();
    /* Preserve records after an I2C/verification failure. Holding E18 is safer
     * than continuing with default balances and committing them over old data.
     * This branch never clears player credit, pending output or EEPROM. */
    if (Eeprom_GetFault() != 0U)
    {
        static uint8_t storage_fault_entered;
        Motor_StopAll();
        WiperMotor_Stop();
        Solenoid_Release();
        Ticket_Pause();
        AwardLamp_Stop();
        Segment_SetFaultCode(18U);
        GD32F407_HeartbeatSet(1U);
        if (storage_fault_entered == 0U)
        {
            storage_fault_entered = 1U;
#ifdef BOOT_APP_BASE
            /* A data fault must not send this board back into a destructive
             * EEPROM-rescue image that may still occupy the previous slot. */
            BootApp_Confirm();
#endif
        }
        return;
    }
#ifdef BOOT_APP_BASE
    BootApp_CheckDeadline(HAL_GetTick());
#endif
    Modbus_Task();
    Jackpot_Task();
    JackpotDisplay_SetValue(Jackpot_GetCurrent());
    JackpotDisplay_Task();
    Game_Task();
#endif
}
