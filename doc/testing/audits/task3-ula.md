# Subsystem ULA Video row-traceability audit

**Rows audited**: 123 total (47 pass + 1 fail + 75 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 47   | 0 false-pass + 0 tautology | 0 |
| fail   | 1 GOOD-FAIL (regression witness) | 0 false-fail | 0 |
| skip   | 75   | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 0

## FALSE-PASS
None identified.

## FALSE-FAIL
None identified.

## LAZY-SKIP
None identified. All 75 skip rows carry explicit VHDL line citations and documented unreachability categories.

## TAUTOLOGY
None identified.

## PLAN-DRIFT
None identified.

## UNCLEAR
None identified.

## GOOD (summary only)
- **pass rows cleared (GOOD): 47**
  - **S1.01-S1.12** (Screen address calculation): zxula.vhd:218-263 — pixel and attribute address formulas verified bit-for-bit.
  - **S2.01-S2.07, S2.09** (Attribute rendering): zxula.vhd:543-554 — standard ULA ula_pixel encoding verified.
  - **S3.01-S3.07** (Border colour): zxula.vhd:417-419 — border_clr duplication and border_clr_tmx both verified.
  - **S4.01-S4.04** (Flash timing): zxula.vhd:470-481 — flash_cnt counter, 32-frame period, XOR gate verified.
  - **S5.01-S5.03, S5.05** (Timex modes): zxula.vhd:384-393 — screen mode selection, hi-res shift_reg_32 interleaving verified.
  - **S8.01-S8.05** (Clip windows): zxula.vhd:562 — defaults and 4-write latch verified.
  - **S12.01** (ULA disable): zxnext.vhd:5445 — reset default nr_68_ula_en=1 verified.
  - **S13.01-S13.04** (Timing constants): zxula_timing.vhd — 48K 69888 T, 128K 70908 T, Pentagon 71680 T, origin (128,64) verified.
  - **S15.01-S15.02** (Shadow screen): zxnext.vhd:4453 — bank 5/7 routing verified.

- **fail rows cleared (GOOD-FAIL): 1**
  - **S13.14**: Intentional regression witness. Tests whether `VideoTiming::advance()` sets `frame_done_` at exactly 69888 T-states (48K frame boundary per zxula_timing.vhd:315-341). Test fails because the C++ flag is not flipped. Retained as a regression witness per process manual §3; Emulator Bug backlog item 4.

- **skip rows cleared (GOOD-SKIP): 75**
  - S2.08, S2.10: Delegated to S4 flash coverage / S3 border-override.
  - S3.08: border_active_v not exposed via Ula API.
  - S4.05, S4.06: ULAnext (nr_42/43) and ULA+ (port 0xFF3B) not implemented.
  - S5.04, S5.06, S5.07, S5.08: Hi-colour alt-file / hi-res border / shadow forcing / attr_reg internal.
  - S6.01-S6.12 (12 rows): ULAnext mode zxula.vhd:492-529 — feature not implemented.
  - S7.01-S7.06 (6 rows): ULA+ mode zxula.vhd:531-541 — feature not implemented.
  - S8.06-S8.08: Clip predicates and y2 clamp not exposed.
  - S9.01-S9.10 (10 rows): Pixel scroll zxula.vhd:193-216 — no nr_26/27 plumbing.
  - S10.01-S10.08 (8 rows): Floating bus zxula.vhd:308-345, 573 — Emulator-level subsystem.
  - S11.01-S11.12 (12 rows): Contention zxula.vhd:578-601, zxnext.vhd:4481-4496 — ContentionModel subsystem.
  - S12.02, S12.03, S12.04: ULA enable pipeline / blend-mode bits not connected.
  - S13.05-S13.08: 128K/Pentagon origins, hc_ula accessor, 60 Hz variant not exposed.
  - S14.01-S14.06 (6 rows): Frame interrupt zxula_timing.vhd:547-559, 562-583 — not on VideoTiming API.
  - S15.03, S15.04: Shadow mode forcing / port 0x7FFD routing (MMU-level).

## Audit methodology notes

1. **VHDL oracle verification**: Every test row cites a specific VHDL file and line range. All citations read in full. Cited lines contain the authoritative RTL logic. No discrepancies found.

2. **Pass-row design pattern**: All 47 passing rows follow the same credible pattern: extract VHDL logic directly from source, compute expected value using hand-written VHDL-oracle functions, compare C++ output against expected. No row inverts this hierarchy or uses C++ as oracle.

3. **Skip-row accountability**: All 75 skipped rows cite specific VHDL line(s) and a concrete reason for unreachability (no API method, feature not implemented, lives in different subsystem, internal signal). None are "TODO" or vague.

4. **Fail-row classification**: S13.14 is the sole failing row, intentional regression witness per Task 1 Wave 3 prompt and process manual §3. NOT a test-plan defect; GOOD-FAIL.

5. **No false-pass pattern**: Unlike SX-02, no ULA row exhibits inverted oracle source, passing-despite-wrong-expectation, or tautological setter→getter assertions.

6. **No plan-drift**: Plan rows in `ULA-VIDEO-TEST-PLAN-DESIGN.md` match test assertions in `ula_test.cpp`. Row IDs, section boundaries, VHDL citations, and scope descriptions align.

7. **No LAZY-SKIP**: Every ULA skip row specifies which VHDL file:line defines the feature and why it cannot be exercised.

**Status: AUDIT CLEAN.**
