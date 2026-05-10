# Pass-13 Blind Audit Report — NMI + Multiface + Port + NextREG Subsystem

**Branch**: `task2/verify13-nmi-mf-port`
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify13-nmi-mf-port`
**Off integration HEAD**: `adcc752`
**Auditor**: Pass-13 blind audit agent (no prior pass reports read)
**Mandate**: ULTRATHINK; honest convergence; no probes; subsystem scope.

## Summary

| Metric | Count |
| --- | --- |
| Findings | 1 |
| Class-(a) | 0 |
| Class-(b) | 1 |
| Class-(c) | 0 |
| Class-(d) | 0 |

The Pass-13 sweep targeted previously-unexplored angles in the
NMI/MF/Port/NextREG subsystem, with deliberate emphasis on
**Pentagon-mode-gated state-cache canonicalisation** (a parallel of
the V11-NMP-02/03 `config_mode`-gate cluster), exhaustive multi-writer
fan-out (parallel of the V12-NMP-01/02 `port_ff_reg(6)` fan-out
family), readback-mux byte-fidelity, FSM priority/edge semantics, and
hotkey debounce.

One class-(b) gap was found and fixed in the same commit chain:
**V13-NMP-01** — NR 0x05 bit 2 (`nr_05_5060`) cache leakage when
Pentagon mode is engaged. VHDL forces the underlying FF to '0' every
clock while Pentagon timing is active, but the C++ cache previously
stored the raw write byte verbatim, leaking through the read path the
moment Pentagon mode was later exited. Fix is the canonical
"V11-NMP-02 / V11-NMP-03 shape", applied at both the gated-write edge
and the gating-state-change edge.

A discriminative regression test was added in the same commit and
revert-checked (FAILs without the fix, PASSes with it).

All other audit angles closed with no findings: the multi-writer
`port_ff_reg(6)` family is fully canonicalised post V12-NMP-01/02, the
NR readback masks are byte-exact for every NR with a defined read mux
entry (NR 0x01/0x0E/0x0F RO-guard, NR 0x82/85/86/89 packing, NR 0x07
composed read, NR 0xC0 bit-4 forced-zero, NR 0xC4 ULA/LINE/expbus
composition, NR 0xCC/CE bits 6:2 / 7 / 3 forced-zero, NR 0xD8 bit-7:1
forced-zero, NR 0xDA bits 7:2 forced-zero, NR 0x06 bit-2 ps2_mode read
mask, NR 0x0A bit-2 forced-zero, NR 0x09 bits 4 / 3 read masks, NR
0x05 Pentagon read mask), the NMI source-priority/FSM model is
faithful (MF > DivMMC > ExpBus, config_mode latches force-clear, FSM
IDLE→FETCH→HOLD→END→IDLE with VHDL-correct hold gate per producer),
the Multiface 4-FF state machine matches `multiface.vhd` 1:1, and the
joystick mode-select decode + Kempston/Pentagon/Sinclair fan-out
match `zxnext.vhd:5157-5158`. NR 0xC2/C3 NMI return-address shadow,
both NMIACK-write and software-write paths, are correct (latched via
`set_nmi_return_address` from `Z80Cpu::on_nmi_servicing`; software
writes round-trip through the bare-byte path which matches VHDL
`nr_c2/c3_we`).

## Findings

### V13-NMP-01 — class-(b)

**Title**: NR 0x05 bit 2 (`nr_05_5060`) leaks through `regs_[0x05]`
cache when Pentagon mode is engaged.

**VHDL oracle** (`cores/zxnext/src/zxnext.vhd:5832-5841`):

```vhdl
process (i_CLK_28)
begin
   if rising_edge(i_CLK_28) then
      if nr_03_machine_timing(2) = '1' then
         nr_05_5060 <= '0';   -- Pentagon is always 50 Hz
      elsif nr_05_we = '1' then
         nr_05_5060 <= nr_wr_dat(2);
      elsif hotkey_5060 = '1' then
         nr_05_5060 <= not nr_05_5060;
      end if;
   end if;
end process;
```

The `if nr_03_machine_timing(2) = '1'` branch is priority-1: while
Pentagon is active, the underlying FF is held at `'0'` every clock,
and any explicit NR 0x05 write attempting to set bit 2 = 1 is
silently overwritten on the very next clock edge. Once Pentagon mode
is exited, the FF stays at `'0'` until a fresh NR 0x05 write or an F3
hotkey toggles it.

**C++ pre-fix divergence**: the NR 0x05 write_handler (emulator.cpp
~1082) returned `v` verbatim, so `regs_[0x05]` cached the raw byte.
Pass-10's `TC-NR05-PENTAGON` fix added a read-side mask in the NR
0x05 read_handler that clears bit 2 while Pentagon is active — but
the underlying cache still leaked, so two specific sequences
diverged from VHDL:

  * **Sequence A** (write-while-Pentagon): activate Pentagon, write
    NR 0x05 bit 2 = 1 (VHDL drops; FF stays 0), exit Pentagon, read
    NR 0x05.
    - VHDL: bit 2 = 0 (FF was held at 0 during the write attempt).
    - jnext pre-fix: bit 2 = 1 (cached value surfaces post-Pentagon-exit;
      read mask only fires while Pentagon live).

  * **Sequence B** (Pentagon-engagement clear): non-Pentagon, write
    NR 0x05 bit 2 = 1 (FF latches 1), activate Pentagon (FF forced to
    0 next clock edge), exit Pentagon, read NR 0x05.
    - VHDL: bit 2 = 0 (FF cleared by Pentagon engagement; no fresh
      write to set it post-Pentagon-exit).
    - jnext pre-fix: bit 2 = 1 (cache never cleared on the
      timing→Pentagon edge).

**Fix shape**: same canonicalisation pattern as V11-NMP-02 (NR 0x0A
bits 7:5 config_mode gate) and V11-NMP-03 (NR 0x06 bit 2 ps2_mode
config_mode gate), applied here on the Pentagon-timing gate. Two
canonicalisation seams:

  1. **Gated-write edge** — NR 0x05 write_handler at
     `src/core/emulator.cpp` (post-fix lines ~1112-1118): when
     Pentagon timing is active, force bit 2 of the returned canonical
     byte to '0' so `regs_[0x05]` matches the VHDL FF state.

  2. **Gating-state-change edge** — NR 0x03 write_handler at
     `src/core/emulator.cpp` (post-fix lines ~1976-1992): on the
     timing→Pentagon transition, clear cached(0x05) bit 2 so a
     pre-Pentagon write that legitimately latched bit 2 = 1 in the FF
     gets cleared in lock-step with the VHDL FF.

The read-side mask added in Pass-10 (TC-NR05-PENTAGON) is still
correct — it remains the source of truth while Pentagon is live. The
new write/edge canonicalisation extends correctness to **Pentagon-
exit** scenarios, where the read mask no longer fires.

**Patch sites** (`emulator.cpp`):

  * NR 0x05 write_handler — wrap the `return v` in a Pentagon-mode
    bit-2 mask (post-fix path); the previous handler is preserved
    verbatim outside Pentagon.
  * NR 0x03 write_handler timing-update block — after
    `nextreg_.set_nr_03_machine_timing(new_timing)`, if `new_timing &
    0x04` is non-zero (Pentagon), call `nextreg_.write(0x05,
    cached & ~0x04)` to canonicalise the cache. The
    write-handler-mediated path then re-applies the joystick
    fan-out with the bit-2-cleared byte (joystick joy0/joy1 fields
    are unaffected by bit 2; this is a pure cache step).

**Discriminative regression test**: `V13-NMP-01` in
`test/nextreg/nextreg_integration_test.cpp` (TestCov-NMI-MF-Port
group). Single check exercises both Sequences A and B in one go.
Revert-check: verified that with the two write_handler patches
removed, the test fails with `got_a=0x04 got_b=0x04` (the
pre-fix divergent values). With the fix in place,
`got_a=0x00 got_b=0x00` (VHDL-correct).

**Class**: (b) — observable divergence; latent because Pentagon mode
is rarely toggled at runtime (NextZXOS does not use it), but
correctness debt is real. Not (a) because no current jnext-tested
software path exercises the sequence; not (c) because the divergence
is reachable from any guest that toggles Pentagon timing through NR
0x03 writes around an NR 0x05 bit-2 write.

## Defensible-zero context for closed angles

For the record, the following Pass-13 audit angles closed with **no
new findings** — explicitly checked and found correct:

  * **NR multi-writer fan-out exhaustive** — re-checked `port_ff_reg`
    (3 writers: port-0xFF write @ emulator.cpp:2914, NR 0x22 b2 path
    @ ~1657, NR 0xC4 b0 NOT path @ ~2401). All three writers fan to
    `port_ff_reg_`, `ula_int_disabled_`, `video_timing_.set_interrupt_enable`,
    and `renderer_.ula().set_screen_mode` consistently — closed by
    V12-NMP-01/02. Searched for other latches with ≥3 distinct VHDL
    writers fanning into port_ff_reg / ula_int_en / nmi_assert_*
    family, none found.
  * **NR readback masks** — every NR with a VHDL `when X"NN"` read-mux
    entry was verified byte-exact:
    - NR 0x82/0x84 — full 8 bits (no mask needed; NextReg::write
      stores raw)
    - NR 0x83 — full 8 bits (verified)
    - NR 0x85/0x89 — `reset_type & "000" & enable[3:0]`, masked via
      explicit read_handler `& 0x8F` (line 2277/2287)
    - NR 0x86/0x87/0x88 — full 8 bits
    - NR 0x8A — `"00" & propagate[5:0]` (mask 0x3F applied at write
      time)
    - NR 0xC0 — bit 4 forced '0' via composed read_handler
    - NR 0xC4 — bits 6:2 forced '0', composed from im2_c4_expbus_ /
      video_timing_.line_interrupt_enable / !ula_int_disabled_
    - NR 0xC6 — bits 7,3 forced '0' (mask 0x77 applied at write
      time)
    - NR 0xCC — bits 6:2 forced '0' via composed read_handler
    - NR 0xCE — bits 7,3 forced '0' via composed read_handler
    - NR 0xD8 — bits 7:1 forced '0' via composed read_handler
    - NR 0xDA — bits 7:2 forced '0' via composed read_handler
    - NR 0x06 — bit 2 sourced from authoritative shadow
    - NR 0x09 — bit 4 from sprites_, bit 3 forced '0', bit 2 from
      cached (round-trip OK per VHDL inverter)
    - NR 0x0A — bit 2 forced '0', other bits from authoritative
      sources
    - NR 0x05 — Pentagon-mode bit-2 mask + V13-NMP-01 fix above
    - NR 0x07 — composed (act<<4 | req); approximation of
      bus-idle-committed effective (act follows req in jnext —
      noted previously, not a Pass-13 finding because no functional
      consumer probes the post-write/pre-commit window)
  * **Multiface FSM MF1 vs MF128 vs MF+3** — multiface.cpp's 4-FF
    state machine matches multiface.vhd:81-99 and 122-184 1:1.
    Mode-decode (mf_type "00"/"01"/"10"/"11" → mode_p3/mode_128
    variant A/B/mode_48) and the per-mode I/O protocol comment block
    match. invisible_eff combinational composition (`invisible AND NOT
    mode_48`) is correct. mf_port_en gate (multiface.vhd:195) is
    correct.
  * **NMI source priority arbitration** — full chain MF > DivMMC >
    ExpBus (nmi_source.cpp:380-391) matches VHDL:2097-2113. MF latch
    correctly gated on `!divmmc_conmem_ AND !divmmc_nmi_hold_`.
    DivMMC latch on `!mf_is_active_ AND !nmi_mf_`. ExpBus latch on
    `!nmi_mf_ AND !nmi_divmmc_`. Hold gate per VHDL:2118 — MF arm
    uses mf_nmi_hold, DivMMC arm uses divmmc_nmi_hold, ExpBus arm
    uses nmi_assert_expbus (the combinational producer, NOT
    divmmc_nmi_hold — bug fixed in earlier pass and confirmed
    persistent). config_mode force-clear at recompute_() entry
    matches VHDL:2102-2105.
  * **Hotkey debounce + edge-vs-level** — F1/F2/F3/F4/F7/F8 hotkey
    paths route through EmuFnKeys with NR 0x06 b7/b5 enable gates.
    F3 5060 callback gates on Pentagon timing (line ~3450) per
    VHDL:5836. F4 soft-reset gates on NR 0x03 config_mode per
    VHDL:6370. F1 hard-reset has no gate per VHDL:6371.
    F9/F10 NMI dispatch routes through `Emulator::on_hotkey_f9*/f10*`.
  * **NR 0x02 boot reset bits** — bit 7 (bus_reset) latched into
    `nr_02_bus_reset_` shadow + read mux + spi_.set_flash_cs_enable
    fan-out (G149/V11). Bit 4 (iotrap) sources from
    `nr_da_iotrap_cause_` per VHDL:3885. Bits 3/2 (MF/DivMMC NMI)
    routed through nmi_source_.nr_02_write(). Bits 1:0 (reset_type)
    from nmi_source_.reset_type(). Bit 0 (soft_reset) strobes
    nmi_source_.strobe_soft_reset() per VHDL:6370. Bit 1 (hard reset)
    routes through reset() — correct per VHDL:6371.
  * **NR 0x00/0x01/0x06 timing/feature flags dependencies on NR
    0x03 machine_type** — NR 0x06 has no machine-type dependency
    (timing_p3/p3_floating_bus etc. are derived from machine_timing
    not from NR 0x06 directly). NR 0x05 5060 dependency on Pentagon
    is the V13-NMP-01 finding.
  * **Joystick mode select (NR 0x05)** — every variant (Sinclair2 000,
    Kempston1 001, Cursor 010, Sinclair1 011, Kempston2 100, Md3Left
    101, Md3Right 110, IoMode 111) decoded per VHDL:5157-5158. Read
    formula at :5897 surfaces joy0[1:0]<<6 / joy1[1:0]<<4 /
    joy0[2]<<3 / joy1[2]<<1 — verified.
  * **Mouse port read formula** — 0xFADF (buttons + wheel + fixed
    bit-3), 0xFBDF (X register), 0xFFDF (Y register), all gated on
    NR 0x83 b5. Formula at KempstonMouse::read_port_*() is faithful
    per VHDL:3541-3562. (Audit-time skim, not unit-tested in this
    pass — no divergence visible.)
  * **NR 0xFF (current-NR data port)** — `read_selected()` →
    `read(selected_)` is correct; the data-port read returns the
    selected NR's value via the NR-specific read mux/handler/cached
    path. No bug here.
  * **Floating-bus on unmatched port** ($FF and similar) — VHDL
    has machine-specific behavior (port_p3_float on +3 with bits 0
    forced; ULA floating-bus on 48K/128K). emulator.cpp's
    `floating_bus_read()` (port-default) plus the explicit handler
    at port 0x0FFD (most-specific-wins for +3 mode) are correct
    per VHDL:2589 + zxula.vhd:573.
  * **Port-dispatch precedence** — most-specific-wins
    (port_dispatch.cpp:35-66) correctly handles overlaps. Multiface
    decode runs as a pre-dispatch IO observer (independent of the
    most-specific handler) per VHDL:2615-2616. Verified against the
    Kempston/Profi-DAC/MF-port LSB overlap (0x1F/0x3F/0x9F/0xBF).
  * **Cross-cutting cache leaks (V11-NMP-02/03 family)** — re-swept
    every NR write_handler. Found: V13-NMP-01 (NR 0x05 bit 2 in
    Pentagon mode, fixed). Did NOT find: any other config_mode-gated
    or machine_timing-gated NR-bit that stores raw `v` without
    canonicalisation. NR 0x06 bit 2 is V11-NMP-03 (closed). NR 0x0A
    bits 7:5 are V11-NMP-02 (closed). NR 0x11 (config_mode-gated
    timing field) returns the cached value when gate is closed
    (correct). NR 0x10 coreid is config_mode-gated but the
    write_handler returns the canonical readback byte
    `(coreid & 0x1F) << 2` which already enforces the gate semantics.

  Out-of-subsystem (skipped per scope rules):
    - DMA port 0x6B / 0x0B NR-side gate (NR 0x82 b5 / NR 0x85 b1) —
      DMA scope, not NMI/MF/Port.
    - Bus port enables (NR 0x86-0x89) AND-gate fan-out into
      port-decode when expbus_eff_en='1' — expbus scope (no expbus
      device in jnext).
    - NR 0xFF write read-zero (write-only register; VHDL `when others
      => 0`) — palette scope, not NMI/MF/Port. Same shape as the
      G149-fixed NR 0x04/0x29/0x35-39.
    - NR 0x60/0x63 (Copper) read-zero leak — copper scope.
    - NR 0x2A/0x2B (PS/2 keymap) read-zero leak — keyboard scope.

## Build & test status

```
cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
cmake --build build -j$(nproc)        # OK (release-mode)
ctest --test-dir build --output-on-failure
  → 38/38 PASS
./build/test/fuse_z80_test build/test/fuse
  → 1356/1356 PASS
./build/test/nextreg_integration_test
  → all groups PASS, including TestCov-NMI-MF-Port 45/45
    (was 44/44 pre-V13-NMP-01)
```

Revert-check on V13-NMP-01: temporarily disabled both
canonicalisation seams; the new test failed with
`got_a=0x04 got_b=0x04` (pre-fix divergent values), confirming
discriminative shape.

## Files touched

  * `src/core/emulator.cpp` — NR 0x05 write_handler (Pentagon-mode
    bit-2 mask) + NR 0x03 write_handler timing-update block
    (timing→Pentagon edge cache clear).
  * `test/nextreg/nextreg_integration_test.cpp` — added V13-NMP-01
    test in `test_testcov_nmi_mf_port` (TestCov-NMI-MF-Port group).
  * `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY13-NMI-MF-PORT.md`
    — this report.
