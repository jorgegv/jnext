# Memory Subsystem Verification Re-Audit

**Date:** 2026-05-09
**Branch:** `task2/verify-memory`
**Scope:** `src/memory/mmu.{h,cpp}`, `src/memory/ram.{h,cpp}`,
`src/memory/rom.{h,cpp}`, `src/memory/contention.{h,cpp}`, plus the
NR-write dispatch in `src/core/emulator.cpp` for NR `$03/$07/$08/$50-$57/$82-$84/$8C/$8E/$8F`
and ports `$7FFD/$1FFD/$DFFD/$EFF7/$123B`.

**Methodology.** Blind re-audit (no prior analysis docs read).
VHDL oracle walked: `zxnext.vhd` lines 882-895 (paging-trigger signals),
1099-1115 / 1226-1235 / 1278-1303 / 1378-1380 (NR 0x03 / 0x07 / 0x08 /
0x82-0x85 power-on defaults), 2247-2265 (NR 0x8C latch), 2392-2413
(internal_port_enable fan-out), 2961-3008 (sram_rom / sram_rom3 /
sram_alt_128_n combinational selection), 3010-3138 (SRAM arbiter +
priority cascade), 3193-3220 (boot ROM), 3640-3742 (port 7FFD/DFFD/1FFD
register processes + NR 0x8E lock-bypass elsifs), 3744-3762
(port_1ffd_mtr_n), 3768-3801 (port_7ffd_locked / Pentagon mapping mode),
3805-3818 (port_memory_change_dly / port_memory_ram_change_dly),
3904-3935 (port 0x123B Layer 2 latches + read-back), 4498-4509
(p3_floating_bus_dat), 4481-4496 (contention enable + memory/port
contend), 4607-4700 (MMU0..7 process), 5052-5057 (NR 0x82-0x84 reset
gate), 5124-5151 (NR 0x03 timing/type/config_mode FSM), 5176 (NR 0x08
contention_disable shadow), 5741-5757 (machine_type_48/128/p3 decode),
5783-5828 (cpu_speed shadow + bus-idle commit + eff_nr_08_contention
hc(8) commit), 5894-6162 (port_253b_dat read-mux for NR 0x03/0x07/0x08/
0x12/0x50-0x57/0x68/0x8E/0x8F), 6370 (nr_02_soft_reset).

**What was spot-checked beyond a typical memory audit:**
- Soft-reset preservation list (`Mmu::reset(hard)`) cross-checked
  against every "if reset='1'" clause in the VHDL paging-register
  processes — already correct (`hard` parameter unused, comment in
  source documents the rationale).
- `port_1ffd_special_old` capture-on-change semantics (VHDL :3720 vs
  :3729 vs :3738) reconciled against C++ sticky-bool model — end-state
  matches across special→normal and normal→special transitions in the
  paging-update arbiter (`apply_paging_update_`).
- `+3 special paging` table (VHDL :4625-4632) decoded bit-by-bit and
  matched against `Mmu::apply_plus3_special_paging_` C++ table —
  verified algebraically for all four cfg states.
- `compose_bank_()` Pentagon vs non-Pentagon paths checked against
  VHDL :3763-3766; bank composition fits in uint8_t for max DFFD value.
- `contention.cpp` window gate (zxula.vhd hc_adj wrap) and per-machine
  mem_active_page contention masks verified against zxnext.vhd:4489-4493.
- `to_sram_page` shift formula (`+0x20` for non-special pages) verified
  algebraically against VHDL `("0001"+page(7:5)) & page(4:0)` for every
  page in `[0x00..0xDF]`; pages `[0xE0..0xFF]` have mmu_A21_A13(8)=1
  (= inactive).
- `nr_03_machine_type` / `nr_03_config_mode` reset preservation across
  hard + soft reset confirmed against VHDL absence of reset clauses for
  signals declared at :1102-1103.
- Layer 2 `port_123b_layer2_map_segment` gating audited carefully
  against VHDL :3037 vs :3060-3065 — found the asymmetry called out as
  Finding 5 below.
- All four paging triggers (port 7FFD / 1FFD / DFFD / EFF7) confirmed
  to invoke `apply_paging_update_` in C++; NR 0x8E and NR 0x8F do too.
  port_memory_ram_change_dly (NR 0x8E bit 3 suppression of MMU6/7
  rebuild) honored.
- `set_l2_port` cpu_do(4) bifurcation (offset-only vs full-update)
  matches VHDL :3915-3923 exactly.

---

## Verdict

**NEW FINDINGS — 5 total (2 class-(a) fixed, 3 class-(b)/(c) noted).**

The post-fix code is largely VHDL-faithful, but two clear divergences
were found and fixed on this branch; three more are class-(b)/(c) noted
in the report.

---

## Findings

### Finding 1 — NR 0x8C lock change does not rebuild slot 0/1 (FIXED, class (a))

**VHDL spec.** `zxnext.vhd:2256-2265` stores `nr_8c_altrom` verbatim,
but the lock bits feed `sram_rom` / `sram_rom3` / `sram_alt_128_n`
through the COMBINATIONAL process at `zxnext.vhd:2981-3008`. Any change
in `nr_8c_altrom_lock_rom1` / `nr_8c_altrom_lock_rom0` is immediately
visible to the SRAM arbiter at `:3052` (`sram_pre_A21_A13 <= "000000" &
sram_rom & cpu_a(13)`).

**Pre-fix jnext behavior.** `Mmu::set_nr_8c(uint8_t v)` was a header
inline that only stored the value:

```cpp
void set_nr_8c(uint8_t v) { nr_8c_reg_ = v; }   // before fix
```

The cached `read_ptr_[0/1]` (set by the last `apply_legacy_rom_slots_`)
remained stale until the next paging-port write forced a rebuild. On
+3 / Next mode, a firmware sequence that flips `altrom_lock_rom1` /
`altrom_lock_rom0` to switch ROM banks WITHOUT writing port 0x1FFD or
0x7FFD would read stale ROM bytes through `read_ptr_[0..1]`.

**Fix.** Move `set_nr_8c` out-of-line in `mmu.cpp` and call
`apply_legacy_rom_slots_()` after the store. Idempotent for unchanged
locks; cheap (two pointer rebuilds).

```cpp
void Mmu::set_nr_8c(uint8_t v) {
    nr_8c_reg_ = v;
    apply_legacy_rom_slots_();
}
```

**Test impact.** `mmu_tests` 202/180 PASS / 0 FAIL / 22 SKIP unchanged.
`ROM-10` (48K hardwires sram_rom3) and `ROM-11` (ZXN/128K altrom-lock
follows lock_rom1) consume `sram_rom3()` (a pure accessor) so the
rebuild does not affect them.

### Finding 2 — Slot 0/1 with NR `$50/$51` value `[0xE0..0xFE]` mis-routed to RAM (FIXED, class (a))

**VHDL spec.** `zxnext.vhd:2964` derives
`mmu_A21_A13 <= ("0001" + page(7:5)) & page(4:0)`, so logical page
`>= 0xE0` (page(7:5) = "111") → `mmu_A21_A13(8) = '1'`. For slot 0/1
(`cpu_a(15:14) = "00"`) the SRAM arbiter at `:3037` takes the
`elsif mmu_A21_A13(8) = '0'` branch ONLY when bit 8 is 0; otherwise it
falls through to `:3044` (config_mode) or `:3052` (legacy ROM —
`sram_pre_A21_A13 <= "000000" & sram_rom & cpu_a(13)`). I.e. ANY
`MMU0/1 ∈ [0xE0..0xFF]` re-engages the legacy ROM path, NOT just the
canonical `0xFF` sentinel.

**Pre-fix jnext behavior.** `emulator.cpp:1378-1408` only treats
`v == 0xFF` as the "re-engage legacy ROM" sentinel; for any other
value (including `0xE5`, `0xE0`, `0xFE`) it calls
`mmu_.set_page(slot, v)`, which sets `read_only_=false` and asks
`rebuild_ptr` to resolve the SRAM page via `to_sram_page(v)`. The
`to_sram_page` formula returns `v + 0x20` for non-special pages — but
for `v ∈ [0xE0..0xFF]` the addition wraps mod 256 to `[0x00..0x1F]`,
mis-routing slot 0/1 to a wrong-aliased RAM region (e.g. `v=0xE5` →
SRAM page `0x05`, which is bank 2 hi).

This is a low-probability edge case (real firmware uses `0xFF`), but a
clear VHDL spec violation: the verbatim `MMU<i>` storage at
`zxnext.vhd:4690` admits any 8-bit value, and the arbiter's
high-page-handling is the canonical legacy-ROM fallback.

**Fix.** Extend the `page >= 0xE0` gate in `Mmu::rebuild_ptr` to cover
slot 0/1 as well, mirroring `engage_legacy_rom_paging_slot()`'s
sram_rom-derived ROM-page resolution but WITHOUT touching
`nr_mmu_[slot]` (so the NR `$50/$51` read-back returns the verbatim
VHDL `MMU<i>` value, e.g. `$E5`, NOT the `$FF` sentinel that
`engage_legacy_rom_paging_slot` would force). Honors
`port_eff7_reg_3 = 1` (RAM-at-0x0000 mode) by serving from the same
RAM pages 0/1 that `apply_legacy_rom_slots_` would push on the next
paging trigger. Slots 2-7 keep the existing inactive-slot behavior
(`read_ptr_ = nullptr` → reads return 0xFF, writes drop).

**Test impact.** `mmu_tests` and full `ctest` suite (37/37) pass
without regression.

### Finding 3 — NR 0x08 readback returns shadow not effective (NOT FIXED, class (b))

**VHDL spec.** `zxnext.vhd:5176` stores `nr_08_contention_disable`
immediately on every NR 0x08 write (the shadow). `zxnext.vhd:5822-5823`
latches `eff_nr_08_contention_disable` from the shadow ONLY on CPU
bus-idle cycles where `hc(8) = '1'` (right border / horizontal blank).
The NR 0x08 read-mux at `:5906` reads `eff_nr_08_contention_disable`
(the EFFECTIVE), not the shadow.

**Pre-fix jnext behavior.** `emulator.cpp:3127` reads
`mmu_.contention_disabled()`, which is set immediately by the NR 0x08
write (`mmu_.set_contention_disabled(...)` at `:3074`). I.e. the
read-back returns the SHADOW, not the EFFECTIVE.

**Why not fixed.** `ContentionModel::contention_disable()` already
exposes the effective value (post-`commit_contention_disable_on_hc`),
so the fix is a 1-line readback change in `emulator.cpp`. However:

- This is a NextReg readback handler, not strictly part of the memory
  subsystem.
- The lag is observable only on a write-immediately-read sequence
  inside the same line before `hc >= 256`; firmware that writes NR 0x08
  bit 6 typically does not poll within that microscopic window.
- Touching the readback would require auditing whether other consumers
  of `mmu_.contention_disabled()` need the shadow vs the effective
  view, which is out of scope for the memory audit.

**Recommendation.** A follow-up ticket should change
`emulator.cpp:3127` to consult `contention_.contention_disable()`
(EFFECTIVE) and document `mmu_.contention_disabled()` as the SHADOW
view. Verified by adding a unit-test row that writes NR 0x08 bit 6
mid-line, polls NR 0x08 before `hc >= 256` (expects shadow value
unchanged in read-mux), then commits via the per-instruction tick and
re-reads (expects effective value).

### Finding 4 — Layer 2 segment-mask gates slot 0/1 differently from VHDL (NOT FIXED, class (b))

**VHDL spec.** The SRAM-arbiter override `sram_pre_override(1)` (Layer
2 mapping enable per access) is set asymmetrically:

- **slot 0/1** (`cpu_a(15:14) = "00"`): `:3037` / `:3044` / `:3057`
  branches all set `sram_pre_override(1) = '1'` unconditionally
  (regardless of `port_123b_layer2_map_segment`). The `port_123b_layer2`
  enable / shadow latches still gate `sram_layer2_map_en` at `:3077`,
  but the segment field does NOT gate slot 0/1 mapping. The seg field
  is consumed only by `layer2_active_bank_offset_pre <= cpu_a(15:14)
  when seg = "11" else seg` at `:2966` — i.e. as a SOURCE-page selector,
  not a mapping-enable gate.
- **slot 2-5** (`cpu_a(15:14) ∈ {"01", "10"}`): `:3060-3065` sets
  `sram_pre_override(1) = ((not a15) or (not a14)) AND seg(1) AND
  seg(0)`. So L2 mapping enabled ONLY when seg = "11" (auto). For
  seg = "00" / "01" / "10", L2 mapping in slots 2-5 is suppressed.
- **slot 6+** (`cpu_a(15:14) = "11"`): `((not a15) or (not a14)) = 0` →
  override(1) = 0 always (no L2 mapping in slot 6/7).

**Pre-fix jnext behavior.** `mmu.h::read` / `write` use a single
`l2_segment_mask_` filter:

```cpp
if (addr < 0xC000) {
    int segment = addr / 0x4000;     // 0, 1, or 2
    if (l2_segment_mask_ & (1 << segment)) { /* L2 map */ }
}
```

with mask = `0x01` for seg=00, `0x02` for seg=01, `0x04` for seg=10,
`0x07` for seg=11. This means:

- `seg=01` + slot 0/1 (segment 0): mask `0x02 & 0x01 = 0` → C++ does
  NOT route to L2. VHDL DOES route. **Mismatch.**
- `seg=10` + slot 0/1: same mismatch.
- `seg=01` + slot 2/3 (segment 1): mask `0x02 & 0x02 = 0x02` → C++
  routes to L2. VHDL does NOT (only seg=11 enables slot 2-5).
  **Mismatch.**
- `seg=10` + slot 4/5: same mismatch.

Real-firmware impact: firmware that uses non-auto segments
(`seg ∈ {00, 01, 10}`) for slot-2-or-higher L2 mapping would see jnext
miscompute the L2 address. Conversely, slot 0/1 with non-auto seg
expects L2 routing in VHDL but C++ disables it. The most common
pattern is `seg=11` (auto) for all-slot L2 mapping, in which case
both implementations behave identically.

**Why not fixed.** The C++ semantics arguably match the documented
"port 0x123B segment field" intent better than the literal VHDL
behavior, but the audit charter requires VHDL fidelity. Fixing
requires:

1. For slot 0/1: drop the `l2_segment_mask_` gate entirely (always
   route to L2 when `l2_read_enable_` / `l2_write_enable_` is on).
2. For slot 2-5: gate on `l2_segment_raw_ == 0x03` (= seg "11" / auto)
   AND `addr < 0xC000` AND `addr / 0x4000 != 3`. The `l2_segment_mask_`
   field becomes redundant with the simple `addr < 0xC000` test once
   the seg-11-only gate is applied.
3. The `l2_offset_` arithmetic (`bofs = segment + l2_offset_`) keeps
   the existing `segment = addr / 0x4000` derivation (matches VHDL
   `offset_pre = cpu_a(15:14) when seg="11"` because in seg="11" the
   only allowed slots are 0/1/2 where `cpu_a(15:14) = segment`).

The fix touches 5 read/write paths and would need targeted unit-test
coverage for the four cross-product cases (seg ∈ {00,01,10,11} ×
slot ∈ {0/1, 2/3, 4/5}). Out of scope for this audit's branch.

**Recommendation.** Open a follow-up ticket. Discriminative test row:
write port 0x123B with seg=01 + l2_read_enable_, read from `addr=0x0000`
— expect L2 page-1 source (per VHDL); jnext currently reads from the
normal MMU0 ROM/RAM page.

### Finding 5 — RAM contents cleared to 0 on construction (NOT FIXED, class (c))

**VHDL spec.** Real SRAM contents are random at FPGA configuration /
power-on; there is no explicit reset clause in `zxnext.vhd` that zeros
SRAM.

**jnext behavior.** `Ram::Ram(size_t)` constructs with
`std::vector<uint8_t> data_(size_bytes, 0)` — full zero. This is a
deliberate simplification (every emulator does this; non-deterministic
boot is hostile to debugging and regression testing).

**Class (c) — intentional simplification.** Documented for audit
transparency.

---

## What was spot-checked and found correct (no findings)

- **NR 0x8E write-side decomposition** (`Mmu::write_nr_8e`): bit
  ↔ port-register mapping verified bit-by-bit against VHDL :3662-3670 /
  :3696-3704 / :3726-3734. The lock-bypass (NR 0x8E always fires
  regardless of `port_7ffd_locked`) is correct.
- **NR 0x8E read-back recomposition** (`Mmu::read_nr_8e`): formula at
  zxnext.vhd:6158-6159 reproduced bit-by-bit including the
  `dffd(0) | 7ffd(2:0) | '1' | 1ffd(0) | 1ffd(2) | ((7ffd(4) AND NOT
  1ffd(0)) OR (1ffd(1) AND 1ffd(0)))` formula — bit 3 is a fixed `'1'`
  sentinel, NOT round-tripping the bit 3 that was written. ✓
- **+3 special paging table** (`Mmu::apply_plus3_special_paging_`):
  decoded VHDL :4625-4632 algebraically for all four `(B, A) =
  (1ffd(2), 1ffd(1))` configurations; C++ table at `configs[4][4]`
  matches exactly:
  - `(0,0)` → banks 0,1,2,3
  - `(0,1)` → banks 4,5,6,7
  - `(1,0)` → banks 4,5,6,3
  - `(1,1)` → banks 4,7,6,3
- **+3 special-paging exit** (`revert_slots_2_to_5_post_special_`):
  matches VHDL :4655-4670.
- **port_eff7_reg_3 (RAM-at-0x0000)**: `apply_legacy_rom_slots_`
  correctly forces `MMU0/MMU1 = 0x00/0x01` per VHDL :4636-4644.
- **`engage_legacy_rom_paging_slot`** (G46(b) Wave 8 fix):
  per-slot semantics match VHDL :4686-4696 explicit `nr_mmu_we`
  semantics. Honors `port_eff7_reg_3` for the RAM-at-0x0000 case.
- **`compose_bank_`** (port_7ffd_bank composition): VHDL :3763-3766
  Pentagon vs non-Pentagon paths match; bit width fits in uint8_t for
  all valid input combinations.
- **NR 0x50-0x57 dispatch** (`emulator.cpp:1378-1409`): correctly
  treats `v == 0xFF` for slot 0/1 as legacy-ROM re-engagement (per-slot
  helper preserves the OTHER slot's NR mapping). Slot 2-7 with
  `v == 0xFF` correctly stores 0xFF and lets `rebuild_ptr` nullify the
  slot pointer.
- **NR 0x50-0x57 read-back**: `mmu_.get_page(i)` returns the
  authoritative `nr_mmu_[i]` mirror per VHDL :6075-6082, NOT the stale
  `regs_[0x50+i]` byte that bare NextReg::write would have stored.
- **`port_memory_change_dly` / `port_memory_ram_change_dly` gating**:
  C++ `apply_paging_update_` honors the special-paging arbitration;
  `write_nr_8e` correctly suppresses MMU6/7 rebuild when bit 3 = 0
  AND not in / leaving special mode (matching VHDL :3814).
- **`port_7ffd_locked` semantics**: C++ `effective_paging_locked()`
  composes the Pentagon-1024 override per VHDL :3769; profi branch
  always false (VHDL :3797) confirmed.
- **NR 0x82 / 0x83 / 0x84 port-enable gates**: 7FFD, 1FFD, DFFD
  handlers in `emulator.cpp` correctly check the relevant bit of NR
  0x82 (bits 1, 3, 2 respectively).
- **NR 0x07 cpu_speed bus-idle commit**: `ContentionModel`
  shadow/effective split with `commit_pending_cpu_speed_on_bus_idle`
  matches VHDL :5796-5828.
- **NR 0x08 contention_disable hc(8) commit**:
  `commit_contention_disable_on_hc` matches VHDL :5822-5823.
- **`port_1ffd_special_old_` reset**: cleared in `Mmu::reset(bool)`
  per VHDL :3716. End-state of the sticky-bool model matches VHDL's
  one-cycle pulse semantics for the special→normal transition revert.
- **`set_machine_type` immediate slot-rebuild**: `apply_legacy_rom_slots_`
  call on transition matches VHDL combinational `sram_rom`
  re-evaluation at :2981-3008.
- **`load_state` re-derives cached pointers**: explicit
  `for (int i = 0; i < 8; ++i) rebuild_ptr(i)` after load.
- **Boot ROM overlay**: 8 KB internal buffer with `addr & 0x1FFF`
  mirror per VHDL :3199-3204; gate on `addr < 0x4000` per :1856.
- **Multiface mf_mem_en** (`mf_overlay_active_`): consumes
  `multiface_->is_mem_active()` which composes
  `mf_enable OR fetch_66` per multiface.vhd:186.
- **Layer 2 read/write hot-path bank arithmetic**: bank-shadow
  selection (`l2_map_shadow_ ? l2_shadow_bank_ : l2_bank_`),
  `bofs = segment + l2_offset_`, `(bank + bofs) * 2 + a13` page
  composition all match VHDL :2966-2971.
- **port 0x123B read-back composition** (`l2_port_readback`):
  matches VHDL :3933 `seg & "00" & shadow & rd_en & en & wr_en`.
- **NR 0x03 machine-type FSM**: NR 0x03 write triggers boot-ROM
  disable, machine-timing update (gated on bit 7 + dt_lock + bit 3),
  machine-type commit (gated on previous config_mode), config_mode FSM
  toggle. Order of operations correct.
- **NR 0x82-0x84 soft-reset preservation** (`NextReg::reset`):
  preserved when NR 0x85 bit 7 (reset_type) is 0 per VHDL :5052-5057.
- **Contention LUT generation** (`ContentionModel::build`): hc_adj
  4-bit wrap, +3 extra hc_adj(3:1)=000 case, vc>=192 / vc>=256 border
  exclusion, per-machine page mask all match VHDL.

---

## Open questions

1. **NR 0x8C without altrom_en** — confirmed Finding 1 fix is correct,
   but is there a real-firmware sequence that exercises the cache
   staleness? NextZXOS 2.x and tbblue.fw use NR 0x8C primarily with
   bit 7 (altrom_en) — the read goes through the altrom path, which
   recomputes the SRAM page per access. The lock bits without altrom_en
   path is unusual but spec-compliant. The fix is defensive: it makes
   jnext match VHDL even for the unusual case.

2. **G46(b) supervisor-stack divergence vs memory-subsystem fidelity**.
   Per the EOD-22..EOD-24 investigation logs, the surviving boot bug is
   in the supervisor's PUSH/POP balance between RST 0x08 hits #2 and
   #3 — 3 missing PUSHes / 3 extra POPs vs CSpect. Neither this audit
   nor the prior pass surfaces a memory-subsystem cause that explains
   the imbalance. The slide-trigger `NEXTREG $8E,$03` semantics are
   spec-correct in jnext (audited bit-by-bit). The supervisor must be
   diverging upstream of the memory subsystem (e.g. RST $08 dispatch,
   alternate-stack management, or NR-write side effects in another
   subsystem). Memory subsystem is NOT the upstream cause.

3. **Layer 2 segment-mask asymmetry (Finding 4)** — should the C++
   semantics be retrofitted to match VHDL (always-on slot 0/1 + only
   seg=11 for slot 2-5)? The current behavior is more "documented-spec
   intuitive" but less VHDL-faithful. Recommendation: open a ticket
   with a discriminative test that exercises each cross-product cell.

---

## Files modified on this branch

- `src/memory/mmu.h` — moved `set_nr_8c` declaration out-of-line +
  added rationale comment.
- `src/memory/mmu.cpp` —
  - new out-of-line `Mmu::set_nr_8c(uint8_t)` calls
    `apply_legacy_rom_slots_()` to refresh slot 0/1 cache (Finding 1).
  - extended `Mmu::rebuild_ptr` `page >= 0xE0` gate to slot 0/1, with
    sram_rom-derived legacy ROM fallback honoring `port_eff7_reg_3`
    (Finding 2).

## Test status

```
ctest --test-dir build (full suite): 37/37 PASS
mmu_test: 202 total / 180 PASS / 0 FAIL / 22 SKIP (unchanged)
fuse_z80_tests: 1356/1356 PASS (unchanged)
contention_tests: PASS
```

No regressions introduced.
