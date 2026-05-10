# Pass-17 — NMI / Multiface / Port-decode / NextREG audit — Independent Review

Branch: `task2/verify17-nmi-mf-port-reviewer` (off audit HEAD `f76b5d9`)
Worktree: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify17-nmi-mf-port-reviewer`
Build: Release-mode CMake `-DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON`.
Tests: ctest 38/38 PASS, FUSE 1356/1356 PASS.

## Verdict: **APPROVE**

All three audit fixes verified against VHDL. Each discriminative test fails on the
reverted code and passes with the fix in place. No missed findings under the
NMP scope.

## 1. Audit fix verification

For every fix the reviewer (a) read the commit diff and cross-checked the cited
VHDL lines in `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`,
(b) reverted only the source-side change in the worktree, rebuilt, and confirmed
the discriminative test FAILs, (c) restored the fix and confirmed the test
PASSes again. All three steps succeeded for every finding.

### V17-NMP-01 — NR 0xB8/0xB9/0xBA/0xBB readback returns reset defaults

- VHDL `zxnext.vhd:5087-5090` master-reset block confirmed:
  `nr_b8_divmmc_ep_0 <= X"83"; nr_b9_divmmc_ep_valid_0 <= X"01"; nr_ba_divmmc_ep_timing_0 <= X"00"; nr_bb_divmmc_ep_1 <= X"CD";`
- VHDL `zxnext.vhd:6217-6227` read mux confirmed: each NR returns the storage signal verbatim.
- DivMmc subsystem already stores the same values in `entry_points_0_=0x83`,
  `entry_valid_0_=0x01`, `entry_timing_0_=0x00`, `entry_points_1_=0xCD`
  (`src/peripheral/divmmc.h:342-345` + `divmmc.cpp:43-46`); fix routes the
  NR readback through these accessors. Symmetric with the existing NR 0x12/0x13
  pattern that pulls from Layer2.
- Discriminative test `RST-13` in `test/nextreg/nextreg_integration_test.cpp`
  asserts the four bytes equal 0x83/0x01/0x00/0xCD after `Emulator::init()`.
  Reverted source → `FAIL RST-13: ... NR 0xbb got=0x00 expected=0xcd`.
  Re-applied → PASS.

### V17-NMP-02 — port_FE handler matches any even port

- VHDL `zxnext.vhd:2582` confirmed: `port_fe <= '1' when cpu_a(0) = '0' else '0';`
  ULA decode matches **any** port with bit 0 = 0 (the standard 48K Spectrum rule).
- Pre-fix mask `0x00FF/0x00FE` required LSB = 0xFE exactly — narrower than
  VHDL spec.
- Fix changes mask to `0x0001/0x0000` (any port with bit 0 = 0).
- **Conflict analysis (mandatory regression check)**: I scanned all 52
  `port_.register_handler` call sites in `src/core/emulator.cpp`. Every other
  handler that could match an even port has either:
  - LSB = 1 in its `value` field (e.g. 7FFD `0x8003/0x0001`, AY `0xC007/0xC005`)
    — disjoint from the new port_FE handler.
  - Higher specificity (8+ bits of mask) AND its `value` LSB is odd
    (0x1F, 0x0F, 0x4F, 0xDF, 0x6B, 0x0B, 0xE7, 0xEB, 0xE3, 0x37, 0x57, 0x5B,
    0xFF, 0x3F, 0xBF, 0x9F).
  None has even LSB. Therefore the new `0x0001/0x0000` (1-bit specificity) port_FE
  handler is the **only** matcher for even ports outside of more-specific
  16-bit-pinned handlers (which have ODD LSBs). No conflict, no regression.
- The Multiface enable/disable port-decodes use ODD LSBs (0x9F/0xBF/0x3F/0x1F
  per VHDL `:2612-2613`); `effective_internal_port_enable` audit (V16-NMP-02)
  is also untouched by this change.
- The MF M1 hotkey decode at $66/$0066 is via `io_observer` (M1 fetch),
  not via port_dispatch — independent of this handler.
- Discriminative test `REG-01b` in `test/port/port_test.cpp` writes to
  0x00FC, 0x00F8, 0x4242 and verifies border updates each time.
  Reverted source → `FAIL REG-01b: ... borders=0,0,0 expected 2,7,5`.
  Re-applied → PASS.

### V17-NMP-03 — port_FF handler matches any LSB == 0xFF

- VHDL `zxnext.vhd:2540-2571` LSB case statement and `:2583` confirmed:
  `port_ff_lsb <= '1'` when `cpu_a(7:0) = X"FF"`; `port_ff <= port_ff_lsb`.
  High byte irrelevant.
- Pre-fix mask `0xFFFF/0x00FF` required full 16-bit address = 0x00FF.
- Fix changes mask to `0x00FF/0x00FF` (LSB-only).
- The handler is **write-only** (`nullptr` read callback). The dispatcher
  skips read-null handlers when selecting reads, so reads at LSB == 0xFF
  still fall through to `floating_bus_read` exactly as before.
- Discriminative test `REG-02b` in `test/port/port_test.cpp` writes
  `0x06` (hi-res mode) to port `0x12FF` and verifies the Timex screen-mode
  bits 2:0 = 6.
  Reverted source → `FAIL REG-02b: ... baseline=0 after=0`.
  Re-applied → PASS.

## 2. Independent re-audit (NMP scope)

### NR readback defaults — full sweep

I extracted **all** non-zero VHDL reset defaults from `zxnext.vhd:4928-5111`
(master reset block) and cross-checked against the emulator's `regs_[]` cache,
read_handlers, and constructor seeds:

| NR     | VHDL default                        | jnext disposition                            |
|--------|--------------------------------------|----------------------------------------------|
| 0x06   | bits 7+5 = 1 (0xA0)                  | `regs_[0x06]=0xA0` constructor seed; reset() preserves bits 6/4/3/2/1/0; read_handler composes ps2_mode bit. ✓ |
| 0x0B   | bit 0 = 1 (0x01)                     | `regs_[0x0B]=0x01` in reset(). ✓             |
| 0x12   | 0x08                                 | read_handler from `layer2_.active_bank_lsb()`. ✓ |
| 0x13   | 0x0B                                 | read_handler from `layer2_.shadow_bank_lsb()`. ✓ |
| 0x14   | 0xE3                                 | `regs_[0x14]=0xE3` in reset(). ✓             |
| 0x18-0x1B | clip rect non-zero defaults       | read_handlers pull from layer2/sprites/ula/tilemap subsystems; subsystem reset()'s install correct defaults. ✓ |
| 0x42   | 0x07                                 | read_handler from `ula().get_ulanext_format()`; `ulanext_format_=0x07` default. ✓ |
| 0x4A   | 0xE3                                 | `regs_[0x4A]=0xE3` in reset(). ✓             |
| 0x4B   | 0xE3                                 | `regs_[0x4B]=0xE3` in reset(). ✓             |
| 0x4C   | 0x0F                                 | `regs_[0x4C]=0x0F` in reset(). ✓             |
| 0x68   | bit 7 of read = NOT(ula_en) = 0      | cache-based read; cache zeroed → bit 7 = 0; matches VHDL. ✓ |
| 0x6E   | 0x2C                                 | read_handler from `tilemap_.get_map_base_read()`; `tilemap_.reset()` installs `map_base_raw_=0x2C`. ✓ |
| 0x6F   | 0x0C                                 | read_handler from `tilemap_.get_def_base_read()`; `tilemap_.reset()` installs `def_base_raw_=0x0C`. ✓ |
| 0x82-0x89 | 0xFF / 0x8F (gated by reset_type) | reset() honors VHDL gating per V11-NMP/V13-NMP. ✓ |
| 0x98-0x9B | OUTPUT shadow non-zero          | Read mux returns INPUT pins (`i_GPIO`), not output shadow. read_handler returns 0 (no Pi). Correct. |
| 0xA9   | OUTPUT shadow = 1                    | Read mux returns INPUT pin (`i_ESP_GPIO_20(2,0)`), not output. read_handler returns 0. Correct. |
| 0xB8   | 0x83                                 | **V17-NMP-01 fix**: read_handler from `divmmc_.entry_points_0()`. ✓ |
| 0xB9   | 0x01                                 | **V17-NMP-01 fix**: read_handler from `divmmc_.entry_valid_0()`. ✓ |
| 0xBA   | 0x00                                 | **V17-NMP-01 fix**: read_handler from `divmmc_.entry_timing_0()`. ✓ |
| 0xBB   | 0xCD                                 | **V17-NMP-01 fix**: read_handler from `divmmc_.entry_points_1()`. ✓ |
| 0xC4   | bit 7 = 1                            | `im2_c4_expbus_=true` in init(); read_handler composes. ✓ |

**No additional missed defaults.**

### Cross-cutting families

- **Cache-leak (config-mode-gated bits)**: NR 0x06 b2, NR 0x0A b7:5, NR 0x05 b2,
  NR 0x11, NR 0x03 — all canonicalised on the write side or composed in the
  read_handler. None leak.
- **Multi-writer fan-out**: `port_e3_reg(6)` (V12-NMP-01), `port_ff_reg(6)`
  (NR 0x22 + NR 0xC4 + port-FF — V12-NMP-01) — all fanned out correctly.
  The NR 0x68 ulap_en + port-0xFF3B path also cleanly composes via read-time
  read of the ulap_en shadow.
- **Write-only NR readback**: NR 0xC0 b4, NR 0x09 b3, NR 0x22 b6:3, NR 0xC4 b6:2
  all force-zero correctly. NR 0x28 returns palette-stored-value (V14-NMP-02).
  NR 0xFF write-only set (V15) closed.
- **Default-FF for unimplemented NR**: VHDL `when others => port_253b_dat <= (others => '0')` (line 6286-6287) — jnext returns 0x00 via `regs_[reg]` default.
  Match.
- **load_state shadow re-push**: `NmiSource::load_state` and `Multiface::load_state`
  use direct member assignment with no setter side effects. Verified.
- **Port-decode AND-mask vs OR-mask**: V17-NMP-02/03 are this family; both
  correctly fixed and verified above.
- **`effective_internal_port_enable` helper** (V16-NMP-02 added): the V17-NMP-02
  port_FE handler does NOT route through this helper because `port_fe_io_en`
  is **not** in `internal_port_enable[5:0]` (the helper's domain) — VHDL
  `port_fe` is unconditionally enabled (line 2582 has no IO-enable gate). Same
  for V17-NMP-03 port_FF: it DOES gate on `effective_internal_port_enable(0x82) & 0x01`
  (NR 0x82 b0 = `port_ff_io_en`) which the existing handler already does.
  No new sites need routing.
- **NMI assert/deassert**: `Z80Cpu::request_nmi()` HALT/EI handling unchanged
  by Pass-17. RETN semantics correct (FUSE 1356/1356 covers RETN edge cases).
- **Save/load symmetry** for the NR 0xB8-0xBB read_handlers: the new
  read_handlers pull from `divmmc_` state, which `Multiface`/`DivMmc` already
  serialise/deserialise via their own save/load_state methods. The
  `regs_[0xB8..0xBB]` cache value is now technically irrelevant but harmless
  — NR 0xB8-0xBB write_handlers still echo the byte, so cache stays consistent
  with subsystem state. No state-machine drift on load.

### Other reviewed code paths (no findings)

- **`PortDispatch::read/write`** (`src/port/port_dispatch.cpp`): most-specific-wins
  selection works correctly; `mask_specificity()` counts set bits; read fall-through
  for nullptr-read handlers is intact.
- **`NmiSource::reset()`/`save_state`/`load_state`**: clean direct-member assignments.
- **`Multiface::reset()`/`save_state`/`load_state`**: lossy `mf_type_` reconstruction
  from mode bits already documented and class-(c)'d in the audit's deferred list.
- **NR 0x80 expbus byte-fold on reset**: handled by `computed_80` shadow.
- **NR 0x8C altrom byte-fold**: handled by `computed_8c` shadow.

## 3. Observations / nits (no findings, no fixes required)

- **`regs_[0xB8..0xBB]` cache is now dead state**: the V17-NMP-01 read_handlers
  bypass it. The write_handlers echo the byte, so the cache is updated on every
  write; reads ignore it. Not a bug — symmetric with the NR 0x12/0x13 pattern.
  Could be trimmed for tidiness in a future cleanup pass, but no behaviour
  consequence.
- **REG-01b also adds load on the assertion message**: `borders=2,7,5` is the
  expected vector. Test reads cleanly.
- **Pre-existing regression failures NOT induced by V17**: `parallax-demo`
  (44636 pixel diff) and `rewind-func` (stale regression-script regex
  expecting 18 PASSes vs current 22). Both verified present at integration
  HEAD `f718876` BEFORE the V17-NMP commits land. Not in scope for Pass-17.

## 4. Build / test summary

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_QT_UI=ON
cmake --build build -j$(nproc)            → green, 100% target build
ctest --test-dir build -j$(nproc)         → 38/38 PASS
./build/test/fuse_z80_test build/test/fuse → 1356/1356 PASS
```

## 5. Conclusion

The audit is **VHDL-faithful**, **discriminative**, and **does not regress**
any other test. The reviewer finds no missed bugs under the NMP scope.

**APPROVE.**
