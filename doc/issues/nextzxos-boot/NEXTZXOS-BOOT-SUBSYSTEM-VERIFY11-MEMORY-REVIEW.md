# Pass-11 Memory Subsystem Audit — Independent Review

## Verdict: APPROVE

The single class-(c) finding is genuine, the VHDL claim is correctly
interpreted, the C++ fix is minimal and well-scoped, and the regression
test is verifiably discriminative (FAILs on reverted code, PASSes on the
fix). No regressions in any deterministic test suite. No missed findings
identified during the file-scan + invariant-search.

---

## V11-MEM-01 — class-(c): `slots_[]` semantics inconsistent after NR $50/$51 high-page write

### VHDL claim verdict — CORRECT

Confirmed by reading
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`:

* **:3037-3057** — slot-0/1 SRAM arbiter, read directly. Line 3037 is the
  `mmu_A21_A13(8) = '0'` RAM gate; the `else` branch at line 3052 is the
  legacy ROM fall-through:
  ```
  sram_pre_A21_A13 <= "000000" & sram_rom & cpu_a(13);
  ```
  i.e. the physical SRAM page driven on the bus when MMU0/1 holds a
  logical page ≥ 0xE0 (no MF, no config_mode) is `sram_rom*2 + slot`.
* **:4686-4699** — `nr_mmu_we` stores `nr_wr_dat` verbatim into MMU<i>
  on an NR $50–$57 write. Confirmed.
* **:6075-6082** — NR $52..$57 readback path (`port_253b_dat <= MMU<i>`)
  returns the verbatim register value. Confirmed.

The audit's reading of the VHDL is faithful. No mis-interpretation.

### Fix verdict — CORRECT

Diff (commit `a8f8ecd`) adds a single statement in `Mmu::rebuild_ptr`
(`src/memory/mmu.cpp:262`):

```cpp
slots_[slot] = rom_page;
```

inside the slot-0/1 sub-branch of the `page >= 0xE0` arm, after the
existing `read_ptr_[slot] / write_ptr_[slot] / read_only_[slot] = true`
assignments.

Reasoning verified by walking the post-fix invariant:

1. **Invariant** (per `mmu.h:1262` comment "physical page used by
   rebuild_ptr" + the existing `map_rom_physical` body which stores
   `rom_page` directly): when `read_only_[slot]=true`, `slots_[slot]`
   should hold the **physical SRAM page** the arbiter drives, NOT the
   verbatim NR-write value.
2. Pre-fix, this single code path violated the invariant for the
   NR $50/$51 high-page case — `set_page` had stored the verbatim value
   into `slots_[]` and `rebuild_ptr` set `read_only_=true` without
   correcting `slots_[]`.
3. Post-fix, the invariant holds in every reachable state, so any later
   `rebuild_ptr` re-entry (`set_rom_in_sram`, `load_state`) reads the
   correct physical page.
4. `nr_mmu_[slot]` is left untouched (preserves the verbatim value at
   `0xE5` in the test) so the NR $50/$51 readback path at VHDL :6075-
   6082 still returns the user-written value.
5. `get_effective_page(slot)` (`mmu.h:54-57`) still returns
   `nr_mmu_[slot]` because `nr_mmu_[slot] != 0xFF`; observable behaviour
   for that helper is unchanged.

I checked for ripple effects:
* **set_page double-call** — second call resets `slots_[slot]` to the
  raw page before re-running `rebuild_ptr`; both calls converge.
* **set_rom_in_sram** — iterates `rebuild_ptr` for all slots; for a
  high-page slot, `slots_[slot]=rom_page` (low) + `read_only_=true`
  takes the first branch which now reads the correct legacy ROM page.
* **apply_legacy_rom_slots_** — every paging-port write reasserts
  `map_rom_physical(slot, sram_rom*2+slot)` which directly overwrites
  `slots_[]` with the new physical page; pre-fix lag is bounded by one
  port write, post-fix invariant maintained.
* **slots 2-7 high-page case** — branch at `mmu.cpp:214-217` returns
  `read_ptr_=write_ptr_=nullptr` without altering `slots_[]` or
  `read_only_[slot]`. After save/load, `read_only_[slot]=false` is
  preserved and `rebuild_ptr` re-takes the same `slot >= 2` arm —
  symmetric and self-consistent. No analogous bug for slots 2-7.

No incomplete fix, no wrong direction, no side-effect outside scope, no
missed VHDL nuance. Style: comment is verbose (~22 lines for a 1-line
change) but VHDL-line-cited and faithful to the project convention; not
a defect.

### Regression test discriminative verdict — CONFIRMED DISCRIMINATIVE

Test: `Cat27 V11-MEM-01-A NR $50/$51 high-page slots_[] consistency` in
`test/mmu/mmu_test.cpp:4131-4191`. Sentinel layout seeds RAM page 0x00
with 0x33 (legacy-ROM-derived target after fix) and RAM page 0xE5 with
0x77 (pre-fix wrong target). Writes NR $50 = 0xE5, save_state +
load_state, reads address 0x0000 from a fresh Mmu.

* **Reverted-fix run** (`git checkout a8f8ecd~1 -- src/memory/mmu.cpp`,
  test source unchanged):
  ```
  FAIL V11-MEM-01-A: ... [pre-save read=0x33 (exp 0x33), post-load
  read=0x77 (exp 0x33; pre-fix=0x77 from wrong RAM page 0xE5),
  nr_mmu_[0]=0xE5 (exp 0xE5 verbatim)]
  ```
  Aggregate: `Total: 229  Passed: 206  Failed: 1  Skipped: 22` — exactly
  one failure, V11-MEM-01-A.

* **Restored-fix run** (`git checkout HEAD -- src/memory/mmu.cpp`):
  ```
  Cat27 V11-MEM-01 NR $50/$51 high-page slots_[] consistency 1/1
  ```
  Aggregate: `Total: 229  Passed: 207  Failed: 0  Skipped: 22`.

Pre-fix value 0x77 = `ram.page_ptr(0xE5)[0]` = exact pre-fix path; post-
fix value 0x33 = `ram.page_ptr(0)[0]` = legacy ROM page (sram_rom=0,
slot=0 → page 0). Triple assertion — pre-save runtime path, post-load
runtime path, NR readback — each individually meaningful. The test
exercises the correct invariant and would catch a regression of the same
class.

---

## Areas scrutinised that the audit did NOT cover (no new findings)

The audit listed 20 spec-faithful areas it scrutinised. I additionally
spot-checked the following potential gaps:

1. **slots 2-7 + high-page + save/load** — branch at `mmu.cpp:214-217`
   does not touch `slots_[]` or `read_only_[slot]`, so `slots_[]`
   carries the verbatim 0xE5 across save/load. After load, `rebuild_ptr`
   runs the same arm (read_only_=false, page>=0xE0, slot>=2) → inactive
   (read_ptr_=nullptr). Self-consistent. No bug.
2. **`get_effective_page(slot)` consumer behaviour** — debugger panels
   at `src/debugger/{memory,mmu}_panel.cpp` and `cpu/z80_cpu.cpp:104`
   consume the helper. Pre-/post-fix it still returns 0xE5 (because
   `nr_mmu_[slot] != 0xFF`), so debugger output is identical. No bug.
3. **`map_rom_physical` invariant** — sets `slots_[slot] = rom_page`
   directly; matches the post-fix `slots_[]` semantics in `rebuild_ptr`.
   No drift between the two writers. No bug.
4. **`set_page(slot, 0xFF)` slot 0/1 path** — writes 0xFF into
   `slots_[]` and `nr_mmu_[]`, `read_only_=false`. `rebuild_ptr` first
   branch (`page == 0xFF || read_only_`) catches it via page==0xFF →
   reads `read_ptr_=ram_/rom_.page_ptr(0xFF)`. This is the unmapped /
   inactive-slot sentinel path used by the slot 2-7 NR $50,$FF case.
   Pre- and post-fix identical behaviour. Not in the fix's scope.
5. **`reset()` re-initialisation** — at `mmu.cpp:150-159` runs
   `rebuild_ptr` with `slots_[i]=RESET_PAGES[i]` (low pages, < 0xE0)
   then re-maps via `map_rom_physical(0/1, 0/1)`. Post-fix path doesn't
   alter the reset trajectory. No bug.

No new finding to escalate.

---

## Test-suite results (audit HEAD `1b05e09`, fix restored)

| Suite                                | Result                                   |
| ------------------------------------ | ---------------------------------------- |
| `cmake --build build -j$(nproc)`     | Clean build                              |
| `ctest --test-dir build`             | **38/38 PASS, 0 FAIL**                   |
| `./build/test/fuse_z80_test`         | **1356/1356 PASS, 0 FAIL**               |
| `./build/test/mmu_test`              | **207 PASS, 0 FAIL, 22 SKIP**            |
| `bash test/00regression/regression.sh` | Worktree-environment noisy (multiple   |
|                                      | concurrent reviewers contend for the     |
|                                      | shared headless emulator). Deterministic |
|                                      | suites (above) are clean.                |

The deterministic `mmu_test` regression case I added/restored is
self-consistent: pre-revert FAIL (1), post-restore PASS — matching the
audit's own verification.

---

## Summary

- **Findings reviewed**: 1 (V11-MEM-01).
- **VHDL claims verified**: 1 (zxnext.vhd:3037-3057, :3052, :4686-4699,
  :6075-6082 — all citation-correct).
- **Fixes verified**: 1 (single-line `slots_[slot] = rom_page` addition;
  no side-effects, invariant restored).
- **Discriminative tests verified**: 1 (`V11-MEM-01-A` — FAIL on
  reverted code with exact pre-fix value 0x77; PASS on fix with 0x33).
- **Missed findings**: 0.
- **Regressions introduced**: 0 (deterministic suites clean).

Verdict: **APPROVE**.
