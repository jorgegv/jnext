# NextZXOS Boot — Pass-12 Fix-of-Reviewer Independent Review (NMI + Multiface + Port + NextREG)

**Date:** 2026-05-10
**Reviewer:** independent fix-reviewer (did not author audit / fix-of-reviewer / prior reviewer reports)
**Subject:** V12-NMP-02 (port-0xFF write fan-out into `ula_int_disabled_` shadow + `video_timing_`)
**Subject branch:** `task2/verify12-nmi-mf-port` HEAD `ad0765f`
**Reviewer branch:** `task2/verify12-nmi-mf-port-fix-reviewer`
**Worktree:** `.claude/worktrees/task2-verify12-nmi-mf-port-fix-reviewer`
**Oracle:** `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`

## Verdict

**APPROVE.**

V12-NMP-02 closes the third-writer fan-out gap into `port_ff_reg(6)`
that the prior pass-12 reviewer (`e2db16c`) flagged as a NIT. The fix
is VHDL-faithful, symmetric with the existing NR 0x22 / NR 0xC4
write_handlers (lines 1657-1658 / 2423-2424), the discriminative
regression coverage (`ULA-INT-V12-NMP-02` and `ULA-INT-V12-NMP-02b`)
both FAIL when the C++ fix is reverted and PASS once restored, and
ctest 38/38 + FUSE 1356/1356 + ctc_interrupts 23/23 are clean. No other
multi-writer fan-out gaps remain in `port_ff_reg`.

## VHDL claim — VERIFIED

`zxnext.vhd:3610-3635` (port_ff_reg writer process) and `:6711`
(`ula_int_en` composition) read directly:

```vhdl
3610:   process (i_CLK_28)
3611:   begin
3612:      if rising_edge(i_CLK_28) then
3613:         if reset = '1' then
3614:            port_ff_reg <= (others => '0');
3615:         elsif port_ff_wr = '1' then
3616:            port_ff_reg <= cpu_do;             -- (A) port-0xFF FULL byte (incl. bit 6)
3617:         elsif nr_69_we = '1' then
3618:            port_ff_reg(5 downto 0) <= nr_wr_dat(5 downto 0);
3619:         elsif nr_22_we = '1' then
3620:            port_ff_reg(6) <= nr_wr_dat(2);    -- (B) NR 0x22 b2
3621:         elsif nr_c4_we = '1' then
3622:            port_ff_reg(6) <= not nr_wr_dat(0); -- (C) NR 0xC4 b0 (INVERTED)
3623:         end if;
3624:      end if;
3625:   end process;
3635:   port_ff_interrupt_disable <= port_ff_reg(6);
6711:   ula_int_en <= nr_22_line_interrupt_en & (not port_ff_interrupt_disable);
```

Three writers (A, B, C) feed `port_ff_reg(6)` →
`port_ff_interrupt_disable` → `ula_int_en(0)`. The VHDL writer
priority is encoded by the `elsif` chain (port-FF wins; NR-side updates
are mutually exclusive within a 28 MHz tick). The audit's claim that
port-0xFF is the third writer is correct.

`zxnext.vhd:6239` confirms the NR 0xC4 read mux returns
`nr_c4_int_en_0_expbus & "00000" & ula_int_en` → bit 0 reads
`(not port_ff_interrupt_disable)`, i.e. the live `port_ff_reg(6)`
inverted. The audit also cites this oracle correctly.

## Fix correctness — VERIFIED

`fdd21ca` C++ diff at `src/core/emulator.cpp:2914-2932`: after the
existing `port_ff_reg_ = val; renderer_.ula().set_screen_mode(val);`
the handler now mirrors the NR 0x22 / NR 0xC4 pattern:

```cpp
ula_int_disabled_ = (port_ff_reg_ & 0x40) != 0;
video_timing_.set_interrupt_enable(!ula_int_disabled_);
```

Bit 6 = `0x40` is the correct mask for `port_ff_reg(6)` =
`port_ff_interrupt_disable`. The polarity matches the NR 0x22 path
(non-inverted: `(v & 0x04) != 0` reads bit 2 of the NR-22 write byte
which goes into `port_ff_reg(6)` non-inverted). The `video_timing_`
fan-out matches both prior writers exactly. No other side effects
introduced — bits 5..0 still flow through `set_screen_mode(val)` (which
the existing line at :2915 already does), and the
`if ((nextreg_.cached(0x82) & 0x01) == 0) return;` guard at line 2910
is preserved (port_ff_io_en gating per VHDL :2397).

The fix is the minimum-symmetric closure of the third writer.

## Discriminative tests — VERIFIED

`ctc_interrupts_test.cpp` adds:

- **`ULA-INT-V12-NMP-02`** (lines 305-325) — readback path. Drives
  `OUT (0xFF),0x40` then `OUT (0xFF),0x00`, asserts NR 0xC4 read bit 0
  follows the live `port_ff_reg(6)` (1→0→1).
- **`ULA-INT-V12-NMP-02b`** (lines 332-342) — scheduler gate. Drives
  `OUT (0xFF),0x40` and runs a frame, asserts NR 0xC8 bit 0 stays
  clear (= ULA INT was suppressed).

Pre-revert verification (replaced `src/core/emulator.cpp` with the
parent `fdd21ca^` and rebuilt):

```
FAIL ULA-INT-V12-NMP-02: ... [initial=0x81 after_OUT_FF_40=0x81 after_OUT_FF_00=0x81 (bit 0 should follow 1,0,1)]
FAIL ULA-INT-V12-NMP-02b: ... [NR 0xC8=0x01 (expected bit 0 clear after OUT FF,40)]
```

Both fail with diagnostically clear messages — the readback test sees
the stale shadow stuck at bit 0 = 1 across all three reads, and the
scheduler test sees NR 0xC8 bit 0 set (= ULA INT was raised). Post-
restore both PASS (ctc_interrupts 23/23, ULA-Integration group 8/8).

This is exactly the discriminative shape required: the new tests
cannot accidentally pass on a stale shadow.

## Full Release-mode test suite — CLEAN

After restoring the fix:

- `cmake -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON` → configure OK.
- `cmake --build build` → all targets built (jnext + every test).
- `ctest --test-dir build --output-on-failure` → **38/38 pass**.
- `./build/test/fuse_z80_test build/test/fuse` → **1356/1356 pass**.

Zero FAILs. Zero new SKIPs.

## Hunt for other multi-writer fan-out gaps in `port_ff_reg`

Per VHDL :3610-3624 the writer matrix on `port_ff_reg(7:0)` is:

| Bit | port_ff_wr | nr_69_we | nr_22_we | nr_c4_we |
|-----|-----------|---------|---------|---------|
| 7   | full byte | —       | —       | —       |
| 6   | full byte | —       | b2      | NOT b0  |
| 5..0| full byte | b5..0   | —       | —       |

**Bit 7** — single writer (port-FF). No fan-out gap possible.

**Bit 6** — three writers (port-FF, NR 0x22 b2, NR 0xC4 b0 NOT). Each
maps to two C++ side-effects:
1. `port_ff_reg_` bit 6 store (canonical).
2. `ula_int_disabled_` shadow + `video_timing_.set_interrupt_enable()`
   fan-out.

After V12-NMP-02 all three writers update both. Cross-checked at
`emulator.cpp:1657-1658` (NR 0x22), `emulator.cpp:2423-2424`
(NR 0xC4), `emulator.cpp:2931-2932` (port-FF). **No remaining gap.**

**Bits 5..0** — two writers (port-FF, NR 0x69 b5..0). C++ side-effects:
1. `port_ff_reg_` bits 5..0 store (canonical).
2. `Ula::set_screen_mode(port_ff_reg_)` fan-out (the renderer surface).

Both writers update both:
- port-FF write: `port_ff_reg_ = val; renderer_.ula().set_screen_mode(val);`
  (`emulator.cpp:2914-2915`).
- NR 0x69 write: `port_ff_reg_ = (port_ff_reg_ & 0xC0) | (v & 0x3F);
  renderer_.ula().set_screen_mode(port_ff_reg_);` (`emulator.cpp:2163-2170`).

The NR 0x69 read (`emulator.cpp:2179-2185`) composes from
`layer2_.enabled()` (live), `mmu_.shadow_screen_en()` (live), and
`port_ff_reg_ & 0x3F` (the canonical store) — no separate shadow that
could go stale. The NR 0x22 read (`emulator.cpp:1693-1701`) reads
`(port_ff_reg_ >> 4) & 0x04` for bit 2 → also keyed off the canonical
store. The NR 0xC4 read (`emulator.cpp:2429-2437`) uses
`!ula_int_disabled_` for bit 0 — that shadow is now correctly synced
by all three writers. **No remaining gap.**

## Audit closure status

After V12-NMP-02:

- All three writers to `port_ff_reg(6)` now keep `ula_int_disabled_` in
  sync with the canonical store.
- The stale narrative in `ctc_interrupts_test.cpp:157-163`
  (ULA-INT-02's "DIRECT OUT 0xFF latent gap" caveat) is replaced by
  V12-NMP-02 closure note + the new V12-NMP-02 / -02b rows below the
  ULA-INT-02 block.
- The `port_ff_reg` writer matrix is now fully fanned out on the C++
  side — the multi-writer-shadow class of bug is **closed for
  `port_ff_reg`**.

## Observations / minor notes (not blocking APPROVE)

1. The `port_ff_reg_ = val` assignment on line 2914 keeps bit 7
   verbatim from the CPU byte; bit 7 is unused by any consumer (no
   read mux references `port_ff_reg(7)`), so this is harmless and
   matches the VHDL `port_ff_reg <= cpu_do` exactly. Worth a one-line
   comment if a future pass touches this code, but not in scope here.
2. The discriminative test for V12-NMP-02b uses `run_frame()` and
   reads NR 0xC8 bit 0; the symmetry with ULA-INT-02 (NR-22 mirror)
   makes this comparison self-explanatory.
3. The audit doc `NEXTZXOS-BOOT-SUBSYSTEM-VERIFY12-NMI-MF-PORT.md`
   already records V12-NMP-02 with the correct VHDL anchors and
   commit `fdd21ca`. Aggregate count update (class-(a)=2) is
   internally consistent.

## Files reviewed

- `src/core/emulator.cpp` (port-FF write handler, NR 0x22 / NR 0xC4
  / NR 0x69 write+read handlers).
- `src/video/ula.h`, `src/video/ula.cpp` (`set_screen_mode` /
  `screen_mode_reg_`).
- `test/ctc_interrupts/ctc_interrupts_test.cpp` (new V12-NMP-02 /
  -02b rows + comment cleanup at lines 157-163).
- `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY12-NMI-MF-PORT.md`
  (audit doc post fix-of-reviewer update).
- VHDL: `zxnext.vhd:605, :866-868, :2714, :3601-3635, :5992, :6096,
  :6239, :6711, :6750`.

## Reproducibility

- Worktree HEAD: `ad0765f`.
- Build: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`.
- ctest 38/38, FUSE 1356/1356, ctc_interrupts 23/23 (post-restore).
- Pre-revert (parent `fdd21ca^` of `emulator.cpp`):
  `ULA-INT-V12-NMP-02` + `-V12-NMP-02b` both FAIL. Restore by
  `git checkout ad0765f -- src/core/emulator.cpp` + rebuild.

## Verdict (final)

**APPROVE.** VHDL claim correct, fix correct, both tests
discriminative, no other multi-writer fan-out gaps remain in
`port_ff_reg`.
