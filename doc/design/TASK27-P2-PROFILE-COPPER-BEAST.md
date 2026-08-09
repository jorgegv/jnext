# Task 27 P2 — Copper-demo / Beast Re-Profile (Track A evidence)

> **Status:** measurement report, 2026-07-15. Branch `task27-p2-profile`, off `main` @ `81e624a1`
> (Wave-1 complete). **No emulator code was changed by this task.** It scopes the Wave-2
> Track A candidates **C9 / C7 / C8** to the two workloads P1 explicitly did *not* clear them
> against: `copper-demo` and `beast`. P1's kill of C7/C8 was DoD-scoped (boot-nextzxos) and
> does not transfer here.

---

## 1. Methodology

| item | value |
|---|---|
| Host | AMD Ryzen AI 5 340 (hybrid P/E), Fedora 44, kernel 7.0.14-201 |
| Core pinning | **core 0 @ 4,900,000 kHz** (fastest class, lowest-numbered — bench.sh derivation) |
| Binary | `build/gui-release/jnext`, `CMAKE_BUILD_TYPE=Release` (`-O2 -DNDEBUG`, **no** `-g`, not stripped) |
| perf | 7.1.3-201.fc44; `perf record -F 4000 -e cycles:u --call-graph dwarf,16384`, exclusive `flock` on `test/bench/.lock` |
| Sampling scope | `cycles:u` only (`perf_event_paranoid=2`). Kernel time invisible; "other" ≤ 0.3% — negligible |
| Samples | copper-demo 14,679 · beast 13,783 (one pass each) |
| Representativeness | the profiled runs' BENCH lines (copper-demo 109.7 fps, beast 116.5 fps) match the official baseline (`baseline-db3f2136.txt`: 109.6 / 116.1) within noise — the profiles are of the same machine state the bench measures |

**Attribution caveat.** Release has no `.debug_line`, so `perf annotate`/`addr2line` yield no
source lines. Per-instruction attribution below is by **symbol + byte offset**, cross-referenced
against `objdump -d` of the exact binary. The three candidate divide instructions were located
in the disassembly and their sample skid summed by offset range (x86 retirement skid lands the
sample a few bytes past the slow op — accounted for).

> **Lambda-thunk attribution correction (2026-08-09).** Everywhere below that this report
> names a `lambda#N` thunk, it named the **wrong callback** and read the number as dispatch
> cost. `lambda#5` is **`cpu_.on_m1_prefetch`** and `lambda#1` (the `(uint16_t, uint8_t)` one)
> is **`cpu_.on_m1_cycle`** — verified by disassembly of the v0.99.144 release binary. Neither
> has anything to do with `VideoTiming::advance` or with contention, and neither number is
> `std::function` overhead: with `-O2` the callback body is inlined *into* the `_M_invoke`
> thunk, so the thunk's self-time **is** the callback's work. Full evidence, and the reason
> this misreading is easy to make, in
> [TASK27-PROFILE-REPORT.md §4.1 and §1 caveat (d)](TASK27-PROFILE-REPORT.md). The affected
> rows below are annotated in place; **the measurements are unchanged, only their labels.**

`tick_devices_after_instruction` and `Copper::execute` are the modern (post-Wave-1) symbols;
P1's `Emulator::run_frame (self)` copper attribution now lives inside these two. The C9 loop
body (`tick_copper_for_master_cycles`, `emulator.cpp:7821-7859`) is **inlined into**
`tick_devices_after_instruction`; it is offsets **0x40–0x89** of that symbol, bounded in the
disassembly by the loop back-edge `jb …+0x40` at +0x89 and the `call Copper::execute` at +0x81.

---

## 2. Baseline (median-of-5, `test/bench/baseline-db3f2136.txt`)

| workload | machine | fps | T-states/s | ms/frame | own 400% target | gap |
|---|---|---:|---:|---:|---|---:|
| copper-demo | next @ 28 MHz, 311-line 50 Hz | 109.6 | 62.2 M | 9.12 | 113.5 M / 200 fps | **1.83×** |
| beast | next @ 28 MHz, **264-line 60 Hz** (NR 0x05=0x04) | 116.1 | 55.9 M | 8.58 | 115.6 M / **240 fps** | **2.07×** |

---

## 3. Hot-function ranking (self-cost % of the whole run)

### 3.1 copper-demo

| rank | symbol | self % | Track-A id | note |
|---:|---|---:|---|---|
| 1 | `Emulator::tick_devices_after_instruction` | **14.05** | — | per-instruction device fan-out; contains the C9 loop (6.22) + CTC/UART/NMI/Mixer/Clock dispatch (7.8) |
| 2 | `Z80Cpu::execute` | 12.05 | — | interpreter fabric |
| 3 | `Copper::execute` | **8.35** | *(not C9)* | the Copper's **own** WAIT/MOVE work — irreducible; C9 does not touch it |
| 4 | `VideoTiming::advance` | **6.72** | **NEWCOMER** | per-instruction ULA hc/vc advance; **two `idiv` inside** (0x4bb080, 0x4bb130) |
| — | *(of #1)* **C9 copper div/mod loop body, +0x40..0x89** | **6.22** | **C9** | `divq` +0x61, `idiv` +0x78 per master cycle |
| 5 | `Emulator::step_one_instruction` | 6.14 | — | |
| 6 | ~~per-cycle video-timing dispatch `lambda#5` glue~~ **→ `cpu_.on_m1_prefetch` body** | 5.77 | ~~(Track B)~~ **C-M1** | **Corrected 2026-08-09:** not a thunk into `VideoTiming::advance` (that is a direct member call, never behind a `std::function`) and not dispatch cost — it is the `on_m1_prefetch` callback's own DivMMC-automap / Multiface M1 work, inlined into the thunk |
| 7 | `sync_regs_from_fuse` | 3.61 | (Track B / C10) | |
| 8 | `Mixer::accumulate` | 3.52 | — | |
| 9 | `UartChannel::tick` | 3.02 | (C1, shipped) | |
| 10 | ~~contention `lambda#1` glue~~ **→ `cpu_.on_m1_cycle` body (IM2 RETI/RETN decoder)** | 2.86 | ~~(Track B)~~ **C-IM2** | **Corrected 2026-08-09:** contention is reached through a plain global pointer (`z80_set_contention_runtime`, `z80_cpu.cpp:1336`) — there is **no contention `std::function` thunk in the binary at all** |
| 11 | **`Renderer::composite_scanline`** | **2.45** | **C8** | per-pixel `switch(layer_priority_)` |
| 12 | `Ctc::tick` | 2.43 | (C1, shipped) | |
| — | `Tilemap::render_scanline` | **~0** (< 0.3) | **C7** | **copper-demo has no active tilemap — C7 is inert here** |

### 3.2 beast

| rank | symbol | self % | Track-A id | note |
|---:|---|---:|---|---|
| 1 | `Emulator::tick_devices_after_instruction` | **15.69** | — | contains C9 loop 6.57 + fan-out 9.1 |
| 2 | `Copper::execute` | **12.26** | *(not C9)* | beast's Copper program is heavy — irreducible own work |
| 3 | `Z80Cpu::execute` | 8.68 | — | |
| — | *(of #1)* **C9 copper div/mod loop body, +0x40..0x89** | **6.57** | **C9** | |
| 4 | ~~per-cycle video-timing `lambda#5` glue~~ **→ `cpu_.on_m1_prefetch` body** | 5.70 | ~~(Track B)~~ **C-M1** | corrected 2026-08-09, as §3.1 rank 6 |
| 5 | **`Tilemap::render_scanline`** | **5.48** | **C7** | per-pixel `%wrap_x`, `/8`, `%8`, 2× `vram_read` (`tilemap.cpp:429-512`) |
| 6 | **`Renderer::composite_scanline`** | **5.15** | **C8** | |
| 7 | `Emulator::step_one_instruction` | 4.63 | — | |
| 8 | `fuse_z80_readbyte_raw` | 4.30 | — | |
| 9 | `Mixer::accumulate` | 3.24 | — | |
| 10 | `VideoTiming::advance` | **2.37** | **NEWCOMER** | |
| 11 | `contend_read` | 2.14 | (C3, shipped) | |
| 12 | `sync_regs_from_fuse` | 2.03 | (Track B) | |
| 13 | `Layer2::render_scanline` | 1.53 | — | |

---

## 4. C9 / C7 / C8 verdict — they ARE real hot spots here (unlike on the DoD workload)

Combined self-cost across the two Track-A workloads:

| id | site | copper-demo | beast | **combined** | risk (plan) |
|---|---|---:|---:|---:|---|
| **C9** | copper per-master-cycle div/mod loop, `emulator.cpp:7836-7857` | **6.22** | **6.57** | **12.79** | Med |
| **C8** | compositor per-pixel `switch`, `renderer.cpp:627,789` | 2.45 | 5.15 | **7.60** | Low |
| **C7** | tilemap per-pixel work, `tilemap.cpp:429-512` | ~0 | 5.48 | **5.48** | Med |

**C9 is confirmed as copper-demo's single largest strength-reducible lever and is material on
beast too.** The loop runs the copper body once per master cycle whenever the Copper is running
(most of every frame in both demos), and each iteration executes a 64-bit `divq` (elapsed / and %
`master_cycles_per_line` → `vc`,`hc`) plus a 32-bit `idiv` (`% lines_per_frame` → `cvc`). The
`idiv` skid alone is the hottest single offset in the whole `tick_devices` symbol (+0x7a = 2.84%
copper-demo / 2.63% beast). All three divides are trivially replaceable by incremental counters
(the loop steps `at` by exactly 1 each iteration: `hc++` with wrap, `vc++` on wrap, `cvc` derived)
— a mechanical, semantics-preserving rewrite. Realistic reclaim ≈ 4–6% per workload (the divides
themselves plus surrounding load/setup; the `call Copper::execute` inside the loop stays).

**C8 (Low risk) helps both workloads** (2.45 + 5.15). **C7 (Med risk) is beast-only** — copper-demo
has no active tilemap, so C7 does nothing there; do not measure C7 against copper-demo.

### Recommended Track-A implementation order

**C9 → C8 → C7**, justified by combined measured share and risk:

1. **C9 first** — largest combined lever (12.8%), benefits *both* workloads, purely arithmetic
   (counter substitution). It edits the `emulator.cpp` inner loop, so per plan §"Serialise …"
   it must land before/rebase-against any other inner-loop branch (C10) and re-measure.
2. **C8 second** — Low risk, cross-workload (7.6% combined), no inner-loop-serialisation conflict
   with C9 (different file, `renderer.cpp`).
3. **C7 last** — Med risk, beast-only (5.5%). Gate its before/after on **beast**, not copper-demo.

---

## 5. Honest bound — Track A improves margin but does NOT reach 400% on these workloads

Summing the full C9+C8+C7 self-cost as an **upper bound** on what Track A can remove (it cannot
remove all of it — `Copper::execute`'s own 8–12% and the interpreter fabric are untouched):

| workload | C9+C8+C7 self | best-case speed-up if fully eliminated | gap to own 400% |
|---|---:|---:|---|
| copper-demo | 8.67% | ×1.095 → ~120 fps | needs ×1.83 — Track A closes ~**12%** of it |
| beast | 17.20% | ×1.208 → ~140 fps | needs ×2.07 — Track A closes ~**19%** of it |

The residual is the same floor P1 named for boot-nextzxos, and it is **larger** on games:
`Z80Cpu::execute` + `tick_devices` fan-out + `step_one_instruction` + `sync_regs` + the two
per-instruction M1 callback thunks (`on_m1_prefetch`, `on_m1_cycle` — see the §1 correction;
they are callback *bodies*, not dispatch) + `fuse_z80_*` + `Mmu::read` + `contend_read`, **plus**
`Copper::execute`'s own irreducible WAIT/MOVE work (8.35% copper-demo, **12.26% beast**). Nothing
in C7/C8/C9 restructures that block. **Reaching 400% on copper-demo/beast is not achievable with
Track A alone** — it would need the deeper fabric work (LTO/IPO, dispatch-glue devirtualisation,
or a core change), and even then `Copper::execute`'s own cost caps beast. Track A's honest value
is **margin**, not clearing the games' 400% bars.

## 6. Newcomer P1 did not surface: `VideoTiming::advance`

`VideoTiming::advance(int)` (`src/video/timing.cpp:91`) is **6.72% on copper-demo** (larger than
both C8 and C7) and 2.37% on beast — combined **9.1%**, the biggest un-listed lever on the copper
workload. It is called **per instruction** ~~through a `std::function` thunk (`lambda#5`, itself
5.77% / 5.70%)~~ — **corrected 2026-08-09: as a direct member call** (`emulator.cpp`,
`video_timing_.advance(tstates)` in `step_one_instruction`); it was never behind a
`std::function`, and `lambda#5` is `on_m1_prefetch`, an unrelated callback. `advance()`
contains **two `idiv` instructions** — the same divide-per-tick anti-pattern
as C9 and the shipped C3+C-DIV, but on a *different* code path (the ULA `hc/vc` line-int counter,
not the contention shim). P1 bucketed it into "video render" and never isolated it.

**It is not a Track-A (copper/beast) item** — it is cross-cutting (fires on every Next workload,
including boot-nextzxos) and belongs with **Track B / a C-DIV-family follow-up**: strength-reduce
its `% lines_per_frame` ~~and collapse the per-instruction `std::function` thunk to a direct
call~~ *(second half struck 2026-08-09 — there is no thunk to collapse; the call is already
direct)*.
Flagging it because on copper-demo it outweighs the entire C7+C8 pair. Recommend a dedicated
before/after on boot-nextzxos + copper-demo before committing to it.

---

## 7. Reproduction

```bash
flock -x test/bench/.lock -c '
perf record -F 4000 -e cycles:u --call-graph dwarf,16384 -o cd.data -- \
  taskset -c 0 build/gui-release/jnext --headless --machine next \
  --sdcard roms/nextzxos-1gb-fat32fix.img --load test/00regression/nex/copper_demo.nex \
  --benchmark 400 --benchmark-label copper-demo'
# (beast: swap the --load to test/00regression/nex/beast.nex)
perf report --stdio --no-children -g none --percent-limit 0.3 -i cd.data     # flat self-cost
perf report --stdio -n --no-children -g none --sort symoff -i cd.data \
  | grep tick_devices_after_instruction                                       # per-offset (C9 = +0x40..+0x89)
objdump -d -C build/gui-release/jnext | grep -A400 tick_devices_after_instruction # locate divq/idiv
```

---

## 8. Final decision — Task 65 (2026-07-17): **WONT**

Re-measured live on the same host (core-0 pinned, machine quiet), current `main`:

| workload | own 400% target | Wave-2 report | **now** | short by |
|---|---|---:|---:|---:|
| copper-demo | 200 fps | 159 fps | **163.5 fps** (92.7M T/s) | **1.22×** (~327% eff.) |
| beast (60 Hz) | 240 fps | 168 fps | **178.0 fps** (85.7M T/s) | **1.35×** (~297% eff.) |

The modest gain over the Wave-2 report is because `VideoTiming::advance` was since **gated out of the
free-running loop** (`emulator.cpp` — `if (debug_state_.active()) video_timing_.advance(...)`), removing
§6's 6.7%-copper-demo newcomer ~~**and** its per-instruction `std::function` thunk (lambda#5, ~5.8%)~~.
That was the single largest safe lever §5/§6 flagged, and it is now spent.

> **Corrected 2026-08-09.** The struck clause is wrong twice over, and the gain it explains is
> correspondingly smaller. `VideoTiming::advance` never had a `std::function` thunk — it is a
> direct member call — so the gate removed **only** the 6.7% / 2.4% of `advance` itself, not a
> further ~5.8%. And **`lambda#5` is `cpu_.on_m1_prefetch`, which is still live and still runs
> once per M1 fetch** in today's binary (`{lambda(unsigned short)#5}::_M_invoke` at `0x4750b0`,
> v0.99.144). Its cost was never `advance`'s to give up, and gating `advance` could not and did
> not remove it. The WONT verdict is unaffected — the re-measured 163.5 / 178.0 fps figures are
> the *outcome*, measured directly, and do not depend on this explanation.

### Ways forward evaluated (and why none is taken)

| option | effort | risk | expected gain | reaches 400%? |
|---|---|---|---|---|
| ~~Devirtualise the remaining per-cycle **contention** thunk (Track B, lambda#1)~~ — **void, see the 2026-08-09 correction**: there is no contention thunk; `lambda#1` is `on_m1_cycle`, and devirtualising it is worth ≈ 0.5% at most, not 2–3% | Med | Med (hot loop + FUSE callback boundary) | **≲0.5%** *(was "~2–3%")* | No |
| Faster/alternative Z80 core (internal VHDL-derived core, INTERNAL-Z80N-CORE-PLAN) | Weeks | High | **Negative** — cycle-accurate microcode stepping is ~4× *slower* per that plan | No (hurts) |
| Z80N JIT | 6–12 mo | High | ~10%, fragmented by contention/interrupts | No (already rejected) |
| Cycle-accurate rewrite | Months | Very high | Negative — more work per cycle | No (counterproductive) |

Even summing every remaining **safe** lever, best case is ≈ +15%: copper-demo grazes ~190 fps (near its
200 target), beast reaches ~200 fps — **still short of 240**. beast cannot reach 240 without making the
Copper do less work, which is impossible without breaking accuracy (`Copper::execute`'s own 8%/12% WAIT/MOVE
cost is irreducible — it is the emulation of what the demo asks the hardware to do).

### Verdict

**NOT WORTH IT.** User impact is negligible — both demos run flawlessly at 100% real-time; the shortfall is
confined to *fast-forward on the two heaviest demos*, where they still achieve ~3×, and every other workload
(boot-nextzxos 284 fps, ordinary games) already exceeds 400%. The only remaining safe lever
(**≲0.5%** after the 2026-08-09 correction — stated as "~2–3%" above; the verdict only hardens) does not
close the gap, and every gap-closing option is a months-long, high-risk core change with uncertain or
*negative* payoff. Marked **WONT**; no code change. This closes Task 65.
