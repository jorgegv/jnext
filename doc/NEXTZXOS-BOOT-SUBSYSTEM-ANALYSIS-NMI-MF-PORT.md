# NextZXOS Boot — NMI / Multiface / NextREG / Port subsystem analysis

**Date**: 2026-05-09
**Branch**: `task2/nmi-mf-port-review`
**VHDL oracle**: `cores/zxnext/src/zxnext.vhd` + `cores/zxnext/src/device/multiface.vhd`
**Scope**: `src/peripheral/nmi_source.{cpp,h}`, `src/peripheral/multiface.{cpp,h}`,
`src/port/nextreg.{cpp,h}`, `src/port/port_dispatch.{cpp,h}`, plus the
NextREG / port-handler installation site in `src/core/emulator.cpp`.

## Executive summary

Five discrepancies vs the VHDL spec were identified and fixed in this branch:

| ID | Severity | Subsystem | Summary |
|----|----------|-----------|---------|
| NMI-1 | a (bug) | NmiSource | `HOLD→END` selector ignored the ExpBus-active arm |
| NMI-2 | a (bug) | NmiSource | NR 0x02 readback bits 3/2 cleared on FSM `S_NMI_END`, not on the VHDL "write back with bit 0" path |
| NMI-3 | a (critical) | NmiSource | FSM stuck in `S_NMI_END` forever — `observe_cpu_wr` never wired |
| NMI-4 | a (bug) | NextREG / NmiSource | NR 0x02 readback bit 4 (`nr_02_iotrap`) not composed |
| NR-2  | a (bug) | NextREG | NR 0x52..0x55 with `$FF` write silently remapped to physical page 0 |

All five fixes are VHDL-faithful; one in-tree unit row (NR02-05) was
updated to reflect the corrected semantics. No probes / instrumentation
introduced. All 36 ctest targets pass post-fix.

The most significant fix is **NMI-3**: the previous behaviour caused the
NmiSource FSM to fire exactly one NMI per emulator session (the FSM
remained in `End` with no path back to `Idle`). Subsequent NMI assertions
(button presses, NR 0x02 software-NMI strobes, IO traps, ExpBus pin
edges) were silently dropped. This is unrelated to the current G46(b)
slide-cascade root cause but is a load-bearing correctness fix for any
NMI-driven NextZXOS / Multiface workflow.

NR-2 (slot 2-5 `$FF` fallback) is latent — no current boot path writes
`$FF` to NR 0x52-0x55, but the previous behaviour (`map_rom(i, 0)` =
silent remap to physical page 0) was a deviation from VHDL that could
mask future regressions.

## Methodology

For each file in scope:

1. Read the cpp header + implementation end-to-end.
2. Cross-reference each VHDL signal cited in the cpp comments against
   the VHDL source (line numbers, sensitivity lists, set/clear paths).
3. Audit the `case` blocks and `if` chains in the cpp against the
   VHDL `process` blocks they model.
4. Walk every NMI / port / NR write+read handler in `emulator.cpp` to
   confirm wiring (caller → NmiSource setter → FSM → output → CPU).

Cross-cuts:

* `nmi_source.cpp:271-281` (`observe_cpu_wr`) was found to be defined
  but never called — a `grep` of `src/` confirmed no caller exists.
* The NR 0x02 readback path was traced from `Emulator::set_read_handler(0x02)`
  → `NmiSource::nr_02_read()` → `nr_02_pending_*`.

## Findings — detailed

### NMI-1 — `HOLD→END` selector ignores ExpBus arm

**File**: `src/peripheral/nmi_source.cpp:361-374`

VHDL zxnext.vhd:2118 selects three different sources for `nmi_hold`:

```vhdl
nmi_hold <= mf_nmi_hold     when nmi_mf     = '1'
       else divmmc_nmi_hold when nmi_divmmc = '1'
       else nmi_assert_expbus;
```

The previous cpp code collapsed the third arm:

```cpp
const bool hold = nmi_mf_ ? mf_nmi_hold_ : divmmc_nmi_hold_;
```

— effectively using `divmmc_nmi_hold` as the selector when the latched
source is the ExpBus. With `divmmc_nmi_hold` typically false at idle,
the FSM advanced HOLD→END unconditionally for ExpBus NMIs (matching the
VHDL `nmi_assert_expbus` only by accident when the bus pin transitioned
back to idle in the same tick). When DivMMC was holding while ExpBus
asserted, the cpp would WRONGLY hold the FSM in HOLD.

**Fix** (committed): the `case State::Hold` block now selects
`mf_nmi_hold_`, `divmmc_nmi_hold_`, or `nmi_assert_expbus()` based on
which priority latch is active, matching VHDL line 2118 verbatim.

### NMI-2 — NR 0x02 readback latch clear path

**Files**: `src/peripheral/nmi_source.cpp`, `test/nmi/nmi_test.cpp`

VHDL processes at zxnext.vhd:3840-3864 implement the readback latches:

```vhdl
if reset = '1' then
   nr_02_generate_mf_nmi <= '0';
elsif nmi_gen_nr_mf = '1' and nmi_accept_cause = '1' then
   nr_02_generate_mf_nmi <= '1';
elsif nr_02_we = '1' and nr_wr_dat(3) = '0' then
   nr_02_generate_mf_nmi <= '0';
end if;
```

— with `nmi_accept_cause <= '1' when nmi_state = S_NMI_IDLE or S_NMI_FETCH`
(VHDL:2164). Critically, the FSM signal `nmi_state` is NOT in this
process's sensitivity list, so the latches are NOT auto-cleared at
`S_NMI_END`.

The previous cpp:

* Set `nr_02_pending_*` unconditionally inside `nr_02_write` (no
  `nmi_accept_cause` gate).
* Auto-cleared `nr_02_pending_*` in `recompute_()` `case State::End`.
* Provided no "write 0 to clear" path.

**Fix** (committed):

* `nr_02_write()` gates the pending-bit set on `state_ in {Idle, Fetch}`
  (= `nmi_accept_cause`), and clears the pending bit when the
  corresponding bit is written low.
* `recompute_()` no longer touches `nr_02_pending_*` in `case End`.
* Test row NR02-05 was rewritten to verify the new (VHDL-faithful)
  semantics: the readback bit survives the FSM `END` transition and
  clears on a subsequent NR 0x02 write with the bit explicitly low.

### NMI-3 — FSM stuck in `End` (critical)

**File**: `src/peripheral/nmi_source.cpp` + `emulator.cpp` (no caller).

VHDL zxnext.vhd:2143-2147:

```vhdl
when others =>             -- S_NMI_END
   if cpu_wr_n = '1' then
      nmi_state_next <= S_NMI_IDLE;
   else
      nmi_state_next <= S_NMI_END;
   end if;
```

Practically, `cpu_wr_n='1'` for almost every cycle except a write-cycle
data phase, so the END→IDLE advance happens on the next clock edge after
entering END.

The cpp `observe_cpu_wr(bool)` member was defined but never called from
`Emulator`. The Z80 core (FUSE-derived) does not surface a `wr_n` line
through the `Z80Cpu` callback API. Consequence: once an NMI completed
(reaching `State::End`), the FSM stuck there permanently. Any subsequent
NMI request would set the priority latch (the `!is_activated()` guard
permits it) but the next `recompute_()` pass through `case End` would
re-clear the latches — meaning **only the very first NMI ever fired**.

**Fix** (committed): the `case End` body now advances `state_ = State::Idle`
unconditionally on the same recompute pass that clears the priority
latches. The `observe_cpu_wr` hook is retained as a no-op that updates
`prev_wr_n_` for forward compatibility with a future Z80-bus-callback
plumbing.

The coarse-tick approximation differs from VHDL only in the (unlikely)
case where an emulator tick spans a CPU write-cycle data phase. In our
per-instruction tick model that aligns with NMI service flow, the
approximation is faithful.

### NMI-4 — NR 0x02 readback bit 4 (iotrap) not composed

**File**: `src/core/emulator.cpp` NR 0x02 read handler.

VHDL zxnext.vhd:5891 places `nr_02_iotrap` at bit 4 of the NR 0x02 read,
defined at line 3885 as `nr_02_iotrap <= nr_da_iotrap_cause(1) or
nr_da_iotrap_cause(0);`. The cpp NR 0x02 read handler returned only
`NmiSource::nr_02_read()`, which omits bit 4 entirely.

`nr_da_iotrap_cause_` IS tracked in `Emulator` (set by the +3 floppy IO
trap at port 0x2FFD/0x3FFD; cleared by NR 0x02 writes with bit 4 low),
but its bit 4 contribution to NR 0x02 was missing.

**Fix** (committed): the NR 0x02 read handler now composes
`(nr_da_iotrap_cause_ & 0x03) != 0 → bit 4` into the returned byte.

### NR-2 — Slot 2-5 `$FF` fallback silently remaps to page 0

**File**: `src/core/emulator.cpp` NR 0x50..0x57 write handler loop.

VHDL has no `$FF`-special-case for ANY of NR 0x50..0x57 (line 4690-4699
just stores `nr_wr_dat` into `MMU<i>`). The "$FF means revert to legacy
auto-paging" pattern is a *firmware convention* implemented in the cpp
specifically for slots 0/1 (`engage_legacy_rom_paging`) and 6/7
(`engage_legacy_ram_paging`). Per VHDL zxnext.vhd:4611-4612 and 4677-4680,
these slots have automatic legacy-bank derivation that interacts with
`sram_rom` / `port_7ffd` / `port_dffd`; firmware writes `$FF` to ask the
FPGA to "go back to that derivation".

For slots 2-5 there is no such legacy derivation — they only respond to
explicit MMU writes. The cpp's previous fallback `mmu_.map_rom(i, 0)`
silently remapped these slots to physical page 0 on a `$FF` write, which
diverges from VHDL spec (page 0xFF should be stored verbatim and resolve
to "unmapped" in `Ram::page_ptr()`).

**Status**: latent. Per the EOD-23 G46(b) audit (memory note), no
current code path writes `$FF` to NR 0x52..0x55. The fix removes a
silent-corruption trap for any future supervisor path that might.

**Fix** (committed): slot-2-5 fallback now stores `0xFF` via
`mmu_.set_page(i, 0xFF)`, matching the verbatim-store VHDL semantics for
non-legacy slots.

## G46(b) cross-check

The G46(b) slide-cascade investigation (memory note, 2026-05-09 EOD-24)
isolated the slide trigger to `NEXTREG $8E,$03` (paging atomic), and the
"3 missing PUSHes / 3 extra POPs" stack divergence between RST $08 hits
#2 and #3 to user code at `$423C` (= reading font-glyph 'A' bytes in
slot 7 page $21 offset $1F59).

**None of the NMI / Multiface / port findings here are on the G46(b)
critical chain**. Specifically:

* NMI-3 (FSM stuck in End) cannot trigger spurious NMIs — it suppresses
  them. The "missing PUSHes" pattern rules out NMI-induced phantom pushes.
* NMI-2 (auto-clear at End) only affects the readback bit, not the FSM
  flow. Its old bug was visible only to software polling NR 0x02.
* NMI-1 / NMI-4 are correctness fixes outside the boot critical path.
* NR-2 (slot-2-5 $FF) is latent: no current `NR $5{2,3,4,5},$FF` write
  exists in the supervisor's code path (per the EOD-23 static audit:
  "11 NR $5x,$FF sites, ALL are NR $51,$FF").

The recent Wave 8 fix (`8242098`) for NR $51 was the first concrete fix
in this register family and correctly engaged legacy ROM auto-paging.
The slot-2-5 `$FF` fix here is a parallel hardening that does not change
any current boot trajectory.

## Open questions

1. **Multiface ROM-paging overlay** (Wave 1 E in TASK-8-MULTIFACE-PLAN):
   not yet wired. `multiface_.is_mem_active()` is consumed by DivMmc's
   automap-eligibility logic and by the NMI priority arbiter, but the
   8 KB ROM/RAM overlay at `$0000-$3FFF` is not yet routed through
   `Mmu`. Out of scope for this task; tracked in the Multiface plan.

2. **Bit 4 of NR 0x02 (iotrap) ack semantics**: VHDL clears
   `nr_da_iotrap_cause` on `nr_02_we = '1' AND nr_wr_dat(4) = '0'`. The
   cpp NR 0x02 write handler does this correctly (line 1542-1544). With
   NMI-4 now composing bit 4 into the read, NR 0xDA-based iotrap polling
   should work end-to-end.

3. **`nr_02_bus_reset` (bit 7 of NR 0x02 readback)**: not modelled in
   jnext. The VHDL signal is set on a Pi-driven external bus reset; for
   an emulator this is always 0. No in-tree software polls this bit.

4. **FSM tick granularity**: NmiSource's `tick(master_cycles)` ignores
   the `master_cycles` argument and runs `recompute_()` once per call.
   This is fine at our per-instruction tick boundary but may matter for
   sub-instruction NMI-edge precision (e.g. ExpBus pin asserting and
   releasing within a single Z80 instruction). Not currently exercised.

5. **NR 0x86-0x89 reset gating**: jnext applies the soft-reset reload
   unconditionally; VHDL gates on `nr_89_bus_port_reset_type='0'`. Power-on
   default is `'1'`, so the VHDL-correct behaviour is "leave NR 0x86-0x89
   alone on soft reset". Documented as approximation; not a load-bearing
   bug for any current test.

## Test status

Build: clean (`cmake -B build -DENABLE_QT_UI=ON` → `cmake --build`).

`ctest` (full suite, 36 targets): **36/36 PASS**.

Targeted post-fix runs:

* `nmi_tests` (40 rows): PASS
* `nmi_integration_tests`: PASS
* `multiface_tests`: PASS
* `nextreg_tests`: PASS
* `nextreg_integration_tests`: PASS
* `port_tests`: PASS
* `fuse_z80_tests` (1356/1356): PASS

## File diffs summary

* `src/peripheral/nmi_source.cpp` — fixes NMI-1, NMI-2, NMI-3.
* `src/core/emulator.cpp` — fixes NMI-4 and NR-2.
* `test/nmi/nmi_test.cpp` — updates row NR02-05 to encode the
  VHDL-faithful semantics.
