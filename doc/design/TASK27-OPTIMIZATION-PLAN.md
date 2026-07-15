# Task 27 — Optimization Plan (executable, task by task)

> **Supersedes** `doc/design/PROFILING-OPTIMIZATION-PLAN.md` (whose Phase A is right, whose
> ranking is guesswork, and whose item C2 is now *wrong* — see §0.3).
> **Input:** [TASK27A-ARCHITECTURE-ASSESSMENT.md](TASK27A-ARCHITECTURE-ASSESSMENT.md) (rev 3).
> **Written to be executed autonomously**, one task per branch, in the stated order.

---

## 0. Rules of engagement

These are binding on every task below. An agent that skips one has failed the task, even if the
code works.

### 0.1 No optimization without a baseline (user, 2026-07-14)

Every optimization task **must**:

1. Take a **before** measurement on the current branch point (`make bench`, §T1).
2. Make the change.
3. Take an **after** measurement, same core, same session, same repeat count.
4. Report **both**, plus the delta, in the commit message and the review request.

A change whose measured delta is **within the noise band (< 5%)** is **not merged as an
optimization**. It may be merged as a *simplification* if it also makes the code clearer — but
it must be relabelled honestly, never reported as a speed-up.

### 0.2 The four dead hypotheses

Read TASK27A §2 before starting. In one session, four confident, well-evidenced hypotheses were
refuted by measurement: the `-O0` build; a Qt pacing bug; "Next mode is 3.4× slower"; and "only
C1/C3 explain the 48K penalty". **Two of the four were written by the assessment's own author,
in a document warning against exactly that error.** Assume your favourite hypothesis is next.

### 0.3 Known-poisoned prior art

`PROFILING-OPTIMIZATION-PLAN.md` item **C2** ("Pentagon/Next don't have contention — null the
callback") is **WRONG** and would break Next-mode contention (Tasks 50/54: NextZXOS commits +3
timing and contention *is* live). Do not implement it. Do not trust that document's ranking.

### 0.4 Benchmark hygiene (host-specific, non-negotiable)

- The dev box is **hybrid P/E** (AMD Ryzen AI 5 340): cores 0,1,3,6,7,9 = 4.9 GHz **P**; cores
  2,4,5,8,10,11 = 3.425 GHz **E**. **Pin to a P-core.** Print core id + `scaling_max_freq` in
  every report. Numbers from different core classes are not comparable (~40% swing).
- Median of ≥5, print min/max spread. **Spread > 5% ⇒ the run is void, rerun on an idle box.**
- **Never benchmark while another agent is building or testing.** Serialise.
- Primary metric is **T-states/sec**, not FPS (FPS is not comparable across guest CPU speeds).

### 0.4b Concurrency protocol (user-authorized, 2026-07-15)

Workers run **in parallel**; only performance measurements are exclusive:

1. **Heavy non-measurement ops** (builds, unit tests, FUSE, regressions) run under a **shared**
   lock: `flock -s /home/jorgegv/src/spectrum/jnext/test/bench/.lock -c "<command>"`. Any
   number may hold it concurrently. Regressions may overlap each other (user decision).
2. **Measurements** (`make bench`, perf runs) take the same lock **exclusively** (the harness
   already does) — the kernel drains all shared holders first and blocks new heavy ops for the
   duration. Additionally verify loadavg < 1.0 before measuring; the wait is bounded (20 min),
   then report instead of stalling.
3. **Pacing-row safety valve**: `audio-underrun-func` and `screenshot-paused-func` are
   real-time-pacing bounded (Task 39) and may false-FAIL under parallel load. A FAIL on ONLY
   those rows in a parallel context gets ONE serialized re-run before being treated as real.
   Any other FAIL is real immediately — no retries.
4. **Merges stay one-at-a-time**: rebase onto current main → fresh BEFORE/AFTER under the
   exclusive lock (short window) → merge. Development parallelizes; delta accounting does not.

### 0.5 Per-task workflow (every task, no exceptions)

1. Branch off current `main`: `git checkout -b task27-<id>` — **never commit to `main`.**
2. `make worktree-bootstrap` if working in a fresh worktree (`roms/*` is git-ignored).
3. Baseline measure → change → measure.
4. **Full triplet green**: `make clean && make gui-release`, then `make unit-test`,
   FUSE (`./build/test/fuse_z80_test build/test/fuse` → 1356/1356), and
   `JNEXT_TEST_JOBS=4 bash test/00regression/regression.sh 2>&1 | tee /tmp/reg.log`.
   No FAIL anywhere. SKIPs are acceptable only where already declared.
5. **Independent review** by an agent that did *not* write the change (project rule; use
   `subsystem-reviewer`). Reviewer gets its **own worktree** — never the author's.
6. Verdict binary: APPROVE or REJECT. On REJECT, fix and re-review.
7. **Merge-on-green is AUTHORISED (user, 2026-07-14 evening)** for this plan's tasks: after an
   independent APPROVE and a green triplet, the **manager** (not the worker agent) merges the
   branch to `main`. Workers still never touch `main` and never push. **Pushing to origin
   remains forbidden** — the user pushes.

---

## Phase T — Tooling (gates everything; no optimization may land first)

### T0. Build-type safety net *(independent, do first — 15 min)*

**Why:** `cmake -B build` with no `-DCMAKE_BUILD_TYPE` silently yields **`-O0`** — 5.8× slower —
and `CLAUDE.md:87` points every human *and every future agent* at that binary. This is not the
400% bug (§2), it is a trap that will corrupt someone's measurements.

- Branch `task27-t0-build-type`.
- Root `CMakeLists.txt`: `if(NOT CMAKE_BUILD_TYPE) set(CMAKE_BUILD_TYPE RelWithDebInfo) endif()`.
- Add `-fno-omit-frame-pointer` to **RelWithDebInfo only**. *(Amended after T0 review: the plan
  originally said "Release too, costs ~1%" — the T0 reviewer MEASURED it at **5.8%** on
  gui-release. Release stays clean; P1 profiles the Release binary with
  `perf record --call-graph dwarf` instead. Another estimate killed by a measurement.)*
- Update `CLAUDE.md:87` to point at `build/gui-release/jnext` for anything performance-related.
- **Acceptance:** full triplet green; `build/` binary is no longer `-O0` (check its
  `flags.make`). **`make bench` does not exist yet** (it is T1's deliverable), so measure T0
  ad-hoc, exactly as TASK27A §1 did: `taskset -c 0 <bin> --headless --machine next --sdcard
  roms/nextzxos-1gb-fat32fix.img --delayed-automatic-exit-frames 400`, median-of-5, wall-clock.
  Expect ~5× on `build/jnext`. **This is a build fix, not an optimization — label it as such and
  do not count it toward the Task 27 target.** T1 re-validates the number once the harness lands.

### T1. The benchmark harness *(the gate — nothing below runs until this is merged)*

**Why:** every later task's acceptance criterion is a measurement. The scratchpad shell script
used to produce TASK27A is not good enough to hang weeks of work on.

- Branch `task27-t1-benchmark`.
- **`--benchmark N`** in `headless_app` (supersedes `PROFILING-OPTIMIZATION-PLAN.md` A1):
  run N frames uncapped, then print one machine-parseable line **and** a human line:
  - wall seconds, emulated FPS, **T-states/sec**, T-states/frame, guest CPU speed (from NR 0x07),
  - host core id + `scaling_max_freq`, binary build type,
  - e.g. `BENCH workload=boot-nextzxos frames=400 wall=3.16 fps=126.8 tstates_per_sec=71.9M cpu=28MHz core=0@4900MHz build=Release`
- **`make bench`** target + `test/bench/bench.sh`: runs five canonical workloads —
  `boot-48k` (HALT-idle), `boot-nextzxos`, `copper-demo`, `beast`, **and `bifrost` (the busy-48K
  workload**, i.e. a 48K program that is *not* HALT-idle; `bifrost`/`nirvana` already exist as
  regression assets and are the P1 discriminator's control). Median-of-5, prints spread.
- **Core selection must be DERIVED, not hardcoded**: read `scaling_max_freq` for every core at
  runtime, pick the fastest class, print core id + frequency. Hardcoding this box's P-core ids
  would silently mis-measure on any other host or after a kernel renumbering.
- **Serialise with a real lock**, not a process scan: `make bench` acquires `flock` on
  `test/bench/.lock` and blocks. A TOCTOU "is anything running?" check is not enough when several
  Phase-C agents may run overnight — and a poisoned benchmark is the exact failure mode that
  corrupted TASK27A twice.
- Store results as `test/bench/baseline-<git-sha>.txt` so any later task can diff against the
  exact commit it branched from.
- **Interpretation caveats (T1 review, 2026-07-15):** (a) BENCH times the **frame loop only** —
  it excludes ~0.25 s of SD/ROM init, so BENCH figures sit ~6-13% above whole-process wall-clock
  numbers; **only compare BENCH against BENCH.** (b) `tstates_per_sec` samples the CPU divisor
  **at exit** — a run whose guest switches speed mid-way (NextZXOS boot) is biased toward the
  final speed. Fine for before/after deltas on the same workload (bias cancels); do NOT read the
  absolute number as the true integral of executed T-states. For `boot-48k` and `boot-nextzxos` (± rewind), `make bench` reproduces TASK27A §1.1's
  **P-core** figures within 5%. **`copper-demo`, `beast` and `bifrost` have NO P-core baseline
  yet** — TASK27A only measured the first two on an E-core. T1 *establishes* their P-core
  baseline; there is nothing to reproduce, and claiming otherwise would be fabricating a
  comparison.

---

## Phase A — The measured wins (only optimizations authorised without a profile)

### A1. Rewind buffer opt-in *(+24-31%, measured, reproduced by an independent reviewer)*

**Why:** `rewind_buffer_frames` defaults to **500** (`emulator_config.h:135`), so *every* run —
GUI, headless, screenshot regression, unit tests — eagerly memsets **1.09 GB** at startup and
takes a **2.29 MB full-machine `save_state` every frame** (`emulator.cpp:6099-6100`) for a feature
nobody switched on.

- Branch `task27-a1-rewind-optin`.

> **TRAP — read before editing.** The obvious edit (`emulator_config.h:135`, `rewind_buffer_frames
> = 500`) is **dead for every real invocation**. `src/main.cpp:160` declares its *own*
> `rewind_buffer_frames = 500` and `main.cpp:410` assigns it into `cfg` **unconditionally**,
> whether or not `--rewind-buffer-size` was passed. Changing only the config header alters
> nothing for the GUI or the CLI — every user still gets the 1.09 GB memset — while a
> flag-vs-flag `make bench` A/B would still show the +25%, so the task would *appear* to pass its
> own acceptance test. **This is a no-op commit that reports itself as a 25% win.** Found by the
> plan reviewer; it is exactly what an agent following rev 1 of this plan would have shipped.

- **Required edit sites** (all of them): `src/main.cpp:160` (the real default), `src/main.cpp:89`
  (help text), `src/core/emulator_config.h:135` (the shadow default), and
  `src/debugger/debugger_window.cpp:314` ("(default 500)" string).
- **Allocation stays boot-time only. Do NOT attempt lazy allocation.** The plan reviewer showed
  `DebuggerManager::set_enabled()` has no relationship to rewind, and that a deliberate UX already
  exists (`debugger_window.cpp:303-318`): if `rewind_buffer() == nullptr`, the "Enable Rewind"
  action pops a dialog telling the user to **restart with `--rewind-buffer-size N`**. With the
  default at 0 that dialog becomes the *normal* path, which is coherent and needs no new API.
  A runtime `Emulator::enable_rewind(int)` API does not exist and inventing one is out of scope.
- Still worth doing: swap `vector::resize(…, 0)` for the `Profiler`'s `mmap(MAP_ANONYMOUS)`
  strategy (`profiler.cpp:17-35`) so that *when* rewind is requested, the 1.09 GB is lazily
  faulted rather than eagerly memset.
- Update `USAGE.md`: rewind is now opt-in via `--rewind-buffer-size N`.
- **User-confirmed direction (2026-07-14 evening):** default OFF, activated by the CLI option —
  exactly this task — *plus* a debugger-window toggle. The toggle exists today
  (`debugger_window.cpp:303-318`) but demands a restart when no buffer was allocated at boot.
  Making it a **live** toggle needs a new `Emulator::enable_rewind(int frames)` API and is split
  out as **A1b** (own branch, after A1): allocate the buffer at toggle time (mmap strategy, so
  the GB is faulted lazily), enable snapshots from the next frame, and rewrite the restart
  dialog. A1 must not block on A1b.
- **Watch for:** `rewind_test` (10 declared SKIPs, Task 13a) **and `test/contention/contention_test.cpp`**,
  the only other test that builds an `EmulatorConfig` without pinning `rewind_buffer_frames`
  (benign — memory only — but it *is* in the enumeration). Every other test already pins it to 0.
- **Acceptance:**
  1. Triplet green.
  2. **Run the binary with NO rewind flags at all** and assert **no `RewindBuffer` is
     constructed** — grep the startup log for the absence of the "Rewind buffer: N frames × …"
     line, and check RSS via `/usr/bin/time -v` drops by ~1.09 GB. **A flag-vs-flag bench delta
     is NOT sufficient acceptance** (see the trap above).
  3. `make bench` shows **≥ +20%** on `boot-nextzxos` *in the default configuration*.

### A2. Decouple the trace log from rewind *(part of A1's win; separate commit so it is separately measurable)*

**Why:** `emulator.cpp:5712` calls `trace_log_.set_enabled(true)` **inside** the rewind-enable
block. So rewind's default silently switches on a **per-instruction** recorder that does ~6
`Mmu::read`s + a `std::function` construction into a 10,000-entry ring that is overwritten and
never read unless someone steps back.

- Branch `task27-a2-trace-decouple` (**after** A1, so its delta is measured against A1's baseline).
- **Re-locate the target before editing.** `emulator.cpp:5712` sits inside the
  `if (cfg.rewind_buffer_frames > 0)` block that A1 rewrites; A1 may have moved it. Do not trust
  this line number across A1 — find `trace_log_.set_enabled` afresh.
- Give the trace log its own flag (`--trace` / debugger-controlled). Rewind must not imply it.
- Make `z80_instruction_length()` take a template/raw callable instead of
  `std::function<uint8_t(uint16_t)>` (`trace.h:21`).
- `step_back()` (`emulator.cpp:8518`) is the only consumer: if the trace is off, it must fail
  **loudly and cleanly**, not silently misbehave.
- **Acceptance:** triplet green; measured delta reported (may be small once A1 has landed — if it
  is < 5%, say so and land it as a *simplification*, per §0.1).

---

## Phase P — Profile (adjudicates everything in Phase C)

### P1. The profile, and the three-way discriminator

**Why:** TASK27A §1.3 leaves a genuine three-way tie for the 48K per-T-state penalty, and the
1.58× Next-mode shortfall has **no attributed cause at all**. Everything in Phase C is a guess
until this task finishes. This is the single highest-value task in the plan.

- Branch `task27-p1-profile` (produces a **document + data**, no emulator code change).
- `perf record --call-graph dwarf` on the **Release** binary (frame pointers were kept off
  Release — T0 review measured them at 5.8%), all canonical workloads, P-core.
  Produce flamegraphs + a flat `perf report` per workload; commit them under `doc/perf/`.
- **Run the discriminator experiment.** The 48K penalty has three candidate causes that the
  aggregate ratio cannot separate:
  - **C1** — O(master_cycles) CTC/UART tick loops (8× more master cycles per T-state at 3.5 MHz),
  - **C3** — contention (disabled ≠ 3.5 MHz, so Next skips it entirely),
  - **C11** — HALT dispatch density (HALT is never fast-forwarded; an idle BASIC prompt
    re-dispatches a 4-T HALT ~17,500×/frame at worst-case density).

  **The experiment:** benchmark a **busy** 48K workload (a demo, not the idle prompt) against the
  **idle** one. If the per-T-state penalty largely vanishes → the cause is **C11/dispatch
  density**, not CPU-speed scaling. Then, independently: force a Next machine to 3.5 MHz
  (NR 0x07 = 0) running the *same* code, and compare against the same code at 28 MHz — that
  isolates C1/C3 from workload composition entirely.
- **Deliverable:** `doc/design/TASK27-PROFILE-REPORT.md` — where the 71.9M T-states/s actually go,
  with a **ranked, evidence-backed** list of what to fix, and an explicit statement of which
  Phase-C candidates the profile **killed**. Expect casualties: on the evidence so far, *at least
  one* of C1/C3/C11 is not the story.
- **Acceptance:** independent review of the *reasoning*, not just the numbers. The reviewer's job
  is to find the confound — as it did twice on TASK27A.

---

## Phase C — Profile-guided optimizations

> **P1 HAS LANDED (2026-07-15, merge fa26847d, two review rounds).** This section is now
> profile-ranked, per doc/design/TASK27-PROFILE-REPORT.md. Measured shares are of the
> boot-nextzxos frame unless stated.
>
> **EXECUTION ORDER (profile-decided):**
> 1. **C-IM2** — `Im2Controller::tick` per-instruction `step_pulse`+`step_devices`: **14.1%
>    measured**, the largest single subsystem on the target workload. (New — found by the
>    profile, absent from every pre-P1 list.) Early-out when no device is armed; VHDL fidelity
>    of the pulse fabric must be preserved (also mind Task 60d's open question on tick units).
> 2. **C3 + C-DIV merged** — the contention/raster-math chain (`derive_hc_vc` +
>    `to_ula_counters` + `get_page` before a gate that returns 0): **8.2% measured** at 28 MHz
>    as pure dead work. One task, same code path.
> 3. **C1** — CTC+UART O(master_cycles) loops: **7.0% measured** on boot-nextzxos (27-32% at
>    3.5 MHz). Accumulator pattern per `md6_connector_x2.cpp:245`.
> 4. **C10 + glue** — wrapper sync 5.95%, per-instruction `Z80Registers` copies, getenv guard,
>    dead `on_contention` store.
> 5. **C-M1** — the DivMMC-automap/Multiface/NmiSource per-M1-fetch chain: **7.7% measured**.
>    (New — found by the profile.)
> 6. **C6** (render skip at turbo), **B1** (LTO), **C9** (Copper div/mod — dominant on
>    copper-demo, which sits at 46M).
>
> **KILLED by the profile (deleted, with evidence):** C11 (density mechanism real, ~51 ns/dispatch,
> but bounded at ≤24% of the 48K gap and ~0% for real workloads — boot-48k is NOT HALT-bound,
> 8.36 T/dispatch); C4 (port dispatch peaks 0.84%); C7/C8 (sub-relevant on the DoD workload;
> 48K/beast-only effects). C5 stays parked-low.
>
> **DoD reality check (P1 §6):** the pre-P1 list alone projects ~90-95M — NOT enough. With
> C-IM2 + C-M1 the realistic landing zone is **95-105M** vs the 113.5M target. boot-nextzxos is
> also the EASIEST Next workload (copper-demo/beast sit at 43-46M and need 2.5×+; beast runs a
> 264-line 60 Hz frame — NR 0x05=0x04 — so its fps target is 240, not 200). If after the list
> is exhausted the target is unmet, §"Definition of done"'s escape hatch applies — honestly.
>
> Each item: own branch, before/after `make bench`, full triplet, independent review, manager
> merges. Serialisation rule below still applies.

**Serialise the branches that touch the same hot loop.** C5 (`:6911-6925`), C6 (`:6965-6972`),
C9 (`:7815-7837`) and C10 all edit `emulator.cpp`'s inner loop. Their measured deltas are taken
against a **common ancestor**, so they are **not provably additive** and two of them may conflict
textually or semantically. Rule: land them **one at a time**; after each merge, the next branch
**rebases and re-measures its before/after against the new `main`**. A delta measured against a
stale ancestor is not evidence.

| id | candidate | site | risk |
|---|---|---|---|
| C1 | Kill O(master_cycles) CTC/UART tick loops; use the accumulator pattern **already in this repo** at `md6_connector_x2.cpp:245` | `ctc.cpp:228`, `uart.cpp:89` | Low |
| C3 | Hoist the contention early-out **above** the raster math (currently the gate is inside the callee, so every bus cycle computes ~5 divisions to learn the answer is 0). **NOT the same as the poisoned `PROFILING-OPTIMIZATION-PLAN.md` C2** — that one nulls the callback per *static machine type*, which is wrong because NR 0x07 is switchable at runtime. This one keeps the same live gate, just evaluates it earlier. | `contention.cpp:246-248` + 7 call sites | Very low |
| C11 | Fast-forward HALT to the next scheduled interrupt | `fuse_z80_core.c:188,198` | **Med — interrupt timing must stay exact** |
| C6 | Skip `render_frame()` for frames the frontend will never display (at 400% we composite 200 frames/s while the GUI shows ~60). **MUST guard screenshot / video-recorder / debugger paths**: `emulator.cpp:6969-6972` feeds `render_frame()`'s output straight into `video_recorder_.capture_frame()`, so a naive skip **silently corrupts `--record` MP4s** — and because the regression suite runs headless/uncapped, a wall-clock-gated skip **may never trigger during testing**, letting the defect ship. **Acceptance must include `--record` and `--delayed-screenshot-frames` at a non-100% speed.** | `emulator.cpp:6965-6972` | Low *only if* guarded |
| C-DIV | Kill the ~5 runtime integer divisions per bus cycle (`derive_hc_vc`). *(Labelled C-DIV, not "C2", to avoid colliding with the poisoned prior-art item of that name — see §0.3.)* | `z80_cpu.cpp:98-114` | **Med — most correctness-sensitive code in the emulator; FUSE + Nirvana/BIFROST are the gate** |
| C4 | Port dispatch: cache the popcount at registration; parallel key array (5 cache lines, not 60) | `port_dispatch.cpp:16-20,45,87` | ~0 |
| C7 | Tilemap: hoist per-pixel work to per-tile (~490k `vram_read`/frame → ~30k) | `tilemap.cpp:429-512` | Med |
| C8 | Compositor: hoist the per-pixel `switch(layer_priority_)`; skip clears for disabled layers | `renderer.cpp:627,789,290-296` | Low |
| C5 | Audio: don't generate PSG/mixer output that is then discarded at speed ≠ 1× | `emulator.cpp:6911-6925` | Med |
| C9 | Copper: strength-reduce two 64-bit divisions **per master cycle** | `emulator.cpp:7815-7837` | Med |
| C10 | Per-instruction odds and ends (2 `Z80Registers` by-value copies, a `std::getenv` static guard, dead `on_contention` store, test-only `video_timing_.advance()`) | §4.2 of TASK27A | ~0 |
| B1 | LTO/IPO (nothing cross-library inlines today; every CPU→MMU access is an un-devirtualisable vtable call) | root `CMakeLists.txt` | Low-med |

---

## Phase R — Refactor (27b)

User's stated preference (design plan, 2026-07-14): **subsystem by subsystem, then globally.**
Runs *after* Phase C so the profile informs what is worth restructuring — **except R1**, which
comes first because it makes every other diff reviewable.

| id | change | why now |
|---|---|---|
| **R1** | **Split `emulator.cpp` (8,690 lines; `init()` alone is 5,690)** into `_nextreg` / `_ports` / `_state` / `_media` / `_run`. Pure code motion. | Highest value/risk in the whole report. Do it **first** — every later diff becomes reviewable. |
| R4 | Real `Peripheral` lifecycle (`reset`/`tick`/`save_state`) → the 130-line tick cluster becomes a loop | This is what makes **Task 60a** (single-step drops interrupts) structurally impossible to reintroduce |
| R2 | Unify the nine per-scanline replay mechanisms behind one `PerLineLog<T>` | |
| R3 | Unify the three frontend loops (already diverged in user-visible ways) | |
| R5 | Delete dead code (NR 0x15 change log — zero production callers; `Ula::render_frame`) | |
| R8 | Restore the core/SDL boundary (`emulator.h` → `keyboard.h` → `<SDL2/SDL.h>`, violating the design plan's own §3 rule) | |
| R7 | Relocate long-form archaeology to `doc/`, leaving VHDL citations | |

## Phase M — Magic numbers (27c)

**Last.** `emulator.cpp` has ~2,411 hex literals (1 per 3.6 lines) against 256 `constexpr` in the
whole tree. It is a large mechanical diff that **will collide with every other Task-27 branch**,
so it goes after them. Nearly every literal already carries a VHDL citation in the comment above
it — this is transcription into named constants, not archaeology. Do it **per-register**, not in
one sweep. Model: `src/video/timing.h:36-53`.

---

## Task 60 — the bugs (independent of everything above)

These are **not** optimizations and must not be bundled into an optimization commit. They can run
in parallel with Phase T/A. See `.prompts/2026-07-14.md`.

- **60a** debugger single-stepping drops interrupts (`execute_single_instruction()` never ticks
  IM2) — *fixed structurally by R4; a point-fix now is also fine*
- **60b** `Saveable` is a dead interface; `StateWriter`/`StateReader` never check `capacity_`
- **60c** all of `src/input/` is absent from snapshots (MD6's live FSM vanishes on rewind)
- **60d** IM2 pulse counter runs 8× fast at 3.5 MHz (comment says the caller was fixed; git says
  it never was)

---

## Execution order (autonomous)

```
T0 (build type)  ─┐
                  ├─→ T1 (harness) ──→ A1 (rewind) ──→ A2 (trace) ──→ P1 (profile+discriminator)
Task 60a-d ───────┘                                                        │
                                                                           ▼
                                              Phase C, re-ranked BY THE PROFILE, one branch each
                                                                           │
                                                                           ▼
                                                        R1 → R4 → R2/R3/R5/R8/R7 → Phase M
```

**Hard gates:**
- No Phase-A commit before **T1** is merged.
- No Phase-C commit before **P1** is merged.
- Every task stops at **review-passed**; the user merges. (Change this line if merge-on-green is
  ever authorised.)

## Definition of done for Task 27

`make bench` on a P-core shows **≥ 113.5M T-states/s** on `boot-nextzxos` — i.e. 200 FPS at 400%
in Next mode — with the full triplet green and no screenshot regression.

**If the profile says that is not reachable**, the honest outcome is to say so, publish the
number we *can* reach, and amend the design plan's 400% claim — not to keep optimizing until the
benchmark is gamed.

**The escape hatch has an evidentiary floor.** It may be invoked **only after every candidate
that P1 ranks High or Med-high has a landed, measured branch** — not a predicted one. "We did C1,
saw diminishing returns, and declared it unreachable" is not an acceptable close: it is the
goalpost-moving this rule exists to prevent. If a High-ranked candidate is abandoned, the reason
must be a *measurement* or a *correctness* argument, recorded in the profile report.
