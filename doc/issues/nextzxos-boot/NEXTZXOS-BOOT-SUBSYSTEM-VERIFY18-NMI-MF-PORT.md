# Pass-18 Verify-Audit Report — NMI + Multiface + Port-Decode + NextREG

**Branch:** `task2/verify18-nmi-mf-port` (off integration HEAD `126d764`).
**Audit mode:** BLIND — VHDL oracle as ground truth, no consultation of
prior NEXTZXOS-BOOT-SUBSYSTEM-ANALYSIS docs during the audit sweep.
**Subsystem scope:** `src/peripheral/multiface.{h,cpp}`,
`src/peripheral/nmi_source.{h,cpp}`, port dispatch in
`src/core/emulator.cpp` (port-decode handlers + IO observers + NR
write/read handlers), `src/port/nextreg.{h,cpp}`,
`src/port/port_dispatch.{h,cpp}`.
**Build/test profile:** Release (`cmake -B build -DCMAKE_BUILD_TYPE=Release`).

## Trend

Pass-17 closed three class-(b) findings in this subsystem (NR $B8-$BB
readback defaults, port_FE LSB-only decode, port_FF LSB-only decode).
Pass-18 sweeps the remaining VHDL-vs-C++ port-decode mask discrepancies
and finds **four** more — all the same family: handlers registered with
`mask = 0xFFFF` (full 16-bit address compare) where VHDL constrains
only a subset of address lines. The pattern is structurally identical
to Pass-17 V17-NMP-02 / V17-NMP-03; the four below were missed in
that pass.

## Findings

### V18-NMP-01 — Kempston mouse ports decoded by 12 bits, not 16

**Class:** (b) — VHDL-faithful fix + discriminative regression test
landed in same commit.

**Site:** `src/core/emulator.cpp:4344-4361` (pre-fix).

**Pre-fix behaviour:** Three handlers registered with `mask=0xFFFF`,
`value=0xFADF` / `0xFBDF` / `0xFFDF` respectively. Required the FULL
16-bit address to match exactly; software using e.g.
`IN A,(0x2ADF)` would miss the mouse buttons decode in jnext.

**VHDL spec (`zxnext.vhd:2668-2670`):**
```vhdl
port_fadf <= '1' when cpu_a(11 downto 8) = X"A" and port_df_lsb = '1' and port_mouse_io_en = '1' else '0';
port_fbdf <= '1' when cpu_a(11 downto 8) = X"B" and port_df_lsb = '1' and port_mouse_io_en = '1' else '0';
port_ffdf <= '1' when cpu_a(11 downto 8) = X"F" and port_df_lsb = '1' and port_mouse_io_en = '1' else '0';
```
Bits A11..A8 must equal A / B / F (per port); A7..A0 must equal 0xDF
(per `port_df_lsb` decode at zxnext.vhd:2530). **A15..A12 are
DON'T-CARE.** Mask must therefore be `0x0FFF`, not `0xFFFF`.

**Fix:** Change masks to `0x0FFF / 0x0ADF`, `0x0FFF / 0x0BDF`,
`0x0FFF / 0x0FDF`. Most-specific-wins dispatch keeps the mouse
handlers (12 bits) winning over the broader Specdrum-alias handler
(`0x00FF / 0x00DF`, 8 bits) for the A/B/F-MSB cases — exactly
matching VHDL's parallel `port_fadf` / `port_fbdf` / `port_ffdf`
decodes.

**Discriminative test:** `test/port/port_test.cpp` row **V18-NMP-01**
probes the mouse buttons port via the canonical address (0xFADF) and
three different A15..A12 aliases (0x2ADF, 0x5ADF, 0x9ADF). All four
must return the same non-zero buttons-port byte. Pre-fix the three
aliased reads return 0x00 (the Specdrum-alias handler's mouse-
enabled gate), distinct from the canonical 0x0F (idle wheel + bit3
+ ~buttons[2:0]); post-fix all four return 0x0F.

**Test result:** FAIL pre-fix (canon=0x0f, aliases=0x00). PASS post-fix.

### V18-NMP-02 — Profi-Covox DAC ports 0x003F / 0x005F decoded by LSB only

**Class:** (b).

**Site:** `src/core/emulator.cpp:3470-3482` (pre-fix).

**Pre-fix behaviour:** Two write handlers registered with `mask=0xFFFF`,
`value=0x003F` (Profi DAC channel A) and `value=0x005F` (Profi DAC
channel D). Required full 16-bit address match; software using
`OUT (0x123F), A` would miss the DAC channel A write in jnext.

**VHDL spec (`zxnext.vhd:2661, :2664`):**
```vhdl
port_dac_A <= '1' when ... or (port_3f_lsb = '1' and port_dac_stereo_AD_3f5f_io_en = '1');
port_dac_D <= '1' when ... or (port_5f_lsb = '1' and port_dac_stereo_AD_3f5f_io_en = '1');
```
`port_3f_lsb` / `port_5f_lsb` decode is LSB-only (zxnext.vhd:2549, 2553).
A15..A8 are DON'T-CARE. Mask must be `0x00FF`.

**Fix:** Change masks to `0x00FF / 0x003F` and `0x00FF / 0x005F`. The
MF+3 readback handler at the same `0x00FF / 0x003F` already exists
(read-only); equal specificity is fine because the dispatcher
picks based on handler presence (MF readback for reads, DAC for writes).

**Discriminative test:** Rows **V18-NMP-02a** / **V18-NMP-02b**. Enable
DAC (NR 0x08 b3) and DAC gates (NR 0x84 b3 = 1). Write a baseline
value to 0x003F / 0x005F, capture `pcm_left()` / `pcm_right()`. Write
a different value to the aliased 0x123F / 0x125F. The pcm output
must change (post-fix); pre-fix it stays at the baseline.

**Test result:** FAIL pre-fix (baseline = aliased = 0xC0). PASS post-fix.

### V18-NMP-03 — Soundrive Mode 2 DAC ports 0x00F1/F3/F9/FB decoded by LSB only

**Class:** (b).

**Site:** `src/core/emulator.cpp:3490-3522` (pre-fix).

**Pre-fix behaviour:** Four write handlers at `mask=0xFFFF`, values
0x00F1 / 0x00F3 / 0x00F9 / 0x00FB. Required full 16-bit match.

**VHDL spec (`zxnext.vhd:2661-2664`):** Same pattern as V18-NMP-02 —
`port_f1_lsb` / `port_f3_lsb` / `port_f9_lsb` / `port_fb_lsb` are
LSB-only (zxnext.vhd:2534-2538). Mask must be `0x00FF`.

**Fix:** Change masks to `0x00FF` for all four; values unchanged.

**Discriminative test:** Row **V18-NMP-03**. Enable DAC + NR 0x84 b2
(SD2 gate). Baseline write to canonical 0x00F1, capture pcm_left();
write to aliased 0x12F1, verify pcm_left() changed.

**Test result:** FAIL pre-fix. PASS post-fix.

### V18-NMP-04 — GS Covox port 0xB3 decoded by LSB only

**Class:** (b).

**Site:** `src/core/emulator.cpp:3529-3537` (pre-fix).

**Pre-fix behaviour:** Write handler at `mask=0xFFFF`, `value=0x00B3`.

**VHDL spec (`zxnext.vhd:2659`):**
```vhdl
port_dac_mono_BC <= '1' when port_b3_lsb = '1' and port_dac_mono_BC_b3_io_en = '1' else '0';
```
`port_b3_lsb` is LSB-only (zxnext.vhd:2559). Mask must be `0x00FF`.

**Fix:** Change mask to `0x00FF / 0x00B3`. Mono-fans to channels B and C.

**Discriminative test:** Row **V18-NMP-04**. Enable DAC + NR 0x84 b6
(GS Covox gate). Baseline write to canonical 0x00B3, capture
`pcm_left()` / `pcm_right()`. Write to aliased 0x12B3, verify both
channels changed.

**Test result:** FAIL pre-fix. PASS post-fix.

## Test Invariants Held Post-Fix

* `ctest --output-on-failure` from `build/`: **38/38 PASS** (Release).
* `./build/test/fuse_z80_test build/test/fuse`: **1356/1356 PASS**.
* `bash test/00regression/regression.sh`: 32 PASS / 1 FAIL — the
  remaining `parallax-demo` failure is a pre-existing screenshot
  divergence unrelated to V18 changes (verified by re-running the
  regression on the pre-fix tree: same 1 FAIL).

## Class-(d) Architectural Items

None identified in this pass. The Multiface VHDL state machine, the
NmiSource priority arbiter, and the NR readback paths are all
structurally faithful at the level the unit-tier rig can observe.
Two pre-existing items from earlier passes (Stackless NMI — NR 0xC0
b3, ExpansionBus device emulation — `i_BUS_NMI_n`) remain class-(d)
deferred per the original plan.

## Convergence Status

This pass adds 4 effective findings (all class-(b)) to the NMI / MF /
Port-decode / NextREG subsystem. The bugs are all in the same family
as Pass-17 V17-NMP-02 / V17-NMP-03 (over-strict port-decode masks) —
a sweep through every `register_handler(0xFFFF, ...)` registration
matched against the VHDL `port_*` decoders surfaced these four
remaining over-constrained masks.

Remaining `mask=0xFFFF` registrations after this pass that I verified
**are** legitimately full-16-bit decodes per VHDL:
- 0x123B (Layer2), 0x243B (NR select), 0x253B (NR data), 0x303B
  (Sprite), 0xBF3B / 0xFF3B (ULA+), 0x103B / 0x113B (I2C),
  0x133B / 0x143B (UART), 0x153B / 0x163B (RTC). All require both
  `port_xxxx_msb` and `port_3b_lsb` decode — full 16-bit match
  matches VHDL.

Subsystem **not yet converged** — but the remaining gaps (if any) are
likely in deeper VHDL signal semantics rather than port-mask
arithmetic.
