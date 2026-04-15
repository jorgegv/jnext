# Subsystem Compositor row-traceability audit

**Rows audited**: 114 total (90 pass + 24 fail + 0 skip)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 88   | 0 false-pass + 2 tautology | 0 |
| fail   | 24   | 0 false-fail | 0 |
| skip   | 0    | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 0

## FALSE-PASS
None.

## FALSE-FAIL
None. All 24 failing rows assert correct VHDL oracles and cite exact VHDL lines. Failures are due to documented emulator gaps (blend modes, stencil, L2 priority, border exception, sprite enable).

## LAZY-SKIP
None. Test code contains no SKIP(...) macro; all rows execute.

## TAUTOLOGY

### TR-11
- **Current status**: pass
- **Test assertion**: `got == fb` where both `fb = vhdl_fallback_argb(0xE3)` and `r.ula_line_[0] = Renderer::rrrgggbb_to_argb(0xE3)`
- **Test location**: `test/compositor/compositor_test.cpp:170-180`
- **Plan row scope**: "ULA pixel with palette output = NR 0x14 is transparent; fallback wins"
- **VHDL fact**: `zxnext.vhd:7100` — `ula_mix_transparent <= (ula_rgb_2(8 downto 1) = transparent_rgb_2) OR ula_clipped_2`
- **C++ behaviour**: `Renderer::rrrgggbb_to_argb()` always sets alpha=0xFF (line 86), so `is_transparent()` (line 136) always returns false. Mode 000 priority picks ULA if u_opaque, so opaque ULA wins.
- **Why it's a tautology**: The test sets both the ULA colour and fallback colour to the identical ARGB value (0xE3). The test passes whether the ULA wins (opaque) or the fallback is shown (transparent), because they are equal. The transparency mechanism is never actually tested. The test comment states "expected to fail until Task 3 fixes the compositor to honour NR 0x14" but it passes by accident.
- **Suggested remediation**: Replace with distinct fallback colour (e.g., 0x10) so opaque ULA and transparent fallback produce different outputs and can be distinguished.

### TR-12
- **Current status**: pass
- **Test assertion**: `(got_a == fb) && (got_b == fb)` where both test cases use identical input (ULA = fallback = 0xE3)
- **Test location**: `test/compositor/compositor_test.cpp:182-199`
- **Plan row scope**: "ULA palette entry whose LSB differs from NR 0x14 LSB is still transparent"
- **VHDL fact**: `zxnext.vhd:7100` — comparison uses only bits [8:1], bit 0 is ignored
- **C++ behaviour**: Same as TR-11; `is_transparent()` uses ARGB alpha (always 0xFF), so both LSB variants are treated identically (opaque). ULA always wins in mode 000.
- **Why it's a tautology**: The test acknowledges in its comment that "emulator has no 9-bit LSB, so we reuse the same input". Both LSB=0 and LSB=1 cases use the same ARGB value, so the test trivially passes without actually exercising the distinction. A proper test would use fallback != ULA so a transparent result and opaque result could be distinguished.
- **Suggested remediation**: Use distinct fallback colour so the two LSB cases can produce observably different outputs (fallback vs ULA) if the implementation changes.

## PLAN-DRIFT
None identified.

## UNCLEAR
None.

## GOOD (summary only)

- pass rows cleared (GOOD): 88
- fail rows cleared (GOOD-FAIL): 24
- skip rows cleared (GOOD-SKIP): 0

### All passing non-tautology rows (88 GOOD)

**TR group (18/19)**: TR-10, TR-13–TR-17, TR-20–TR-23, TR-30–TR-33, TR-40–TR-41 all GOOD.
**TRI group (3/3)**: All GOOD. Correctly verify index-based transparency signals (sprites.vhd:1067, zxnext:4395, 7118, 7109).
**FB group (8/8)**: All GOOD. Correctly verify fallback colour logic (zxnext:7214).
**PRI group (17/20)**: 17 rows GOOD (basic priority orderings, modes 000–101).
**PRI-BOUND group (3/3)**: All GOOD.
**L2P group (3/9)**: L2P-16, L2P-14, L2P-15 GOOD.
**BL group (5/17)**: BL-12, BL-16 GOOD (edge cases of blend mode 110). Rest fail.
**UTB group (7/7)**: All GOOD. ULA/Tilemap blend mode routing correctly implemented (zxnext:7142–7177).
**STEN group (6/8)**: STEN-10, STEN-11, STEN-14–17 GOOD.
**SOB, LINE, BLANK, PAL, RST groups (28/28)**: All GOOD.

### All failing rows (24 GOOD-FAIL)

**Border Exception (3 rows, VHDL 7256/7266/7278):**
- PRI-011-LUS-border, PRI-100-USL-border, PRI-101-ULS-border — no border-exception clause in C++.

**L2 Priority Bit Promotion (6 rows, VHDL 7123/7220/7242/7264/7276/7300/7342):**
- L2P-10, L2P-11, L2P-12, L2P-13, L2P-17, L2P-18 — no palette bit 15 implementation.

**Blend Modes 110/111 (12 rows, VHDL 7286–7356):**
- BL-10, BL-11, BL-13, BL-15, BL-20, BL-21, BL-22, BL-24, BL-26, BL-27, BL-28, BL-29 — blend modes unimplemented (renderer.cpp:259 falls back to SLU).

**Stencil Mode (2 rows, VHDL 7112–7113):**
- STEN-12, STEN-13 — no stencil AND logic.

**Global Sprite Enable (1 row, VHDL 6934/6819/7118):**
- TR-42 — NR 0x15[0]=0 should force sprite_pixel_en_2=0, but C++ does not gate.

## Audit methodology notes

- **VHDL traceability**: Every row asserts behaviour from a specific VHDL line(s). Failing rows verified against zxnext.vhd lines 6819, 6934, 7100–7123, 7214–7278, 7286–7356. Passing rows spot-checked.
- **Emulator architecture**: Renderer uses ARGB alpha (alpha=0 for transparent; renderer.h:136) as the transparency signal, not palette-compare RGB. Tests encode VHDL-correct oracles and will fail until emulator implements palette-compare transparency and other deferred features.
- **Row count**: 114 total (90 pass + 24 fail). Plan lists 113 rows; test code expands table entries (L2P rows 10–15, PRI rows) into loops.
- **Tautology detection**: TR-11 and TR-12 identified by code inspection: both set ULA and fallback to identical ARGB values (0xE3), so output cannot distinguish transparent vs opaque cases.
- **No lazy-skips**: All rows run via check() macro.
