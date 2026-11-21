# Test-cycle speed plan (draft for iteration)

Status: DRAFT, 2026-11-21. Not started; scheduled after the coming patch release and before
epics #224 / #486. Owner decisions are marked **Q<n>** below; nothing is built until they are answered.

## 1. Where the time goes today (measured)

Batch gate, step by step (nas01, gate-v1.3.40, 2026-11-21; local is ~30% faster, ~71 min total):

| step | s | note |
|---|---|---|
| gui-release (PGO) | 290 | |
| unit-test | 831 | includes ~15 lint/selftest prerequisites, run one after another |
| unit-test-sdl | 466 | same core suites as unit-test, other configuration |
| unit-test-win | 438 | same suites, MinGW under wine |
| FUSE | 0 | |
| regression | 1305 | rows already parallel; `quiet` and `serial` phases are not |
| regression-win | 767 | |
| wasm-core | 90 | |
| wasm-browser-test | 1462 | **the 17 Playwright specs run one after another** (`browser-smoke.sh` `for spec`) |
| unit-test-wasm | 434 | |
| regression-wasm | 111 | |
| **total** | **6194** (~103 min) | **every step runs after the previous one, on one host** |

Slowest unit suites (they run in parallel, so the slowest one bounds the step):
`minimal_card_test` 287 s, `sdcard_file_add_test` 150, `tap_loader_test` 101, `rewind_test` 91,
six `halt_batch_corpus_*` ~55 each, `cli_options_native_test` 56, `sdcard_provisioner_test` 45.

Branch side: every review round costs one `make unit-test` (author) plus the reviewer's targeted
runs, all through two local slots at nice 19, so agents queue behind each other.

Not yet measured: per-row regression times, per-spec browser times, the prerequisite chain of
`make unit-test`, build vs run share of each step. **Phase 0 measures these first.**

## 2. Levers, biggest expected win first

### A. Run the gate's steps in parallel, across both hosts
The gate is ten independent steps run in series. Split it into two lanes that run at the same
time, e.g. native + Windows on one host, the WebAssembly trio on the other. Expected: gate time
≈ the longer lane instead of the sum (~103 → ~55 min on today's numbers, less with B and C).
Each lane still must be fail=0 skip=0; the stamp logic stays.

### B. Parallelise the browser specs
`wasm-browser-test` is the single longest step and runs its 17 specs serially. Run them with N
workers (each spec has its own server port / page). Expected: 1462 s → ~300-400 s.

### C. Shorten the long tail of slow suites
`minimal_card_test` alone bounds unit-test's run phase. Candidates: cache expensive fixtures
built from the SD image (keyed on the image hash), split big suites into shards that run in
parallel (the halt_batch corpus is already sharded; minimal_card and sdcard_file_add are not),
replace wall-clock waits with frame-counted waits. Every change keeps the rows, IDs and counts.

### D. Selective testing on branches ("test impact")
A table mapping source directories to the suites and regression rows that exercise them
(the manifests already know suites; this adds the source → suite edge). `make test-affected`
runs exactly what the diff touches plus a small always-run smoke set. Branch and review rounds
use it; the full `make unit-test` moves to once per landing (or only the batch gate, **Q1**).
Risk: a missing edge lets a break through to the batch gate, where it costs a fix-forward.
Mitigation: the gate still runs everything; a miss is fed back into the table.

### E. Platform runs only where the platform can differ
`unit-test-sdl`, `-win`, `-wasm` re-run ~150 core suites whose code is the same everywhere.
On branches (already the rule, not enforced) run them only when the diff touches platform,
frontend, `_WIN32`/`__EMSCRIPTEN__` code or the harness; make that check mechanical.

### F. Let GitHub CI carry the batch gate (**Q2**)
CI already re-runs the full gate on every push to `main`, on 15 parallel GitHub jobs, in
~45-65 min, including macOS which no local gate covers. Option: the batch gate becomes "CI
green on the pushed `gate-v<version>` branch" instead of a local/nas01 run, freeing both hosts.
Cost: a red result arrives in ~1 h on GitHub instead of locally; free for a public repo.

### G. Automatic overflow to nas01
Now that agents may push their own branch: `slot.sh` itself sends a heavy run to nas01 when both
local slots are busy and nas01's lane is free (push the branch, run, return the verdict), so no
agent has to remember the load rule.

### H. More workers (**Q4**)
- A second remote host, if one exists (same skill, two lanes).
- The parked "private CI runner" idea (self-hosted GitHub runner): CI capacity on owner hardware.
- More local slots when the desktop is not in use (nights, unattended runs) (**Q3**).

### I. Smaller things
- `make unit-test` prerequisites (~15 lints and selftests) run serially: run them in parallel.
- Batch regression more: the full regression already runs once per batch; keep it so, and make
  sure no agent ever runs a full regression on a branch (rule exists; check the briefs).
- PGO build in every gate (290 s): skip retraining when nothing relevant changed (it already
  skips training; check what the 290 s is).

## 3. Phases

0. **Measure** (1 short issue): per-step build vs run split, per-row regression times, per-spec
   browser times, the unit-test prerequisite chain. Output: a table in this doc.
1. **Quick wins**: B (browser specs parallel), A (two-lane gate), I (parallel prerequisites).
2. **Branch cycle**: D (test-affected) + E (platform check), G (slot overflow).
3. **Slow suites**: C, one suite per issue, each with its own before/after measurement.
4. **Capacity**: F and H, as the owner decides.

Each phase is a normal issue: plan, implement, independent review; a speed change never
removes a row, a SKIP never becomes a pass, and every claimed saving is measured, not estimated.

## 4. Targets (**Q5**)

Proposed: batch gate ≤ 40 min wall; a branch review round ≤ 10 min of test time for a
typical one-subsystem change.

## 5. Open questions

- **Q1** Branch rounds: affected tests only, with the full `make unit-test` once before landing,
  or only in the batch gate?
- **Q2** May a green CI run on the pushed gate branch replace the local/nas01 batch gate?
- **Q3** May the local slot cap rise (e.g. to 4) during unattended runs or chosen hours?
- **Q4** Any other machine available as a worker? Revive the private-CI-runner idea?
- **Q5** Targets as proposed, or different?
- **Q6** Is a small always-run smoke set on every branch acceptable as the safety net for D?
- **Q7** Priority: is this work allowed ahead of the epics, as one short milestone of its own?

## 6. Owner decisions (2026-11-21, round 1)

- **Q1 → affected only, full at land.** Review rounds run the affected suites and rows plus a
  smoke set; the full `make unit-test` runs once, just before a branch lands. The gate still runs
  everything.
- **Q2 → CI is the batch gate.** Push `gate-v<version>`, a green CI run on it is the gate, then
  push `main`. Lever A (two-lane local gate) drops to a fallback; lever B still pays, inside CI.
- **Q3 → 4 local slots during unattended runs** (still nice 19), 2 when the owner is at the machine.
- **Q4 → automatic nas01 overflow** (lever G). No private runner or second host for now.

## 7. Owner decisions (2026-11-21, round 2)

- **Q5 → targets:** CI wall time ≤ 45 min; a typical one-subsystem branch round ≤ 10 min of test time.
- **Q6 → smoke set:** the fast core suites (CPU, memory, ULA, NextREG), FUSE, docs-check,
  cli-check and one boot screenshot row (~2 min), on top of the affected tests.
- **Q7 → schedule:** a short milestone of its own, right after the coming patch release and
  before the epics: phases 0, 1 and 2 first, then phase 3.
- **Q8 → the full `make unit-test` at landing** runs on the host picked by load (here if free,
  else nas01); the CI gate confirms everything once per batch.

## 8. Resulting work list (to file as issues in the new milestone)

1. Measure: per-step build/run split, per-row regression and per-spec browser times, the unit-test
   prerequisite chain (phase 0).
2. Browser specs in parallel inside `wasm-browser-test` (lever B).
3. `make unit-test` prerequisites in parallel (lever I).
4. CI as the batch gate: CLAUDE.md merge protocol, the gate-branch push and wait, the stamp
   (`make regression-ci-check` already exists for the release path), the autonomous-run skill.
5. `make test-affected` + the smoke set + mechanical platform check (levers D, E).
6. `slot.sh` automatic overflow to nas01 (lever G) and the 4-slot unattended cap (Q3).
7. Slow suites, one issue each, starting with `minimal_card_test` (lever C).
