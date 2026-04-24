# Task — MMU Shadow-Screen Routing SKIP-Reduction Plan

Plan authored 2026-04-23 as part of the ULA Phase 4 closure. Captures
the 2 rows re-homed from `test/ula/ula_test.cpp` §15 that test the
port 0x7FFD bit 3 → `i_ula_shadow_en` MMU-level routing.

## Starting state

- `mmu_test` currently **148/0/0** (clean zero-skip).
- **This plan reopens the MMU suite** by adding 2 new rows. Accept
  the reopen per the "finish ULA_test" directive — the new rows test
  real missing functionality.
- Shadow-screen selection happens at the MMU / port-0x7FFD level:
  bit 3 of the 0x7FFD write selects bank 7 (page 14) instead of bank
  5 for the ULA's VRAM read. The Ula-side plumbing (
  `Ula::set_shadow_screen_en`) is already in place; the MMU-side
  routing is NOT wired — nothing calls `set_shadow_screen_en` today.
- 2 ULA plan rows re-homed with `// RE-HOME:` comments in
  `test/ula/ula_test.cpp` §15 pointing at this plan.

## Rows inherited from ULA plan

| Row ID | Scope | VHDL cite |
|---|---|---|
| S15.03 | Shadow disables Timex mode — `screen_mode` forced to "000" when shadow asserted | `zxula.vhd:191` |
| S15.04 | Port 0x7FFD bit 3 → `i_ula_shadow_en` routing | `zxnext.vhd:4453` |

## Approach

- **Phase 0 — trace**: Read `src/port/port_dispatch.cpp` or wherever
  port 0x7FFD is handled today; confirm bit 3 is NOT forwarded to
  `Ula::set_shadow_screen_en`.
- **Phase 1 — wire**: Single-line fix in the 0x7FFD port handler:
  `ula_.set_shadow_screen_en((v & 0x08) != 0);`. Same pattern as the
  NR 0x68 bit 3 → `set_ulap_en` fix that landed as `a1495ba`.
- **Phase 2 — test**: Add 2 rows to `test/mmu/mmu_test.cpp` (or
  `test/port/port_test.cpp` depending on which is the canonical owner
  of 0x7FFD) covering the S15.03/04 rationale.
- **Phase 3 — un-skip**: flip the ULA re-homes back to check()s in
  whichever test file hosts them.

## Scope + risk

- Tiny: 1-line production fix + 2 test rows.
- **Screenshot regression risk**: shadow-screen functionality is
  currently inert. Enabling it means programs that previously
  silently ignored shadow writes will start displaying shadow data.
  Spot-check NextZXOS boot (shadow is used by some firmware paths).

## Open questions

- Does 0x7FFD also need bits 0-2 (RAM bank select) rewiring? Out of
  scope here — those bits land in the MMU already. This plan is only
  bit 3.

## Status: **DONE** (2026-04-24)

Landed in a single worktree session:

- `src/memory/mmu.h`: added `Mmu::shadow_screen_en()` accessor
  (`port_7ffd_ & 0x08`) — the MMU is now the sole producer of the shadow
  bit, matching VHDL where `port_7ffd_reg(3)` is the only source of
  `i_ula_shadow_en`.
- `src/core/emulator.cpp`: 0x7FFD port handler now calls
  `renderer_.ula().set_shadow_screen_en(mmu_.shadow_screen_en())` after
  `map_128k_bank(v)`. Same pattern as the NR 0x68 bit 3 → `set_ulap_en`
  forward (commit a1495ba).
- `test/mmu/mmu_test.cpp`: P7F-16 / P7F-17 flipped from skip() to live
  check() against `Mmu::shadow_screen_en()`. P7F-17 deliberately avoids
  bit 5 (lock) across its probe bytes.
- `test/ula/ula_integration_test.cpp`: new Group E + INT-SHADOW-01 row
  covers the full port-dispatch → MMU → Ula chain end-to-end.
- Dashboard `test/SUBSYSTEM-TESTS-STATUS.md`: MMU → "All tests pass.";
  ULA Video (int) 7/7 → 8/8.

Counts: mmu_test 150/148/0/2 → **150/150/0/0**. ula_integration_test
7/7/0/0 → 8/8/0/0. Aggregate 3318/3191/0/127 → 3319/3194/0/125.
