# Phase 9: Performance Profiling & Optimization Plan

## Context

Originally planned for Phase 7.8, this work is deferred to Phase 9 to run after the general code audit, refactor, and reorganization. Profiling clean code gives more meaningful results, and refactoring often eliminates inefficiencies naturally before targeted optimization begins.

The emulator works correctly across all machine types with full regression test coverage. The goal is to understand performance characteristics and make targeted improvements -- measure first, then optimize the biggest bottlenecks.

## Phase A: Measurement Infrastructure (no optimization yet)

### A1. Headless benchmark mode (`--benchmark N`)
- **Files**: `src/platform/headless_app.h`, `src/platform/headless_app.cpp`, CLI arg parser
- Run N frames (default 5000 = 100s emulated), print: wall time, speed ratio, effective MHz, avg ms/frame
- Example output: `Benchmark: 5000 frames in 2.34s — 2136x real-time (106.8 MHz effective)`

### A2. Built-in per-frame profiler (`--profile`)
- **File**: new `src/core/frame_profile.h` (header-only), modify `src/core/emulator.cpp`
- 7 timing zones via `steady_clock`: CPU exec, contention, tape, copper, audio, scheduler, video rendering
- Print summary every 250 frames (5s)

### A3. External profiling baseline
- Build with `RelWithDebInfo` (`-O2 -g`)
- Run `perf stat` and `perf record -g` on `--headless --benchmark 5000`
- Document baseline numbers

## Phase B: Low-Risk Optimizations

### B1. Compiler flags
- **File**: `CMakeLists.txt`
- Release: `-O2 -march=native -flto -DNDEBUG`
- `-O2` over `-O3` (emulators benefit from smaller code / better icache)

### B2. Scheduler fast-path
- **File**: `src/core/emulator.cpp` line ~1241
- Inline `next_cycle() <= clock` check before calling `run_until()` (saves function call ~70K times/frame when no events due; events fire only ~320 times/frame)

### B3. Conditional layer buffer clearing
- **File**: `src/video/renderer.cpp` lines ~42-45
- Skip clearing disabled layers (saves ~1MB writes/frame when layers are off)

### B4. Compositor switch hoisting
- **File**: `src/video/renderer.cpp` composite function
- Move `switch(layer_priority_)` outside the 320-pixel loop -- dispatch once per scanline to specialized per-mode functions

## Phase C: Medium-Risk Optimizations (careful testing needed)

### C1. Register sync elimination (biggest expected win)
- **File**: `src/cpu/z80_cpu.cpp`
- Currently: `sync_fuse_from_regs` + `sync_regs_from_fuse` on every instruction = ~2.7M field copies/frame
- Fix: dirty-flag pattern. Only sync when registers are externally accessed (debugger, Z80N opcodes)
- Caveat: Z80N opcode path must still sync

### C2. Contention callback null for non-contended machines
- **File**: `src/cpu/z80_cpu.cpp`
- Pentagon/Next don't have contention -- set callback to nullptr to skip check entirely

## Phase D: Profile-Guided (only if profiling reveals as hot)
- SIMD compositing (SSE2/AVX2)
- AY chip tick batching
- Memory layout for cache friendliness

## Verification

- Run `test/regression.sh` before and after each change
- FUSE Z80 opcode tests critical for register sync changes (C1)
- Benchmark with `--headless --benchmark 5000` before/after each optimization
- 3 runs, take median for reproducibility

## Implementation Order
A1 -> A2 -> A3 -> B1 -> B2 -> B3 -> B4 -> C1 -> C2 -> (D: only if needed)
