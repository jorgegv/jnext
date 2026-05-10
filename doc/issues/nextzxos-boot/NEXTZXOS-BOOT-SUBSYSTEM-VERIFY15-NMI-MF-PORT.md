# NextZXOS Boot Subsystem — Pass-15 Verify Audit (NMI + Multiface + Port + NextREG)

- **Pass:** 15
- **Branch:** `task2/verify15-nmi-mf-port`
- **Worktree:** `.claude/worktrees/task2-verify15-nmi-mf-port`
- **Integration HEAD at start:** `a86c671`
- **Methodology:** Blind audit (no prior pass-N reports read).
- **VHDL oracle:** `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
- **Build mode:** Release (`-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`).

## Summary

Two findings, both class-(a) write-only NR readback cache leaks. Pattern
identical to the G149 / V14-NMP-03 / V14-NMP-04 family already closed in
prior passes — the gap is that two more NRs (0x63 Copper data, 0xFF ULA+
palette poke) were missed by those earlier sweeps. Fixes land in the same
commit as the discriminative WO-INT-* regression rows (revert-confirmed:
each row fails on revert with the exact wrote-bytes-back signature).

| ID | NR | Class | Status |
| --- | --- | --- | --- |
| V15-NMP-01 | 0x63 | (a) | Fixed + tested |
| V15-NMP-02 | 0xFF | (a) | Fixed + tested |

No class-(b), -(c), or -(d) escalations.

## Audit angles exercised

1. **Cache-leak family (every VHDL `if <gate>='1' then <signal> <= dat`)**:
   walked every NR write-decoder branch (zxnext.vhd:4838-4910 first decoder +
   :5113-5872 second decoder) cross-referenced against the read mux
   (`case nr_register` at :5878-6289). Diff produced **13 writeable-but-not-readable
   NRs** (0x04, 0x29, 0x2A, 0x2B, 0x35-0x39, 0x60, 0x63, 0x75, 0xFF). Of
   these, all but 0x63 and 0xFF are already handled in prior passes (G149
   for 0x04/0x29/0x35-0x39/0x60; loop at line 1507 for 0x75-0x79 returns 0;
   V14-NMP-03 for 0x2B; V14-NMP-04 for 0x2A). The two new finds are 0x63
   and 0xFF.

2. **NR multi-writer fan-out (every flip-flop with 2+ writers)**: spot-
   checked
   - `port_ff_reg`: 4 writers (port-0xFF, NR 0x69, NR 0x22, NR 0xC4) — all
     correctly fanned in C++ to `port_ff_reg_` and `ula_int_disabled_`.
   - `port_e3_reg(6)`: 2 writers (port-0xE3 OR-latch, NR 0x09 bit 3 clear)
     — both correctly handled (NR 0x09 → `divmmc_.clear_mapram()`).
   - `port_7ffd_reg(3)`: 2 writers (port-0x7FFD, NR 0x69 bit 6) — both
     correctly handled.
   - `port_123b_layer2_en`: 2 writers (port-0x123B, NR 0x69 bit 7) —
     both correctly handled (`mmu_.set_l2_enable`).
   - `nr_22_line_interrupt_en`: 2 writers (NR 0x22 bit 1, NR 0xC4 bit 1)
     — both correctly handled (`video_timing_.set_line_interrupt_enable`).
   No fan-out asymmetry detected.

3. **NR readback masks (byte-exact match per VHDL)**: verified the
   three composed-readbacks NR 0x06 (`& 0xFB | ps2`), NR 0x09 (`& 0xE7 | tie`),
   NR 0x0B (`& 0xB1`), NR 0x85/0x89 (`& 0x8F`), NR 0x07 (composed), NR 0x10
   (composed) all pack per VHDL formula. NR 0xC0 readback bit-by-bit
   matches `vector(2:0) & '0' & stackless & z80_im_mode(1:0) & pulse_0_im2_1`.

4. **WO (write-only) registers**: full enumeration above — two new finds.

5. **Multiface FSM completeness**: cross-walked multiface.vhd:122-184
   (port_io_dly, nmi_active, invisible, mf_enable processes) against C++
   `clock_edge_()`. Reset values, button_pulse semantics, fetch_66 gate,
   port-strobe priority cascade, invisible_eff = invisible AND NOT mode_48,
   mf_enable_eff = mf_enable OR fetch_66, mf_port_en gate (mode_128 OR
   mode_p3 AND NOT invisible_eff AND port_mf_enable_rd) — all correct.
   Cross-model differences (mf_type "00"/"01"/"10"/"11" → enable_io 0x3F/
   0xBF/0x9F/0x9F, disable_io 0xBF/0x3F/0x1F/0x1F per VHDL :2612-2613)
   correctly decoded by the port observer at emulator.cpp:369-393.

6. **NMI source priority** when multiple fire same cycle: VHDL :2107-
   2113 elsif chain (MF > DivMMC > ExpBus). C++ `recompute_()` enforces
   equivalence via three serial `if` blocks each guarded on `!nmi_mf_`/
   `!nmi_divmmc_` updated within the same call. Functionally equivalent
   to the elsif chain. Gate masks match: MF `port_e3_reg(7)` AND
   `divmmc_nmi_hold` (= `!divmmc_conmem_ && !divmmc_nmi_hold_`); DivMMC
   `mf_is_active`; ExpBus none.

7. **Hotkey edge-vs-level + multi-button**: F4 soft-reset gates on
   `!nr_03_config_mode` (VHDL :6370). F1 hard-reset has no gate. F9 has
   no gate (raw VHDL :6348). F10 gates on `port_divmmc_io_en` (VHDL :6349).
   All match.

8. **NR 0x02 bit 7 (bus_reset)**: latched verbatim on every NR 0x02 write
   per VHDL :5119; surfaces on NR 0x02 readback bit 7 per :5891. C++
   `nr_02_bus_reset_` at emulator.cpp:1862. No reset clause anywhere in
   VHDL, so the latch survives reset — C++ `nr_02_bus_reset_` initial
   `false` matches VHDL ':= '0'`, and reset() does NOT clear it.
   Correctly preserved.

9. **NR 0x05 / 0x06 / 0x00 / 0x01 ↔ NR 0x03 machine_type dependencies**:
   NR 0x06 bit 2 (ps2_mode) gated on config_mode (VHDL :5167) — correctly
   masked by V11-NMP-03 fix at line 3544-3548. NR 0x0A bits 7:5 gated on
   config_mode (VHDL :5191-5198) — correctly masked at line 1053-1057.
   NR 0x10 bits 4:0 (coreid) gated on config_mode (VHDL :5682) — correctly
   gated at line 1234-1235. NR 0x11 video_timing gated on config_mode
   (VHDL :5209) — handled at the NR 0x11 write_handler.

10. **Mouse/joystick mode select transitions**: out of focus scope (drives
    Joystick + Mouse subsystems, audited in those passes). Briefly cross-
    checked — NR 0x05/0x0B/0x0A bit 3 fan-outs to those subsystems are
    in place.

11. **NR 0xFF data port** when no NR is selected: NR write goes through
    NextReg::write(reg=selected_, val); reads return regs_[selected_] via
    NextReg::read. The "selected" register is whatever was last written
    via 0x243B. No "no NR selected" state — `selected_` initialises to
    0x24 (per VHDL :4594). Behaviour is byte-exact with VHDL.

12. **Floating-bus on unmatched port**: PortDispatch::set_default_read
    installed at emulator.cpp:341 returns `floating_bus_read()`. VHDL
    behaviour for ports not in `port_internal_response` (line 2696) is
    floating-bus — matches.

13. **Port dispatch precedence tie-breaks**: most-specific-mask wins via
    `bits > best_bits` (strict-greater, so ties favour first-registered).
    No reachable tie in current handler set; equal-mask handlers would
    be a programmer error. Class-(c) defense-in-depth concern, not
    promoted to a finding.

## Findings

### V15-NMP-01 — NR 0x63 (Copper data byte) write leaks through cache to read

**Class:** (a) — single-NR write-only readback divergence. Spec-faithful
fix is a one-line return-value change with discriminative regression test.

**VHDL oracle:**
- `zxnext.vhd:4887` — `when X"63" => nr_copper_we <= '1';` (write-strobe
  enabled for the Copper-data branch).
- `zxnext.vhd:5433-5439` — write-side process: latches `nr_copper_data_stored`
  when `nr_copper_addr(0)='0'` and unconditionally advances `nr_copper_addr`.
  No flip-flop visible to the NR-read mux.
- `zxnext.vhd:5878-6289` — the `case nr_register is` read mux has **no
  `when X"63"` branch**.
- `zxnext.vhd:6286-6287` — the unmapped fall-through is
  `when others => port_253b_dat <= (others => '0');`

Therefore real hardware: a read of NR 0x63 (after any write) returns
**0x00**, not the byte that was written.

**Pre-fix C++ behaviour** (`src/core/emulator.cpp:1634`):
```cpp
nextreg_.set_write_handler(0x63, [this](uint8_t v) -> uint8_t {
    copper_.write_reg_0x63(v); return v;   // ← cache leaks the byte
});
```
`NextReg::write()` stores the handler's return value in `regs_[0x63]`;
`NextReg::read()` falls through to `regs_[]` since no read handler is
installed — so a subsequent NR 0x63 read returns the last-written byte
instead of 0x00.

**Fix:** return 0 from the write handler so the cache canonicalises to
0x00, mirroring the existing G149 / V14-NMP-03 / V14-NMP-04 pattern.
The `copper_.write_reg_0x63(v)` side-effect (which actually drives the
Copper data path) is preserved.

**Regression test:** `WO-INT-63` (`test/nextreg/nextreg_integration_test.cpp`,
group `WO-Integration`). Writes 0x55, reads, asserts == 0x00.
Revert-confirmed: failure signature `wrote=0x55 got=0x55 want=0x00`.

### V15-NMP-02 — NR 0xFF (ULA+ palette poke) write leaks through cache to read

**Class:** (a) — same shape as V15-NMP-01.

**VHDL oracle:**
- `zxnext.vhd:4906` — `when X"FF" => nr_ff_we <= '1';`
- `zxnext.vhd:4919` — `nr_palette_value <= (nr_wr_dat & ...) when ... or
  nr_ff_we = '1' else ...;` (composes the 9-bit palette poke).
- `zxnext.vhd:6957-6958` — `nr_ulatm_we` and `nr_palette_index_utm` route
  the byte to the ULA-TM palette dpram.
- `zxnext.vhd:5878-6289` — read mux has **no `when X"FF"` branch**.
- `zxnext.vhd:6286-6287` — same `when others => (others => '0')` fall-
  through.

Therefore real hardware reads of NR 0xFF return 0x00.

**Pre-fix C++ behaviour** (`src/core/emulator.cpp:910-917`):
```cpp
nextreg_.set_write_handler(0xFF, [this](uint8_t v) -> uint8_t {
    const bool bank_second = (palette_.read_control() & 0x40) != 0;
    const uint8_t bf3b_index = renderer_.ula().get_ulap_index();
    palette_.nr_ff_poke(bank_second, bf3b_index, v);
    return v;   // ← cache leaks the byte
});
```

**Fix:** return 0 from the write handler. The `palette_.nr_ff_poke()`
side-effect remains intact.

**Regression test:** `WO-INT-FF` (same group). Writes 0xA5, reads, asserts
== 0x00. Revert-confirmed: `wrote=0xA5 got=0xA5 want=0x00`.

## Tests

- `ctest --test-dir build`: **38/38 pass** (was 38/38 before; new rows
  added inside `nextreg_integration_tests`, group went from 6→8 in
  `WO-Integration`).
- `./build/test/fuse_z80_test build/test/fuse`: **1356/1356 pass**.
- `WO-Integration` group: **8/8 pass** (was 6/6 before V15).

## Class-(d) escalations

None. Both findings are tactical write-only readback fixes with no
architectural implications.

## Convergence assessment for this subsystem

After 14 prior passes plus this one, the subsystem still produces
new class-(a) findings on a focused angle (write-only NR readback
canonicalisation). Two more NRs (0x63, 0xFF) joined the existing G149 +
V14-NMP-03 + V14-NMP-04 family this pass; the writeable-but-not-readable
NR set is now exhausted (verified by full diff: only 0x63 and 0xFF
remained un-canonicalised; both are now fixed).

Convergence on the WO family **for this subsystem** is now achieved at
the byte-level: the diff
`writeable_NRs - readable_NRs - {already_handled}` is empty.

Recommend Pass-16 audit angles outside the WO family, e.g. dynamic-state
read divergence (effective vs shadow signals), reset-clause symmetry,
and Multiface FSM corner cases (simultaneous port strobes, mode-change
mid-FSM-state, retn_seen during fetch_66 pulse).
