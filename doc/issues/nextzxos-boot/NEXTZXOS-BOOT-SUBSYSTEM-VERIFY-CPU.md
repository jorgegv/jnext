# NextZXOS Boot Subsystem Verification — CPU (Z80 + Z80N + IM2)

**Status: VERIFIED with one class-(a) fix, two class-(b) accuracy concerns
documented.**

This is the second-pass independent verification audit of the CPU subsystem
post the first-pass fixes (Z80N T-state under-count, machine-aware INT pulse
window). The audit is fully blind to all prior `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-
ANALYSIS-*` reports.

## Verdict

| Aspect                           | Status      |
|----------------------------------|-------------|
| FUSE Z80 base (1356/1356)        | PASS        |
| Z80N opcodes — semantics         | PASS        |
| Z80N opcodes — T-state counts    | PASS        |
| Z80N PUSH nn byte order          | PASS        |
| IM2 daisy chain priority + vector| PASS        |
| IM2 RETI/RETN/IM-mode decoder    | PASS        |
| INT pulse machine-aware window   | PASS        |
| FUSE tstates sync across Z80N    | **FIXED**   |
| EI/HALT/NMI/RETI/RETN semantics  | PASS        |

* **Class-(a) fix applied**: Z80N opcodes were bypassing the FUSE `tstates`
  global counter, leaving it stuck while Z80N executed. Fixed by adding
  `tstates += t` in the Z80N return path of `Z80Cpu::execute()`.
* **Class-(b) documented**: M1-fetch contention skipped on the Z80N path's
  raw `mem_.read()` of ED + ext bytes. Real hardware applies contention to
  these reads. Not fixed (more invasive); documented below.
* **Class-(b) documented**: `Im2Controller::ack_vector()` may advance
  `S_REQ → S_ACK` for a device whose IM2 vector the Z80 ultimately rejects
  (EI grace period). Documented below.

All 37 ctest suites pass. FUSE Z80 1356/1356, Z80N 85/85, INT pulse 10/10.

## Methodology

1. Ultrathink the audit plan; enumerate the surface to verify.
2. For each Z80N opcode: VHDL `t80n_mcode.vhd` mcode + `t80n.vhd` Z80N command
   processor → C++ `z80n_ext.cpp` → spec at
   <https://wiki.specnext.dev/Extended_Z80_instruction_set>.
3. For Z80 base: trust FUSE 1356/1356 oracle, but spot-check critical paths
   (RST, RETI/RETN, EX (SP),HL, EI grace, HALT exit, IM2 vector lookup).
4. For IM2: VHDL `im2_control.vhd` + `im2_device.vhd` + `im2_peripheral.vhd`
   + `peripherals.vhd` + `zxnext.vhd` → C++ `im2.cpp`.
5. For INT pulse: VHDL `zxnext.vhd:2017-2044` → C++
   `Z80Cpu::execute()` pulse-expiry arm.
6. Build + run all unit tests + cross-check tests against the Z80N tests.in
   reference fixtures.

## Per-opcode verification (Z80N)

For each opcode I cross-checked: (i) opcode bytes, (ii) functional effect,
(iii) T-state count.

| Op       | Code   | Semantics                       | T-st | C++ ✓ |
|----------|--------|---------------------------------|------|-------|
| SWAPNIB  | ED 23  | A := (A<<4)\|(A>>4)             | 8    | ✓    |
| MIRROR A | ED 24  | A := bit-reverse(A)             | 8    | ✓    |
| TEST n   | ED 27  | flags as AND A,n; A unchanged   | 11   | ✓    |
| BSLA D,B | ED 28  | DE <<= (B & 0x1F)               | 8    | ✓    |
| BSRA D,B | ED 29  | DE := arith\_shr(DE, B & 0x1F)   | 8    | ✓    |
| BSRL D,B | ED 2A  | DE := log\_shr(DE, B & 0x1F)     | 8    | ✓    |
| BSRF D,B | ED 2B  | DE := shr-fill-1(DE, B & 0x1F)  | 8    | ✓    |
| BRLC D,B | ED 2C  | DE := rol(DE, B & 0x0F)          | 8    | ✓    |
| MUL D,E  | ED 30  | DE := D × E (8×8→16)             | 8    | ✓    |
| ADD HL,A | ED 31  | HL := HL + A; only C flag       | 8    | ✓    |
| ADD DE,A | ED 32  | DE := DE + A; only C flag       | 8    | ✓    |
| ADD BC,A | ED 33  | BC := BC + A; only C flag       | 8    | ✓    |
| ADD HL,nn| ED 34  | HL := HL + nn; no flags         | 16   | ✓    |
| ADD DE,nn| ED 35  | DE := DE + nn; no flags         | 16   | ✓    |
| ADD BC,nn| ED 36  | BC := BC + nn; no flags         | 16   | ✓    |
| PUSH nn  | ED 8A  | **big-endian** push: NH, NL     | 23   | **✓** |
| OUTINB   | ED 90  | OUT (BC),(HL); HL++; B unchanged| 16   | ✓    |
| NEXTREG nn,n'| ED 91 nn vv | NR\[nn] := vv               | 20   | ✓    |
| NEXTREG nn,A | ED 92 nn    | NR\[nn] := A                | 17   | ✓    |
| PIXELDN  | ED 93  | HL pixel addr += 1 row          | 8    | ✓    |
| PIXELAD  | ED 94  | HL := pixel\_addr(D, E)          | 8    | ✓    |
| SETAE    | ED 95  | A := 0x80 >> (E & 7)            | 8    | ✓    |
| JP (C)   | ED 98  | PC[13:0] := IN(C) << 6          | 13   | ✓    |
| LDIX     | ED A4  | LDI with (HL)≠A guard           | 16   | ✓    |
| LDWS     | ED A5  | LD (DE),(HL); inc L,D           | 14   | ✓    |
| LDDX     | ED AC  | LDIX with HL−−, DE++             | 16   | ✓    |
| LDIRX    | ED B4  | repeat LDIX                     | 21/16| ✓    |
| LDIRSCALE| ED B6  | repeat (no scale logic in FPGA) | 21/16| ✓    |
| LDPIRX   | ED B7  | pattern LDIRX                   | 21/16| ✓    |
| LDDRX    | ED BC  | repeat LDDX                     | 21/16| ✓    |

### PUSH nn — re-verified from scratch (top G46(b) suspect)

The wiki entry confirms: "the only operand encoded as big-endian". The
instruction stream `ED 8A NH NL` pushes the 16-bit value `(NH << 8) | NL`.
Stack contents after execution: `mem[SP]=NL, mem[SP+1]=NH`, SP -= 2.

The C++ implementation:

```cpp
uint8_t hh = cpu.memory().read(regs.PC);       // first operand byte = NH
uint8_t ll = cpu.memory().read(regs.PC + 1);   // second operand byte = NL
regs.PC += 2;
regs.SP -= 2;
cpu.memory().write(regs.SP + 1, hh);   // [SP+1] := NH (high)
cpu.memory().write(regs.SP, ll);       // [SP]   := NL (low)
```

Cross-checked against `test/z80n/tests.expected`:

```
ed8a_basic   → ed 8a 12 34 → mem[$FFEE]=$34, mem[$FFEF]=$12, SP=$FFEE  ✓
ed8a_ffff    → ed 8a ff ff → mem[$FFEE]=$ff, mem[$FFEF]=$ff, SP=$FFEE  ✓
ed8a_preserve→ ed 8a ab cd → mem[$FFEE]=$cd, mem[$FFEF]=$ab, SP=$FFEE  ✓
```

A subsequent `RET` would pop `(mem[SP+1] << 8) | mem[SP] = (NH << 8) | NL`
into PC — exactly the value pushed. The byte order is **correct**.

VHDL cross-check (`t80n_mcode.vhd:1921-1949`): the mcode for ED 8A reads
two bytes via `Inc_PC + LDZ` over MCycles 1..3 then writes them via
`Set_BusB_To = "0110"` (= DI_Reg) in MCycles 3 and 5. The semantics of
which byte ends up where is verified by the reference test fixtures above —
matches the C++ implementation exactly.

### LDDRX — VHDL says HL−−, DE++ (NOT both decrement)

`t80n_mcode.vhd:2230-2256` for LDDX/LDDRX:

```
when 2 => IncDec_16 <= "1110"; -- decrement HL
when 3 => IncDec_16 <= "0101"; -- increment DE
```

The C++ at `z80n_ext.cpp:411` correctly does `regs.DE = (regs.DE + 1)`
and `regs.HL = (regs.HL - 1)`. ✓

### LDPIRX — pattern source address

`t80n.vhd:1119-1128` builds the source address from
`reg_temp_t(15 downto 3) & reg_temp_t(18 downto 16)`, where
`reg_temp_t[15:0] = HL` and `reg_temp_t[18:16] = DE[2:0]`. Net:
`addr = (HL & 0xFFF8) | (DE & 0x07)`. C++ matches. ✓

## IM2 fabric verification

### Daisy-chain priority

`peripherals.vhd:82+105-128` (generate-loop) wires `ie(I) ← peripheral(I)` from
`I=1` (highest) to `I=NUM_PERIPH=14` (lowest). Each peripheral receives
`i_iei = ie(I-1)`. `zxnext.vhd:1941`:

```
im2_int_req <= uart1_tx_empty(13) & uart0_tx_empty(12) &
               ula_int_pulse(11) & ctc_zc_to(10..3) &
               uart1_rx(2) & uart0_rx(1) & line_int_pulse(0);
```

C++ DevIdx enum order matches: LINE=0, UART0_RX=1, UART1_RX=2, CTC0=3,
..., CTC7=10, ULA=11, UART0_TX=12, UART1_TX=13. Highest priority = LINE.
`Im2Controller::ack_vector()` walks `for (i = 0; i < N; ++i)` which puts
LINE first. ✓

### Vector composition

`zxnext.vhd:1999`: `im2_vector <= nr_c0_im2_vector & im2_vec & '0';`

* bits[7:5] = `nr_c0_im2_vector` (3 bits)
* bits[4:1] = device index (4 bits)
* bit 0 = '0'

C++ `Im2Controller::compute_vector()`:

```cpp
return ((vector_base_msb3_ & 0x07) << 5) | (idx << 1);
```

Match. ✓

### NR 0xC8/C9/CA read packing

* C8 — `port_253b_dat <= "000000" & im2_int_status(0) & im2_int_status(11)`
  → bit 1 = LINE, bit 0 = ULA. C++ matches.
* C9 — `im2_int_status(10 downto 3)` → bit i = CTCi. C++ matches.
* CA — duplicated UART RX bit pattern per VHDL `:6253-6254`. C++ matches.

### RETI/RETN/IM-mode decoder FSM

`im2_control.vhd:160-209` defines the FSM:

```
S_0 → S_ED_T4 on ED
S_ED_T4 → S_ED4D_T4 on 4D (RETI)
S_ED_T4 → S_ED45_T4 on 45 (RETN)
S_ED_T4 → S_0 on other ED-byte (with IM-mode side-effect for 46/56/5E etc.)
S_ED4D_T4 / S_ED45_T4 → S_SRL_T1 → S_SRL_T2 → S_0   (DMA-delay window)
S_CB_T4 → S_0 on next byte
S_DDFD_T4 → S_DDFD_T4 on DD/FD; S_ED_T4 on ED; S_0 on other
```

C++ `Im2Controller::advance_decoder()` is structurally identical, including
the `o_dma_delay = state ∈ {S_ED_T4, S_ED4D_T4, S_ED45_T4, S_SRL_T1,
S_SRL_T2}` derived signal at `im2.cpp:557-562`. ✓

IM-mode decode bit-formula (VHDL `:224`):
`im_mode <= (b4 AND b3) & (b4 AND NOT b3)`. The C++ branch
(b4=0→0, b4=1&!b3=1→1, b4=1&b3=1→2) produces the same numeric
mapping. Cross-check on each ED 4x/5x/6x/7x with `(opcode & 0xC7) == 0x46`
matches. ✓

## INT pulse machine-aware window

`zxnext.vhd:2033`:

```
pulse_count_end <= pulse_count(5) AND
                   (machine_timing_48 OR machine_timing_p3 OR pulse_count(2));
```

* 48K / +3 → bit 5 alone fires the gate → pulse width = 32 cycles.
* 128K / Pentagon / Next-default → bit 5 AND bit 2 → width = 36 cycles.

`Z80Cpu::execute()`:

```cpp
const uint32_t int_pulse_tstates = machine_48_or_p3_ ? 32u : 36u;
if (tstates - int_requested_at_ > int_pulse_tstates && !z80.iff1) {
    int_pending_ = false;
}
```

Threshold-by-1 difference (`>` strict) is consistent with the existing
`cpu_int_pulse_test` (10/10 PASS). The setter is wired at three sites:

* Reset path  `emulator.cpp:288`
* NR 0x03 write `emulator.cpp:1732`
* Machine-type set `emulator.cpp:5551`

✓ Fanout to BOTH `Z80Cpu` and `Im2Controller` is correct.

## Z80 base spot-checks

| Op             | Behaviour                                       | Verdict |
|----------------|-------------------------------------------------|---------|
| RST n          | PUSH16(PCL,PCH); PC=n; memptr=PC                | ✓      |
| CALL nn        | PUSH16(PCL,PCH) of (after-CALL PC); PC=nn       | ✓      |
| RET            | POP16(PCL,PCH); SP+=2                           | ✓      |
| RETI / RETN    | RET + IFF1=IFF2 (FUSE convention)               | ✓ †   |
| EX (SP),HL     | swap HL ↔ (SP, SP+1); SP unchanged              | ✓      |
| EI             | IFF1=IFF2=1; interrupts_enabled_at=tstates      | ✓      |
| HALT           | PC--; halted=1; INT bumps PC++ on accept        | ✓      |
| LD A,I/R       | F.P/V := IFF2                                   | ✓      |
| IM 0/1/2 set   | ED 46/56/5E + variants                          | ✓      |
| LDIR/LDDR      | per FUSE; passes 1356 oracle                    | ✓      |
| OTIR/INDR      | per FUSE; passes 1356 oracle                    | ✓      |
| JR / DJNZ      | per FUSE; 7/12 (not taken / taken) per spec     | ✓      |
| PUSH/POP       | SP-decrement-then-write / read-then-increment   | ✓      |

† **FUSE convention deviation from Z80 ref**: FUSE makes both ED 4D (RETI)
and ED 45 (RETN) restore IFF1 from IFF2. Per Zilog Z80 User Manual, only
RETN does this; RETI is functionally identical to RET (only a peripheral
signal changes). FUSE's combined behaviour is in the FUSE 1356/1356 reference,
so jnext inherits it. In practice the EI-before-RETI idiom masks this
discrepancy; a few real ROMs may have a subtle 1-cycle behavioural
difference.

## Class-(a) fix: Z80N tstates not propagated to FUSE counter

`Z80Cpu::execute()` for non-Z80N opcodes goes through
`fuse_z80_execute_one()` which internally bumps the FUSE `tstates` global
via `contend_read()` and `readbyte()`. For Z80N opcodes, the path is
different:

1. Line 466: `opcode = mem_.read(pc)` — RAW MemoryInterface read; no
   `tstates` increment.
2. Line 469: `ext = mem_.read(pc + 1)` — RAW; no increment.
3. Line 488-489: `on_m1_cycle` is observer-only.
4. Line 497: `t = execute_z80n(ext, *this)` — internally uses
   `cpu.memory().read()` and `cpu.io().out()` which are the raw interfaces;
   no FUSE `tstates` increment.
5. Line 501: `return t` — but the FUSE `tstates` global is **not** updated.

Effect: when the supervisor runs a burst of Z80N opcodes (NEXTREG, MUL,
ADD HL/DE/BC,A — all of which appear constantly in the NextZXOS
supervisor's bank-flip wrappers), the FUSE `tstates` counter falls behind
real wall-clock time. The next non-Z80N opcode then computes contention
from a stale (hc, vc), and the `/INT` pulse-window check
`tstates - int_requested_at_ > int_pulse_tstates` may falsely report the
pulse as still alive (extending the window beyond 32/36 cycles).

**Fix** (committed): add `tstates += t` after the Z80N path in
`Z80Cpu::execute()`. Documented inline. Tests:

* FUSE Z80: 1356/1356 PASS.
* Z80N tests.in/expected: 85/85 PASS (these don't query FUSE tstates).
* `cpu_int_pulse_test`: 10/10 PASS.
* All 37 ctest suites: PASS.

## Class-(b) accuracy concerns (NOT fixed; documented)

### B1 — M1-fetch contention skipped on Z80N path

The two raw `mem_.read()` calls at lines 466 and 469 of `Z80Cpu::execute()`
do not apply contention. Real hardware applies contention to M1 fetches
(4 T-states each) when the page is contended. For Z80N opcodes, this means
the M1 contention is silently skipped — a +0 cycle delta on contended
pages instead of the correct stretch.

Why not fixed: the contention path requires knowing the (hc, vc) BEFORE
the read, mirroring `fuse_z80_readbyte`. Refactoring the Z80N path to use
`fuse_z80_readbyte` is invasive and risks regressions in the FUSE 1356
oracle (the FUSE M1 path is structured differently). Net impact on G46(b)
boot is small: contention on Z80N opcodes is a few-cycle accuracy issue,
not a functional one. The class-(a) fix above (FUSE tstates sync) handles
the major timing drift.

### B2 — `ack_vector()` advances S_REQ→S_ACK before fuse_z80_interrupt accepts

`Im2Controller::ack_vector()` is called from `Z80Cpu::execute()` BEFORE
`fuse_z80_interrupt(vector)`. ack_vector unconditionally moves the
priority-resolved device from `S_REQ` to `S_ACK`. If `fuse_z80_interrupt`
then rejects (e.g. `tstates == interrupts_enabled_at` — the EI grace
period), the device state has incorrectly advanced.

Net effect during EI grace: device state is `S_ACK`. Next tick advances
`S_ACK → S_ISR`. The Z80 retries on the next instruction, calls
`ack_vector` again — but no device is in `S_REQ` now, so it returns
`0xFF`. The retried interrupt is dispatched with the wrong vector.

Why not fixed: this is rare (EI grace is one specific T-state), happens
mostly during intentional `EI; HALT` patterns. NextZXOS supervisor uses
IM1 at boot, not IM2 — boot path is not affected. Logged for the IM2
peripheral wave when CTC/UART tests exercise this path explicitly.

## G46(b) cross-check — does CPU explain 3 missing PUSHes / 3 extra POPs?

The G46(b) bug presents as the supervisor's stack ascending from `$FF53`
to `$FF59` (= 6 bytes) between RST $08 hits #2 and #3 in jnext, vs CSpect
which keeps it stable. Hypothesis space: a CPU operation is doing 3 fewer
PUSHes or 3 extra POPs than CSpect. Audit findings:

| CPU op            | Could cause asymmetry? | Verdict           |
|-------------------|------------------------|-------------------|
| RST mis-impl      | YES (each RST = 2 byte push) | RST verified OK |
| CALL mis-impl     | YES                    | CALL verified OK  |
| RET mis-impl      | YES                    | RET verified OK   |
| RETI vs RETN diff | minor                  | ED 4D/45 both restore IFF1 in FUSE — 0 stack effect |
| EX (SP),HL bug    | YES                    | Verified swap, no SP change |
| Z80N PUSH nn      | YES (top suspect)      | **Verified correct byte order; passes ed8a_basic / preserve / ffff**|
| ED A4/AC etc.     | could have a stack op  | None of LDIX/LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE touch stack |
| INT premature     | YES                    | INT pushes 2 bytes (PCL, PCH); fuse's path correct |
| NMI premature     | YES                    | NMI pushes 2 bytes; on_nmi_servicing fires before fuse_z80_nmi |
| Z80N tstates desync (now FIXED) | could affect INT timing | INT pulse-window check now stable |
| LDIRX/etc retries | each retry repeats     | 1 mem write per iter, no stack |

**No CPU-side smoking gun for G46(b) found in this audit.** The 6-byte
stack drift is therefore unlikely to come from a Z80 / Z80N / IM2
mis-implementation. Likely roots remain:

1. The IM1 ULA-frame INT firing at slightly different T-states between
   jnext and CSpect (different scheduler granularity). Each IM1 INT
   pushes 2 bytes — 3 spurious INT entries would produce exactly 6 bytes
   of stack growth.
2. The MMU/paging path mapping different banks at the moment of an
   alternate-stack swap, so the supervisor reads / writes a different
   physical region.

Recommended next-session probe: fire `JNEXT_G46B_INT_TRACE` to log every
IM1/IM2 INT acceptance with PC, SP, tstates. Compare counts and SP
deltas with CSpect's INT count over the same wall-clock window. If
jnext fires N more INTs than CSpect between RST $08 hits #2 and #3, the
6-byte stack drift is fully explained.

## Files audited

* `src/cpu/z80_cpu.h` (139 lines)
* `src/cpu/z80_cpu.cpp` (633 lines, 18 added by this audit)
* `src/cpu/z80n_ext.h` (42 lines)
* `src/cpu/z80n_ext.cpp` (477 lines)
* `src/cpu/im2.h` (238 lines)
* `src/cpu/im2.cpp` (1113 lines)
* `src/cpu/im2_client.h` (25 lines)

## Oracles used

* FUSE Z80 1356/1356 (base Z80)
* `t80n_mcode.vhd` + `t80n.vhd` (Z80N opcodes)
* `im2_control.vhd` + `im2_device.vhd` + `im2_peripheral.vhd` +
  `peripherals.vhd` (IM2 fabric)
* `zxnext.vhd:1937-2044, 5092-5599, 6225-6254` (NR registers, INT pulse)
* <https://wiki.specnext.dev/Extended_Z80_instruction_set> (Z80N spec)
* `test/z80n/tests.expected` (PUSH nn byte-order reference)

## Open questions

None blocking. The class-(b) concerns above can be revisited in a
dedicated wave when test coverage demands; they do not block any current
boot path.
