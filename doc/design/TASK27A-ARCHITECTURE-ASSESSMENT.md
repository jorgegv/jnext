# Task 27a — Architecture Assessment

> **Status:** rev 2 (2026-07-14), after two independent reviews. Rev 1 was **REJECTED** by the
> methodology reviewer for a confounded headline claim; §1/§2 are rewritten accordingly, and
> the errors found by the claims reviewer are corrected inline. **No code was changed.**
> This document is an input to Task 27b/27c/27d — it authorises nothing on its own.

---

## Table of Contents

- [0. The one-paragraph version](#0-the-one-paragraph-version)
- [1. Measured baseline](#1-measured-baseline)
  - [1.1 Raw FPS](#11-raw-fps)
  - [1.2 FPS is NOT comparable across workloads — use T-states/sec](#12-fps-is-not-comparable-across-workloads--use-t-statessec)
  - [1.3 What the numbers establish](#13-what-the-numbers-establish)
- [2. Two hypotheses that measurement killed](#2-two-hypotheses-that-measurement-killed)
- [3. Measurement rule and benchmark harness](#3-measurement-rule-and-benchmark-harness)
- [4. Architecture as it stands](#4-architecture-as-it-stands)
- [5. Candidate optimizations](#5-candidate-optimizations)
- [6. Comprehensibility / refactor candidates](#6-comprehensibility--refactor-candidates)
- [7. Bugs found (not optimization — Task 60)](#7-bugs-found-not-optimization--task-60)
- [8. Proposed sequencing for 27b / 27c / 27d](#8-proposed-sequencing-for-27b--27c--27d)
- [9. What was NOT assessed](#9-what-was-not-assessed)

---

## 0. The one-paragraph version

**The emulator is not slower in Next mode — it is doing 8× more work per frame, because
NextZXOS runs the guest CPU at 28 MHz** (verified: NR 0x07 = 0x03). Measured in guest work per
host second, Next mode is the *fastest* configuration we have (71.9M T-states/s) and 48K at
3.5 MHz is the *slowest* (23.0M T-states/s) — the opposite of rev 1's claim. The real target:
**200 FPS at 400% in Next mode needs 113.5M T-states/s; we sustain 71.9M with rewind off. A
1.6× shortfall is what Task 27 must close.** Only one optimization is measured today — **the
rewind buffer costs 24-31% of every frame** for a feature nobody switches on. Everything else
in §5 is a structural hypothesis that has not been profiled. **Four** confident hypotheses have
already been killed by measurement this session (the `-O0` build; a pacing bug; rev 1's own
"Next mode is 3.4× slower"; and rev 2's "only C1/C3 explain the 48K penalty") — which is the
entire argument for the no-baseline-no-optimization rule.

---

## 1. Measured baseline

Method: headless, uncapped, fixed emulated-frame count, `taskset`-pinned, 5 repeats, **median
reported with min/max spread**. Binary: `build/gui-release/jnext` (`-O3`) unless stated. Machine
idle.

> ### The benchmark host is HYBRID. Disclose the core class or the number is meaningless.
>
> Dev box: **AMD Ryzen AI 5 340**. Cores 0,1,3,6,7,9 are **P-cores (4.9 GHz)**; cores 2,4,5,8,10,11
> are **E-cores (3.425 GHz)** — a 1.43× hardware ratio, confirmed by `scaling_max_freq`. The same
> binary and workload gives **126.8 FPS on core 0 and 91.5 FPS on core 2** (measured, both
> median-of-5).
>
> **Rev 1 and rev 2 of this document reported E-core numbers without knowing it** — found by the
> methodology reviewer, not by me. Every figure below is now labelled. The headline shortfall
> moves from 2.19× (E-core) to **1.58× (P-core)**: a ~40% swing in "the number Task 27 must
> close", caused entirely by which core `taskset` happened to be given.

### 1.1 Raw FPS

**P-core (core 0) — the reference figures. Use these.**

| workload | binary | median | spread | FPS |
|---|---|---|---|---|
| boot-48k (600 fr) | `-O3` | 1.82 s | 1.81-1.83 | **329.3** |
| **boot-nextzxos (400 fr)** | `-O3` | 4.20 s | 4.16-4.34 | **95.2** |
| boot-nextzxos, `--rewind-buffer-size 0` | `-O3` | 3.16 s | 3.10-3.28 | **126.8** |

**E-core (core 2) — kept only because the `-O0` and demo rows were measured there.**

| workload | binary | median | spread | FPS |
|---|---|---|---|---|
| boot-48k (600 fr) | `-O0` `build/jnext` | 14.76 s | — | **40.6** |
| boot-48k (600 fr) | `-O3` | 2.54 s | 2.53-2.71 | **236.3** |
| boot-48k, `--rewind-buffer-size 0` | `-O3` | 2.03 s | — | **295.2** |
| boot-nextzxos (100 fr) | `-O0` `build/jnext` | 6.70 s | — | **14.9** |
| boot-nextzxos (400 fr) | `-O3` | 5.73 s | 5.71-5.79 | **69.8** |
| boot-nextzxos, rewind 0 | `-O3` | 4.37 s | 4.35-4.56 | **91.5** |
| copper-demo (400 fr) | `-O3` | — | — | 47.4 |
| beast/layers (400 fr) | `-O3` | — | — | 52.5 |

The `-O0`/`-O3` ratio (5.8×) and the rewind delta (24-31%) are **A/B pairs on the same core**, so
they are unaffected by the P/E mix-up. Only the *absolute* figures moved.

### 1.2 FPS is NOT comparable across workloads — use T-states/sec

**This is the correction that rev 1 got wrong.** A frame is a fixed budget of 28 MHz *master
cycles* (`emulator.cpp:6347`, `:6064` — derived from video geometry, independent of CPU speed).
Since `master_cycles = tstates × cpu_divisor` (`emulator.cpp:6595`), **the number of Z80
instructions inside one emulated frame is inversely proportional to the CPU divisor.** NextZXOS
commits NR 0x07 = 0x03 (28 MHz, divisor 1) during boot — *verified by live log_ — so it executes
**8× more T-states per frame** than a 3.5 MHz 48K machine. Comparing their FPS compares two
different quantities.

The metric that isolates *emulator cost* is **guest T-states executed per host second**
(**P-core figures**):

| workload | FPS | T-states/frame | **T-states/sec** |
|---|---|---|---|
| boot-48k (3.5 MHz, divisor 8) | 329.3 | 69,888 | **23.0M** |
| boot-nextzxos (28 MHz, divisor 1) | 95.2 | 567,264 | **54.0M** |
| boot-nextzxos, rewind off | 126.8 | 567,264 | **71.9M** |

**Next mode processes ~3× more guest work per host second than 48K mode.** The emulator is not
inefficient in Next mode; it is *most* efficient there. The FPS gap is the guest asking for 8×
more computation.

*(Precision note: T-states/sec assumes the whole run is at 28 MHz. The boot has brief speed dips
in its first frames, so these are a <1% overestimate.)*

### 1.3 What the numbers establish

1. **The real target, stated precisely.** 200 FPS at 400% in Next mode = 200 × 567,264 =
   **113.5M T-states/s**. Best measured (P-core, rewind off) = **71.9M**. **Shortfall: 1.58×.**
   That is Task 27's number. In 48K mode the target is 14.0M and we already do 23.0M —
   **400% in 48K mode already works** (329 FPS ≥ 200), which is why nobody noticed.
2. **48K costs ~3× more per T-state than Next — but WHY is not settled.** At least three
   mechanisms predict this direction, and **the single aggregate ratio cannot distinguish
   between them**:
   - **C1** — the O(master_cycles) peripheral tick loops: 8× more master cycles per T-state at
     divisor 8, so CTC/UART looping costs 8× more per unit of guest work at 3.5 MHz.
   - **C3** — contention: *disabled* at any CPU speed ≠ 3.5 MHz (`contention.cpp:246-248`), so
     Next at 28 MHz skips the whole raster-math path that 48K pays on every bus cycle.
   - **Workload composition (dispatch density)** — found by the methodology reviewer, and the
     one I missed: **HALT is never fast-forwarded.** `fuse_z80_execute_one()`
     (`fuse_z80_core.c:188,198`) re-fetches and re-dispatches every call regardless of
     `z80.halted`, and HALT does not advance PC (`opcodes_base.c:523-524`). An idle 48K BASIC
     prompt therefore spends its whole frame re-dispatching a 4-T HALT — ~17,500 dispatches per
     frame, the **worst possible T-states-per-dispatch density** — while NextZXOS boot runs
     varied code (very plausibly `LDIR`-style block ops) that amortises the same fixed
     per-instruction overhead over far more T-states. This has **nothing to do with CPU speed**
     and predicts the same result.

   **Rev 2 of this document claimed "nothing else predicts this" and promoted C1/C3 on that
   basis. That was wrong** — it is precisely the "fit a hypothesis to one number" error §2 warns
   about, committed by the document that warns about it. C1/C3 are *consistent with* the data,
   not *supported by* it. **The `perf` profile adjudicates. Nothing is promoted before then.**
3. **The rewind buffer costs 24-31%, on every workload, by default.** Independently reproduced
   by the reviewer (+26.6% / +33.3%). The only measured win available.
4. **`-O0` vs `-O3` is 5.8×** (both 48K and Next: 5.87× / 4.7×). A real trap — `CLAUDE.md:87`
   points everyone at the `-O0` binary — but **not** the cause of the 400% gap (§2).

---

## 2. Two hypotheses that measurement killed

**Hypothesis A: "the `-O0` build explains the 75-vs-200 FPS gap."** Five of six independent
assessors converged on this, unprompted. It is **wrong**: the `-O3` binary still delivers only
~70 FPS in Next mode. Corroboration the reviewer added: the `-O0` binary manages **14.9 FPS** in
Next mode — nowhere near the reported ~75 — so the user's original observation was certainly
taken on an *already optimised* binary.

**Hypothesis B: "it's a pacing bug — the Qt timer / audio pacing can't deliver 200 FPS."** Also
wrong. At 400% the timer is 5 ms and runs one frame per tick, so the ceiling *is* 200 FPS; but
one frame already costs ~14 ms, so the timer never binds. Moving the timer from 20 ms to 5 ms
changes total wall time by **3.5%** (measured by the audio/GUI assessor).

Rev 1 of this document then made **its own** unmeasured claim — "Next mode costs 3.4×" — and the
methodology reviewer killed that too, by running an experiment nobody had thought to run (§1.2).

Three hypotheses, three refutations, all from measurements that took minutes. **Every remaining
item in §5 has exactly the status those three had before they were tested.** Treat accordingly.

> *Caveat carried over from rev 1:* the "audio pacing is exonerated" claim rests on the
> audio/GUI assessor's measurement, not on one taken in this document. It is reported, not
> independently verified here.

---

## 3. Measurement rule and benchmark harness

**HARD RULE (user, 2026-07-14): no optimization without a baseline.** Every change carries a
before- and an after-measurement. A change that cannot be measured does not get made.

**The harness is a prerequisite deliverable** — it lands *before* the first optimization commit.
Requirements, all derived from mistakes made while producing this document:

- **Primary metric: T-states/sec**, not FPS. §1.2 is the reason. Report FPS too, but never
  compare it across workloads with different CPU speeds.
- **Workloads must be plural**: `boot-48k` (3.5 MHz — the *worst* per-T-state case),
  `boot-nextzxos` (28 MHz — the real target), `copper-demo`, `beast`. All exist as regression
  assets.
- **Check whether the host is hybrid** (`scaling_max_freq` across cores; this box is P/E with a
  1.43× ratio). **Pin to the fastest core class, and print the core id + its max frequency in
  every benchmark report.** Never compare figures taken on different core classes — a 40% swing
  in the headline number came from exactly this, undetected across two document revisions.
- **Median of ≥5 with min/max spread printed.** A spread over ~5% voids the run.
- **Never benchmark while agents or builds run.**
- **Mode: headless.** Justified by measurement — the Qt presentation path costs ~4% — and §2
  establishes there is no pacing bug for headless to hide.
- **Attribution: `perf record`** on `-O3`. Release does not set `-fno-omit-frame-pointer`; add
  it, or use `--call-graph dwarf`.
- `src/profiler/` is a **guest** profiler (T-states per Z80 address). It cannot say which C++
  function is hot. Do not extend it for 27d.
- **`doc/design/PROFILING-OPTIMIZATION-PLAN.md` already specifies this harness** (`--benchmark N`,
  a per-frame zone profiler, a `perf` baseline) and should be superseded by, not duplicated in,
  the 27d plan. Note its item **C2 ("Pentagon/Next don't have contention — null the callback") is
  now WRONG** and would break Next-mode contention (Tasks 50/54).

---

## 4. Architecture as it stands

### 4.1 Shape

~61 kLOC / 14 subsystem libraries. The core is a **god object**: `emulator.cpp` is 8,690 lines,
of which **`Emulator::init()` alone is 5,690** (178 NextREG handler lambdas + 53 port handler
lambdas, registered inline). `emulator.h` owns **48 subsystems by value**, exposes **154 public
methods** (77 reference-returning accessors), and `#include`s 56 subsystem headers — so touching
any subsystem recompiles the GUI, the debugger and every loader.

**The core/SDL boundary is violated** (found by the claims reviewer; rev 1 wrongly asserted the
opposite). `emulator.h:35` includes `input/keyboard.h`, which includes `<SDL2/SDL.h>` and
declares `set_key(SDL_Scancode, bool)` — **SDL is in the core's public API**, contradicting the
project's own rule (EMULATOR-DESIGN-PLAN.md §3: "SDL lives exclusively in `src/platform/`").
Worth its own follow-up regardless of Task 27.

### 4.2 The hot loop

`run_frame()` (`emulator.cpp:6330`, 677 lines) drives everything **per Z80 instruction**. At
3.5 MHz, `master_cycles = tstates × 8`, so a 4-T instruction drags 32 master cycles of peripheral
work behind it. Per instruction, on the default path: 2 full `Z80Registers` by-value copies
(`:6412`, `:6573`), ~41 register field copies (FUSE sync in+out), a `std::getenv` static guard
(`:6526`), `im2_.tick()` walking a **14**-device daisy chain, `Ctc::tick()` looping
`master_cycles × 4`, `Uart::tick()` looping `master_cycles`, 2-5 `TurboSound::tick()` each with an
unconditional stereo remix, and — because rewind is on by default — a per-instruction trace
record costing ~6 `Mmu::read`s plus a `std::function` construction.

**The perverse scaling is now measured, not just predicted** (§1.3, item 2): because
`master_cycles = tstates × cpu_divisor`, the O(master_cycles) loops cost **8× more per T-state at
3.5 MHz than at 28 MHz**. The emulator pays most to emulate the slowest machine.

### 4.3 Per-subsystem summary

| subsystem | verdict |
|---|---|
| **core** | God object; `run_frame()` plus a drifted hand-copy of it (`execute_single_instruction`, → Task 60a). Five clean seams for splitting. |
| **cpu/memory** | ~5 runtime integer divisions **per bus cycle** (`derive_hc_vc`, `z80_cpu.cpp:108`; 8 call sites), with the contention-off early-out on the **wrong side** of the call. `Mmu::read` is *not* the O(1) inline the 1,706-line header implies — six sequential overlay gates + an out-of-line `divmmc_read`. No LTO, so CPU→MMU is an un-devirtualisable cross-TU vtable call. |
| **video** | **Architecturally the healthiest**: zero virtuals, zero allocations, zero `std::function` on the hot path. Its problem is redundancy — tilemap re-decodes each tile 8-16×, the compositor re-dispatches a per-line-constant `switch` 163,840×/frame, sprites re-resolve the anchor chain 256×/frame, and every frame is fully rendered even at 400% when the GUI shows ~60. |
| **audio** | PSG ticks at 1.75 MHz = 35,000 `TurboSound::tick()`/frame **regardless of guest CPU speed**, each with an unconditional stereo remix, even when silent. At speed ≠ 1× the output is generated and then thrown away (the ring overflows every tick). |
| **peripherals/ports** | Port dispatch is a **flat linear scan of all 53 handlers × 72 bytes (60 cache lines) on every IN/OUT** — there is **no early exit**, so the cost is identical regardless of which handler matches — while recomputing a constant popcount (`mask_specificity()`) on every match. NextREG next door is a clean O(1) array dispatch: the right shape already exists in the same directory. |
| **platform/gui** | Three frontends, three unrelated pacing loops, already diverged (`.rzx` loads in headless but not Qt; SdlApp has no `--speed` at all). GUI paint ~4% — not a bottleneck. |
| **cross-cutting** | `Saveable` is a **dead interface** (→ 60b). `StateWriter`/`StateReader` never check the `capacity_` they store. spdlog `trace()`/`debug()` are **never compiled out** (zero `SPDLOG_*` macros in the tree), so every IN/OUT pays a live level check. |

---

## 5. Candidate optimizations

> **Read §2 first.** Everything below Tier A is a structural prediction. Three such predictions
> have already been refuted this session. Rank order is *predicted* value — 27d's job is to
> falsify it, not implement it.

### Tier A — measured

| # | Change | Measured | Risk | Site |
|---|---|---|---|---|
| **A1** | **Rewind buffer opt-in** (default 500 → 0) or lazily allocated when the debugger opens. Also eagerly memsets **1.09 GB** at every startup — including headless screenshot runs and unit tests. | **+24-31%** on every workload (reproduced by reviewer) | Low | `emulator_config.h:135`; `emulator.cpp:5704-5718`, `:6099-6100` |
| **A2** | **Decouple the trace log from rewind.** `emulator.cpp:5712` enables a per-instruction recorder *inside* the rewind-enable block; its output is overwritten every 10k instructions and never read unless you step back. | subset of A1 | Very low | `emulator.cpp:5712`, `:6505-6521` |

### Tier B — build/config

| # | Change | Predicted | Risk | Site |
|---|---|---|---|---|
| **B0** | **Default `CMAKE_BUILD_TYPE`.** `cmake -B build` silently yields `-O0` — **5.8× slower** — and `CLAUDE.md:87` points every human and agent at that binary. Dev-hygiene bug, **not** the 400% bug. | 5.8× for `build/` users | ~0 | root `CMakeLists.txt`; `CLAUDE.md:87` |
| **B1** | **LTO/IPO on release.** 14 static libs ⇒ every CPU→MMU access is an un-devirtualisable cross-TU call and no idle early-out below can be hoisted. | unknown | Low-med (re-run full triplet) | root `CMakeLists.txt` |
| **B2** | `-fno-omit-frame-pointer` on release so `perf` call graphs work. **Not a speedup — an enabler. Do it first.** | — | none | `Makefile` |

### Tier C — hot-loop structure

**No Tier-C item is promoted above the others.** Rev 2 promoted C1/C3 on the strength of the 48K
per-T-state penalty; the methodology reviewer showed a third mechanism (HALT dispatch density,
§1.3 item 2) predicts the same thing and is indistinguishable with that data. The `perf` profile
decides — that is the whole point of §8 step 3. **A new candidate falls out of the same finding:**

| # | Change | Predicted | Risk | Site |
|---|---|---|---|---|
| **C11** | **Fast-forward HALT.** `fuse_z80_execute_one()` re-fetches and re-dispatches the HALT opcode through the full per-instruction machinery ~17,500×/frame on an idle machine, when it could jump straight to the next scheduled interrupt. Costs nothing on busy workloads; large on idle/menu/BASIC-prompt ones (which is what a user staring at a menu is running). | Unknown — profile first | Med (interrupt timing must be exact) | `fuse_z80_core.c:188,198`; `opcodes_base.c:523-524` |

| # | Change | Predicted | Risk | Site |
|---|---|---|---|---|
| **C1** | **Kill the O(master_cycles) peripheral tick loops.** `Ctc::tick` loops `master_cycles × 4` (128-320 no-op cross-TU calls per instruction at 3.5 MHz); `Uart::tick` loops `master_cycles`. Both early-out *inside* the callee. `Md6ConnectorX2::tick` (`md6_connector_x2.cpp:245`) already shows the correct accumulator pattern **in this repo**. | High | Low | `ctc.cpp:228`; `uart.cpp:89` |
| **C3** | **Hoist the contention early-out above the raster math.** The gate lives *inside* `contention_tick` (`contention.cpp:246-248`), so every bus cycle computes ~5 divisions before learning the answer is 0. | High | Very low | `contention.cpp:246-248`; 7 call sites in `z80_cpu.cpp` |
| **C2** | **Kill the ~5 runtime integer divisions per bus cycle** (`derive_hc_vc`, `z80_cpu.cpp:98-114`; `%`/`/` by *runtime* variables ⇒ no strength reduction). Memoize or track `(hc,vc)` incrementally. | High | **Med** — the contention path is the most correctness-sensitive code in the emulator; FUSE + Nirvana/BIFROST are the gate | `z80_cpu.cpp:98-114`, 8 call sites |
| **C6** | **Skip `render_frame()` for frames the frontend will never display** (turbo/fast-forward). At 400% we composite 200 full 640×256 frames/s while the GUI shows ~60. | **High for the 400% case specifically** | Low (guard screenshot / recorder / debugger) | `emulator.cpp:6965-6968` |
| **C4** | **Port dispatch**: cache the popcount at registration; split match keys into a parallel hot array (5 cache lines instead of 60). Full 64K LUT is the end-state — do the cheap half first, then measure. | Med-high | ~0 | `port_dispatch.cpp:16-20`, `:45`, `:87` |
| **C7** | **Tilemap: hoist per-pixel work to per-tile.** ~490k `vram_read`/frame where ~30k would do; runtime-variable `%` per pixel. | High | Med (per-pixel clip gate must not move) | `tilemap.cpp:429-512` |
| **C8** | **Compositor**: hoist the per-pixel `switch (layer_priority_)` (a per-line constant, 163,840 dispatches/frame) and the dead `trace_active_` branch; skip layer-buffer clears for disabled layers (**~2.5 MB/frame** of stores — rev 1 said 3.3 MB; corrected). | Med-high | Low | `renderer.cpp:627`, `:789`, `:290-296` |
| **C5** | **Audio**: skip PSG/mixer work whose output is discarded at speed ≠ 1×; hoist `Mixer::mix()` off the per-instruction path. | Med | Med (must not regress 1× audio) | `emulator.cpp:6911-6925`, `:7164` |
| **C9** | **Copper**: two 64-bit divisions per master cycle while running (up to ~240/instruction). Only bites Copper demos — which measured slowest (47 FPS). | High when active | Med (delicate `cvc` origin logic) | `emulator.cpp:7815-7837` |
| **C10** | Per-instruction odds and ends: 2 `Z80Registers` by-value copies; a `std::getenv` static guard; `video_timing_.advance()` (the code's own comment calls it a test-only observable); the dead `on_contention` callback stored every instruction. | Low-med each | ~0 | as cited in §4.2 |

---

## 6. Comprehensibility / refactor candidates

Feeds 27b. User's stated preference: **subsystem by subsystem, then globally.**

| # | Change | Value |
|---|---|---|
| **R1** | **Split `emulator.cpp` (8,690 lines)** along its five seams: `_nextreg` (~2,400), `_ports` (~1,800), `_state` (~675), `_media` (~250), `_run` (~1,200 — *the file that actually matters*). Pure code motion. **Highest value/risk in the report; do it first — it makes every later diff reviewable.** | Very high |
| **R2** | **Unify the nine per-scanline replay mechanisms** behind one `PerLineLog<T>` (`render_frame` hand-lists 8 rewinds + 7 applies + 8 flushes). | High |
| **R3** | **Unify the three frontend loops**, which have already diverged in user-visible ways. | High |
| **R4** | **Define a real `Peripheral` lifecycle** (`reset`/`tick`/`save_state`). Today `Dma`'s tick takes `uint64_t` while others take `uint32_t`, `Copper` has no tick, `Spi`/`I2c`/`SdCard` have no lifecycle. This is what lets the 130-line tick cluster — **duplicated** in `execute_single_instruction()`, i.e. Task 60a — become a loop. | High |
| **R5** | **Dead code**: the NR 0x15 per-scanline change log (~90 LOC + a 1024-entry log) has **zero production callers** (verified twice); `Ula::render_frame` has none either. Tested but unused — a trap. | Med |
| **R6** | **Magic numbers (27c)**: `emulator.cpp` has ~2,411 hex literals (1 per 3.6 lines), `nextreg.cpp` 291 (1 per 1.8), against **256 `constexpr` in the whole tree**. Mitigating: nearly every literal carries a VHDL citation in the comment above it — the knowledge is present, just not compiler-checked. `src/video/timing.h:36-53` shows the target shape. Do it per-register, or it collides with every other branch. | Med |
| **R7** | **Relocate the archaeology** (Pass-4/5/6, G12/G46/G53/G141 blocks >40 lines inline) to `doc/`, leaving a one-line VHDL citation. Would roughly halve `z80_cpu.cpp` and `mmu.h` without losing a fact. | Med |
| **R8** | **Restore the core/SDL boundary** (§4.1): `emulator.h` → `input/keyboard.h` → `<SDL2/SDL.h>`, with `SDL_Scancode` in the core's public API. Violates EMULATOR-DESIGN-PLAN.md §3. | Med |

---

## 7. Bugs found (not optimization — Task 60)

Four defects, **all verified by direct inspection** (not taken on an assessor's word). Tracked in
`.prompts/2026-07-14.md`; each needs its own branch, fix, test and independent review.

- **60a — debugger single-stepping silently drops interrupts.** `execute_single_instruction()`
  never calls `im2_.tick()` and never polls `pulse_int_n()`, though `run_frame()` does both every
  instruction. IM2 is what delivers interrupts. Reachable from `debugger_manager.cpp:281` (Step
  Into) every time a user steps through interrupt-driven code; no compensating mechanism exists.
- **60b — `Saveable` is a dead interface** (zero implementers), and `StateWriter`/`StateReader`
  never check the `capacity_` they store: an asymmetric save/load edit corrupts all subsequent
  subsystem state silently, with no sentinel.
- **60c — `src/input/` is absent from every snapshot** (no `save_state` anywhere in the directory;
  zero input references in `Emulator::save_state`). Keyboard/joystick/mouse/MD6 state is lost on
  every rewind; MD6 has a live FSM ticked every instruction.
- **60d — IM2 pulse counter is 8× fast at 3.5 MHz.** Commit `314a752d` ("pulse_count_ ticks per
  CPU T-state, not per Z80 instruction") rewrote `im2.cpp` and its comment — which states the
  caller "now passes `tstates`, not `tstates × cpu_divisor`" — **but never changed the caller**.
  `emulator.cpp:6619` passes `master_cycles` and always has (git-verified: `im2_.tick(tstates)`
  never existed). So `pulse_count_advance_` receives 8× its intended value at the default 3.5 MHz
  (divisor 8), and is correct only at 28 MHz (divisor 1). Needs investigation against
  `zxnext.vhd:2037-2044` before being called a bug or a stale comment — but one of the two is
  wrong, and both are load-bearing.

---

## 8. Proposed sequencing for 27b / 27c / 27d

Profiling moves **early** — the design plan puts it last. §2 is the justification: three
confident, well-evidenced hypotheses were refuted by measurements that took minutes each, and
refactoring first means refactoring blind to what matters.

1. **B2 + the benchmark harness** (`--benchmark N` reporting **T-states/sec**, fixed core, median
   of ≥5, spread printed). *No optimization commit lands before this.* Supersedes
   `PROFILING-OPTIMIZATION-PLAN.md` Phase A.
2. **A1 + A2** (rewind/trace) — the only measured wins, one commit each, before/after measured.
   Do these first: they are large, safe, and they clean the profile of a 25-30% distortion that
   would otherwise smear across every later measurement.
3. **`perf record` the four workloads on `-O3`** (P-core, pinned). The question is **not** "why is
   Next slower" — §1.2 answered that (it isn't). It is: **where do the 71.9M T-states/s go, and
   what closes the 1.58× gap to 113.5M?** The profile must also adjudicate the three-way tie in
   §1.3 item 2 (C1 vs C3 vs C11/dispatch density) — a cheap discriminator is to benchmark a
   *busy* 48K workload against the *idle* one: if the 48K penalty largely vanishes, the cause is
   HALT dispatch density, not CPU-speed scaling.
4. **B0/B1** (build type, LTO) — cheap, independent, measurable.
5. **27b refactor, subsystem by subsystem** (user's preference), starting with **R1** so every
   later diff is reviewable. Task 60a's fix (R4-shaped) falls out naturally here.
6. **Tier C, in profile order** — not in the order §5 lists them.
7. **27c magic numbers** last: a large mechanical diff that will collide with every other branch.

---

## 9. What was NOT assessed

- **No profiler has been run.** Every Tier-B/C item is structural. All six assessors said so
  themselves, unprompted.
- **The 1.58× shortfall has no attributed cause yet.** C1, C3 and C11 are all *consistent with*
  §1.3 item 2 and none is proven; the data cannot separate them.
- **The G12 attribute-mux "5-10%" figure was not re-measured.** Two assessors argue the cost
  actually lives in `z80_cpu.cpp`'s *double* call to `derive_hc_vc` per Z80 write (`:202-204`),
  one of which is simply redundant. Unverified.
- **Audio pacing was not measured here** (§2 caveat).
- Not read: FUSE opcode files, `z80n_ext.cpp`, most of `mmu.cpp`, the debugger panels, `dma.cpp`
  burst internals, `sd_card.cpp` FSM, `i2c.cpp`, `multiface.cpp`, the loaders.
- **One machine** (AMD Ryzen AI 5 340, **hybrid P/E**, `powersave` governor), one compiler (GCC),
  one OS. Cross-core-class FPS varies by ~40% — see §1.
