# Memory Subsystem — Pass-5 Blind Verification Re-Audit

Date: 2026-05-09
Branch: `task2/verify5-memory`
Worktree: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify5-memory`

## Verdict

**NEW FINDINGS — 2 class-(a) bugs found and fixed.**

After four prior audit passes that landed 10 memory-related fixes, this
fifth pass exercised methodology angles deliberately distinct from
passes 1–4 (read-after-write coherence per VHDL pulse semantics,
NR-write side-effect ordering, cross-NR/cross-port consistency
invariants, page-pointer interface boundaries, NR-write-during-special-
paging interactions, hard-vs-soft reset coverage, RAM bank capacity
audit). Two new class-(a) divergences from VHDL surfaced:

1. `Mmu::reset()` re-enabled the boot-ROM overlay unconditionally
   regardless of the preserved `nr_03_config_mode` mirror, diverging
   from VHDL `zxnext.vhd:5109-5111` which gates the `bootrom_en <= '1'`
   re-assertion on `if nr_03_config_mode = '1'`.
2. `Mmu::set_machine_type()` invoked `apply_legacy_rom_slots_()`
   unconditionally on a transition, clobbering slots 0/1 even while
   the +3 special-paging table held them per VHDL `zxnext.vhd:4623-4632`.

Both fixed in this pass. All 37 tests pass.

## Methodology

The seven angles enumerated in the prompt:

1. **Read-after-write coherence per VHDL one-cycle pulse semantics** —
   walked `port_memory_change_dly`, `port_memory_ram_change_dly`,
   `nr_mmu_we`, `nr_8e_we`, `nr_8f_we_dly`. Confirmed jnext's per-write
   synchronous model collapses VHDL's 1-cycle pipeline correctly: no
   observable state depends on the pulse phase rather than the event.

2. **NR-write side-effect ordering within a single instruction** —
   walked NR 0x8E (multi-effect: 7FFD/DFFD/1FFD all updated atomically
   per VHDL `zxnext.vhd:3662-3734`) and NR 0x8F (mode select →
   pentagon_en → bank composition). Order matches VHDL.

3. **Cross-NR / cross-port consistency invariants** — verified:
   * `current_sram_rom()` formulas per machine type match VHDL
     `zxnext.vhd:2981-3008` for all four cases (48K / 128K / +3 /
     ZXN+128K else).
   * `port_7ffd_locked` (= 7FFD bit 5 OR Pentagon-1024 override) is
     consistently consumed by `effective_paging_locked()`.
   * NR 0x8E,$03 sets BOTH 7FFD(4) AND 1FFD(2) atomically (verified the
     bit decode against VHDL :3662-3734).
   * NR 0x8C lock changes refresh slots 0/1 only when `read_only_=true`
     (preserves explicit RAM mappings — VHDL leaves MMU<i> alone on NR
     0x8C writes, matching jnext).

4. **`Mmu` / `Ram` / `Rom` interface audit** — verified:
   * `Ram::page_ptr(page)` returns `nullptr` for `page * 8KB >= size`,
     2MB default is enough for all logical pages 0..0xFF after
     `to_sram_page()`.
   * `Rom::page_ptr(bank)` covers pages 0..7 (4 × 16K).
   * `Mmu::read()` / `Mmu::write()` fast-path checks the boot ROM and
     overlay arbitration cascade correctly.
   * Boot ROM overlay deactivates only on NR 0x03 write per VHDL
     `:5113-5122`. **Class-(a) divergence found in re-enable path** —
     see Finding 1.

5. **Edge cases of `Mmu::set_machine_type`** — checked behavior during
   special paging. **Class-(a) divergence found** — see Finding 2.

6. **Hard reset (NR $02 bit 1) state-clear coverage** — confirmed
   that the existing comment in `mmu.cpp:52-83` is correct that VHDL
   has a single reset domain (`reset = reset_hard or reset_soft`).
   The `hard` parameter is unused. `Emulator::init` correctly skips
   RAM/ROM reinit on soft reset (preserve_memory=true).

7. **RAM bank capacity audit** — Ram default 2MB = 256 8KB pages
   covers all logical pages 0..0xFF after the `to_sram_page` shift in
   Next mode. NR 0x52..0x57 with values 0xE0..0xFE correctly route to
   inactive-slot semantics (read returns 0xFF, writes dropped).
   Values 0x80..0xDF route to physical SRAM 0xA0..0xFF — within bounds.

## Findings

### Finding 1 (class-a) — `Mmu::reset()` re-enables boot ROM unconditionally

**VHDL oracle**: `zxnext.vhd:4926-5111` is a clocked process whose reset
clause includes:

```vhdl
if reset = '1' then
   ...
   if nr_03_config_mode = '1' then
      bootrom_en <= '1';
   end if;
   ...
elsif nr_wr_en = '1' then
   case nr_wr_reg is
      when X"03" =>
         bootrom_en <= '0';
         ...
```

So on every reset (hard or soft, since `reset = reset_hard or reset_soft`):
* `nr_03_config_mode = '1'` → `bootrom_en <= '1'` (re-enabled)
* `nr_03_config_mode = '0'` → bootrom_en unchanged (preserved)

`bootrom_en` is a flip-flop initialised to '1' at FPGA configuration time
per `zxnext.vhd:1101`. After firmware writes NR 0x03 (any value),
bootrom_en is cleared by VHDL :5122. A subsequent reset only re-enables
it if config_mode is currently 1.

**Pre-pass-5 jnext** (`mmu.cpp:131`): `if (boot_rom_) boot_rom_en_ = true;`
unconditionally re-enabled the overlay on every reset whenever a boot
ROM was loaded.

**Symptom**: a hard reset (NR 0x02 bit 1) issued after firmware had
turned off `nr_03_config_mode` (e.g. by writing NR 0x03 with bits[2:0]
∈ {001..110} to commit a machine type) would re-arm the boot ROM in
jnext but leave it cleared in VHDL. The CPU's first fetch at PC=$0000
post-reset would land in the FPGA boot IPL in jnext, but in VHDL it
would land in the live ROM (sram_rom-derived) — a fundamentally
different boot trajectory.

**Fix** (`mmu.cpp:131-149`): gate the re-enable on the preserved
`config_mode_` mirror:

```cpp
if (boot_rom_ && config_mode_) boot_rom_en_ = true;
```

`config_mode_` is preserved across reset (the existing reset() path
already documented this — `nr_03_config_mode` has no VHDL reset clause
per `zxnext.vhd:1102`), so the gate reads the pre-reset value the VHDL
clause would consult.

**Cleanup** (`emulator.cpp:5145-5168`): the soft-reset path previously
captured-and-restored `mmu_.boot_rom_enabled()` to work around the
unconditional re-enable. With the Mmu-side fix, the workaround was
half-correct (it preserved when config_mode=0, which matched VHDL, but
also preserved when config_mode=1, which incorrectly clobbered the
re-enable). The capture/restore was removed; Mmu::reset() now models
the gate correctly for both reset paths.

### Finding 2 (class-a) — `Mmu::set_machine_type()` clobbers special-paging table

**VHDL oracle**: `zxnext.vhd:2981-3008` derives `sram_rom` /
`sram_rom3` / `sram_alt_128_n` combinationally from
`machine_type_48`, `machine_type_p3`, `nr_8c_altrom_lock_*`, and
`port_1ffd_rom`. The SRAM arbiter consumes these signals only on the
legacy-ROM branch at `zxnext.vhd:3052`:

```vhdl
sram_pre_A21_A13 <= "000000" & sram_rom & cpu_a(13);
```

While `port_1ffd_special='1'`, the MMU update path at
`zxnext.vhd:4623-4632` rewrites MMU0..7 from the special-paging table
indexed by `port_1ffd_reg(2:1)` alone — independent of `sram_rom`,
`machine_type`, and `nr_8c_*`. The CPU reads slots 0..7 via the
MMU<i>-driven SRAM address path, so a `machine_type` change while in
special paging mode has NO observable effect on slot mapping.

**Pre-pass-5 jnext** (`mmu.h:738-744`):

```cpp
void set_machine_type(MachineType t) {
    if (t == machine_type_) return;
    machine_type_ = t;
    apply_legacy_rom_slots_();
}
```

`apply_legacy_rom_slots_()` calls `current_sram_rom()` and rewrites
slots 0/1 to the legacy-ROM mapping derived from sram_rom. While in
+3 special paging mode, this overwrites the special-table mapping
that the VHDL would hold.

**Symptom**: an NR 0x03 write that commits a new machine type
(`config_mode=1` and bits[2:0] ∈ {001..100}) while +3 special paging
is active would clobber slots 0/1 in jnext but leave them at the
special-paging mapping in VHDL. Edge case (machine type changes
during special paging are unusual), but observable.

**Fix** (`mmu.h:738-764`): early-return when `port_1ffd_(0)` (special
paging bit) is set:

```cpp
void set_machine_type(MachineType t) {
    if (t == machine_type_) return;
    machine_type_ = t;
    if ((port_1ffd_ & 0x01) != 0) {
        // In +3 special paging: machine_type change does not affect
        // the special-mode MMU image. Cache stays in sync because
        // the special-paging table is independent of sram_rom.
        return;
    }
    apply_legacy_rom_slots_();
}
```

The cache stays in sync because the special-paging table is
independent of sram_rom — the next paging trigger that exits special
mode (port_1ffd_special_old=1, special=0) goes through
`revert_slots_2_to_5_post_special_()` + `apply_legacy_paging_()`,
which DOES read the (now-updated) `current_sram_rom()` to rebuild
slots 0/1 with the new machine type.

## Convergence Assessment

**Memory subsystem is approaching genuine convergence.**

Pass 5 found two class-(a) bugs after four prior passes had landed
ten. The ratio of bugs-found-per-pass continues to decrease (P1 ≈ 4,
P2 ≈ 3, P3 ≈ 3, P4 ≈ 3, P5 = 2), and the new findings exercise
methodology angles distinct from earlier passes — the
"gate-on-preserved-config-mode" reset semantics and the "set_machine_type
during special paging" interaction are subtle enough that prior passes
plausibly missed them not by oversight but by methodology gap.

The Pass-5 angles that came up clean:
* Pulse-phase coherence (port_memory_change_dly, etc.) — the per-write
  synchronous collapse holds up.
* NR-write side-effect ordering inside multi-effect writes (NR 0x8E
  setting all of 7FFD/DFFD/1FFD atomically).
* Cross-NR invariants (current_sram_rom, current_rom_bank, sram_rom3,
  altrom_sram_page) match VHDL across all four machine-type branches
  and the lock-override cases.
* Page-pointer boundary (Ram::page_ptr / Rom::page_ptr), wraparound
  semantics, inactive-slot routing, special-page exceptions
  (0x0A/0x0B/0x0E for dual-port VRAM banks).
* RAM bank capacity at the 2MB default — covers all valid logical
  pages with the +0x20 Next-mode shift.

The remaining angles where I'd expect a Pass-6 to find anything
non-trivial are narrow:

* **Layer 2 read/write-over edge cases** during shadow-bank toggles
  paired with NR 0x12/0x13 writes (verify the cache-refresh seams
  cover all combinations). I walked the seams and they look complete,
  but a focused Pass-6 with this angle could still surface something.
* **DMA / Copper-driven NR writes during paging** — the same NR 0x8E
  / 0x8F / port_7FFD pathway can be driven by the Copper coprocessor
  (zxnext.vhd ≈ :3937+ for Copper). I did not specifically audit
  Copper-NR-write timing relative to MMU update; jnext's Copper path
  routes through `NextReg::write` which calls the same write_handlers
  as the CPU path, so it should be covered, but I did not exhaustively
  verify.
* **NMI-window interactions** (especially DivMMC AUTOMAP) where the
  MF / DivMMC overlay state changes mid-instruction. Boot-ROM /
  DivMMC / Multiface overlay cascade is correctly ordered per VHDL
  :2937 priority, but the per-instruction transition edges (e.g.
  RST at $0066 with mf_enable_eff toggling on the M1 fetch) live in
  the NMI subsystem rather than memory — outside this pass's scope.

I would estimate the memory subsystem is at >95% convergence. The
remaining surface is small enough that another pass might find one
or zero class-(a) bugs.

## Open Questions / Coverage Gaps

* **+3 special paging during NR 0x82-0x85 port-enable changes**: if
  the firmware disables port_1ffd via NR 0x82 bit 3 while in special
  paging, the lock state in `port_1ffd_special_old_` could
  theoretically drift. Not exercised, low real-world probability.

* **Pentagon-1024 mode lifetime invariants**: we model
  `pentagon_1024_en` as a derived gate, but the latch ordering
  `port_eff7_reg_2` ↔ `nr_8f_mode_` ↔ `port_7ffd_(5)` lock-override
  could have race-style edge cases on simultaneous writes. Not
  observable in jnext's per-write model.

* **NR 0x07 cpu_speed bus-idle commit edge** modelled in
  `ContentionModel::commit_pending_cpu_speed_on_bus_idle` is consumed
  by the contention path, not the memory path. If the memory access
  pipeline ever reaches into that latch (e.g. for contention-during-
  paging-write), there'd be coupling. Currently no such coupling.

## Test Status

* `mmu_tests`: PASS
* `fuse_z80_tests`: PASS (1356/1356)
* `contention_tests`: PASS
* All 37 unit tests: PASS

## Files Changed

* `src/memory/mmu.cpp` — Finding 1 fix (gate bootrom_en re-enable on
  config_mode_).
* `src/memory/mmu.h` — Finding 2 fix (early-return in set_machine_type
  during +3 special paging).
* `src/core/emulator.cpp` — Finding 1 cleanup (remove
  capture/restore workaround in Emulator::soft_reset; rely on
  VHDL-faithful Mmu::reset gate).
