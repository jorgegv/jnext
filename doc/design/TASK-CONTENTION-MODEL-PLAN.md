# Task — Contention Model SKIP-Reduction Plan

Plan authored 2026-04-23 as part of the ULA Phase 4 closure. Captures
the 12 rows re-homed from `test/ula/ula_test.cpp` §11 that test
memory + I/O contention behaviour. Contention is a machine-wide
subsystem (affects CPU T-state counting across 48K / 128K / +3 /
Pentagon / Next timing regimes) that has no dedicated class or test
suite in jnext today — the logic is spread across ad-hoc checks in
the CPU / MMU / IoPortDispatch paths.

## Starting state

- No `ContentionModel` class exists.
- No `test/contention/` suite exists.
- Contention effects are currently ignored (or approximated) in the
  CPU's T-state accounting, which means any program that relies on
  exact contention timing (most real-hardware Spectrum demos) runs
  faster than hardware.
- 12 ULA plan rows re-homed with `// RE-HOME:` comments in
  `test/ula/ula_test.cpp` §11 pointing at this plan.

## Rows inherited from ULA plan

| Row ID | Machine | Scope | VHDL cite |
|---|---|---|---|
| S11.01 | 48K | Bank-5 contention phase | `zxula.vhd:582-583` |
| S11.02 | 48K | Bank-0 non-contended | `zxnext.vhd:4489` |
| S11.03 | 48K | `hc_adj(3:2)="00"` non-contended phase | `zxula.vhd:582` |
| S11.04 | 48K | `vc>=192` `border_active_v` | `zxula.vhd:582` |
| S11.05 | 48K | Even-port I/O contention | `zxnext.vhd:4496` |
| S11.06 | 48K | Odd-port I/O | `zxnext.vhd:4496` |
| S11.07 | 128K | Bank-1 odd-bank contention | `zxnext.vhd:4491` |
| S11.08 | 128K | Bank-4 non-contended | `zxnext.vhd:4491` |
| S11.09 | +3 | Bank≥4 `WAIT_n` contention | `zxula.vhd:600` |
| S11.10 | +3 | Bank-0 non-contended | `zxnext.vhd:4493` |
| S11.11 | Pentagon | Never-contended | `zxnext.vhd:4481` |
| S11.12 | Next (turbo) | CPU speed >3.5 MHz disables contention | `zxnext.vhd:4481` |

## Approach (phased, TBD when picked up)

- **Phase 0 — design**: Should contention live as a dedicated
  `ContentionModel` class (with per-machine strategy objects) or as
  a module integrated into `Emulator` next to the CPU tick loop? The
  VHDL treats it as combinational logic on `bank_sel`, `hc_adj`, and
  `vc` — translating to C++ this becomes a small per-machine
  lookup/mask called from the CPU's memory-access path. Similar to
  VideoTiming in shape.
- **Phase 1 — scaffold**: Create `src/cpu/contention.{h,cpp}` (or
  `src/peripheral/contention.{h,cpp}`) with a `ContentionModel`
  class. Add a `contention_model_` member to `Emulator`. Wire the
  CPU mem-access + I/O-port paths through a cheap
  `contention_wait(bank, hc, vc)` call that returns additional
  T-states.
- **Phase 2 — implementation waves**: 3–4 agents can parallelise per
  machine (48K, 128K/+3, Pentagon, Next-turbo).
- **Phase 3 — un-skip**: flip the 12 ULA re-homes back to check()s
  living in the new `test/contention/` suite.
- **Phase 4 — audit**: refresh dashboards, validate no screenshot
  regression (contention affects frame timing which compositor
  screenshots depend on).

## Scope + risk

- ~20-40 hours of work given the per-machine complexity and the
  need to re-run all screenshot-regression references after the CPU
  timing model changes.
- **Blocks nothing critical today** — jnext works without contention
  for NextZXOS boot, game loading, and most demos. Contention-
  sensitive demos (classic 48K timing tricks) are a cosmetic
  correctness gap.

## Open questions

- Does the existing `MachineTiming timing_` member on `Emulator` end
  up being the right home for contention state, merged in, or should
  contention be a separate class?
- Per-bank contention tables: keep them in the class, or fold into
  `MachineTiming`?

## Status: **PENDING** — low priority unless a user hits a
contention-sensitive demo. Queued behind NMI + UART+I2C + ESP plans.
