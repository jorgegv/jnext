# Pass-19 review report — CPU + Z80N + IM2 subsystem

Reviewer branch `task2/verify19-cpu-z80n-im2-reviewer`. Review conducted against audit HEAD `d792228` (= integration HEAD of audit work). Independent — reviewer is a different agent than the auditor.

## Verdict

**APPROVE-WITH-NITS — 1 missed class-(c) finding (V19R-CPU-01) + 0 reviewer NITs.**

All four V19-IM2-* fixes are VHDL-faithful and discriminative tests are correct (sandwich-verified). However, the audit's enumeration table missed one adjacent integration-wiring gap that is squarely within the Pass-19 scope ("integration-wiring gaps surfaced by the enumeration table mandate") and is mechanistically the same kind of bug as V19-IM2-01..04. See V19R-CPU-01 below.

Pass-19 was honest about scope — 4 effective findings, integration-wiring family, sustained ≈5–10 per pass rate. CPU/IM2 is NOT converged; Pass-20 should target the V19R-CPU-01 follow-up plus another sweep.

## Enumeration table verification

### Row count

Audit claims ~120 rows. Counted:
- Z80N opcode dispatch entries: **31** (`grep -c "case Z80NOpcode::" src/cpu/z80n_ext.cpp` = 31). Each surface (flags, MEMPTR, T-states, R-reg, etc.) gets its own row → roughly 30–80 rows.
- IM2 surfaces in `im2.cpp`/`im2.h`: ~40 rows for dev_[].int_en/int_status/int_unq/int_req setters + state-machine transitions + decoder states + DMA delay + pulse fabric.
- NR register handlers touching IM2: 11 rows (0x20/0x22/0xC0/0xC4/0xC5/0xC6/0xC8/0xC9/0xCA/0xCC/0xCD/0xCE).
- FUSE Z80 integration hooks: 6 rows.
- Init/save-state schema: 3 rows.

Tabulated total **≈120 rows** as claimed. No coverage-theatre.

### Spot-checks (10 random rows)

| Row (audit) | Spot-check | VHDL oracle re-read | Verdict |
|---|---|---|---|
| 23 TEST_N AND | `t80n_mcode.vhd:1779-1788` confirmed `IR(5:3)="100"` selects AND in `Save_ALU` block; F bits S/Z/H=1/X/Y/N=0/C=0 match standard Z80 AND r,n flag composition (FUSE z80_ed.c case 0xa6) | C++ summary matches | ✓ |
| 29 BSLA_DE_B shift | `t80n.vhd:987-993` confirms `shift_left(unsigned 16-bit, B[4:0])`; Z80N opcode 0x28 dispatches to BSLA branch; mask 0xFFFF in C++ matches 16-bit shift semantic | ✓ |
| 47 ADD_HL_NN MEMPTR | `t80n.vhd:1181-1186` LDZ writes MEMPTR-lo, LDW writes MEMPTR-hi; mcode :1872-1878 confirms LDZ/LDW assertions on ADD_HL_nn path | ✓ |
| 79 INT pulse drop | `zxnext.vhd:2017-2033` pulse_int_n returns to '1' on pulse_count_end IRREGARDLESS of IFF1 — V18R-CPU-01 fix codifies this | ✓ |
| 89 DevIdx priority order | `zxnext.vhd:1941` priority order: LINE(0), UART0_RX(1), UART1_RX(2), CTC0..7(3..10), ULA(11), UART0_TX(12), UART1_TX(13) — matches im2.h:33-43 | ✓ |
| 92 raise(Im2Level) DMA/DIVMMC/MULTIFACE | Confirmed VHDL has no daisy-chain slot for DMA (vhdl:2003-2008 — DMA is INT-victim), and DIVMMC/MULTIFACE are NMI-driven not IM2; the V18R-CPU-02 fix at im2.cpp:158-165 correctly no-ops these | ✓ |
| 99 int_status_mask_c8 | `zxnext.vhd:6247-6248` confirms `"000000" & im2_int_status(0) & im2_int_status(11)` = bit 1 LINE, bit 0 ULA; matches im2.cpp:358-363 | ✓ |
| 100 int_status_mask_c9 | `zxnext.vhd:6250-6251` confirms `im2_int_status(10 downto 3)`; CTC4..CTC7 hard-zero per zxnext.vhd:4092 | ✓ |
| 134 step_pulse pulse_count_end | `zxnext.vhd:2033` `pulse_count(5) AND (machine_timing_48 OR machine_timing_p3 OR pulse_count(2))` — matches im2.cpp:1042-1044 | ✓ |
| 156 cpu_.on_int_ack | `zxnext.vhd:1999` `im2_vector <= nr_c0_im2_vector & im2_vec & '0'` — matches im2.cpp:563-591 ack_vector composition via compute_vector() | ✓ |

All 10 spot-checks pass. **Enumeration table integrity confirmed.**

## Per-finding verification

### V19-IM2-01 — NR 0x22 bit 1 → IM2 fabric LINE int_en — **APPROVED**

**VHDL re-read**:
- `zxnext.vhd:5297` — `nr_22_line_interrupt_en <= nr_wr_dat(1);` ✓
- `zxnext.vhd:5610` — same FF written by NR 0xC4 bit 1 ✓
- `zxnext.vhd:6711` — `ula_int_en <= nr_22_line_interrupt_en & (not port_ff_interrupt_disable);` ✓
- `zxnext.vhd:1949-1950` — `im2_int_en[0] = ula_int_en(1) = nr_22_line_interrupt_en` ✓

**C++ implementation**: `src/core/emulator.cpp:1868` correctly fans `(v & 0x02) != 0` into `im2_.set_int_en(LINE, ...)`. Init at line 261 sets `LINE int_en = false` matching VHDL :4983 reset default.

**Sandwich**: Commented out the line 1868 assignment, rebuilt, ran `ctc_interrupts_test`:
- **FAIL** `ULA-INT-V19-IM2-01`: state=0 (S_0); int_line=0. Pre-fix path reproduced exactly.

Restored, re-ran: PASS. Test is discriminative.

### V19-IM2-02 — port_ff_reg(6) → IM2 fabric ULA int_en (3 writers + init) — **APPROVED**

**VHDL re-read**:
- `zxnext.vhd:3614` — reset: `port_ff_reg <= (others => '0')` ✓
- `zxnext.vhd:3616` — port-FF wr: full byte → port_ff_reg ✓
- `zxnext.vhd:3620` — nr_22_we: bit 2 → port_ff_reg(6) ✓
- `zxnext.vhd:3622` — nr_c4_we: NOT bit 0 → port_ff_reg(6) ✓
- `zxnext.vhd:3635` — `port_ff_interrupt_disable <= port_ff_reg(6);` ✓
- `zxnext.vhd:6711` — `ula_int_en(0) = NOT port_ff_interrupt_disable` ✓
- `zxnext.vhd:1949` — `im2_int_en[11] = ula_int_en(0) = ULA int_en` ✓

**C++ implementation**: 3 production writers + 1 init writer:
- init: emulator.cpp:254-255 sets `(port_ff_reg_ & 0x40) == 0` (= true at reset since port_ff_reg=0) ✓
- NR 0x22 write: emulator.cpp:1887-1888 fans after port_ff_reg_ update ✓
- NR 0xC4 write: emulator.cpp:2761-2762 fans after port_ff_reg_ update ✓
- port-FF write: emulator.cpp:3308-3309 fans after port_ff_reg_ update ✓

**Side-effect check — vsync flood**: V19-IM2-02 init sets `dev_[ULA].int_en = true` at boot. In **pulse mode** (default), the IM2 state machine is held at S_0 by `step_state_machine_with_iei` line 230 (`if (!im2_mode_) return;`) and by im2.cpp:813 `im2_reset_n = im2_mode_` (forces `im2_int_req=false` in pulse mode). V19-IM2-04's poll is gated `if (im2_.is_im2_mode() && im2_.int_line_asserted())` — so in pulse mode NO request_interrupt is invoked. No vsync flood observed; `cpu_int_pulse_test` 11/11 PASS confirms pulse mode unaffected.

**Sandwich**: Commented out all 4 ULA assignments (init + 3 writers), rebuilt, ran:
- **FAIL** `ULA-INT-V19-IM2-02`: all 3 sub-checks fail (state=0 instead of S_REQ for a) and c)).
- **FAIL** `ULA-INT-V19-IM2-02-PORTFF`: state=0 after disable+re-enable (should be S_REQ).
- **FAIL** `ULA-INT-V19-IM2-04`: state=0 (cascade effect — without ULA int_en, the int_line never asserts).

Restored, re-ran: PASS. Discriminative.

### V19-IM2-03 — int_unq one-shot semantic — **APPROVED**

**VHDL re-read**:
- `zxnext.vhd:1946-1947` — `im2_int_unq[i] <= nr_20_we and nr_wr_dat(N);` with `nr_20_we` a one-cycle write pulse. Each int_unq bit is high for EXACTLY one CLK_28 cycle.
- `im2_peripheral.vhd:167-178` — `im2_int_req` latch persists until `im2_isr_serviced` clears it; **does NOT** re-set from int_unq after it goes back to 0.

**C++ implementation**: `src/cpu/im2.cpp:90` clears `dev_[k].int_unq = false;` at end of `tick()`. The clear happens AFTER `step_pulse()` and `step_devices()` have consumed the int_unq this tick — so observable effects within the tick are preserved, only the next tick sees the cleared value.

**Side-effect check — pulse mode int_unq tests (IM2W-* + UNQ-* in ctc_test)**: 
- IM2W-08 (raise_unq + tick → S_REQ): the tick captures int_unq=true at line 837-840, advances state to S_REQ, THEN V19-IM2-03 clears. Test observes state after the tick. **PASS.**
- UNQ-04 (raise_unq bypasses int_en → S_REQ): same flow. **PASS.**
- UNQ-05 (int_unq sets int_status): raise_unq line 327-328 sets int_status=true directly; persists post-tick. **PASS.**
- ctc_test 132/132 PASS confirms no IM2W/UNQ regression.

**Sandwich**: Commented out im2.cpp:90 clear loop, rebuilt:
- **FAIL** `V19-IM2-03-INT-UNQ-ONE-SHOT-AFTER-ISR`: after full S_0→S_REQ→S_ACK→S_ISR→S_0 cycle and one extra tick, state=1 (S_REQ re-trigger) instead of S_0. Pre-fix re-trigger reproduced.

Restored, re-ran: PASS. Discriminative.

### V19-IM2-04 — int_line_asserted() drives CPU /INT in IM2 mode — **APPROVED**

**VHDL re-read**:
- `zxnext.vhd:1840` — `z80_int_n <= ((pulse_int_n AND im2_int_n) OR NOT expbus_disable_int) AND (i_BUS_INT_n OR expbus_disable_int);` Confirmed: /INT pin is AND of pulse and IM2 lines (both active-low). Either pulled low asserts /INT.
- `zxnext.vhd:1837` — `expbus_disable_int = '1' when expbus_eff_en='0' or expbus_eff_disable_io='1' or nr_c4_int_en_0_expbus='0' else '0'`. **With expbus_disable_int='1'**, the LHS becomes `(pulse_int_n AND im2_int_n) OR NOT '1'` = `pulse_int_n AND im2_int_n` (unchanged); only the bus INT (RHS) is gated. So IM2/pulse INT delivery is **independent of `nr_c4_int_en_0_expbus`** — confirms jnext correctly stores but does not gate on `im2_c4_expbus_`.

**C++ implementation**: `src/core/emulator.cpp:5675-5677` polls after `im2_.tick()` and calls `cpu_.request_interrupt(0xFE)` when `is_im2_mode() && int_line_asserted()`. The placeholder 0xFE is replaced at IntAck time by `on_int_ack()` → `ack_vector()` (init line 716-717).

**Side-effect checks**:
- **Pulse mode**: Gate `im2_.is_im2_mode()` is false → no request_interrupt fired in V19-IM2-04 path. `cpu_int_pulse_test` 11/11 PASS confirms pulse mode preserved.
- **Idempotent re-stamp**: `request_interrupt()` at z80_cpu.cpp:925-929 re-stamps `int_requested_at_` to current tstates on every call. While IM2 fabric stays in S_REQ, V19-IM2-04 re-stamps every inner-loop iteration. The pulse-expiry drop at z80_cpu.cpp:467 (`tstates - int_requested_at_ > int_pulse_tstates`) never triggers because re-stamping keeps the window fresh — correct VHDL semantic for IM2 mode where /INT is level-held while any device is in S_REQ.
- **Vector composition at IntAck**: `on_int_ack` → `ack_vector()` walks priority chain, advances S_REQ → S_ACK, returns `(nr_c0_im2_vector << 5) | (im2_vec << 1)` matching VHDL :1999. Placeholder 0xFE is overwritten.

**Sandwich**: Commented out V19-IM2-04 poll, rebuilt:
- **FAIL** `ULA-INT-V19-IM2-04`: ULA state stuck at 1 (S_REQ) after run_frame.

Restored, re-ran: PASS. Discriminative.

## Adjacent re-audit

Audit's "integration-wiring gap" family was the dominant Pass-19 theme. I swept adjacent surfaces:

### Surfaces that match audit's findings
- All `dev_[].int_en` writers: enumeration covers LINE (V19-IM2-01), ULA (V19-IM2-02), CTC0..7 via NR 0xC5 set_int_en_c5 (row 104), UART0/1 RX/TX via NR 0xC6 set_int_en_c6 (row 105). ✓
- All `int_status_clear` writers: NR 0xC8/C9/CA cover LINE/ULA/CTC0-7/UART × {TX,RX} via emulator.cpp:2810-2848. Bit-positions verified against VHDL :1952-1955 — all 14 paths correctly mapped. ✓
- `expbus_disable_int` (NR 0xC4 bit 7): correctly NOT used to gate IM2/pulse INT in jnext (consistent with VHDL :1840 analysis above). ✓
- DMA int_en (NR 0xCC/CD/CE): emulator.cpp:2859-2887 recompose 14-bit mask via `compose_im2_dma_int_en()` → `set_dma_int_en_mask`. Wave E NMI-activated path also wired (line 2866). ✓
- DevIdx priority order (zxnext.vhd:1941) and `to_devidx` mapping: confirmed correct after V18R-CPU-02 (DMA/DIVMMC/MULTIFACE no-op'd in legacy raise). ✓

### Surfaces NOT in audit's enumeration — MISSED FINDING

#### V19R-CPU-01 — class-(c) — IM2-mode peripheral int_req level-vs-pulse mismatch

**Scope**: This bug is directly within Pass-19's stated enumeration scope ("one row per IM2 source"). Multiple peripherals fall into this gap (ULA, LINE, CTC0-3, UART0/1 RX/TX); the audit's enumeration treats `raise_req()` as a level setter but does NOT enumerate its absence-of-clear in production code paths.

**VHDL reference**: `zxnext.vhd:1941` — `im2_int_req <= uart1_tx_empty & uart0_tx_empty & ula_int_pulse & ctc_zc_to & ... & line_int_pulse;`. For ULA, LINE, CTC the sources are **one-cycle pulses** (e.g., `ula_int_pulse` from `zxula_timing.vhd:551-559` is a 1-cycle pulse at the INT raster position). The wrapper edge-detect at `im2_peripheral.vhd:90-101` (`int_req <= i_int_req AND NOT int_req_d`) captures the rising edge on each pulse.

**C++ implementation**: `src/cpu/im2.cpp:307-309` `raise_req()` sets `dev_[i].int_req = true;` as a level. Production peripherals (ULA scheduler at `emulator.cpp:5390`, LINE at `:6600`, CTC `on_interrupt` at `:4620`, UART `on_*_interrupt` at `:4665`/`:4683`) call `raise_req()` on each event. **NONE of them call `clear_req()`** — no production code does. After the first edge, `int_req_d` settles to `true`; subsequent `raise_req()` calls produce no new edge (`edge = int_req && !int_req_d = true && !true = false`).

**Discriminative observation** (reproduced via standalone test compiled against `libjnext_cpu.a`):
```
Frame 1 after raise+tick:    state=1 (S_REQ)     ← first edge fires
Frame 1 after full ISR:      state=0 (S_0)       ← latch cleared by RETI
Frame 2 after raise+tick:    state=0 (NOT S_REQ) ← no new edge → no new latch
Frame 2 int_line_asserted=0                       ← /INT not asserted
```

VHDL fires every frame; jnext fires once-per-emulator-lifetime per device.

**Why missed by Pass-19 tests**: `ULA-INT-V19-IM2-04` runs ONE `run_frame()`. It validates the first INT cycle but cannot observe the second frame. Audit row 93 marks raise_req `✓` with comment "per-device level input" — this is the documented model but it diverges from VHDL pulse semantics in production usage.

**Suggested fix shape** (class-(c) — NOT required this pass; user authorization needed):
- Option A: have `raise_req()` synthesize a one-tick pulse — set `int_req=true` then auto-clear at end of next `tick()`, mirroring the VHDL 1-cycle pulse semantic.
- Option B: have peripherals pair `raise_req()` with deferred `clear_req()` (e.g., scheduler also schedules a clear N cycles later).
- Option C: in `tick()`, auto-clear `int_req` after `int_req_d` has settled (i.e., reset `int_req` once edge has been propagated).

Option A is most VHDL-faithful and contained — single-line change in `raise_req()` plus a clear-list scan at end of tick. Tests would need a 2-frame ULA INT discriminative regression to lock the fix.

**Severity / impact**: Real boot impact in NextZXOS IM2-mode game loops that rely on FRAME interrupts to schedule sprite/audio updates. Boot path may use pulse mode pre-IM2-switch so impact varies by program. Suggest user authorize as Pass-20 follow-up.

### Negative-result sweeps (no further missed findings)

- NR 0x69 — bit-mask 0xC0 in port_ff_reg update; does NOT touch bit 6 → no ULA int_en fan-out needed. ✓ correctly omitted.
- NR 0xC2/C3 — stackless NMI RETN address; not IM2 fabric. ✓
- NR 0x06/0x05 — no IM2 fabric bits. ✓
- `step_pulse()` `pulse_count_end` int_unq clear (im2.cpp:1053) becomes redundant with V19-IM2-03 but harmless (the audit notes this). ✓
- Vector composition: `compute_vector()` at im2.cpp:1077 = `(base<<5) | (idx<<1)`; matches VHDL :1999 byte composition. ✓
- IM2 stackless NMI (set_stackless_nmi, store-only): pre-existing class-(d) acknowledged at audit row 108. ✓
- DD/FD prefix walk for Z80N opcodes: pre-existing class-(d) V15-CPU-NIT-01. ✓

## Side-effect inspection summary

| Concern | Outcome |
|---|---|
| Pulse-mode regression from V19-IM2-04 poll | None — gate `im2_.is_im2_mode()` excludes pulse mode. `cpu_int_pulse_test` 11/11 PASS. |
| Vsync flood from V19-IM2-02 init ULA int_en=true | None — fabric state machine is held at S_0 in pulse mode (im2.cpp:230, :813). Polling gate covers IM2 mode. |
| IM2W-* / UNQ-* regression from V19-IM2-03 int_unq clear | None — int_unq is consumed by step_pulse + step_devices within the same tick; cleared AFTER use. ctc_test 132/132 PASS. |
| V19-IM2-04 re-stamping pulse-T expiry | Intentional — pulse-T drop never fires while IM2 device stays in S_REQ; matches VHDL `im2_int_n` level-hold semantic. |
| Vector idempotency (request_interrupt(0xFE) every tick) | OK — `on_int_ack` always replaces 0xFE with `ack_vector()` result; 0xFE never escapes. |

## Test invariants — final

| Suite | Result |
|---|---|
| `ctest --test-dir build` | 38/38 PASS |
| `./build/test/fuse_z80_test build/test/fuse` | 1356/1356 PASS (non-negotiable) |
| `bash test/00regression/regression.sh` | 33/0/0 PASS |
| `./build/test/cpu_int_pulse_test` | 11/11 PASS |
| `./build/test/cpu_z80n_im2_regressions_test` | 44/44 PASS (includes V19-IM2-03) |
| `./build/test/ctc_interrupts_test` | 27/27 PASS (includes V19-IM2-01/02/02-PORTFF/04) |
| `./build/test/ctc_test` | 132/132 PASS (IM2W-*, UNQ-*) |

## Final commit

This review will be committed as the last commit on `task2/verify19-cpu-z80n-im2-reviewer` (off audit HEAD `d792228`):
`review(task2-verify19-cpu-z80n-im2): independent review — APPROVE-WITH-NITS`

## Confidence statement

**High confidence** in the APPROVE side: each of V19-IM2-01..04 is VHDL-faithful, sandwich-verified, and side-effect-clean. The 4 integration-wiring fixes resolve the documented bugs without introducing regressions.

**High confidence** in the NIT side: V19R-CPU-01 is a real, reproducible, in-scope adjacent finding that the audit's enumeration table did not surface. The level-vs-pulse model mismatch is mechanistically the same family as V19-IM2-01..04 (integration-wiring), so it should have been caught by the same enumeration-table sweep. Class-(c) classification is appropriate because: (a) impact is observable but only on multi-frame IM2 program flows; (b) fix is structural (touches raise_req() semantics); (c) boot path may use pulse mode for most of cold start.

CPU/Z80N/IM2 is **NOT converged**. Pass-20 should: (1) take V19R-CPU-01 fix; (2) add a multi-frame IM2 discriminative regression; (3) sweep again with the enumeration-table mandate, paying explicit attention to peripheral→fabric input-signal lifecycle (level vs pulse vs edge).
