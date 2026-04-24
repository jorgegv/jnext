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
`renderer.cpp:336`), so 3 of the 4 variants are silently wrong.

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
- Variant `"01"` produces a fully-transparent mix_rgb, which collapses
  modes 6/7 to an "overlay only" result — effectively TM-over-ULA (or
  ULA-over-TM if `tm_below`) with L2 supplying fallback via `layer2_priority`
  / the final `elsif layer2_transparent = '0'` arm of the priority
  cascade. Note `when others` applies to `"01"` AND any
  out-of-2-bit value, which is a VHDL-level catch-all; for us, only
  `"01"` can reach this branch.

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
`mix_rgb_transp` / `mix_top_transp` / `mix_bot_transp` expressions
exactly equal VHDL 7142-7148 (not "close enough"). If there is an
existing drift, it must be corrected in Phase 1 as part of the
refactor; do NOT preserve a pre-existing bug as "mode 00".

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
