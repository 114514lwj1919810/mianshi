"""Execute the built Keil ARM image, including real account/lottery/algorithm code.

Only EEPROM and physical peripherals are simulated. Never accesses a real board.
Requires unicorn and pyelftools; pass an AXF path to test another build.
"""
import os
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(os.environ['TEMP']) / 'joker-arm-check-deps'))
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE
from unicorn.arm_const import *

ROOT = Path(__file__).resolve().parents[1]

class Board:
    def __init__(self, path):
        self.u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
        self.u.mem_map(0x08000000, 0x100000)
        self.u.mem_map(0x20000000, 0x40000)
        self.symbol = {}
        with open(path, 'rb') as f:
            elf = ELFFile(f)
            for segment in elf.iter_segments():
                if segment['p_type'] == 'PT_LOAD':
                    self.u.mem_write(segment['p_paddr'], segment.data())
            for s in elf.get_section_by_name('.symtab').iter_symbols():
                if s.name and s['st_value']:
                    self.symbol[s.name] = s['st_value']
        self.ee = bytearray(b'\xff' * 32768)
        self.now = 1000
        self.hooks = {}
        self.u.hook_add(UC_HOOK_CODE, self.dispatch)
        self.stub('main', lambda: self.u.emu_stop())
        self.call('__main')  # real scatter loader, including initialized .data
        self.stub('Gtim_NowMs', lambda: self.now)
        self.stub('Gtim_IsElapsed', lambda: int((self.now-self.arg(0)) & 0xffffffff >= self.arg(1)))
        self.stub('Eeprom_IsReady', lambda: 1)
        self.stub('Eeprom_Check', lambda: 1)
        self.stub('Eeprom_GetFault', lambda: 0)
        self.stub('Eeprom_ProtectData', self.protect)
        self.stub('Eeprom_Read', self.read_ee)
        self.stub('Eeprom_Write', self.write_ee)
        for name in ('Audio_SetVolumeLevelOn', 'Audio_PlayIndexOn', 'Audio_LoopIndexOn',
                     'Audio_CancelLoopResumeOn', 'Audio_PlayLoudIndexOn',
                     'Audio_IsLoudSequenceBusy', 'Segment_LotteryLampShow',
                     'Segment_LotteryLampClearAll', 'Segment_EyeLedClearChannelMask',
                     'Segment_SetMiddleNumber', 'Segment_SetRightNumber',
                     'Payout_IsBusy', 'Ticket_IsBusy', 'WiperMotor_Run',
                     'MachineAddress_GetModeValue', 'MachineAddress_GetNodeId',
                     'MachineAddress_IsNetworkEnabled'):
            self.stub(name, lambda: 0)
        self.stub('Jackpot_GetCurrent', lambda: 20)

    def stub(self, name, fn):
        if name in self.symbol:
            self.hooks[self.symbol[name] & ~1] = fn

    def arg(self, i):
        return self.u.reg_read(UC_ARM_REG_R0 + i)

    def dispatch(self, u, address, size, data):
        fn = self.hooks.get(address)
        if fn is not None:
            result = fn()
            u.reg_write(UC_ARM_REG_R0, result or 0)
            u.reg_write(UC_ARM_REG_PC, u.reg_read(UC_ARM_REG_LR))

    def call(self, name, *args):
        self.u.reg_write(UC_ARM_REG_SP, 0x20030000)
        self.u.reg_write(UC_ARM_REG_LR, 0x080ffff1)
        for i, arg in enumerate(args):
            self.u.reg_write(UC_ARM_REG_R0+i, arg)
        try:
            self.u.emu_start(self.symbol[name] | 1, 0x080ffff0, count=10000000)
        except Exception:
            print('ARM exception', name, 'PC', hex(self.u.reg_read(UC_ARM_REG_PC)),
                  'SP', hex(self.u.reg_read(UC_ARM_REG_SP)),
                  'R0..3', [hex(self.arg(i)) for i in range(4)])
            raise
        pc = self.u.reg_read(UC_ARM_REG_PC)
        assert pc == 0x080ffff0 or name == '__main', (name, hex(pc))
        return self.u.reg_read(UC_ARM_REG_R0)

    def protect(self):
        raise AssertionError('EEPROM protect: ' + hex(self.arg(0)))

    def read_ee(self):
        a, p, n = [self.arg(i) for i in range(3)]
        assert a+n <= len(self.ee)
        self.u.mem_write(p, bytes(self.ee[a:a+n]))
        return 1

    def write_ee(self):
        a, p, n = [self.arg(i) for i in range(3)]
        assert a+n <= len(self.ee)
        self.ee[a:a+n] = self.u.mem_read(p, n)
        return 1

    def diagnostics(self):
        p = 0x20038000
        self.call('GiftAlgorithm_GetDiagnostics', 0, p)
        data = self.u.mem_read(p, 88)
        # ARM struct: 11 uint32 counts, guarantee/limited, pad4, then int64 fields.
        return struct.unpack_from('<qqQ', data, 56)

    def init(self):
        self.call('Account_Init')
        self.call('Setting_Init')
        self.call('Setting_SetValue', 4, 95)
        self.call('Setting_SetValue', 30, 15)
        self.call('Setting_SetValue', 29, 0)
        self.call('Account_AddCoinIn', 100)
        self.call('Account_AddTotalPayout', 130)
        self.call('Lottery_Init')
        self.call('GiftAlgorithm_ResetForAccountClear')

    def draw(self):
        self.call('Lottery_Request', 1, 0)
        self.now += 701
        self.call('Lottery_Task')
        award = self.call('Lottery_TakeCoinAward')
        if award:
            assert self.call('Game_AddLotteryPrizeAndPayout', award) == 1
            if 'GiftAlgorithm_CommitCoinAward' in self.symbol:
                assert self.call('Game_AddLotteryPrizeAndPayout', award) == 0
            self.call('Game_SetPrize', 0)  # payout completion, real accounting above
            self.call('Lottery_NotifyAwardComplete')
        else:
            self.now += 2001
        self.call('Lottery_Task')
        return award


if __name__ == '__main__':
    path = Path(sys.argv[1]) if len(sys.argv)>1 else ROOT/'Output/IAP_A/IAP_A.axf'
    b = Board(path)
    b.init()
    total = 0
    for i in range(5):
        b.call('Account_AddCoinIn', 1)
        b.call('GiftAlgorithm_OnStake')
        before = b.diagnostics()
        prize = b.draw()
        total += prize
        print('stake', i+1, 'before', before, 'prize', prize, 'after', b.diagnostics())
    assert total == 3, total
    for i in range(10):
        assert b.draw() == 0
    print('PASS Keil ARM: 5 sequential stakes, E initially zero, total award 3; no unstaked awards')
    if 'GiftAlgorithm_CommitCoinAward' in b.symbol:
        # Inject the same result-release event twice before Game consumes it.
        b.call('Account_AddCoinIn', 5)
        b.call('GiftAlgorithm_OnStake')
        b.call('Lottery_Request', 1, 0)
        b.now += 701
        b.call('Lottery_Task')
        b.call('Lottery_ReleaseResultAward')
        assert b.call('Lottery_TakeCoinAward') == 5
        # A full middle screen delays delivery without losing its authorization.
        b.call('Game_SetPrize', 1)
        assert b.call('Game_AddLotteryPrizeAndPayout', 5) == 0
        b.call('Lottery_ReturnCoinAward', 5)
        assert b.call('Lottery_TakeCoinAward') == 5
        b.call('Game_SetPrize', 0)
        assert b.call('Game_AddLotteryPrizeAndPayout', 5) == 1
        # A stale return must not make the same approved result payable again.
        b.call('Lottery_ReturnCoinAward', 5)
        assert b.call('Lottery_TakeCoinAward') == 0
        assert b.call('Game_AddLotteryPrizeAndPayout', 5) == 0
        print('PASS Keil ARM: duplicate release stays5, queue retry delivers once, stale return rejected')
        b = Board(path)
        b.init()
        b.call('Account_AddTotalPayout', 1000)  # keep this run in deficit
        paid = 0
        for i in range(200):
            b.call('Account_AddCoinIn', 1)
            b.call('GiftAlgorithm_OnStake')
            b.call('GiftAlgorithm_OnStake')  # repeated task must not fund twice
            paid += b.draw()
            assert paid*100 <= (i+1)*80
            if i % 31 == 30:
                b.call('GiftAlgorithm_Task')
                b.call('Account_SaveNow')
                b.call('Account_Init')
                b.call('Lottery_Init')
        assert paid == 159 and b.diagnostics()[1] == 1, (paid, b.diagnostics())
        print('PASS Keil ARM: 200 sequential stakes fund160/pay159/remain1, repeated tasks and reloads do not overspend')
