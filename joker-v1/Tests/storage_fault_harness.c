/* Actual persistence code over a fault-injected I2C device. No board is used. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define GD32F407_HAL_H
#define I2C_MEMADD_SIZE_16BIT 2U
#include "../BSP/Src/eeprom.c"
#include "../Service/Src/setting.c"
#include "../Service/Src/account.c"

static uint8_t ee[32768], saved[32768];
static uint8_t fail_mode, write_protect, offline;
static unsigned read_failures, bus_recoveries, write_calls;
uint8_t Iic_Init(void) { bus_recoveries++;return 1U; }
uint8_t Iic_IsDeviceReady(uint16_t a,uint32_t t,uint32_t m)
{(void)a;(void)t;(void)m;return offline?0U:1U;}
uint8_t Iic_MemRead(uint16_t d,uint16_t a,uint16_t s,uint8_t *p,uint16_t n,uint32_t t)
{
    (void)d;(void)s;(void)t;assert((unsigned)a+n<=sizeof(ee));
    if (read_failures) {read_failures--;return 0U;}
    if (offline || (fail_mode==1 && a<0x80) ||
        (fail_mode==2 && (a==0x100 || a==0x1400)) ||
        (fail_mode==3 && (a==0xc0 || a==0xd0))) return 0U;
    memcpy(p,ee+a,n);return 1U;
}
uint8_t Iic_MemWrite(uint16_t d,uint16_t a,uint16_t s,const uint8_t *p,uint16_t n,uint32_t t)
{
    (void)d;(void)s;(void)t;assert((unsigned)a+n<=sizeof(ee));
    assert((a%64U)+n<=64U);write_calls++;
    if (offline) return 0U;
    if (!write_protect) memcpy(ee+a,p,n);
    return 1U;
}
void Audio_SetVolumeLevelOn(Audio_Chip c,uint8_t n) {(void)c;(void)n;}
uint8_t MachineAddress_GetModeValue(void) {return 4;}
uint8_t MachineAddress_SetModeValue(uint8_t n) {(void)n;return 1;}
uint8_t MachineAddress_GetNodeId(void) {return 4;}
uint8_t MachineAddress_IsNetworkEnabled(void) {return 1;}
uint8_t MachineAddress_SetConfig(uint8_t net,uint8_t n) {(void)net;(void)n;return 1;}
uint32_t Gtim_NowMs(void) {return 1000;}
uint8_t Gtim_IsElapsed(uint32_t a,uint32_t b) {(void)a;(void)b;return 1;}
void Jackpot_ClearLocalPayoutTotal(void) {}

static void check_restored(void)
{
    Account_Data a;
    fail_mode=0;assert(Eeprom_Init());
    Account_Init();Setting_Init();Account_GetData(&a);
    assert(Eeprom_GetFault()==0);
    assert(a.coin_in_total==1234 && a.payout_total==567);
    assert(a.physical_coin_in_total==100 && a.physical_coin_out_total==20);
    assert(Setting_GetValue(SETTING_PARAM_IMMEDIATE_AWARD)==0);
    assert(Setting_GetValue(SETTING_PARAM_LOTTERY_PAYOUT_PERCENT)==96);
}

int main(void)
{
    unsigned mode, writes_before;
    uint8_t bytes[100], output[100];
    memset(ee,0xff,sizeof(ee));assert(Eeprom_Init());
    Account_Init();Setting_Init();
    Setting_SetValue(SETTING_PARAM_IMMEDIATE_AWARD,0);
    Setting_SetValue(SETTING_PARAM_LOTTERY_PAYOUT_PERCENT,96);
    Account_AddCoinIn(1234);Account_AddTotalPayout(567);
    Account_AddPhysicalCoinIn(100);Account_AddPhysicalCoinOut(20);
    assert(Account_SaveNow());assert(Account_SaveNow());
    check_restored();
    puts("PASS: new blank EEPROM initializes; P25, settings and four ledgers survive healthy reboot");

    for(mode=1;mode<=3;mode++)
    {
        memcpy(saved,ee,sizeof(ee));assert(Eeprom_Init());fail_mode=(uint8_t)mode;
        if(mode==1) Account_Init();else Setting_Init();
        assert(Eeprom_GetFault()!=0);writes_before=write_calls;
        (void)Account_SaveNow();(void)Setting_SaveNow();
        assert(write_calls==writes_before && memcmp(saved,ee,sizeof(ee))==0);
        check_restored();
    }
    puts("PASS: account/main-setting/P25-extension read faults preserve every EEPROM byte; next healthy boot restores data");

    memcpy(saved,ee,sizeof(ee));
    ee[0]^=0x40;ee[0x40]^=0x40;
    assert(Eeprom_Init());writes_before=write_calls;Account_Init();
    assert(Eeprom_GetFault()==4 && write_calls==writes_before);
    memcpy(ee,saved,sizeof(ee));check_restored();
    memcpy(saved,ee,sizeof(ee));ee[0x100]^=0x40;ee[0x1400]^=0x40;
    assert(Eeprom_Init());writes_before=write_calls;Setting_Init();
    assert(Eeprom_GetFault()==4 && write_calls==writes_before);
    memcpy(ee,saved,sizeof(ee));check_restored();
    puts("PASS: two corrupt nonblank records are protected instead of reinitialized to zero/default");

    /* Single-copy damage still permits recovery from a valid backup. */
    ee[0x100]^=0x40;assert(Eeprom_Init());Setting_Init();
    assert(Eeprom_GetFault()==0 && Setting_GetValue(SETTING_PARAM_IMMEDIATE_AWARD)==0);
    check_restored();
    read_failures=2;assert(Eeprom_Read(0x100,output,64));
    assert(Eeprom_GetFault()==0 && g_eeprom_read_retry_count==2 && bus_recoveries>=2);
    puts("PASS: one bad mirror recovers; two transient bus errors recover without latching a fault");

    /* P6/P7 may execute only after a live menu/wireless action, not on reboot. */
    Setting_SetValue(SETTING_PARAM_CLEAR_ACCOUNT,1);
    Setting_SetValue(SETTING_PARAM_FACTORY_RESET,1);
    assert(Setting_GetValue(SETTING_PARAM_CLEAR_ACCOUNT)==1);
    assert(Setting_GetValue(SETTING_PARAM_FACTORY_RESET)==1);
    Setting_Init();
    assert(Setting_GetValue(SETTING_PARAM_CLEAR_ACCOUNT)==0);
    assert(Setting_GetValue(SETTING_PARAM_FACTORY_RESET)==0);
    check_restored();
    puts("PASS: pending P6/P7 actions are not persisted or replayed on reboot");

    memset(bytes,0x5a,sizeof(bytes));assert(Eeprom_Write(0x601f,bytes,sizeof(bytes)));
    assert(Eeprom_Read(0x601f,output,sizeof(output)) && memcmp(bytes,output,sizeof(bytes))==0);
    write_protect=1;memset(bytes,0xa5,sizeof(bytes));
    assert(!Eeprom_Write(0x601f,bytes,sizeof(bytes)) && Eeprom_GetFault()==3);
    writes_before=write_calls;write_protect=0;
    assert(!Eeprom_Write(0x601f,bytes,sizeof(bytes)) && write_calls==writes_before);
    puts("PASS: page boundaries, readback verification and ACK-with-no-write detection; later writes remain blocked");
    offline=1;assert(!Eeprom_Init() && Eeprom_GetFault()==1);
    offline=0;assert(!Eeprom_Write(0x6000,bytes,1));
    assert(Eeprom_Init() && Eeprom_GetFault()==0);
    puts("PASS: offline startup blocks writes until explicit next boot initialization");
    return 0;
}
