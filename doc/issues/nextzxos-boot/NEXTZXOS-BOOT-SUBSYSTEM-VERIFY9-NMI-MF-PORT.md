# Pass-9 — NextZXOS Boot Subsystem Verify (NMI / MF / Port / NextREG)

**Branch:** `task2/verify9-nmi-mf-port`
**Worktree:** `.claude/worktrees/task2-verify9-nmi-mf-port`
**Mandate:** strictest convergence — class-(a)/(b)/(c) ALL must be fixed; only
class-(d) "truly architectural" gaps may be carried.
**VHDL oracle:**
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`
+ `…/device/multiface.vhd`.

## Verdict

**CONVERGED at the strictest criterion.** The blind class-(c) audit identified
1 systemic gap (NR 0x80 ExpBus enable / disable_mem absent from the VHDL
`nmi_assert_expbus` gate) and 4 stale-comment / doc-clean issues. ALL of them
are fixed in this pass. After the fixes, no class-(a), class-(b), or class-(c)
items remain in the NMI / MF / Port / NextREG surface area covered by this
pass. The single remaining out-of-scope item (Stackless NMI = NR 0xC0 bit 3)
is class-(d) per the original `nmi_source.h` plan annotation and is documented
as such — it is a wholly separate Z80N-mode FSM that requires CPU-side
plumbing absent from jnext today.

## Class-(c) backlog disposition

| Item | Source | Disposition | VHDL anchor | jnext fix |
|------|--------|-------------|-------------|-----------|
| ExpBus producer ignores `expbus_eff_en` (NR 0x80 bit 7) | `nmi_source.cpp` `nmi_assert_expbus()` | **FIXED** (class-(c) → class-(a) elevated) | zxnext.vhd:2089, 2197, 5804/5811 | New `set_expbus_eff_en()` setter; gate added to `nmi_assert_expbus()`; fan-out from NR 0x80 write handler |
| ExpBus producer ignores `expbus_eff_disable_mem` (NR 0x80 bit 4) | `nmi_source.cpp` `nmi_assert_expbus()` | **FIXED** (class-(c) → class-(a) elevated) | zxnext.vhd:2089, 2200, 5806/5813 | New `set_expbus_eff_disable_mem()` setter; gate added; fan-out from NR 0x80 write handler |
| END→IDLE comment labelled "approximation" | `nmi_source.cpp` `recompute_()` State::End branch | **FIXED** (doc clean) | zxnext.vhd:2143-2147 | Comment rewritten — the per-instruction tick collapse is in fact spec-faithful, since `cpu_wr_n='1'` is guaranteed at instruction boundaries (NmiSource::tick is invoked between Z80 instructions, never mid-bus-cycle). No code change needed. |
| MF latch gating comment omitted `divmmc_nmi_hold` | `nmi_source.cpp` `recompute_()` priority section | **FIXED** (doc clean) | zxnext.vhd:2107 | Comment expanded to spell out all three AND terms; the code path was already correct. |
| `iotrap_strobe_pending_` documented as "stub not yet routed" | `nmi_source.h` private members | **FIXED** (doc clean) | zxnext.vhd:3837 | Routing has been live since Wave A; comment rewritten to describe the live VHDL OR-into-MF path. |
| MF consumer-feedback inputs documented as "stubbed false until Task 8" | `nmi_source.h` setter docs + `multiface.h` `is_nmi_hold` doc + `nmi_source.cpp` file header | **FIXED** (doc clean) | zxnext.vhd:2099, 2118 | Task 8 already wired both `set_mf_nmi_hold()` and `set_mf_is_active()` from `Multiface` accessors via Emulator pre-tick fan-out (`emulator.cpp:5057-5058, 5292-5293`); comments updated to reflect wired state. |
| Stackless NMI (NR 0xC0 bit 3) | `nmi_source.h` class doc | **CARRIED — class-(d)** | zxnext.vhd:2052-2086 | Z80N-mode FSM (NMIACK_LSB/MSB / RETN_LSB/MSB) requires CPU-side `Z80N_command_s` + `z80_stackless_retn_en` plumbing. Out of scope per original plan Q1 cut. Genuinely architectural — not a faithfulness gap in the present pipeline. |

## Class-(a) found and fixed

The two ExpBus gate omissions above (`expbus_eff_en`, `expbus_eff_disable_mem`)
were initially flagged class-(c) (documented intentional gap) but on audit
elevated to class-(a) — the previous code's `return !expbus_nmi_n_;` was a
genuine VHDL-faithfulness violation, not just a documented gap, because the
two AND terms are present in `zxnext.vhd:2089` verbatim. With no expbus device
in jnext today the violation is unobservable in normal play (the pin stays
idle, so the producer is always false either way), but it would cause a
silent divergence the moment any caller invoked `set_expbus_nmi_n(false)`
without first ensuring NR 0x80 bit 7 = 1 / bit 4 = 0.

## Class-(b) found and fixed

- **MF priority-latch comment** in `nmi_source.cpp` listed only
  `port_e3_reg(7)` as the AND term; VHDL line 2107 also requires
  `divmmc_nmi_hold = '0'`. Code was correct; comment was misleading.
  Comment expanded to match the code.
- **`iotrap_strobe_pending_` private-member comment** described the field as
  "Not currently routed to a producer; kept for a future wave" while in fact
  `nmi_assert_mf()` ORs the strobe into the MF assert path (live since Wave A).
  Comment rewritten.
- **MF consumer-feedback comments** in `nmi_source.h` and `multiface.h` still
  said "stubbed until Task 8" / "F-gate Wave will replace the existing stub";
  Task 8 already landed and wired both setters from Emulator. Comments
  updated.
- **`nmi_source.cpp` file header** still referenced "Consumer-feedback values
  for the MF side are stubbed false until Task 8 (Multiface) lands"; updated
  to acknowledge Task 8 + Pass-9 deltas.

## Files modified

| File | Class | Change |
|------|-------|--------|
| `src/peripheral/nmi_source.h` | (a) + (b) | Added `set_expbus_eff_en()`, `set_expbus_eff_disable_mem()`, accessors `expbus_eff_en()` / `expbus_eff_disable_mem()`, private fields `expbus_eff_en_` / `expbus_eff_disable_mem_`. Doc-cleans on stub comments. |
| `src/peripheral/nmi_source.cpp` | (a) + (b) | Reset clause for new fields. New setter implementations. `nmi_assert_expbus()` now checks the full VHDL gate. `save_state` / `load_state` persist new fields. End-IDLE comment rewritten. MF gating comment expanded. File header updated. |
| `src/peripheral/multiface.h` | (b) | Doc-clean: `is_nmi_hold` no longer says "F-gate Wave will replace the existing stub". |
| `src/core/emulator.cpp` | (a) | NR 0x80 write handler installs the fan-out to the new NmiSource setters. Initial-state push immediately before. |
| `test/nmi/nmi_test.cpp` | test | DMA-01 amended to set `expbus_eff_en(true)` first. GATE-08 row extended with the two new flags. New rows GATE-09 (eff_en=0 blocks producer), GATE-10 (disable_mem=1 blocks producer), GATE-11 (full positive path still latches). |
| `test/ctc/ctc_test.cpp` | test | DMA-04 amended to set `expbus_eff_en(true)` first. |

## Verification

### Build

```
LANG=C cmake -B build -DENABLE_QT_UI=ON
LANG=C cmake --build build -j$(nproc)
```

Both commands clean — no warnings or errors.

### CTest

```
LANG=C ctest --test-dir build --output-on-failure
…
100% tests passed, 0 tests failed out of 37
Total Test time (real) =   1.34 sec
```

### NMI test detail

The NMI suite exercises the new gates via three additional rows:

```
  GATE                 11/11
  …
  Total:   61  Passed:   55  Failed:    0  Skipped:    6
```

(skipped rows are pre-existing F-skips for stackless-NMI and other class-(d)
items, not regressions.)

### FUSE Z80

```
./build/test/fuse_z80_test build/test/fuse
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0
```

## Convergence verdict

**STRICT CONVERGENCE ACHIEVED.** Pass-9 closes every class-(a), class-(b),
and class-(c) item visible in the NMI / MF / Port / NextREG surface during
this blind audit. The only carried item is Stackless NMI (class-(d),
documented architectural gap). Tests: 37/37 ctest groups pass; FUSE
1356/1356; NMI suite 55 pass + 6 pre-existing skips. No regressions.

The "search for any other intentional simplification" sweep found no
remaining markers in `src/peripheral/{nmi_source,multiface}.{cpp,h}` or
`src/port/{nextreg,port_dispatch}.{cpp,h}` that meet the class-(c) bar.
Markers in adjacent subsystems (sd_card, i2c, dma) are out of scope for
this pass.
