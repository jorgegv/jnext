# Task 27 P1 — Profile Report and Three-Way Discriminator Verdict

> **Status:** measurement report, 2026-07-15. Branch `task27-p1-profile` @ `732214d1`
> (main after A1 rewind-opt-in + A2 trace decoupling). **No emulator code was changed
> by this task.** This document adjudicates TASK27A §1.3 item 2 and re-ranks Phase C
> of [TASK27-OPTIMIZATION-PLAN.md](TASK27-OPTIMIZATION-PLAN.md).

---

## 1. Methodology

| item | value |
|---|---|
| Host | AMD Ryzen AI 5 340 (hybrid P/E), Fedora 44, kernel 7.0.14-201 |
| Core pinning | **core 0 @ 4,900,000 kHz** (fastest class, lowest-numbered — derived from `scaling_max_freq` at runtime, bench.sh logic) |
| Binary | `build/gui-release/jnext`, `CMAKE_BUILD_TYPE=Release` (`-O2 -DNDEBUG`, **no** frame pointers per T0 review), not stripped |
| perf | 7.1.3-201.fc44 (matches kernel); `perf record -F 4000 -e cycles:u --call-graph dwarf,16384` |
| Sampling scope | `cycles:u` only — `perf_event_paranoid=2` permits user-space self-profiling; kernel time is invisible (it is small: the catch-all "other" category stays ≤ 5.5% everywhere) |
| Serialisation | `flock` on `test/bench/.lock` held for every measurement block; box verified idle (no task60a jnext / regression.sh processes, 1-min loadavg ≤ 0.92 at every block start) |
| Timing source | `--benchmark N` BENCH lines (T1 harness), median-of-5 via `make bench` → `test/bench/baseline-732214d1.txt` |
| Samples per profile | 4.2K–19.7K (one perf pass per workload; percentages below are self-cost shares of the whole process run) |

Artifacts committed under `doc/perf/`:
`<workload>-flat.txt` (flat self-cost, `--no-children -g none --percent-limit 0.1`) and
`<workload>-folded.txt` (collapsed caller stacks, `--children -g folded,0.5,caller`), for the
5 canonical workloads plus the 2 discriminator runs, and `exp3-density-bench.txt` (the
density-controlled Experiment-1 rerun, added after the independent review). Raw `perf.data`
files (~1.3 GB total) were deliberately **not** committed.

**Caveats.** (a) Profile percentages include the ~0.25 s SD/ROM init that BENCH excludes —
for run lengths of 1–5 s this dilutes hot-loop shares by a few percent uniformly; it does not
reorder anything. (b) Single perf pass per workload (not median-of-5); the independent
median-of-5 BENCH numbers bracket the profiled runs within their spread. (c) `-O2` inlining
means some helpers are attributed to their inlining site (e.g. `derive_hc_vc` /
`to_ula_counters` cost appears inside `contend_read*` / `fuse_z80_readbyte` /
`ContentionModel` symbols).

---

## 2. Baseline (median-of-5, `make bench`, this commit)

From `test/bench/baseline-732214d1.txt` (all spreads ≤ 4.82%, all OK):

| workload | machine | fps | T-states/s | ms/frame |
|---|---|---:|---:|---:|
| boot-48k | 48k, idle prompt | 402.6 | 28.14 M | 2.484 |
| bifrost | 48k, static "press any key" menu — **NOT busy** in this window, see §3.1 | 417.3 | 29.16 M | 2.396 |
| boot-nextzxos | next @ 28 MHz | 128.5 | **72.87 M** | 7.782 |
| copper-demo | next @ 28 MHz | 81.1 | 46.03 M | 12.33 |
| beast | next @ 28 MHz | 89.3 | 43.02 M | 11.20 |

Target (plan "definition of done"): **113.5 M T-states/s on boot-nextzxos** = 200 fps =
5.00 ms/frame. Current shortfall **1.557×** (2.78 ms/frame must go).

> Note in passing: **copper-demo and beast sit at 43–46 M** — a Next *game* is ~1.7× further
> from 400% than the boot workload the target is defined on. See §6.4.

---

## 3. The discriminator (TASK27A §1.3 item 2)

Three candidate mechanisms for "48K costs ~2.6× more per T-state than Next":
**C1** (O(master_cycles) CTC/UART loops), **C3** (contention raster math), **C11**
(HALT/dispatch density — idle 48K re-dispatches a 4-T HALT ~17.5k×/frame).

### 3.1 Experiment 1 — dispatch density at constant divisor

> **Revision after independent review (REJECT, 2026-07-15).** The first version of this
> experiment compared boot-48k against bifrost and called bifrost "busy". That control was
> **invalid**: in the measured 600-frame window bifrost sits on its static "press any key"
> menu (the reviewer instruction-counted it: 82,885 instructions/10 frames vs boot-48k's
> 83,591 — the same idle loop, 0.85% apart). Idle-vs-idle proves nothing about density.
> The experiment below replaces it with density-**controlled** injected workloads.
>
> The reviewer's count also establishes a fact the C11 hypothesis got wrong: the idle 48K
> prompt runs at 69,888 / 8,359 = **8.36 T-states per dispatch — it is a keyboard-scan loop,
> not a HALT loop** (HALT density would be ~4.0). TASK27A's "~17,500 HALT re-dispatches per
> frame" premise does not describe boot-48k.

Three hand-assembled programs injected on **--machine 48k** (divisor 8 throughout, same as
boot-48k; inject at frame 150, `--benchmark 1800`, median-of-5, locked, core 0). Busy-frame
cost = (wall − 150 × 2.484 ms boot frames) / 1650. Raw data:
`doc/perf/exp3-density-bench.txt`.

| workload | T-states/dispatch | dispatches/frame | ms/frame (busy) | ns/T-state |
|---|---:|---:|---:|---:|
| exp3-nopspin (200×NOP + JP) — HALT-density worst case | 4.03 | 17,342 | 2.807 | **40.2** |
| boot-48k idle prompt (reference; measured density 8.36) | 8.36 | ~8,359 | 2.484 | **35.5** |
| exp3-busy48 (the exp2 loop body, no NEXTREG) | 8.67 | ~8,062 | 2.573 | **36.8** |
| exp3-exspin (200×`EX (SP),HL` + JP) — densest practical | 18.96 | 3,687 | 2.108 | **30.2** |
| *(for scale)* exp2-next-28mhz | 8.67 | — | — | 13.5 |

Findings:

1. **The reviewer's requested A/B (busy-48k vs idle boot-48k at matched density): 36.8 vs
   35.5 ns/T — within 4%.** A busy 48K workload of the *same* instruction density costs the
   same as the idle prompt. The original bifrost "busy ≈ idle" result was an artifact of
   comparing two idle loops, not evidence.
2. **The density mechanism itself is real and now quantified.** nopspin vs exspin (a 4.7×
   density swing, everything else identical — same machine, divisor, loop structure) moves
   per-T-state cost 40.2 → 30.2 ns. The slope gives a **per-dispatch overhead of ≈ 51 ns**
   (0.699 ms / 13,655 dispatches) — the per-instruction fabric (IM2 step, register sync,
   M1 callback chain) measured a second, independent way.
3. **But density is a minority contributor to the 48K penalty.** Even the densest workload
   (30.2 ns/T) remains 2.2× the 28 MHz cost (13.5 ns/T). Of the idle-48K-vs-Next gap
   (35.5 − 13.5 = 22.0 ns/T), an *extreme* density improvement recovers at most 5.3 ns/T
   (~24%); at boot-48k's actual density the recoverable amount is ~0 (it already sits at the
   matched-density busy control's cost). The dominant mechanism is §3.2/§3.3's
   divisor-driven fixed-per-frame work, not dispatch density.

### 3.2 Experiment 2 — CPU-speed scaling, workload held constant

There is no CLI to pin NR 0x07, but `--inject` allows it cleanly: a 16-byte hand-assembled
Z80 program (`DI; NEXTREG 0x07,v; loop: LD HL,0x9000; LD B,0; inner: LD A,(HL); INC HL;
DJNZ inner; JR loop`) injected at 0x8000 on **--machine next** at frame 20, differing *only*
in the NR 0x07 value (0 = 3.5 MHz vs 3 = 28 MHz). Same machine, same code, same bus mix —
only the divisor differs. Median-of-5, core 0, lock held:

| run | fps | T-states/s | ms/frame | per-T-state cost |
|---|---:|---:|---:|---:|
| exp2-next-3p5mhz | 395.2 | 28.03 M | 2.531 | 35.7 ns |
| exp2-next-28mhz | 130.5 | 74.01 M | 7.663 | 13.5 ns |

**The 2.64× penalty reproduces exactly with the workload held constant** (48K vs Next was
2.59×). The penalty follows the divisor, not the code. Additionally, the pinned-3.5 MHz
run's per-T-state cost (35.7 ns) matches boot-48k's (35.5 ns) within 1%, and the pinned-28 MHz
run matches boot-nextzxos within 2% — per-T-state cost at fixed divisor is insensitive to
workload **within the class measured here: plain-CPU workloads of comparable instruction
density, Copper off, ordinary render duty**. It is *not* workload-independent in general:
§4's own data shows copper-demo (46.0 M) and beast (43.0 M) at the same 28 MHz divisor as
boot-nextzxos (72.9 M) because Copper execution and render/copper duty add large per-frame
cost, and §3.1 shows instruction density moves the 3.5 MHz cost by ±14%.

### 3.3 The two-component cost model

The exp2 pair executes the same master cycles per frame (567,264) but 8× different T-states.
Solving `wall = T·tstates + M` across the pair:

- **T = 10.34 ns per T-state** — per-instruction / per-bus-cycle work (FUSE core, Mmu virtual
  reads, contention preamble, IM2 per-instruction step, M1 callback chain, register sync).
- **M = 1.80 ms per frame** — fixed per-frame work proportional to master cycles / scanlines /
  samples (CTC+UART master-cycle loops, renderer, audio, per-call tick overhead).

At 3.5 MHz, M is amortised over 8× fewer T-states: 1.80 ms/70,908 T = 25.4 ns/T of overhead —
that **is** the 48K penalty (25.4 + 10.3 ≈ 35.7 ns measured). At 28 MHz M costs only
3.2 ns/T.

### 3.4 Verdict

- **C11 (dispatch density as the 48K-penalty explanation): KILLED — now on valid evidence**
  (the first version of this kill rested on the broken bifrost control; §3.1's
  density-controlled pair replaces it). The *mechanism* is real — ≈ 51 ns per dispatch,
  worth ±14% at 3.5 MHz across a 4.7× density swing — but it explains at most ~24% of the
  48K-vs-Next gap in the extreme, and ~0% for the actual idle prompt, which is **not
  HALT-bound in the first place** (8.36 T/dispatch measured, not ~4). The specific
  optimization "fast-forward HALT" would therefore not even engage on boot-48k. The 51
  ns/dispatch overhead itself is exactly what ranks 1/4/5 in §5 attack directly.
- **C1 (O(master_cycles) tick loops): REAL and dominant for the 48K penalty.** CTC+UART self
  cost is 27–32% of every 3.5 MHz frame (0.69 ms of M's 1.80 ms). The rest of M is renderer
  (~13%), audio (~10–12%), and per-call loop overhead.
- **C3 (contention raster math): REAL but mis-filed.** At 3.5 MHz the raster math is *live
  work* (contention must be computed) and costs only ~5% of the frame — it is a minor part of
  the 48K penalty. The big C3 payoff is at **28 MHz**, where `contend_read` /
  `contend_read_no_mreq` / `fuse_z80_readbyte` still execute `mem_active_page_for()` +
  `derive_hc_vc()` (div+mod) + `to_ula_counters()` (two mods) on **every bus cycle** before
  `contention_tick()` returns 0 from its speed gate (`contention.cpp` enable gate: `cpu_speed_
  != 0 → return 0`). Measured **8.2% of boot-nextzxos and 13.1% of the pinned-28 MHz run** —
  pure dead math on the workload the 400% target is defined on. C3 and C-DIV are the same
  code path and should be one task.
- Note: 48K mode **already exceeds** its 400% requirement (28.1 M ≥ 14.0 M), so the 48K
  penalty is not itself a Task-27 blocker; C1's value is that the same fixed-M work also
  costs 0.5–0.6 ms/frame at 28 MHz (CTC+UART = 7.0% of boot-nextzxos).

---

## 4. Where the time goes (self-cost % of each profiled run)

Category aggregation of the flat profiles (categories = sums of symbol self-cost; full
symbol-level data in `doc/perf/*-flat.txt`):

| category | boot-48k | bifrost | **boot-nextzxos** | copper-demo | beast | exp2-3.5 | exp2-28 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Emulator::run_frame (self) | 7.8 | 8.4 | **12.5** | 25.6 | 21.1 | 9.7 | 11.0 |
| Copper::execute | — | — | — | 4.3 | 3.8 | — | — |
| Ctc::tick | 15.3 | 12.5 | **4.7** | 1.0 | 1.1 | 11.5 | 6.6 |
| Uart/UartChannel::tick | 16.5 | 17.0 | **2.3** | 1.0 | 1.3 | 15.3 | 2.8 |
| Im2Controller (tick+step_pulse+step_devices+on_m1) | 4.6 | 5.4 | **14.1** | 13.6 | 14.9 | 7.7 | 15.9 |
| contention preamble+gate (contend_read*, contention_tick) | 5.6 | 4.4 | **8.2** | 4.0 | 5.7 | 5.6 | 13.1 |
| Mmu::read/write/get_page | 3.2 | 1.0 | **8.5** | 10.9 | 9.7 | 1.0 | 8.9 |
| FUSE core (fuse_z80_execute_one, readbyte/writebyte) | 4.1 | 3.4 | **5.9** | 2.0 | 1.8 | 2.1 | 6.3 |
| CPU wrapper (Z80Cpu::execute, sync_regs_from_fuse) | 5.5 | 8.5 | **12.1** | 9.2 | 6.5 | 6.0 | 13.7 |
| M1 callback chain (DivMmc automap, Multiface::on_m1, NmiSource) | 2.2 | 3.0 | **7.7** | 6.6 | 4.7 | 4.6 | 4.4 |
| std::function dispatch glue | 2.7 | 2.5 | **5.5** | 8.2 | 7.5 | 3.9 | 4.2 |
| video render (Renderer, Tilemap, Layer2, VideoTiming) | 13.6 | 13.7 | **5.4** | 3.8 | 12.5 | 13.0 | 5.1 |
| audio (AY, TurboSound, Mixer) | 11.6 | 12.1 | **3.7** | 3.3 | 3.2 | 9.9 | 2.1 |
| Dma::tick_burst_wait | 1.1 | 1.0 | **1.8** | 0.8 | 0.7 | 1.1 | 1.2 |
| Scheduler::run_until | 1.8 | 1.7 | **1.5** | 1.3 | 1.1 | 1.7 | 1.2 |
| TraceLog | 0 | 0 | **0.1** | 0.6 | 0.7 | 0.2 | 0.1 |
| other (libc, memset, unlisted) | 3.4 | 4.4 | **4.3** | 2.9 | 2.4 | 5.5 | 2.7 |

Top-15 symbols per workload are the head of each `doc/perf/<workload>-flat.txt`; the three
worth quoting for boot-nextzxos: `Emulator::run_frame` 12.52%, `Mmu::read` 7.26%,
`Z80Cpu::execute` 6.19%, then `sync_regs_from_fuse` 5.95%, `Im2Controller::step_devices`
4.98%, `Im2Controller::tick` 4.87%, `Ctc::tick` 4.67%, `contend_read` 4.23%,
`Im2Controller::step_pulse` 3.64%, `contend_read_no_mreq` 3.39%.

Structural findings behind the table:

- **`Im2Controller::tick()` runs `step_pulse()` + `step_devices()` once per *instruction***
  (`im2.cpp:66-91`) — per-instruction cost, which is why it explodes at 28 MHz (14–16%).
  It was not on the Phase-C candidate list at all. It is the **largest single subsystem**
  on the target workload.
- **`Ctc::tick(N)` loops N master cycles × 4 channels** (`ctc.cpp:227-235`) and
  **`UartChannel::tick(N)` loops N master cycles** (`uart.cpp:89+`) — the C1 mechanism,
  confirmed verbatim in source and in the profile.
- **`Emulator::run_frame` self-cost is the per-instruction dispatch machinery** (interrupt
  polls, scanline-crossing checks, tick fan-out). With the Copper running it jumps
  +9 to +13 points (copper-demo 25.6%, beast 21.1%): `tick_copper_for_master_cycles()`
  (`emulator.cpp:7816`) early-outs when the Copper is stopped but otherwise does a 64-bit
  div **and** mod per master cycle — the C9 mechanism, confirmed. On boot-nextzxos the
  Copper is off and C9 contributes ~0.
- **TraceLog is ~0** (≤ 0.7% — `TraceLog::enabled()` check per instruction), confirming A2.
- `sync_regs_from_fuse` alone is 5.95% of boot-nextzxos — the C10 register-copy item, now
  measured.

---

## 5. What closes the 1.55× gap on boot-nextzxos?

Frame budget: 7.78 ms now → 5.00 ms target. **2.78 ms must go.** Attribution of the 7.78 ms
(from §4, boot-nextzxos column) and honest per-candidate projections:

| rank | candidate | measured share | ms/frame | plausible cut | saved ms |
|---|---|---:|---:|---|---:|
| 1 | **C-IM2 (NEW)** — per-instruction `step_pulse`+`step_devices` even when no interrupt source is active/pending; needs an "armed sources" early-out | 14.1% | 1.10 | 40–60% (interrupt timing is the risk; VHDL im2_* is the oracle) | 0.45–0.65 |
| 2 | **C3 + C-DIV (one task)** — hoist the speed/disable gate above `mem_active_page_for` + `derive_hc_vc` + `to_ula_counters` in the 7 shim call sites | 8.2% | 0.64 | ~85% (gate is 2 compares; keep it live per NR 0x07) | 0.54 |
| 3 | **C1** — CTC+UART accumulator/event-horizon pattern (`md6_connector_x2.cpp:245` model) | 7.0% | 0.55 | ~90% (idle channels skip to next event) | 0.50 |
| 4 | **C10 + glue** — `sync_regs_from_fuse` (5.95%) + remaining `std::function` dispatch (5.5%) → fn-ptr/template (A2 already did this for trace) | 11.4% | 0.89 | ~50% | 0.44 |
| 5 | **C-M1 (NEW)** — consolidate the per-M1 chain (`DivMmc::check_automap` 2.98% + `Multiface::on_m1` 2.45% + retn-delay + NmiSource) behind one cheap "anything armed?" test | 7.7% | 0.60 | ~50% | 0.30 |
| 6 | **C6** — skip `render_frame` for undisplayed frames (guarding recorder/screenshot per plan) | 5.4% | 0.42 | ~80% at 400% GUI; **0% in headless bench as currently defined** | 0.35* |
| 7 | **B1 LTO/IPO** — cross-cutting on the un-devirtualisable Mmu/fuse/wrapper block (Mmu::read 7.3% is a virtual call per byte) | (diffuse) | — | 5–10% whole-binary | 0.4–0.8 |

\* C6's saving is real for the user-facing 400% GUI goal but will only show in `make bench`
if the benchmark defines "frame the frontend would display" — otherwise headless renders
every frame and the bench number won't move. The acceptance criterion for C6 must say which.

**Sum of mid-range projections: ≈ 3.0 ms** against the 2.78 ms needed.

**Honest bottom line:** 113.5 M is **plausibly reachable, but only just, and only if the two
NEW candidates land**. The pre-P1 Phase-C list alone (C1 + C3 + C6 + C10 + B1, mid
estimates ≈ 2.2 ms) gets to ~90–95 M and **does not close the gap**. The profile's most
important output is that **IM2 per-instruction stepping (1.10 ms) and the M1 callback chain
(0.60 ms) — neither of which was on the list — are together the size of the largest listed
candidate.** The floor is also visible: the FUSE core + wrapper + Mmu + run_frame dispatch
block (~37% ≈ 2.9 ms) is the interpreter itself; nothing in Phase C restructures it, and
its share grows as everything else shrinks. If C-IM2 stalls at the low end and LTO
disappoints, the realistic landing zone is **95–105 M** (170–185 fps at 400%), and the plan's
escape hatch (publish the reachable number) becomes the honest close — but per the plan's
evidentiary floor, only after ranks 1–5 above have landed, measured branches.

**Is the top cost fuse_z80 opcode dispatch itself?** No. `fuse_z80_execute_one` +
opcode-table work is only ~6% self. The interpreter *fabric around it* (wrapper sync, virtual
Mmu, callbacks, per-instruction subsystem stepping) is 4–6× larger than the opcode dispatch —
Phase C's targets are the right ones; a core rewrite is not the binding constraint at this
target.

## 6. Ranked outcomes and kill list

### 6.1 Promoted (High)

1. **C-IM2 (NEW)** — 14.1% on the target workload. Highest measured share of any candidate.
   Risk: Med (interrupt timing exactness; `im2_test`/`ctc_interrupts_test` + FUSE are gates).
2. **C3+C-DIV as one branch** — 8.2%, and the risk is *very low* (pure hoist of an existing
   gate; the 3.5 MHz path keeps identical semantics — Nirvana/BIFROST + FUSE gate it).
3. **C1** — 7.0% on boot-nextzxos, and 27–32% of every 3.5 MHz-mode frame (48K/128K/+3
   modes at stock speed benefit ~1.3×). Low risk, VHDL-faithful accumulator exists in-repo.
4. **C10+glue** (register sync + std::function → fn-ptr) — 11.4% combined, mechanical.

### 6.2 Kept (Med / conditional)

5. **C-M1 (NEW)** — 7.7%; needs care (DivMMC automap correctness is boot-critical).
6. **C6 render-skip** — real for GUI 400%; define the bench semantics first (see §5 note).
7. **B1 LTO** — try once, measure; cheap to attempt, uncertain payoff.
8. **C9 copper div/mod** — ~0 on boot-nextzxos, but **+13 points of run_frame self on
   copper-demo and +9 on beast**; it is the main reason Next *games* sit at 43–46 M. Required
   if Task 27's goal is 400% on real content, optional for the boot-nextzxos DoD number.
9. **C5 audio-at-speed** — 3.7% (Next) / 11.6–12.1% (48K modes). Small for the DoD target;
   worthwhile only bundled with C6's "undisplayed frame" concept.

### 6.3 Killed (measured < ~2% on the target workload, or refuted)

- **C11 HALT fast-forward — KILLED** as a 48K-penalty fix, on the §3.1 density-controlled
  evidence (revised — the original bifrost control was invalid): the idle 48K prompt is not
  HALT-bound (8.36 T/dispatch), so HALT fast-forwarding would not engage on it; an extreme
  density improvement recovers ≤ ~24% of the gap on a mode that already exceeds its 400%
  target; and it carries Med interrupt-timing risk. The underlying ≈ 51 ns/dispatch overhead
  is better attacked head-on by C-IM2 / C10+glue / C-M1 (§5 ranks 1, 4, 5).
- **C4 port-dispatch popcount — KILLED.** `PortDispatch::read/write` peaks at **0.84%**
  (boot-nextzxos) and is ≤ 0.35% everywhere else.
- **C7 tilemap hoist — KILLED for the DoD target** (0% on boot-nextzxos; 4.3% on beast).
  Re-open only under a beast/copper-focused goal alongside C9.
- **C8 compositor switch-hoist — KILLED for the DoD target** (3.1% on boot-nextzxos but the
  bulk of `composite_scanline`'s 10.6% share lives in 48K modes, which already meet 400%).
  Cheap and safe if someone wants it as a simplification, but it is not gap-closing.
- **C2 (poisoned prior art)** — already dead per plan §0.3; the profile confirms the *gate*
  must stay live (contention is real work at 3.5 MHz in Next mode too — exp2-3.5 pays 5.6%).
- **TraceLog residue** — 0.1–0.7%; nothing to do, A2 verified.

### 6.4 A finding the plan must absorb

The DoD workload is the *easiest* Next workload measured. copper-demo (46.0 M) and beast
(43.0 M) need **2.5–2.6×**, not 1.55×; their extra cost is C9 (copper master-cycle div/mod)
plus higher render/copper duty. Hitting 113.5 M on boot-nextzxos while games run at ~80 fps
at 400% is a true-but-misleading success; the plan should either add a second DoD line for a
copper-active workload or state explicitly that 400% is a boot-workload metric.

**Frame-geometry caveat on beast's ratio.** beast reports `tstates_per_frame=481536`
(456 pixel ticks × 264 lines × 4 master cycles/tick), not boot-nextzxos's 567,264 (311-line
50 Hz frame). Root cause: **beast itself writes NR 0x05 = 0x04** — bit 2 set = 60 Hz —
observed directly (`--log-level nextreg=trace`: `NextREG write reg=0x05 val=0x04` during NEX
startup), which switches the video timing to the 264-line 60 Hz frame. Its T-states/s figure
is therefore earned on **~15% smaller frames at a 20% higher frame rate** than the DoD
workload; the "needs 2.6×" ratio above compares T-state *throughputs*, which remains valid
as a throughput statement, but the *fps* needed for beast at 400% is 240 (60 Hz × 4), not
200 — the geometry difference should be kept in mind before quoting beast beside
boot-nextzxos as if they shared a frame clock.

---

## 7. Reproduction

```bash
# baseline (median-of-5, locked, core-derived):
make bench                                # → test/bench/baseline-732214d1.txt
# profiles (lock held, box idle, core 0):
perf record -F 4000 -e cycles:u --call-graph dwarf,16384 -o <wl>.data -- \
  taskset -c 0 build/gui-release/jnext --headless --machine <m> \
  --sdcard roms/nextzxos-1gb-fat32fix.img [--load <asset>] \
  --benchmark <600|400> --benchmark-label <wl>
perf report --stdio --no-children -g none --percent-limit 0.1 -i <wl>.data
# discriminator exp2 binaries (16 bytes, org 0x8000, --machine next --inject-delay 20):
#   F3 ED 91 07 {00|03} 21 00 90 06 00 7E 23 10 FC 18 F5
# density exp3 binaries (org 0x8000, --machine 48k --inject-delay 150 --benchmark 1800):
#   busy48 : F3 21 00 90 06 00 7E 23 10 FC 18 F5      (8.67 T/dispatch)
#   nopspin: F3, 200x 00, C3 01 80                     (4.03 T/dispatch)
#   exspin : F3 31 00 90, 200x E3, C3 04 80            (18.96 T/dispatch)
```
