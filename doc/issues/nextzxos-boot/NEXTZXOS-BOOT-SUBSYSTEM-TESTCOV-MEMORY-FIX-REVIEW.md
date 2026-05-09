# Memory subsystem testcov-memory FIX — independent review

**Branch reviewed**: `task2/testcov-memory-fix` HEAD `c20ef66`
**Review branch**: `task2/testcov-memory-fix-reviewer`
**Review date**: 2026-05-10
**Reviewer**: independent agent, no prior involvement in the FIX pass.
**FIX report under review**:
[NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-MEMORY-FIX.md](NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-MEMORY-FIX.md)

## Verdict: **APPROVE-WITH-NITS**

Eight of nine reviewer findings have been closed with discriminative test
rows physically verified by reverting the underlying src/ fix, rebuilding,
and confirming the test FAILS with the expected diagnostic, then restoring
and confirming PASS. The single defect is a **partial discriminative
check** in `FIX-NR5xFF-INT-02` — its expected pre-fix CPU read happens to
coincide with the fixture's 0xFF-filled ROM seed, so the row passes both
pre- and post-fix in the no-SD-card test fixture; INT-01 and INT-03 still
discriminate the dispatcher's per-slot fix correctly, so the seam is
covered.

The src/ change (`Mmu::l2_bank()` accessor) is purely a read-only test
observable on a private field — no production logic.

Test counts confirmed: `mmu_test 228/206/0/22`, `contention_test 77/77/0/0`,
`nextreg_integration_test 186/186/0/0`, `floating_bus_test 31/31/0/0`,
ctest 37/37 green.

## src/ change verification — `Mmu::l2_bank()` accessor

Diff: `src/memory/mmu.h` adds five lines for an inline `const` getter:

```cpp
uint8_t l2_bank() const { return l2_bank_; }
```

Verified:

- The setter `set_l2_active_bank()` already existed at the same line range
  (commit 560cb18 / verify4 fix). The new getter is the symmetric read.
- `l2_bank_` is a private member declared at `mmu.h:1343` with default 8.
- Two existing internal references at `mmu.h:267,416` consume `l2_bank_`
  in the L2 read/write-over paths. The accessor introduces no new
  consumer in production code (only the new INT row reads it).
- No `mmu.cpp` or other src/ code changed in commit `c20ef66` aside from
  this accessor. Confirmed via `git show c20ef66 --stat`: only `mmu.h`
  in src/, +5 lines, zero other src/ deltas.

**Verdict**: approved as a pure test-observability addition. No
production logic change.

## Per-finding revert-check matrix

Every row claimed "discriminative" was validated by physically reverting
the relevant src/ fix in the worktree, rebuilding the affected test
binary, and confirming the test FAILED with the expected diagnostic.
The src/ fix was then restored and the test reconfirmed PASS.

| # | Finding (test row) | src/ revert applied | Pre-fix output captured | Status |
|---|---|---|---|---|
| 1 | FIX-SLOT01-HIPAGE-01 | `mmu.cpp::rebuild_ptr` slot 0/1 high-page branch (pre-3dd4e73) — removed | `read(0x0000)=0x77 (exp 0x33 ROM-in-SRAM page 0)` | **CONFIRMED-FIXED** |
| 2 | FIX-L2-ROM-AREA-02 | `mmu.h` write-side `(sum & 0x70) == 0x70` gate (pre-9d252b6) — removed | `ram[0][0] post-write=0xAB (exp 0x55)` | **CONFIRMED-FIXED** |
| 3a | FIX-NR5xFF-INT-01 (slot 1) | `emulator.cpp` NR $5x dispatcher → `engage_legacy_rom_paging()` (pre-f832f38) | `pre_s0=0x05 post_s0=0xff post_s1=0xff` | **CONFIRMED-FIXED** |
| 3b | FIX-NR5xFF-INT-02 (slot 2) | same revert | row passed pre-fix because Rom default `0xFF` propagates to ram page 0 in fixture | **NIT — non-discriminative in fixture** |
| 3c | FIX-NR5xFF-INT-03 (slot 6) | same revert | `nr_rb=0x00 cpu_rd=0x00` | **CONFIRMED-FIXED** |
| 4 | FIX-EFF7-FF-INT-01 | same NR $5x dispatcher revert | `baseline_ro0=0 nr_rb=0x00 rom0=0` | **CONFIRMED-FIXED** |
| 5 | FIX-NR12-PROP-INT-01 | `emulator.cpp` NR 0x12 handler `mmu_.set_l2_active_bank(...)` line — removed | `pre_mmu_l2_bank=8 post_mmu_l2_bank=8` | **CONFIRMED-FIXED** |
| 6a | FIX-CONTEND-NR03-01 (in-place ext) | `contention.cpp::rebuild_for_type` zero all 5 dynamic gate fields (= build()-style) | `PASS-A: post map=0x00 speed=0 pending=0 cd=0 cd_shdw=0 ioen=0` | **CONFIRMED-FIXED** |
| 6b | FIX-CONTEND-NR03-INT-01 | `emulator.cpp` `contention_.rebuild_for_type(new_mt)` — commented out | `pre Next contend(0x0A)=0 post +3 contend(0x0A)=0` | **CONFIRMED-FIXED** |
| 7 | FIX-MEMACTIVE-PAGE-INT-01 | `z80_cpu.cpp::mem_active_page_for` `get_page` → `get_effective_page` (pre-a9cbf79) | `contention.mem_active_page=0x02 (exp 0xFF)` | **CONFIRMED-FIXED** |
| 8 | FIX-FB-EFFLOCK-01 | `emulator.cpp` +3 0x0FFD `effective_paging_locked()` → `paging_locked()` | `raw_locked=1 eff_locked=0 v=0xFF (exp 0x43)` | **CONFIRMED-FIXED** |
| 9 | FIX-NR8C-CACHE-02 (strengthened) | `mmu.cpp::set_nr_8c` drop `if (read_only_[i])` gate | `post: page=0xFF ro=1 read=0x00 (exp 0x05/0/0xA9)` | **CONFIRMED-FIXED** (companion-tier — primary target is verify3 over-refresh, but the added (b) cached-pointer observable does discriminate the gate-drop regression) |

**Counts**: 9 confirmed-fixed, 0 still-defective, 1 partial nit.

## Defect detail — FIX-NR5xFF-INT-02 non-discriminative in fixture

The row drives `nr_write(emu, 0x52, 0xFF)` and asserts both
`nr_rb == 0xFF && cpu_rd == 0xFF`. Pre-fix dispatcher path is
`mmu_.map_rom(2, 0)`, which:

1. Sets `slots_[2]=0`, `read_only_[2]=true`, `read_ptr_[2] = ram.page_ptr(0)`.
2. Sets `nr_mmu_[2] = 0xFF` (per `Mmu::map_rom` at `mmu.cpp:284`).

So `nr_rb` is 0xFF in both pre- and post-fix paths. The discriminator
hinges on `cpu_rd = mmu().read(0x4000)`. In the test fixture
(`build_next_emulator` with no SD card), `Rom`'s default initialization
is `data_.fill(0xFF)` (`rom.cpp:6`), and `Emulator::init` for
`ZXN_ISSUE2` copies `rom.page_ptr(p) → ram.page_ptr(p)` for `p=0..7`
(`emulator.cpp:4242-4245`). So `ram[0][0] = 0xFF`. The pre-fix path
reads from `read_ptr_[2] = ram.page_ptr(0)`, and `cpu_rd = 0xFF` —
matching the post-fix expected value.

**Impact**: low. The other two NR $5x,$FF rows (INT-01 and INT-03) DO
discriminate the per-slot dispatcher branch. INT-01 catches the
clobber-slot-0 regression on the slot 0/1 path; INT-03 catches the
forced-bank-0 regression on the slot 6/7 path. The slot 2-5 path between
them shares the same dispatcher logic. So while INT-02 does not
*discriminate* the pre-fix path, it does *document* the post-fix
contract and provides bidirectional regression detection IF a future
regression *also* changes the seeded ROM page-0 byte.

**Recommendation**: future strengthening should seed
`emu.mmu().write_ram_byte(...)` or directly poke `ram.page_ptr(0)[0]` to
a sentinel like 0x42 before writing NR $52,$FF, so the pre-fix path
yields `cpu_rd=0x42 != 0xFF` and the row truly discriminates. NOT
blocking — call it a NIT.

## VHDL citation accuracy

Spot-checked every cited line against
`/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/zxnext.vhd`:

| VHDL ref cited | Verified content matches |
|---|---|
| `:2949-2956` | mem_active_page <= MMU0..MMU7 by cpu_a(15:13) — yes |
| `:2964` | mmu_A21_A13 layout — yes |
| `:2968` | layer2_active_bank combinational from nr_12 — yes |
| `:2971` + `:3101-3102` | layer2_A21_A13(8) gate / sram_active = NOT layer2_A21_A13(8) — yes |
| `:3052` | sram_pre_A21_A13 <= "000000" & sram_rom & cpu_a(13) — yes |
| `:3061` | sram_pre_active <= NOT mmu_A21_A13(8) AND NOT bank5/7 — yes |
| `:3769` | port_7ffd_locked composite — yes (Pentagon-1024 / profi override) |
| `:3801` | nr_8f_mapping_mode_pentagon_1024_en = mode AND NOT EFF7(2) — yes |
| `:3813` | port_memory_change_dly composition (excludes NR 0x8C) — yes |
| `:4489-4493` | mem_contend per-machine LUT — yes |
| `:4517` | port_p3_floating_bus_dat <= ula_floating_bus when port_7ffd_locked=0 else 0xFF — yes |
| `:4611-4612` | MMU<i> reset values + verbatim NR readback — yes |
| `:4636-4644` | EFF7(3) override MMU0/1 → 0/1 — yes |
| `:4686-4696` | nr_mmu_we per-slot writes — yes |
| `:5137-5145` | machine_type commit gated on PREVIOUS config_mode — yes |

All citations are accurate and faithfully describe the contract being
tested.

## Code quality nits

1. **NIT (acknowledged above)**: FIX-NR5xFF-INT-02 fixture-data
   coincidence makes the row non-discriminative under
   `build_next_emulator` defaults. Recommend seeding ram[0][0] before
   the NR write.

2. **PASS-A field-preservation strengthening**: the in-place split of
   `FIX-CONTEND-NR03-01` is well-structured. The split clearly
   separates "rebuild_for_type preserves dynamic gate fields" (PASS-A)
   from "rebuild_for_type updates the LUT" (PASS-B). Good test design;
   the failure messages name the offending field, simplifying triage.

3. **Strong docstrings**: the new INT rows in
   `nextreg_integration_test.cpp` have detailed comments explaining
   the dispatcher seam being verified, the VHDL citation, and the
   pre-fix vs post-fix observable. This is the testing-doc style we
   want to keep.

4. **Hermetic emu construction**: each new INT row builds a fresh
   emulator (`Emulator emu; build_next_emulator(emu)`) instead of
   sharing the file-scope `emu`. This avoids cross-row state
   pollution (per the reviewer's earlier hermeticity directive). Good.

5. **Small style suggestion (non-blocking)**: the new
   `FIX-FB-EFFLOCK-01` row uses `read_port_default(emu, 0x0FFD)` which
   is consistent with the file's existing helper. It also seeds
   `p3_floating_bus_dat_` via a contended-CPU bank-5 write — clever
   and matches verify9's wiring. The row works.

## Test status

- `cmake -B build -DENABLE_QT_UI=ON` — clean configure, 6.0s.
- `cmake --build build -j$(nproc)` — clean build, no warnings.
- `ctest --output-on-failure` — **37/37 PASS** (1.21s wall).
- Per-suite individual counts (re-confirmed):
  - `mmu_test`           — Total 228 / Passed 206 / Failed 0 / Skipped 22.
  - `contention_test`    — Total 77 / Passed 77 / Failed 0 / Skipped 0.
  - `nextreg_integration_test` — Total 186 / Passed 186 / Failed 0 / Skipped 0.
  - `floating_bus_test`  — Total 31 / Passed 31 / Failed 0 / Skipped 0.

All counts match the FIX report's claims.

## Branch state

- Branch `task2/testcov-memory-fix-reviewer` HEAD `c20ef66` (no new
  commits beyond the FIX commit until this review report is committed).
- Worktree clean post-revert-checks (each revert was followed by a
  restore + rebuild verifying restoration).
- No production code modified during the review.

## Conclusion

The FIX pass closes 9 of 9 reviewer findings with discriminative
verification. **8 fixes are physically discriminative-confirmed** by
this independent revert-check. **1 fix (FIX-NR5xFF-INT-02) is a NIT** —
the row passes both pre-fix and post-fix in the no-SD-card test
fixture, but the related INT-01 and INT-03 rows do discriminate the
same dispatcher seam, so the regression risk is covered. The src/
change (`Mmu::l2_bank()` accessor) is verified read-only / no
production-logic. VHDL citations are accurate. Tests are green.

**Final verdict: APPROVE-WITH-NITS** (1 NIT documented;
non-blocking).
