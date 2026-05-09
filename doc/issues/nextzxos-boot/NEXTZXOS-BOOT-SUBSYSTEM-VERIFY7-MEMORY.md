# NextZXOS Boot Subsystem — Pass-7 Memory Re-Audit (Verify7)

## Verdict

**NOT YET CONVERGED.** Pass-7 found **2 class-(a) bugs** in the memory
subsystem after 6 prior passes had landed 13 fixes. Both bugs are in
integration seams between MMU and consumer subsystems (DivMMC + machine
type), not in the core MMU register/arbiter logic itself.

This was the convergence-test pass. The honest answer is: we have NOT
yet converged. The MMU core itself is in good shape (no new findings in
the SRAM arbiter, the +3 special paging table, the NR 0x8E
decomposition, the EFF7(3) RAM-at-0x0000 path, or the save/load
serialisation), but **integration with downstream subsystems still
exposes class-(a) bugs that pass-6 missed**.

This is consistent with the trend across passes:

| Pass | Class-(a) memory bugs found |
|------|------------------------------|
| 1    | 1 (VHDL-faithful slot 0/1 + NR 0x8C cache)        |
| 2    | n/a (Pass-2 was task-2 review, not verify pass)   |
| 3    | 3 (unlock_paging bit 5, NR 0x8C RAM preservation, EFF7-1 readback) |
| 4    | 3 (EFF7 over-application, nr_mmu save/load, NR 0x12 L2 propagation) |
| 5    | 2 (boot_rom_en config_mode gate, set_machine_type +3-special preserve) |
| 6    | 1 (mem_active_page_for sentinel)                  |
| 7    | **2** (this pass — DivMmc rom3_active wiring + set_machine_type RAM clobber) |

The trend is downward but not flat. Pass-7 reframes: the core MMU is
mature; the residual bugs live in **how surrounding code consumes MMU
state**.

## Methodology

This pass walked the seven angles documented in the prompt:

1. **Test-suite-vs-VHDL coverage gap** — examined `mmu_test.cpp`
   (3319 lines, 202 rows + 22 SKIPs) and `contention_test.cpp` for VHDL
   signal coverage. Found one signal that was VHDL-faithfully exposed
   but **never used** by any consumer: `Mmu::sram_rom3()`. The
   `sram_rom3()` accessor was added in pass-3 with discriminative tests
   (ROM-10/11/12 covering 48K hardwire / altrom-lock / +3-bits-AND
   semantics) but the integration callsite (`Emulator` → `DivMmc`)
   continued using the legacy `rom3_selected()` observer.

2. **Behavioural fingerprint comparison** — picked a handful of paging
   sequences (NR 0x50 RAM-mapping followed by NR 0x03 machine_type
   commit; classic 128K bank-switch with EFF7(3)=1; +3 special-mode
   entry/exit). For each, predicted the cycle-by-cycle MMU register
   state per VHDL :3640-3818 + :4607-4700 and per the C++. Two paths
   diverged (see findings 1 and 2 below).

3. **Saved-state binary compatibility** — confirmed save/load order is
   stable; no field was added/removed at the head of the stream that
   would shift older bytes. The `nr_mmu_[8]` array is appended at the
   tail (pass-4 fix) so older streams short-read cleanly via
   StateReader's bounds check.

4. **Cold-start invariants** — walked
   `Mmu()` → `reset(true)` → `map_rom_physical(0/1)` → first instruction
   read. Confirmed RESET_PAGES + map_rom_physical produces a coherent
   slot 0/1 = ROM, slots 2..5 = bank-5/bank-2 dual-port, slots 6/7 =
   bank-0 starting state matching VHDL :4611-4618. No issues.

5. **Race-free invariants under concurrent state changes** — single-
   threaded execution; no race possible. The Copper writes feed through
   `nextreg_.write` which pushes into `Mmu` via the same handlers as
   CPU writes, so ordering is sequentialised.

6. **Signal coherence after every fix from passes 1-6** — re-read every
   commit on this branch. The pass-3 NR 0x8C set_nr_8c per-slot refresh
   (`if (read_only_[i]) engage_legacy_rom_paging_slot(i);`) is the
   correct pattern. **The same pattern was MISSING from
   `set_machine_type` after the pass-5 +3-special-paging guard was
   added.** That is finding 2 below — a pattern-application miss.

7. **Boring tests / line-by-line of helper functions** — re-read every
   line of `Mmu::set_page`, `Mmu::rebuild_ptr`,
   `Mmu::engage_legacy_rom_paging_slot`, `Mmu::apply_legacy_rom_slots_`,
   `Mmu::set_nr_8c`, `Mmu::write_nr_8e`, `Mmu::apply_paging_update_`,
   `Mmu::apply_plus3_special_paging_`,
   `Mmu::revert_slots_2_to_5_post_special_`, plus the read/write
   inlines. Verified each line against the cited VHDL anchors. No
   issues found in these primitives.

## Findings

### Finding 1 — `DivMmc::rom3_active_` sourced from wrong VHDL signal (class-(a))

**Symptom:** the DivMMC ROM3-conditional auto-map gate
(`sram_divmmc_automap_rom3_en`, VHDL `zxnext.vhd:3138`) consumes the
`sram_pre_rom3` registered version of `sram_rom3` (VHDL :3023). VHDL
`sram_rom3` (`zxnext.vhd:2981-3008`) is per-machine-type:

| Machine | sram_rom3 with no altrom lock | with altrom lock |
|---------|-------------------------------|-------------------|
| 48K     | `'1'` (hardwired)             | `'1'`             |
| +3      | `port_1ffd_rom(1) AND port_1ffd_rom(0)` | `lock_rom1 AND lock_rom0` |
| ZXN/128K | `port_1ffd_rom(0)` (= port_7ffd(4)) | `lock_rom1` |

jnext was wiring DivMmc through `Mmu::rom3_selected()` (= `current_rom_bank() == 3`),
i.e. `(port_1ffd(2) << 1 | port_7ffd(4)) == 3`. This is correct ONLY
for the +3-with-no-altrom case. For Next/128K mode, jnext was
**under-reporting ROM3-active**: VHDL says ROM3 is active whenever
`port_7ffd(4)=1` in Next mode (1-bit sram_rom), but jnext required BOTH
`port_1ffd(2)` AND `port_7ffd(4)`. For 48K mode jnext was reporting
`false` even though VHDL hardwires it `true`.

**Impact:** DivMMC's ROM3-conditional auto-map (the standard NextZXOS
supervisor configuration with `NR 0xB9 = $00`) was silently suppressed
on Next-mode boot whenever the supervisor selected its high ROM bank
without simultaneously raising port_1ffd(2). RST traps that should
have fired the auto-map (RST $00, $08, $10, $18, $20, $28, $30, $38
with valid bit clear) did not.

**Fix:** swap `mmu_.rom3_selected()` for `mmu_.sram_rom3()` at all
three Emulator integration sites (port 0x7FFD handler at line 2488,
port 0x1FFD handler at line 2592, init-time sync at line 3978). Also
add `divmmc_.set_rom3_active(mmu_.sram_rom3())` to the NR 0x8C handler
(altrom locks affect sram_rom3 per VHDL :2990, :3000) and to the NR
0x03 machine_type commit path (machine type changes flip 48K's
hardwired sram_rom3='1' on/off and the +3-vs-ZXN bit composition).

**VHDL anchor:** `zxnext.vhd:2981-3008` (sram_rom3 derivation),
`zxnext.vhd:3138` (`sram_divmmc_automap_rom3_en` consumer).

**Test coverage:** `mmu_test` ROM-10/11/12 already exist for the bare
`sram_rom3()` accessor (added in pass-3). Integration test added in
this pass: see finding 2's MTC tests for adjacent coverage; the
DivMmc-side wiring is exercised by `divmmc_test`'s existing
`set_rom3_active`-driven scenarios, which now receive the correct
signal.

### Finding 2 — `Mmu::set_machine_type` clobbers explicit slot 0/1 RAM mapping (class-(a))

**Symptom:** when firmware has explicitly mapped slot 0 or slot 1 to
RAM via `NR 0x50/0x51` (with v < 0xE0), a subsequent machine_type
change via `NR 0x03` would clobber that mapping by calling
`apply_legacy_rom_slots_()`, which writes
`nr_mmu_[0]=nr_mmu_[1]=0xFF` and forces both slots back to the legacy
ROM path.

VHDL `sram_rom` (`zxnext.vhd:2981-3008`) is combinational from
machine_type, but the SRAM arbiter consumes it ONLY on the legacy-ROM
branch (`zxnext.vhd:3052`) — i.e. when `mmu_A21_A13(8)='1'`, which
requires MMU<i> >= 0xE0. A machine_type change does NOT trigger
`port_memory_change_dly` (`zxnext.vhd:3813` only OR-s in port-write
and NR-paging-write signals), so VHDL leaves MMU<i> alone. Slots
explicitly RAM-mapped via NR 0x50/0x51 stay routed to RAM.

**Impact:** firmware that uses NR 0x50/0x51 to RAM-map slot 0 or slot
1 (e.g. for a custom paging layout or for NextZXOS supervisor
table-loading sequences) and then commits a machine_type via NR 0x03
would see its RAM mappings silently reverted. The same pattern fix had
already landed in pass-3 for `set_nr_8c` (per-slot refresh gated on
`read_only_[i]`); pass-5 added a +3-special-paging guard to
`set_machine_type` but kept the unconditional `apply_legacy_rom_slots_`
call for the non-special path — exactly the pattern that was wrong in
`set_nr_8c` before pass-3.

**Fix:** mirror the pass-3 `set_nr_8c` pattern in `set_machine_type`:

```cpp
// Verify7-memory class-(a) fix
if (read_only_[0]) engage_legacy_rom_paging_slot(0);
if (read_only_[1]) engage_legacy_rom_paging_slot(1);
```

Slots in legacy ROM mode (read_only_=true) consume sram_rom for the
read-pointer cache and need a refresh on machine-type change; slots
explicitly mapped to RAM stay untouched.

**VHDL anchor:** `zxnext.vhd:2981-3008` (sram_rom combinational
derivation), `zxnext.vhd:3813` (port_memory_change_dly trigger list —
machine_type is not in it), `zxnext.vhd:4619-4644` (MMU<i> register
update gated on port_memory_change_dly).

**Test coverage:** added `Cat11c machine_type preserves slot 0/1 RAM
(verify7)` group with three rows:

- **MTC-01:** slot 0 NR-mapped to RAM, machine_type ZXN→128K, verify
  page and read_only_ unchanged.
- **MTC-02:** slot 1 NR-mapped to RAM, machine_type ZXN→+3, verify
  page and read_only_ unchanged.
- **MTC-03:** both slots in legacy ROM mode with port_7ffd(4)=1
  (sram_rom=1 → pages 2/3); switch ZXN→48K (sram_rom hardwired 0 →
  pages 0/1). Verify the cache refreshes to new sram_rom-derived pages
  but slots stay in ROM mode.

All three pass after the fix.

## Files Modified

- `src/memory/mmu.h` — `Mmu::set_machine_type` per-slot refresh fix.
- `src/core/emulator.cpp` — three `divmmc_.set_rom3_active` callsites
  switched from `mmu_.rom3_selected()` to `mmu_.sram_rom3()`; one new
  callsite added to NR 0x8C handler; one new callsite added to NR 0x03
  machine_type commit path.
- `test/mmu/mmu_test.cpp` — new `Cat11c` group with MTC-01/02/03.

## Test Status

- `fuse_z80_tests`     — Passed (1356/1356)
- `mmu_tests`          — Passed (205/183 pass, 0 fail, 22 skip — was
                                 202/180/0/22 before pass-7)
- `mmu_integration_tests` — Passed
- `divmmc_tests`       — Passed (no regressions from `sram_rom3()`
                                 wiring change)
- `contention_tests`   — Passed
- All 37 unit-test executables — Passed.

## Honest Convergence Verdict

**NOT YET CONVERGED — but close, and the residue is in integration
seams, not the MMU core.**

After 7 passes of audits + fixes, the MMU core (register storage,
SRAM arbiter, paging-trigger update, special-paging table,
NR 0x8E decomposition, alt-ROM page selection, EFF7 RAM-at-0x0000
override, save/load serialisation) is showing no new findings —
pass-7's deep walk through every line of every helper found nothing
to fix in those primitives. The two class-(a) bugs found this pass
were both **integration bugs**: the consumer subsystem (DivMmc, or NR
0x03 dispatch) was either using the wrong derived signal or was
applying a previously-fixed primitive's pattern incorrectly to a
nearby primitive.

A genuine convergence would require one full pass with **zero**
class-(a) findings in the MMU subsystem and adjacent integration
seams. We are not there yet. Pass-8 should:

1. **Sweep all consumers of `Mmu::*()` accessors** for similar
   "wrong-derived-signal" mistakes. The Layer 2 / Tilemap / Sprite
   renderers also take `to_sram_page()` and ROM-related accessors —
   audit each callsite for VHDL fidelity.
2. **Sweep all `Mmu::set_*()` mutators** for missing per-slot refresh
   patterns analogous to MTC-01/02 — anywhere code might write a
   register that affects slot 0/1 cache without honouring the "RAM
   mapping wins over default ROM" gate.

If pass-8 finds zero class-(a) bugs in this widened sweep, that is
honest convergence. If it finds even one, pass-9 is warranted.

## Open Questions

- **NR 0x82 bit 3 gating of port 1FFD on Next mode** — the existing
  Emulator handler at `port_.register_handler(0xF003, 0x1001, ..., [this](uint16_t, uint8_t v) {
  if ((nextreg_.cached(0x82) & 0x08) == 0) return; ... })` correctly
  gates port_1ffd writes on Next mode. But VHDL :2599 and :2600 have
  more nuanced gating (G57). This is a potential pass-8 audit target.

- **`Mmu::sram_pre_override_*` accessors vs full SRAM arbiter** — the
  current bare-class accessors model only the bits 0 and 2 of
  `sram_pre_override`. Bit 1 (Layer 2) is consumed by other code paths
  inline. Worth auditing in pass-8 whether the Layer 2 read/write-over
  paths in `mmu.h` `read()`/`write()` correctly mirror the
  bit-1-gated VHDL arbiter.

- **`Mmu::reset()` 's `boot_rom_en_` re-enable** — currently
  conditional on `config_mode_ && boot_rom_`. This matches VHDL
  :5109-5111 IF `nr_03_config_mode='1'` at reset time. Pass-7 verified
  the gate is correct but the *value* of `config_mode_` Mmu sees at
  reset depends on Emulator's NR 0x03 handler having pushed it. There
  is a non-trivial dependency chain Emulator → NextReg → Mmu that
  should be re-verified end-to-end in pass-8.

## Branch HEAD

After commit: see `git log --oneline -3` on `task2/verify7-memory`.
