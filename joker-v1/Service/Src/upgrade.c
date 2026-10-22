/* AUTO-FILE-DOC
 * 文件职责：升级兼容入口。
 * 阅读方法：函数注释列出直接调用者、参数/指针方向和关键位运算；文件级变量的完整引用位置见工程根目录《代码阅读说明.md》。
 * 范围声明：本注释描述当前源码，不替代原理图、电气极性和硬件实测。
 */
#include "upgrade.h"

#include "account.h"
#ifdef BOOT_APP_BASE
#include "boot_contract.h"
#endif
#include "eeprom.h"
#include "jackpot.h"
#include "lottery.h"
#include "setting.h"

#include <string.h>

#ifdef BOOT_APP_BASE
/* 与机台地址和打码记录各自隔离。仅IAP目标依赖Boot元数据。 */
#define UPGRADE_MARKER_A       0x1520U
#define UPGRADE_MARKER_B       0x1540U
#define UPGRADE_MARKER_MAGIC   0x55505231UL
#define UPGRADE_PHASE_PENDING  1UL
#define UPGRADE_PHASE_DONE     2UL

typedef struct {
    uint32_t magic;
    uint32_t release;
    uint32_t generation;
    uint32_t phase;
    uint32_t checksum;
} Upgrade_Marker;

typedef char Upgrade_MarkerSizeCheck[(sizeof(Upgrade_Marker) == 20U) ? 1 : -1];

static uint8_t Upgrade_MarkerValid(const Upgrade_Marker *m)
{
    return ((m->magic == UPGRADE_MARKER_MAGIC) &&
            ((m->phase == UPGRADE_PHASE_PENDING) ||
             (m->phase == UPGRADE_PHASE_DONE)) &&
            (m->checksum == Boot_Crc(m, 16U))) ? 1U : 0U;
}

static uint8_t Upgrade_MarkerWrite(uint16_t address, uint32_t release,
                                   uint32_t generation, uint32_t phase)
{
    Upgrade_Marker marker, verify;
    marker.magic = UPGRADE_MARKER_MAGIC;
    marker.release = release;
    marker.generation = generation;
    marker.phase = phase;
    marker.checksum = Boot_Crc(&marker, 16U);
    return ((Eeprom_Write(address, (const uint8_t *)&marker,
                          (uint16_t)sizeof(marker)) != 0U) &&
            (Eeprom_Read(address, (uint8_t *)&verify,
                         (uint16_t)sizeof(verify)) != 0U) &&
            (memcmp(&marker, &verify, sizeof(marker)) == 0)) ? 1U : 0U;
}

#endif

/* 只在 TF 包产生的试运行状态写 PENDING；Keil 直接下载没有试运行记录。 */
uint8_t Upgrade_PrepareTrialRelease(void)
{
#ifdef BOOT_APP_BASE
    BootRecord boot;
    Upgrade_Marker a, b, current;
    uint8_t valid_a, valid_b, current_slot;
    uint32_t release;
    uint8_t i;
    const uint8_t *pa, *pb;

    if ((Boot_Load(&boot) == 0) || (boot.active > 1U)) return 2U;
    if ((boot.phase != BOOT_TRIAL) || (boot.pending != boot.active)) return 0U;
    release = boot.release[boot.active];
    if (release == 0U) return 0U;
    if ((Eeprom_Read(UPGRADE_MARKER_A, (uint8_t *)&a, sizeof(a)) == 0U) ||
        (Eeprom_Read(UPGRADE_MARKER_B, (uint8_t *)&b, sizeof(b)) == 0U))
        return 2U;
    valid_a = Upgrade_MarkerValid(&a);
    valid_b = Upgrade_MarkerValid(&b);
    if ((valid_a == 0U) && (valid_b == 0U))
    {
        pa = (const uint8_t *)&a;
        pb = (const uint8_t *)&b;
        for (i = 0U; i < sizeof(a); i++)
            if ((pa[i] != 0xFFU) || (pb[i] != 0xFFU))
            {
                Eeprom_ProtectData(UPGRADE_MARKER_A);
                return 2U;
            }
        memset(&current, 0, sizeof(current));
        current_slot = 1U;
    }
    else if ((valid_b != 0U) && ((valid_a == 0U) ||
             ((int32_t)(b.generation - a.generation) > 0)))
    {
        current = b;
        current_slot = 1U;
    }
    else
    {
        current = a;
        current_slot = 0U;
    }
    if ((current.release == release) &&
        ((current.phase == UPGRADE_PHASE_DONE) ||
         (current.phase == UPGRADE_PHASE_PENDING))) return 0U;
    if (Upgrade_MarkerWrite((current_slot == 0U) ? UPGRADE_MARKER_B :
                            UPGRADE_MARKER_A, release,
                            current.generation + 1U,
                            UPGRADE_PHASE_PENDING) == 0U)
    {
        Eeprom_ProtectData(UPGRADE_MARKER_A);
        return 2U;
    }
#endif
    return 0U;
}

uint8_t Upgrade_ResetOnNewRelease(void)
{
#ifdef BOOT_APP_BASE
    BootRecord boot;
    Upgrade_Marker a, b, current;
    uint8_t valid_a, valid_b;
    uint8_t current_slot;
    uint32_t release;

    if ((Boot_Load(&boot) == 0) || (boot.active > 1U)) return 2U;
    release = boot.release[boot.active];
    if (release == 0U) return 0U;
    if ((Eeprom_Read(UPGRADE_MARKER_A, (uint8_t *)&a, sizeof(a)) == 0U) ||
        (Eeprom_Read(UPGRADE_MARKER_B, (uint8_t *)&b, sizeof(b)) == 0U))
        return 2U;
    valid_a = Upgrade_MarkerValid(&a);
    valid_b = Upgrade_MarkerValid(&b);
    if ((valid_a == 0U) && (valid_b == 0U))
    {
        const uint8_t *pa = (const uint8_t *)&a;
        const uint8_t *pb = (const uint8_t *)&b;
        uint8_t i;
        for (i = 0U; i < sizeof(a); i++)
            if ((pa[i] != 0xFFU) || (pb[i] != 0xFFU))
            {
                Eeprom_ProtectData(UPGRADE_MARKER_A);
                return 2U;
            }
        return 0U; /* 没有 TF 试运行标记；Keil直烧绝不清账。 */
    }
    else if ((valid_b != 0U) && ((valid_a == 0U) ||
             ((int32_t)(b.generation - a.generation) > 0)))
    {
        current = b;
        current_slot = 1U;
    }
    else
    {
        current = a;
        current_slot = 0U;
    }
    if ((current.release != release) ||
        (current.phase == UPGRADE_PHASE_DONE))
        return 0U;

    /* 不触碰P29、产品身份和MachineLock(A1/锁定/打码次数)。 */
    Setting_RestoreDefault();
    if (Setting_IsReady() == 0U) goto storage_error;
    if (Account_ResetForUpgrade() == 0U) goto storage_error;
    if (Jackpot_ResetForUpgrade() == 0U) goto storage_error;
    if (Lottery_ResetForAccountClear() == 0U) goto storage_error;

    if (Upgrade_MarkerWrite((current_slot == 0U) ? UPGRADE_MARKER_B :
                            UPGRADE_MARKER_A, release,
                            current.generation + 1U,
                            UPGRADE_PHASE_DONE) == 0U)
        goto storage_error;
    return 1U;

storage_error:
    Eeprom_ProtectData(UPGRADE_MARKER_A);
    return 2U;
#else
    return 0U;
#endif
}

/* 原注释编码已经损坏；本文件职责、函数调用和变量说明已由 AUTO-DOC 重新整理。 */
