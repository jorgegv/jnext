# Pass-18 Independent Review — NMI + Multiface + Port-Decode + NextREG

**Reviewer branch:** `task2/verify18-nmi-mf-port-reviewer` (off audit HEAD `e9eac08`).
**Audit branch under review:** `task2/verify18-nmi-mf-port` (fix `01f7212`, report `e9eac08`).
**Verdict:** **APPROVE-WITH-NITS** — 4 audit findings verified VHDL-faithful and
discriminative, 1 new finding family added (NIT-01: missing per-port
`*_io_en` enable gates).

## Verification Protocol

For each of V18-NMP-01..04 the reviewer:

1. Read the audit's VHDL citation; confirmed line numbers + decode equations.
2. Read the audit's C++ fix; confirmed the new mask correctly implements
   the VHDL decode (mask = popcount of address lines VHDL constrains,
   value = the AND of those constrained bits).
3. Reverted `src/core/emulator.cpp` to integration HEAD `126d764` (pre-fix),
   rebuilt port_test, ran and confirmed every V18 row FAILed pre-fix; then
   restored the audit HEAD `e9eac08` and confirmed every V18 row PASSes.

## Per-Fix Verdicts

### V18-NMP-01 — Kempston mouse 12-bit decode

* **VHDL citation correct.** zxnext.vhd:2668-2670 decode `port_fadf` /
  `port_fbdf` / `port_ffdf` as `cpu_a(11 downto 8) = X"A"/X"B"/X"F"`
  AND `port_df_lsb = '1'` AND `port_mouse_io_en = '1'`. A15..A12
  are DON'T-CARE. The decode involves 12 constrained bits.
* **C++ fix correct.** `register_handler(0x0FFF, 0x0ADF, ...)` (and
  the 0x0BDF / 0x0FDF siblings). The mask `0x0FFF` selects bits
  A11..A0; the value `0x0ADF` has A11..A8 = 0xA and A7..A0 = 0xDF.
  Most-specific-wins dispatch keeps the 12-bit mouse handler winning
  over the 8-bit Specdrum-alias handler at LSB 0xDF.
* **Test discriminative.** Reverted the fix; `port_test.cpp::V18-NMP-01`
  FAILs (`canon=0x0f 2ADF=0x00 5ADF=0x00 9ADF=0x00`). Restored — PASSes.
  Note: the canonical reply 0x0F equals `(~0 & 0x07) | 0x08` per
  `KempstonMouse::read_port_fadf()` idle (wheel=0, btns=0, fixed bit3=1)
  — verified at `src/input/mouse.cpp:99-116`.

VERDICT: **VHDL-faithful + discriminative — YES.**

### V18-NMP-02 — Profi-Covox 0x003F / 0x005F LSB-only

* **VHDL citation correct.** zxnext.vhd:2661, :2664 fold these into
  `port_dac_A` / `port_dac_D` via `port_3f_lsb` / `port_5f_lsb` (set
  by the LSB decoder at :2549, :2553 when `cpu_a(7:0) = X"3F"/X"5F"`).
  A15..A8 are DON'T-CARE.
* **C++ fix correct.** Masks changed to `0x00FF / 0x003F` and
  `0x00FF / 0x005F`. The pre-existing MF+3 readback handler at LSB
  0x3F (read-only, registered earlier) coexists fine — at the same
  8-bit specificity, the dispatcher picks based on which side has a
  callback (MF readback for reads, Profi DAC for writes).
* **Test discriminative.** Reverted: `port_test.cpp::V18-NMP-02a/b` FAIL
  (`baseline_L=0xc0 aliased_L=0xc0`). Restored — PASS (aliased
  reaches `0xE0`).

VERDICT: **VHDL-faithful + discriminative — YES.**

### V18-NMP-03 — Soundrive Mode 2 0x00F1/F3/F9/FB LSB-only

* **VHDL citation correct.** zxnext.vhd:2661-2664 fold the SD2 channels
  A/B/C/D via `port_f1_lsb` / `port_f3_lsb` / `port_f9_lsb` /
  `port_fb_lsb` (LSB decoder :2534-2538). A15..A8 DON'T-CARE.
* **C++ fix correct.** All four handlers changed to mask `0x00FF`,
  values unchanged.
* **Test discriminative.** Reverted: `port_test.cpp::V18-NMP-03` FAILs.
  Restored — PASSes.

VERDICT: **VHDL-faithful + discriminative — YES.**

### V18-NMP-04 — GS Covox 0x00B3 LSB-only

* **VHDL citation correct.** zxnext.vhd:2659 — `port_dac_mono_BC <= '1'
  when port_b3_lsb = '1' and port_dac_mono_BC_b3_io_en = '1'`. The
  `port_b3_lsb` decoder is LSB-only (:2559). A15..A8 DON'T-CARE.
* **C++ fix correct.** Mask changed to `0x00FF / 0x00B3`. The handler
  fans to channels B and C via `dac_.write_channel(1/2, ...)`.
* **Test discriminative.** Reverted: `port_test.cpp::V18-NMP-04` FAILs
  (both pcm_left and pcm_right unchanged). Restored — PASSes.

VERDICT: **VHDL-faithful + discriminative — YES.**

## Test Invariants

Reviewer re-ran after the verification sandwich:

* `ctest --output-on-failure` Release: 38/38 PASS.
* `./build/test/fuse_z80_test build/test/fuse`: 1356/1356 PASS.
* `./build/test/port_test`: 91/91 PASS, 0 FAIL.

## Independent Re-Audit

Reviewer enumerated every `register_handler(...)` call in
`src/core/emulator.cpp` (52 total) and cross-referenced each against
its corresponding VHDL `port_*` decode in
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`.

### Port-decode mask sweep — exhaustive

The remaining `register_handler(0xFFFF, ...)` calls after the fix are
all at `*_3B` 16-bit ports (0x123B / 0x243B / 0x253B / 0x303B / 0x103B /
0x113B / 0x133B / 0x143B / 0x153B / 0x163B / 0xBF3B / 0xFF3B), plus
the optional magic-port at `cfg.magic_port_address`. Each is gated in
VHDL by a `port_xxxx_msb = '1'` (matches `cpu_a(15:8) = X"xx"` exactly)
AND `port_3b_lsb = '1'` (matches `cpu_a(7:0) = X"3B"` exactly) — that
is genuinely a full 16-bit decode. The audit's "verified as legitimate"
list is correct.

The UART decode (0x133B/143B/153B/163B) deserves a footnote: VHDL
`port_uart` decodes `cpu_a(15:11) = "00010"` AND `(cpu_a(10) xor
(cpu_a(9) AND cpu_a(8))) = '1'` AND `port_3b_lsb = '1'`. Walked the
four valid (A10, A9, A8) tuples and confirmed they uniquely map to
the four UART register-select values used by the C++ handlers. Each
individual handler at `mask=0xFFFF` is a single-point decode, so a
full 16-bit match is correct.

Reviewer spot-checked the partial-mask handlers as well: CTC at
`0xF8FF / 0x183B` (VHDL :2690 — A15:11 + LSB), port_p3_float at
`0xF003 / 0x0001` (A15:12 + A1:0), port_eff7 at `0xF0FF / 0xE0F7`
(A15:12 + LSB), AY at `0xC007 / 0xC005` / `0xC007 / 0x8005` /
`0xC00F / 0x8005` (A15:14 + A2/A3 + A1:0). All decode correctly.

**No additional over-strict masks found.**

### Cross-cutting families

* **Cache-leak across reset.** Multiface (`src/peripheral/multiface.cpp::reset`)
  and NmiSource (`src/peripheral/nmi_source.cpp::reset`) both load every
  FF from its VHDL reset value. NextReg reset preserves NR 0x05/0x06/0x09/
  0x0A/0x10/0x11/0x8A/0xB8-0xBB per Pass-7/8/17 audit history. Nothing
  new identified.
* **WO-NR readback default-FF.** Pass-17 V17-NMP-01 fixed NR $B8-$BB. Reviewer
  cross-checked every NR with a non-zero VHDL reset (NR $12 $13 $14 $18-$1B
  $42 $4A $4B $4C $68 $6E $6F $98-$9B $A8 $A9 $C0 $C4): each either has an
  explicit `set_read_handler` OR has a seeding write at
  `emulator.cpp:2875-2880` that primes `regs_[]`. No additional gaps.
* **NR readback reset-default semantics for NR $98-$9B / $A9.** VHDL has
  these reads return live `i_GPIO` / `i_ESP_GPIO_20` inputs, not the
  output shadow. Already correctly stubbed at `emulator.cpp:2438-2441,
  2457`. No new finding.
* **NMI request edge vs level.** Pass-17 V17-CPU-01 added the IM2
  `im2_int_req` pulse-mode latch. NmiSource models `nmi_generate_n`
  per VHDL :2164-2170 with one-cycle pulse semantics — no new bug here.
* **MF button latch / page-in/out / disable-pulse.** VHDL multiface.vhd:122-184
  implements the four FFs faithfully in `src/peripheral/multiface.cpp:88+`.
  No new finding.
* **Expansion-bus aliasing.** ExpBus device emulation remains class-(d)
  per prior passes. Not in scope for this pass.

### Missed findings — port-side IO-enable gates (NIT family)

The audit's scope explicitly includes "VHDL signal semantics" beyond
the port-decode mask. The reviewer found multiple handlers that
decode the address correctly but omit the corresponding
`port_*_io_en` gate (per `effective_internal_port_enable(NR_reg)` &
mask check) — VHDL would silence the port when the gate bit is
cleared. All gates default to '1' at reset, so the omission is
latent for default-boot paths, but it is a real divergence and
several already-fixed handlers in this same file DO honour the gate
(e.g. UART, I2C, mouse, joystick, DivMMC, SPI, Multiface). Treating
this as a single NIT cluster.

#### V18-NMP-NIT-01 — Missing `port_*_io_en` gates on six handlers

* **Sprite Slot Select / Status — port 0x303B**
  Site: `src/core/emulator.cpp:3308-3310`.
  VHDL: zxnext.vhd:2681 — `port_303b <= ... and port_sprite_io_en = '1'`,
  where `port_sprite_io_en <= internal_port_enable(14)` (NR 0x83 bit 6,
  VHDL :2423). When NR 0x83 b6 is cleared, real hardware silences the
  port; jnext still routes the write into `sprites_.write_slot_select`.
* **Sprite Attribute — port 0x57** (`emulator.cpp:3313-3315`)
* **Sprite Pattern — port 0x5B** (`emulator.cpp:3318-3320`)
  Both gated by `port_sprite_io_en` per VHDL :2679-2680.

* **Layer 2 Control — port 0x123B**
  Site: `emulator.cpp:2904-2930`.
  VHDL: zxnext.vhd:2635 — `port_123b <= ... and port_layer2_io_en = '1'`,
  `port_layer2_io_en <= internal_port_enable(15)` = NR 0x83 bit 7
  (VHDL :2424).

* **ULA+ Register Select — port 0xBF3B** (`emulator.cpp:4419-4432`)
* **ULA+ Data — port 0xFF3B** (`emulator.cpp:4433-4440`)
  Both gated by `port_ulap_io_en` = `internal_port_enable(24)` =
  NR 0x85 bit 0 (VHDL :2439, :2685-2686).

* **CTC — ports 0x183B..0x1F3B** (`emulator.cpp:4157-4163`)
  Gated by `port_ctc_io_en = internal_port_enable(27)` = NR 0x85 bit 3
  (VHDL :2442, :2690).

* **DMA — ports 0x6B and 0x0B** (`emulator.cpp:4169-4182`)
  Gated by `port_dma_6b_io_en = internal_port_enable(5)` (NR 0x82 bit 5)
  and `port_dma_0b_io_en = internal_port_enable(25)` (NR 0x85 bit 1)
  respectively, per VHDL :2405, :2440, :2643. Note: the 0x6B handler
  already does have a `dma_holds_bus()` guard but does NOT consult the
  NR 0x82 b5 IO-enable bit.

The fix shape is uniform across the cluster: each handler's read+write
prologue gains an `if ((effective_internal_port_enable(0x82|0x83|0x85)
& gate_bit) == 0) return 0xFF;` (read) / `return;` (write). The same
pattern is already used by the UART / I2C / mouse / joystick / DivMMC /
SPI handlers in this file, so this is mechanically a no-debate fix.

The bugs are latent at default boot but a software write to NR 0x82-0x85
that clears any of the listed `*_io_en` bits will diverge jnext from
hardware. Treated as a NIT (not class-b) because:

  (a) Default NR 0x83/0x85 reset values are 0xFF, so the gate stays
      open in all current boot paths.
  (b) No regression test surfaces the divergence (jnext's existing
      tests do not enumerate every gate-bit clear).
  (c) The fix is structurally identical and could be batched into a
      single follow-up patch.

Per the workflow rules in `feedback_task2_audit_thorough_per_pass.md`,
flagging as a NIT preserves the audit's substantive APPROVE; the fix
can be folded into the manager's next consolidated pass.

## Final Verdict

**APPROVE-WITH-NITS** — V18-NMP-NIT-01 (six missing `port_*_io_en` gates).
The four port-decode-mask fixes are correct, the discriminative tests
genuinely discriminate, baseline test suites still pass at 38/38 + 1356/1356
+ 91/91. The NIT documents a related but distinct family (gate-AND
omission rather than mask-too-strict) that the audit's scope footnote
arguably permits as a follow-up rather than a re-roll.

## Confidence

* Audit verification: **very high.** Reverted, re-tested, restored — direct
  evidence each fix is necessary and sufficient.
* Re-audit thoroughness: **high.** Enumerated all 52 `register_handler`
  call sites; spot-checked every partial-mask handler; cross-referenced
  every legitimate-0xFFFF call against its VHDL decode.
* Missed-finding completeness: **medium-high.** The IO-enable gate cluster
  is a real divergence; without a written regression test covering the
  gate-clear case I cannot 100% prove no further latent bug, but the
  audit's "structural family" view is well-supported.
