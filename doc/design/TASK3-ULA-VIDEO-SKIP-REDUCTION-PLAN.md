# Task 3 — ULA Video SKIP-reduction plan

Plan authored 2026-04-23. Follows the Task 3 Input SKIP-reduction
template (0→4 phases, parallel agent waves, independent critic review
per wave).

## Starting state

- `ula_test.cpp`: **123 / 48 / 0 / 75** (pass/fail/skip).
- User priority: **ULA hardware scroll (NR 0x26 / NR 0x27 / NR 0x68
  fine_scroll_x bit)** is the current scrolling regression blocker; 9
  of the 75 skips are direct tests of this feature, none of which has
  any implementation behind it today (grep confirms `src/video/ula*`
  and `src/port/nextreg*.cpp` contain zero mentions of `0x26`, `0x27`,
  or `scroll`). Wave A targets this cluster first so the user can
  validate the fix with real Next programs immediately after merge.

## Skip inventory by §-group

| Group | Rows | Cluster | Landing wave |
|---|---:|---|---|
| S2 | 2 | flash_cnt / border_active — "exercised by S3/S4" → **redundant coverage** | Phase 0 comment-sweep |
| S3 | 1 | border_active_v (raw vc, no accessor) → **internal detail** | Phase 0 comment-sweep |
| S4 | 2 | ULA+/ULAnext enable terms in pixel_en gate | follows Waves B+C |
| S5 | 4 | Hi-colour + shadow screen + border_clr_tmx + internal shift-reg | Wave D (3 real rows, 1 comment-sweep) |
| S6 | 12 | **ULAnext feature, entire subsystem missing** | Wave B |
| S7 | 6 | **ULA+ feature, entire subsystem missing** | Wave C |
| S8 | 3 | clip predicates (2 unobservable) + y2 clamp | Wave F (1 real row, 2 comment-sweep) |
| S9 | 10 | **ULA scroll — USER PRIORITY** (NR 0x26/0x27/0x68 bit) | Wave A (9 real rows, 1 comment-sweep) |
| S10 | 8 | Floating bus — **Emulator subsystem**, not Ula's responsibility | Phase 0 re-home F-skip (keep 5) + 3 comment-sweep |
| S11 | 12 | Contention model — **ContentionModel subsystem** | Phase 0 re-home F-skip |
| S12 | 3 | NR 0x68 blend-mode bits — **compositor subsystem** | Phase 0 re-home F-skip |
| S13 | 4 | Per-machine video timing — **VideoTiming subsystem** | Phase 0 re-home F-skip |
| S14 | 6 | Line interrupt + int position | Wave E (3 real rows, 3 re-home F-skip) |
| S15 | 2 | Shadow-screen routing — **Emulator/MMU** | Phase 0 re-home F-skip |

Post-Phase-0 target split (of 75 current skips):
- **~10 reclassified to in-line comments** (redundant with other §,
  or strictly internal VHDL shift-register detail with no C++
  observer).
- **~30 re-homed as F-skips** citing the correct subsystem plan
  (Emulator floating-bus, ContentionModel, Compositor, VideoTiming,
  Emulator/MMU shadow-screen routing).
- **~35 rows left as flippable F-skips**, distributed across six
  parallel implementation waves (see Phase 2).

## Phase 0 — triage (single agent)

1. Reclassify 10 rows to in-line `// G: <reason>` comments (no
   `skip()` call), per `feedback_unobservable_audit_rule` taxonomy:
   - S2.08, S2.10 (flash_cnt / border_active upstream, exercised by
     S3/S4 end-to-end).
   - S3.08 (border_active_v raw vc, no accessor — compositor-level
     boundary).
   - S5.08 (attr_reg loading with border_clr_tmx — shift-reg detail).
   - S8.06, S8.07 (clip predicates with no observable comparator —
     compositor verifies end-to-end).
   - S9.01 (no-scroll baseline covered by §1/§2).
   - S10.02, S10.03, S10.04 (hc-phase capture states — internal,
     end-to-end by §1/§2).
2. Re-home F-skips with explicit subsystem pointers:
   - S10.01, S10.05, S10.06, S10.07, S10.08 → "F: blocked on
     Emulator::floating_bus_read — separate subsystem".
   - S11.01-12 → "F: blocked on ContentionModel subsystem".
   - S12.02, S12.03, S12.04 → "F: blocked on Compositor NR 0x68
     blend-mode wiring".
   - S13.05-08 → "F: blocked on VideoTiming per-machine accessor
     expansion".
   - S14.01-03 → "F: blocked on VideoTiming per-machine int-position
     exposure".
   - S15.03, S15.04 → "F: blocked on Emulator/MMU shadow-screen
     routing (port 0x7FFD bit 3 + i_ula_shadow_en)".
3. Refresh all skip reason strings to include authoritative VHDL
   line-ranges so the audit dashboard reads cleanly.

**Expected Phase 0 delta**: `123 / 48 / 0 / 75 → ~113 / 48 / 0 / 65`
(10 rows move out of skip()-land into comment-land) with the
remaining 65 rows cleanly classified.

## Phase 1 — scaffold (single agent)

Extend `src/video/ula.{h,cpp}` with:

- NR 0x26 `ula_scroll_x_coarse` (8-bit, zxula.vhd:199) setter/getter.
- NR 0x27 `ula_scroll_y` (8-bit, zxula.vhd:193-207) setter/getter.
- NR 0x68 bit 2 `ula_fine_scroll_x` (1-bit, zxula.vhd:199) setter/getter
  (standalone — the rest of NR 0x68 is compositor-level).
- NR 0x42 `ulanext_format` (8-bit, zxula.vhd:503-515) setter/getter.
- NR 0x43 control register plumbing for `ulanext_en` (bit 0) and
  `ulap_en` gating per zxula.vhd:531. (The other bits of NR 0x43 are
  palette-selector bits owned by the compositor — do NOT fold them in
  here; a narrow setter that forwards just the two enable bits to
  the Ula is sufficient.)
- Port 0xFF3B / 0xBF3B handler for `port_ff3b_ulap_en` (zxula.vhd:531).
- `set_shadow_screen_en(bool)` — mirror i_ula_shadow_en, forces
  `screen_mode` to 000 per zxula.vhd:191.
- `set_alt_file(bool)` — mirror the alt-file bit at zxula.vhd:218 for
  the HI_COLOUR+alt mode 011 discrimination.
- `set_clip_y2()` — apply the `y2>=0xC0` clamp per
  zxnext.vhd:6779-6782 at store time (not at read time — match VHDL).
- `render_border_line()` — route border_clr_tmx through the hi-res
  path per zxula.vhd:419.

NR-handler additions: NEW write_handlers 0x26, 0x27, 0x42. EXTENDED
0x43 (forward bits 0 + enable flag to Ula; preserve existing
compositor/palette logic). EXTENDED 0x68 (forward bit 2 to Ula;
preserve existing blend-mode bits for the compositor).

Port-handler additions: NEW 0xFF3B read/write + 0xBF3B read/write
mirror per VHDL decode pattern.

Also in Phase 1: **add test-only accessors** on Ula (`get_scroll_x_*`,
`get_scroll_y`, `get_fine_scroll_x`, `get_ulanext_format`,
`get_ulanext_en`, `get_ulap_en`, `get_port_ff3b_en`,
`get_shadow_screen_en`, `get_alt_file`, `get_clip_y2`) so Wave A-F
tests can assert the setter side deterministically without depending
on pixel output.

No test flips in Phase 1 — pure API surface.

## Phase 2 — parallel waves (5 agents, 1 wave)

Agent allocations run in one 5-up wave (user-approved budget from
2026-04-21). Each agent owns a single branch
`task3-ula-<wave-letter>-<short-name>`, gets an independent critic
after merge, and must stay in its worktree.

### Wave A — ULA scroll (USER PRIORITY, 9 rows)

Rows: **S9.02, S9.03, S9.04, S9.05, S9.06, S9.07, S9.08, S9.09, S9.10**.

Scope:
- Implement render-time X/Y scroll folding per zxula.vhd:193-216:
  effective_x = (raw_x + scroll_x_coarse*8 + (fine_scroll_x ? 1 : 0))
  mod 256; effective_y wraps at 192 (cross-third wrap per :200-207).
- Flip the 9 skip() calls to check() with stimulus/expected taken
  from zxula.vhd:193-216 + the VHDL :199/:200-207 exact line cites.

This is the cluster the user will verify in real Next programs; land
it first regardless of serialisation order.

### Wave B — ULAnext (13 rows)

Rows: **S4.05, S6.01-12**.

Scope:
- NR 0x42 + NR 0x43 format/enable per zxula.vhd:492, 503-515.
- Paper-lookup table for formats 0x07/0x0F/0x3F/0xFF/0x01 per
  :503-515.
- `ulanext_en` term in pixel_en gate per :470 (unblocks S4.05).
- Ink AND format per :497; transparent border for format 0xFF per
  :520-525; paper_base_index border per :492-495.

### Wave C — ULA+ (7 rows)

Rows: **S4.06, S7.01-06**.

Scope:
- Port 0xFF3B enable mux per zxula.vhd:531.
- Palette group 3 encoding from attr bits 7:6 per :531.
- Paper path for palette group 3 per :531-541.
- Hi-res forces bit 3 via screen_mode(2) per :531.
- attr(7) reinterpretation as palette-group bit (not flash) when
  ULA+ enabled per :531.
- `ulap_en` term in pixel_en gate per :470 (unblocks S4.06).

### Wave D — Hi-colour + shadow + border_clr_tmx (3 rows)

Rows: **S5.04, S5.06, S5.07**.

Scope:
- Distinguish HI_COLOUR (mode 010) from HI_COLOUR+alt (mode 011)
  via alt_file bit per zxula.vhd:218.
- Route border_clr_tmx through `render_border_line()` hi-res path
  per :419.
- Wire `i_ula_shadow_en` → `screen_mode = 000` forcing per :191.

Note: S15.03 ("port 0x7FFD bit 3 → i_ula_shadow_en") is **not** in
this wave — it's the Emulator/MMU side of the wiring and stays as an
F-skip under the "Emulator/MMU shadow-screen routing" re-home. Wave D
implements the Ula side; a future Emulator/MMU patch wires port
0x7FFD bit 3 into `Ula::set_shadow_screen_en`.

### Wave E — Line interrupt + inten_ula_n (3 rows)

Rows: **S14.04, S14.05, S14.06**.

Scope:
- `inten_ula_n` accessor + disable-pulse path per zxula_timing.vhd:547-559.
- Line-interrupt at `hc_ula=255` when `cvc==target` per :562-583.
- `line=0` fires at `cvc=max_vc` boundary case per :562-583.

Does NOT touch S14.01/02/03 (per-machine int positions) — those stay
as VideoTiming F-skips. Per-machine exposure is out of Ula's scope;
Wave E just checks the mechanism fires when fed correct inputs.

### Wave F — Clip y2 clamp (1 row)

Row: **S8.08**.

Scope:
- Apply `y2 >= 0xC0` clamp at `Ula::set_clip_y2` store time per
  zxnext.vhd:6779-6782.

Small wave; can be rolled into Wave D if the agent budget is tight.
Keep separate by default — simpler critic review, zero merge risk.

## Phase 3 — un-skip + integration (single agent)

1. Flip all Wave A-F `skip()` calls to `check()` with deterministic
   assertions against Phase 1 accessors + pixel-output where
   end-to-end is available.
2. Create `test/ula/ula_int_test.cpp` (new suite) covering:
   - Scroll: render a known pattern, set NR 0x26=8 + NR 0x27=32,
     verify the pixel offset matches VHDL expectation.
   - ULA+: enable port 0xFF3B, verify palette group 3 selects the
     correct indices in a rendered row.
   - ULAnext: enable NR 0x43, set NR 0x42=0x0F, verify paper index
     matches the zxula.vhd:503-515 lookup.
3. Target: zero net new `skip()`. Any newly-surfaced F should be
   fixed in-phase (per `feedback_surfaced_regressions_in_phase`).

## Phase 4 — dashboard + audit

1. Update:
   - `test/SUBSYSTEM-TESTS-STATUS.md` with new `ula_test` + `ula_int_test`
     counts.
   - `test/TRACEABILITY-MATRIX.md` adding NR 0x26, 0x27, 0x42, 0x43,
     port 0xFF3B rows.
   - `doc/testing/ULA-TEST-PLAN-DESIGN.md` (the ULA test plan) to
     match the closed rows — amend §9 scroll, §6 ULAnext, §7 ULA+,
     §14 line-interrupt sections with the live test IDs.
   - `doc/design/EMULATOR-DESIGN-PLAN.md` ULA section with the new
     NR/port handlers.
2. Write `doc/testing/audits/task3-ula-phase4.md` — per-wave results,
   critic findings, any plan-doc inconsistencies surfaced mid-wave
   (queued for a follow-up backlog if we're short on time), and the
   final ula_test + ula_int_test numbers.

## Expected end-state

| Suite | Before | After |
|---|---:|---:|
| `ula_test` | 123 / 48 / 0 / 75 | **~123 / 95 / 0 / ~28** |
| `ula_int_test` (NEW) | — | **~6-8 pass** |
| Project aggregate | 3219 / 3001 / 0 / 218 | **~3230 / 3060 / 0 / ~170** |

~28 skips remain — they are the re-homed F-skips awaiting
Emulator floating-bus (5), ContentionModel (12), Compositor NR 0x68
(3), VideoTiming (7 — 4 per-machine + 3 int-position), Emulator/MMU
shadow-screen (2 — wired but not the Ula side). Each has a pointer
to the responsible subsystem plan.

## Risks + open questions

1. **Scroll interaction with compositor.** The ULA layer's scrolled
   output is consumed by the compositor. Verify the compositor does
   not also apply scroll (double-offset bug). If it does, Wave A's
   integration test must lock the correct layering.
2. **NR 0x43 bit split between Ula and Compositor.** The register
   holds both ULAnext/ULA+ enables (Ula) and palette-group selects
   (Compositor). Agent B + Agent C must not both write the whole
   byte through — they need a narrow "enable bits only" accessor or
   they'll trample each other's state. Phase 1 scaffold MUST design
   this split explicitly before Phase 2 kicks off.
3. **Port 0xFF3B decoder.** Currently no 0xFF3B entry in
   `src/port/port_dispatch.cpp`. Agent C lands a new entry;
   Phase-1-scaffold pre-adds the handler stub so Wave C just fills
   it in (no cross-branch port-dispatch conflict).
4. **ULA+ 64-colour palette.** ULA+ brings a 64-entry RGBRRGBB
   palette; our palette subsystem currently is 512-entry
   RRRGGGBB. The ULA+ path reuses the 512-entry store but addresses
   different indices. Verify Agent C doesn't accidentally resize the
   palette array.

## Kickoff checklist

Before Phase 0 launches, main-session confirms:
- [ ] Full unit-test baseline 3219 / 3001 / 0 / 218 (22 suites).
- [ ] Full regression 34 / 0 / 0.
- [ ] Worktree + branch budget — Phase 2 Wave uses 5 parallel agents
  (A, B, C, D, E). Waves F can piggy-back on D or land serialised
  later.
- [ ] User approval for the 2 plan-doc Open Questions above (NR 0x43
  split, compositor scroll interaction).
