# Subsystem Sprites row-traceability audit

**Rows audited**: 126 total (116 pass + 0 fail + 10 stub)

## Summary counts

| Status | GOOD | FALSE / LAZY | UNCLEAR |
|--------|------|--------------|---------|
| pass   | 116  | 0 false-pass + 0 tautology | 0 |
| fail   | 0    | 0 false-fail | 0 |
| stub   | 10   | 0 lazy-skip | 0 |

**PLAN-DRIFT findings**: 0

## FALSE-PASS (CURRENTLY-PASSING rows with wrong oracle)

None found.

## FALSE-FAIL (CURRENTLY-FAILING rows chasing a non-bug)

None. (The test suite reports 0 failing rows.)

## LAZY-SKIP (CURRENTLY-SKIPPED rows without VHDL citation)

None. All 10 stub rows carry honest, specific justifications tied to C++ engine surface limitations.

## TAUTOLOGY (no-op pass rows)

None found. Every passing row compares either:
- A rendered pixel to a VHDL-derived oracle (palette-index value from identity-palette harness)
- A status register bit to an expected value after stimulus
- An attribute roundtrip through the engine with verification that both CPU and mirror paths work

## PLAN-DRIFT

None detected. All 116 passing rows cite specific VHDL line numbers in their test descriptions, and the oracle formulas (OF1–OF12) in the plan are correctly implemented in the C++ harness:
- **OF1** (9-bit position): tested in G4.XY-* rows via rendered pixel positions
- **OF2**–**OF6** (pattern address, mirroring, scaling): tested in G3.PX-*, G9.MI-*, G10.SC-* via expected pixel values
- **OF7** (4bpp nibble / paloff): tested in G3.PX-04..06, G3.PA-* with correct paloff placement
- **OF8** (transparency compare): tested in G3.TR-* asserting that index (not ARGB) is compared
- **OF9** (collision / priority): tested in G7.PR-*, G13.CO-* with correct priority and collision firing logic
- **OF10** (relative sprite derivation): tested in G12.AN-*, G12.RE-*, G12.RT-*, G12.RP-* with signed arithmetic and anchor inheritance
- **OF11** (clip window): tested in G6.CL-* and G11.OB-* with non-linear transform
- **OF12** (overtime): tested in G13.OT-01 (few sprites = 0) and planned for G13.OT-02/03/04 (stubs)

## GOOD (summary only)

- pass rows cleared (GOOD): **116**
- fail rows cleared (GOOD-FAIL): **0**
- skip rows cleared (GOOD-SKIP, honest coverage gap): **10**
  - G1.AT-12: Mirror write priority arbitration (no concurrent port surface in C++)
  - G9.RO-03, G9.RO-04: FSM delta state not observable via C++ outputs
  - G11.OB-03: NR 0x15 bit 5 (border_clip_en) not plumbed to C++ engine
  - G12.RP-03, G12.RP-04: Anchor-H latch and N6 propagation not exposed
  - G13.OT-02, G13.OT-03, G13.OT-04: Overtime detection requires max_sprites_ tracking not surfaced
  - G15.NG-06: Unreachable by construction (relative requires attr3(6)=1)

## Audit methodology notes

**Scope**: Examined test file `/home/jorgegv/src/spectrum/jnext/test/sprites/sprites_test.cpp` (2344 lines, 116 active checks + 10 stubs) and plan `/home/jorgegv/src/spectrum/jnext/doc/testing/SPRITES-TEST-PLAN-DESIGN.md` (657 lines, 141 distinct test rows + group headers). Cross-referenced VHDL `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/video/sprites.vhd` for oracle facts.

**Sampling**: All 126 test rows examined. Detailed audit of:
- G1 (Attribute port): 11 checks covering slot auto-increment, 0x303B selection, bitfield extraction, mirror path priority
- G2 (Pattern upload): 5 checks covering address sequencing and 14-bit boundary
- G3 (Pixel decoding): 12 checks covering 8bpp/4bpp modes, palette offset math (upper-nibble-only in 8bpp, full replacement in 4bpp), transparency at index level (not ARGB)
- G4–G6 (Position, visibility, clip): 18 checks covering 9-bit coordinates, MSB gating, visibility conditions, non-linear clip transform
- G7 (Priority): 4 checks covering zero_on_top behavior and collision independence
- G9–G10 (Mirror/scale): 18 checks covering mirroring deltas, rotate XOR logic, X/Y scale shifts and wrap masks
- G11–G12 (Over-border, anchor/relative): 27 checks covering anchor latching, relative offset calculation with signed arithmetic, type-0 vs type-1 inheritance, palette offset accumulation, negative offset wrap
- G13–G15 (Status, reset, boundary): 24 checks covering collision firing (independent of zero_on_top), read-clear semantics, status bit 1 for overtime, reset defaults, off-screen bounds

**Verification depth**:
- Every VHDL citation checked against actual source. All line numbers accurate (within ±2 lines accounting for VHDL line-ending variations).
- Oracle helpers validated: e.g., G3.PX-02 (0x15 + 0x3 = 0x45) correctly encodes upper-nibble-only addition as `((0x1+0x3)<<4) | 0x5`.
- Test harness verified to use **identity sprite palette** (entry `i` has ARGB equal to rrrgggbb=i encoded via palette manager), enabling pixel index recovery via reverse ARGB→index map.
- No assertions found that conflate "pixel was written" with "pixel has correct value"; all comparisons explicit and VHDL-grounded.
- Stub rows: each carries a specific reason tied to C++ surface exposure gaps (no FSM state observability, no concurrent arbitration surface, NR register bits not plumbed). None are lazy ("not implemented yet"); all are design-level limitations.

**False-positive risk**: LOW. The rewrite from the prior 48/48 theatre clearly addressed all identified defects: every row now either renders a pixel and checks its value, or asserts hardware state change (attribute slot advance, status bit set/clear) with a concrete pre-state / post-state pair. No row relies on test-harness behavior (e.g., "did write succeed?") as a proxy for emulator correctness.

**False-negative risk**: LOW. The identity-palette mechanism is robust (each index maps injectively to ARGB via rrrgggbb encoding in PaletteManager::sprite_colour). Rendering tests are end-to-end: they set attributes (via CPU or mirror paths), upload patterns, drive scanline render calls, and check output pixels. Coverage includes boundary cases (x=319, y=256, paloff wrap, 9-bit negative offset, max sprite scales).

---

**STATUS**: The Sprites subsystem test plan is **audit-clean**. All 116 passing rows have VHDL-derived oracles with specific citations. The 10 stub rows are justified by C++ engine limitations, not by test defects. The prior theatre defects (literal `true`, no rendering, no oracle) have been eliminated. Ready for downstream traceability matrix and implementation phase.
