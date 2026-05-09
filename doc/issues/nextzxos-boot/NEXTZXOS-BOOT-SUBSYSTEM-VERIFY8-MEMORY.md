# Verify8 — Memory Subsystem (Pass 8)

## Verdict

**NEW FINDINGS** — five class-(a) bugs identified and fixed. Zero
class-(b) findings outstanding (all resolved as fixes). Several
class-(c) intentional simplifications retained from prior passes,
all already documented inline with VHDL citations.

A previous pass-8 agent had partially completed two of the five fixes
(L2 overlay segment gating + NR 0x08 readback bit 6) when interrupted
by a desktop crash. Verified those against VHDL, kept them, then
continued the audit and found three more class-(a) bugs:

  1. NR 0x08 readback bit 7 — wrong-signal wiring (paging_locked vs
     effective_paging_locked).
  2. +3 floating bus port read — same wrong-signal wiring.
  3. `Mmu::current_sram_rom()` — ZX128K case missing the altrom-lock
     branch that VHDL :2997-3007 applies to BOTH 128K and Next/Pentagon.

## Pre-crash partial work review

The previous run left the following files modified (uncommitted):

| File | Verdict | Reasoning |
|------|---------|-----------|
| `src/memory/mmu.h` | KEPT (with extension) | Two fixes: (1) `l2_overlay_active_for(addr)` + `l2_offset_pre_for(addr)` helpers correctly model VHDL :3037-3066 sram_pre_override(1) gate + :2966 offset_pre formula. (2) Replaced bitmask-based seg gate in read()/write() with the new helpers. Verified each gate branch against VHDL line-by-line. Also extended this commit with a fix for `current_sram_rom()` ZX128K branch. |
| `src/memory/mmu.cpp` | KEPT | `set_l2_port` retains the legacy `l2_segment_mask_` switch only for save_state schema compatibility (the read/write hot path now ignores the mask). Comment correctly cites VHDL :3043/:3050/:3057. |
| `src/core/emulator.cpp` | KEPT (with extensions) | NR 0x08 readback bit 6 fix is correct — VHDL :5906 reads `eff_nr_08_contention_disable` (effective gate) not the immediate shadow. Extended this commit with two more wrong-signal fixes (bit 7 of NR 0x08 readback + +3 floating bus read). |
| `test/contention/contention_test.cpp` | KEPT | Correctly pokes `commit_contention_disable_on_hc(300)` so the bare-Emulator harness latches the shadow→effective transition without running an instruction. Mirrors what `run_frame()` does per-instruction at runtime. |
| `test/nextreg/nextreg_integration_test.cpp` | KEPT | Same rationale as the contention test. |

## Class-(a) bugs found and fixed

### A1 — Layer 2 overlay segment gating (pre-crash work)

**Site**: `src/memory/mmu.h:261, 367` (read/write hot paths) +
`src/memory/mmu.h:1016-1041` (new helpers).

**VHDL oracle**: `zxnext.vhd:3037-3066` (sram_pre_override(1) gate),
`:2966` (layer2_active_bank_offset_pre formula), `:3077`
(sram_layer2_map_en mux).

**Bug**: Pre-fix C++ used a 3-bit bitmask `l2_segment_mask_` indexed
by `addr / 0x4000`:
- seg=00 → mask=0x01 → enabled at 0x0000-0x3FFF only
- seg=01 → mask=0x02 → enabled at 0x4000-0x7FFF only
- seg=10 → mask=0x04 → enabled at 0x8000-0xBFFF only
- seg=11 → mask=0x07 → enabled at 0x0000-0xBFFF

This diverged from VHDL in two ways:

1. **Low half**: VHDL :3043/:3050/:3057 set `sram_pre_override(1) <=
   '1'` UNCONDITIONALLY in the non-MF low-half cases. So L2 should
   be eligible at 0x0000-0x3FFF for ANY seg value (when not MF).
   Pre-fix only enabled L2 in the low half when seg=00 or seg=11.

2. **High half**: VHDL :3065 enables L2 in the high half only when
   `((NOT cpu_a(15)) OR (NOT cpu_a(14))) AND seg(1) AND seg(0)` —
   i.e. addr ∈ [0x4000, 0xBFFF] AND seg=11. Pre-fix enabled L2 at
   0x4000-0x7FFF for seg=01 and 0x8000-0xBFFF for seg=10, which
   VHDL does NOT do.

Additionally, VHDL :2966 makes `layer2_active_bank_offset_pre =
cpu_a(15:14) when seg="11" else seg`. Pre-fix C++ used
`addr / 0x4000` (the cpu_a(15:14) value) as the bank-offset for ALL
seg values — diverging when seg ≠ 11.

**Fix**: Added two helpers in `mmu.h`:

```cpp
inline bool l2_overlay_active_for(uint16_t addr) const {
    if (addr < 0x4000) {
        if (multiface_ && mf_overlay_active_()) return false;
        return true;                            // VHDL :3043,:3050,:3057
    }
    if (addr >= 0xC000) return false;           // VHDL :3065
    return l2_segment_raw_ == 0x03;             // seg = "11"
}

inline uint8_t l2_offset_pre_for(uint16_t addr) const {
    if (l2_segment_raw_ == 0x03) {
        return static_cast<uint8_t>((addr >> 14) & 0x03);
    }
    return l2_segment_raw_;                     // VHDL :2966
}
```

The read/write paths now consult these helpers instead of the bitmask.
The legacy `l2_segment_mask_` field is retained only for save-state
schema compatibility.

### A2 — NR 0x08 readback bit 6 (pre-crash work)

**Site**: `src/core/emulator.cpp:3389-3404`.

**VHDL oracle**: `zxnext.vhd:5906`:
```
port_253b_dat <= (not port_7ffd_locked) & eff_nr_08_contention_disable & ...
```
Plus `:5800-5823` (latch process — `eff_nr_08_contention_disable <=
nr_08_contention_disable` only when `cpu_mreq_n='1' AND cpu_iorq_n='1'
AND cpu_m1_n='1' AND dma_holds_bus='0' AND hc(8)='1'`).

**Bug**: Pre-fix C++ read `mmu_.contention_disabled()` for bit 6.
That accessor mirrors the IMMEDIATE shadow (the value written by the
NR 0x08 handler), not the effective gate which only commits on the
bus-idle / hc(8)=1 edge. Mid-line read-backs would return the
freshly-written shadow instead of the last-committed effective gate.

**Fix**: Read `contention_.contention_disable()` instead — the
ContentionModel's effective field is the one updated by
`commit_contention_disable_on_hc()` (per-instruction polling from
`run_frame()`).

### A3 — NR 0x08 readback bit 7 (this pass)

**Site**: `src/core/emulator.cpp:3389-3402`.

**VHDL oracle**: `zxnext.vhd:5906`: bit 7 = `(not port_7ffd_locked)`
where `port_7ffd_locked` is the EFFECTIVE expression at `:3769`:
```
port_7ffd_locked <= '0' when (nr_8f_mapping_mode_pentagon_1024_en = '1')
                          or (nr_8f_mapping_mode_profi = '1' and port_dffd_reg(4) = '1')
                       else port_7ffd_reg(5);
```

**Bug**: Pre-fix C++ read `mmu_.paging_locked()` — which mirrors the
RAW `port_7ffd_reg(5)` only, not the effective-locked expression.
When Pentagon-1024 mode is enabled (NR 0x8F=11 AND EFF7(2)=0), VHDL
forces `port_7ffd_locked='0'` even if bit 5 is set, so bit 7 of the
NR 0x08 readback should be `'1'` (= NOT 0 = unlocked). Pre-fix C++
would still report "locked" in that case.

**Fix**: Replace `mmu_.paging_locked()` with
`mmu_.effective_paging_locked()`. The accessor models the full :3769
gate (Pentagon-1024 override; profi branch is correctly elided since
profi is forced 0 in jnext per VHDL :3797).

### A4 — +3 floating bus port read uses raw paging lock (this pass)

**Site**: `src/core/emulator.cpp:2787-2798` (port 0x0001 / 0xF003 mask handler).

**VHDL oracle**: `zxnext.vhd:4517`:
```
port_p3_floating_bus_dat <= ula_floating_bus when port_7ffd_locked = '0' else X"FF";
```

**Bug**: Pre-fix C++ read `mmu_.paging_locked()` for the gate. Same
issue as A3 — the gate should consult the EFFECTIVE
`port_7ffd_locked` signal that respects the Pentagon-1024 override.

**Fix**: Replace `mmu_.paging_locked()` with
`mmu_.effective_paging_locked()`. Same accessor, same VHDL :3769
expression.

### A5 — `current_sram_rom()` ZX128K missing altrom-lock (this pass)

**Site**: `src/memory/mmu.h:807-836`.

**VHDL oracle**: `zxnext.vhd:2981-3008` — sram_rom selection process.
The else branch (`:2997`) covers BOTH `machine_type_128` AND `Next`/
`Pentagon` (everything that isn't 48K and isn't +3). Both share the
altrom-lock semantics:
```vhdl
if nr_8c_altrom_lock_rom1 = '1' or nr_8c_altrom_lock_rom0 = '1' then
   sram_rom <= '0' & nr_8c_altrom_lock_rom1;
   ...
else
   sram_rom <= '0' & port_1ffd_rom(0);
   ...
end if;
```

**Bug**: Pre-fix C++ had a separate ZX128K case that returned
`(port_7ffd_ >> 4) & 1` UNCONDITIONALLY — bypassing the altrom-lock
override. VHDL routes 128K through the same else branch as Next, so
altrom-lock applies symmetrically.

**Fix**: Merge the ZX128K and ZXN_ISSUE2 cases — both share the
altrom-lock check + `current_rom_bank() & 1` fallback. Mirrors the
`sram_rom3()` accessor above which already groups them together.

```cpp
case MachineType::ZX128K:
case MachineType::ZXN_ISSUE2:
default:
    if (nr_8c_altrom_lock_rom1() || nr_8c_altrom_lock_rom0()) {
        return static_cast<uint8_t>(nr_8c_altrom_lock_rom1() ? 1 : 0);
    }
    return static_cast<uint8_t>(current_rom_bank() & 1);
```

## Class-(b) findings resolution table

| Finding | Resolution | Citation |
|---------|------------|----------|
| L2 overlay segment gating divergence | RESOLVED — class-(a) fix A1 | VHDL :3037-3066, :2966, :3077 |
| NR 0x08 bit 6 readback uses shadow | RESOLVED — class-(a) fix A2 | VHDL :5906, :5800-5823 |
| NR 0x08 bit 7 readback uses raw lock | RESOLVED — class-(a) fix A3 | VHDL :5906, :3769 |
| +3 floating bus uses raw paging lock | RESOLVED — class-(a) fix A4 | VHDL :4517, :3769 |
| ZX128K current_sram_rom missing altrom | RESOLVED — class-(a) fix A5 | VHDL :2997-3007 |

No class-(b) findings remain unresolved. All identified divergences
were re-classified as class-(a) and fixed.

## Class-(c) intentional simplifications (retained, documented)

These pre-existing simplifications are documented inline in the code
with VHDL citations and remain class-(c) (intentional, not bugs):

1. **`Mmu::p3_floating_bus_dat` per-slot contention**
   (`src/memory/mmu.h:325, 419`). VHDL `:4498-4509` updates the latch
   on every contended access using the per-byte `mem_contend` signal
   (per-page granularity). Jnext approximates with a per-16K-slot
   flag (`slot_contended_[]`) pushed by the Emulator. Branch B
   simplification — coarser than VHDL but adequate for the Branch B
   scope. Documented at the call sites.

2. **`Mmu::shadow_screen_en()`** uses `port_7ffd_` directly while VHDL
   `:3768` uses `port_7ffd_dat(3)` (a falling-edge-latched copy of
   `port_7ffd_reg`). At per-instruction granularity these are
   indistinguishable; at sub-instruction (sub-cycle) granularity they
   differ by half a CPU clock. Acceptable for jnext's instruction-
   level tick model.

3. **+3 floating bus simplification** (`src/core/emulator.cpp:2796`)
   returns `mmu_.p3_floating_bus_dat() | 0x01` instead of VHDL
   :4517's `ula_floating_bus`. Branch B simplification — the active-
   display-window path at zxula.vhd:573 (modified `floating_bus_r`
   byte) is glossed over; jnext returns the latched cpu_di/cpu_do
   byte unconditionally with bit 0 set. Documented at the site.

4. **`sram_alt_128_n` not modelled** (`src/memory/mmu.h:719`). VHDL
   `:2986/:2991/:2995/:3001/:3005` drives this signal which gates
   the altrom mirror selection at `:3117`. Jnext's altrom path uses
   a simpler page calculation that doesn't consume sram_alt_128_n.
   Out of scope for the verify-passes — documented as a known gap.

5. **NR 0x82 bit-3 gate on direct port_1ffd writes**
   (`src/memory/mmu.h:723-726`). VHDL gates port_1ffd writes on Next
   mode through NR 0x82 bit 3; jnext's port handler doesn't apply
   the gate. Affects only direct port_1ffd writes outside the
   firmware-managed boot path. Documented as a known gap (G57).

## Convergence verdict

**Five class-(a) bugs found and fixed.** Pass 8 found NEW
divergences (no inheritance from pass-7 or earlier passes). The same
pattern that surfaced in pass-6 (mem_active_page_for using effective
page instead of MMU sentinel) and pass-7 (DivMmc rom3_active using
rom3_selected instead of sram_rom3) repeats here in three places:

  * NR 0x08 readback bit 7 — used the immediate shadow (paging_locked)
    instead of the effective signal (port_7ffd_locked / VHDL :3769).
  * NR 0x08 readback bit 6 — used the immediate shadow instead of
    `eff_nr_08_contention_disable` (VHDL :5906).
  * +3 floating bus port read — same paging_locked vs effective issue.

These are textbook "wrong-signal wiring" bugs (pass-6/7 systemic
pattern). They may exist elsewhere in the audit surface — but the
current sweep across all Mmu accessor consumers in `src/core/`,
`src/cpu/`, `src/video/`, `src/peripheral/`, and `src/debugger/`
did not surface any further divergences after these fixes.

The L2 overlay fix (A1) is qualitatively different — it's a wholesale
re-modeling of the segment-decode formula that pre-existing C++ had
fundamentally wrong (segment as window-mask vs segment as
bank-offset-shifter). Surface-level the L2 path tests pass because
the boot-path / regression-screenshot suite uses seg=00 (low-half)
and seg=11 (auto-segment) — the two values where the old and new
behaviors agree at the addresses tested. Real-firmware seg=01 / seg=10
patterns would have exposed the divergence.

The `current_sram_rom()` fix (A5) is a regression-prone branch error
— the parallel `sram_rom3()` accessor above already handles ZX128K
correctly, and the divergence between the two accessors is itself a
red flag that didn't surface in tests because boot-path is exercised
mainly under ZXN_ISSUE2 (Next mode), not ZX128K mode.

**Convergence**: zero class-(a) AND zero class-(b) targeted in this
pass. After fixes:
- 0 class-(a) outstanding
- 0 class-(b) outstanding
- All identified divergences re-classified to class-(a) and fixed.

To declare TRUE convergence (zero across all passes including
A1-A5), a pass-9 sweep would need to re-audit all 35+ Mmu accessors
+ mutators against VHDL one more time and confirm no further
divergences surface. The pattern of "one or two NEW class-(a) findings
per pass" suggests the audit is still finding incremental
divergences — true zero-finding convergence is not yet reached.

## Test status

- Build: clean, all targets compile.
- Unit tests: 37/37 PASS (mmu, mmu_integration, fuse_z80, contention,
  nextreg, nextreg_integration, layer2, multiface, divmmc, ula, plus
  all other subsystem suites).
- Regression screenshots: 4 pre-existing flaky/env failures (parallax-
  demo pixel diff, video/rzx record/playback — unrelated to memory
  audit). No new regressions introduced by the fixes.

## Branch state

- Branch: `task2/verify8-memory`
- Worktree: `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify8-memory`
- HEAD before commit: `5adef21` (pass-7 doc)
- Commits added by this pass: pending (one fixes commit + one report commit)
