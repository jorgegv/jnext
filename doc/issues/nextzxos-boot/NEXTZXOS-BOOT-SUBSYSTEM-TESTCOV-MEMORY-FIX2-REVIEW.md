# Memory subsystem testcov-memory FIX2 — independent review

**Branch (review)**: `task2/testcov-memory-fix2-reviewer`
**Worktree**: `.claude/worktrees/task2-testcov-memory-fix2-reviewer`
**Reviewing commit**: `5140250` — "testcov(memory-fix2): close FIX-NR5xFF-INT-02 NIT — seed ram[0][0]=0x42"
**Date**: 2026-05-10
**Predecessor doc**: [NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-MEMORY-FIX2.md](NEXTZXOS-BOOT-SUBSYSTEM-TESTCOV-MEMORY-FIX2.md)

## Verdict: **APPROVE**

The 1-line NIT closure is correct, minimal, and independently verified
to be discriminative. No nits.

## What was reviewed

The fix agent applied a single edit to
`test/nextreg/nextreg_integration_test.cpp` inside the
`FIX-NR5xFF-INT-02` row: after `emu.mmu().reset(true)` and BEFORE
`nr_write(emu, 0x52, 0xFF)`, it inserts:

```cpp
emu.ram().page_ptr(0)[0] = 0x42;
```

plus an expanded comment block (rationale + reviewer attribution) and
an updated assert detail string mentioning the seed. No `src/` change.

## Independent revert-check — executed

I reproduced the discriminative-check protocol independently of the fix
agent's report (without re-using their wording).

**Step 1 — fix in place**: built `nextreg_integration_test` from clean
HEAD `5140250`, ran it. Result: `186/186/0/0`, no FAIL.

**Step 2 — revert pass-2 dispatcher fix**: in
`src/core/emulator.cpp:1582` I replaced

```cpp
mmu_.set_page(i, 0xFF);          // post-fix (slots 2-7 unified)
```

with the pre-fix branch logic (slots 2-5 → `mmu_.map_rom(i, 0)`,
slots 6-7 → `mmu_.engage_legacy_ram_paging()`), rebuilt, ran the test.
Result:

```
FAIL FIX-NR5xFF-INT-02: ... [nr_rb=0xff cpu_rd=0x42
  (exp 0xFF/0xFF; ram[0][0]=0x42 seed)]
FAIL FIX-NR5xFF-INT-03: ... [nr_rb=0x00 cpu_rd=0x00 (exp 0xFF/0xFF)]
Total:  186  Passed:  184  Failed:    2  Skipped:    0
```

The `cpu_rd=0x42` diagnostic in `FIX-NR5xFF-INT-02` is **exactly** what
the fix agent predicted: with the pre-fix dispatcher mapping slot 2 to
`mmu_.map_rom(2, 0)` and Next mode having `rom_in_sram_=true`, slot 2
reads from `ram_.page_ptr(0)`. The seed `ram[0][0]=0x42` is observed by
the CPU read at `0x4000` and the row fails. With seed absent (pre-fix
fixture), the read would have been `0xFF` (the propagated `Rom::Rom()`
fill), masking the bug — exactly the fixture-data coincidence the NIT
flagged. Seed is therefore discriminative.

(`FIX-NR5xFF-INT-03` also fails because my revert intentionally
restored the broader pre-fix path including slots 6/7. The agent's NIT
closure was scoped to INT-02; INT-03 has its own coverage and is not
affected by the FIX2 edit.)

**Step 3 — restore fix**: reverted `emulator.cpp` to HEAD, rebuilt,
re-ran. Result: `186/186/0/0`, no FAIL. Confirmed.

## Test code — sequencing review

Lines 1696-1718 of `nextreg_integration_test.cpp`:

1. `emu.mmu().reset(true)` — full reset.
2. **Seed**: `emu.ram().page_ptr(0)[0] = 0x42`.
3. `nr_write(emu, 0x52, 0xFF)` — triggers the dispatcher under test.
4. Read NR readback + CPU read at `0x4000`.

Sequencing is correct:

- Seed must follow reset (otherwise reset wipes the value via
  `Emulator::init`'s rom→ram copy). Audited `Mmu::reset(bool)` in
  `src/memory/mmu.cpp:52+` — does not memset RAM contents; seed
  survives the `reset(true)` call (which is a per-row mmu-only reset,
  not a full `Emulator::init`).
- Seed must precede the NR write (so the pre-fix `map_rom(2, 0)` would
  observe `0x42`). Verified.
- The CPU read at `0x4000` is after the NR write, so the dispatcher's
  effect is observed. Verified.

Audited `Mmu::map_rom_physical` (`src/memory/mmu.cpp:260+`):
`read_ptr_[slot] = rom_in_sram_ ? ram_.page_ptr(rom_page) :
rom_.page_ptr(rom_page)`. In Next mode `rom_in_sram_=true`, so a
hypothetical pre-fix `map_rom(2, 0)` does read `ram_.page_ptr(0)` —
the same buffer the test seeds. Discriminative path is real.

## Test results — fix in place

```
LANG=C ctest --test-dir build -R 'nextreg|fuse_z80' --output-on-failure
```

| Test                          | Result  | Time   |
|-------------------------------|---------|--------|
| fuse_z80_tests                | Passed  | 0.03 s |
| nextreg_tests                 | Passed  | 0.00 s |
| nextreg_integration_tests     | Passed  | 0.02 s |
| audio_nextreg_tests           | Passed  | 0.01 s |

`100% tests passed, 0 tests failed out of 4`.

Full build (`cmake --build build -j$(nproc)`) succeeds; `jnext` binary
links cleanly.

## Findings

- **No nits.** The fix is minimal, correctly placed, discriminative,
  well-documented in code comments (with reviewer attribution and
  rationale), and consistent with the testcov plan's protocol.
- The expanded comment block (lines 1685-1695) accurately documents
  why `Rom::Rom()` + `Emulator::init` rom→ram copy made the row
  non-discriminative pre-seed. This will help future readers.
- The assert detail string (`"... ram[0][0]=0x42 seed"`) makes the
  failure self-explanatory if the dispatcher regresses.

## Constraints honoured

- Pure tests + docs diff. No `src/` changes.
- Reviewer revert-and-restore done in working tree only; emulator.cpp
  is back to HEAD content; tree clean (only the untracked `roms`
  symlink, not committed).
- No `git push`, no merge, no PR.

## Branch HEAD

`5140250` (single fix commit on top of `88cc687`). Working tree clean
post-review (revert fully reversed).
