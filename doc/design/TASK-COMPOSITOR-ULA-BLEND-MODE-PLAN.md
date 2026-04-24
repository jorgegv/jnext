# Task — Compositor `ula_blend_mode` (NR 0x68 bits 6:5) Plan

Plan authored 2026-04-24. Follows the staged plan template established by
`TASK-MMU-SHADOW-SCREEN-PLAN.md` (small scope, Phase 0→3),
`TASK-COMPOSITOR-NR68-BLEND-PLAN.md` (adjacent compositor work, BLOCKED
status), and the larger `TASK-NMI-SOURCE-PIPELINE-PLAN.md` (Phases 0→4,
VHDL-as-oracle per `doc/testing/UNIT-TEST-PLAN-EXECUTION.md`).

This plan closes one of the 3 rows parked by the BLOCKED
`TASK-COMPOSITOR-NR68-BLEND-PLAN.md` re-audit (2026-04-24) —
specifically UDIS-03. UDIS-01 (bit 7 end-to-end observation) and
UDIS-02 (mid-scanline Copper toggle) remain blocked on orthogonal
infrastructure (compositor integration-test fixture, Copper+NMI
pipeline) and are out of scope here.

## Summary

Wire NR 0x68 bits 6:5 (`ula_blend_mode`) through the NR 0x68 write
handler to a new `Renderer::set_blend_mode(uint8_t)` accessor, and
extend the existing priority-mode-6 / priority-mode-7 branches in
`Renderer::composite_scanline` to produce the correct `mix_rgb` /
`mix_top` / `mix_bot` signals per VHDL `zxnext.vhd:7141-7178` for ALL
four ula_blend_mode variants. Today those two branches hard-code the
`ula_blend_mode = "00"` mapping (see the comment at
`renderer.cpp:336` and the expressions at `renderer.cpp:337-339`
(mode 6) and `renderer.cpp:365-367` (mode 7)), so 3 of the 4 variants
are silently wrong.

**User-visible driver**: `CSpect3_1_0_0/Beast/beast.nex` triggers this
bug. The game selects a non-"00" `ula_blend_mode` for its Layer-2 sky
gradient; on CSpect the ULA layer is correctly masked out of the
blend. On jnext, the ULA-attribute cells leak through as 8×8 colored
patches in the upper sky / tree-edge regions (compare `beast-good.png`
CSpect reference vs jnext headless capture of the same NEX).

Expected row delta:

| Suite                    | Rows | Current      | Post-plan |
|--------------------------|-----:|--------------|-----------|
| `compositor_test` UDIS-03 |    1 | skip         | **pass**  |
| `compositor_test` Group BL (new mode 01/10/11 rows) | ~9 | — | **pass** |

Aggregate expected: approximately **+10 rows pass, −1 skip** in
`compositor_test`. No cross-suite changes. No regression impact
expected (the existing 20 Group BL rows all use mode "00", whose
behaviour must remain pixel-identical).

## Starting state

- `compositor_test`: 117 / 114 / 0 / 3 (UDIS-01, UDIS-02, UDIS-03 skip).
- Aggregate unit (post-NMI plan close, 2026-04-24 EOD): 31 suites,
  ≈ 3319 / 3194 / 0 / 125.
- Regression: 34 / 0 / 0. FUSE Z80: 1356 / 1356.
- Branch: `main`, working tree clean. Main tip: `5342464` (fetch
  confirmed 2026-04-24).

### UDIS-03 current skip reason (to refresh)

`test/compositor/compositor_test.cpp:1453-1458`:

> "F-UDIS-BLEND: NR 0x68 bits 6:5 (ula_blend_mode) feed mix_rgb at
> VHDL 7141-7178 but those signals only flow to NR 0x15 priority
> modes 110/111 (VHDL 7286-7356), which the emulator does not
> implement (see Group BL comment; renderer.cpp:~259 falls back to
> SLU for 6/7). UDIS-03 folds into the BL backlog."

**This reason is stale** (caught in Phase 0 triage). Priority modes
6/7 ARE implemented — see `renderer.cpp:335-403`. What is missing is
the ula_blend_mode branching INSIDE those modes. Phase 0 refreshes
the reason string to reflect the real gap.

### NR 0x68 handler today

`src/core/emulator.cpp:816-825`:

```cpp
nextreg_.set_write_handler(0x68, [this](uint8_t v) {
    renderer_.ula().set_ula_enabled((v & 0x80) == 0);
    // VHDL 7112: ula_blend_mode bits 6:5 (stencil when bit 0 set)
    renderer_.set_stencil_mode((v & 0x01) != 0);
    renderer_.ula().set_ula_fine_scroll_x((v & 0x04) != 0);
    renderer_.ula().set_ulap_en((v & 0x08) != 0);
});
```

Bits 7 / 3 / 2 / 0 are handled. Bits 6:5 are DROPPED despite the
in-line comment acknowledging them. Bit 4 (`nr_68_cancel_extended_keys`,
keyboard) is also dropped but is owned by the Input plan, not this one.

### Renderer priority-mode 6 / 7 today

`src/video/renderer.cpp:335-403` hard-codes the ula_blend_mode = "00"
interpretation:

```cpp
// ULA blend mode "00" (default): mix_rgb=ULA, mix_top=TM
const bool mix_rgb_transp = ula_transp;
const bool mix_top_transp = tm_transp || ula_over_flags_[x];
const bool mix_bot_transp = tm_transp || !ula_over_flags_[x];
```

The mapping is correct for "00" (VHDL 7142-7148) but the 3 other
variants (`"01"` / `"10"` / `"11"`) each produce different mix_* values
and are currently ignored.

## VHDL authority

| File | Lines | Content |
|---|---|---|
| `cores/zxnext/src/zxnext.vhd` | 5444-5450 | NR 0x68 write handler: `nr_68_blend_mode <= nr_wr_dat(6 downto 5)` |
| `cores/zxnext/src/zxnext.vhd` | 1199 | `signal nr_68_blend_mode : std_logic_vector(1 downto 0)` |
| `cores/zxnext/src/zxnext.vhd` | 5027 | `nr_68_blend_mode <= "00"` (reset default) |
| `cores/zxnext/src/zxnext.vhd` | 6811, 6900-6901, 7065 | Pipeline staging (`_0 → _1a → _1 → _2`) — emulator treats as same-frame-constant |
| `cores/zxnext/src/zxnext.vhd` | 7139-7178 | `ula_blend_mode_2` combinatorial mux → `mix_rgb / mix_top / mix_bot` |
| `cores/zxnext/src/zxnext.vhd` | 7286-7310 | Priority mode "110" (additive) consumes `mix_rgb/top/bot` |
| `cores/zxnext/src/zxnext.vhd` | 7312-7352 | Priority mode "111" (subtractive) consumes `mix_rgb/top/bot` |
| `cores/zxnext/src/zxnext.vhd` | 6093 | NR 0x68 readback: `port_253b_dat <= ... & nr_68_blend_mode & ...` |

## Scope

### In scope (MUST)

1. **`Renderer::set_blend_mode(uint8_t mode)` accessor** (`renderer.h`),
   storing the 2-bit value into a new `uint8_t blend_mode_` member.
   Reset default: `0b00` (VHDL `zxnext.vhd:5027`).
2. **NR 0x68 handler extension** (`emulator.cpp:816-825`): decode bits
   6:5 and forward via `renderer_.set_blend_mode((v >> 5) & 0x03)`.
3. **Variant branching in `composite_scanline`** priority-mode 6 and
   priority-mode 7 (`renderer.cpp:335-403`): replace the hard-coded
   `"00"` computation of `mix_rgb_transp` / `mix_rgb` / `mix_top_*` /
   `mix_bot_*` with a 4-way branch driven by `blend_mode_`, per VHDL
   7141-7178.
4. **Test row authoring**: extend Group BL with mode 01/10/11 variants
   of the existing mode-00 rows (≈3 rows per variant covering mix_rgb,
   mix_top, mix_bot observation), plus a sanity-regression pass on the
   existing 20 mode-00 rows (no change expected).
5. **UDIS-03 flip**: remove the `skip("UDIS-03", ...)` call and replace
   with a live `check()` demonstrating that toggling NR 0x68 bits 6:5
   observably changes compositor output in priority mode 6 or 7.

### Out of scope (explicit)

- **Priority modes 0-5** (SLU/LSU/SUL/LUS/USL/ULS): these never consume
  `mix_rgb/top/bot`, so `blend_mode_` has no effect on them. The 114
  existing non-BL compositor rows must stay pixel-identical.
- **NR 0x68 bit 0 (stencil mode)**: already wired at `renderer.h:83`
  via `set_stencil_mode`, separate code path (VHDL 7125-7137). Not
  touched here.
- **NR 0x68 bit 4** (`nr_68_cancel_extended_keys`): keyboard-side
  concern, owned by the Input subsystem.
- **UDIS-01 / UDIS-02**: remain skipped for the reasons documented in
  `TASK-COMPOSITOR-NR68-BLEND-PLAN.md` Status block (compositor
  integration-test fixture + Copper+NMI pipeline). Not this plan's
  remit.
- **Mid-scanline blend-mode changes** (Copper MOVE to NR 0x68 during
  DMA or an ISR): `blend_mode_` is read once per composite_scanline
  call; per-pixel Copper writes land with UDIS-02 (which depends on
  this plan only for the final observation, not for the mechanism).

### Explicitly deferred

None.  This plan fully closes UDIS-03.

## Triage — the 4 `ula_blend_mode` variants

Per VHDL `zxnext.vhd:7141-7178`, the mux has 3 explicit branches and
a `when others` fall-through. `"01"` falls into the `when others`
clause (not into `"00"` — this is the **non-obvious finding** surfaced
by Phase 0 reading).

| bits 6:5 | `mix_rgb` | `mix_rgb_transp` | `mix_top_rgb` / `mix_top_transp` | `mix_bot_rgb` / `mix_bot_transp` | VHDL |
|---|---|---|---|---|---|
| `00` | `ula_mix_rgb` (ULA, zeroed when transparent) | `ula_mix_transparent` | `TM` / `tm_transp OR tm_below` | `TM` / `tm_transp OR NOT tm_below` | 7142-7148 |
| `01` | `0` | `1` (always transparent) | `tm_below?ULA:TM` / `tm_below?ula_transp:tm_transp` | `tm_below?TM:ULA` / `tm_below?tm_transp:ula_transp` | 7163-7176 (`when others`) |
| `10` | `ula_final_rgb` (ULA after stencil/ulatm merge) | `ula_final_transparent` | `TM` / `1` (forced transp) | `TM` / `1` (forced transp) | 7149-7155 |
| `11` | `tm_rgb` (TM, not ULA) | `tm_transparent` | `ULA` / `ula_transp OR NOT tm_below` | `ULA` / `ula_transp OR tm_below` | 7156-7162 |

Observations:

- Only variant `"00"` is implemented today. Variants `"01"`, `"10"`,
  `"11"` fall through to the same code and produce the wrong
  `mix_rgb / mix_top / mix_bot`.
- Variant `"10"` is the most user-visible: it uses `ula_final_rgb`
  (which honours stencil mode + the ulatm merge) as the blend source
  and forces mix_top/mix_bot transparent — the blend is "pure ULA
  plus Layer 2", no TM overlay. Likely what demos exercising NR 0x68
  = 0x40 actually rely on.
- Variant `"11"` swaps ULA and TM in the blend: TM is the blend source,
  ULA becomes the overlay.
- Variant `"01"` sets `mix_rgb_transparent = 1` (VHDL 7165), which
  short-circuits the L2+mix add/sub stage so the mix-path contributes
  zero to the blended result — no ULA contribution, even on a pixel
  where ULA would otherwise be opaque. Downstream cascade in priority
  modes 6/7 then selects from the `mix_top` / `mix_bot` overlays
  (typically TM with the ULA/below swap applied) plus the L2 fallback
  via `layer2_priority` / the final `elsif layer2_transparent = '0'`
  arm. Net effect: TM-over-L2 (or L2-over-TM if `tm_below`) with ULA
  masked out — exactly what `beast.nex` needs so its Layer-2 sky
  gradient renders cleanly without 48K ULA-attribute leakage. Note
  `when others` applies to `"01"` AND any out-of-2-bit value, a
  VHDL-level catch-all; for us, only `"01"` can reach this branch.

Test-row count:

- Existing Group BL rows (all mode 00): **20** (BL-10..16, BL-20..29,
  L2P-17/18).
- New rows (Phase 2): ~3 per new variant × 3 variants = **9 new rows**
  (BL-30..32 for mode 01, BL-40..42 for mode 10, BL-50..52 for mode 11),
  each covering `mix_rgb` selection + a mix_top / mix_bot asymmetry.
- UDIS-03 flip: **1 row**.

Total new live rows: **≈10**.

## Phases

### Phase 0 — plan-doc refresh + skip-reason refresh + VHDL comprehension sign-off (single agent + critic)

**Test-code / doc-only changes.**

1. Refresh `UDIS-03` skip reason in `test/compositor/compositor_test.cpp`
   to cite the real gap (NR 0x68 handler drops bits 6:5 AND priority
   6/7 branches in `renderer.cpp:335-403` hard-code the mode-00
   mapping). Point at `TASK-COMPOSITOR-ULA-BLEND-MODE-PLAN.md`.
2. Update the Group BL top-of-group comment
   (`compositor_test.cpp:876-883`) to note that modes 01/10/11 land in
   this plan, not the BL-backlog.
3. Write this plan doc (the one you are reading) covering Phases 0→3.
4. Produce the row-list appendix for Phase 2 (≈10 new rows) with a
   VHDL-cite per row, committed as an addendum to this doc or to
   `doc/testing/COMPOSITOR-TEST-PLAN-DESIGN.md`.

**Phase 0 skip-count delta**: 0 (reason refresh + doc only).

**Critic**: 1 independent agent verifies the 4-variant table in this
doc against VHDL 7141-7178 line-by-line, and confirms the existing
mode-00 code path at `renderer.cpp:335-403` is a faithful encoding of
VHDL 7142-7148.

### Phase 1 — implementation (single agent + critic)

**Branch: `task-compositor-udis-03-phase-1-impl`.**

1. Add `uint8_t blend_mode_ = 0;` member to `Renderer`
   (`src/video/renderer.h`). Reset to `0`. Add `save_state` /
   `load_state` persistence.
2. Add `set_blend_mode(uint8_t v) { blend_mode_ = v & 0x03; }` accessor
   (header inline, same style as `set_stencil_mode`).
3. Extend NR 0x68 handler (`src/core/emulator.cpp:816-825`) with
   `renderer_.set_blend_mode((v >> 5) & 0x03);`. Update the comment
   block immediately above to note bits 6:5 are now wired.
4. Refactor priority-mode 6 and 7 branches
   (`src/video/renderer.cpp:335-403`) to compute `mix_rgb / mix_top /
   mix_bot` via a 4-way switch on `blend_mode_`, per the table in §Triage.
   Keep the shared bits (per-channel add, subtractive formula,
   output-chain cascade) outside the switch. The existing mode-00
   code becomes one arm of the switch unchanged.
5. **Verification seam**: a hand-crafted BL extension row per variant
   must pass at end of Phase 1 (embedded inside the Phase-1 commit,
   NOT yet rolled into the Group BL block — that is Phase 2's job).
   This keeps Phase 1 self-verifying before Phase 2 grows the suite.

**Phase 1 skip-count delta**: 0 (UDIS-03 still skip; scaffolding only).

**Critic**: 1 independent agent verifies:
- The switch implements the §Triage table line-by-line against VHDL.
- Mode-00 pixel output is unchanged vs pre-Phase-1 (run the existing
  20 mode-00 Group BL rows pre- and post-merge and diff the output).
- `set_blend_mode` is on Renderer (not Ula), matching the VHDL signal
  home (`nr_68_blend_mode` is a top-level compositor signal, not a
  ULA-internal signal).
- Regression 34 / 34 passes unchanged.

### Phase 2 — test-row authoring (single agent + critic)

**Branch: `task-compositor-udis-03-phase-2-test`.**

1. Extend Group BL with ≈9 new rows (IDs per Phase-0 appendix, e.g.
   BL-30..32 / BL-40..42 / BL-50..52). Each row:
   - `r.set_blend_mode(0b01 | 0b10 | 0b11)`.
   - `r.set_layer_priority(6)` or `(7)`.
   - Stimulus chosen so the VHDL oracle result for that variant
     DIFFERS from the mode-00 oracle (otherwise the row tests
     nothing new).
   - Expected value computed from §Triage table + existing `bl_add` /
     `bl_sub` helpers.
2. Flip `UDIS-03` from `skip()` to a live `check()` row. Minimal
   stimulus: set NR 0x68 to 0x00 / 0x20 / 0x40 / 0x60 via the NR
   write handler, composite, observe 4 distinct outputs. (The
   assertion is "the 4 outputs are pairwise distinct", not any
   specific pixel — this keeps UDIS-03 a wiring-test, leaving the
   pixel-oracle assertions to the new BL-3x/4x/5x rows.)
3. Regenerate dashboards.

**Phase 2 skip-count delta**: UDIS-03 skip → pass. +9 new pass rows.
`compositor_test`: 117 / 114 / 0 / 3 → 127 / 124 / 0 / 2 (UDIS-01 and
UDIS-02 carry forward).

**Critic**: 1 independent agent verifies each new row's expected value
against the §Triage VHDL cite and confirms mode-00 regression is
zero.

### Phase 3 — independent audit (optional, single agent)

Triggered only if the author of Phases 0-2 catches plan-drift during
Phase 2 (e.g. the Triage table needs correction after VHDL
re-reading), or if the Critic's pass raises structural concerns.
Otherwise skip — this plan is small enough that Phases 0-2 critics
are sufficient.

If run: 1 audit agent produces
`doc/testing/audits/task-compositor-udis-03-phase3.md` with plan-drift
catalogue, critic verdicts, and final aggregate delta.

## Dependencies

- **None on other in-flight plans.** Compositor priority modes 6/7 are
  already implemented (renderer.cpp:335-403); the NMI, MMU-shadow,
  and Input plans are all closed or orthogonal.
- **UDIS-01 / UDIS-02 independence**: both remain skip after this
  plan. UDIS-01 needs a new `compositor_integration_test.cpp` fixture.
  UDIS-02 additionally needs Copper MOVE + the NMI pipeline (now
  landed; re-check after NMI plan Phase-4 audit closes).
- **Group BL pre-existing rows** (BL-10..16, BL-20..29, L2P-17/18):
  all 20 use `ula_blend_mode = "00"` implicitly (Renderer currently
  has no blend_mode state; it is effectively "00"). The Phase-1 fix
  must leave their pixel output bit-identical — tested by diffing
  `compositor_test` output pre/post.

## Risks + open questions

### Risk 1 — 4-variant branching in a tight pixel loop hurts performance

Each composite_scanline call iterates over 320-640 pixels × 192 lines
× 50 frames = 3-6M branches per second on the blend path. A 4-way
switch inside the loop is cheap for a predictable branch, but worth
watching.
**Mitigation**: `blend_mode_` is constant across a frame (NR 0x68 is
set at most a handful of times per frame, and only Copper writes could
change it per-scanline — UDIS-02 territory, out of scope). Branch
prediction will be perfect. If profiling ever shows an issue, lift
the `switch` out of the per-pixel loop and dispatch to one of 4
specialised inner loops. Defer that optimisation until measured.

### Risk 2 — VHDL-fidelity of `when others` (variant `"01"`) mapping

VHDL `when others` is a catch-all. For us it fires exactly for
`"01"` (since `"00" / "10" / "11"` are the other explicit cases).
The mix_top / mix_bot swap based on `tm_pixel_below_2` (VHDL
7163-7176) is non-obvious — **trace carefully in Phase 0**.
**Mitigation**: Phase 0 critic specifically re-verifies the §Triage
`"01"` row against VHDL 7163-7176 before Phase 1 touches code.

### Risk 3 — Regression on existing BL mode-00 rows

The refactor must produce pixel-identical output for `blend_mode_ =
0`. Any divergence = bug.
**Mitigation**: Phase 1 critic runs the existing 20 Group BL rows
pre-merge (capture output) and post-merge (diff). Any divergence
blocks the merge.

### Risk 4 — NR 0x68 readback parity

VHDL `zxnext.vhd:6093` packs `nr_68_blend_mode` into the NR 0x68
readback byte at bits 6:5. If the emulator has a NR 0x68 readback
handler, it must reflect `blend_mode_`.
**Mitigation**: Phase 1 audits `emulator.cpp` for NR 0x68 read
handlers. If absent (readback falls back to cached raw value from
`NextReg::regs_[]`), the write-through path at
`nextreg.cpp` already stores the raw byte — readback will naturally
return bits 6:5 intact. No action needed unless a masking handler is
found.

### Open Q1 — Does the current mode-00 implementation exactly match VHDL mode-00?

The code comment at `renderer.cpp:336` claims "ULA blend mode '00'
(default)". Phase 0 critic should verify that the current
`mix_rgb_transp` / `mix_top_transp` / `mix_bot_transp` expressions at
`renderer.cpp:337-339` (priority mode 6) and `renderer.cpp:365-367`
(priority mode 7) exactly equal VHDL 7142-7148 (not "close enough").
If there is an existing drift, it must be corrected in Phase 1 as part
of the refactor; do NOT preserve a pre-existing bug as "mode 00".

**Resolution (Phase 0, 2026-04-24):** No functional drift. Current
live-line numbers are `renderer.cpp:342-344` (mode 6) and
`renderer.cpp:370-372` (mode 7) — the plan-doc earlier cite of
`337-339` / `365-367` is slightly off but both point at the same
expressions. VHDL 7142-7148 vs emulator line-by-line:

| VHDL 7142-7148 | Emulator 342-344 / 370-372 | Match? |
|---|---|---|
| `mix_rgb <= ula_mix_rgb` | `mx_{r,g,b} = mix_rgb_transp ? 0 : argb_*(ula_px)` | YES — `ula_mix_rgb` is `ula_rgb_2` zeroed on transparency (VHDL 7100-7101); emulator does the same zeroing via the `?0:` ternary. |
| `mix_rgb_transparent <= ula_mix_transparent` | `mix_rgb_transp = ula_transp` | FUNCTIONAL-YES, SEMANTIC-DRIFT — `ula_mix_transparent` (VHDL 7100) ignores `ula_en_2`, while `ula_transp` (renderer.cpp:242) folds it in. Masked because `ula_line_` is pre-zeroed when the per-line `ula_enabled_per_line_[row]` flag is false (renderer.cpp:88-90), so the two are observationally identical. Phase 1 need NOT fix this unless the switch introduces a variant that needs the distinction — variant "10" uses `ula_final_transparent` which DOES include `ula_en_2` (via `ula_transparent`), so the current conflation is benign. |
| `mix_top_transparent <= tm_transparent OR tm_pixel_below_2` | `mix_top_transp = tm_transp \|\| ula_over_flags_[x]` | YES — `ula_over_flags_[x]` is literally `pixel_below` stored by tilemap.cpp:400 (misleading name — it does NOT mean "ULA over", it means "tilemap is below ULA"). |
| `mix_top_rgb <= tm_rgb` | uses `tm_px` downstream in output chain | YES (implicit — cascade at renderer.cpp:362/403 uses `tm_px`). |
| `mix_bot_transparent <= tm_transparent OR NOT tm_pixel_below_2` | `mix_bot_transp = tm_transp \|\| !ula_over_flags_[x]` | YES. |
| `mix_bot_rgb <= tm_rgb` | uses `tm_px` downstream | YES. |

**Conclusion:** mode-00 is a faithful encoding of VHDL 7142-7148. No
Phase-1 fix required for the existing expressions. The Phase-1
switch refactor should preserve these expressions verbatim as the
`case 0b00:` arm (see also §Risk 3 — mode-00 pixel-identity regression).
One hygiene note: the variable name `ula_over_flags_` is backwards
(it is truthy when TM is BELOW ULA, not "ULA over TM"). Consider
renaming to `tm_below_flags_` in Phase 1 as an incidental cleanup;
not strictly required.

### Open Q2 — `ula_mix_rgb` vs `ula_final_rgb` distinction

VHDL uses `ula_mix_rgb` in variant "00" (VHDL 7101: the bare ULA
colour zeroed on transparency) but `ula_final_rgb` in variant "10"
(VHDL 7134: post-stencil/ulatm merge). The emulator today uses the
ULA line buffer directly, with no explicit post-merge "final"
variant. Phase 0 must confirm whether the jnext pipeline's ULA line
buffer corresponds to `ula_rgb`, `ula_mix_rgb`, or `ula_final_rgb` —
and whether variant "10" needs a separate buffer.

**Most likely answer**: the jnext ULA line buffer already encodes the
`ula_final_rgb` semantics for the non-stencil path (since stencil
mode has its own branch at `renderer.cpp` mode-0 family, not mode
6/7). Variant "10" can therefore use the existing ULA line. Phase 0
must document this — if wrong, Phase 1 grows a second ULA line buffer.

**Resolution (Phase 0, 2026-04-24):** The "most likely answer" is
*wrong*. `ula_line_[x]` is the `ula_rgb` / `ula_mix_rgb` equivalent
(raw ULA display pixel, post-clip, post-NR-0x68-bit-7-disable, but
**pre stencil/ulatm merge**). `Ula::render_scanline` at
`src/video/ula.cpp:270-310` populates only the ULA pixel colour; it
has no knowledge of TM or stencil.

However — the `ula_final_rgb` equivalent IS available at composite
time as the local variable `u_px` (computed at
`renderer.cpp:256-280`), together with `ulatm_transp` at
`renderer.cpp:257` / `274`. This is precisely VHDL 7125-7137's
`ula_final_rgb` / `ula_final_transparent` pair: the stencil branch
(258-271) matches VHDL 7130-7132 (stencil-AND), the else branch
(272-280) matches VHDL 7115-7116 (`ulatm_rgb` selection + 7134-7135
`ulatm_rgb` fall-through).

**Conclusion for Phase 1:**
- Variant `"00"` mix_rgb source = `ula_line_[x]` (zeroed when
  `ula_transp`). Current code.
- Variant `"10"` mix_rgb source = `u_px` (zeroed when `ulatm_transp`).
  Use the already-computed `u_px` / `ulatm_transp` — **NO second
  buffer needed**. This is important because `u_px` already fuses
  stencil mode + ulatm merge, matching `ula_final_rgb` exactly.
- Variant `"11"` mix_rgb source = `tm_px` (zeroed when `tm_transp`).
  No buffer needed.
- Variant `"01"` mix_rgb = 0, `mix_rgb_transparent = 1`. No buffer
  needed; the mix_top/mix_bot swap uses `ula_px` and `tm_px`
  directly with `tm_pixel_below_2` (= `ula_over_flags_[x]`).

One caveat for variant "10": VHDL 7151's `mix_rgb_transparent <=
ula_final_transparent` includes ULA disabled via `ula_transparent`
(VHDL 7103 = `ula_mix_transparent OR NOT ula_en_2`). Since
`ula_line_` is pre-zeroed when disabled, `ulatm_transp` correctly
includes that case already (via the `ula_transp` term in 274). Safe
to reuse `ulatm_transp` as `ula_final_transparent`.

**Phase 1 must NOT grow a second ULA line buffer.** The `u_px` /
`ulatm_transp` locals suffice.

### Open Q3 — Test coverage for Copper mid-scanline blend_mode changes

Out of scope per §Scope, but the open-ended question is whether
adding a UDIS-02-style "mid-scanline blend_mode toggle via Copper
MOVE" row should be scheduled in a future plan.
**Answer**: defer to the UDIS-02 follow-up plan (when the integration
fixture and Copper MOVE land).

## Acceptance criteria

Plan is DONE when ALL of the following hold:

- [ ] Phase 0 → Phase 2 all merged via author + critic cycles, no
      self-review.
- [ ] `Renderer` has `blend_mode_` member + `set_blend_mode(uint8_t)`
      accessor + `save_state` / `load_state` persistence.
- [ ] `emulator.cpp` NR 0x68 handler decodes bits 6:5 and forwards via
      `set_blend_mode`.
- [ ] `renderer.cpp` priority-mode 6 + 7 branches respect all 4
      `blend_mode_` values per VHDL 7141-7178.
- [ ] `compositor_test` UDIS-03 is a live `check()`, not a `skip()`.
- [ ] Group BL has ≈9 new rows covering modes 01 / 10 / 11.
- [ ] Existing 20 mode-00 Group BL rows still pass unchanged (pixel
      diff verified).
- [ ] `compositor_test` count: 117 / 114 / 0 / 3 → 127 / 124 / 0 / 2
      (UDIS-01 / UDIS-02 carry).
- [ ] Regression 34 / 34 / 0. No FUSE Z80 regression.
- [ ] No screenshot-test changes expected (blend modes 01/10/11 are
      not exercised by any reference screenshot today); if any
      screenshot DOES change, investigate before regenerating the
      reference.

## Relation to the BLOCKED `TASK-COMPOSITOR-NR68-BLEND-PLAN.md`

That plan covers UDIS-01 + UDIS-02 + UDIS-03 as a single unit. After
this plan closes, update its Status block to note UDIS-03 is closed
here, and keep UDIS-01 / UDIS-02 parked on their respective
independent blockers. The two plans are complementary, not
conflicting — closing this one shrinks the BLOCKED plan's remit from
3 rows to 2.

## Appendix A — Phase 2 test-row list

Authored in Phase 0. Row IDs follow the existing Group BL numbering
(BL-10..16, BL-20..29, L2P-17/18 all mode "00"). Phase 2 adds 10 new
rows: three per new variant (BL-30/31/32 for "01", BL-40/41/42 for
"10", BL-50/51/52 for "11") plus BL-60 for mode-"11" under priority
mode 7 (subtractive). Each row fixes `layer_priority = 6` (additive)
or `7` (subtractive), calls `r.set_blend_mode(<variant>)`, and sets
stimulus so the VHDL oracle for that variant differs from the mode-00
oracle.

Every row cites VHDL `zxnext.vhd:<lines>` and names the mix signal
whose selection is being observed.

| ID | Mode | Prio | VHDL | Focus / stimulus | Oracle |
|----|------|------|------|------------------|--------|
| BL-30 | "01" | 6 | 7163-7176 | L2=(3,2,1), ULA=(3,2,1) opaque, TM transp — additive with mix_rgb=0 | L2 through the `elsif layer2_transparent=0` arm = L2 (not blended). |
| BL-31 | "01" | 6 | 7163-7176 | L2=(0,0,0), ULA=(3,2,1) opaque, TM=(1,1,1) opaque, `tm_below=0` → `mix_top_transp=tm_transp=0` | mix_top wins → TM (ULA masked out). |
| BL-32 | "01" | 6 | 7163-7176 | Same as BL-31 but `tm_below=1` → `mix_top=ULA` (transp or not per `ula_transparent`), `mix_bot=TM` | mix_top = ULA (opaque here), mix_bot = TM — cascade selects ULA via `!mix_top_transp` arm. |
| BL-40 | "10" | 6 | 7149-7155 | L2=(3,2,1), ULA=(3,2,1) opaque, TM transp, stencil OFF → `ula_final_rgb = ulatm_rgb = ULA` | mixer = add(L2, ULA) = (6,4,2). `mix_top_transparent=1` forced → cascade skips both TM arms → mixer via L2-opaque arm. |
| BL-41 | "10" | 6 | 7149-7155 | L2=(1,1,1), ULA transp, TM=(2,2,2) opaque, stencil OFF, `tm_below=1` → `ulatm_rgb=TM` (VHDL 7116 `ulatm_rgb <= tm_rgb when tm_below=0 or ula_transp`) | mix_rgb = TM; mixer = add(L2, TM) = (3,3,3). `mix_top_transparent=1` forced → TM arms skipped → mixer. |
| BL-42 | "10" | 6 | 7149-7155 + 7130 | L2=(0,0,0), ULA=(3,2,1), TM=(3,2,1), stencil ON + `ula_en=tm_en=1` → `ula_final_rgb = stencil_rgb = ULA AND TM` = (3,2,1) | mixer = add(L2, stencil) = (3,2,1). Observes stencil routing through `ula_final_rgb`. |
| BL-50 | "11" | 6 | 7156-7162 | L2=(3,2,1), ULA=(1,1,1) opaque, TM=(2,2,2) opaque — additive with mix_rgb=TM, mix_top/mix_bot=ULA | `tm_below=0` → `mix_top_transp = ula_transp OR NOT tm_below = ula_transp OR 1 = 1`, `mix_bot_transp = ula_transp OR 0 = 0` → cascade: mix_top skipped, mix_bot=ULA. |
| BL-51 | "11" | 6 | 7156-7162 | Same stimulus as BL-50 but `tm_below=1` → `mix_top_transp = ula_transp = 0` (ULA opaque), `mix_bot_transp = 1` | cascade: mix_top wins = ULA. |
| BL-52 | "11" | 6 | 7156-7162 | L2=(0,0,0), ULA transp, TM=(4,2,1) opaque, `tm_below=0` → mix_rgb=TM mixer=add(L2,TM)=(4,2,1), mix_top=ULA (transp), mix_bot=ULA (transp) | cascade falls to L2-opaque-else arm? L2 transp here → mixer via final `elsif layer2_transparent=0`? Set L2=(0,0,0) opaque (non-transparent black) → mixer = (4,2,1). Observes TM-as-mix-source. |
| BL-60 | "11" | 7 | 7156-7162 + 7312-7352 | Mode "11" under priority-7 (subtractive). L2=(5,5,3), ULA transp, TM=(4,2,1) opaque, `tm_below=0` → mix_rgb=TM | subtractive: s_r=5+4=9 → >=12? no → s−5=4; s_g=5+2=7 → s−5=2; s_b=3+1=4 → s<=4 → 0. Result = (4,2,0). mix_top/bot both ULA(transp) → cascade to L2-opaque arm → mixer. |

Notes for Phase 2 authoring:

- `Renderer` does not currently expose `set_blend_mode`; Phase 1 adds
  it. Phase 2 depends on Phase 1 landing first (enforced by the
  phase ordering in §Phases).
- Each row should use `clear_layers(r)` then set only the stimulus
  pixels, matching the pattern of the existing BL-10..29 rows.
- Expected values are computed via the existing `bl_add` / `bl_sub`
  helpers (lines 886-901 of `compositor_test.cpp`) where they apply.
- UDIS-03 flip is separate (not a BL row); see §Phase 2 item 2.

This appendix supersedes the row-ID placeholder mentions in the body
of the plan (BL-3x / 4x / 5x — concrete IDs are as tabled above).
