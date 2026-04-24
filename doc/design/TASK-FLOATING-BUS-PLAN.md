# Task — Emulator Floating Bus SKIP-Reduction Plan

Plan authored 2026-04-23 as part of the ULA Phase 4 closure. Captures
the 5 rows re-homed from `test/ula/ula_test.cpp` that cannot be tested
at the `Ula` abstraction because the floating-bus behaviour lives on
`Emulator::floating_bus_read` (NOT the Ula itself — per VHDL
`zxula.vhd:573` + `zxnext.vhd:2813`).

## Starting state

- No dedicated floating-bus test suite today.
- Floating-bus behaviour lives in `Emulator::floating_bus_read`
  (existing method, not yet audited against VHDL).
- 5 ULA plan rows re-homed with `// RE-HOME:` comments in
  `test/ula/ula_test.cpp` §10 pointing at this plan.

## Rows inherited from ULA plan

| Row ID | Scope | VHDL cite |
|---|---|---|
| S10.01 | 48K border-phase floating-bus read returns 0xFF | `zxula.vhd:573` |
| S10.05 | +3 timing forces bit 0 high | `zxula.vhd:573` |
| S10.06 | +3 border fallback `p3_floating_bus_dat` | `zxula.vhd:573` |
| S10.07 | Port 0xFF read path through `Emulator::floating_bus_read` | host impl |
| S10.08 | Port 0xFF gated on NR 0x08 `ff_rd_en=1` | `zxnext.vhd:2813` |

## Approach (phased, TBD when picked up)

- **Phase 0**: Audit the existing `Emulator::floating_bus_read` against
  VHDL `zxula.vhd:573-585` + `zxnext.vhd:2813-2820`. Catalogue the
  gaps (per-machine variant behaviour, NR 0x08 gating, hc/vc phase
  dependency).
- **Phase 1**: Either create a new `test/floating_bus/` suite OR add
  the 5 rows + any missing neighbours to an existing Emulator-level
  test. Given the tight coupling with ULA's horizontal-counter phase,
  an Emulator-level fixture is likely the right home.
- **Phase 2**: Wire missing paths: NR 0x08 `ff_rd_en` gate, +3
  floating-bus timing forcing.
- **Phase 3**: Critic + merge.

## Open questions

- New test file vs. extend existing? Likely extend `test/port/` or
  create `test/emulator/` since `floating_bus_read` is Emulator-level.
- Does this plan need to execute BEFORE NextZXOS boot debugging picks
  up? Floating-bus reads are common in tape-loader hardware detection
  but NextZXOS itself probably doesn't exercise them heavily.

## Status: **PENDING** — unblock when a session has budget for a small
Emulator-side audit + 5-row test rebuild.
