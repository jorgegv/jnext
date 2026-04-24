# Task — VideoTiming Expansion SKIP-Reduction Plan

Plan authored 2026-04-23 as part of the ULA Phase 4 closure. Captures
the 7 rows re-homed from `test/ula/ula_test.cpp` §13+§14 that need
`VideoTiming` to expose per-machine origin constants and per-machine
interrupt-position accessors.

## Starting state

- `VideoTiming` class exists at `src/video/timing.{h,cpp}` (owned by
  the ULA plan; also gained the test-only pulse-counter surface
  during ULA Wave E, 2026-04-23).
- `VideoTiming::DISPLAY_LEFT/TOP/W/H` are currently shared 48K
  constants. Per-machine variants (128K/+3, Pentagon, 60 Hz) are
  NOT exposed.
- Interrupt position (hc/vc where the ULA IRQ fires per machine)
  is NOT exposed on `VideoTiming` today — it lives in the
  scheduler's per-machine timing calculations.
- 7 ULA plan rows re-homed with `// RE-HOME:` comments in
  `test/ula/ula_test.cpp` §13+§14 pointing at this plan.

## Rows inherited from ULA plan

| Row ID | Scope | VHDL cite |
|---|---|---|
| S13.05 | 128K `min_hactive=136` active display origin | `zxula_timing.vhd` |
| S13.06 | Pentagon `min_vactive=80` | `zxula_timing.vhd` |
| S13.07 | ULA `hc_ula=0` at `min_hactive-12` (12-cycle prefetch) | `zxula_timing.vhd` |
| S13.08 | 60 Hz variant: 264 lines, 59136 T-states | `zxula_timing.vhd` |
| S14.01 | 48K interrupt position `hc=116, vc=0` | `zxula_timing.vhd:547-559` |
| S14.02 | 128K interrupt position `hc=128, vc=1` | `zxula_timing.vhd:547-559` |
| S14.03 | Pentagon interrupt position `hc=439, vc=319` | `zxula_timing.vhd:547-559` |

## Approach (phased, TBD when picked up)

- **Phase 0 — design**: Decide whether per-machine origins + int
  positions live:
  - (a) as per-MachineType lookup tables on `VideoTiming`, or
  - (b) merged with `MachineTiming` on `Emulator` as one big
    timing-parameters-per-machine record.
- **Phase 1 — scaffold**: Expand VideoTiming with
  `per_machine_origin(MachineType) -> {left, top}` and
  `per_machine_int_position(MachineType) -> {hc, vc}`. Add 60 Hz
  machine variant if not already present.
- **Phase 2 — un-skip**: flip the 7 ULA re-homes back to check()s
  in either `ula_test.cpp` (if VideoTiming-only) or a new
  `test/videotiming/` suite.

## Coupling with VideoTiming production-wiring backlog

The "VideoTiming pulse-counter production wiring" backlog item
(see `.prompts/2026-04-23.md`) describes an architectural refactor
to route the Emulator scheduler through `VideoTiming`. If that
refactor lands first, the per-machine int-position accessor
naturally falls out of it (the scheduler would ask VideoTiming
`when is the next frame IRQ for this machine?`). These two backlog
items should probably be combined into a single VideoTiming
subsystem plan when a session picks them up.

## Scope + risk

- Small if treated as a pure VideoTiming expansion (just accessors
  + tables).
- Medium if combined with the scheduler-through-VideoTiming
  architectural refactor.
- **Blocks nothing critical today** — per-machine timing effects
  are subtle (affect where the border-top transitions, interrupt
  latency relative to scan-line). No current user bug depends on
  this.

## Status: **PENDING** — candidate to merge with the VideoTiming
production-wiring backlog item into a single VideoTiming subsystem
plan. Low priority.
