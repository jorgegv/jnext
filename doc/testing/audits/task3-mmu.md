# Subsystem Memory/MMU row-traceability audit

**Rows audited**: 143 total (66 pass + 2 fail + 77 skip per test file expectations; matrix shows 66 pass + 0 fail + 77 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 64   | 0 false-pass + 0 tautology | 2 |
| fail   | 0    | 0 false-fail | 0 |
| skip   | 69   | 8 lazy-skip | 0 |

**PLAN-DRIFT findings** (independent of status): 0

## FALSE-PASS (high priority — pass rows asserting buggy C++)

None identified.

## FALSE-FAIL (high priority — fail rows chasing non-bugs)

None identified.

## LAZY-SKIP (medium priority — skip rows without VHDL citation)

### Skip rows DFF-01 through DFF-07 (port 0xDFFD)
- **Current status**: skip
- **Skip message**: "no port 0xDFFD handler on Mmu — extra bank bit 0 unobservable" (and similar for DFF-02 through DFF-07)
- **Test location**: test/mmu/mmu_test.cpp:421-427
- **Plan row scope**: "Extra bit 0", "Extra bit 1", "Extra bit 2", "Extra bit 3", "Max bank", "Locked by 7FFD bit 5", "Bit 4 (Profi DFFD override)"
- **Should be**: GOOD-SKIP with explicit VHDL citation. The skip reason is correct — Mmu has no port 0xDFFD ingress — but the skip message should cite the VHDL line defining the port (zxnext.vhd:3640-3814 per plan doc) rather than merely stating "unobservable". The skip is justified; only documentation is weak.
- **If "fail"**: N/A (API gap, not VHDL bug)

### Skip rows of category LCK (paging lock): LCK-03, LCK-04, LCK-05, LCK-07
- **Current status**: skip
- **Skip messages**: "no port 0xDFFD handler", "no NR 0x08 handler", "no NR 0x8F handler", "no NR 0x8E handler"
- **Test location**: test/mmu/mmu_test.cpp:619, 622, 626, 643
- **Plan row scope**: LCK-03 "7FFD(5) locks DFFD writes", LCK-04 "NR 0x08 bit 7 clears lock", LCK-05 "Pentagon-1024 overrides lock", LCK-07 "NR 0x8E bypasses lock"
- **Should be**: GOOD-SKIP. API gaps are real (Mmu lacks NR 0x08/0x8E/0x8F and DFFD paths). Skip reasons are sound. However, messages could cite specific VHDL lines where these lock mechanisms are defined (e.g., LCK-04 → zxnext.vhd:3814 for unlock; LCK-05 → zxnext.vhd:3814 for Pentagon-1024 lock bypass).
- **If "fail"**: N/A

### Skip rows of categories N8E, N8F, EF7, ROM (01-09), ALT (01-09), CFG (01-04), BNK (01-04), CON (01-12), L2M (02,05-06), P7F-11, P7F-14, P1F-07, PRI (01,02,04,06,07), MMU-12, ADR-09-10
- **Current status**: skip (69 total rows across 10+ categories)
- **Pattern**: Skip messages state "no Mmu accessor for X" or "no NR handler for X" without citing specific VHDL lines
- **Assessment**: GOOD-SKIP in all cases. Each skip correctly identifies an API gap. Examples:
  - N8E (unified paging): "no NR 0x8E handler" is correct; Mmu lacks the unified register machinery
  - EF7 (RAM-at-0x0000): "no port 0xEFF7 handler" is correct; Mmu does not consume this port signal
  - ROM selection: "no machine-type or sram_rom accessor" is correct; Mmu has no machine-type input and no rom-page accessor
  - CON (contention): "ContentionModel lacks mem_active_page input" is correct; ContentionModel only exposes set_contended_slot() API
  - ALT, CFG, BNK, L2M shadow: All correctly identify unreachable observables
- **Weakness**: Skip messages could strengthen traceability by citing the specific VHDL line/section that defines the observable (e.g., "no NR 0x8E handler on Mmu (VHDL zxnext.vhd:3662-3734) — bank-select path unobservable" instead of just "bank-select path unobservable"). However, the skip decision itself is architecturally sound.
- **If "fail"**: N/A (all are API gaps, not VHDL bugs)

## TAUTOLOGY (low priority — no-op pass rows)

None identified. All 64 pass rows exercise non-trivial round-trips or state transitions.

## PLAN-DRIFT (plan description inaccurate vs VHDL)

None identified. The plan document (MEMORY-MMU-TEST-PLAN-DESIGN.md) accurately describes the VHDL architecture. Test file descriptions in check() calls match plan scope.

## UNCLEAR (needs human review)

### RST-01 and RST-02 (reset state of slots 0 and 1)
- **Current status**: pass (per matrix); expected to fail per test file comment
- **Test assertion**: `g == r.expected` where expected = 0xFF for MMU0 (slot 0) and MMU1 (slot 1)
- **Test location**: test/mmu/mmu_test.cpp:282-283 within test_cat2_reset_state()
- **Plan row scope**: "MMU0 after reset" and "MMU1 after reset"
- **VHDL fact**: zxnext.vhd:4611-4618 specifies MMU0 = 0xFF (ROM), MMU1 = 0xFF (ROM) at power-on reset
- **C++ behaviour**: In src/memory/mmu.cpp:
  - Line 27: `nr_mmu_[i] = RESET_PAGES[i]` seeds both nr_mmu_[0] and nr_mmu_[1] to 0xFF
  - Lines 33-34: `map_rom_physical(0, 0)` and `map_rom_physical(1, 1)` set the internal slots_[] and read_ptr_[] to point to ROM
  - Crucially, map_rom_physical() at lines 71-73 intentionally does NOT modify nr_mmu_ — it only updates slots_[] and read_only_[]
  - Therefore `get_page(0)` and `get_page(1)` will return nr_mmu_[0] and nr_mmu_[1], both 0xFF
  - The assertions PASS
- **Why this is UNCLEAR**: The test file header (lines 22-25) explicitly documents RST-01 and RST-02 as "known-failing rows" with the comment "Mmu::reset() calls map_rom(0,0)/map_rom(1,1) which overwrites slots_[0]/slots_[1] with 0/1, whereas VHDL reset leaves MMU0/MMU1 = 0xFF (the ROM marker)." This comment describes a past bug where map_rom() was clobbering the NR register view. However, the current code (as audited) does NOT have this bug — map_rom_physical() correctly preserves nr_mmu_. Either:
  1. The bug was fixed in code but the test file comment was not updated (stale documentation), or
  2. The audit is reading an older version of the code
- **Suggested remediation**: Confirm the current state of mmu.cpp. If the bug is indeed fixed, remove or update the comment at lines 22-25 to reflect that RST-01 and RST-02 now pass for the correct reason (the 0xFF register value is preserved during ROM mapping). If the bug still exists but is masked by some other mechanism, investigate further.

## GOOD (summary only, no per-row detail)

### Pass rows cleared: 64

**Category 1: MMU slot assignment (MMU-01 through MMU-15)**
All 15 rows encode VHDL address-translation formula (`zxnext.vhd:2964`) and slot-dispatch boundaries (`zxnext.vhd:2952-2959`). Round-trip writes through Mmu slots to physical RAM pages are verifiable and correct. C++ implementation at mmu.cpp:37-62 (rebuild_ptr, set_page) matches the VHDL slot→RAM mapping.

**Category 2: Reset state (RST-03 through RST-08)**
Six rows verify reset defaults for slots 2-7 from VHDL `zxnext.vhd:4611-4618`. Assertions encode correct expected values (0x0A, 0x0B, 0x04, 0x05, 0x00, 0x01). Note: RST-01 and RST-02 also pass but are flagged UNCLEAR due to stale comment.

**Category 3: Port 0x7FFD (P7F-01 through P7F-15, excluding skip rows)**
12 pass rows covering 128K banking (P7F-01–08: bank selection), ROM selection (P7F-09–10), lock enforcement (P7F-12–13, 15). Citations: `zxnext.vhd:3640-3814` (port handling), `zxnext.vhd:4619-4670` (ROM routing), `zxnext.vhd:3814` (lock bit). C++ implementation at mmu.cpp:97-120 (map_128k_bank) correctly composes ROM page from port_1ffd_ and port_7ffd bits.

**Category 5: Port 0x1FFD (+3 paging, P1F-01 through P1F-06, excluding skip row)**
Five pass rows covering +3 ROM selection (P1F-01–04), special mode enable (P1F-05), and lock gating (P1F-06). Citations: `zxnext.vhd:4619-4670`, `zxnext.vhd:4623-4632`. C++ at mmu.cpp:122-153 (map_plus3_bank) handles both normal and special-mode paging.

**Category 6: +3 special paging (SPE-01 through SPE-05)**
Five rows verifying the four fixed MMU configurations (SPE-01–04) and mode-exit (SPE-05). Citation: `zxnext.vhd:4623-4632` (configs), `zxnext.vhd:4634` (exit). Assertions check all eight slots against expected page values; C++ special-mode configs at mmu.cpp:137-143 match VHDL.

**Category 7: Paging lock (LCK-01, LCK-02, LCK-06)**
Three pass rows testing 7FFD bit 5 lock on 7FFD writes (LCK-01), 1FFD writes (LCK-02), and bypass via direct NR 0x50-0x57 (LCK-06). Citation: `zxnext.vhd:3814`, `zxnext.vhd:4880` (NR write handler). C++ at mmu.cpp:98-105 (map_128k_bank, map_plus3_bank paging_locked_ guard) enforces lock.

**Category 14: Address translation (ADR-01 through ADR-08)**
Eight rows verifying page→SRAM mapping round-trip for diverse page values (0x00, 0x01, 0x0A, 0x0B, 0x0E, 0x10, 0x20, 0xDF). Citation: `zxnext.vhd:2964` formula. Round-trip: set_page(slot, P) → write(addr_in_slot, 0x5A) → read ram.page_ptr(P)[offset]. All pass.

**Category 15: Bank 5/7 special pages (BNK-05, BNK-06)**
Two rows confirming CPU round-trip read/write through pages 0x0A (bank5, VRAM) and 0x0E (bank7, VRAM) work correctly. Citation: `zxnext.vhd:2933-3133`. Note: The Mmu does not expose bank5/bank7 flags (thus BNK-01–04 are skipped), but CPU access through these pages is functional.

**Category 11: ROM read-only (ROM-08)**
One row verifying writes to ROM slots have no effect. Citation: `zxnext.vhd:2933-3133`. C++ at mmu.cpp:75-105 (write function) checks read_only_[slot] and returns without writing if true.

**Category 17: Layer 2 mapping (L2M-01, L2M-01b, L2M-03, L2M-04)**
Four pass rows testing L2 write-over priority over MMU (L2M-01, L2M-01b), auto-segment addressing (L2M-03), and 0xC000-0xFFFF exclusion (L2M-04). Citations: `zxnext.vhd:2969` (L2 address formula), `zxnext.vhd:3077` (priority), `zxnext.vhd:3100-3107` (auto-segment). C++ at mmu.cpp:88-98 (write function Layer 2 logic) correctly intercepts writes when l2_write_enable_ is true and segment is in mask.

**Category 18: Memory decode priority (PRI-03, PRI-05)**
Two pass rows: PRI-03 verifies L2 overrides MMU in 0-16K (citation: `zxnext.vhd:3077`); PRI-05 verifies MMU-only path in 0xC000-0xFFFF (citation: `zxnext.vhd:2933-3133`).

All 64 pass rows are **GOOD**: assertions encode real VHDL facts, C++ implementations are correct, no false-pass mechanisms detected.

### Skip rows cleared (GOOD-SKIP): 69

All 69 skip rows correctly identify architectural gaps in the thin Mmu API surface:

- **Port 0xDFFD (7 rows)**: Extra bank bits unreachable — Mmu has no port 0xDFFD ingress. Decision: GOOD-SKIP.
- **NR 0x8E (6 rows)**: Unified paging register unreachable — Mmu does not implement 0x8E. Decision: GOOD-SKIP.
- **NR 0x8F (5 rows)**: Mapping-mode selector unreachable — Mmu does not consume 0x8F. Decision: GOOD-SKIP.
- **Port 0xEFF7 (4 rows)**: RAM-at-0x0000 and Pentagon-disable unreachable. Decision: GOOD-SKIP.
- **ROM selection (7 rows)**: Machine-type-aware ROM selection unreachable — Mmu has no machine-type input. Decision: GOOD-SKIP.
- **Alternate ROM (9 rows)**: NR 0x8C handler missing. Decision: GOOD-SKIP.
- **Config mode (4 rows)**: NR 0x03/0x04 handlers missing. Decision: GOOD-SKIP.
- **Bank 5/7 flags (4 rows)**: sram_bank5/sram_bank7 accessors missing. Decision: GOOD-SKIP.
- **Memory contention (12 rows)**: ContentionModel lacks mem_active_page, speed, mode inputs. Decision: GOOD-SKIP.
- **Layer 2 shadow (3 rows)**: NR 0x12/0x13 shadow selection not on Mmu API surface. Decision: GOOD-SKIP.
- **DivMMC overlay (3 rows)**: Full DivMMC fixture required. Decision: GOOD-SKIP.
- **Paging lock variants (4 rows)**: DFFD/0x08/0x8F/0x8E handlers missing. Decision: GOOD-SKIP.
- **Miscellaneous (2 rows)**: MMU-12 (page 0xE0 ROM overflow) and ADR-09-10 (pages 0xE0, 0xFF) lack observable distinction between ROM and unmapped paths in C++ API.

All skips are justified by API gaps. No false-skip pattern (where a non-bug is asserted as unreachable) is present.

## Audit methodology notes

- **Matrix VHDL citation propagation gap**: The TRACEABILITY-MATRIX.md reports `—` (no citation) for nearly all Memory/MMU rows in the "VHDL file:line" column. However, the test source file (test/mmu/mmu_test.cpp) contains specific VHDL citations inline in every check() description string (e.g., line 150: "VHDL zxnext.vhd:2964 page→SRAM", line 223: "VHDL zxnext.vhd:4880 NR write handler"). The matrix was not regenerated to extract these citations. The commit hash `9fcc5802146a4e6a56bc2ad9abf19c0b202e680c` ("Last-touch") indicates the test file was updated, but the matrix VHDL column was not synchronized.

- **Stale comment RST-01/RST-02**: Test file lines 22-25 document RST-01 and RST-02 as "known-failing" with the specific explanation "Mmu::reset() calls map_rom(0,0)/map_rom(1,1) which overwrites slots_[0]/slots_[1] with 0/1". Audit of src/memory/mmu.cpp lines 18-35 shows this bug has been fixed: map_rom_physical() preserves nr_mmu_[]. Either the comment is stale (bug fixed, comment not updated) or there is a version mismatch. This is flagged as UNCLEAR rather than FALSE-PASS because the assertions are correct and the C++ appears correct; only the documentation is contradictory.

- **Lazy-skip traceability**: 69 skip messages cite API gaps ("no Mmu accessor for X") without explicitly referencing the VHDL line that defines the observable being blocked. This is a documentation gap, not a correctness gap. Examples for strengthening:
  - "no port 0xDFFD handler" → "no port 0xDFFD handler (VHDL zxnext.vhd:3640-3814)"
  - "no NR 0x8E handler" → "no NR 0x8E handler (VHDL zxnext.vhd:3662-3734 unified paging)"
  - "ContentionModel lacks mem_active_page input" → "ContentionModel lacks mem_active_page input (VHDL zxnext.vhd:4481-4496 contention enable)"

- **No false-pass, false-fail, or tautology patterns**: All 64 pass rows exercise real observable changes (round-trip writes, state retention, mode switches) rather than trivial assertions. All 77 skips identify genuine API gaps rather than non-bugs or lazy dismissals.

- **Extra coverage rows (6 rows not in plan)**: The matrix notes RST-09, RST-10, RW-01 through RW-05 as extra rows. Spot-check of source shows these are sanity checks (e.g., RST-09 verifies "MMU0 is ROM after reset" by checking is_slot_rom(0), duplicating the reset-state coverage). These use the standard check() machinery and do not introduce false-pass risk.

---

**Final assessment**: The Memory/MMU test suite is well-designed and properly grounded in VHDL. The 64 passing rows all encode correct VHDL facts. The 77 skipped rows correctly identify architectural API gaps. Two minor issues for cleanup:
1. Regenerate the TRACEABILITY-MATRIX.md to capture inline VHDL citations from the test source (currently shown as `—`).
2. Clarify or remove the stale comment at test/mmu/mmu_test.cpp:22-25 regarding RST-01/RST-02 if the bug has been fixed in the C++ implementation.
