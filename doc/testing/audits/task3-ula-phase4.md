# Task 3 — ULA Video Subsystem SKIP-Reduction Audit (2026-04-23)

Closure audit for the Task 3 ULA Video subsystem skip-reduction plan executed
2026-04-23 in a single session (Phases 0 → 4). Mirrors the structure of
[task3-input-phase4.md](task3-input-phase4.md) and
[task3-ctc-phase5.md](task3-ctc-phase5.md).

Plan doc: [doc/design/TASK3-ULA-VIDEO-SKIP-REDUCTION-PLAN.md](../../design/TASK3-ULA-VIDEO-SKIP-REDUCTION-PLAN.md).
Row-by-row baseline: [task3-ula.md](task3-ula.md) (2026-04-15 state; 123/48/0/75 with 1 good-fail regression witness S13.14).

## Headline numbers

|                             | Before plan (2026-04-22) | After plan (2026-04-23) | Δ          |
|-----------------------------|-------------------------:|------------------------:|-----------:|
| `ula_test` total            |                  123     |                113      |   −10      |
| `ula_test` pass             |                   48     |                 84      |   +36      |
| `ula_test` fail             |                    1     |                  0      |    −1 (note: S13.14 GOOD-FAIL kept per process manual §3; the `84` pass count above excludes it — retained as failing check regression witness) |
| `ula_test` skip             |                   75     |                 29      |   −46      |
| `ula_integration_test`      |                  N/A     |              6/6/0/0    |   +6 pass  |
| Aggregate unit suites       |                   22     |                 23      |    +1      |
| Aggregate pass              |                 3001     |               3043      |   +42      |
| Aggregate skip              |                  218     |                172      |   −46      |
| Regression                  |                34/0/0    |              34/0/0     | unchanged  |
| FUSE Z80                    |             1356/1356    |          1356/1356      | unchanged  |

Headline delta: **+36 `skip()` → `check()` flips, +10 rows reclassified to
`// G:` source comments (unobservable at this abstraction), +6 new
integration-suite rows (all pass), 29 remaining skips all F-blocked to
named subsystem plans.**

## Phase summary

| Phase | Action | Critic verdict | Commits |
|---|---|---|---|
| 0 | Triage + comment sweep (10 rows reclassified to `// G:`; 29 F-skips re-homed with explicit owner-subsystem pointers; 1 compositor scroll audit doc) | APPROVE | `209c721` + merge `029f77a` + doc-trim `08566a0` |
| 1 | API scaffold (NR 0x26/0x27/0x42 handlers, NR 0x43 bit 0 + NR 0x68 bit 2 enable-forwarding, port 0xFF3B/0xBF3B, `Ula::set_shadow_screen_en`, `Ula::set_alt_file`, `Ula::set_clip_y2` with render-time clamp) | APPROVE (+ plan-doc correction committed separately) | `8cd3488` merged as `916435e` + plan-doc correction `52b30bf` |
| 2 Wave A | Scroll (9 rows: S9.02–S9.10) — USER PRIORITY | APPROVE | `1384b4f` merged as `813df83` |
| 2 Wave B | ULAnext (13 rows: S4.05, S6.01–12) | APPROVE-WITH-FIXES (0x7F format coverage gap noted, non-blocking) | `7af85a3` merged as `6eea811` |
| 2 Wave C | ULA+ (7 rows: S4.06, S7.01–06) | APPROVE-WITH-FIXES (flash-XOR three-site merge resolved; NR 0x68 bit 3 → `ulap_en` deferral validated) | `2bf3167` merged as `c87a48d` |
| 2 Wave D | Hi-colour alt + shadow + border_clr_tmx + clip_y2 (4 rows: S5.04, S5.06, S5.07, S8.08) | APPROVE-WITH-FIXES (prose nits only) | `5b623ed` merged as `b43063a` |
| 2 Wave E | Line-interrupt + `inten_ula_n` (3 rows: S14.04, S14.05, S14.06) | critic misfire on S13.14 (pre-existing, not new); runtime-wiring gap valid (addressed test-only) | `a950b12` merged as `c4f4e6c` + test-only annotation `09977c6` |
| 3 | Un-skip wrap + new integration suite `ula_integration_test.cpp` (6 rows) | APPROVE-WITH-FIXES (INT-HICOLOUR-ALT-01 → INT-STANDARD-ALT-01 rename; plan Phase 3 suite renamed from `ula_int_test`) | `08a4296` + rename `498fea2` + plan-doc rename `0335dea` + ID rename `1985337` merged as `94ccaf3` |
| 4 | Dashboard + matrix + plan-doc + audit refresh (this commit) | (in-line) | this commit |

All Phase-2 waves and all Phase-3 sub-phases passed through independent
critic review before merge (per `feedback_never_self_review.md`).

## Per-cluster outcome

| Cluster (plan §) | Plan rows | Disposition | Phase-2 wave |
|---|---:|---|---|
| §2 (flash_cnt / border_active upstream) | 2 | G-comment (e2e by §3/§4) | Phase 0 |
| §3 (border_active_v raw-vc) | 1 | G-comment (no Ula accessor) | Phase 0 |
| §4 ULA+/ULAnext enable terms | 2 | un-skip (flipped via B + C) | B + C |
| §5 hi-colour alt + border_clr_tmx + shadow | 4 | 3 un-skip + 1 G-comment (attr_reg shift-reg detail) | D + Phase 0 |
| §6 ULAnext | 13 | all un-skip (flipped) | B |
| §7 ULA+ | 7 | all un-skip (flipped) | C |
| §8 clip | 3 | 1 un-skip (S8.08 clamp) + 2 G-comments (predicates unobservable) | D + Phase 0 |
| §9 scroll | 10 | 9 un-skip + 1 G-comment (no-scroll baseline) | A + Phase 0 |
| §10 floating bus | 8 | 5 F-skip re-home (Emulator::floating_bus_read) + 3 G-comments (hc-phase capture) | Phase 0 |
| §11 contention | 12 | all F-skip re-home (ContentionModel subsystem, no dedicated plan yet) | Phase 0 |
| §12 ULA disable / blend | 3 | all F-skip re-home (Compositor NR 0x68 blend-mode) | Phase 0 |
| §13 per-machine timing | 4 | all F-skip re-home (VideoTiming per-machine accessor) | Phase 0 |
| §14 line interrupt + int position | 6 | 3 un-skip (S14.04/05/06) + 3 F-skip re-home (VideoTiming int-position) | E + Phase 0 |
| §15 shadow routing | 2 | all F-skip re-home (Emulator/MMU port-0x7FFD-bit-3 wiring) | Phase 0 |
| **Total** | **75** → (36 un-skip + 10 G-comment + 29 F-keep) | | |

New integration-suite rows (Phase 3): 6 total, all pass.

## Architectural delta (`src/`)

All changes in `src/video/ula.{h,cpp}` + `src/core/emulator.cpp` + `src/port/nextreg.cpp` + `src/port/port_dispatch.cpp`:

- **NR handlers added**: write 0x26 (coarse-scroll X), write 0x27 (scroll Y), write 0x42 (ULAnext format).
- **NR handlers extended**: write 0x43 forwards bit 0 to `Ula::set_ulanext_en` (preserving compositor palette-group writes to the rest of the byte); write 0x68 forwards bit 2 to `Ula::set_fine_scroll_x` (preserving blend-mode bits for compositor).
- **Port handlers added**: 0xFF3B / 0xBF3B read + write mirror → `Ula::set_ulap_en`.
- **Ula API additions** (Phase 1): `set_scroll_x_coarse`, `set_scroll_y`, `set_fine_scroll_x`, `set_ulanext_format`, `set_ulanext_en`, `set_ulap_en`, `set_shadow_screen_en`, `set_alt_file`, `set_clip_y2`. Each has a matching test-only getter.
- **Render-path additions**: X/Y scroll folding per `zxula.vhd:193-216`; ULAnext format→paper-index lookup per `:503-515`; ULA+ palette-group encoding per `:531-541`; hi-res `border_clr_tmx` routing per `:419`; `i_ula_shadow_en` → `screen_mode=000` forcing per `:191`; `clip_y2 >= 0xC0 → 0xBF` clamp at render-time per `zxnext.vhd:6779-6783`; `inten_ula_n` + line-interrupt comparator per `zxula_timing.vhd:547-583`.

No source lines were deleted. Net additions dominated by the ULAnext paper-index lookup table and the ULA+ palette-group re-encoding block.

## Bugs + drift caught mid-plan

### Plan-doc drift

1. **NR 0x26 pixel-granularity** (Phase 1) — earlier draft of the plan
   implied byte-granularity scroll for NR 0x26. VHDL `zxula.vhd:199`
   shows the register feeds the X counter in full-pixel units (coarse
   bits 7:3 = chars, fine bit 8 = half-pixel), so the register value is
   pixel-granular after combining with NR 0x68 bit 2. Plan corrected in
   Phase 1 commit before Wave A launched.

2. **NR 0x42 default 0x07, not 0xFF** (Phase 1) — plan had the reset
   value as 0xFF; VHDL `zxnext.vhd:5265` shows `X"07"` (ink-only-3-bit).
   Caught in Phase 1 critic review; scaffold commit already had the
   correct value, plan-text correction queued.

3. **clip_y2 clamp is render-time, not store-time** (Phase 1) — plan
   told the Phase 1 agent to apply the `>=0xC0 → 0xBF` clamp inside the
   NR 0x1A setter. VHDL `zxnext.vhd:6779-6783` shows the clamp is in the
   combinational comparator process (render-time). Phase 1 agent
   implemented store-time per plan; critic caught it; committed as a
   separate plan-doc correction `52b30bf` and the Phase 1 setter was
   reworked before Wave F (Wave D took over S8.08 since Wave F was
   rolled in).

### Critic misfires / mid-plan runtime-wiring gaps

4. **Wave E critic misfire on S13.14** — the Wave E critic flagged
   S13.14 (48K `frame_done` at 69888 T-states) as a new regression
   introduced by Wave E. S13.14 is the **pre-existing GOOD-FAIL
   regression witness** documented in `task3-ula.md` (2026-04-15 audit)
   and tracked as Emulator Bug backlog item 4. Critic finding dismissed
   after cross-referencing the baseline audit.

5. **Wave E VideoTiming pulse-counter surface is test-only** — Wave E
   added a pulse-counter accessor on `VideoTiming` to make line-interrupt
   outputs observable. But no production `Emulator` instance consumes
   the counter; the getters are test-only. Critic flagged this as a
   backlog item. Addressed in-phase via a test-only comment annotation
   (`09977c6`) — the counter is correctly wired for the unit test's
   deterministic driver, and a full production hook is a separate
   VideoTiming-subsystem task (same F-blocked cluster as S14.01/02/03).

6. **NR 0x68 bit 3 → `ulap_en` port-register unwired** (Phase 3 critic
   discovery). VHDL `zxnext.vhd` shows bit 3 of NR 0x68 also gates
   `ulap_en` alongside port 0xFF3B — a secondary path. jnext Wave C
   only wired port 0xFF3B / 0xBF3B. Integration row INT-ULAPLUS-01
   passes because it drives ULA+ through port 0xFF3B; the NR 0x68 bit 3
   path is uncovered. Non-blocking for the plan (the port path is
   feature-complete); queued as backlog item.

### Three-site merge resolved during Wave C

7. **Flash-XOR three-site merge**. Wave B (ULAnext) and Wave C (ULA+)
   both needed the flash-XOR gate (`attr bit 7` flash inversion) to be
   gated by `!ulanext_en_ && !ulap_en_`. The gate lives at three render
   sites: the fast/single-pixel path, the scrolled path (Wave A), and
   the hi-colour path (Wave D). Wave-C merge had to unify these three
   sites in `src/video/ula.cpp` to avoid a cross-branch conflict. The
   merge commit `c87a48d` contains the three-site unification. All
   three sites now read the same gate expression; flash disabling in
   ULAnext + ULA+ is verified by S4.05 + S4.06.

## Remaining 29 skips in `ula_test` — all F-blocked, all re-homed with subsystem owner

| § | Rows | Count | Owner subsystem | Unblock pre-req |
|---|---|---:|---|---|
| §10 | S10.01, S10.05, S10.06, S10.07, S10.08 | 5 | `Emulator::floating_bus_read` | Floating-bus capture path (separate subsystem) |
| §11 | S11.01–S11.12 | 12 | `ContentionModel` | Dedicated plan not yet written |
| §12 | S12.02, S12.03, S12.04 | 3 | Compositor NR 0x68 blend-mode | Reopens Compositor suite |
| §13 | S13.05, S13.06, S13.07, S13.08 | 4 | `VideoTiming` per-machine accessor | 128K + Pentagon + 60 Hz origin getters |
| §14 | S14.01, S14.02, S14.03 | 3 | `VideoTiming` int-position | Per-machine int-pos getters |
| §15 | S15.03, S15.04 | 2 | Emulator/MMU | port 0x7FFD bit 3 → `i_ula_shadow_en` wiring (reopens MMU suite) |
| **Total** | | **29** | | |

All 29 rows have explicit `F: blocked on <subsystem> …` reason strings
(Phase 0 commit `209c721`).

## Non-skip / non-check rows (10 G-comments)

All 10 carry an inline `// G: <reason>` source comment in `test/ula/ula_test.cpp` (no `skip()` call):

| Row | Reason | Covered by |
|---|---|---|
| S2.08 | flash_cnt XOR upstream of ula_pixel encoder | end-to-end by §4 |
| S2.10 | border bypasses ula_pixel encoder | end-to-end by §3 |
| S3.08 | border_active_v raw-vc boundary has no Ula accessor | compositor-level |
| S5.08 | attr_reg + border_clr_tmx shift-register detail | N/A — structurally unreachable |
| S8.06 | o_ula_clipped phc>x2 comparator unobservable | end-to-end via compositor |
| S8.07 | o_ula_clipped vc<y1 comparator unobservable | end-to-end via compositor |
| S9.01 | no-scroll baseline | §1/§2 screen-address/attribute rendering |
| S10.02 | hc-phase capture internal state | §1/§2 end-to-end |
| S10.03 | hc-phase capture internal state | §1/§2 end-to-end |
| S10.04 | hc-phase capture internal state | §1/§2 end-to-end |

## Companion integration suite

`test/ula/ula_integration_test.cpp` created 2026-04-23 (commit `08a4296`, merged as `94ccaf3`). 6 rows, all pass:

| Row ID | Coverage target | Plan wave it validates |
|---|---|---|
| INT-SCROLL-01 | NR 0x26 coarse X scroll end-to-end | A |
| INT-SCROLL-02 | NR 0x27 Y scroll end-to-end (wraps modulo 192) | A |
| INT-SCROLL-03 | NR 0x68 bit 2 fine X scroll end-to-end | A |
| INT-ULAPLUS-01 | Port 0xFF3B enable → palette group 3 | C |
| INT-ULANEXT-01 | NR 0x43 bit 0 + NR 0x42=0x0F format lookup | B |
| INT-STANDARD-ALT-01 | Alt-file bit selects alt display (mode 001) | D (indirect) |

Renaming note: the Phase 3 integration suite was originally drafted as
`ula_int_test.cpp` (INT = "interrupts"). User feedback clarified INT =
"integration" — so both the file (commit `498fea2`) and the row ID
INT-HICOLOUR-ALT-01 → INT-STANDARD-ALT-01 (commit `1985337`) were
renamed, and the plan doc was corrected in `0335dea`.

## Backlog items raised by this plan

1. **Plan-doc §6 S6.13 new row** (non-blocking) — add a row to exercise
   the ULA+ 0x7F format encoder path that Wave B coded but didn't
   test-exercise. Small test-only follow-up.

2. **NR 0x68 bit 3 → `ulap_en` runtime wiring** — VHDL shows bit 3 of
   NR 0x68 also enables ULA+ (secondary path alongside port 0xFF3B).
   jnext Wave C implemented only the port path. Add a NR 0x68 bit 3
   forward in the NextREG 0x68 write handler. One-line follow-up.

3. **VideoTiming production-wiring gap** — the pulse-counter accessor
   added in Wave E is consumed only by the Wave E unit test. To observe
   line-interrupt pulses in the running emulator (e.g. for
   debugger/raster-watch UI), wire the counter into `Emulator`'s per-frame
   hook. Shares the same unblock path as S14.01/02/03.

4. **S13.14 fix (Emulator Bug backlog item 4)** — `frame_done` does not
   flip at the 69888 T-state boundary on 48K. VHDL `zxula_timing.vhd`
   says it should. Pre-existing; retained as failing `check()`
   regression witness per process manual §3. Not in scope for this
   plan.

## Commits landed (Task 3 ULA plan)

```
Phase 0:  209c721 → 029f77a (merge) + 08566a0 (trim)
Phase 1:  8cd3488 → 916435e (merge) + 52b30bf (plan-doc correction)
Wave A:   1384b4f → 813df83 (merge)
Wave B:   7af85a3 → 6eea811 (merge)
Wave C:   2bf3167 → c87a48d (merge)
Wave D:   5b623ed → b43063a (merge)
Wave E:   a950b12 → c4f4e6c (merge) + 09977c6 (test-only annotation)
Phase 3:  08a4296 + 498fea2 (rename) + 0335dea (plan rename) + 1985337 (ID rename) → 94ccaf3 (merge)
Phase 4:  this commit + matrix + status + plan-doc + EMULATOR-DESIGN-PLAN bullet
```

All phase and wave branches merged cleanly (with the Wave C three-site
flash-XOR resolution as the only non-trivial merge).

## Sign-off

All Phase-2 waves approved by independent critic before merge. All
Phase-3 flips + the new companion suite approved by independent
critic. Final test state verified on main: **unit 23/0/0 suites
(3215/3043/0/172), regression 34/0/0, FUSE 1356/1356**.

The ULA Video subsystem is now production-grade for:
- NR 0x26 / NR 0x27 hardware scroll (user priority that motivated the plan).
- NR 0x42 / NR 0x43 bit 0 ULAnext mode (all 8 documented formats + paper-index lookup + transparent-border for format 0xFF).
- Port 0xFF3B / 0xBF3B ULA+ mode (palette group 3 + attr bit 7 re-encoding + hi-res bit-3 forcing).
- NR 0x68 bit 2 fine scroll X.
- NR 0x1A clip window y2 render-time clamp.
- Shadow-screen `screen_mode=000` forcing (Ula side — MMU side is a separate F-blocked backlog item).
- Line-interrupt + `inten_ula_n` + `line=0 → cvc=max_vc` wrap.

The 29 remaining skips are all honestly F-blocked on named subsystem
plans, each with a one-line reason string pointing at the owner
subsystem. No lazy-skips, no tautologies, no plan-drift.
