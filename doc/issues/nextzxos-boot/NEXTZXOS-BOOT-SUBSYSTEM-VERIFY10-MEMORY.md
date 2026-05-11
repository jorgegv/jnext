# Verify-10 Memory Subsystem — Convergence-Test Pass

**Branch**: `task2/verify10-memory`
**Worktree**: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify10-memory`
**Pass goal**: tenth-pass blind audit; convergence test for "zero pending bugs of any class"
**VHDL oracle**: `cores/zxnext/src/zxnext.vhd` (no prior verify reports consulted)

---

## Verdict

**NEW FINDINGS — 1 class-(c) fixed; 3 class-(c) corners catalogued (no fix this pass).**

Convergence not yet at strict zero-pending. One real VHDL-faithfulness gap was
fixed (Layer 2 ROM-area inactive gate). Three additional class-(c) corner cases
were identified but left unfixed because they live outside the Mmu surface
proper (port-dispatch ordering, runtime machine-timing tracking) or model
sub-cycle hardware behaviour that the per-write granularity model cannot
faithfully capture without architectural change. None block any current boot
target — the Mmu hot-path read/write semantics are now VHDL-faithful within
the Mmu's responsibility envelope.

| Bucket | Count | Notes |
|--------|-------|-------|
| class-(a) | 0 | none found |
| class-(b) | 0 | none found |
| class-(c) | 1 (fixed) + 3 (catalogued) | see below |
| class-(d) | 0 | none escalated |

---

## What was spot-checked (audit transparency)

Pass-10 walked five angles named in the prompt:

1. **Differential against fresh VHDL signals**: enumerated every signal in
   `zxnext.vhd` whose name starts with `mmu_*`, `sram_*`, `port_7ffd*`,
   `port_1ffd*`, `port_dffd*`, `port_eff7*`, `nr_8c*`, `nr_8e*`, `nr_8f*`,
   `nr_03*`, `nr_04*`, `nr_50_..._57*`, `MMU0..7`, `bootrom*`, `altrom*`,
   plus the SRAM arbiter (`sram_pre_*`, `sram_layer2_*`, `sram_altrom_*`,
   `sram_romcs*`, `sram_active`, `sram_bank5/7`, `sram_rdonly`,
   `sram_pre_override`). Cross-checked each against C++ counterpart in
   `mmu.h`/`mmu.cpp` and the `Emulator::install_port_handlers` /
   `install_nextreg_handlers` glue.

2. **Boundary inputs at $00 / $7F / $80 / $FF / wrap-around** for every NR
   write that touches paging:
   - NR $50/$51 with `0xFF` — re-engages legacy ROM (verified
     `engage_legacy_rom_paging_slot()` matches VHDL `nr_mmu_we` at lines
     4686-4699 + arbiter routing through line 3052).
   - NR $50/$51 with `0xE0..0xFE` — stored verbatim in MMU<i>;
     `rebuild_ptr` correctly nullifies pointer for slots 2-7 and routes
     slot 0/1 through legacy ROM (matches VHDL :3061 sram_pre_active=0
     and :3052 sram_pre_A21_A13 fallthrough).
   - NR $52..$57 with `0xFF` — `set_page(i, 0xFF)` correctly nullifies
     pointers (matches VHDL :3061).
   - NR $8E with bit 3=0 — `write_nr_8e()` correctly suppresses MMU6/7
     update (matches VHDL :3814 `port_memory_ram_change_dly = NOT (nr_8e_we
     AND NOT nr_wr_dat(3))` plus the OR with `port_1ffd_special_old`).
   - NR $8C bit changes (altrom locks) — `set_nr_8c()` per-slot rebuild
     only when `read_only_[slot]` (matches VHDL combinational `sram_rom`
     selection + the no-port_memory_change_dly-pulse semantics from
     :2981-3008 and :3813).
   - port 0xEFF7 bit 3 toggle — `apply_paging_update_()` honours
     port_1ffd_special arbitration AND the EFF7(3) RAM-at-0x0000 override
     at :4636-4644.
   - NR $8F mapping mode — `pentagon_en()` / `pentagon_1024_en()` /
     `effective_paging_locked()` match VHDL :3798-3801 / :3769 exactly.

3. **Multi-state interactions**:
   - machine_type × port_1ffd / port_7ffd × NR $8C lock — `current_sram_rom()`
     and `sram_rom3()` decode every machine-type branch matching VHDL
     :2981-3008 (48K hardwired sram_rom=00, +3 with/without lock, ZXN/128K
     with/without lock).
   - MF overlay × DivMMC × Layer 2 × config_mode × normal — read/write
     cascade priority in `Mmu::read()`/`write()` matches VHDL arbiter
     order at :2937 and the per-half override gate in
     `l2_overlay_active_for()` matches :3043/:3050/:3057/:3065.
   - +3 special paging × machine_type change — `set_machine_type()` early-
     returns when special bit set (matches VHDL: special-paging table at
     :4623-4632 is independent of sram_rom).
   - cold/soft/hard reset preservation — `reset(bool hard)` parameter is
     now ignored (corrected G46(b) 2026-05-08 per VHDL `reset='1'` covers
     both domains); confirmed all "preserved" fields (`nr_8f_mode_`,
     `p3_floating_bus_dat_`) match VHDL no-reset signals; confirmed all
     cleared fields (`port_7ffd_`, `port_1ffd_`, `port_dffd_reg_`,
     `port_eff7_*`, `paging_locked_`, `contention_disabled_`,
     `port_1ffd_special_old_`, L2 latches) match VHDL :3646-3648,
     :3713-3716, :3686-3690, :3777-3779, :4935, :3907-3913.
   - bootrom_en re-arm gate — `if (boot_rom_ && config_mode_) boot_rom_en_ = true;`
     matches VHDL :5109-5111 exactly (re-arm only when nr_03_config_mode='1'
     at the reset edge).

4. **Walked every public Mmu function**:
   `set_boot_rom`, `set_page`, `get_page`, `get_effective_page`,
   `is_slot_rom`, `slot_in_rom_area`, `sram_pre_override_*`, `set_l2_*`,
   `set_machine_type`, `current_sram_rom`, `sram_rom3`, `current_rom_bank`,
   `rom3_selected`, `map_rom`, `engage_legacy_rom_paging`,
   `engage_legacy_rom_paging_slot`, `engage_legacy_ram_paging`,
   `map_128k_bank`, `map_plus3_bank`, `write_port_dffd`, `write_port_eff7`,
   `write_nr_8f`, `write_nr_8e`, `read_nr_8e`, `unlock_paging`,
   `set_contention_disabled`, `set_nr_8c`, `set_p3_floating_bus_dat`,
   `set_slot_contended`, `to_sram_page`, `set_l2_active_bank`,
   `l2_port_readback`, `set_port_7ffd_bit3`, `shadow_screen_en`,
   `effective_paging_locked`, `pentagon_en`, `pentagon_1024_en`,
   `port_eff7_ram_at_0000`, `port_eff7_disable_p1024`. All correct against
   VHDL.

5. **Save/load state final coverage**: enumerated all 30 member fields
   in `Mmu` private section. All runtime-relevant state is round-tripped
   via `save_state()`/`load_state()`. `boot_rom_buf_` / `boot_rom_` /
   `boot_rom_size_` are correctly excluded (configuration data, not state).
   `read_ptr_` / `write_ptr_` are excluded and rebuilt via
   `rebuild_ptr()` after load. Order in save matches order in load. No
   missing state.

---

## Findings

### F1 — class-(c) — Layer 2 ROM-area `sram_active` gate not honoured (FIXED)

**VHDL**: `zxnext.vhd:2971` + `:3101-3102`. The Layer 2 SRAM arbiter
computes `layer2_A21_A13(8) = '1'` when
`layer2_active_page(7:5) = "111"`, which after the
`("0001" + ('0' & layer2_active_page(7 downto 5)))` carry resolves to:

```
layer2_active_page(7:5) = sum[6:4]   where sum = bank + bank_offset
=> sum & 0x70 == 0x70
=> sum ∈ {0x70..0x7F, 0xF0..0xFF}
```

When the gate fires, the arbiter at `:3101-3102` emits:
```vhdl
elsif sram_layer2_map_en = '1' then
   sram_A21_A13 <= '0' & sram_pre_layer2_A21_A13(7 downto 0);
   sram_active  <= not sram_pre_layer2_A21_A13(8);   -- '0' here
```

`sram_active='0'` means the SRAM does NOT respond. Reads return floating
bus (0xFF in practice via the cpu_di MUX at `:1864-1865`); writes are
dropped (`sram_req` requires `sram_active`).

**Pre-fix C++** (`mmu.h:261-281` / `:408-424`):
```cpp
if (l2_read_enable_ && l2_overlay_active_for(addr)) {
    const uint8_t bank   = l2_map_shadow_ ? l2_shadow_bank_ : l2_bank_;
    const uint8_t off_pre = l2_offset_pre_for(addr);
    const uint8_t bofs   = static_cast<uint8_t>(off_pre + l2_offset_);
    uint16_t l2_page = static_cast<uint16_t>((bank + bofs) * 2);
    uint8_t phys_page = to_sram_page(static_cast<uint8_t>(l2_page | ((addr >> 13) & 1)));
    const uint8_t* p = ram_.page_ptr(phys_page);
    uint8_t val = p ? p[addr & 0x1FFF] : 0xFF;
    ...
}
```

The C++ unconditionally computed `phys_page = to_sram_page(...)`. For
`sum >= 0x70` the `to_sram_page()` `+0x20` shift wraps:
- `bank+bofs = 0x70`, `cpu_a(13)=0` → logical L2 page = `0xE0`
  → `to_sram_page(0xE0) = 0xE0 + 0x20 = 0x00`. SRAM page 0x00 is
  **ROM-in-SRAM bank 0**. A WRITE here would corrupt the live ROM bytes
  the supervisor relies on.
- `bank+bofs = 0x77`, `cpu_a(13)=1` → logical L2 page = `0xEF`
  → `to_sram_page(0xEF) = 0x0F`. SRAM page 0x0F is bank 7 hi.
  A read pulls back unrelated CPU/ULA data.

**Reachability**: real-world firmware (NextZXOS, demos) keeps NR $12 /
NR $13 at banks 8-19, with offsets 0-10. `sum = bank + bofs` is at most
~30 (= 0x1E) — never inside the 0x70..0x7F / 0xF0..0xFF window. So no
known software hits this corner. But the gate is VHDL-spec; honouring it
is essentially free.

**Fix**: added the `(sum & 0x70) == 0x70` early-out in both read() and
write() L2 paths in `mmu.h`. Read returns 0xFF (floating-bus mirror);
write silently dropped. Both branches fully comment-cited the VHDL line
references. Verified: `mmu_test` (202 cases, 180 pass / 0 fail / 22
skip — the skip distribution is unchanged from pass-9), `mmu_integration_test`,
`fuse_z80_test` (1356/1356), `contention_test` all green.

---

### F2 — class-(c) — port `0x3FFD` on 128K timing routes to FDC stub instead of port_7ffd (NOT FIXED — port-dispatch architecture)

**VHDL**: `zxnext.vhd:2593` (port_7ffd decode) + `:2601-2602` (port_2ffd /
port_3ffd decode):

```vhdl
port_7ffd  <= '1' WHEN cpu_a(15)='0' AND (cpu_a(14)='1' OR NOT p3_timing_hw_en)
                       AND port_fd='1' AND port_1ffd='0' AND port_7ffd_io_en='1';
port_2ffd  <= '1' WHEN cpu_a(13:12)="10" AND port_xffd='1' AND nr_d8_io_trap_fdc_en='1';
port_3ffd  <= '1' WHEN cpu_a(13:12)="11" AND port_xffd='1' AND nr_d8_io_trap_fdc_en='1';
```

On 128K timing (`p3_timing_hw_en='0'`), the `(cpu_a(14)='1' OR NOT p3_timing)`
clause is TRUE for any A14, so `port_7ffd` matches at addresses like
0x3FFD (A14=0). `port_2ffd` / `port_3ffd` are gated by
`nr_d8_io_trap_fdc_en='1'` — they don't decode at all when the FDC trap is
disabled (default on 128K). Writes to 0x3FFD on 128K go to port_7ffd_wr.

**Pre-existing C++** (`emulator.cpp:2601` and `:2697-2724`):
- 0x7FFD handler registered with mask `0x8003` / value `0x0001`.
- 0x3FFD handler registered with mask `0xF003` / value `0x3001` (FDC iotrap).

`port_dispatch.cpp` uses **most-specific-mask-wins** dispatch
(`port_dispatch.cpp:35-85`). For port 0x3FFD, both masks match, but the
FDC mask (12 bits set) is more specific than the 7FFD mask (3 bits set).
The FDC handler wins. Inside the handler, `nr_d8_io_trap_fdc_en_` gates
the iotrap; when off, the handler returns silently. The 7FFD handler
never runs. Result: 0x3FFD writes on 128K timing are **silently dropped**
in jnext, but should bank-switch per VHDL.

**Reachability**: 128K software historically uses 0x7FFD for paging. Some
diagnostic tools / demos may use 0x3FFD relying on the original 128K's
sloppy decode. None of the supported boot targets (NextZXOS, demo NEX
files) exercises this.

**Why not fixed this pass**: this is a port-dispatch ordering issue, not
a Mmu-surface issue. The fix would require either (a) gating the FDC
handler registration on `nr_d8_io_trap_fdc_en_` so it doesn't match when
disabled, or (b) reworking `port_dispatch` to allow handlers to return
"didn't match" so dispatch falls through. Option (a) is fragile (handler
registration is one-shot at init, the gate is dynamic); option (b) is
a generic dispatcher overhaul. Neither belongs in a memory-subsystem fix.

Logged as class-(c) for future port-dispatch architectural review.

---

### F3 — class-(c) — `port_1ffd_special_old` back-to-back capture window not modelled (NOT FIXED — sub-cycle architectural)

**VHDL**: `zxnext.vhd:3713-3742`. The `port_1ffd_special_old` flip-flop is:
- captured to `port_1ffd_special` ONLY on a port_1ffd_wr or nr_8e_we cycle,
  AND ONLY when the previous-cycle `port_memory_change_dly='0'` (i.e.
  there was no paging trigger on the immediately-prior 28 MHz cycle);
- assigned `'0'` on every other cycle (the else branch at `:3738`).

Then the MMU update process at `:4677` reads `port_1ffd_special_old` only
when `port_memory_change_dly='1'` (the cycle FOLLOWING a trigger). Most
realistic scenarios (Z80 OUT at ≥4 T-states, 7 MHz CPU) leave the dly
signal long enough between writes that the capture always succeeds.

**Sub-cycle gap**: the **copper coprocessor** can write NextREG at
1 28-MHz cycle per MOVE. Two consecutive copper MOVEs to NR $8E (an
extreme but legal program) hit the 28 MHz cycle gap where the second
write's capture is BLOCKED by `port_memory_change_dly='1'` (still
asserted from the first write). VHDL: the second write doesn't update
`port_1ffd_special_old`. jnext: `apply_paging_update_()` at the end of
`write_nr_8e()` updates `port_1ffd_special_old_ = special;` regardless.

For the back-to-back enter-then-exit-special copper sequence:
- VHDL: second cycle leaves `port_1ffd_special_old='0'` (prior value);
  next MMU update sees old=0, new=0 → no revert.
- jnext: between the two writes, `port_1ffd_special_old_=1` (set after
  first write); second write fires `revert_slots_2_to_5_post_special_()`
  inappropriately.

**Reachability**: requires a copper program that writes NR $8E twice in
adjacent 28 MHz cycles, toggling bit 2 (port_1ffd_special). No known
software does this. The copper is rarely used for paging; usually it
drives palette/scroll registers.

**Why not fixed this pass**: faithful modelling would require tracking
the dly signal as a per-28-MHz-cycle flag, which contradicts the
per-write-trigger granularity model. The fix would be architectural —
either model 28 MHz cycles explicitly for the paging registers, or add
a "back-to-back trigger" bypass flag set by every paging write and
cleared after one CPU instruction boundary.

Logged as class-(c). Documented as architectural follow-up if/when the
copper paging-write feature becomes a target.

---

### F4 — class-(c) — port_7ffd `(A14='1' OR NOT p3_timing)` gate uses static `config_.type` instead of runtime `machine_timing` (NOT FIXED — runtime-timing architecture)

**VHDL**: `zxnext.vhd:2593`:
```vhdl
port_7ffd <= '1' WHEN cpu_a(15)='0' AND (cpu_a(14)='1' OR NOT p3_timing_hw_en)
                  AND port_fd='1' AND port_1ffd='0' AND port_7ffd_io_en='1';
```

`p3_timing_hw_en` is a registered version of `machine_timing_p3`
(`zxnext.vhd:2457`), which itself decodes from `eff_nr_03_machine_timing`
(`:5773-5774`, value "11") — runtime-mutable via NR $03 timing field.

**Pre-existing C++** (`emulator.cpp:2605`):
```cpp
if (config_.type == MachineType::ZX_PLUS3 && (port & 0x4000) == 0) return;
```

Uses `config_.type`, a static EmulatorConfig field set at init that
never updates. So a Next machine that boots with 128K timing then
commits +3 timing via NR $03 still has the gate computed from the
**original** config_.type (e.g., ZXN_ISSUE2), causing the gate to fire
the +3 A14=1 requirement only for ZX_PLUS3 builds — not for runtime
+3-timing transitions on a Next.

**Reachability**: a Next firmware that runtime-switches into +3 timing
mode (NextZXOS does NOT do this; its plus3 mode boots with config.ini
already set to +3, so config_.type=ZX_PLUS3 from start). Demos that
flip timing post-boot would expose this.

**Why not fixed this pass**: the fix would re-route the dispatch gate
through `nextreg_.nr_03_machine_timing()` (already tracked in jnext per
`:1899` `set_nr_03_machine_timing`). The 7FFD handler runs hot — adding
a NextReg field read per port-write is fine perf-wise. But the same
issue applies symmetrically across many port handlers (NMI hotkey
gating, line-int scheduling, etc.) and a full audit of every place
`config_.type` is consulted-versus-runtime-machine-timing-tracked is
broader than the memory subsystem. Logged for the cross-cutting
machine-timing follow-up.

---

## Test status

```
$ cd /home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify10-memory
$ LANG=C cmake --build build -j$(nproc)
[100%] Built target jnext   (clean build, 0 warnings)

$ LANG=C ctest --test-dir build --output-on-failure
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   1.34 sec

$ LANG=C ctest --test-dir build -R 'mmu|fuse_z80|contention'
100% tests passed, 0 tests failed out of 4
```

mmu_test: 202 cases / 180 pass / 0 fail / 22 skip — distribution unchanged from pass-9.
fuse_z80_test: 1356 / 1356 (100%).

---

## Convergence verdict

The Mmu **proper** (everything that lives behind `class Mmu`'s public
surface) is now VHDL-faithful within the per-write-granularity model.
The L2 ROM-area gate fix closes the only behaviour-divergent gap on the
hot read/write path.

The three remaining class-(c) findings (F2, F3, F4) span:
- **port-dispatch architecture** (F2) — not Mmu's responsibility;
- **sub-28-MHz-cycle modelling** (F3) — fundamentally outside the
  per-write-trigger granularity model;
- **runtime machine-timing tracking** (F4) — cross-cutting, applies to
  many subsystems beyond Memory.

Strict-zero-pending across all-class is **not** achieved this pass for
honesty's sake — the three corners are real and reachable in extreme
configurations. They are catalogued at class-(c) for their respective
architectural follow-ups; none is a class-(a) emulation correctness bug
on supported boot targets, and none is a class-(b) or class-(d) escalation.

Within the Mmu surface only, the audit converges at zero pending of any
class.

---

## Files touched

- `src/memory/mmu.h` — added L2 ROM-area gate in read() (line ~273) and
  write() (line ~422). Verbatim VHDL line citations in code comments.
