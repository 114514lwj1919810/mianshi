"""Run actual account, settings, algorithm and extracted game functions on host.
Only simulated EEPROM / peripherals are touched; no board or production keys.
"""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
game = (ROOT / 'Service/Src/game.c').read_text(encoding='utf-8')
assert re.search(r'#define\s+GAME_ACCOUNT_CLOCK_PAGE_INDEX\s+0xFFU', game), 'C13 must not be intercepted by the old clock page'
def extract(name):
    m = re.search(r'(?:static )?(?:void|uint8_t|uint32_t) '+name+r'\([^;{}]*\)\s*\{', game)
    assert m, name
    end, depth = m.end(), 1
    while depth:
        depth += (game[end] == '{') - (game[end] == '}')
        end += 1
    return game[m.start():end] + '\n'

prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define EEPROM_GUARD_TEST_EXTERNAL
uint8_t Eeprom_Check(void);
uint8_t Gtim_IsElapsed(uint32_t,uint32_t);
#include "game.h"
#include "setting.h"
#include "account.c"
#include "setting.c"
#include "gift_algorithm.c"
static uint8_t ee[32768], fault, fail_write, reject_ticket;
static uint32_t now, jackpot_paid;
static uint16_t s_game_score,s_game_prize,s_game_coin;
static uint16_t s_line_payout_pending,s_card_payout_x,s_card_payout_y;
static uint8_t s_redeeming_credits,s_card_payout_requested;
static Game_Mode s_game_mode;
static uint32_t s_card_payout_remaining,s_ticket_display_remaining;
static uint32_t s_line_payout_hold_start_ms,s_state_start_ms,s_credit_transfer_ms;
static uint8_t s_line7_lock_active;
static uint16_t s_coins_in_channel_mask;
static uint32_t ticket_pending,payout_pending,settlement_calls;
#define GAME_TICKET_SHOW_HOLD_MS 1200U
#define GAME_CREDIT_TRANSFER_STEP_MS 250U
#define GAME_ACCOUNT_PAGE_COUNT 56U
#define TICKET_PENDING_MAX 99999999UL
#define GAME_AUDIO_TRIG_LINE_WIN 11U
uint32_t Gtim_NowMs(void){return now;}
uint8_t Gtim_IsElapsed(uint32_t a,uint32_t n){return now-a>=n;}
uint8_t Eeprom_IsReady(void){return fault==0;}
uint8_t Eeprom_Check(void){return fault==0;}
uint8_t Eeprom_GetFault(void){return fault;}
void Eeprom_ProtectData(uint16_t a){(void)a;fault=4;}
uint8_t Eeprom_Read(uint16_t a,uint8_t*p,uint16_t n){assert((unsigned)a+n<=sizeof(ee));memcpy(p,ee+a,n);return 1;}
uint8_t Eeprom_Write(uint16_t a,const uint8_t*p,uint16_t n){assert((unsigned)a+n<=sizeof(ee));if(fail_write||fault)return 0;memcpy(ee+a,p,n);return 1;}
void Audio_SetVolumeLevelOn(Audio_Chip c,uint8_t n){(void)c;(void)n;}
void Audio_PlayIndexOn(Audio_Chip c,uint16_t n){(void)c;(void)n;}
uint8_t MachineAddress_GetModeValue(void){return 0;}
uint8_t MachineAddress_SetModeValue(uint8_t n){(void)n;return 1;}
uint8_t MachineAddress_GetNodeId(void){return 0;}
uint8_t MachineAddress_IsNetworkEnabled(void){return 0;}
uint8_t MachineAddress_SetConfig(uint8_t a,uint8_t b){(void)a;(void)b;return 1;}
void Jackpot_ClearLocalPayoutTotal(void){jackpot_paid=0;}
void Jackpot_AddLocalPayout(uint32_t n){jackpot_paid+=n;}
uint32_t Jackpot_GetLocalPayoutTotal(void){return jackpot_paid;}
uint16_t Jackpot_GetCurrent(void){return 100;}
static uint8_t Ticket_IsBusy(void){return ticket_pending!=0;}
static uint8_t Payout_IsBusy(void){return payout_pending!=0;}
static uint8_t Ticket_Add(uint32_t n){if(reject_ticket)return 0;ticket_pending+=n;return 1;}
static uint8_t Payout_AddPrize(uint16_t n){payout_pending+=n;return 1;}
static void Game_EnterIdleOrReady(void){s_game_mode=s_game_coin?GAME_MODE_READY:GAME_MODE_IDLE;}
void Game_SetPrize(uint16_t n){s_game_prize=n;Account_SetGameState(s_game_score,n,s_game_coin);}
void Game_SetCoin(uint16_t n){s_game_coin=n;Account_SetGameState(s_game_score,s_game_prize,n);}
static uint8_t Game_BeginLineSettlement(void){settlement_calls++;return 1;}
static uint8_t Game_TransferScoreToCredits(void){return Game_BeginLineSettlement();}
static uint8_t Game_StartCardPayout(void);
static uint8_t Game_RedeemCreditsToAward(void);
'''
array = re.search(r'static const uint8_t s_account_page_to_legacy\[GAME_ACCOUNT_PAGE_COUNT\]\s*=\s*\{.*?\};', game, re.S)
assert array
code = prefix + array.group() + '\n'
for name in ('Game_BeginCreditTransfer','Game_UpdateCreditTransfer','Game_CalcTicketPulseCount',
             'Game_RecordReturnCommitment','Game_AddLotteryPrizeAndPayout',
             'Game_AddLinePrizeAndHold','Game_StartCardPayout','Game_RedeemCreditsToAward',
             'Game_HandleTicketKey','Game_GetAccountPageValue'):
    code += extract(name)
code += r'''
static void reset(void){
 memset(ee,0xff,sizeof(ee));fault=0;fail_write=0;reject_ticket=0;now=1000;
 jackpot_paid=0;ticket_pending=0;payout_pending=0;settlement_calls=0;s_line_payout_pending=0;
 s_card_payout_remaining=0;s_game_prize=0;s_game_score=0;s_game_coin=0;
 s_redeeming_credits=0;s_line7_lock_active=0;Account_Init();Setting_Init();
 GiftAlgorithm_Init();s_game_mode=GAME_MODE_READY;
}
int main(void){
 Account_Data a;GiftAlgorithm_Result draw;GiftAlgorithm_Diagnostics diag;
 uint32_t old_award,old_line,old_jackpot;unsigned i,mode,cycle;
 reset();assert(Account_IsReady());
 for(i=0;i<=100;i++){
   Setting_SetValue(SETTING_PARAM_RECOVERY_DROP,(uint16_t)i);
   assert(Setting_GetValue(SETTING_PARAM_RECOVERY_DROP)==i);
 }
 Setting_Init();assert(Setting_GetValue(SETTING_PARAM_RECOVERY_DROP)==100);
 assert(GiftAlgorithm_GetConfiguredPercent(1)==0);
 Setting_SetValue(SETTING_PARAM_RECOVERY_DROP,1000);
 assert(Setting_GetValue(SETTING_PARAM_RECOVERY_DROP)==100);
 Setting_SetValue(SETTING_PARAM_RECOVERY_DROP,0);
 assert(Setting_AdjustValue(SETTING_PARAM_RECOVERY_DROP,0)==0);
 for(i=0;i<101;i++)Setting_AdjustValue(SETTING_PARAM_RECOVERY_DROP,1);
 assert(Setting_GetValue(SETTING_PARAM_RECOVERY_DROP)==100);
 puts("PASS P15 0..100, clamping, key adjustment, persistence and zero effective rate");
 reset();
 assert(Setting_GetJackpotBase()==20);
 assert(Setting_GetValue(SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL)==30);
 Setting_SetValue(SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL,10);
 assert(Setting_SaveNow());Setting_Init();
 assert(Setting_GetValue(SETTING_PARAM_MAJOR_POOL_STAKE_INTERVAL)==10);
 Account_AddCoinIn(9);GiftAlgorithm_OnStake();
 GiftAlgorithm_GetDiagnostics(0,&diag);assert(diag.reserved==0&&diag.available==8);
 Account_AddCoinIn(1);GiftAlgorithm_OnStake();
 GiftAlgorithm_GetDiagnostics(0,&diag);assert(diag.reserved==1&&diag.available==8);
 GiftAlgorithm_Task();assert(Account_SaveNow());Account_Init();GiftAlgorithm_Init();
 GiftAlgorithm_GetDiagnostics(0,&diag);assert(diag.reserved==1&&diag.available==8);
 Account_AddTotalPayout(30);Account_AddCoinIn(10);GiftAlgorithm_OnStake();
 GiftAlgorithm_GetDiagnostics(0,&diag);
 assert(diag.recovery==1&&diag.reserved==1&&diag.available==8);
 puts("PASS P9 base20; P17 persisted; each 10 positive stakes moves one C22 coin to D; deficit does not fund D");
 reset();
 Account_AddLotteryPayout(3);Account_AddLotteryPayout(3);
 Account_AddLotteryPayout(5);Account_AddLotteryPayout(10);
 assert(Account_SaveNow());assert(Account_SaveNow());Account_Init();
 Account_GetData(&a);
 assert(a.lottery_coin3_value_total==6&&a.lottery_coin5_value_total==5&&a.lottery_coin10_value_total==10);
 assert(a.lottery_payout_total==21);
 assert(Game_GetAccountPageValue(9)==6&&Game_GetAccountPageValue(10)==5&&Game_GetAccountPageValue(11)==10);
 assert(Game_GetAccountPageValue(8)==21);
 assert(Game_GetAccountPageValue(47)==GAME_FIRMWARE_VERSION);
 jackpot_paid=123;assert(Game_GetAccountPageValue(12)==123);
 for(i=0;i<GAME_ACCOUNT_PAGE_COUNT;i++)(void)Game_GetAccountPageValue((uint8_t)i);
 ee[ACCOUNT_COIN_PRIZE_SLOT0_ADDR]^=0x40;Account_Init();
 assert(Account_IsReady());Account_GetData(&a);assert(a.lottery_coin10_value_total==10);
 fail_write=1;Account_AddLotteryPayout(5);assert(!Account_SaveNow());fail_write=0;
 assert(Account_SaveNow());Account_Init();Account_GetData(&a);assert(a.lottery_coin5_value_total==10);
 Account_ClearBookData();Account_Init();Account_GetData(&a);
 assert(a.lottery_payout_total==0&&a.lottery_coin3_value_total==0&&a.lottery_coin5_value_total==0&&a.lottery_coin10_value_total==0);
 puts("PASS actual prize subtotals, C mapping, EEPROM reload, mirror recovery, failed-write retry and clear");
 reset();assert(Account_SaveNow());
 ee[ACCOUNT_COIN_PRIZE_SLOT0_ADDR]^=0x40;ee[ACCOUNT_COIN_PRIZE_SLOT1_ADDR]^=0x40;
 Account_Init();assert(!Account_IsReady()&&fault==4);
 puts("PASS two nonblank corrupt prize mirrors protect data instead of silently resetting");
 for(cycle=0;cycle<=1;cycle++)for(mode=1;mode<=4;mode++){
   reset();Setting_SetValue(SETTING_PARAM_AWARD_MODE,(uint16_t)mode);
   Setting_SetValue(SETTING_PARAM_IMMEDIATE_AWARD,(uint16_t)cycle);
   Game_SetCoin(20);s_game_score=4;
   Account_GetData(&a);old_award=a.payout_total;old_line=a.line_ticket_total;old_jackpot=jackpot_paid;
   Game_HandleTicketKey();assert(s_game_coin==20&&s_game_score==4);
   assert(settlement_calls==1);
   s_game_score=0;
   Game_HandleTicketKey();assert(s_game_coin==0);
   Account_GetData(&a);assert(a.payout_total==old_award&&a.line_ticket_total==old_line&&jackpot_paid==old_jackpot);
   assert(!Account_IsCreditTransfer());
   if(mode==1||mode==3)assert(s_line_payout_pending==20);
   if(mode==2||mode==3||mode==4)assert(ticket_pending>0);
   assert(Account_SaveNow());
 }
 puts("PASS left award before right refund in both P14 modes / all P1 outputs");
 reset();Setting_SetValue(SETTING_PARAM_AWARD_MODE,2);Game_SetCoin(8);reject_ticket=1;
 Game_HandleTicketKey();assert(s_game_coin==8&&s_game_prize==0);
 reject_ticket=0;Game_HandleTicketKey();assert(s_game_coin==0&&ticket_pending>0);
 reset();Account_SetCreditTransfer(1);Game_SetPrize(3);Game_SetCoin(5);
 Game_HandleTicketKey();assert(s_game_coin==5&&s_game_prize==3);
 Account_SetCreditTransfer(0);Game_SetPrize(0);s_line_payout_pending=7;
 Game_HandleTicketKey();assert(s_game_coin==5);
 puts("PASS failed queue and active transfer/pending batch never swallow credits");
 reset();Setting_SetValue(SETTING_PARAM_IMMEDIATE_AWARD,0);
 Account_AddCoinIn(12);GiftAlgorithm_OnStake();GiftAlgorithm_Select(0,&draw);
 assert(draw.type==GIFT_ALGORITHM_AWARD_COIN_10);
 assert(Game_AddLotteryPrizeAndPayout(10));
 assert(!Game_AddLotteryPrizeAndPayout(10)); /* 同一许可不能入账两次 */
 assert(Game_GetAccountPageValue(11)==10&&Game_GetAccountPageValue(3)==10);
 for(i=0;i<10;i++){now+=250;Game_UpdateCreditTransfer();}
 assert(s_game_coin==10&&s_game_prize==0);
 Game_HandleTicketKey();assert(s_game_coin==0&&s_line_payout_pending==10);
 assert(Game_GetAccountPageValue(11)==10&&Game_GetAccountPageValue(3)==10);
 puts("PASS actual award -> middle -> credits -> refund counts one award only");
 reset();Setting_SetValue(SETTING_PARAM_LOTTERY_PAYOUT_PERCENT,95);
 Setting_SetValue(SETTING_PARAM_RECOVERY_DROP,15);
 Account_AddCoinIn(100);Account_AddTotalPayout(130);
 assert(GiftAlgorithm_ResetForAccountClear());
 for(i=0;i<20;i++){GiftAlgorithm_Select(0,&draw);assert(draw.type==GIFT_ALGORITHM_AWARD_NONE);}
 GiftAlgorithm_GetDiagnostics(0,&diag);assert(diag.recovery==1&&diag.available==0);
 Account_AddCoinIn(13);GiftAlgorithm_OnStake();
 GiftAlgorithm_GetDiagnostics(0,&diag);
 assert(diag.recovery==1&&diag.available==10);
 GiftAlgorithm_Select(0,&draw);
 GiftAlgorithm_GetDiagnostics(0,&diag);
 assert(draw.type==GIFT_ALGORITHM_AWARD_COIN_10&&diag.effective_percent==80);
 assert(diag.available==0);
 GiftAlgorithm_CommitCoinAward(10);
 for(i=0;i<20;i++){GiftAlgorithm_Select(0,&draw);assert(draw.type==GIFT_ALGORITHM_AWARD_NONE);}
 puts("PASS deficit E updates immediately per stake; empty E cannot award; 13 stakes at 80% fund 10");
 reset();Setting_SetValue(SETTING_PARAM_IMMEDIATE_AWARD,0);
 Account_AddCoinIn(100);Account_AddTotalPayout(130);
 assert(GiftAlgorithm_ResetForAccountClear());
 assert(!Game_AddLotteryPrizeAndPayout(3));
 for(i=0;i<5;i++){
   Account_AddCoinIn(1);GiftAlgorithm_OnStake();GiftAlgorithm_OnStake();
   GiftAlgorithm_Select(0,&draw);
   if(i==3){assert(draw.type==GIFT_ALGORITHM_AWARD_COIN_3);assert(Game_AddLotteryPrizeAndPayout(3));}
   else assert(draw.type==GIFT_ALGORITHM_AWARD_NONE);
 }
 Account_GetData(&a);assert(a.lottery_payout_total==3);
 GiftAlgorithm_GetDiagnostics(0,&diag);
 assert(diag.available==1&&diag.recovery_stakes==5&&diag.recovery_funded_cents==400&&diag.recovery_paid==3);
 assert(Game_GetAccountPageValue(52)==5&&Game_GetAccountPageValue(53)==400&&Game_GetAccountPageValue(54)==3);
 assert(Account_SaveNow());GiftAlgorithm_Task();Account_Init();GiftAlgorithm_Init();
 GiftAlgorithm_GetDiagnostics(0,&diag);assert(diag.available==1&&diag.recovery_paid==3);
 /* 单余额损坏或last_stake倒退不能制造新E预算。 */
 s_recovery_pool_scaled=100*ACCOUNT_PAYOUT_VALUE_SCALE;s_last_stake-=5;
 GiftAlgorithm_OnStake();GiftAlgorithm_GetDiagnostics(0,&diag);assert(diag.available==1);
 for(i=0;i<10;i++){GiftAlgorithm_Select(0,&draw);assert(draw.type==GIFT_ALGORITHM_AWARD_NONE);}
 fail_write=1;Account_AddCoinIn(3);GiftAlgorithm_OnStake();GiftAlgorithm_Select(0,&draw);
 assert(draw.type==GIFT_ALGORITHM_AWARD_NONE&&!Game_AddLotteryPrizeAndPayout(3));
 fail_write=0;GiftAlgorithm_Select(0,&draw);assert(draw.type==GIFT_ALGORITHM_AWARD_COIN_3);
 assert(GiftAlgorithm_CoinAwardAuthorized(3));GiftAlgorithm_CommitCoinAward(3);
 GiftAlgorithm_GetDiagnostics(0,&diag);assert(diag.available==0&&diag.recovery_paid==6);
 puts("PASS five sequential stakes award exactly3; no duplicate grants; audit reload, stale checkpoint and failed save cannot overspend");
 return 0;
}
'''

with tempfile.TemporaryDirectory(prefix='joker-10200001-') as folder:
    work = Path(folder)
    (work/'check.c').write_text(code,encoding='utf-8')
    vc = r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat'
    include = ' '.join('/I"'+str(ROOT/p)+'"' for p in ('Tests/gameplay_stubs','Tests/stubs','Service/Src','Service/Inc','BSP/Inc'))
    command=f'call "{vc}" >nul && cl /nologo /TC /utf-8 /W3 {include} check.c "{ROOT / "Service/Src/score.c"}" /Fe:check.exe'
    result=subprocess.run(command,shell=True,cwd=work,capture_output=True)
    if result.returncode:
        raise RuntimeError((result.stdout+result.stderr).decode(errors='replace'))
    subprocess.run([str(work/'check.exe')],cwd=work,check=True)
