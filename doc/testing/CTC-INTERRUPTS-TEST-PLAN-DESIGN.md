# CTC and Interrupt Controller Compliance Test Plan

VHDL-derived compliance test plan for the CTC (Counter/Timer Circuit) and
IM2 Interrupt Controller subsystems of the JNEXT ZX Spectrum Next emulator.

## Purpose

The CTC and interrupt controller are tightly coupled subsystems unique to the
ZX Spectrum Next. The CTC provides 4 independent timer/counter channels, each
capable of generating ZC/TO (Zero-Count/Time-Out) pulses that feed into the
IM2 interrupt priority chain. The interrupt controller manages 14 interrupt
sources with a daisy-chain priority scheme, supporting both legacy pulse mode
and hardware IM2 mode. This test suite validates both subsystems against the
authoritative VHDL sources.

## Current status

Task 3 CTC+Interrupts SKIP-reduction plan (`doc/design/TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md`)
ran Phase 0 → Phase 5 on 2026-04-21, merged to main at Phase 3 (commit `a397422`)
with dashboard refresh at `0336c20`.

- **150 plan rows total** (plan summary line says "~151" — (D) plan nit; Section 13 header says "18" but lists 17 NR-C* rows).
- **`test/ctc/ctc_test.cpp`** runtime: **`Total:  133  Passed:  128  Failed:    0  Skipped:    5`**.
  - Runtime total is 133 (not 150) because 17 plan rows migrated to source-level comments during Phase 0 triage rather than staying as `check()`/`skip()` calls: 5 rows were B/D/E-class unobservables merged with their neighbours; 10 rows (Section 12 ULA-INT and Section 13 NR-C* read-composition rows requiring a full `Emulator` fixture) re-home to `test/ctc_interrupts/ctc_interrupts_test.cpp`; 2 rows (JOY-01/02) re-home to the emulator/input integration layer.
  - Delta from pre-Task-3 baseline (150/44/0/106): **−101 skip, +84 pass, −17 total**.
- **Historical Phase-3 follow-ups** (five plan items; a mix of runtime skips,
  source-level re-homes, and WONT rows):
  - **CTC-NR-04** — NR 0xC5 vs port-write overlap; cycle-accurate bus arbitration; user-deferred review-later (WONT-sweep candidate, not WONT this wave).
  - **NR-C0-02** — NR 0xC0 `stackless_nmi` execution; re-homed to `atic_atac_nmi_test` ATIC-NMI-02 after GH #84 closed G49.
  - **DMA-04** — NMI-driven DMA delay; blocked on the same NMI subsystem.
  - **ULA-INT-04** — line interrupt at `cvc` match; re-home candidate to `ctc_interrupts_test.cpp` (needs live ULA line-counter state).
  - **ULA-INT-06** — line 0 → `c_max_vc` wrap; re-home candidate (needs ULA `c_max_vc` observable).
- **New companion suite `test/ctc_interrupts/ctc_interrupts_test.cpp`** (created Phase 3c, commit `87fb998`): 10 rows re-homed from `ctc_test.cpp` — **`Total:   10  Passed:   10  Failed:    0  Skipped:    0`** — covering ULA-INT-01/02/03/05, NR-C0-04, NR-C4-02/03, NR-C6-02, ISC-09/10 against a full `Emulator` fixture.
- **Architectural change**: the IM2 fabric, previously a 45-line priority-mask stub in `src/cpu/im2.{h,cpp}`, expanded to a full VHDL-faithful `Im2Controller` + `Im2Client` mixin (~171 lines of `.h` + ~800 lines of `.cpp` + new `src/cpu/im2_client.h`). It now covers `device/im2_control.vhd` (RETI/RETN/IM-mode decoder, DMA delay), `device/im2_device.vhd` (per-device state machine `S_0`/`S_REQ`/`S_ACK`/`S_ISR`), `device/im2_peripheral.vhd` (edge detect + int_unq + int_status latches), `device/peripherals.vhd` (daisy chain), and the NR 0xC0/C4/C5/C6/C8/C9/CA/CC/CD/CE handlers in `emulator.cpp`. The legacy `Im2Level` enum is preserved as a compatibility wrapper. `Z80Cpu` gained an opt-in `on_int_ack` callback that is byte-identical to the legacy path when null. Regression (34/0/0) and FUSE Z80 (1356/1356) unchanged throughout.
- **Follow-up closure:** NR-C0-02 now passes in `atic_atac_nmi_test`
  ATIC-NMI-02 (GH #84 / G49). Historical phase counts below remain as the landing
  record for the original CTC skip-reduction work.
- **GH #196 Phase 1.3 (2026-08-01)** — the traceability matrix's "Extra
  coverage (not in plan)" table for this suite has been removed. It held 3
  rows: MC-01 ("4 channels loaded with different TCs"), MC-02 ("Channels
  decrement independently"), MC-03 ("Read invalid channel returns 0xFF") —
  none with a live `check()`/`skip()` call anywhere in `test/ctc/ctc_test.cpp`
  or `test/ctc_interrupts/ctc_interrupts_test.cpp` today. `git log -S` on each
  ID shows all 3 were implemented in the original compliance runner
  (`f7e1b035`) and removed by the same commit, `8ec4383a`
  ("rewrite in Phase 2 per-row idiom, 150 rows, 44 check / 106 skip") —
  real assertions from an earlier revision, not rows that were never
  implemented. None of the 3 described behaviors is asserted today by a
  live row under a different ID either: the closest analog, CTC-CH-06,
  loads all 4 channels with the *same* time constant in counter mode and
  asserts they stay stuck (no clock source), not 4 *different* TCs
  decrementing independently in timer mode; no row anywhere probes an
  out-of-range channel read. Dropped rather than folded: recording them as
  covered would be fabricating coverage no live test provides.

## VHDL Source Files

| File | Role |
|------|------|
| `device/ctc.vhd` | Top-level CTC: 4 channels, port selection, data mux |
| `device/ctc_chan.vhd` | Single CTC channel: state machine, prescaler, counter |
| `device/im2_control.vhd` | RETI/RETN decoder, IM mode detection, DMA delay |
| `device/im2_device.vhd` | Per-device IM2 state machine: daisy chain, vector, ACK |
| `device/im2_peripheral.vhd` | Wrapper: pulse/IM2 mode select, status bits, edge detect |
| `device/peripherals.vhd` | Instantiates 14 `im2_peripheral` instances with daisy chain |
| `zxnext.vhd` | Top-level wiring: CTC ports, interrupt priority, NextREGs |

## Architecture

### Test runner

A dedicated test harness (`ctc_interrupts_test.cpp`) that:

1. Provides direct access to CTC channel registers via simulated I/O writes
   to ports 0x183B-0x1B3B (channels 0-3, selected by A[10:8]).
2. Simulates clock/trigger edges for counter mode testing.
3. Hooks into the IM2 interrupt controller to verify vector generation,
   priority ordering, and daisy-chain behaviour.
4. Verifies NextREG reads/writes for interrupt enable, status, and DMA
   interrupt enable registers.

### CTC port addressing (from zxnext.vhd)

CTC ports are decoded when `A[15:11] = "00011"` and `A[0] = '1'` (port_3b_lsb)
and CTC I/O is enabled (internal_port_enable[27]):

| Port   | A[10:8] | Channel |
|--------|---------|---------|
| 0x183B |   000   |    0    |
| 0x193B |   001   |    1    |
| 0x1A3B |   010   |    2    |
| 0x1B3B |   011   |    3    |

Reading a CTC port returns the current down-counter value (t_count).
Writing follows the CTC protocol: control words (D0=1), time constants
(after D2=1 in control word), or interrupt vectors (D0=0).

### Interrupt priority order (from zxnext.vhd lines 1930-1939)

14 interrupt sources, bit 0 = highest priority:

| Index | Source      | im2_int_req signal         |
|-------|-------------|----------------------------|
|   0   | Line        | line_int_pulse             |
|   1   | UART0 RX    | uart0_rx_near_full/avail   |
|   2   | UART1 RX    | uart1_rx_near_full/avail   |
| 3-10  | CTC 0-7     | ctc_zc_to[7:0]             |
|  11   | ULA         | ula_int_pulse              |
|  12   | UART0 TX    | uart0_tx_empty             |
|  13   | UART1 TX    | uart1_tx_empty             |

Note: Only CTC channels 0-3 are active; channels 4-7 are hardwired to 0.

### IM2 vector formation (from zxnext.vhd line 1999)

```
im2_vector = nr_c0_im2_vector[2:0] & im2_vec[3:0] & '0'
```

The 4-bit `im2_vec` encodes the peripheral index (0-13), giving each device
a unique vector. The 3-bit MSB comes from NextREG 0xC0 bits [7:5]. The LSB
is always 0 (vectors are word-aligned).

## Test Case Catalog

### Section 1: CTC Channel State Machine

Tests based on the 5-state machine in `ctc_chan.vhd`:
`S_RESET -> S_RESET_TC -> S_TRIGGER -> S_RUN -> S_RUN_TC`

| ID | Test | Expected |
|----|------|----------|
| CTC-SM-01 | Hard reset: channel starts in S_RESET | Read returns time_constant_reg (initially 0x00) |
| CTC-SM-02 | Write control word without D2=1 while in S_RESET | Channel stays in S_RESET, no counting |
| CTC-SM-03 | Write control word with D2=1 (TC follows) | Channel transitions to S_RESET_TC, awaits TC |
| CTC-SM-04 | Write time constant after D2=1 control word | Channel transitions to S_TRIGGER then S_RUN |
| CTC-SM-05 | Timer mode (D6=0) without trigger (D3=1): wait in S_TRIGGER | Channel stays in S_TRIGGER until clock edge |
| CTC-SM-06 | Timer mode (D6=0) without trigger (D3=0): immediate S_RUN | Channel goes directly to S_RUN |
| CTC-SM-07 | Counter mode (D6=1): immediate S_RUN from S_TRIGGER | Counter mode skips trigger wait |
| CTC-SM-08 | Write control word with D2=1 while in S_RUN | Channel transitions to S_RUN_TC |
| CTC-SM-09 | Write time constant while in S_RUN_TC | Channel returns to S_RUN with new TC loaded |
| CTC-SM-10 | Soft reset (D1=1, D2=0): return to S_RESET | Channel stops counting, enters S_RESET |
| CTC-SM-11 | Soft reset (D1=1, D2=1): go to S_RESET_TC | Channel stops, awaits new time constant |
| CTC-SM-12 | Double soft reset required when in S_RESET_TC | First CW write with D1=1 consumed as TC; second resets |
| CTC-SM-13 | Control word write while running (D1=0, D2=0) | Channel keeps running, control bits updated |

### Section 2: CTC Timer Mode (Prescaler)

Tests based on the prescaler logic in `ctc_chan.vhd`.

| ID | Test | Expected |
|----|------|----------|
| CTC-TM-01 | Prescaler = 16 (D5=0): counter decrements every 16 clocks | ZC/TO after TC * 16 clocks |
| CTC-TM-02 | Prescaler = 256 (D5=1): counter decrements every 256 clocks | ZC/TO after TC * 256 clocks |
| CTC-TM-03 | Time constant = 1: ZC/TO after 1 prescaler cycle | Fastest possible ZC/TO |
| CTC-TM-04 | Time constant = 0 means 256 (8-bit wrap) | Counter loads 0x00, counts 256 before ZC/TO |
| CTC-TM-05 | Prescaler resets on soft reset | p_count returns to 0 when not in S_RUN/S_RUN_TC |
| CTC-TM-06 | ZC/TO reloads time constant automatically | After ZC/TO, counter reloads TC and continues |
| CTC-TM-07 | ZC/TO pulse duration is exactly 1 clock cycle | o_zc_to asserted for one i_CLK cycle (delayed by 1) |
| CTC-TM-08 | Read port returns current down-counter value | Verify intermediate counter values |
| CTC-TM-G120-01 | Mid-stream TC reload preserves prescaler (S_RUN_TC → S_RUN) | skip — ctc.cpp:41 clears prescaler_ on every TC write incl. running-reload; VHDL clears only on reset_soft (see G120) |

### Section 3: CTC Counter Mode

Tests based on counter mode (D6=1) in `ctc_chan.vhd`.

| ID | Test | Expected |
|----|------|----------|
| CTC-CM-01 | Counter mode: decrement on falling external edge (D4=0) | Count decrements on high-to-low transition |
| CTC-CM-02 | Counter mode: decrement on rising external edge (D4=1) | Count decrements on low-to-high transition |
| CTC-CM-03 | Counter mode: ZC/TO when count reaches 0 | ZC/TO pulse after TC external edges |
| CTC-CM-04 | Counter mode: automatic reload after ZC/TO | Counter reloads TC and continues counting |
| CTC-CM-05 | Changing edge polarity (D4) counts as clock edge | Writing CW that changes D4 decrements counter |

### Section 4: CTC Chaining (ZC/TO as Clock/Trigger)

Tests based on `i_clk_trg` wiring in zxnext.vhd line 4084:
`i_clk_trg <= ctc_zc_to(2 downto 0) & ctc_zc_to(3)`

| ID | Test | Expected |
|----|------|----------|
| CTC-CH-01 | Channel 0 trigger = ZC/TO of channel 3 | Ch0 counter mode decrements on ch3 ZC/TO |
| CTC-CH-02 | Channel 1 trigger = ZC/TO of channel 0 | Ch1 counter mode decrements on ch0 ZC/TO |
| CTC-CH-03 | Channel 2 trigger = ZC/TO of channel 1 | Ch2 counter mode decrements on ch1 ZC/TO |
| CTC-CH-04 | Channel 3 trigger = ZC/TO of channel 2 | Ch3 counter mode decrements on ch2 ZC/TO |
| CTC-CH-05 | Cascaded chain: ch0 timer -> ch1 counter -> ch2 counter | 3-stage cascade produces expected ZC/TO rate |
| CTC-CH-06 | Circular chain avoided: only one channel in timer mode | Ring topology with all in counter mode is dead |

### Section 5: CTC Control Word and Vector Protocol

Tests based on the register logic in `ctc_chan.vhd`.

| ID | Test | Expected |
|----|------|----------|
| CTC-CW-01 | Control word (D0=1): bits [7:3] stored in control_reg | Verify each bit field: D7=int_en, D6=mode, D5=prescaler, D4=edge, D3=trigger |
| CTC-CW-02 | Vector word (D0=0): only accepted by channel 0 | o_vector_wr pulsed only for channel 0 writes |
| CTC-CW-03 | Vector word to channels 1-3: treated as vector but o_vector_wr not pulsed | Per ctc.vhd line 150: o_vector_wr = iowr_vc(0) only |
| CTC-CW-04 | Time constant follows control word with D2=1 | Next write is treated as TC regardless of D0 |
| CTC-CW-05 | Write during S_RESET_TC: any byte is the time constant | Even byte with D0=1 is TC, not a new CW |
| CTC-CW-06 | Control word with D7=1: enable interrupt for channel | o_int_en goes high |
| CTC-CW-07 | Control word with D7=0: disable interrupt for channel | o_int_en goes low |
| CTC-CW-08 | External int_en_wr overrides D7 bit | i_int_en_wr writes i_int_en to control_reg[7-3] |
| CTC-CW-09 | Hard reset clears control_reg to all zeros | All control bits default to 0 |
| CTC-CW-10 | Hard reset clears time_constant_reg to 0x00 | TC defaults to 0 |
| CTC-CW-11 | Write edge: iowr is rising-edge detected (i_iowr AND NOT iowr_d) | Double-write prevention on held signals |

### Section 6: CTC Interrupt Enable via NextREG

Tests based on zxnext.vhd CTC interrupt enable wiring.

| ID | Test | Expected |
|----|------|----------|
| CTC-NR-01 | NextREG 0xC5 write: sets CTC interrupt enable bits [3:0] | nr_c5_we triggers i_int_en_wr for CTC |
| CTC-NR-02 | NextREG 0xC5 read: returns ctc_int_en[7:0] | All 8 bits readable, upper 4 always 0 |
| CTC-NR-03 | CTC control word D7 also sets int_en independently | Both paths (CW and NextREG) can set int_en |
| CTC-NR-04 | NextREG 0xC5 write does not overlap with port CTC write | Constraint from VHDL: i_int_en_wr must not overlap i_iowr |

> `CTC-NR-03` covers the control-word path only as far as the NR 0xC5
> readback. Its propagation into the IM2 fabric's per-device `int_en` —
> the seam that shipped as GH #47/#48 — is covered by **Section 17**.

### Section 7: IM2 Control Block (RETI/RETN Detection)

Tests based on `im2_control.vhd` state machine.

| ID | Test | Expected |
|----|------|----------|
| IM2C-01 | ED prefix detected: enter S_ED_T4 | State machine recognizes ED opcode |
| IM2C-02 | ED 4D sequence: o_reti_seen pulsed | RETI detected, one-cycle pulse |
| IM2C-03 | ED 45 sequence: o_retn_seen pulsed | RETN detected, one-cycle pulse |
| IM2C-04 | ED followed by non-4D/45: return to S_0 | No false RETI/RETN detection |
| IM2C-05 | o_reti_decode asserted during S_ED_T4 | Peripherals see decode window |
| IM2C-06 | CB prefix: enter S_CB_T4, wait for next fetch | CB handled correctly (not ED) |
| IM2C-07 | DD/FD prefix chain: stay in S_DDFD_T4 | Multiple DD/FD prefixes handled |
| IM2C-08 | DMA delay: asserted during ED, ED4D, ED45, SRL states | o_dma_delay covers RETI pop sequence |
| IM2C-09 | SRL delay states: 2 extra cycles after RETI/RETN | Prevents DMA accumulation of return addresses |
| IM2C-10 | IM mode detection: ED 46 = IM 0 | im_mode = "00" |
| IM2C-11 | IM mode detection: ED 56 = IM 1 | im_mode = "01" |
| IM2C-12 | IM mode detection: ED 5E = IM 2 | im_mode = "10" |
| IM2C-13 | IM mode updates on falling edge of CLK_CPU | Per VHDL: `falling_edge(i_CLK_CPU)` |
| IM2C-14 | IM mode default after reset: IM 0 | im_mode = "00" after reset |
| IM2C-G87-01 | RETI (ED 4D) seen by Im2Controller via real Z80Cpu M1 stream | skip — Z80Cpu::on_m1_cycle fires once per execute() with FIRST byte only; ED 2nd byte unobserved (see G87) |
| IM2C-G87-02 | RETN (ED 45) seen by Im2Controller via real Z80Cpu M1 stream | skip — same per-byte-M1-hook prerequisite as IM2C-G87-01 (see G87) |

### Section 8: IM2 Device (Per-Peripheral State Machine)

Tests based on `im2_device.vhd` state machine:
`S_0 -> S_REQ -> S_ACK -> S_ISR -> S_0`

| ID | Test | Expected |
|----|------|----------|
| IM2D-01 | Interrupt request: S_0 -> S_REQ when i_int_req=1 and M1=high | Transition requires M1 not active |
| IM2D-02 | INT_n asserted in S_REQ when IEI=1 and IM2 mode | o_int_n = '0' |
| IM2D-03 | INT_n not asserted when IEI=0 | Higher priority device blocking |
| IM2D-04 | INT_n not asserted when not in IM2 mode | i_im2_mode must be '1' |
| IM2D-05 | Acknowledge: S_REQ -> S_ACK on M1=0, IORQ=0, IEI=1 | Standard Z80 interrupt acknowledge cycle |
| IM2D-06 | S_ACK -> S_ISR when M1 returns high | ISR is now being executed |
| IM2D-07 | S_ISR -> S_0 on RETI seen with IEI=1 | ISR serviced, device resets |
| IM2D-08 | S_ISR stays in S_ISR without RETI | ISR not yet complete |
| IM2D-09 | Vector output during S_ACK (or S_ACK transition) | o_vec = i_vec during acknowledge |
| IM2D-10 | Vector output = 0 when not in ACK | Allows OR-combining of multiple device vectors |
| IM2D-11 | o_isr_serviced pulsed on S_ISR -> S_0 transition | One-cycle pulse to reset external interrupt logic |
| IM2D-12 | DMA interrupt: o_dma_int=1 whenever state != S_0 and dma_int_en=1 | DMA can be interrupted by any pending interrupt |

### Section 9: IM2 Daisy Chain Priority

Tests based on daisy chain logic in `im2_device.vhd` and `peripherals.vhd`.

| ID | Test | Expected |
|----|------|----------|
| IM2P-01 | IEO = IEI in S_0 state (idle) | Pass-through when no interrupt |
| IM2P-02 | IEO = IEI AND reti_decode in S_REQ state | Only pass IEO during RETI decode when requesting |
| IM2P-03 | IEO = 0 in S_ACK and S_ISR states | Block all lower-priority devices during service |
| IM2P-04 | Highest-priority device (index 0) has IEI=1 always | Wired to '1' in peripherals.vhd |
| IM2P-05 | Two simultaneous requests: lower index wins | Higher priority device gets acknowledged first |
| IM2P-06 | Lower-priority device queued while higher is serviced | IEO=0 blocks lower device's INT_n |
| IM2P-07 | After RETI of higher-priority ISR: lower device proceeds | IEO restored, lower device can now be acknowledged |
| IM2P-08 | Chain of 3: device 0 in ISR, device 1 requesting, device 2 requesting | Device 1 next after device 0 RETI |
| IM2P-09 | INT_n is AND of all device int_n signals | Any device can assert interrupt |
| IM2P-10 | Vector OR: only acknowledged device outputs non-zero vector | Correct vector formed by OR-combine |

### Section 10: Pulse Interrupt Mode

Tests based on `im2_peripheral.vhd` pulse mode and zxnext.vhd pulse logic.

| ID | Test | Expected |
|----|------|----------|
| PULSE-01 | Pulse mode (nr_c0[0]=0): pulse_en from qualified int_req | o_pulse_en = (int_req AND int_en) OR int_unq |
| PULSE-02 | IM2 mode (nr_c0[0]=1): pulse_en suppressed | No pulse output when in IM2 mode |
| PULSE-03 | ULA exception (EXCEPTION='1'): pulse even in IM2 when CPU not in IM2 | ULA generates pulse when mode_pulse=1 but im2_mode=0 |
| PULSE-04 | pulse_int_n goes low on pulse_en, stays low for count duration | Pulsed interrupt starts on falling edge of CLK_28 |
| PULSE-05 | 48K/+3 timing: pulse duration = 32 CPU cycles | pulse_count[5] terminates when machine_timing_48 or _p3 |
| PULSE-06 | 128K/Pentagon timing: pulse duration = 36 CPU cycles | pulse_count[5] AND pulse_count[2] terminates |
| PULSE-07 | Pulse counter resets when pulse_int_n=1 | Counter only runs while interrupt is active |
| PULSE-08 | INT_n to Z80 = pulse_int_n AND im2_int_n | Both paths combined |
| PULSE-09 | External bus INT: o_BUS_INT_n = pulse_int_n AND im2_int_n | Same signal output to expansion bus |
| PULSE-G89-01 | LDIRX samples INT/NMI between iterations | skip — z80n_ext.cpp:352-427 closed C for-loop runs all iterations without INT/NMI sample (see G89) |
| PULSE-G89-02 | LDDRX samples INT/NMI between iterations | skip — same closed-loop prerequisite as PULSE-G89-01 (see G89) |
| PULSE-G89-03 | LDPIRX samples INT/NMI between iterations | skip — same closed-loop prerequisite as PULSE-G89-01 (see G89) |
| PULSE-G89-04 | LDIRSCALE samples INT/NMI between iterations | skip — same closed-loop prerequisite as PULSE-G89-01 (see G89) |
| PULSE-G90-01 | 28 MHz turbo SRAM-read wait state asserts sram_wait_n | skip — ContentionModel gated OFF at 28 MHz; no sram_wait references in jnext (see G90) |
| PULSE-G121-01 | NR 0x03 machine-timing post-boot updates pulse_count_end | skip — Im2Controller::set_machine_timing_48_or_p3 called once at Emulator::reset_machine only; tape loaders flipping NR 0x03 see wrong pulse width (see G121) |

### Section 11: IM2 Peripheral Wrapper

Tests based on `im2_peripheral.vhd` edge detection and status logic.

| ID | Test | Expected |
|----|------|----------|
| IM2W-01 | Edge detection: int_req = i_int_req AND NOT int_req_d | Only rising edge of interrupt request triggers |
| IM2W-02 | im2_int_req latched: stays high until ISR serviced | Holds request across clock domains |
| IM2W-03 | im2_int_req cleared by im2_isr_serviced | ISR serviced edge-detected and clears pending |
| IM2W-04 | int_status set by int_req or int_unq | Status bit reflects any interrupt source |
| IM2W-05 | int_status cleared by i_int_status_clear | Software clear via NextREG write |
| IM2W-06 | o_int_status = int_status OR im2_int_req | Combined status visible to software |
| IM2W-07 | im2_reset_n = mode_pulse AND NOT reset | IM2 device held in reset during pulse mode |
| IM2W-08 | Unqualified interrupt (int_unq): bypasses int_en | Direct interrupt regardless of enable bit |
| IM2W-09 | isr_serviced edge detection across clock domains | isr_serviced_d used for CLK_28 domain crossing |
| IM2W-G119-01 | ZC/TO raised unconditionally; int_en AND at fabric edge | skip — ctc.cpp:251-282 only calls on_interrupt when channel int_enabled; mid-pulse int_en flip drops prior edge (see G119) |

### Section 12: ULA and Line Interrupts

Tests based on ULA timing module and zxnext.vhd wiring.

| ID | Test | Expected |
|----|------|----------|
| ULA-INT-01 | ULA interrupt generated at specific HC/VC position | int_ula pulse when hc=c_int_h and vc=c_int_v |
| ULA-INT-02 | ULA interrupt disabled by port 0xFF bit (port_ff_interrupt_disable) | inten_ula_n=1 suppresses ULA interrupt |
| ULA-INT-03 | ULA interrupt enable: ula_int_en[0] = NOT port_ff_interrupt_disable | Directly tied to port FF |
| ULA-INT-04 | Line interrupt at configurable scanline | Fires when cvc matches nr_23_line_interrupt - 1 at hc_ula=255 |
| ULA-INT-05 | Line interrupt enable: nr_22_line_interrupt_en | NextREG 0x22 bit 1 controls line interrupt |
| ULA-INT-06 | Line interrupt scanline 0 maps to c_max_vc | Special case: line 0 wraps to maximum VC |
| ULA-INT-07 | ULA interrupt is priority index 11 | Maps to im2_int_req bit 11 |
| ULA-INT-08 | Line interrupt is priority index 0 (highest) | Maps to im2_int_req bit 0 |
| ULA-INT-09 | ULA has EXCEPTION='1' in peripherals instantiation | Only ULA gets pulse in IM2 mode when CPU not in IM2 |

### Section 13: NextREG Interrupt Control Registers

Tests based on NextREG read/write logic in zxnext.vhd.

| ID | Test | Expected |
|----|------|----------|
| NR-C0-01 | Write NextREG 0xC0: bits [7:5] = IM2 vector MSBs | nr_c0_im2_vector set |
| NR-C0-02 | Write NextREG 0xC0: bit [3] = stackless NMI | pass — re-homed to `atic_atac_nmi_test` ATIC-NMI-02 after GH #84 / Atic Atac supplied the user-visible driver; pins suppressed NMIACK stack writes, C2/C3 capture, live RETN substitution and save/load |
| NR-C0-03 | Write NextREG 0xC0: bit [0] = pulse(0)/IM2(1) mode | nr_c0_int_mode_pulse_0_im2_1 set |
| NR-C0-04 | Read NextREG 0xC0: returns vector, stackless, im_mode, int_mode | Format: VVV_0_S_MM_I |
| NR-C4-01 | Write NextREG 0xC4: bit [7] = expansion bus int enable | nr_c4_int_en_0_expbus set |
| NR-C4-02 | Write NextREG 0xC4: bit [1] = line interrupt enable | nr_22_line_interrupt_en updated |
| NR-C4-03 | Read NextREG 0xC4: returns expbus & ula_int_en | Format: E_00000_UU |
| NR-C5-01 | Write NextREG 0xC5: CTC interrupt enable bits [3:0] | Writes to CTC via i_int_en_wr |
| NR-C5-02 | Read NextREG 0xC5: returns ctc_int_en[7:0] | Upper 4 bits always 0 |
| NR-C6-01 | Write NextREG 0xC6: UART interrupt enable | nr_c6_int_en_2_654[6:4] and nr_c6_int_en_2_210[2:0] |
| NR-C6-02 | Read NextREG 0xC6: returns 0_654_0_210 | Format matches write |
| NR-C8-01 | Read NextREG 0xC8: line and ULA interrupt status | Bits: 000000_line_ula |
| NR-C9-01 | Read NextREG 0xC9: CTC interrupt status [10:3] | 8 CTC channel status bits |
| NR-CA-01 | Read NextREG 0xCA: UART interrupt status | Format: 0_uart1tx_uart1rx_uart1rx_0_uart0tx_uart0rx_uart0rx |
| NR-CC-01 | Write NextREG 0xCC: DMA interrupt enable group 0 | Bit 7 = dma_int_en_0_7, bits [1:0] = dma_int_en_0_10 |
| NR-CD-01 | Write NextREG 0xCD: DMA interrupt enable group 1 | Full byte = nr_cd_dma_int_en_1 |
| NR-CE-01 | Write NextREG 0xCE: DMA interrupt enable group 2 | Bits [6:4] and [2:0] |
| NR-C2-01 | NMI captures PC into NR 0xC2 (RETN address LSB) | skip — no NR 0xC2/0xC3 read handler in jnext; fuse_z80_nmi() pushes PC but does not propagate to NextReg shadow (see G88) |
| NR-C3-01 | NMI captures PC into NR 0xC3 (RETN address MSB) | skip — same handler-missing prerequisite as NR-C2-01 (see G88) |

### Section 14: Interrupt Status and Clear

Tests based on im2_status_clear wiring in zxnext.vhd.

| ID | Test | Expected |
|----|------|----------|
| ISC-01 | Write NextREG 0xC8 bit 1: clear line interrupt status | nr_c8_we AND nr_wr_dat(1) clears index 0 |
| ISC-02 | Write NextREG 0xC8 bit 0: clear ULA interrupt status | nr_c8_we AND nr_wr_dat(0) clears index 11 |
| ISC-03 | Write NextREG 0xC9: clear individual CTC status bits | Each bit of nr_c9_we AND nr_wr_dat clears CTC 0-7 |
| ISC-04 | Write NextREG 0xCA bit 6: clear UART1 TX status | nr_ca_we AND nr_wr_dat(6) clears index 13 |
| ISC-05 | Write NextREG 0xCA bit 2: clear UART0 TX status | nr_ca_we AND nr_wr_dat(2) clears index 12 |
| ISC-06 | Write NextREG 0xCA bits 5|4: clear UART1 RX status | OR of bits clears index 2 |
| ISC-07 | Write NextREG 0xCA bits 1|0: clear UART0 RX status | OR of bits clears index 1 |
| ISC-08 | Status bit re-set by new interrupt while clear pending | New int_req OR int_unq overrides clear |
| ISC-09 | Legacy NextREG 0x20 read: returns mixed status | Bit layout: line_ula_00_ctc6..ctc3 |
| ISC-10 | Legacy NextREG 0x22 read: includes pulse_int_n state | Bit 7 = NOT pulse_int_n |

### Section 15: DMA Interrupt Integration

Tests based on DMA interrupt wiring in zxnext.vhd.

| ID | Test | Expected |
|----|------|----------|
| DMA-01 | im2_dma_int set when any peripheral has dma_int=1 | OR of all peripheral dma_int signals |
| DMA-02 | im2_dma_delay latched on im2_dma_int | Delay starts on DMA interrupt |
| DMA-03 | im2_dma_delay held by dma_delay signal | RETI pop holds DMA delay |
| DMA-04 | NMI also triggers DMA delay when nr_cc_dma_int_en_0_7=1 | nmi_activated AND dma_int_en_0_7 |
| DMA-05 | DMA delay cleared on reset | im2_dma_delay = 0 after reset |
| DMA-06 | Per-peripheral DMA int enable via NextREGs 0xCC-0xCE | Each source independently controllable |

### Section 16: Unqualified Interrupts

Tests based on im2_int_unq wiring in zxnext.vhd.

| ID | Test | Expected |
|----|------|----------|
| UNQ-01 | NextREG 0x20 write bit 7: unqualified line interrupt | nr_20_we AND nr_wr_dat(7) sets index 0 |
| UNQ-02 | NextREG 0x20 write bits [3:0]: unqualified CTC 0-3 | nr_20_we AND nr_wr_dat[3:0] sets indices 3-6 |
| UNQ-03 | NextREG 0x20 write bit 6: unqualified ULA interrupt | nr_20_we AND nr_wr_dat(6) sets index 11 |
| UNQ-04 | Unqualified interrupt bypasses int_en check | im2_int_req set regardless of enable bit |
| UNQ-05 | Unqualified interrupt sets int_status | Status bit reflects unqualified source |

### Section 17: CTC ZC/TO to Joystick IO Mode

Tests based on zxnext.vhd joystick IO mode wiring.

| ID | Test | Expected |
|----|------|----------|
| JOY-01 | Joystick IO mode 01: CTC channel 3 ZC/TO toggles pin7 | ctc_zc_to(3) toggles joy_iomode_pin7 |
| JOY-02 | Toggle conditioned on nr_0b_joy_iomode_0 or pin7=0 | Guard condition for toggle |

### Section 18: Debugger Single-Step Interrupt Delivery (Task 60a)

Regression rows for the Task 60a bug: `Emulator::execute_single_instruction()`
(the debugger step path) was a hand-maintained copy of `run_frame()`'s
per-instruction cluster and had drifted — it never called `im2_.tick()`,
never polled the IM2-mode / pulse-mode /INT lines (VHDL zxnext.vhd:1840
`z80_int_n <= pulse_int_n AND im2_int_n` with expbus_disable_int='1'),
never ticked `md6_` and never recorded trace entries. Interrupts were
silently dropped while stepping. Fix: both paths share one body
(`step_one_instruction()` + `tick_devices_after_instruction()`).
Rows live in `test/ctc_interrupts/ctc_interrupts_test.cpp` (group
SingleStep) and are the single-step analogues of CTC-INT-V20-IM2-01 /
ULA-INT-V19-IM2-04.

| ID | Test | Expected |
|----|------|----------|
| SSTEP-01 | Pulse-mode CTC INT delivered via execute_single_instruction loop (zxnext.vhd:1840; im2_peripheral.vhd:186-194) | PC reaches IM1 vector 0x0038 (<0x4000); pre-fix stuck at 0x8000+ |
| SSTEP-02 | IM2-mode CTC INT delivered via single-step (zxnext.vhd:1840, :1999) | CTC0 device state >= S_ACK after IntAck; pre-fix stuck at S_0 |
| SSTEP-03 | Trace log records one entry per single-step (parity with run_frame) | +1 entry per step; pre-fix +0 |
| SSTEP-04 | MD6 FSM advances during single-step (md6_joystick_connector_x2.vhd:103-114, :151-152) | raw bits 5:0 latched into joy_left_word; pre-fix latch stays 0 |

### Section 18b: Debugger Single-Step Frame Turnover (GH #207, 2026-08-06)

Task 60a shared the per-INSTRUCTION body; the per-FRAME body stayed in
`run_frame()`, and the frontends stop calling `run_frame()` altogether while
the debugger is paused (`src/platform/frame_sequencer.h`: `if (!fx.paused())`).
So a stepping session ran the clock straight past the end of the frame it was
in and never began another: `begin_new_frame()` is the ONLY site that schedules
the ULA frame interrupt (zxula_timing.vhd:551, fired once per frame at
(c_int_h, c_int_v)), the line interrupt, and the per-scanline SCANLINE/VSYNC
events. Nothing per-frame could fire again for the rest of the session.

The user-visible symptom (GH #207) was a `HALT` that no number of Steps could
leave: t80n.vhd:496 freezes PC while `Halt_FF` is set and :502-503 forces IR to
0x00 (repeated M1 NOP fetches at the same address), and t80n.vhd:1727 clears
`Halt_FF` only on an accepted interrupt or NMI cycle — so with no interrupt
ever scheduled the CPU could never leave the halt.

Fix: `end_of_frame()` extracted from `run_frame()`'s tail and shared with the
new `step_frame_slot()`, both driven by the new `Emulator::debugger_step()` —
the debugger's Step, as distinct from the raw one-slot primitive
`execute_single_instruction()` that most of the test tree uses and that keeps
its frame-agnostic behaviour. A Step issued at a halt additionally runs the
halt out (bounded to two frames, since the ULA interrupt fires once per frame
and the current one may already be past its firing point). Rows live in
`test/ctc_interrupts/ctc_interrupts_test.cpp` (group SingleStep) and drive
`debugger_step()`.

| ID | Test | Expected |
|----|------|----------|
| SSTEP-05 | ULA frame INT scheduled and delivered during single-step (zxula_timing.vhd:551; zxnext.vhd:1840) | PC reaches IM1 vector 0x0038 (<0x4000); pre-fix stuck at 0x8000+ |
| SSTEP-06 | A Step at a HALT leaves the halt into the ISR (t80n.vhd:496, :502-503, :1727) | not halted, PC <0x4000 after the step; pre-fix halted at 0x8000 forever |
| SSTEP-07 | Frames keep turning over — a SECOND HALT is left too (zxula_timing.vhd:551; t80n.vhd:1727) | second halt also left, at a later clock; pre-fix the next frame's INT is never scheduled |
| SSTEP-08 | A Step at a DI'd HALT is bounded and reports no progress (t80n.vhd:1727) | still halted, PC unmoved, <= 2-frame budget spent (guards against an unbounded halt-run loop) |

## Section 15: C-IM2 quiescent early-out equivalence (Task 27 C-IM2, 2026-07-15)

`Im2Controller::tick()` early-outs when the fabric is quiescent (Task 27
optimization; field-by-field equivalence proof at the early-out in
`src/cpu/im2.cpp`). These rows pin the contract: a skipped tick must be
observably identical to an executed one. Rows live in
`test/ctc_interrupts/ctc_interrupts_test.cpp` (group CIM2-Quiescence),
on a bare `Im2Controller`. All four designed mutations (predicate
always-true; live `reti_seen_pulse_` check dropped; `int_req_d` clause
dropped; `raise_req` invalidation dropped) were run and are caught.

| ID | Test | Expected |
|----|------|----------|
| CIM2-QUIESCE-01 | Pulse mode: 1000 quiescent ticks leave `save_state` byte-identical; a pulse raised after the stretch keeps the exact 36-CPU-cycle width (zxnext.vhd:2033-2044); a second edge after a stretch is not masked by a stale `int_req_d` (im2_peripheral.vhd:98-101, :154-162) | snapshots equal; pulse low through count 32, high at 36; `int_status` set by the post-stretch edge |
| CIM2-QUIESCE-02 | IM2 mode: S_REQ and S_ISR are stable across quiescent stretches (byte-identical snapshots); IntAck after a stretch yields vector 0x06 for CTC0 (zxnext.vhd:1999); RETI decoded after a stretch still clears S_ISR via the tick path (im2_device.vhd:123-128) | snapshots equal; vec==0x06; S_ISR→S_0 on the RETI tick; fresh raise re-enters S_REQ |

## Section 16: C1 CTC event-horizon tick() equivalence (Task 27 C1, 2026-07-15)

`Ctc::tick(N)` was rewritten from an O(N×4) per-cycle loop to an
O(ZC/TO events) event-horizon loop: gap cycles are applied in closed
form by `CtcChannel::advance()`, and the cycle in which the earliest
ZC/TO fires runs the original exact per-channel inner loop (validated
by the pre-existing VHDL-cited rows). These rows pin the seam. Rows
live in `test/ctc_interrupts/ctc_interrupts_test.cpp` (group
CTC-C1-ACC), on a bare `Ctc`. All three designed mutations (advance()
drops prescaler accumulation; advance() drops counter decrement; gap
advance over-advances by one cycle) were run and are caught.

| ID | Test | Expected |
|----|------|----------|
| CTC-C1-ACC-01 | Timer /16 TC=3: one tick(150) span crosses three ZC/TO events (ctc_chan.vhd:143-146 prescaler_clk, :162-170 zc_to); prescaler phase survives the closed-form jump | exactly 3 pulses in the span; counter reads 3; 4th pulse exactly at tick 192 |
| CTC-C1-ACC-02 | ch0 timer /16 TC=3 daisy-chained into ch1 counter TC=2 (zxnext.vhd:4084); one tick(200) vs 200× tick(1) | both produce callback sequence 0,0,1,0,0,1 and identical final counters (3, 2) |
| CTC-C1-ACC-03 | ch1 timer armed with D3=1 (S_TRIGGER) started mid-span by ch0's ZC/TO at 16; activation cycle still ticks the newly-RUN channel (established per-cycle model ordering) | ch1's first ZC/TO at tick 31 (sequence 0,1 in tick(31)); ch0's next at 32; tick(31) == 31× tick(1) |

## Section 17: CTC control-word D7 reaches the IM2 fabric (GH #47/#48, 2026-07-22)

`ctc_chan.vhd:265-276` keeps **one** interrupt-enable flip-flop per
channel, `control_reg(7)`, with two mutually exclusive writers:

```vhdl
if reset_hard='1'     then control_reg <= (others => '0');
elsif iowr_cw='1'     then control_reg <= i_cpu_d(7 downto 3);   -- control word, D7
elsif i_int_en_wr='1' then control_reg(7-3) <= i_int_en;         -- NR 0xC5
...
o_int_en <= control_reg(7-3);
```

`o_int_en` is exported as `ctc_int_en` and is the ONLY signal
`zxnext.vhd:1949` fans into `im2_int_en`, so **both** writers reach the
IM2 fabric and the later one wins.

Section 6's `CTC-NR-03` already claimed "CTC control word D7 also sets
int_en independently", but it was only ever exercised as far as the
NR 0xC5 readback — which is sourced from `Ctc::get_int_enable()` and so
was correct all along. The seam that was NOT covered is the propagation
from `control_reg(7)` into the fabric's `dev_[CTCn].int_en`: jnext
refreshed that copy from the NR 0xC5 write handler alone, so a control
word carrying D7=1 updated the CTC's own enable while the fabric's copy
stayed stale and the channel's ZC/TO was discarded by the wrapper's
`int_en` AND. Both audio players shipped on the official SD card
(`NextSIDplayer.nex`, `nxmodplayer.nex`) write NR 0xC5 first and then
program their channels with control words carrying D7=1, so their engine
interrupts never reached the Z80 and both played silence (GH #47, #48).

These rows close that seam at the fabric, not at the readback. They live
in `test/ctc_interrupts/ctc_interrupts_test.cpp` (group CTC-CW-INTEN) and
drive the real port path (`OUT 0x183B..0x1B3B`) on a full `Emulator`,
observing `Im2Controller::state()` — the state machine is what depends on
the fabric's `int_en`, whereas `int_status` (NR 0xC8-0xCA) latches on any
edge regardless of `int_en` (`im2_peripheral.vhd:160`) and is therefore
NOT a usable observable here. All three rows fail with the fix reverted.

| ID | Test | Expected |
|----|------|----------|
| CTC-CW-INTEN-01 | NR 0xC5 ← 0x01 (CTC0 only), then ch1 control word 0xA5 (D7=1) + TC; `raise_req(CTC1)` + `tick(1)` (ctc_chan.vhd:269,276 → zxnext.vhd:1949) | CTC1 reaches S_REQ; NR 0xC5 reads back 0x03. Pre-fix: S_0 |
| CTC-CW-INTEN-02 | NR 0xC5 ← 0x0F (all four), then ch1 control word 0x25 (D7=0) + TC; `raise_req(CTC1)` + `tick(1)`. `iowr_cw` writes the WHOLE control_reg, so D7=0 disables | CTC1 stays S_0; NR 0xC5 reads back 0x0D. Pre-fix: S_REQ — a phantom interrupt from a channel software had just disabled |
| CTC-CW-INTEN-03 | NR 0xC5 ← 0x01, then ch2 control word 0xA5 (D7=1) + TC; `raise_req` on CTC0/CTC2/CTC3/CTC7 + `tick(1)` (zxnext.vhd:4067 four channels, :4093 `ctc_int_en(7:4) <= "0000"`) | CTC0 and CTC2 reach S_REQ; CTC3 and CTC7 stay S_0; NR 0xC5 reads back 0x05. Pre-fix: CTC2 stuck at S_0 |

Note on NR 0xC5 bits 7:4: `zxnext.vhd:4079` wires
`i_int_en => nr_wr_dat(3 downto 0)`, so only four bits ever reach the CTC
entity, and `:4093` ties `ctc_int_en(7 downto 4)` low. CTC4..7 are
architecturally dead — nothing can raise them (`:4092` ties
`ctc_zc_to(7 downto 4)` low too). `CTC-CW-INTEN-03` pins CTC7 as a
negative control rather than as live support.

## Special Handling

### Clock domain crossing

The `im2_peripheral` operates across two clock domains: `i_CLK_28` (28 MHz
system clock) and `i_CLK_CPU` (variable CPU clock). The `isr_serviced` signal
from `im2_device` (CPU clock domain) is edge-detected into the 28 MHz domain
via `isr_serviced_d`. Tests must verify that this domain crossing works
correctly under various CPU clock speeds (3.5 MHz, 7 MHz, 14 MHz, 28 MHz).

### CTC prescaler timing

The CTC prescaler runs on the 28 MHz clock. Prescaler = 16 means the counter
decrements every 16 system clocks (571.4 ns). Prescaler = 256 means every
256 system clocks (9.14 us). Tests must use cycle-accurate counting.

### Edge detection on write signals

CTC channel write detection uses rising-edge detection: `iowr = i_iowr AND NOT
iowr_d`. This prevents a held write signal from being treated as multiple
writes. Tests must verify this single-pulse behaviour.

### CTC read timing

The CTC data output is latched on the falling edge of CLK_CPU:
`port_ctc_dat <= ctc_do` on `falling_edge(i_CLK_CPU)`. Tests reading CTC
ports must account for this half-cycle delay.

## File Layout

```
test/
  ctc_int/
    ctc_channel_tests.cpp      # CTC channel unit tests (Sections 1-5)
    ctc_nextreg_tests.cpp      # CTC NextREG integration tests (Section 6)
    im2_control_tests.cpp      # IM2 control block tests (Section 7)
    im2_device_tests.cpp       # IM2 device state machine tests (Section 8)
    im2_daisy_chain_tests.cpp  # Daisy chain priority tests (Section 9)
    im2_pulse_tests.cpp        # Pulse interrupt mode tests (Section 10)
    im2_peripheral_tests.cpp   # IM2 peripheral wrapper tests (Section 11)
    ula_line_int_tests.cpp     # ULA/line interrupt tests (Section 12)
    nextreg_int_tests.cpp      # NextREG interrupt registers tests (Sections 13-14)
    dma_int_tests.cpp          # DMA interrupt tests (Section 15)
    unqualified_int_tests.cpp  # Unqualified interrupt tests (Section 16)
  CMakeLists.txt               # Updated: new test executables
doc/design/
  CTC-INTERRUPTS-TEST-PLAN-DESIGN.md  # This document
```

## CMake Integration

```cmake
# CTC and Interrupt Controller compliance tests
add_executable(ctc_interrupts_test
    ctc_int/ctc_channel_tests.cpp
    ctc_int/im2_control_tests.cpp
    ctc_int/im2_device_tests.cpp
    ctc_int/im2_daisy_chain_tests.cpp
    ctc_int/im2_pulse_tests.cpp
    ctc_int/im2_peripheral_tests.cpp
    ctc_int/ula_line_int_tests.cpp
    ctc_int/nextreg_int_tests.cpp
    ctc_int/dma_int_tests.cpp
    ctc_int/unqualified_int_tests.cpp
)
target_link_libraries(ctc_interrupts_test PRIVATE jnext_core)
add_test(NAME ctc_interrupts_test COMMAND ctc_interrupts_test)
```

## How to Run

```bash
# Build
cmake --build build -j$(nproc) 2>&1 | tail -5

# Run CTC/interrupt tests standalone
./build/test/ctc_interrupts_test

# Run full regression suite (includes CTC/interrupt tests)
bash test/regression.sh
```

## Summary

| Section | Tests | Focus |
|---------|------:|-------|
| 1. CTC State Machine | 13 | Channel lifecycle, state transitions |
| 2. CTC Timer Mode | 9 | Prescaler, auto-reload, ZC/TO timing (+G120) |
| 3. CTC Counter Mode | 5 | External edge counting, polarity |
| 4. CTC Chaining | 6 | ZC/TO cascade, circular topology |
| 5. CTC Control/Vector | 11 | Register protocol, write classification |
| 6. CTC NextREG Enable | 4 | NextREG 0xC5 interaction |
| 7. IM2 Control Block | 16 | RETI/RETN decode, IM mode, DMA delay (+G87×2) |
| 8. IM2 Device | 12 | Per-device state machine, vector, ACK |
| 9. IM2 Daisy Chain | 10 | Priority, blocking, chain restore |
| 10. Pulse Mode | 15 | Legacy pulse timing, mode selection (+G89×4 +G90 +G121) |
| 11. IM2 Peripheral | 10 | Edge detect, status, domain crossing (+G119) |
| 12. ULA/Line Interrupts | 9 | ULA timing, line number, priority |
| 13. NextREG Registers | 20 | All interrupt NextREGs 0xC0-0xCE (+G88×2) |
| 14. Status/Clear | 10 | Status bits, clear mechanism |
| 15. DMA Interrupt | 6 | DMA delay, NMI interaction |
| 16. Unqualified Int | 5 | Bypass enable, NextREG 0x20 |
| 17. Joystick IO Mode | 2 | CTC ch3 ZC/TO toggle |
| **Total** | **~163** | |

## Planned rows carried over from the traceability matrix (GH #196)

These rows were recorded only in `TRACEABILITY-MATRIX.md`, which is now a
generated artifact and can no longer hold a claim of its own. They are
planned and NOT implemented, so they are recorded here — the one place the
generator reads planned rows from — and the matrix emits them as `missing`,
which is what they are.

| ID | Description | VHDL file:line |
|----|-------------|----------------|
| IM2-G89-01 | LDIRX samples INT/NMI between iterations | — |
| IM2-G89-02 | LDDRX samples INT/NMI between iterations | — |
| IM2-G89-03 | LDPIRX samples INT/NMI between iterations | — |
| IM2-G89-04 | LDIRSCALE samples INT/NMI between iterations | — |
| IM2-G90-01 | 28 MHz turbo SRAM-read wait state asserts sram_wait_n | — |

## Coverage notes (moved from the traceability matrix, GH #196)

The matrix is a generated artifact now and carries no prose of its own; it
links here instead. These notes were written alongside the rows they explain.

Task 3 SKIP-reduction plan (`doc/design/TASK3-CTC-INTERRUPTS-SKIP-REDUCTION-PLAN.md`) landed 2026-04-21 Phase 0 → 5. `ctc_test.cpp` moved from `150/44/0/106` to `133/128/0/5` **as of that merge**; it runs at `132 / 132 pass / 0 fail / 0 skip` today. 17 rows migrated from `check()`/`skip()` to source-level re-home or category-merge comments. NR-C0-02 was subsequently closed by GH #84 and now passes in `atic_atac_nmi_test` ATIC-NMI-02. See `doc/testing/audits/task3-ctc-phase5.md` for the historical row-by-row rationale.
Created 2026-04-21 (commit `87fb998`) to host the 10 integration-tier re-home targets from `ctc_test.cpp` that require a full `Emulator` fixture (port 0xFF / NR 0x22 / NR 0xC0-0xCA read-path composition). Runtime: `Total:   48  Passed:   48  Failed:    0  Skipped:    0`. The suite has grown well past those original 10: the 10 rows listed below are only the ones recorded here, 16 more that it asserts are recorded in the parent `## CTC+Interrupts` table above (`ULA-INT-01..06`, `NR-C0-04`, `NR-C2-01`, `NR-C3-01`, `NR-C4-02/03`, `NR-C6-02`, `ISC-09/10`, `IM2C-G87-01/02`), and the rest are reported `unrecorded` on every run. Each entry below cross-references the CTC+Interrupts plan row.
