# NextZXOS Boot — Pass-12 Subsystem Audit (NMI + Multiface + Port + NextREG)

**Date:** 2026-05-10
**Branch:** `task2/verify12-nmi-mf-port` (off integration HEAD `df247c8`)
**Worktree:** `.claude/worktrees/task2-verify12-nmi-mf-port`
**Auditor mandate:** blind audit (no reading prior pass reports), VHDL as
oracle (`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`),
no probes, no push, stay within scope.

## Subsystem scope

- `src/peripheral/nmi_source.{cpp,h}` — central NMI arbiter
- `src/peripheral/multiface.{cpp,h}` — Multiface FSM
- `src/port/nextreg.{cpp,h}` — NR register file + dispatch + read masks
- `src/port/port_dispatch.{cpp,h}` — port matching/mask/precedence
- `src/core/emulator.cpp` slice for NMI/MF/Port-owned NRs (NMI control,
  $C0, $C2, $C3, $C4, $C5, $C8, $CC, $D8, $D9, $D0, $00, $01, $02, $06)
- Cross to `src/input/mouse.{cpp,h}` and `src/input/joystick.{cpp,h}` ONLY
  where they feed NR-readable state
- Hotkey handlers (drive button → soft NMI, hard NMI, Multiface NMI)

## Methodology

The audit walked the full register file via VHDL `port_253b_dat <=` read
mux entries (zxnext.vhd:5878-6300) and the corresponding `when X"NN" =>`
write decoder (zxnext.vhd:5113-5660) cross-referencing the C++ write/read
handlers and the cache hygiene of `regs_[]` for every NR in scope. The
multiface.vhd FSM was diff'd line-by-line against `Multiface::clock_edge_`.
Port dispatch was scanned for one-hot decode mismatches.

## Findings

### Class (a) — fixable in this commit, with regression test

#### V12-NMP-01 — NR 0xC4 b0 ULA-INT-disable shadow fan-out missing

**Location:** `src/core/emulator.cpp` NR 0xC4 write_handler (line ~2385)

**VHDL oracle (zxnext.vhd):**
- :3621-3622 — `nr_c4_we` writes `port_ff_reg(6) <= NOT nr_wr_dat(0)`
- :3635 — `port_ff_interrupt_disable <= port_ff_reg(6)` (combinational tie)
- :6711 — `ula_int_en <= nr_22_line_interrupt_en & (NOT port_ff_interrupt_disable)`
- :6750 — fed into the ULA-INT comparator as `i_inten_ula_n`
- :6239 — NR 0xC4 read mux: `nr_c4_int_en_0_expbus & "00000" & ula_int_en`
  (so bit 0 = `NOT port_ff_interrupt_disable`)

**Bug shape:**

The C++ NR 0xC4 write_handler updates `port_ff_reg_` bit 6 correctly
(matching :3622) but does NOT mirror the fan-out into the
`ula_int_disabled_` shadow nor call
`video_timing_.set_interrupt_enable(...)`. The companion NR 0x22
write_handler at emulator.cpp:1657-1658 DOES update both shadows; the
NR 0xC4 path failed to keep parity even though both NRs feed the same
`port_ff_reg(6)` flip-flop in VHDL.

The NR 0xC4 read_handler (emulator.cpp:2417) reads
`!ula_int_disabled_` for the bit-0 readback. With the shadow stuck at
its prior state, reads were inconsistent with the live `port_ff_reg(6)`
the write actually committed. Worse, `ula_int_disabled_` is consulted by
`Emulator::run_frame()` at line 4840 for ULA-INT scheduling — software
that toggles ULA-INT enable via NR 0xC4 (not NR 0x22) had no effect on
the scheduler.

**Discriminative test scenario:**

1. Write NR 0x22 ← 0x04 (sets `port_ff_reg_(6)=1` AND
   `ula_int_disabled_=true` via the working NR 0x22 fan-out). Read NR 0xC4
   bit 0 = 0 (disabled).
2. Write NR 0xC4 ← 0x01 (b0=1 ⇒ `port_ff_reg_(6)<=0`). Pre-fix the C++
   leaves `ula_int_disabled_=true` (NR 0xC4 didn't touch it). Read NR 0xC4
   bit 0 = `!true = 0` — STALE. Post-fix the new fan-out syncs
   `ula_int_disabled_=false`, so bit 0 = `!false = 1`.
3. Write NR 0xC4 ← 0x00 (b0=0 ⇒ `port_ff_reg_(6)<=1`). Post-fix syncs
   `ula_int_disabled_=true`, so bit 0 = 0.

The pre-fix NR 0xC4 read returns 0x80 in step 2 (expbus stays 0 because
NR 0x22 didn't touch im2_c4_expbus_; the value depends on prior state);
post-fix returns the VHDL-canonical bit-0 transition.

**Fix applied:**

Added two lines to NR 0xC4 write_handler (after the existing
`renderer_.ula().set_screen_mode(port_ff_reg_)` call):

```cpp
// V12-NMP-01: VHDL :3635 + :6711 — port_ff_reg(6) is the canonical
// store for ula_int_disable; both NR 0x22 b2 (:3620) AND NR 0xC4 b0
// (NOT, :3622) feed the same flip-flop. NR 0x22 writes update
// `ula_int_disabled_` + `video_timing_.set_interrupt_enable()`;
// NR 0xC4 must do the same.
ula_int_disabled_ = (port_ff_reg_ & 0x40) != 0;
video_timing_.set_interrupt_enable(!ula_int_disabled_);
```

**Test added:** `test/nextreg/nextreg_integration_test.cpp` —
`V12-NMP-01` row in the `TestCov-NMI-MF-Port` group (~line 4730).
The discriminative shape uses NR 0x22 to seed
`ula_int_disabled_=true`, then asserts that NR 0xC4 = 0x01 / 0x00
correctly toggle the readback bit 0.

**Collateral test fix:** `test/ctc_interrupts/ctc_interrupts_test.cpp`
row `NR-C4-03` had been written assuming the buggy behaviour: it
expected NR 0xC4 readback = 0x83 after a write of NR 0xC4 = 0x82.
Per VHDL the b0=0 in the write means `port_ff_reg(6) <= 1` →
`ula_int_en(0) = 0` → readback bit 0 = 0 → readback = 0x82, NOT 0x83.
The pre-fix C++ accidentally returned 0x83 because `ula_int_disabled_`
stayed at its default false. The test expectation has been corrected
to the VHDL-faithful 0x82 with a comment block explaining the
divergence. This is a coverage-theatre ("test was wrong, code was
buggy in the same direction") cleanup — V12-NMP-01 closes that hole
on the production side AND aligns the test with the spec.

**Class:** (a) — fixable, with same-commit regression test.

#### V12-NMP-02 — port-0xFF write missing ULA-INT-disable shadow fan-out (fix-of-reviewer)

**Location:** `src/core/emulator.cpp` port-0xFF write handler (line ~2914)

**Reviewer NIT:** the independent reviewer at
`task2/verify12-nmi-mf-port-reviewer` returned APPROVE-WITH-NITS noting
that port-0xFF is the *third* writer to `port_ff_reg(6)` per VHDL
`:3614-3616`. V12-NMP-01 closed the NR 0xC4 b0 fan-out and NR 0x22 b2
already had it, but the direct port-0xFF write at emulator.cpp:2914
still wrote `port_ff_reg_` without mirroring the change into
`ula_int_disabled_` / `video_timing_.set_interrupt_enable(...)`.

**VHDL oracle (zxnext.vhd):**
- :3614-3616 — `port_ff_wr` branch latches the entire CPU byte
  (including bit 6) into `port_ff_reg`.
- :3619-3620 — `nr_22_we` partial fan-out into `port_ff_reg(6)`.
- :3621-3622 — `nr_c4_we` partial fan-out into `port_ff_reg(6)` (NOT
  bit 0).
- :3635 — `port_ff_interrupt_disable <= port_ff_reg(6)` (combinational).
- :6711 — `ula_int_en <= ... & (NOT port_ff_interrupt_disable)`.

All three writers feed the same flip-flop; jnext's `ula_int_disabled_`
+ `video_timing_` shadow must be updated by all three.

**Bug shape (pre-V12-NMP-02):**

`OUT (0xFF),0x40` correctly stored `port_ff_reg_(6)=1`, but the C++
scheduler's `ula_int_disabled_` shadow stayed at its previous state and
`video_timing_.set_interrupt_enable(...)` was never called. As a
result:

- NR 0xC4 read bit 0 returned the stale shadow instead of the live
  `port_ff_reg(6)` state (read handler at emulator.cpp:2429-2437
  consults `ula_int_disabled_`).
- The ULA-INT scheduler gate at `Emulator::run_frame()` line ~1989
  consulted the stale shadow, so software disabling the ULA interrupt
  via direct port-0xFF write had no effect on interrupt scheduling.

This was a previously documented "latent gap" in
`test/ctc_interrupts/ctc_interrupts_test.cpp` ULA-INT-02; the gap is now
closed.

**Fix applied:**

In the port-0xFF write handler at `src/core/emulator.cpp:2914+`, mirror
the NR 0x22 / NR 0xC4 pattern after the existing `port_ff_reg_ = val`
update:

```cpp
ula_int_disabled_ = (port_ff_reg_ & 0x40) != 0;
video_timing_.set_interrupt_enable(!ula_int_disabled_);
```

**Tests added:** `test/ctc_interrupts/ctc_interrupts_test.cpp` —
`ULA-INT-V12-NMP-02` and `ULA-INT-V12-NMP-02b` rows in the
`ULA-Integration` group:

- `ULA-INT-V12-NMP-02` exercises the readback path: fresh state ⇒ NR
  0xC4 bit 0 = 1; `OUT (0xFF),0x40` ⇒ NR 0xC4 bit 0 = 0; `OUT (0xFF),0x00`
  ⇒ NR 0xC4 bit 0 = 1.
- `ULA-INT-V12-NMP-02b` exercises the scheduler path:
  `OUT (0xFF),0x40` then `run_frame()` must leave NR 0xC8 bit 0 (ULA
  status) clear — same observable shape as ULA-INT-02 but driven via
  the direct port-0xFF write rather than the NR-22 mirror.

**Discriminative pre-revert verification:** with the V12-NMP-02 fix
reverted, both `ULA-INT-V12-NMP-02` and `ULA-INT-V12-NMP-02b` FAIL
(stale shadow returns bit 0 = 1 in all three steps; NR 0xC8 bit 0 still
fires). With the fix restored, both PASS.

**Stale-comment cleanup:** the ULA-INT-02 commentary documenting the
"DIRECT `OUT 0xFF` TO DISABLE ... latent subsystem gap" was rewritten
into a V12-NMP-02 closure note pointing at the new rows. The
`emulator.cpp:1690-1692` historical note is a fix-rationale comment for
the G56-cluster-C cache-leak fix (NR 0x22 read), not a known-limitation
marker, so it remains as-is.

**Class:** (a) — fixable, with same-commit regression test (reviewer
NIT closure).

### Class (b) — none.

### Class (c) — none.

### Class (d) — architectural escalations

**None new** in this pass. The 4 outstanding class-(d) items from prior
audits (memory half-cycle ×2, DivMMC SPI cycle FSM, NMI Stackless NMI,
CPU IM2 controller bridge) are cited in the aggregate report; no
additional architectural-scope issues surfaced this pass.

## Areas explicitly verified safe (no finding)

- **NR 0x02 read mux (zxnext.vhd:5891)** — bit 7 (bus_reset), bit 4 (iotrap),
  bits 3:2 (mf/divmmc gen), bits 1:0 (reset_type). C++ composes
  authoritatively (emulator.cpp:1838). Bits 6:5 forced 0.
- **NR 0x03 read mux (:5894)** — palette_sub_idx not modelled (bit 7 = 0);
  composed from authoritative state. Machine-type / timing / dt_lock
  preserved across reset per VHDL `nr_03_*` initial-only signals.
- **NR 0x05 (:5897)** — joy0/joy1 from authoritative Joystick; 5060/scandouble
  from cached(0x05) (with Pentagon-mask covered by Pass-10).
- **NR 0x06 (:5900)** — bit 2 (ps2_mode) gated on config_mode (Pass-11
  V11-NMP-03); other bits fan out to NmiSource / EmuFnKeys / mixer
  correctly.
- **NR 0x0A (:5912)** — bits 7:6 (mf_type) gated on config_mode (Pass-11
  V11-NMP-02); bit 2 forced 0 in read.
- **NR 0x10 (:5924)** — coreid gated on config_mode (Issue 2/3 path);
  flashboot bit not in read mux; SPKEY_BUTTONS modelled as 0 (idle).
- **NR 0x11 (:5927)** — config_mode-gated write; "111" → "000" rule;
  Issue 2 bit-0-only mask.
- **NR 0x80 (:6123)** — full byte storage; bits 7/4 fan out to NmiSource
  (Pass-9 V9 fix).
- **NR 0x81 (:6126)** — bits 1:0 hardwired "00" on write per :5496;
  bit 7 = i_BUS_ROMCS_n (idle '1'); bit 5 fans out to NmiSource (Pass-11
  V11-NMP-01 mask 0x78).
- **NR 0x82-0x85 (:6129-6138)** — port-enable register file; NR 0x85
  reads bits 6:4 forced "000" (handler at :2277 returns
  `cached(0x85) & 0x8F`); reset-type-gated reset semantics correct
  (Pass-5 fix).
- **NR 0x86-0x89 (:6141-6150)** — bus-port enable, same shape as 0x82-0x85,
  inverse polarity gate (Pass-5 fix).
- **NR 0x8A (:6153)** — bits 7:6 forced "00" (write handler returns
  `v & 0x3F`).
- **NR 0xC0 (:6230)** — composed authoritatively; bit 4 forced '0'; bits
  2:1 read from Z80's IM mode register.
- **NR 0xC2/C3 (:6233-6236)** — software writes stored verbatim in regs_[];
  NMIACK-LSB/MSB capture goes through `set_nmi_return_address` which
  writes regs_ directly.
- **NR 0xC4 (:6239)** — composed read; bits 6:2 forced "00000". V12-NMP-01
  fixes bit-0 fan-out (above).
- **NR 0xC5 (:6242)** — `ctc_int_en` 8-bit verbatim; CTC peripheral owns
  the storage.
- **NR 0xC6 (:6245)** — bits 7,3 forced 0 (mask 0x77 in
  `nr_c6_uart_int_en_`).
- **NR 0xC8/C9/CA (:6247-6254)** — pack masks correctly mirror VHDL
  index orderings; UART RX bit duplication preserved (im2.cpp:316-364).
- **NR 0xCC/CD/CE (:6257-6263)** — DMA delay enable bytes; mask 0x83
  / 0xFF / 0x77 respectively.
- **NR 0xD8/D9/DA (:6266-6272)** — IO-trap state; bit 7:1 of D8 forced 0;
  cause bits 7:2 forced 0; NR 0x02 b4 cleared cascade (emulator.cpp:1768).
- **Multiface FSM (multiface.vhd:122-184)** — port_io_dly, button_pulse,
  invisible, mf_enable, fetch_66 all modelled correctly. MF1 / MF128 /
  MF+3 mode decode (:105-118) correct. Lockout/unlock sequences
  (port_mf_*_rd/wr behaviour per mode) match VHDL clock-edge logic.
- **NMI source priority (zxnext.vhd:2089-2113)** — MF > DivMMC > ExpBus
  arbitration with `port_e3_reg(7) = !divmmc_conmem` and
  `divmmc_nmi_hold` gating on the MF latch; `mf_is_active` gating on
  the DivMMC latch. config_mode force-clear of priority latches but
  NOT the NR 0x02 readback latches (Verify1 fix kept).
- **nr_02_reset_type FSM (:1732-1739)** — saturating shift-with-OR
  (100 → 010 → 001 → 001) implemented correctly in `strobe_soft_reset()`.
- **PortDispatch most-specific-mask-wins** — read fall-through to
  next-most-specific handler when best has no `read` callback (models
  VHDL's exclusive one-hot decode where some ports are write-only).
- **NR 0x00/0x0E/0x0F** — read-only registers, write-handler guard at
  nextreg.cpp:441-443 silently drops writes for NR 0x01, 0x0E, 0x0F.

## Build & test results

- **CMake configure:** OK (Release, ENABLE_QT_UI=ON).
- **Build:** clean (no warnings, no errors).
- **ctest:** 38/38 passed (after V12-NMP-01 fix + NR-C4-03 expectation
  correction). Pre-fix: 37/38 (NR-C4-03 in ctc_interrupts_test failing
  because the test was theatre-coverage with an incorrect expectation).
- **FUSE Z80 opcode tests:** 1356/1356 passed.
- **NextREG integration test:** 234/234 passed (V12-NMP-01 row green;
  the discriminative scenario works end-to-end through the port path).

## Aggregate counts

| Class | Count |
|-------|-------|
| (a)   | 2     |
| (b)   | 0     |
| (c)   | 0     |
| (d)   | 0     |
| **Total** | **2** |

V12-NMP-01 was found by the audit; V12-NMP-02 was found by the
independent reviewer (APPROVE-WITH-NITS) and resolved as a
fix-of-reviewer cycle. Both findings extend the same "shadow-store
fan-out" family — port_ff_reg(6) has three writers in VHDL and all
three must mirror into ula_int_disabled_ + video_timing_.

## Convergence note

Pass-12 found two class-(a) findings in the "shadow-store fan-out"
family. V12-NMP-01 closed the NR 0xC4 b0 fan-out (audit). V12-NMP-02
closed the direct port-0xFF write fan-out (reviewer NIT). All three
writers to `port_ff_reg(6)` now keep `ula_int_disabled_` /
`video_timing_.set_interrupt_enable(...)` in sync, matching the
combinational `port_ff_interrupt_disable <= port_ff_reg(6)` tie at
VHDL :3635. Symmetric pattern across NR 0x22 / NR 0xC4 / port-0xFF.
No architectural-scope issues surfaced.

## Build & test results (post V12-NMP-02)

- **CMake configure:** OK (Release, ENABLE_QT_UI=ON).
- **Build:** clean.
- **ctest:** 38/38 passed.
- **FUSE Z80 opcode tests:** 1356/1356 passed.
- **ctc_interrupts_test:** 23/23 passed (was 21; 2 new V12-NMP-02 rows).
- **Discriminative revert verification:** both new rows FAIL with
  the V12-NMP-02 fix reverted; both PASS with it restored.
