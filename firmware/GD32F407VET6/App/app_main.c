#include "app_main.h"
#include "game_authority.h"
#include "settings_flash.h"
#include "board_io.h"
#include "board_timer.h"
#include "input_service.h"
#include "protocol_parser.h"
#include "protocol_builder.h"
#include "machine_protocol.h"
#include "serial_port.h"
#ifdef BOOT_APP_BASE
#include "boot_contract.h"
#include "gd32f4xx_fwdgt.h"
#endif
#include <stdio.h>
#include <string.h>

#define STANDBY_RESULT_HOLD_MS 3000U
#define STANDBY_RACE_TIMEOUT_GRACE_MS 5000U
#define STANDBY_RESULT_TIMEOUT_GRACE_MS 4000U
#define STANDBY_RESTART_DELAY_MS 3000U
#define GAMEPLAY_SAVE_DELAY_MS 200U
#define CRITICAL_RETRY_MS 1500U
#define CRITICAL_RETRY_LIMIT 2U
#define CRITICAL_RACE 1U
#define CRITICAL_RESULT 2U
#define CRITICAL_WIN_READY 3U

static GameAuthorityState game,candidate,pending;
static uint8_t ready,storage_ok,loaded,pending_save,gameplay_save_pending,betting_state_pending,authenticated,payout_running;
static uint32_t hello_tick,snapshot_tick,retry_tick,runtime_tick,payout_tick;
static uint32_t unbooked_coins;
static uint32_t full_seq,checkpoint_tick,gameplay_save_tick;
static uint8_t full_wait;
static uint8_t critical_kind,critical_retries;
static uint32_t critical_seq,critical_retry_tick;
static StandbyAnimationState standby_state;
static uint32_t standby_seq,standby_round,standby_deadline,standby_rng,standby_restart_tick;
static uint8_t standby_winner;
static char tx[SERIAL_PORT_MAX_LINE_LENGTH+1];
#ifdef BOOT_APP_BASE
static uint8_t boot_trial_confirmed;
static uint32_t boot_trial_started;
#endif

static uint8_t Send(uint8_t built) { return built && SerialPort_SendLine(tx); }
static void TrackCritical(uint8_t kind,uint32_t seq,uint8_t sent)
{
    if(!sent) return;
    critical_kind=kind; critical_seq=seq; critical_retries=0U;
    critical_retry_tick=BoardTimer_Millis()+CRITICAL_RETRY_MS;
}
static void Configure(void)
{
    BoardIO_ConfigureCounters((uint8_t)game.settings.coin_input,(uint8_t)game.settings.payout_input,(uint8_t)game.settings.acceptor_type);
}
static void StopPayout(void)
{
    if(game.settings.payout_output<16) BoardIO_WriteOutput((uint8_t)game.settings.payout_output,0);
    payout_running=0;
}
static uint8_t Commit(void)
{
    StopPayout();
    if(RuntimeStorage_Save(&candidate)) {
        game=candidate; storage_ok=1; pending_save=0; gameplay_save_pending=0; Configure(); return 1;
    }
    pending=candidate; pending_save=1; gameplay_save_pending=0;
    storage_ok=0; retry_tick=BoardTimer_Millis(); return 0;
}
static void ApplyGameplayChange(uint32_t now,uint8_t requires_save)
{
    game=candidate;
    if(requires_save) gameplay_save_pending=1;
    if(gameplay_save_pending) gameplay_save_tick=now+GAMEPLAY_SAVE_DELAY_MS;
}
static void Settings(void) { Send(MachineProtocol_Settings(tx,sizeof(tx),&game,storage_ok)); }
static void Account(void) { Send(MachineProtocol_Account(tx,sizeof(tx),&game)); }
static void Balance(void)
{
    snprintf(tx,sizeof(tx),"BALANCE_UPDATE:seq=%lu,round=%lu,credit=%lu,win=%lu,delta=0,source=COIN",
        (unsigned long)GameAuthority_NextSequence(&game),(unsigned long)game.round_id,
        (unsigned long)game.balance_points,(unsigned long)game.win_remaining_points);
    Send(1);
}
static uint8_t IsEmpty(void)
{ return !game.balance_points && !game.win_remaining_points && !game.pending_payout_coins && !GameAuthority_TotalBet(&game); }
static uint8_t StandbyEligible(void)
{
    return ready && storage_ok && !pending_save && !game.fault_code &&
        game.state==GAME_STATE_IDLE && IsEmpty();
}
static uint8_t StandbyRandomWinner(uint32_t now)
{
    uint32_t value=standby_rng^now^game.seq^(standby_round*0x9E3779B9UL)^game.settings.device_id;
    if(!value) value=0x6D2B79F5UL;
    value^=value<<13; value^=value>>17; value^=value<<5;
    standby_rng=value;
    return (uint8_t)(value%6U+1U);
}
static void StandbySend(void)
{
    if(!ready || standby_state==STANDBY_ANIMATION_OFF) return;
    if(standby_state==STANDBY_ANIMATION_RACE)
        Send(ProtocolBuilder_ShowDemoRace(tx,sizeof(tx),&game,standby_seq,standby_round,
            standby_winner,game.settings.race_duration_ms));
    else
        Send(ProtocolBuilder_ShowDemoResult(tx,sizeof(tx),&game,standby_seq,standby_round,
            standby_winner,STANDBY_RESULT_HOLD_MS));
}
static void StandbyStartRace(uint32_t now)
{
    if(++standby_round==0U) standby_round=1U;
    standby_winner=StandbyRandomWinner(now);
    standby_seq=GameAuthority_NextSequence(&game);
    standby_deadline=now+game.settings.race_duration_ms+STANDBY_RACE_TIMEOUT_GRACE_MS;
    standby_state=STANDBY_ANIMATION_RACE;
    StandbySend();
}
static void StandbyShowResult(uint32_t now)
{
    standby_seq=GameAuthority_NextSequence(&game);
    /* Includes the Unity finish-line freeze before its three-second result hold. */
    standby_deadline=now+STANDBY_RESULT_HOLD_MS+STANDBY_RESULT_TIMEOUT_GRACE_MS;
    standby_state=STANDBY_ANIMATION_RESULT;
    StandbySend();
}
static void StandbyStop(uint32_t now)
{
    standby_state=STANDBY_ANIMATION_OFF;
    standby_seq=0U; standby_deadline=0U; standby_winner=0U;
    standby_restart_tick=now+STANDBY_RESTART_DELAY_MS;
}
static void StandbyPoll(uint32_t now)
{
    if(!StandbyEligible()) {
        if(standby_state!=STANDBY_ANIMATION_OFF) StandbyStop(now);
        return;
    }
    if(standby_state==STANDBY_ANIMATION_OFF) {
        if((int32_t)(now-standby_restart_tick)>=0) StandbyStartRace(now);
        return;
    }
    if((int32_t)(now-standby_deadline)<0) return;
    if(standby_state==STANDBY_ANIMATION_RACE) StandbyShowResult(now);
    else StandbyStartRace(now);
}
static void Full(void)
{
    full_seq=GameAuthority_NextSequence(&game); full_wait=1;
    Send(ProtocolBuilder_FullState(tx,sizeof(tx),&game,full_seq));
    /* Reconnect must restore an in-progress attract presentation immediately. */
    StandbySend();
    snapshot_tick=BoardTimer_Millis();
}
static void FlushBettingState(void)
{
    if(ready && betting_state_pending && SerialPort_TxIdle()) {
        if(ProtocolBuilder_ShowBetting(tx,sizeof(tx),&game,GameAuthority_NextSequence(&game)) &&
           SerialPort_SendLine(tx)) betting_state_pending=0;
    }
}
static void State(void)
{
    uint32_t seq;
    if(!ready) return;
    critical_kind=0U;
    if(game.state!=GAME_STATE_BETTING) betting_state_pending=0;
    if(game.fault_code) {
        betting_state_pending=0;
        snprintf(tx,sizeof(tx),"SHOW_FAULT:seq=%lu,code=%lu,detail=PAYOUT_CHECK,recoverable=1",
            (unsigned long)GameAuthority_NextSequence(&game),(unsigned long)game.fault_code);
        Send(1); return;
    }
    if(standby_state!=STANDBY_ANIMATION_OFF) { StandbySend(); return; }
    switch(game.state) {
    case GAME_STATE_WAIT_RACE_OVER:
        TrackCritical(CRITICAL_RACE,game.race_seq,Send(ProtocolBuilder_ShowRace(tx,sizeof(tx),&game))); break;
    case GAME_STATE_WAIT_RESULT_OVER:
        TrackCritical(CRITICAL_RESULT,game.result_seq,Send(ProtocolBuilder_ShowResult(tx,sizeof(tx),&game))); break;
    case GAME_STATE_WIN_COLLECTING: Send(ProtocolBuilder_WinTransferUpdate(tx,sizeof(tx),&game)); break;
    case GAME_STATE_WIN_WAIT_START:
        seq=GameAuthority_NextSequence(&game);
        TrackCritical(CRITICAL_WIN_READY,seq,Send(ProtocolBuilder_ShowWinReady(tx,sizeof(tx),&game,seq))); break;
    case GAME_STATE_PAYOUT:
        snprintf(tx,sizeof(tx),"SHOW_PAYOUT_PROGRESS:seq=%lu,target=%lu,done=%lu,remainder=%lu,credit=%lu",
            (unsigned long)GameAuthority_NextSequence(&game),(unsigned long)game.payout_target,
            (unsigned long)game.payout_done,(unsigned long)game.balance_points,(unsigned long)game.balance_points);
        Send(1); break;
    case GAME_STATE_SETTING: SerialPort_SendLine("SERVICE_STATE:open=1"); break;
    case GAME_STATE_IDLE: Send(ProtocolBuilder_ShowIdle(tx,sizeof(tx),&game,GameAuthority_NextSequence(&game))); break;
    case GAME_STATE_BETTING: betting_state_pending=1; break;
    default: Send(ProtocolBuilder_ShowBetting(tx,sizeof(tx),&game,GameAuthority_NextSequence(&game))); break;
    }
}
static void RetryCritical(uint32_t now)
{
    uint8_t built=0U;
    if(!ready || !critical_kind || critical_retries>=CRITICAL_RETRY_LIMIT ||
       (int32_t)(now-critical_retry_tick)<0 || !SerialPort_TxIdle()) return;
    if(critical_kind==CRITICAL_RACE && game.state==GAME_STATE_WAIT_RACE_OVER && game.race_seq==critical_seq)
        built=ProtocolBuilder_ShowRace(tx,sizeof(tx),&game);
    else if(critical_kind==CRITICAL_RESULT && game.state==GAME_STATE_WAIT_RESULT_OVER && game.result_seq==critical_seq)
        built=ProtocolBuilder_ShowResult(tx,sizeof(tx),&game);
    else if(critical_kind==CRITICAL_WIN_READY && game.state==GAME_STATE_WIN_WAIT_START)
        built=ProtocolBuilder_ShowWinReady(tx,sizeof(tx),&game,critical_seq);
    else { critical_kind=0U; return; }
    if(Send(built)) { critical_retries++; critical_retry_tick=now+CRITICAL_RETRY_MS; }
}
static void Reply(uint32_t req,const char *status)
{
    snprintf(tx,sizeof(tx),"CONTROL_RESULT:req=%lu,status=%s,rev=%lu",(unsigned long)req,status,(unsigned long)game.settings_revision);
    Send(1);
}
static void Control(const MachineRequest *r)
{
    uint8_t i; GameSettings settings;
    if(r->type==MC_SETTINGS_GET) { Settings(); return; }
    if(r->type==MC_ACCOUNT_GET) { Account(); return; }
    if(standby_state!=STANDBY_ANIMATION_OFF) StandbyStop(BoardTimer_Millis());
    if(!storage_ok || pending_save) { Reply(r->req,"STORAGE_ERROR"); Settings(); return; }
    candidate=game;
    if(r->type==MC_ENTER) {
        if(game.state!=GAME_STATE_SETTING && !GameAuthority_ToggleSetting(&candidate)) { Reply(r->req,"BUSY"); return; }
    } else if(r->type==MC_EXIT) {
        if(game.state==GAME_STATE_SETTING) GameAuthority_ToggleSetting(&candidate);
        authenticated=0;
    } else {
        if(game.state!=GAME_STATE_SETTING) { Reply(r->req,"BUSY"); return; }
        if(r->type==MC_AUTH) { authenticated=r->password==game.settings.system_password; Reply(r->req,authenticated?"OK":"AUTH_REQUIRED"); return; }
        if(r->type==MC_SETTINGS_APPLY) {
            if(r->req==game.last_settings_request && r->base==game.last_settings_base) { Reply(r->req,"OK"); Settings(); return; }
            if(r->base!=game.settings_revision) { Reply(r->req,"CONFLICT"); Settings(); return; }
            settings=game.settings;
            for(i=0;i<GAME_AUTHORITY_SETTINGS_COUNT;i++) {
                if(i==31 && r->values[i]==0) continue;
                if(!GameSettings_Set(&settings,i,r->values[i])) { Reply(r->req,"INVALID_VALUE"); return; }
            }
            if(!GameSettings_Validate(&settings)) { Reply(r->req,"INVALID_VALUE"); return; }
            if(!authenticated && (settings.device_id!=game.settings.device_id || settings.system_formula!=game.settings.system_formula ||
                settings.system_password!=game.settings.system_password || settings.running_minutes!=game.settings.running_minutes)) {
                Reply(r->req,"AUTH_REQUIRED"); return;
            }
            if(!IsEmpty() && (settings.coin_credit_points!=game.settings.coin_credit_points ||
                settings.payout_unit_points!=game.settings.payout_unit_points || settings.acceptor_type!=game.settings.acceptor_type ||
                settings.coin_input!=game.settings.coin_input || settings.payout_input!=game.settings.payout_input ||
                settings.payout_output!=game.settings.payout_output)) { Reply(r->req,"BALANCE_NOT_EMPTY"); return; }
            /* V3.2 multiplier tables supersede the legacy manual base-odds slots. */
            for(i=0;i<6;i++) if(settings.base_odds[i]!=game.settings.base_odds[i]) { Reply(r->req,"INVALID_VALUE"); return; }
            candidate.settings=settings; if(++candidate.settings_revision==0) candidate.settings_revision=1;
            candidate.last_settings_request=r->req; candidate.last_settings_base=r->base;
            /* Keep the current bag/token: settings edits cannot redraw a favorable ball. */
            candidate.plan_dirty|=PLAN_DIRTY_SETTINGS; GameAuthority_UpdateOdds(&candidate);
        } else if(r->type==MC_REPORT || r->type==MC_CLEAR) {
            if(r->req==game.last_report_request) { Reply(r->req,"OK"); Account(); return; }
            if(r->base!=game.account.report_count || !authenticated) { Reply(r->req,authenticated?"CONFLICT":"AUTH_REQUIRED"); return; }
            candidate.account.report_coin_in=game.account.coin_in_count;
            candidate.account.report_coin_out=game.account.payout_count;
            candidate.account.report_in_value=game.account.coin_in_value;
            candidate.account.report_out_value=game.account.payout_value;
            candidate.account.report_count++; candidate.last_report_request=r->req;
        }
    }
    if(!Commit()) { Reply(r->req,"STORAGE_ERROR"); Settings(); return; }
    Reply(r->req,"OK"); Settings(); Account();
    if(r->type==MC_EXIT) { SerialPort_SendLine("SERVICE_STATE:open=0"); State(); }
    else if(r->type==MC_ENTER) State();
}
static void SerialLine(const char *line)
{
    MachineRequest control; ProtocolRxMessage m;
    if(MachineProtocol_Parse(line,&control)) { BoardIO_NotifyCommunicationActivity(); Control(&control); return; }
    if(!ProtocolParser_ParseLine(line,&m)) return;
    BoardIO_NotifyCommunicationActivity();
    if(m.type==PROTOCOL_RX_UNITY_READY) {
        ready=1; authenticated=0; Full(); Settings(); Account();
        if(game.state==GAME_STATE_SETTING) State(); else StandbyPoll(BoardTimer_Millis());
        return;
    }
    if(m.type==PROTOCOL_RX_ACK) {
        if(m.seq==full_seq) full_wait=0;
        if(m.seq==critical_seq) critical_kind=0U;
        return;
    }
    if(m.type==PROTOCOL_RX_TEST_A || m.type==PROTOCOL_RX_PING_TEST) { SerialPort_SendLine(line); return; }
    if(m.type==PROTOCOL_RX_DEMO_RACE_OVER) {
        if(standby_state==STANDBY_ANIMATION_RACE && m.seq==standby_seq && m.round==standby_round)
            StandbyShowResult(BoardTimer_Millis());
        return;
    }
    if(m.type==PROTOCOL_RX_DEMO_RESULT_OVER) {
        if(standby_state==STANDBY_ANIMATION_RESULT && m.seq==standby_seq && m.round==standby_round)
            StandbyStartRace(BoardTimer_Millis());
        return;
    }
    if(!storage_ok || pending_save) return;
    candidate=game;
    if((m.type==PROTOCOL_RX_RACE_OVER && GameAuthority_HandleRaceOver(&candidate,m.seq,m.round,BoardTimer_Millis())) ||
       (m.type==PROTOCOL_RX_RESULT_OVER && GameAuthority_HandleResultOver(&candidate,m.seq,m.round))) {
        if(Commit()) State();
    }
}
static void Key(BoardKey key)
{
    uint8_t changed=0,fast=0,was_standby,deferred=0;
    uint32_t now=BoardTimer_Millis();
    if(!storage_ok || pending_save) return;
    was_standby=standby_state!=STANDBY_ANIMATION_OFF;
    if(was_standby) StandbyStop(now);
    if(game.state==GAME_STATE_SETTING) {
        static const char *keys[]={"LEFT","RIGHT","UP","DOWN","BACK","CONFIRM","BACK","BACK"};
        snprintf(tx,sizeof(tx),"MENU_KEY:key=%s",keys[key]); Send(1); return;
    }
    candidate=game;
    switch(key) {
    case BOARD_KEY_LEFT: changed=GameAuthority_SelectPrevious(&candidate); deferred=1; break;
    case BOARD_KEY_RIGHT: changed=GameAuthority_SelectNext(&candidate); deferred=1; break;
    case BOARD_KEY_DOWN: changed=GameAuthority_PlaceBet(&candidate); deferred=2; break;
    case BOARD_KEY_SETTING: changed=GameAuthority_ToggleSetting(&candidate); authenticated=0; break;
    case BOARD_KEY_GAME:
        if(game.state==GAME_STATE_WIN_WAIT_START) changed=GameAuthority_StartWinCollect(&candidate,now);
        else if(game.state==GAME_STATE_WIN_COLLECTING) { changed=GameAuthority_FastFinishWinCollect(&candidate); fast=1; }
        else if(ready) changed=GameAuthority_PrepareRace(&candidate,now);
        break;
    case BOARD_KEY_EXCHANGE: changed=GameAuthority_StartPayout(&candidate); break;
    case BOARD_KEY_SERVICE_RESET: changed=GameAuthority_ServiceReset(&candidate); break;
    default: break;
    }
    if(changed && deferred) {
        /* Selection is display-only.  Bets are persisted once the player pauses,
         * while START still commits the complete round before SHOW_RACE. */
        ApplyGameplayChange(now,(uint8_t)(deferred==2U));
        State();
    }
    else if(changed && Commit()) {
        if(game.state==GAME_STATE_PAYOUT) payout_tick=BoardTimer_Millis();
        if(key==BOARD_KEY_SERVICE_RESET) Full();
        if(fast) Send(ProtocolBuilder_WinTransferDone(tx,sizeof(tx),&game,1));
        else if(key==BOARD_KEY_EXCHANGE) Send(ProtocolBuilder_ShowPayout(tx,sizeof(tx),&game,GameAuthority_NextSequence(&game)));
        else State();
        if(game.state==GAME_STATE_SETTING) { Settings(); Account(); }
    }
    else if(was_standby && ready) State();
}
void AppMain_Init(void)
{
#ifdef BOOT_APP_BASE
    /* The independent watchdog remains enabled after the bootloader jump. */
    fwdgt_counter_reload();
#endif
    BoardTimer_Init(); InputService_Init(); SerialPort_Init(USART2);
    GameAuthority_Init(&game); loaded=storage_ok=RuntimeStorage_Load(&game);
#ifdef BOOT_APP_BASE
    fwdgt_counter_reload();
#endif
    GameAuthority_RestoreTimers(&game,BoardTimer_Millis()); Configure(); StopPayout();
    runtime_tick=hello_tick=retry_tick=checkpoint_tick=payout_tick=BoardTimer_Millis();
    gameplay_save_pending=betting_state_pending=critical_kind=critical_retries=0U;
    gameplay_save_tick=BoardTimer_Millis();
    standby_state=STANDBY_ANIMATION_OFF; standby_seq=standby_round=standby_deadline=0U;
    standby_rng=BoardTimer_Millis()^0xA341316CUL; standby_restart_tick=BoardTimer_Millis(); standby_winner=0U;
#ifdef BOOT_APP_BASE
    boot_trial_confirmed=0U; boot_trial_started=BoardTimer_Millis();
#endif
    SerialPort_SendLine("hello");
}
void AppMain_Poll(void)
{
    char line[SERIAL_PORT_MAX_LINE_LENGTH+1]; BoardKey key; uint32_t now,n,raw_mask,bet_before; uint8_t changed,was_collecting,state_before,selected_before;
#ifdef BOOT_APP_BASE
    fwdgt_counter_reload();
#endif
    InputService_Poll(); SerialPort_Poll(); now=BoardTimer_Millis();
    raw_mask=BoardIO_TakeInputPressMask();
    if(!ready && raw_mask) {
        snprintf(tx,sizeof(tx),"IN_DIAG:mask=%07lX",(unsigned long)raw_mask);
        Send(1);
    }
#ifdef BOOT_APP_BASE
    BootApp_CheckDeadline(now);
    if(!boot_trial_confirmed && (uint32_t)(now-boot_trial_started)>=3000U) {
        BootApp_Confirm(); boot_trial_confirmed=1U;
    }
#endif
    n=BoardIO_TakeCoinCount();
    if(n && standby_state!=STANDBY_ANIMATION_OFF) StandbyStop(now);
    if(UINT32_MAX-unbooked_coins>=n) unbooked_coins+=n;
    if(!loaded && now-retry_tick>=1000) {
        retry_tick=now; loaded=storage_ok=RuntimeStorage_Load(&game);
        if(loaded) { GameAuthority_RestoreTimers(&game,BoardTimer_Millis()); Configure(); if(ready) { Full(); Settings(); } }
    }
    if(loaded && unbooked_coins) {
        candidate=pending_save?pending:game;
        while(unbooked_coins && GameAuthority_AddCoin(&candidate)) unbooked_coins--;
        if(Commit()) { if(game.plan_dirty) WinnerControl_Evaluate(&game); if(ready) {
            State(); if(game.state!=GAME_STATE_BETTING && game.state!=GAME_STATE_IDLE) Balance();
            if(game.state==GAME_STATE_SETTING) Account();
        } }
    }
    if(pending_save && now-retry_tick>=1000) {
        candidate=pending;
        if(Commit() && ready) { Full(); Settings(); Account(); State(); }
    }
    n=BoardIO_TakePayoutCount();
    if(n && loaded) {
        StopPayout(); candidate=pending_save?pending:game;
        if(GameAuthority_ConfirmPayout(&candidate,n)) {
            if(Commit()) {
                payout_tick=now;
                if(!game.pending_payout_coins) Send(ProtocolBuilder_ShowPayoutDone(tx,sizeof(tx),&game,GameAuthority_NextSequence(&game),game.payout_done));
                else State();
                Account();
            }
        } else { candidate.fault_code=3; (void)Commit(); Full(); }
    }
    while(SerialPort_GetLine(line,sizeof(line))) SerialLine(line);
    /* Confirmed coin events are consumed before START, including during a prior EEPROM write. */
    while(InputService_GetKeyPress(&key)) {
        state_before=(uint8_t)game.state; selected_before=game.selected_id;
        bet_before=GameAuthority_TotalBet(&game);
        n=BoardIO_TakeCoinCount();
        if(n) {
            if(standby_state!=STANDBY_ANIMATION_OFF) StandbyStop(now);
            candidate=pending_save?pending:game;
            if(loaded) { while(n && GameAuthority_AddCoin(&candidate)) n--; (void)Commit(); }
            unbooked_coins+=n;
        }
        Key(key);
        if(!ready) {
            snprintf(tx,sizeof(tx),"KEY_DIAG:key=%u,state=%u>%u,selected=%u>%u,bet=%lu>%lu,storage=%u,pending=%u",
                (unsigned)key,(unsigned)state_before,(unsigned)game.state,
                (unsigned)selected_before,(unsigned)game.selected_id,
                (unsigned long)bet_before,(unsigned long)GameAuthority_TotalBet(&game),
                (unsigned)storage_ok,(unsigned)pending_save);
            Send(1);
        }
    }
    now=BoardTimer_Millis();
    if(storage_ok && !pending_save) {
        if(game.plan_dirty) WinnerControl_Evaluate(&game);
        if(gameplay_save_pending && (int32_t)(now-gameplay_save_tick)>=0) {
            candidate=game; (void)Commit();
        }
    }
    if(storage_ok && !pending_save) {
        candidate=game; was_collecting=game.state==GAME_STATE_WIN_COLLECTING;
        changed=GameAuthority_HandleRaceTimeout(&candidate,now);
        if(!changed) changed=GameAuthority_HandleResultTimeout(&candidate,now);
        if(!changed) changed=GameAuthority_PollWinCollect(&candidate,now);
        if(changed && Commit()) {
            if(was_collecting && !game.win_remaining_points) Send(ProtocolBuilder_WinTransferDone(tx,sizeof(tx),&game,0));
            else State();
        }
        if(game.state==GAME_STATE_PAYOUT && game.pending_payout_coins && !game.fault_code) {
            if(!payout_running) { BoardIO_WriteOutput((uint8_t)game.settings.payout_output,1); payout_running=1; }
            if(now-payout_tick>5000) { StopPayout(); candidate=game; candidate.fault_code=2; (void)Commit(); Full(); }
        } else StopPayout();
        if(now-runtime_tick>=1000) { game.account.runtime_seconds+=(now-runtime_tick)/1000; runtime_tick+=((now-runtime_tick)/1000)*1000; }
        if(now-checkpoint_tick>=60000) { checkpoint_tick=now; candidate=game; (void)Commit(); }
    } else StopPayout();
    StandbyPoll(now);
    if(!ready && now-hello_tick>=1000) { SerialPort_SendLine("hello"); hello_tick=now; }
    if(ready && full_wait && now-snapshot_tick>=2000 && SerialPort_TxIdle()) {
        snapshot_tick=now;
        Send(ProtocolBuilder_FullState(tx,sizeof(tx),&game,full_seq));
    }
    RetryCritical(now);
    FlushBettingState();
}

StandbyAnimationState AppMain_GetStandbyAnimationState(void)
{ return standby_state; }

uint8_t AppMain_GetStandbyWinner(void)
{ return standby_winner; }
