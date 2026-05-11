# Pass-9 Memory Subsystem Verify Report — Strictest Convergence

**Date:** 2026-05-09
**Branch:** `task2/verify9-memory`
**Worktree:** `.claude/worktrees/task2-verify9-memory`
**Audit type:** Blind (no prior verify reports read).
**Convergence criterion (strictest):** 0 pending bugs of any class —
class-(a), class-(b), or class-(c). class-(d) ARCHITECTURAL items
require explicit user authorization.

---

## Verdict: **ZERO-PENDING (TRULY CONVERGED)**

All concrete VHDL divergences uncovered in Pass-9 have been fixed and
tested. Two phase-domain artifacts that would require an emulator-wide
half-cycle CPU model are escalated as **class-(d) ARCHITECTURAL —
DEFERRED** with clear reasoning; they do not represent functional
divergence under the unit/regression suite, only sub-cycle phase
ordering invisible above the per-instruction granularity.

| Counts | a | b | c | d |
|---|---|---|---|---|
| Found | 4 | 0 | 0 | 2 (escalated) |
| Fixed | 4 | 0 | n/a | n/a |

| Tests | Status |
|---|---|
| `mmu_test` | 205 / 183 PASS / 0 FAIL / 22 SKIP |
| `fuse_z80_test` | 1356 / 1356 PASS |
| `contention_test` | PASS |
| `floating_bus_test` | 30 / 30 PASS (FB-03a updated to test corrected VHDL behavior) |
| Full ctest suite | 37 / 37 PASS |
| Regression (`test/00regression`) | 32 PASS / 1 FAIL (`parallax-demo` — pre-existing baseline failure, unchanged) |

---

## Class-(c) Backlog Disposition Table

The Pass-9 prompt enumerated nine concrete class-(c) candidates the
prior passes had catalogued. Each was audited against VHDL; the
disposition is:

| # | Item | Disposition |
|---|---|---|
| 1 | Per-16K-slot `p3_floating_bus_dat_` granularity | **FIXED → class-(a)** — re-homed onto per-page `mem_contend` decode. |
| 2 | `port_7ffd_reg` vs `port_7ffd_dat` half-cycle phase | **class-(d)** — sub-cycle phase, requires CPU half-cycle model. |
| 3 | +3 floating-bus active-display arm | **FIXED → class-(a)** — VRAM-byte arm wired via shared helper. |
| 4 | `sram_alt_128_n` not modelled | **PROVEN NO DIVERGENCE** — already implemented in `altrom_sram_page_()` (mmu.h:1083-1107) per VHDL :2986/:2995/:3005. |
| 5 | NR $82 bit-3 gate on direct port_1ffd writes | **PROVEN NO DIVERGENCE** — already gated at emulator.cpp:2710. |
| 6 | `port_1ffd_special_old_` decay model (sticky bool vs per-cycle) | **PROVEN NO DIVERGENCE** — VHDL :3729 captures `_old` outside the change-dly window; jnext captures after each paging trigger; end-state observable at any post-trigger point is identical. |
| 7 | Per-cycle SRAM arbiter freeze | **PROVEN NO DIVERGENCE** — VHDL :3010-3014 freezes the arbiter at MREQ falling edge; jnext's `mmu_.read/write` is called once per CPU memory cycle, so the freeze window collapses into our access granularity. No Copper writes are observed mid-access. |
| 8 | Per-instruction `slot_contended_[]` mirror | **REPLACED** — the floating-bus latch path no longer consumes the mirror; gate is now per-page `mem_contend_for_()` (item #1). The mirror is retained for save-state schema compatibility but is decoupled from the runtime hot path. |
| 9 | VHDL `*_q` half-cycle delays generally | **class-(d)** — sub-instruction phase; affects ULA shadow `_dat` only, no observable effect at frame granularity. |

In addition, Pass-9 found **two new** class-(a) divergences during the
"new angles" sweep, both fixed:

- **NR $03 machine-timing commit does not rebuild ContentionModel LUT** — runtime Next→48K/128K/+3/Pentagon switches missed +3-specific `wait_s` corner and per-machine bank decode.
- **`port_7ffd_active` OR-term missing from `port_contend()`** — port 0x7FFD writes on 128K/+3 silently missed contention.

---

## Class-(a) Bugs Found and Fixed

### A1. p3_floating_bus_dat latch gated on per-16K-slot mirror, not per-page mem_contend (`mmu.h`)

**VHDL oracle:** `zxnext.vhd:4498-4509` — the latch fires on every
contended memory cycle (`mem_contend = '1' AND cpu_mreq_n = '0'`),
where `mem_contend` (line :4489-4493) is decoded per the live
`mem_active_page` (= `MMU<i>` register value for the addressed slot)
and the machine timing.

**Pre-fix divergence:** `Mmu::read/write` consulted
`slot_contended_[addr >> 14]` — a coarse 4-entry boolean mirror set
externally by `Emulator` only on 7FFD / +3-special-paging writes. NR
$50/$51 RAM-mappings into slot 0/1 (e.g., NR $51,$05 maps bank-5-hi
into slot 1) did NOT update the mirror, so contended bank-5 accesses
in non-default mappings missed the latch update.

**Fix:** Added `Mmu::mem_contend_for_(addr)` private inline that
implements the VHDL :4489-4493 decode directly from `nr_mmu_[]` (=
`mem_active_page`) and the cached `machine_type_`. The read/write hot
path now consults this gate. Matches VHDL exactly: high pages
(0x10..0xFF) never contend; low pages contend per machine type via
bank-5-only / odd-banks / banks-≥-4 / Pentagon-no-contend.

**Files:** `src/memory/mmu.h` (latch sites + helper).

### A2. +3 floating-bus active-display arm always returned the contended-CPU latch (`emulator.cpp`)

**VHDL oracle:** `zxnext.vhd:4517` + `zxula.vhd:573`:
```
port_p3_floating_bus_dat <= ula_floating_bus when port_7ffd_locked='0'
                            else X"FF";
o_ula_floating_bus       <= (floating_bus_r(7:1) & (floating_bus_r(0) or i_timing_p3))
                              when (border_active_ula='0' and floating_bus_en='1')
                            else i_p3_floating_bus when i_timing_p3='1'
                            else X"FF";
```

**Pre-fix divergence:** Port 0x0FFD on +3 unconditionally returned
`Mmu::p3_floating_bus_dat() | 0x01` — the contended-CPU latch (border
arm). The active-display arm (VRAM byte the ULA is fetching during
display) was never wired. Comment admitted this as "Branch B
simplification".

**Fix:** Extracted `Emulator::ula_floating_bus_active_arm()` from the
existing 48K/128K port-0xFF path. The port 0x0FFD handler now consults
this helper first (active arm), falls back to `p3_floating_bus_dat()`
(border arm) when the active arm is silent. Both arms apply the +3
bit-0 force per VHDL.

**Files:** `src/core/emulator.cpp`, `src/core/emulator.h`,
`test/floating_bus/floating_bus_test.cpp` (FB-03a updated to test
corrected VHDL behavior — VRAM byte | 0x01 in active capture phase).

### A3. NR $03 runtime machine-timing commit did not rebuild ContentionModel LUT (`emulator.cpp`)

**VHDL oracle:** `zxnext.vhd:5137-5145` — NR $03 commit fires on each
write under config-mode. The downstream `i_contention_en` gate
(`zxnext.vhd:4481`) and `mem_contend` decode (`:4489-4493`) follow the
new machine_timing immediately.

**Pre-fix divergence:** `ContentionModel::build()` was called only
from `Emulator::init()`. A runtime NR $03 commit changed
`MachineType` in `Mmu` and `NextReg`, but `ContentionModel` retained
the boot-time LUT — Next→+3 missed the `hc_adj[3:1]=000` corner of
`wait_s` (zxula.vhd:582-583, +3-only); Next→48K/128K used the wrong
per-machine bank decode.

**Fix:** Added `ContentionModel::rebuild_for_type(type)` that updates
`type_` + LUT + per-machine slot mirror without touching dynamic gate
state (mem_active_page / cpu_speed / contention_disable / shadows).
The NR $03 commit at `emulator.cpp` now calls
`contention_.rebuild_for_type(new_mt)` after `mmu_.set_machine_type`.
Preserves paging + contention-disable through the commit.

**Files:** `src/memory/contention.{h,cpp}`, `src/core/emulator.cpp`.

### A4. `port_7ffd_active` OR-term missing from `port_contend()` (`contention.cpp`)

**VHDL oracle:** `zxnext.vhd:4496` + `:2594` + `:2593`:
```
port_contend       <= (not cpu_a(0)) or port_7ffd_active or port_bf3b or port_ff3b;
port_7ffd_active   <= '1' when port_7ffd='1' and (s128_timing_hw_en='1' or p3_timing_hw_en='1') else '0';
port_7ffd          <= cpu_a(15)='0' AND (cpu_a(14)='1' OR NOT p3_timing) AND port_fd
                      AND NOT port_1ffd AND port_7ffd_io_en;
```

**Pre-fix divergence:** `ContentionModel::port_contend()`
intentionally dropped this term. Port 0x7FFD writes on 128K/+3 (odd
port) had `(not cpu_a(0)) = 0`, ULA+ term false, and the missing
port_7ffd term — so contention silently failed, missing the per-phase
clock stretch on every NR $7FFD bank-switch. The legacy "PHASE-B"
comment escalated this to "out of scope" for the bare-class API.

**Fix:** Added `ContentionModel::set_port_7ffd_io_en(en)` (NR $82 bit
1 mirror) + `port_7ffd_io_en_` field; `port_contend()` now factors the
full address decode + machine-timing gate + io_en gate inline. The
runtime caller pushes NR $82 bit 1 via the new write handler in
`Emulator::install_port_handlers`; init time seeds from the cached NR
$82 power-on default (0xFF, all bits set).

**Files:** `src/memory/contention.{h,cpp}`, `src/core/emulator.cpp`.

---

## Class-(d) Architectural Items (Escalated)

These are sub-cycle phase artifacts that would require an emulator-wide
half-cycle / multi-clock-domain CPU model to fix. None show observable
divergence at the test/regression level (per-instruction granularity is
the natural boundary). User authorization required to refactor.

### D1. `port_7ffd_reg` vs `port_7ffd_dat` half-cycle delay

VHDL has two latches: `port_7ffd_reg` (immediate on write) and
`port_7ffd_dat` (one CLK_CPU falling edge later, zxnext.vhd:3676-3681).
The shadow-screen-enable signal at :3768 reads the delayed
`port_7ffd_dat(3)`. Within a single CPU instruction window between
writing 7FFD and the ULA observing the shadow bit, VHDL has a
half-cycle window where the ULA still sees the old value.

In jnext, ULA reads happen at video-frame rendering boundaries (well
after the instruction), so the half-cycle delay collapses cleanly into
"ULA sees the latest value at frame boundaries", which is correct for
any software not racing the ULA at sub-cycle resolution.

Fixing this would require modeling the CPU at half-cycle granularity
and the ULA tap point at the same domain.

### D2. Generic VHDL `*_q` registered/delayed signals

The VHDL has many `_q` signals for register-pipelining (NR write
buses, port latches, etc.). Most are sub-cycle and collapse into
"immediate effect" at our per-instruction granularity. No observable
divergence at instruction-level; modeling them would require a clock-
domain-aware multi-tick simulator.

---

## New Angles Sweep — Findings

### Cycle-precise SRAM arbiter
VHDL `zxnext.vhd:3012-3014` freezes the SRAM arbiter on falling edge
of CLK_28 when `cpu_mreq_n='1'`. Our model invokes `Mmu::read/write`
once per CPU memory cycle, so the freeze window naturally collapses
into our access semantics. No Copper-driven NR writes occur mid-CPU-
access (Copper window is between instructions). **No divergence.**

### Floating-bus byte selection per current display position
Addressed by the active-arm helper (A2). The helper computes the VRAM
pixel/attribute byte at the current raster T-state, matching VHDL
zxula.vhd:319-340 fold semantics (T%8 ∈ {2,3,4,5}).

### DMA-driven memory accesses interaction with MMU contention
DMA accesses go through `Mmu::read/write` (set up at
`emulator.cpp:3965-3966`). With the A1 fix, the floating-bus latch
captures DMA-driven contended memory cycles correctly via the same
per-page `mem_contend_for_()` decode. VHDL's `:4498-4509` latches on
`cpu_di/cpu_do` — the bus signals shared by CPU and DMA — so DMA's
contribution to the latch IS observed in VHDL and now in jnext.
DMA-side CPU contention timing (`o_cpu_contend` / `o_cpu_wait_n`) is
naturally bypassed because DMA isn't routed through the FUSE Z80
contention callbacks; matches VHDL's `dma_holds_bus` semantics. **No
divergence.**

---

## Convergence Verdict

The strictest convergence criterion is **MET** for the memory
subsystem:

- 0 class-(a) bugs pending (4 found+fixed in Pass-9).
- 0 class-(b) bugs found.
- 0 class-(c) items remaining — every prior class-(c) candidate either
  fixed (uplifted to class-(a)) or proven to have no VHDL divergence.
- 2 class-(d) ARCHITECTURAL items escalated (sub-cycle phase domain;
  not observable at per-instruction granularity).

The `slot_contended_[]` legacy mirror remains in the data model for
save-state schema compatibility but is no longer consumed on the
floating-bus latch hot path. It can be removed in a future schema
revision; doing so now would break save-state round-trip.

The `nr_mmu_[]`-driven `mem_contend_for_()` gate exactly mirrors VHDL
`zxnext.vhd:4489-4493` and is the canonical source for any future
per-cycle contention consumer (e.g., a hypothetical Mmu-side
contention-stretch path).

**Pass-9 is converged. Subsequent passes have no pending memory-
subsystem class-(a/b/c) work.**
