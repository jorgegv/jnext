# Task 8 — Nirvana G12-MUX-01..03 — plan

Date: 2026-07-13
Owner (sub-manager): this agent-team-manager instance
Branch/worktree: `task8-nirvana` @ `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task8-nirvana`, off main @ 22a6c156

## Scope
`test/mmu/mmu_test.cpp:1951-1953` — three skipped rows only (G12-MUX-01..03).
Out of scope: the other 9 mmu_test skips, any other subsystem, `.prompts/*.md`.

## Manager pre-research (verified against code, not just the parent's sketch)

1. `Emulator::current_hc()`/`current_scanline()` derive position from
   `clock_.get() - frame_cycle_`, which only advances **between**
   instructions (`clock_.tick()` is called after `cpu_.execute()` returns) —
   so those two APIs give *instruction-boundary* resolution, not per-byte.
2. However, `src/cpu/z80_cpu.cpp`'s `fuse_z80_writebyte()` /
   `fuse_z80_readbyte()` already call `derive_hc_vc(tstates)` **before**
   every single memory access, using FUSE's own running `tstates` counter
   (which DOES advance sub-instruction, byte by byte) — see
   `z80_cpu.cpp:81-86` (`derive_hc_vc`) and `:145-158` (`fuse_z80_writebyte`).
   This is the actual sub-instruction-accurate beam-position source, not
   `current_hc()`. `cpu_.on_contention` itself is wired to `nullptr`
   (`emulator.cpp:706`) — contention flows through direct calls to
   `ContentionModel::contention_tick()` in `z80_cpu.cpp`, not through that
   callback. A write-observer hook should therefore sit at/near
   `fuse_z80_writebyte()` (or wherever `Ram::write` is ultimately reached
   from the CPU data-write path), reusing `derive_hc_vc(tstates)`, not
   invent new timing infrastructure.
3. `Ram` (`src/memory/ram.h`) currently has zero hook surface — flat
   `std::vector<uint8_t>`, `read()`/`write()` only. `mmu_test.cpp`'s
   `Fixture` is `Ram + Rom + Mmu` only — no `Ula`/`Renderer`/`Emulator` in
   scope for this test file, which bounds what G12-MUX-03 can assert
   without reaching into video code.
4. Existing per-scanline replay pattern (to reuse, not reinvent):
   `PaletteManager::{start_frame,set_current_line,rewind_to_baseline,
   apply_changes_for_line}` + `change_log_` (`src/video/palette.h/.cpp`),
   cloned by `Layer2` (scroll/clip/bank/enable/nr70 change logs) and
   `Ula`/`Renderer` (`port_ff_change_log_`, `nr15_change_log_`,
   `scroll_change_log_`, `palsel43_/palsel6b_change_log_`). All are fixed
   `std::array<Change, MAX_CHANGES_PER_FRAME>` logs tagged with a line
   number, replayed row-by-row in the compositor loop.
5. `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Category B explicitly scopes
   this as "per-line", and separately flags true per-**column**
   (T-state-accurate mid-scanline) fidelity as **out of scope** for that
   audit, needing "a future architectural change". The parent's design
   sketch (and finding 2 above) asks for beam-**position**-based (i.e.
   sub-line / column) placement, which is a step beyond every existing
   log-and-replay clone. The worker must reconcile this explicitly: either
   justify why column-accuracy is achievable cheaply given finding 2, or
   explicitly land at per-line granularity and say why, and flag the
   gap. Either is an acceptable outcome as long as it's stated plainly and
   VHDL-cited (`zxula.vhd`, attribute-fetch timing within a row).

## Unit of work (single, not parallelizable — one architectural feature threading Ram → Mmu → CPU write path → Ula/Renderer → RewindBuffer → debugger video_panel)

Dispatch ONE author worker to do the full vertical slice in the
`task8-nirvana` worktree (already checked out, do not create a further
sub-worktree for the author — only the reviewer gets its own detached
worktree per `feedback_reviewer_own_worktree`).

Deliverables (see full brief given to the worker):
1. VHDL citation for attribute-fetch timing (`zxula.vhd`) confirming
   *when within a row* an attribute byte is latched for each 8-pixel
   cell — this determines what "correct" replay means.
2. `Ram::set_write_observer` (or equivalent, following evidence) with a
   callback signature carrying enough info to place a write in
   (line, sub-line-position) space — matching or improving on the
   `(addr, val, m1, t)` sketch in the skip comment.
3. Wiring from the CPU write path (finding 2) into that observer —
   reuse `derive_hc_vc`, do not build new timing infra.
4. ULA-side mid-row mux consumer, following the Palette/Layer2/Ula
   change-log + replay pattern, feeding the compositor's ULA attribute
   read.
5. `RewindBuffer`/`Saveable` integration — new per-line/per-write state
   must serialise or rewind silently diverges (Emulator::save_state/
   load_state; see how Palette's change_log_ already does this as the
   template).
6. Debugger `video_panel.cpp` replay-loop integration (its `replay_rewind()`/
   `replay_line()` already replay per-row using these logs).
7. G12-MUX-01/02/03 un-skipped with real, VHDL-cited assertions +
   mutation evidence (revert → FAIL, restore → PASS) for each.
8. A z88dk demo that races the beam and rewrites ULA attributes mid-row
   (z88dk at `~/src/spectrum/z88dk`, needs PATH+ZCCCFG), run headless,
   screenshotted before/after the fix, visually inspected.
9. Performance measurement (headless, fixed frame count, release build,
   `JNEXT_TEST_JOBS=4`) of the write-observer hot-path cost.
10. Full triplet clean: unit (manifest-pinned counts respected,
    `test/lint-assertions.sh` new:0), FUSE 1356/1356, regression
    (`JNEXT_TEST_JOBS=4`), harness-selftest.

## Review
Independent `subsystem-reviewer` in its own **detached** worktree off
`task8-nirvana` (never the author's worktree). Verdict APPROVE /
APPROVE-WITH-NITS / REJECT. On REJECT, findings go back to the same
author worker (still on `task8-nirvana`) for revision.

## Merge
Not performed by this sub-manager unless/until told to merge to main —
this branch is the deliverable; the parent agent owns final integration
into main (TBD — will ask if unclear at report time). Default assumption:
report findings, keep branch ready, do not merge to main without explicit
authorisation (out of caution — the parent brief's "report back" framing
suggests review, not silent merge).

## Escalation triggers (stop and report to parent immediately)
- Finding 2 (per-write beam position) turns out unusable in practice.
- Perf cost > ~2%.
- VHDL contradicts the sketch in a way that changes feature shape.
- A regression reference screenshot moves.
- Any row is genuinely WONT.
