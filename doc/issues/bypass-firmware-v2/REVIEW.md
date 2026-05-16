# Task 18 bypass-firmware — Independent Code Review

Commit: `c0a0ff1b`  
Branch: `bypass-firmware-exploration-v2`  
Reviewer: independent agent (not the author of c0a0ff1b)  
Date: 2026-05-17  

---

## Findings

### Enumeration table — all 3 gates vs. specification

| Gate | File:line | VHDL oracle | Correct? | Notes |
|------|-----------|-------------|----------|-------|
| 1. Machine ROM swap (`enNextZX.rom` in ZXN case when bypass) | `emulator.cpp:5156-5169` | `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md §2.2` — boot.c numpages=4, blocklen=16384, destpage RAMPAGE_ROMSPECCY=0 → pages 0x00..0x07 | CORRECT | 64 KB / 4 banks / 0x10000 matches the plan exactly. Placed inside `!preserve_memory && !sd_card_image.empty()` — soft resets skip this, which is right (they preserve existing SRAM content). |
| 2. Boot-ROM overlay suppression | `emulator.cpp:5198-5212` | VHDL `zxnext.vhd:1101 — bootrom_en := '1'` initial value; tbblue.fw disables via NR 0x03=0x00 at `boot.c:461` | CORRECT | Gate added: `!cfg.bypass_tbblue_fw`. The `mmu_.set_boot_rom()` call inside the now-bypassed block is the ONLY site that installs the boot-ROM (confirmed by grep: lines 2334-2335 only call `set_boot_rom_enabled(false)`, never `set_boot_rom()`). |
| 3. NR 0x03=0xB3 post-init write | `emulator.cpp:5487-5491` | VHDL `zxnext.vhd:5137` machines type commit gated on config_mode=1; `:5147-5151` config_mode FSM; handler at `emulator.cpp:2333-2508` does machine-type then config_mode_transition in the correct VHDL order | CORRECT — with one concern (see Finding 1) |

### Finding 1 — MINOR: config_mode still active in bypass mode during seed-loop window

**File:** `emulator.cpp:5251-5253`

```cpp
if (cfg.type == MachineType::ZXN_ISSUE2 && mmu_.boot_rom_enabled()) {
    mmu_.set_config_mode(nextreg_.nr_03_config_mode());
    ...
}
```

In the normal (non-bypass) path, `mmu_.boot_rom_enabled()` is true immediately after the boot-ROM overlay is installed, so `set_config_mode(true)` is called and the Mmu routes config_mode SRAM writes correctly for tbblue.fw to use.

In bypass mode, `mmu_.boot_rom_enabled()` is **false** (the overlay was never installed), so this block is skipped entirely. This means `mmu_.config_mode_` stays at whatever `Mmu::reset()` left it — which JNEXT-COLD-RESET-STATE.md §3 shows is `0` (Mmu's `config_mode_` default is false; only the NextReg latch `nr_03_config_mode_` is `true` at VHDL default). Therefore `mmu_.set_config_mode()` is not called between the SRAM seed loop and the `nextreg_.write(0x03, 0xB3)` at line 5487.

The question is whether this matters. The seed loop at 5227-5231 copies `rom_.page_ptr(p)` to `ram_.page_ptr(p)` — this is a host-side memcpy, not a Z80 write, so the Mmu's `config_mode_` flag does not affect it. The NR 0x03=0xB3 write then calls `mmu_.set_config_mode(false)` via the handler (line 2497) — which is already false, so it's a no-op. There is no functional bug here at the current state of the code.

However, if anything between the SRAM seed loop and line 5487 attempted a Z80-visible write through the Mmu (currently nothing does in init()), it would hit the non-config-mode path rather than the config-mode SRAM routing. This is a latent hazard if the bypass code grows. The comment at 5243-5250 should be updated to document that bypass mode intentionally skips config_mode activation (which is correct per spec — there is no firmware to use it).

**Severity: MINOR.** No functional bug today; worth documenting.

### Finding 2 — MAJOR: `enNextZX.rom` filename almost certainly does not exist on the canonical SD image

**File:** `emulator.cpp:5165`

```cpp
load_machine_rom("/MACHINES/NEXT/enNextZX.rom", 4, 0x10000, ...);
```

The CLAUDE.md "ROMs" section lists the canonical SD paths as:
- `/MACHINES/NEXT/48.rom` (16 KB)
- `/MACHINES/NEXT/128.rom` (32 KB)
- `/MACHINES/NEXT/plus3.rom` (64 KB)
- `/MACHINES/NEXT/enNxtmmc.rom` (8 KB)
- `/MACHINES/NEXT/enNextMf.rom` (8 KB)

`enNextZX.rom` is what `tbblue.fw boot.c` calls the file INTERNALLY (the `loadFile()` argument), but the ACTUAL filename on the SD image that tbblue distributes is **`/MACHINES/NEXT/plus3.rom`** (or `48.rom` / `128.rom` depending on mode). The PLAN-AUDIT.md §1 (lines 36-43) explicitly notes this:

> "the plan's reference to `enNextZX.rom` (boot.c's RAMPAGE_ROMSPECCY load) does NOT match jnext's current `/MACHINES/NEXT/*.rom` filename scheme. Per `emulator.cpp:5141-5158`, jnext extracts `48.rom` / `128.rom` / `plus3.rom` keyed off `cfg.type`"

The PLAN-AUDIT even recommends the fix: use `plus3.rom` (the file that IS on the canonical SD image for the standard +3 Next mode).

With the current code, `load_machine_rom("/MACHINES/NEXT/enNextZX.rom", ...)` will fail with `extract_sd_rom()` returning false on the canonical `nextzxos-1gb-fat32fix.img` because that file does not exist there (the SD image uses `plus3.rom`). The fallback: `load_machine_rom` logs a warning and returns false, leaving `rom_` with the stale `48.rom` seed from the non-bypass path... except in bypass mode the non-bypass load was replaced by this call, so if it fails, `rom_` banks 0..3 hold whatever was last loaded (potentially garbage from a previous reset, or the 1-bank `48.rom` if this is first boot). The SRAM seed loop will then copy those corrupt/partial contents into pages 0..7.

The commit message says "NextZXOS reaches its IM2 idle loop" — if that empirical test was done with a non-canonical SD image that happened to contain a file named `enNextZX.rom`, or if it was run against an SD image where the extraction silently fell back to the 48.rom seed, the smoke test may have passed despite the wrong ROM being loaded.

This is the most significant correctness gap in the commit.

**Severity: MAJOR.** On the canonical SD image (`nextzxos-1gb-fat32fix.img`) the `extract_sd_rom` call will fail silently and the bypass will boot with wrong/partial ROM contents. Fix: replace `"/MACHINES/NEXT/enNextZX.rom"` with `"/MACHINES/NEXT/plus3.rom"` (and `"plus3.rom" (bypass)"` for the desc string), matching the `ZX_PLUS3` case at line 5151 which is the correct 64 KB blob.

### Finding 3 — MINOR: NR 0x07 not written to 0x00 before handing to NextZXOS

**File:** `emulator.cpp:5487-5491`

The PLAN-AUDIT §3 "Open question" notes: "NextZXOS expects boot-rate speed" and recommends writing `NR 0x07 = 0x00` (3.5 MHz). The JNEXT-COLD-RESET-STATE.md §1 shows NR 0x07 cold-reset value is already `0x00` on hard reset, so on first boot this is correct by accident (Z80 starts at 3.5 MHz by default). However the commit documentation says firmware writes `NR 0x07 = 0x03` (28 MHz) during boot at `boot.c:448-449` and then "NextZXOS sets its own speed". The PLAN-AUDIT §2, Branch 3 Open question says the minimum NR set is "NR 0x03, NR 0x07, maybe NR 0x82..0x85". Since NR 0x07 defaults to 0x00 already, this is not a runtime bug, but the comment block at 5456-5490 does not explain why NR 0x07 is deliberately omitted. The doc commit in the `doc/design` plan (§3) lists `NR 0x07 = 0x00` as one of the writes; its absence from the code without explanation is a documentation gap.

**Severity: NIT** (no functional bug; defaults are correct for hard-reset path).

### Finding 4 — MINOR: soft-reset interaction with `enNextZX.rom` ROM load

**Context:** `emulator.cpp:5113` — the machine ROM load block is gated on `!preserve_memory`. So on soft reset, the ROM load is skipped entirely, and the SRAM pages remain at whatever content the hard-reset populate left them with. This is correct by design (SRAM is preserved across soft reset).

However, if someone issues a **hard reset** (i.e., calls `init(cfg, false)` again after a previous run), bypass mode will call `load_machine_rom("/MACHINES/NEXT/enNextZX.rom", ...)` again — and if that call fails (per Finding 2), the previous SRAM content is overwritten with `rom_` which at that point may be the 1-bank `48.rom` from a previous non-bypass init cycle, or left partially loaded. Not a new bug introduced by this commit specifically, but it compounds Finding 2.

**Severity: MINOR** (contingent on Finding 2 being fixed; not an independent bug).

### Finding 5 — MAJOR: No regression tests

Per CLAUDE.md: "When a new development is made that changes any interface in any subsystem, make sure there are enough test cases." The commit message explicitly acknowledges: "Test triplet status: not yet re-run on this branch."

More critically, no discriminative test was added. A discriminative test for the 3 gates would:
1. Construct `Emulator` with `bypass_tbblue_fw=true` + a valid SD image.
2. Assert `mmu_.boot_rom_enabled()` is false (gate 2 worked).
3. Assert `ram_.page_ptr(0)[0..3]` matches the expected first bytes of `plus3.rom` (gate 1 + seed loop).
4. Assert `nextreg_.nr_03_config_mode()` is false (gate 3 committed config_mode=0).
5. Assert `mmu_.machine_type()` is `ZX_PLUS3` (gate 3 committed machine type).

None of these exist. The existing regression suite (33 screenshot tests) does not cover the bypass path at all since it requires `--bypass-tbblue-fw` to be exercised. A screenshot regression test for the bypass path (e.g., NextZXOS boot screen at frame N) would be definitive, but requires the display issue (uniform gray) to be resolved first.

Per the project's mandatory standard: **every behavior fix must ship with a discriminative regression test in the same commit.** For a new feature flag, a discriminative unit test (not just a screenshot) is the minimum. Missing tests on a feature branch is acceptable for a WIP scaffold, but must be resolved before merge to main.

**Severity: MAJOR** (blocking for merge; acceptable for current branch status as a scaffold).

### Finding 6 — VHDL spot-check: NR 0x03=0xB3 machine-type commit timing

The commit comment at `emulator.cpp:5462-5476` claims the NR 0x03=0xB3 handler does: machine_type commit THEN config_mode transition. The handler code confirms this ordering at 2440-2494: `if (nextreg_.nr_03_config_mode()) { ... commit machine_type ... }` (line 2440) runs before `nextreg_.apply_nr_03_config_mode_transition(v)` (line 2494). VHDL `zxnext.vhd:5137` reads `if nr_03_config_mode = '1'` (same-cycle gate), then `:5147-5151` updates `nr_03_config_mode` — these are sequential VHDL statements in the same `when X"03"` branch of a clocked process, meaning the `config_mode` gate reads the PRE-write value and THEN the FSM updates it. The C++ ordering matches exactly. **VHDL claim in the comment is verified correct.**

**Severity: none** — confirms correctness.

---

## Verdict

**APPROVE-WITH-NITS** (with caveat: Finding 2 is MAJOR and must be fixed before this feature is considered complete)

The fundamental architecture of the 3-gate bypass is sound. Gate 2 (boot-ROM suppression) is VHDL-correct. Gate 3 (NR 0x03=0xB3 commit timing) is VHDL-correct and the handler ordering matches the spec exactly. The PLAN-AUDIT's Q1/Q2/Q3 citations were spot-checked and verified.

The blocking issue is **Finding 2**: the hardcoded `enNextZX.rom` filename does not exist on the canonical SD image; `plus3.rom` is the correct path. This will cause silent failure on any standard NextZXOS SD image and means the empirical smoke-test result ("NextZXOS reaches IM2 idle loop") may have been achieved with a different SD image or may have observed the 48.rom seed rather than the intended 64 KB NextZXOS ROM.

Finding 5 (no tests) is also MAJOR for merge purposes but is consistent with the branch being a declared scaffold/exploration — the commit message explicitly calls it out. For a branch in active investigation this is tolerable, but must be resolved before merging to main.

For the current branch state (exploration/scaffold, not yet merge-ready), the verdict is:

- Architecture: sound
- VHDL citations: verified (NR 0x03 handler ordering matches spec)
- Non-bypass regressions: zero (all 3 gates strictly conditioned on `cfg.bypass_tbblue_fw`)
- Blocking issues before merge: Finding 2 (wrong ROM filename), Finding 5 (no tests)
- Non-blocking: Finding 1 (config_mode comment gap), Finding 3 (NR 0x07 doc gap), Finding 4 (soft-reset compounding)

**Required fixes before merge:**
1. Replace `"/MACHINES/NEXT/enNextZX.rom"` with `"/MACHINES/NEXT/plus3.rom"` at `emulator.cpp:5165`. Update the comment at 5157-5164 to reference `plus3.rom` (the real SD filename) rather than `enNextZX.rom` (boot.c's internal load argument).
2. Add at minimum a unit test asserting (a) `boot_rom_enabled()=false`, (b) `nr_03_config_mode()=false`, (c) `machine_type()=ZX_PLUS3` after `init()` with `bypass_tbblue_fw=true`.

