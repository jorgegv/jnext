# Task 27 — Final Optimization Report

## 1. Executive summary

- **Scope:** two waves — W1 profile-guided hot-loop fixes; W2 LTO + copper/video hot-spots.
- **Result (boot-nextzxos):** 57.4M → 114.8M → **161.0M T-states/s = +180%** end-to-end (≈2.8×).
- **At 400% speed (Next):** 101 → 202 → **284 fps** — DoD was 200, so **+42% margin**.
- **Biggest lever:** LTO (a 17-line CMake change) alone beat all hand-tuned hot-spots combined.
- **Quality:** every change independently reviewed (7 REJECT rounds, all real bugs); regression
  stayed 0-pixel-diff throughout.

```
boot-nextzxos T-states/s:  57.4M  ──Wave 1 (+100%)──▶  114.8M  ──Wave 2 (+40%)──▶  161.0M
400% fps (Next):            101         ▶                202          ▶              284   (DoD: 200)
```

## 2. Initial performance baseline (Task 27 start)

Settled P-core (4.9 GHz), median-of-5, `--benchmark`, T-states/sec (the only cross-CPU-speed metric):

| workload                     | start | note                                              |
|------------------------------|------:|---------------------------------------------------|
| boot-nextzxos (Next, 28 MHz) | 57.4M | the OS/games path; 400% target = 113.5M / 200 fps |
| copper-demo (Next)           | 36.7M | copper-heavy                                      |
| beast (Next, 60 Hz)          | 34.6M | tilemap + copper + layers                         |
| bifrost (48K, 3.5 MHz)       | 26.7M | busy-48K raster witness                           |
| boot-48k                     | 25.9M | HALT-idle; persistently spread-VOID               |

Symptom that opened the task: at 400% speed the emulator reached only ~75 fps (100% CPU) instead
of the expected 200 — the loop was doing far more per instruction than the guest demanded.

## 3. Optimization areas identified (after profiling)

- **Rewind buffer** eagerly memset 1.09 GB and took a 2.29 MB full-machine snapshot every frame —
  for a feature off by default.
- **Per-instruction device fan-out**: CTC/UART/IM2 ticked in `O(master_cycles)` loops — 8× worse
  per T-state at 3.5 MHz than at 28 MHz.
- **Per-bus-cycle integer divisions** (`derive_hc_vc`, copper `tick`): ~5 divisions recomputed to
  learn the answer was usually 0.
- **No LTO**: 14 static libs ⇒ every CPU→`Mmu::read`/`PortDispatch` was an un-devirtualisable
  cross-TU vtable call; nothing inlined across the boundary.
- **Video per-pixel redundancy**: compositor re-dispatched a per-line-constant `switch` 163,840×/frame;
  tilemap re-decoded each tile every pixel.
- **Build hygiene**: the default dev binary was silently `-O0` (5.8× slow) and CLAUDE.md pointed
  measurements at it.

## 4. Optimizations tried (in application order)

Gains are rough (median of interleaved A/B on the target workload; Wave-2 inline A/Bs mostly VOIDed
under the parallel-agent box load and were confirmed post-hoc from the cumulative bench + reviewer
reproductions). "broad" = helps all workloads.

| # | Subsystem | Optimization (≤20 words) | State | Reject reason | Gain (rough) |
|---|---|---|---|---|---|
| T0 | build | Default `CMAKE_BUILD_TYPE=RelWithDebInfo`; frame-pointers RelWithDebInfo-only | approved | — | 5.8× for `build/` users (infra, not counted) |
| A1 | core/rewind | Rewind buffer opt-in (default off); lazy `mmap` instead of eager 1.09 GB memset | approved | — | +30% broad, −1.07 GB RSS |
| A2 | core/debug | Decouple per-instruction trace log from rewind-enable | approved | — | subset of A1 |
| C-IM2 | cpu/im2 | Quiescent early-out when no IM2 device is armed | approved | — | +16% |
| C3+C-DIV | memory/cpu | Hoist contention early-out above raster math; division-free `derive_hc_vc` | approved | — | +9% |
| C1 | ctc/uart | Replace `O(master_cycles)` tick loops with accumulator (event-horizon) | approved | — | +73% boot-48k / +13% nextzxos |
| C-M1 | cpu | Gate M1-cycle callback chain on live flip-flops | approved | — | +5% |
| C6 | core/gui | Skip `render_frame()` for frames the frontend never displays (turbo only) | approved | — | −6% GUI frame time @400% |
| C11 | cpu | Fast-forward HALT to next scheduled interrupt | **rejected** | interrupt-timing risk; profile showed low residual | — |
| **B1** | **build** | **LTO/IPO on Release only (17-line CMake, gracefully degrading)** | **approved** | — | **+34.5% nextzxos / +17.4% bifrost** |
| C10 | cpu/core | Drop 2 `Z80Registers` by-value copies + dead `on_contention` store; gate test-only advance | approved | — | ~0 (behavior-neutral cleanup) |
| C9 | video/copper | Copper per-master-cycle div/mod → incremental counters (29M-trial proof) | approved | — | ~4–6% copper/beast |
| C8 | video/compositor | Hoist per-pixel `switch(layer_priority_)` to per-scanline template dispatch | approved | — | ~2.5% copper / ~5% beast |
| C7 | video/tilemap | Memoise per-tile work (index/attr/decode) out of the per-pixel loop | approved | — | ~5% beast |
| C10-2 | core | Hoist per-instruction `std::getenv` guard | skipped | already a function-local `static` (no per-call cost) | — |
| C10-5 | cpu/fuse | Reduce 21-field FUSE register sync | skipped | too risky; FUSE is the correctness gate, no safe partial | — |
| — | video/timing | `VideoTiming::advance` per-instruction divisions (9% on copper-demo, newcomer) | deferred | found late by P2; cross-cutting, out of Wave-2 scope | (open) |

## 5. Cumulative result (Wave-2 complete, `baseline-4db0c943.txt`, spread-valid)

| workload | start | Wave-1 end | Wave-2 end | end-to-end | own 400% target | met? |
|---|---:|---:|---:|---|---|---|
| boot-nextzxos | 57.4M | 114.8M | **161.0M / 284 fps** | **+180%** | 113.5M / 200 fps | **YES (+42% margin)** |
| copper-demo | 36.7M | 62.2M | **90.1M / 159 fps** | +145% | 113.5M | no (1.26× to go) |
| beast (60 Hz) | 34.6M | 55.9M | **81.0M / 168 fps** | +134% | 115.6M / 240 fps | no (1.43× to go) |
| bifrost (48K) | 26.7M | 58.0M | **72.9M** | +173% | 14.0M | yes (5.2×) |
| boot-48k | 25.9M | ~58M | ~74M (VOID) | ~+186% | 14.0M | yes |

## 6. Conclusions

- **DoD exceeded**: boot-nextzxos runs 284 fps at 400% (target 200), with a full-triplet-green,
  0-pixel-diff codebase. The OS/games path has ample headroom.
- **copper-demo/beast still miss their own 400% extrapolation** (1.26×/1.43× short). P2 profiling
  proved this is expected: Track A (C7/C8/C9) is margin, not closure — the residual is the
  interpreter fabric plus Copper's own irreducible WAIT/MOVE work. Reaching 400% there needs a
  deeper core change, not more hot-spot tuning.
- **One config change (LTO) > all hand tuning**: +34.5% from a 17-line CMake edit vs ~+4–7% from
  the entire Wave-2 hot-spot campaign on the same workload.

## 7. Lessons learnt

- **Turn on LTO before hand-optimizing.** Cross-TU devirtualization is a compiler's job; it dwarfed
  a week of hot-spot work and made much of it redundant.
- **Measure, never predict.** Multiple confident predictions were refuted by measurement (frame-pointer
  cost 1%→5.8%; C7/C8 "killed" for the DoD were real wins for copper/beast; fps is not comparable
  across guest CPU speeds — only T-states/sec is).
- **Independent review is not optional.** 7 REJECT rounds caught real, shippable bugs an author's
  self-review would have missed (silent-resume gate holes, dispatcher shadow-state stomp, a false
  "I fixed the caller" commit message, dropped collision bits on skipped frames).
- **Serialize measurement, parallelize development.** Interleaved A/B under an exclusive lock cancels
  steady load but not erratic load; run the authoritative bench on an idle box.
- **Prove equivalence for hot-path rewrites.** The riskiest change (C9) shipped only after a
  29-million-trial arithmetic equivalence proof with a negative control — not just a green triplet.
