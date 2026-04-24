# Compositor UDIS-03 (`ula_blend_mode`) Audit (2026-04-24)

Closure audit for the Compositor UDIS-03 plan executed 2026-04-24 in a
single session (Phases 0 → 2 + archive). Mirrors the structure of
[task-nmi-phase4.md](task-nmi-phase4.md) and
[task3-audio-phase4.md](task3-audio-phase4.md).

Plan doc: [doc/design/TASK-COMPOSITOR-ULA-BLEND-MODE-PLAN.md](../../design/TASK-COMPOSITOR-ULA-BLEND-MODE-PLAN.md).
Predecessor (parked UDIS-01/02 sibling rows): [doc/design/TASK-COMPOSITOR-NR68-BLEND-PLAN.md](../../design/TASK-COMPOSITOR-NR68-BLEND-PLAN.md).
Parallel investigation spawned mid-plan: [doc/issues/BEAST-NEX-INVESTIGATION.md](../../issues/BEAST-NEX-INVESTIGATION.md).
Closing commit: `60145d0` (`doc(issues): beast.nex rendering investigation archive`).
Main tip at audit: `60145d0b4f42edf880fb92e2354705d12b3840bd`.

## Headline numbers

|                              | Before plan (2026-04-24) | After plan (2026-04-24) | Δ         |
|------------------------------|-------------------------:|------------------------:|----------:|
| `compositor_test`            |              115/114/0/1 |             125/125/0/0 |  +11/+11/−1 |
| Aggregate unit suites        |                     32   |                     32  |     0     |
| Aggregate pass               |                   3199   |                   3210  |   +11     |
| Aggregate skip               |                    117   |                    116  |    −1     |
| Regression                   |                  34/0/0  |                 34/0/0  | unchanged |
| FUSE Z80                     |               1356/1356  |              1356/1356  | unchanged |

Headline delta: **UDIS-03 flipped `skip()` → live `check()`, 10 new
Group BL rows landed (BL-30/31/32, BL-40/41/42, BL-50/51/52, BL-60)
covering `ula_blend_mode` variants 01/10/11 under priority mode 6
additive and variant 11 under priority mode 7 subtractive.
`compositor_test` hits **zero skips**. No regression.**

## Phase summary

| Phase | Action | Critic verdict | Commits |
|---|---|---|---|
| Plan authoring | Author plan doc + beast.nex driver citation + tighten variant "01" description (Triage-01) + Q1 risk note. | APPROVE-WITH-FIXES (cite beast.nex, tighten Triage-01 / Q1) | `98ff825` |
| 0 | Refresh UDIS-03 skip reason + Group BL top-of-group comment; author row-appendix (10 rows, VHDL cited); Q1/Q2 resolutions inline. | APPROVE-WITH-FIXES (deviation note for mode-00 `ula_transp` vs `ula_mix_transparent`; BL-30/52 oracle tweaks) | `385b75a` + `07b1ac1` + fix `36b3caa` |
| 1 | `Renderer::blend_mode_` member + `set_blend_mode` accessor; NR 0x68 write-handler bits 6:5 decode; 4-variant switch in priority modes 6/7 with cascade generalised to `mix_top_rgb_px` / `mix_bot_rgb_px` locals; Phase-1 sanity row BL-PHASE1-SANITY embedded. | APPROVE (`tm_below` / `ula_over_flags_[x]` polarity verified semantic-equal despite misleading name) | `2d30cff` + `2b9de44` + `ca88d01` |
| 2 | 10 formal Group BL rows replace the sanity row; UDIS-03 flipped to live `check()` iterating 0x00/20/40/60 through NR 0x68 handler; dashboards + plan-doc CLOSED block. | APPROVE | `f4d0275` + `3a4953f` + `3ac39bd` |
| — | Parallel beast.nex investigation archived — UDIS-03 was wired in good faith per the user-visible driver citation; the investigation proved beast's bug is orthogonal (Layer 2 never enabled, tilemap-transparent leak through uninitialised bank-5 ULA memory). UDIS-03 fix remains correct and independently valid. | (in-line, diagnostic only) | `60145d0` |
| 3 | This audit doc. | (in-line) | (this commit) |

Phase 3 was marked *optional* in the plan — triggered here because the
beast.nex driver claim in the plan-body was invalidated mid-session,
meriting a documented post-mortem even though the core work is
otherwise clean.

All Phase-0 and Phase-1 and Phase-2 outputs passed through
independent critic review before merge (per
`feedback_never_self_review.md`).

## Plan-drifts caught during execution

Six drifts surfaced between plan text and VHDL oracle / execution
reality. Each was corrected before the closing commit, or converted
into a documented KNOWN DEVIATION.

### 1. Plan-doc line numbers were off by ~5

Plan body originally cited `renderer.cpp:337-339` (mode 6) and
`renderer.cpp:365-367` (mode 7) for the hard-coded `"00"` expressions.
Phase 0 audit read the live file and found the expressions at
`renderer.cpp:342-344` / `renderer.cpp:370-372`. Plan was updated
pre-Phase-1 (commit `98ff825` partially, `07b1ac1` completed the
correction in the Q1 resolution block). No code impact; pure
documentation hygiene.

### 2. `beast.nex` claimed as user-visible driver — turned out to be orthogonal

Plan initially authored without a user-visible repro (§"User-visible
driver" was absent). Mid-plan-authoring critic flagged "no user
driver" as a weak kick-off; user pointed at `beast.nex` as a visual
repro of ULA-leaking-through-layer-2 cells; plan-doc updated in
`98ff825` to cite beast as the driver.

Post-Phase-1 investigation (archived at
`doc/issues/BEAST-NEX-INVESTIGATION.md`) proved beast never writes
NR 0x68 (default blend_mode = 00, which Phase 1 preserves
pixel-identically), and in fact uses priority mode 0 (SLU) — which
doesn't consult `blend_mode` at all. Beast's real bug is tilemap-
transparency cells leaking uninitialised bank-5 data through ULA.
UDIS-03 fix is orthogonal and correct regardless.

**Lesson**: a visual-comparison screenshot is not a sufficient "user
driver" — future plan-docs demanding a repro driver should require an
NR-write trace confirming the register band cited in the plan is
actually touched by the repro.

### 3. Phase 1 agent hit stale-base bug

Phase 1 author worktree branched from `5342464` rather than current
main tip `36b3caa`. Manual rebase agent landed cleanly — the two
commits touched disjoint regions by luck (Phase 0 only touched plan-
doc + test-skip-reason, Phase 1 only touched renderer.{h,cpp} +
emulator NR 0x68 handler + a scaffold test row). Same systemic bug
observed in UART+I2C, Audio, and NMI plans
(`feedback_agent_worktree_stale_base.md`).

### 4. Phase 0 critic caught 2 wobbly oracles in the row appendix

BL-30 oracle originally computed the cascade wrong (mix_top/mix_bot
semantics misapplied for variant "01" `tm_below=0`). BL-52 oracle
mis-cited the `ula_transparent` composition for variant "11" with
both `mix_top` and `mix_bot` ULA-transparent arms. Phase 0 critic
fixes landed in `36b3caa` — no Phase 1/2 rework because the fix
predates Phase 1.

### 5. Mode-00 `ula_transp` vs `ula_mix_transparent` — KNOWN DEVIATION

Q1 resolution (plan-doc §Open Q1, lines 411-433) documented a
semantic drift: the mode-00 arm uses `ula_transp` (folds `ula_en_2`
via the pre-zeroed `ula_line_` buffer) where VHDL 7143 uses
`ula_mix_transparent` (which doesn't fold `ula_en_2`).
Observationally identical today because `renderer.cpp:88-90`
pre-zeroes `ula_line_` when the per-line `ula_enabled_per_line_[row]`
flag is false.

Phase 1 was **explicitly directed NOT to fix this** (plan §Scope item
4 KNOWN DEVIATION block). Variant "10" uses the correct
`ula_final_transparent` semantics via `ulatm_transp`, so the mode-00
conflation stays benign. Documented in-plan; no test row covers it.

### 6. `ula_over_flags_[x]` naming is misleading — verified semantic-correct

Phase 1 critic flagged that `ula_over_flags_[x]` reads "ULA over TM"
but is actually written by `tilemap.cpp:400` as `pixel_below` (TM
below ULA). Semantic polarity verified against VHDL
`tm_pixel_below_2` — match is exact; the name is misleading but the
behaviour is correct. Optional rename to `tm_below_flags_` deferred
as a hygiene backlog item (plan §Q1 resolution, incidental cleanup
paragraph).

## Bugs fixed + features shipped

### (a) NR 0x68 bits 6:5 silently dropped — fix `2b9de44`

Handler at `src/core/emulator.cpp:816-825` decoded bits 7/3/2/0 but
dropped bits 6:5 despite an in-line comment acknowledging them (VHDL
`zxnext.vhd:5444-5450`). Fix: forward
`renderer_.set_blend_mode((v >> 5) & 0x03)`.

### (b) Priority modes 6/7 hardcoded variant "00" arithmetic — fix `ca88d01`

`renderer.cpp:335-403` computed mix signals per VHDL 7142-7148 only;
variants "01"/"10"/"11" fell through to the same code. VHDL
`zxnext.vhd:7141-7178` has 3 explicit branches + `when others` for
"01". Fix: 4-way switch on `blend_mode_` per plan §Triage table;
priority 6 additive and 7 subtractive share the branching.

### (c) Cascade hardcoded `tm_px` in `!mix_top_transp` / `!mix_bot_transp` arms — fix `ca88d01`

Cascade routed `tm_px` when mix_top/mix_bot was opaque — correct for
"00", wrong for "01"/"11" where ULA can be the overlay. VHDL
`zxnext.vhd:7300-7310` / `7342-7352`:
`l2_p_rgb_2 <= mix_top_rgb` (variant-dependent). Fix:
`mix_top_rgb_px` / `mix_bot_rgb_px` locals pick TM vs ULA per
`blend_mode_`.

### Features shipped

- **Renderer extensions** (`src/video/renderer.{h,cpp}`):
  - `uint8_t blend_mode_ = 0;` member, `save_state` / `load_state`
    persistence.
  - `set_blend_mode(uint8_t)` accessor (header-inline, same style as
    `set_stencil_mode`).
  - 4-variant switch in `composite_scanline` priority-mode 6/7
    branches per VHDL 7141-7178.
  - Cascade generalised to `mix_top_rgb_px` / `mix_bot_rgb_px`
    locals.
- **Emulator handler extension** (`src/core/emulator.cpp:816-825`):
  - NR 0x68 handler now forwards bits 6:5 to
    `renderer_.set_blend_mode`.
- **New test rows** (`test/compositor/compositor_test.cpp`, Group BL):
  - BL-30/31/32 — variant "01" under priority 6 additive.
  - BL-40/41/42 — variant "10" under priority 6 additive (includes
    stencil routing via `ula_final_rgb` per VHDL 7130-7132).
  - BL-50/51/52 — variant "11" under priority 6 additive.
  - BL-60 — variant "11" under priority 7 subtractive.
- **UDIS-03 flipped live** — minimal wiring test iterates NR 0x68 =
  0x00/20/40/60 through the write handler and asserts
  `Renderer::blend_mode()` returns 0/1/2/3; pixel-level coverage
  lives in BL-30..60.

`compositor_test`: 115 / 114 / 0 / 1 → **125 / 125 / 0 / 0**.

## Backlog / follow-ups

1. **`ula_over_flags_[x]` → `tm_below_flags_[x]` rename** — misleading
   name; polarity is semantic-correct. Deferred from Phase 1 as
   incidental cleanup (plan §Q1 resolution).

2. **beast.nex tilemap-transparency / ULA-leak bug** — archived at
   `doc/issues/BEAST-NEX-INVESTIGATION.md`; root causes narrowed to
   uninitialised bank-5 ULA memory + possible Copper-MOVE
   interpretation gap. Needs its own plan doc. Untouched by this plan.

3. **Mode-00 `ula_transp` vs `ula_mix_transparent` semantic drift** —
   benign per Phase 0 Q1 resolution; documented as KNOWN DEVIATION.
   Revisit only if a future variant introduces a case where
   `ula_en_2` folding matters.

4. **MX-06 / UDIS-01 / UDIS-02 remain parked** — `TASK-COMPOSITOR-
   NR68-BLEND-PLAN.md` Status block now reflects UDIS-03 closed; two
   rows still blocked on orthogonal infrastructure (compositor
   integration-test fixture refinement + Copper+NMI pipeline post-
   NMI-plan-close sanity).

## Systemic findings carried forward

- **Agent worktree stale-base bug**
  (`feedback_agent_worktree_stale_base.md`) — recurred on Phase 1.
  Rebase-on-entry pattern remains mandatory; clean fallout this time
  (disjoint regions) but luck-dependent.
- **Main-session-only regression runs**
  (`feedback_regression_main_session.md`) — held. Main session
  confirmed 34/0/0 post-all-phases before this audit was authored.
- **Terse commit messages** (`feedback_terse_commit_messages.md`) —
  all 11 plan commits are ≤4 lines of body. Held.
- **Terse dashboard entries** (`feedback_terse_dashboard_entries.md`) —
  held (Phase 2 dashboard update in `3ac39bd` is ≤12 words per row).
- **Visual-comparison drivers are misleading** — new finding: beast.nex
  was adopted as user-visible driver based on screenshot similarity;
  the real bug had nothing to do with `ula_blend_mode`. Future plan
  docs that cite a specific NEX / demo as the repro driver should
  demand a runtime NR-trace confirming the relevant registers are
  actually touched, not just a visual match. Propose recording this as
  a new feedback entry if it recurs.

## Commits landed (UDIS-03 plan)

```
Plan doc:       (pre-98ff825 earlier authoring) → 98ff825 (critic fixes — beast cite + Triage-01)
Phase 0:        385b75a (UDIS-03 skip-reason refresh + BL top-of-group comment)
                07b1ac1 (Q1/Q2 resolutions + row appendix)
Phase 0 fix:    36b3caa (critic fixes — deviation note + BL-30/52 oracle)
Phase 1:        2d30cff (Renderer::blend_mode_ + set_blend_mode)
                2b9de44 (NR 0x68 bits 6:5 decode)
                ca88d01 (4-variant switch + cascade generalisation)
Phase 2:        f4d0275 (10 BL rows: BL-30/31/32/40/41/42/50/51/52/60)
                3a4953f (UDIS-03 live flip)
                3ac39bd (Phase 2 CLOSED — dashboards + plan update)
Parallel:       60145d0 (beast.nex investigation archive)
```

All Phase-1 and Phase-2 merges passed independent critic review before
landing on main.

## Acceptance criteria checklist

- [x] Phase 0 → Phase 2 all merged via author + critic cycles, no
      self-review.
- [x] `Renderer` has `blend_mode_` member + `set_blend_mode(uint8_t)`
      accessor + `save_state` / `load_state` persistence.
- [x] `emulator.cpp` NR 0x68 handler decodes bits 6:5 and forwards via
      `set_blend_mode`.
- [x] `renderer.cpp` priority-mode 6 + 7 branches respect all 4
      `blend_mode_` values per VHDL 7141-7178.
- [x] `compositor_test` UDIS-03 is a live `check()`, not a `skip()`.
- [x] Group BL has 10 new rows covering modes 01 / 10 / 11 (+ prio 7
      for mode 11).
- [x] Existing mode-00 Group BL rows still pass unchanged (verified
      pre/post-Phase-1 by running the full `compositor_test`).
- [x] `compositor_test` count: 115 / 114 / 0 / 1 → **125 / 125 / 0 / 0**.
- [x] Regression 34 / 0 / 0. No FUSE Z80 regression. (Main-session
      confirmed.)
- [x] No screenshot-test changes.
- [x] Phase-3 audit merged (this document).

## Final verdict — CLOSED

- `compositor_test`: **125 / 125 / 0 / 0** — zero runtime skips.
- Aggregate: **3326 / 3210 / 0 / 116 across 32 suites**.
- Regression: **34 / 0 / 0**. FUSE Z80: **1356 / 1356**.

The `ula_blend_mode` (NR 0x68 bits 6:5) path is now production-grade
for:

- NR 0x68 bits 6:5 decode and forwarding to Renderer state.
- All 4 `ula_blend_mode` variants (00/01/10/11) in priority modes 6
  (additive) and 7 (subtractive) per VHDL 7141-7178 + 7286-7352.
- Mode-00 pixel identity preserved against pre-Phase-1 baseline.
- Variant "10" correctly routes through `ula_final_rgb` (the already-
  computed `u_px` / `ulatm_transp` locals) per VHDL 7149-7155 — no
  second ULA line buffer needed (Q2 resolution).
- Cascade arms correctly select TM vs ULA per variant (not always
  `tm_px` — that was mode-00's special case).

One KNOWN DEVIATION retained as benign (mode-00 `ula_transp` vs
`ula_mix_transparent`). One incidental rename deferred
(`ula_over_flags_` → `tm_below_flags_`). The beast.nex driver
citation was invalidated mid-plan but UDIS-03 stands on its own VHDL-
faithful merits. All 10 Group-BL rows landed; UDIS-03 flipped live.
No residual F-skips in `compositor_test`. No lazy-skips, no
tautologies, no coverage theatre.
