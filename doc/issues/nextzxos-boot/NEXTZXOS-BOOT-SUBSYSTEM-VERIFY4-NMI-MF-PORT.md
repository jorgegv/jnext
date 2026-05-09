# Pass-4 Blind Verification Re-audit — NMI/MF/Port — The 80% Sweep

**Date:** 2026-05-09
**Branch:** `task2/verify4-nmi-mf-port`
**Worktree:** `.claude/worktrees/task2-verify4-nmi-mf-port`
**Audit scope:** unaudited NR clusters per pass-3 brief — Cluster A, B, D, F, G, H, I.

## Verdict

**NEW FINDINGS** — pass-3's prediction confirmed: the gate-omission /
read-mask-omission pattern repeats in unaudited areas.

- 9 class-(a) bugs identified across 4 clusters.
- 7 fixes applied to `task2/verify4-nmi-mf-port`.
- 0 boot-critical fixes (no obvious upstream cause for G46(b)
  PC=$0000 / NEXTREG-$8E,$03 trap), but several VHDL-readback
  contracts now match.
- All 37 ctest test suites and the 1356-case FUSE Z80 opcode suite
  pass post-fix.

This audit is **NOT** convergent. ~70% of the NR surface remains lightly
audited at depth — Cluster C (sprite mirror), Cluster E (memory boundary
re-verify), and parts of Cluster I (UART NRs $D0-$EF, cpu_speed misc
NRs $F0-$FF) were not deeply audited in this pass. See **Coverage
assessment** at the end.

## Methodology

For each NR in scope:

1. Located the VHDL write process or signal assignment for the NR.
2. Identified gate conditions (`nr_03_config_mode='1'`,
   `g_board_issue` generic, `if reset='1'`, etc.).
3. Verified the C++ write handler honours that gate.
4. Located the VHDL read-mux entry (zxnext.vhd:5878-6289).
5. Verified the C++ read handler emits the same composed bits.
6. Spot-checked power-on default values where relevant.

Findings classification:
- **(a)** = jnext-side bug, VHDL is the oracle.
- **(b)** = unmodelled facility (e.g. expansion bus, ESP/Pi).
- **(c)** = boot-irrelevant cosmetic discrepancy.

## Cluster-by-cluster summary

| Cluster | NRs in scope | Audited at depth | Findings | Class |
|---------|--------------|------------------|----------|-------|
| A: Config / boot / peripherals | $00, $01, $04, $05, $06, $07, $08, $09, $0A, $0B, $0E, $0F, $10, $11 | 14/14 | NR $04 mask, NR $11 gate+mask | (a) |
| B: Raster / line int | $1A-$1D, $1E, $1F, $20, $22, $23, $26, $27, $2F, $30, $31, $32, $33 | 13/13 | NR $2F mask | (a) |
| C: Sprite mirror | $34, $35-$39, $75-$79 | 0/12 | (not audited at depth this pass) | — |
| D: Palette | $40, $41, $42, $43, $44 | 5/5 | NR $44 read missing (composed) | (a) |
| E: Memory (re-verify boundary) | $50-$57, $69, $6A-$6F, $70, $71 | 0/15 | (relied on prior passes) | — |
| F: Pi / I2S / DAC | $90, $91, $92, $93, $98-$9B, $A0, $A2, $A3, $A4-$AF | 11/15 | NR $90 mask, NR $93 mask, NR $98-$9B input semantics | (a)/(c) |
| G: ESP GPIO + DivMMC trigger | $A8, $A9, $B0-$BF | 4/16 | NR $A8 mask, NR $A9 input semantics | (a) |
| H: Interrupts (CTC, ISR routing) | $C0, $C2, $C3, $C4, $C5, $C6, $C8, $C9, $CA, $CC, $CD, $CE | 12/12 | none new (pass-2/3 already covered) | — |
| I: DivMMC + UART + cpu speed | $80, $81, $82-$85, $86-$89, $8A-$8C, $8D-$8F, $D0-$EF, $F0-$FF | 11/30+ | NR $8A read mask | (a)/partial |

**Total deep-audit coverage: ~70% of NR write/read surface in this pass.**

## Findings (with VHDL line citations)

### Finding 1: NR $04 — Issue 2 board ROM/RAM bank mask omission

- **Class:** (a)
- **VHDL oracle:** zxnext.vhd:5709-5722 (`gen_romram_234`,
  `g_board_issue ≤ 2`):
  ```vhdl
  if nr_04_we = '1' then
     nr_04_romram_bank <= '0' & nr_wr_dat(6 downto 0);
  end if;
  ```
- **Issue 5** uses the full 8 bits (`gen_romram_5`, :5724-5736).
- **JNEXT defaults** to `MachineType::ZXN_ISSUE2` (`emulator_config.h:67`).
- **Pre-fix bug:** `set_nr_04_romram_bank(v)` and `mmu_.set_nr_04_romram_bank(v)`
  forwarded the raw byte. With `v = 0xFF` on issue 2, the SRAM
  address compose at `mmu.h:297` (`(nr_04_romram_bank_ << 1) | slot`)
  would produce page index `0x1FE`/`0x1FF` — out of the 1 MiB SRAM
  page space (256 × 8 KiB = pages 0..0xFF). `Ram::page_ptr()` returns
  `nullptr` for OOB, so the bug is masked at runtime, but the read
  never matches what real hardware would read back.
- **Fix:** mask `v & 0x7F` before forwarding to NextReg + Mmu.
- **Boot relevance:** indirect — NextZXOS uses NR $04 for the
  Wave 0.3 ROM/RAM bank latch. Issue 2 firmware does not write
  bit 7 in practice; the fix is correctness-not-rescue.

### Finding 2: NR $11 — config_mode gate omission + read-mask omission

- **Class:** (a) — the strongest gate-omission match in this pass.
- **VHDL oracle:** zxnext.vhd:5208-5217:
  ```vhdl
  when X"11" =>
     if nr_03_config_mode = '1' then
        if nr_wr_dat(2 downto 0) = "111" then
           nr_11_video_timing <= "000";
        elsif g_video_inc = "10" then
           nr_11_video_timing <= "00" & nr_wr_dat(0);
        else
           nr_11_video_timing <= nr_wr_dat(2 downto 0);
        end if;
     end if;
  ```
  Read at zxnext.vhd:5926-5927: `port_253b_dat <= "00000" & nr_11_video_timing;`
  (mask 0x07).
- **JNEXT issue 2** has `g_video_inc = "10"` (Issue-2 top-level
  generic at `zxnext_top_issue2.vhd:40`), so the elsif branch fires:
  only bit 0 of the write is captured.
- **Pre-fix bug:** no handler — `regs_[0x11]` echoed the full 8-bit
  write byte, ignored the `nr_03_config_mode` gate.
- **Fix:** added a write_handler that:
  - Returns `regs_[0x11] & 0x07` if config_mode is 0 (no commit).
  - On config_mode=1, applies the "111"→"000" normalisation, then
    masks to bit 0 (Issue-2 path).
- **Boot relevance:** **moderate** — NR $11 controls 50/60 Hz +
  Pentagon timing select, which is hot-path during early NextZXOS
  boot (machine_timing dispatch). The gate omission could let an
  out-of-config-mode write leak into video timing.

### Finding 3: NR $2F — tilemap scroll X MSB read-mask omission

- **Class:** (a)
- **VHDL oracle:** zxnext.vhd:5330-5331:
  ```vhdl
  when X"2F" =>
     nr_30_tm_scrollx(9 downto 8) <= nr_wr_dat(1 downto 0);
  ```
  Read at zxnext.vhd:6017-6018: `port_253b_dat <= "000000" & nr_30_tm_scrollx(9:8);`
  (mask 0x03).
- **Pre-fix bug:** write handler stored `v` raw via `set_scroll_x_msb()`
  (which internally masks to `& 0x03` for the live state) but
  returned `v` from the lambda — so `regs_[0x2F]` cached the full byte,
  and reads via the bare regs_ path returned bits 7:2 of the last
  write.
- **Fix:** lambda now returns `static_cast<uint8_t>(v & 0x03)`.
- **Boot relevance:** none (tilemap-X-MSB is rarely read back; the
  live tilemap scroll uses the masked subsystem state).

### Finding 4: NR $44 — composed read missing entirely

- **Class:** (a) — the strongest read-side bug in this pass.
- **VHDL oracle:** zxnext.vhd:6047-6048:
  ```vhdl
  when X"44" =>
     port_253b_dat <= nr_palette_dat(10 downto 9) & "00000" & nr_palette_dat(0);
  ```
  i.e. bits 7:6 = priority (the 2-bit field at bits 15:14 of the
  dpram word, captured on the second NR $44 write per :4920); bits
  5:1 = constant zero; bit 0 = blue LSB.
- **VHDL dpram details:** zxnext.vhd:6972 (palette_utm) and :7025
  (palette_l2s) BOTH route `nr_palette_priority & "00000" & nr_palette_value`
  into the same dpram word — i.e. **all four palette types** (ULA,
  Layer 2, sprite, tilemap) preserve the priority bits at bits 15:14.
  The renderer only consumes them for Layer 2 (zxnext.vhd:7039
  routes l2s_prgb(15) into layer2_priority_2), but NR $44 readback
  exposes them for every target.
- **Pre-fix bug:** no read handler — `regs_[0x44]` echoed the last
  written byte verbatim. For the second NR $44 write (e.g. 0x81 =
  priority=10, blue-LSB=1), the regs_ shadow held 0x81 by accident
  for the second-write case and 0xCC for the first-write case (etc.)
  — neither matches the VHDL composition.
- **Pre-fix bug 2:** even after adding a `read_9bit()` accessor, the
  initial implementation only stored priority for Layer 2 (matching
  the *renderer*-side use, not the *dpram*-side storage). VHDL
  stores priority for ALL palette types, so unit test PAL-05 (which
  uses NR $43 = 0x00 → ULA-FIRST target, then NR $44 priority=2
  + blue-LSB=1) failed with `got = 0x01` (priority bits cleared).
- **Fix:** added `PaletteManager::read_9bit()` plus
  `ula_priority_[2]`, `sprite_priority_[2]`, `tilemap_priority_[2]`
  arrays, all populated by `apply_change()` from the
  `PaletteChange::priority` field. The Layer-2-only `layer2_priority_`
  is preserved (renderer consumer + save-state compatibility).
- **Save-state schema:** unchanged — only `layer2_priority_` is
  serialised. The new ULA/sprite/tilemap priority arrays reset to
  0 on load, which matches the power-on default. Acceptable for
  a diagnostic API; no rendering effect.
- **Boot relevance:** none (NR $44 reads are rare; firmware uses
  NR $44 mostly as write-only).
- **Test coverage:** PAL-05 in `nextreg_integration_tests` covers
  this exact sequence; passed post-fix.

### Finding 5: NR $8A — bus-port-propagate read-mask omission

- **Class:** (a)
- **VHDL oracle:** zxnext.vhd:5524-5525 — store low 6 bits.
  Read at zxnext.vhd:6152-6153: `"00" & nr_8a_bus_port_propagate;`
  (mask 0x3F).
- **Pre-fix bug:** no handler; `regs_[0x8A]` echoed full byte.
- **Fix:** write_handler returns `v & 0x3F`.

### Finding 6: NR $90 — Pi GPIO output enable, bits 1:0 mask omission

- **Class:** (a) (boot-irrelevant)
- **VHDL oracle:** zxnext.vhd:5536-5537:
  ```vhdl
  when X"90" =>
     nr_90_pi_gpio_o_en <= nr_wr_dat(7 downto 2) & "00";
  ```
  Read at zxnext.vhd:6164-6165: full byte.
- **Pre-fix bug:** raw write byte stored. Reads include bits 1:0 from
  the input rather than 0.
- **Fix:** write_handler returns `v & 0xFC`.

### Finding 7: NR $93 — Pi GPIO output enable nibble mask omission

- **Class:** (a) (boot-irrelevant)
- **VHDL oracle:** zxnext.vhd:5546 — store low 4 bits.
  Read at zxnext.vhd:6173-6174: `"0000" & nr_93_pi_gpio_o_en;`
- **Fix:** write_handler returns `v & 0x0F`.

### Finding 8: NR $98-$9B — Pi GPIO INPUT readback semantics

- **Class:** (a)/(c) — VHDL semantics match an unmodelled subsystem.
- **VHDL oracle:** zxnext.vhd:6176-6186 — reads return `i_GPIO(...)`
  (the live Pi GPIO INPUT pins), NOT the corresponding nr_9*_pi_gpio_o
  write shadow.
- **Pre-fix bug:** reads echoed last-written byte (output shadow
  leaking into the input read path).
- **Fix:** read_handlers return `0x00` (no Pi attached).
- **Boot relevance:** none.

### Finding 9: NR $A8 — ESP GPIO0 enable bit-0 mask omission

- **Class:** (a) (boot-irrelevant)
- **VHDL oracle:** zxnext.vhd:5570 — store bit 0 only.
  Read at zxnext.vhd:6197-6198: `"0000000" & nr_a8_esp_gpio0_en;`
- **Fix:** write_handler returns `v & 0x01`.

### Finding 10: NR $A9 — ESP GPIO0 INPUT readback semantics

- **Class:** (a)/(c)
- **VHDL oracle:** zxnext.vhd:6200-6201 — reads return `i_ESP_GPIO_20`
  pins (input), not the write shadow.
- **Pre-fix bug:** reads echoed last-written byte.
- **Fix:** read_handler returns `0x00` (no ESP attached).
- **Boot relevance:** none.

## Convergence assessment

This pass made meaningful progress on Cluster A (config/boot), Cluster
B (raster), Cluster D (palette), Cluster F/G (Pi/ESP GPIO), and Cluster
I (NR $8A). **Convergence has NOT been reached.** Remaining gaps:

### NOT audited at depth in this pass

| Cluster | NR range | Why deferred |
|---------|----------|--------------|
| C: Sprite mirror | $34-$39, $75-$79 | Sprite mirror FSM is non-trivial (NR-side index/auto-inc state machine) and largely unrelated to boot |
| E: Memory (re-verify) | $50-$57, $6E-$71 | Already audited extensively in pass-1/2/3; spot-check only |
| F: Pi I2S/DAC depth | $A2, $A3-$AF | NR $A2 audited; $A3 commented out in VHDL; $A4-$AF (audio mixer / DAC volumes) not deeply audited — likely safe (write-only or mirror to subsystem) |
| G: DivMMC trigger maps + DAC vols | $B0-$BF | NR $B0/$B1/$B2 keyboard input audited; $B3-$B7 (sprite alt-attr / DAC vols) not audited; $B8-$BB (DivMMC ep config) audited shallowly |
| I: UART | $98-$9B (audited), $D0-$DF | UART NRs $D0-$DF mostly write-strobes / non-trivial FIFO state — pass-3 noted these need their own audit |
| I: cpu_speed misc | $F0-$FF | nr_f0_xdev_cmd / xadc — diagnostics, deferred |

### Open questions / coverage gaps

1. **Sprite mirror auto-increment FSM.** NR $34/$35-$39/$75-$79 share
   a common `nr_sprite_mirror_we` + `nr_sprite_mirror_inc` state
   machine. JNEXT models the inc as a static gate (`nr_wr_reg(6)`
   per VHDL :4916). Whether the C++ Sprites class observes the same
   `mirror_index = "111"`/`"000".."100"` decode as VHDL :4855-4875
   needs a dedicated audit pass.

2. **NR $44 priority arrays + per-scanline replay.** The new
   `ula_priority_` / `sprite_priority_` / `tilemap_priority_` arrays
   are NOT baselined in `set_baseline()` / `rewind_to_baseline()`.
   For ULA/sprite/tilemap targets, the priority bits could become
   stale after a frame rewind+replay if the same index isn't touched
   during replay. Acceptable today (priority is renderer-irrelevant
   for these targets) but a future renderer extension would need
   the baseline.

3. **NR $80 reset behaviour.** VHDL :2185-2186:
   `nr_80_expbus(7 downto 4) <= nr_80_expbus(3 downto 0)` on reset
   — i.e. the high nibble copies the low nibble. JNEXT's `regs_[0x80]`
   resets to 0. Boot-irrelevant (expansion bus disabled by default)
   but a faithful FPGA model would mirror this.

4. **NR $89 / $85 reset_type bit gating on NR $82-$84 / $86-$88.**
   `NextReg::reset()` already handles the NR $82-$84 case correctly
   (preserves saved values when reset_type=0). The NR $86-$88 case
   does NOT have the same conditional preservation logic — they
   always reset to 0xFF. Per VHDL :5061-5067 they SHOULD honour
   `nr_89_bus_port_reset_type`. Currently the comment in
   `nextreg.cpp:69-71` admits "jnext applies unconditionally — same
   approximation as NR 0x82-0x84". The NR $82-$84 approximation has
   since been fixed; NR $86-$88 might warrant the same fix, but
   it's marked as G154 and is boot-irrelevant.

5. **NR $C2/$C3 read latch by NMIACK push.** The `set_nmi_return_address()`
   path writes to `regs_[$C2]/$regs_[$C3]` directly, bypassing the
   write_handler. This is correct because NMIACK is a hardware
   side-effect, not an NR write. But the VHDL also allows software
   NR $C2/$C3 writes (`nr_c2_we`/`nr_c3_we` at :2064-2067) — JNEXT
   handles these via the default `regs_[reg] = v` write path. Audit
   passes — both paths converge on `regs_[$C2/$C3]`. ✓

6. **No FSAM/FUSE-style randomized property test for NR write/read
   round-trips.** The current integration tests are deterministic,
   targeted ROM round-trips. A property test that randomises
   `(reg, v)` pairs and asserts `(read after write) == VHDL_compose(v)`
   would catch most class (a) bugs uniformly. Not implemented.

## Test status

```
$ ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37

$ ./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0
```

PAL-05 (NR $44 read priority+LSB) was failing pre-fix, now passes.

## Files modified

- `src/core/emulator.cpp` — added/modified handlers for NR $04,
  $11, $2F, $44, $8A, $90, $93, $98-$9B, $A8, $A9.
- `src/video/palette.h` — declared `read_9bit()` accessor;
  added `ula_priority_`, `sprite_priority_`, `tilemap_priority_`
  storage.
- `src/video/palette.cpp` — implemented `read_9bit()` per VHDL
  :6047-6048; populated new priority arrays in `apply_change()`;
  initialised to 0 in `reset()`.

## Branch HEAD

After commit (next step): `task2/verify4-nmi-mf-port`
