# Independent Review — Memory Subsystem Test-Coverage Pass

**Reviewer branch**: `task2/testcov-memory-reviewer`
**Reviewed branch**: `task2/testcov-memory` (HEAD `af29460`)
**Review date**: 2026-05-10
**Reviewer**: Independent agent (different from the original test-coverage author)

This review evaluates the 29 regression test rows added by commit
`af29460` ("testcov(memory): regression rows for verify1..verify10
memory fixes") for **discriminative power** (would the test fail
pre-fix and pass post-fix?), VHDL citation accuracy, and integration
coverage. Each fix commit was inspected (`git show <hash>`), the
pre-fix code in the relevant subsystem files was loaded, and the
test was traced both ways.

---

## Verdict: **APPROVE-WITH-NITS**

The pass is **net-positive and lands**. 29 new test rows, all green;
counts match the agent's claims; full ctest 37/37 PASS; no
regressions. Most rows are genuinely discriminative against the
pre-fix bug they cite, and the per-fix coverage table is reasonably
honest.

However, **multiple defects** were uncovered that should be tracked
as follow-up work. None block landing of the pass — the rows still
guard against future regression of the underlying mechanism — but
several do not fully discriminate the original bug in the harness as
written, and the suite has a systemic gap on **emulator-handler
integration** for many fixes.

### Counts

| Status | Count | Description |
|---|---:|---|
| Discriminative | **22** | Test would fail pre-fix and pass post-fix in the harness as written |
| Partial / API-only | **5** | Test verifies the post-fix API contract but does NOT exercise the emulator-side caller that the actual fix introduced |
| Non-discriminative (defect) | **2** | Test passes both pre-fix and post-fix in the harness as written, due to Fixture `rom_in_sram_=false` masking the wrap-aliasing bug the test claims to catch |
| 2-fix-no-test rationale | 1 verified, 1 arguable | See below |

Total tests verified: **29 of 29** (full coverage; "sample at minimum 20" exceeded).

### Test status verified by reviewer

| Suite | Total | Pass | Fail | Skip |
|---|---:|---:|---:|---:|
| `mmu_test` | 228 | 206 | 0 | 22 |
| `contention_test` | 75 | 75 | 0 | 0 |
| `nextreg_integration_test` | 181 | 181 | 0 | 0 |
| `floating_bus_test` | 30 | 30 | 0 | 0 |
| Full `ctest` | 37/37 | 37 | 0 | 0 |

Build clean (Qt6 UI, full link). Counts match the agent's claims.

---

## Per-test reassessment

### `test/mmu/mmu_test.cpp` — Cat 27 (23 rows)

| # | Row | Fix | Status | Reviewer note |
|---|---|---|---|---|
| 1 | `FIX-NR5xFF-01` | `f832f38` | **PARTIAL** | Calls `engage_legacy_rom_paging_slot(1)` directly; verifies the per-slot helper does not clobber the other slot's RAM mapping. Does NOT verify that the emulator NR `$51,$FF` dispatcher invokes the per-slot helper instead of the bothslot `engage_legacy_rom_paging()`. A regression where the dispatcher reverts to the buggy call would not be caught. |
| 2 | `FIX-NR5xFF-02` | `f832f38` | **PARTIAL** | Calls `Mmu::set_page(2, 0xFF)` directly, bypassing the emulator NR `$52,$FF` dispatcher. The actual fix changed `emulator.cpp` from `map_rom(2, 0)` to `set_page(2, 0xFF)`. Test verifies that `set_page(slot, 0xFF)` results in an inactive slot, but does NOT discriminate against a regression of the emulator dispatcher. |
| 3 | `FIX-NR5xFF-03` | `f832f38` | **PARTIAL** | Same as FIX-NR5xFF-02 — direct `set_page(6, 0xFF)`, not via the NR-write integration path. Does not catch a regression where the dispatcher reverts to `engage_legacy_ram_paging()`. |
| 4 | `FIX-PLUS3-01` | `45d8b30` | **DISCRIMINATIVE** | `map_plus3_bank(0x07)` then `map_plus3_bank(0x00)` — this exercises the actual public path. Pre-fix would leave slots 2-5 stale; post-fix reverts to bank-5/bank-2. Confirmed against VHDL :4655-4670 cfg-(B,A) decode. |
| 5 | `FIX-PLUS3-02` | `45d8b30` | **DISCRIMINATIVE** | Enters special, then `map_128k_bank(0x05)` — exercises the apply_paging_update_ arbiter. Pre-fix `apply_legacy_paging_()` would clobber MMU6/7 to 0x0A/0x0B; post-fix special table wins (0x06/0x07). |
| 6 | `FIX-PLUS3-03` | `45d8b30` | **DISCRIMINATIVE** | Save/load round-trip of `port_1ffd_special_old_`. Pre-fix it wasn't persisted → load defaults to `false` → exit-special revert wouldn't fire → slots 2-5 stay at saved special-table values. Post-fix persisted → revert fires correctly. |
| 7 | `FIX-NR8C-CACHE-01` | `3dd4e73` | **DISCRIMINATIVE** | +3 with sram_rom transition via NR 0x8C `lock_rom1`. Pre-fix inline `set_nr_8c` did not refresh cached read_ptr_; post-fix calls `engage_legacy_rom_paging_slot()`. Fixture-tagged ROM bytes (page<<4 \| offset_lo) make the page change observable: 0x00 → 0x40. |
| 8 | `FIX-NR8C-CACHE-02` | `3dd4e73` | **WEAK / Companion test** | Tests that NR 0x8C with non-lock bits (0xC0) preserves explicit slot 0 RAM mapping. Pre-fix inline `set_nr_8c` was a no-op (didn't refresh anything), so this test would also pass pre-fix. The test is really discriminative against the **intermediate** verify3 over-refresh, not the original verify1 missing-refresh. Documented as PRESERVE-companion; arguable but not load-bearing — `FIX-NR8C-PRESERVE-01/02` cover the preserve angle more strictly. |
| 9 | `FIX-SLOT01-HIPAGE-01` | `3dd4e73` | **NON-DISCRIMINATIVE (defect)** | Test claims pre-fix would alias `ram[0x05][0]=0x77` due to wrap-aliasing `to_sram_page(0xE5) = 0x05`. **However Fixture has `rom_in_sram_=false`**, so `to_sram_page(0xE5) = 0xE5` (no shift). Pre-fix would read `ram.page_ptr(0xE5)[0] = 0` (uninitialized). Post-fix reads ROM page 0 byte 0 = 0x00. **Both pre-fix and post-fix return 0** — the test passes either way. The seeded sentinel at page 0x05 is never reached. To fix, the test should call `f.mmu.set_rom_in_sram(true)` or seed page 0xE5 with a sentinel and check that page 0xE5 is NOT read. |
| 10 | `FIX-UNLOCK-01` | `31d1786` | **DISCRIMINATIVE** | Sets bit 5 via `map_128k_bank(0x25)`; calls `unlock_paging()`. Verifies BOTH `paging_locked_` AND `port_7ffd_` bit 5 are cleared. Pre-fix only cleared the flag, not the port mirror. Asserts both pre and post state. |
| 11 | `FIX-NR8C-PRESERVE-01` | `31d1786` | **DISCRIMINATIVE** | `set_page(0, 0x05)` then `set_nr_8c(0x20)`. Pre-fix `apply_legacy_rom_slots_()` clobbered the explicit RAM mapping → `g0` would change to a ROM page, `ro0` → true. Post-fix gates on `read_only_[i]==true`, preserving the RAM mapping. |
| 12 | `FIX-NR8C-PRESERVE-02` | `31d1786` | **DISCRIMINATIVE** | Same pattern on slot 1, +3 machine. |
| 13 | `FIX-EFF7-FF-01` | `31d1786 + 560cb18` | **PARTIAL** | Calls `engage_legacy_rom_paging_slot(0)` directly under EFF7(3)=1. Verifies post-fix routes to legacy ROM (NOT RAM page 0/1) AND `nr_mmu_[0]=0xFF`. Does NOT verify the emulator NR `$50,$FF` dispatcher invokes the per-slot helper. Underlying contract is exercised; integration is not. |
| 14 | `FIX-NRMMU-SAVE-01` | `560cb18` | **DISCRIMINATIVE** | `set_page(0, 0xE5)` (which post-3dd4e73 sets `read_only_[0]=true` via the high-page legacy-ROM branch); save+load. Pre-560cb18 load_state re-derived `nr_mmu_[i] = read_only_[i] ? 0xFF : slots_[i]` → 0xFF. Post-fix persists nr_mmu_[8] verbatim → 0xE5. Discriminative when chained against the 3dd4e73 + 560cb18 stack. |
| 15 | `FIX-NR12-PROP-01` | `560cb18` | **PARTIAL** | Test calls `set_l2_active_bank(16)` directly (the new API). Does NOT verify that the emulator NR 0x12 handler invokes this method. A regression where someone removes `mmu_.set_l2_active_bank(layer2_.active_bank())` from the NR 0x12 handler would NOT be caught. The unit-level API contract is verified; the integration is not. |
| 16 | `FIX-RESET-CFG-01-A` | `165835d` | **DISCRIMINATIVE** | config_mode=0 + reset → boot_rom_en stays cleared. Pre-fix unconditional re-arm; post-fix gates on config_mode_. |
| 17 | `FIX-RESET-CFG-01-B` | `165835d` | **DISCRIMINATIVE** | config_mode=1 + reset → boot_rom_en re-armed. Companion to the negative case above; both together pin the gate behavior precisely. |
| 18 | `FIX-MTC-SPECIAL-01` | `165835d` | **DISCRIMINATIVE** | `map_plus3_bank(0x07)` then `set_machine_type(ZXN_ISSUE2)`. Pre-fix `apply_legacy_rom_slots_()` would clobber slots 0/1 with sram_rom-derived values; post-fix early-returns when `port_1ffd_(0)=1`, preserving the special-mapping pages 0x08/0x09. |
| 19 | `FIX-CURRSRAMROM-128K-01` | `b6b42dd` | **DISCRIMINATIVE** | ZX128K + `map_128k_bank(0x00)` (7ffd(4)=0) + `set_nr_8c(0x20)` (lock_rom1=1). Pre-fix ZX128K branch returned `(port_7ffd_>>4)&1 = 0` (bypassed altrom-lock). Post-fix shared else branch with Next returns `lock_rom1 ? 1 : 0 = 1`. |
| 20 | `FIX-L2-OVERLAY-LOWHALF-01` | `b6b42dd` | **DISCRIMINATIVE** | seg=01 + slot 0 RAM-mapped + write at 0x0000. Pre-fix seg-01 mask = 0x02 (only 0x4000-0x7FFF) → write falls through to MMU (page 0x20). Post-fix `l2_overlay_active_for(0x0000)` always-on for low half → write redirected to L2 page 0x12 (bank=8, offset_pre=1). The test asserts both halves of the disjunction (mmu_side != 0xCC AND l2_side == 0xCC), making it precise. |
| 21 | `FIX-L2-OVERLAY-LOWHALF-02` | `b6b42dd` | **DISCRIMINATIVE** | Same pattern with seg=10; post-fix L2 page 0x14. |
| 22 | `FIX-L2-ROM-AREA-01` | `9d252b6` | **DISCRIMINATIVE** | bank=0x70 read at 0x0000. Post-fix gate fires → returns 0xFF. Pre-fix reads `ram.page_ptr(0xE0)[0] = 0`. Test asserts `v == 0xFF`. Pre-fix would yield 0, fail. |
| 23 | `FIX-L2-ROM-AREA-02` | `9d252b6` | **NON-DISCRIMINATIVE (defect)** | bank=0x70 write at 0x0000. Test seeds `ram.page_ptr(0x00)[0]=0x55` and asserts `after == 0x55`. Pre-fix in Fixture mode (rom_in_sram_=false) writes to `ram.page_ptr(0xE0)[0]` (NOT page 0x00). So page 0x00 stays at 0x55 in **both pre-fix and post-fix** — the test passes either way. The "wrap-aliasing into ROM-in-SRAM" bug only fires when rom_in_sram_=true; Fixture's default `false` masks the bug entirely. To fix: enable rom_in_sram_, or seed page 0xE0 with a sentinel and assert it is NOT modified. |

### `test/contention/contention_test.cpp` — CT-CAT27 + MEMPG (5 rows)

| # | Row | Fix | Status | Reviewer note |
|---|---|---|---|---|
| 24 | `FIX-CONTEND-NR03-01` | `f5ec6d8` | **PARTIAL** | Calls `cm.rebuild_for_type(MachineType::ZX_PLUS3)` directly. Verifies the new API preserves `port_7ffd_io_en` (vs `build()` which clears it) AND updates the per-machine bank-decode LUT. **Does NOT verify the integration**: the actual fix added the call from `emulator.cpp` NR 0x03 handler. A regression where the handler reverts to `contention_.build()` would not be caught. The test also doesn't validate preservation of `mem_active_page_`, `cpu_speed_`, `contention_disable_/_shadow` — only `port_7ffd_io_en_`. |
| 25 | `FIX-CONTEND-7FFD-01` | `f5ec6d8` | **DISCRIMINATIVE** | 128K + 0x7FFD + io_en transition. Pre-fix bare-class `port_contend()` dropped the term entirely → returns false. Post-fix returns true with io_en=1 (and false with io_en=0). Address decode bits verified against VHDL :2593: cpu_a(15)=0, cpu_a(1:0)=01, cpu_a(13:12)≠01, machine type 128K/+3. |
| 26 | `FIX-CONTEND-7FFD-02` | `f5ec6d8` | **DISCRIMINATIVE** | 48K + io_en=1 → still false (gate is type-128K/+3). Verifies the type guard. |
| 27 | `FIX-CONTEND-7FFD-03` | `f5ec6d8` | **DISCRIMINATIVE** | +3 + 0x7FFD + io_en → true. Verifies the +3 cpu_a(14)=1 additional condition. |
| 28 | `FIX-MEMACTIVE-PAGE-01` | `a9cbf79` | **PARTIAL** | Tests `Mmu::get_page(0)/get_page(1)` returns 0xFF for legacy-ROM slots 0/1 at 128K reset. **The fix was in `z80_cpu.cpp::mem_active_page_for()`** which switched from `get_effective_page` to `get_page`. The test verifies the underlying contract (`get_page` returns the nr_mmu_ value, not the resolved physical page) but does NOT exercise the CPU caller. A regression where someone reverts the cpu-side call to `get_effective_page` would not be caught — the agent's fixture only checks the Mmu accessor. The test's sole discriminative claim is "Mmu::get_page is correct"; that contract was already correct pre-fix. |

### `test/nextreg/nextreg_integration_test.cpp` — Cat27-NR08-Effective (1 row)

| # | Row | Fix | Status | Reviewer note |
|---|---|---|---|---|
| 29 | `FIX-NR08-EFFLOCK-01` | `b6b42dd` | **DISCRIMINATIVE** | Strong integration test. Reads NR 0x08 via `nr_read(emu, 0x08)` which goes through the real port path (`out 0x243B; in 0x253B`). Establishes raw lock via port_7FFD bit 5; verifies bit 7 = 0 (raw lock asserted, both pre and post). Enables Pentagon-1024 via `write_nr_8f(0x03)` with EFF7(2)=0; verifies `effective_paging_locked()` is now false; verifies bit 7 = 1 (post-fix uses `effective_paging_locked()`, pre-fix used raw `paging_locked()`). Crisp 3-way assertion (raw_b7=0 ∧ eff_locked=0 ∧ pent_b7=1) precisely pins the fix. |

---

## Two-fix-no-test rationale verification

The agent claims two fixes do not require new tests:

### 1. Verify8 A4 (+3 floating-bus port read uses `effective_paging_locked`)

Agent's claim: "mode-impossible to observe — Pentagon-1024 is a Next/Pentagon-mode feature; on +3 the override is mode-impossible."

**Reviewer verdict: ARGUABLE / WEAK**

`pentagon_1024_en()` depends on `nr_8f_mode_` and `port_eff7_reg_2_` only — there is **no machine_type gate** in either signal. Firmware on a +3 emulator could write `NR 0x8F = 0x03` and clear EFF7(2) deliberately. The +3 floating-bus port handler is gated on `config_.type == ZX_PLUS3` but the inner `effective_paging_locked()` term IS exercisable.

That said, real-world firmware on a +3 instance does not configure Pentagon-1024, so the path is functionally inert. The agent's claim survives in practice but not in principle. A discriminative test would be straightforward: build the emulator with `MachineType::ZX_PLUS3`, lock paging via 7FFD(5), enable NR 0x82(4) (port_p3_floating_bus_io_en), enable NR 0x8F=0x03 + EFF7(2)=0, then read port 0x0FFD. Pre-fix returns 0xFF (raw lock asserted); post-fix returns the floating-bus byte (effective lock dropped by Pentagon-1024 override).

**Recommendation: add a discriminative test in a follow-up pass. Not blocking.**

### 2. Verify9 A1 (`mem_contend_for_` per-page latch decode)

Agent's claim: covered by existing CT-FB-01..04 rows + the new FIX-MEMACTIVE-PAGE-01 row.

**Reviewer verdict: PARTIALLY VERIFIED**

CT-FB-01..04 do exercise the integration (`emu.mmu().read(...)` then `emu.mmu().p3_floating_bus_dat()`); they confirm the latch updates on contended access. However, FIX-MEMACTIVE-PAGE-01 is itself partial (see #28 above) — it pins the Mmu accessor contract, not the CPU-side call. The combined coverage is adequate but not pristine.

**Recommendation: accept agent's rationale; add a CPU-integration row in a follow-up if a stronger guarantee is desired.**

---

## Coverage gaps the agent missed

1. **Systemic: emulator-handler integration not covered.** Five tests (FIX-NR5xFF-01/02/03, FIX-EFF7-FF-01, FIX-NR12-PROP-01, FIX-CONTEND-NR03-01, FIX-MEMACTIVE-PAGE-01) verify the underlying API but bypass the emulator NR-write dispatcher / CPU-side caller that the actual fix changed. A future refactor that reverts the dispatcher / caller code would not be caught.

   **Suggested follow-up**: add integration-level rows in `nextreg_integration_test.cpp` that go through `nr_write(emu, ...)` and observe the side effect (e.g. NR 0x12 → write to 0x0000 with L2 read enabled → verify the new bank is consulted).

2. **FIX-L2-ROM-AREA-02 non-discriminative defect.** The test passes pre-fix in Fixture mode because `rom_in_sram_=false` causes `to_sram_page(0xE0)` to return 0xE0 (not 0x00), so the seeded sentinel at page 0x00 is never reached. Recommend either:
   - call `f.mmu.set_rom_in_sram(true)` before the test, OR
   - seed page 0xE0 with a sentinel and assert it is NOT modified post-fix.

3. **FIX-SLOT01-HIPAGE-01 non-discriminative defect.** Same root cause as #2 — Fixture's default `rom_in_sram_=false` masks the wrap-aliasing bug the test claims to catch. Both pre-fix and post-fix return 0 in this harness. The test's narrative is correct; the harness setup is wrong.

4. **Verify8 A4 not covered.** See "two-fix-no-test rationale" above.

5. **NR 0x12 integration regression.** As above; the API exists by contract but no row exercises the NR-write path that wires the API in.

6. **FIX-CONTEND-NR03-01 dynamic-state preservation undertested.** The fix preserves `mem_active_page_`, `cpu_speed_`, `contention_disable_`, `contention_disable_shadow_`, AND `port_7ffd_io_en_`. The test only verifies `port_7ffd_io_en_` preservation. A regression that preserves only some fields would partially pass.

---

## Code quality nits

1. **VHDL line citations**: spot-checked `:4686-4696` (nr_mmu_we), `:3061` (sram_pre_active gate), `:4623-4684` (+3 special table), `:5109-5111` (boot_rom_en gate), `:2997-3007` (sram_rom shared else branch), `:2971`/`:3101-3102` (L2 layer2_A21_A13 gate), `:2949-2956` (mem_active_page MUX), `:4489-4493` (mem_contend gate), `:5906` (NR 0x08 readback), `:3769` (port_7ffd_locked effective). All citations correctly correspond to the VHDL source — good citation discipline.

2. **FIX-SLOT01-HIPAGE-01 / FIX-L2-ROM-AREA-02 narratives**: the inline test comments describe behavior that depends on `rom_in_sram_=true`, but the Fixture default is `rom_in_sram_=false`. Comments are misleading; tests are non-discriminative as a result. Same defect mechanism in both rows (#9 and #23).

3. **FIX-NR8C-CACHE-02 weak rationale**: the row is documented as "gentle smoke test"; that's accurate. It IS discriminative against the verify3 (intermediate) over-refresh fix, and it IS a valid PRESERVE companion. The agent's report could be more explicit that PRESERVE-01/02 carry the discriminative load and CACHE-02 is supplementary.

4. **Group counts**: ctest output shows the new groups passing (Cat27 fix-regression NR $5x,$FF 3/3, …, CT-CAT27 4/4, CT-CAT27-MEMPG 1/1, Cat27-NR08-Effective 1/1). 3+3+3+1+2+1+1+1+2+1+1+2+2 + 4 + 1 + 1 = **29** rows. Tally is correct.

5. **Save-state schema growth**: `FIX-PLUS3-03` and `FIX-NRMMU-SAVE-01` correctly use `StateWriter::position()` to size the buffer dynamically — robust against future schema additions. Good practice.

6. **Cosmetic**: ordering of new test functions in `int main()` matches their declaration order — readable.

---

## What landed well

- **Build clean**: no warnings, no extra dependencies (besides `core/saveable.h` which was already used elsewhere).
- **Test counts match**: pre-pass 205/183/0/22, post-pass 228/206/0/22 → +23 rows in mmu_test (matches claim). Contention 70 → 75 (+5). NextReg integration 180 → 181 (+1). Floating bus unchanged. Total +29.
- **All 37 ctest targets pass.**
- **VHDL citations accurate** in every row inspected.
- **Coverage table in the agent's report** correctly distinguishes NEW vs PRE rows; the PRE claims for `MTC-01..03` (covering verify7 set_machine_type fix) and `ROM-10..12` (covering verify7 sram_rom3) were verified against actual test code — both check out.

---

## Recommendations (priority order, all FOLLOW-UP not blocking)

1. **Fix the two non-discriminative rows** (FIX-SLOT01-HIPAGE-01, FIX-L2-ROM-AREA-02) by either enabling `rom_in_sram_=true` or relocating sentinels.
2. **Add 5 emulator-integration rows** for the API-only tests (NR 0x12 dispatcher, NR 0x50/0x51 = $FF dispatcher for slots 2-7, NR 0x03 → contention rebuild, mem_active_page_for CPU-side call, NR `$5x,$FF` per-slot dispatcher). These should go through the port-write path (`nr_write(emu, ...)` or `port.out(...)`) and observe the side effect.
3. **Add FIX-FB-EFFLOCK-01** discriminative test for verify8 A4 (+3 floating bus + Pentagon-1024).
4. **Strengthen FIX-CONTEND-NR03-01** to verify ALL 5 dynamic gate fields are preserved across `rebuild_for_type`, not just `port_7ffd_io_en_`.

---

## Conclusion

The pass adds 29 well-organized regression rows that genuinely guard against the most likely refactor regressions in the memory subsystem fix chain. Of those, **22 are fully discriminative**, **5 are partial** (verify the underlying contract but not the integration), and **2 are non-discriminative** in the harness as written (defects). The 2-fix-no-test rationale is mostly verified, with one weak point (verify8 A4) recommended for follow-up.

**Verdict: APPROVE-WITH-NITS.** The pass lands; the listed defects and gaps should be tracked as follow-up work but do not block merge.

**No changes recommended to the agent's tests as part of this review** (per the prompt's "Do NOT modify the agent's tests" constraint). All defects and gaps are documented here for the next pass to address.
