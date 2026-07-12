# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Purpose

This repository contains the code for a ZX Spectrum Next emulator based on the official VHDL sources for the ZX Next FPGA core.

## Reference Files

- Emulator design plan: @EMULATOR-DESIGN-PLAN.md
- FPGA code analysis: @FPGA-REPO-ANALYSIS.md
- FPGA VHDL source (authoritative hardware spec): `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`
- Design plans in directory `doc/design`

### External references

- NextREG Machine ID register reference: <https://wiki.specnext.dev/Machine_ID_Register>
- Boot sequence (authoritative): <https://wiki.specnext.dev/Boot_Sequence>
- TBBlue machine-config boot files (`config.ini` / `menu.ini` / `menu.def`): <https://gitlab.com/thesmog358/tbblue/-/blob/master/docs/config/config.txt>
- TBBlue firmware source (GPLv3): <https://gitlab.com/thesmog358/tbblue/-/tree/master>
  - Contains the `TBBLUE.FW` / `TBBLUE.TBU` source: the IPL + boot module that reads the SD and shows the "Press SPACEBAR for menu" / Configuration screens.

## General guidance

- Use an Agent Team when working on different tasks
- Try to parallelize work on different agents for independent tasks (e.g. emulator, peripherals, GUI, tests, documentation, etc.)
- Skills needed:
  - C/C++ expert developer
  - VHDL expert
  - GUI developer expert in SDL/QT6
- When a new feature or bugfix is developed, ALWAYS schedule an additional agent for code review, with the same expertise as the original one. The code review should NEVER be done by the same agent that created the code in the first place. Make the reviewer agent be very critic with the code created, ensuring that code passes tests and that no regressions are introduced. Also review code style and conformance to our best practices.

## Constraints for development

- Do not include Co-Authored-by headers in commit messages
- Keep commit messages terse but insightful
- When reading daily prompt files (in directory `.prompts`, they contains tasks for the daily work), always keep a Task Completion Status section in each of them. Update this section whenever a task is finished.
- When launching Agent Teams, the Manager agent should NOT write or touch any code
- When launching Agent Teams, each independent function should be worked on in a different branch, to avoid code trashing between agents. When code is ready on each branch, they should be merged to main. If merge problems occur, the agent responsible for fixing them is the one that tried to merge last, and it should try to fix them on their own branch.
- Agents should NOT write to the main branch, ever. Only on their own branches and worktrees!
- **NEVER push to origin without explicit user authorization.** This applies to the manager AND every spawned agent. Local commits, rebases, and merges on owned branches/worktrees are fine; `git push`, `git push -u`, `git push --force`, `gh pr create`, and any equivalent are all forbidden unless the user explicitly says "push" or "open a PR".
- Update task status on the main plan whenever a task is finished
- When the user tells you to prepare for a session handvover, immediately save your memories
- When a commit is made, check that the FEATURES.md and TODO.md files are updated to include the new feature if it's a significant one. Ask the user if in doubt of the relevance of the change meriting an update to these files.
- When a new development is made that changes any interface in any subsystem, make sure there are enough test cases in that subsystem's test  plan to fully test that new code/interface. Modify the plan if needed and do an independent code review for the new code.
- When a bug is fixed in any subsystem, make sure there are enough test cases in that subsystem's test  plan to fully test the fixed new code/interface. Modify the plan if needed and do an independent code review for the new test code.
- For git commands that run against another directory (e.g. a worktree), always use `git -C /abs/path <cmd> ...` instead of `cd /abs/path && git <cmd> ...`. The `-C` flag avoids shell-state side effects and keeps the current working directory stable across tool calls. It also avoids needless permission prompts to the user.

## ChangeLog file

- A ChangeLog file should exist at the root of the repository
- It should contain entries for the different tagged versions, in reverse chronological order (most recent at the top of the file)
- Each entry should consist of the version tag, and below it, an extremely terse description of the new features and fixes of that version, up to the previous version. There should be 4 sections:
  - User Features: new features oriented to users who just run games and programs: GUI, emulation features, main menu, etc.
  - Developer Features: new features oriented to developers: in general, all debugger and instrospection features
  - Bug Fixes
  - Internal JNEXT Development: new plans, enhancements to test results, big architectural changes or enhancements, etc.
- Descriptions for each feature/fix should never be more than one line, and should be about 10-20 words maximum
- Trivial fixes, syntax, reformats, documentation, project plan updates, etc. should not appear on the ChangeLog. Only significative features and fixes.
- The file should only be updated when the user requests it
- The initial version (v0.91.0) should contains a short list of the current features at that time
- If there are commits after the last tag, and the user requests a ChangeLog update, it should be updated up to the current date, and using "(current date)" as the version identifier in the ChangeLog
- Don't be overly confident about features: never put an ongoing feature in the ChangeLog if it's still not tested or has known bugs
- Don't put commit IDs in the ChangeLog
- Try to coalesce similar features or fixes in a single description if possible
- The goal os this file is to give the emulator USERS an overview of the most important things happened since the last version. It's NOT meant to be an exhaustive list of changes at all. It's not meant to be a development diary for the emulator itself either.

## Version bumping

When the user asks to bump the version, follow these steps in order:

1. Run all unit tests (`make unit-test`) and regression tests (`make regression`) — none must have any FAIL (SKIPs are acceptable)
2. Update the traceability matrix
3. Update the unit test status report
4. Update the DEVELOPMENT-SESSIONS document (`doc/DEVELOPMENT-SESSIONS.md`)
5. Update the ChangeLog using the future version that will be bumped to
6. Commit all the above changes
7. Bump the version by running `make bump-<bump_type>` (where bump_type is `patch`, `minor`, or `major`) — this will bump the version in `version.yaml`, commit, and create a new git tag

## Building

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```

The build uses CMake with Qt6 UI enabled (`-DENABLE_QT_UI=ON`). The executable is at `build/jnext`.

## Testing

> **Before authoring, rewriting, or un-skipping any subsystem unit test
> plan**, read [doc/testing/UNIT-TEST-PLAN-EXECUTION.md](doc/testing/UNIT-TEST-PLAN-EXECUTION.md).
> It documents the VHDL-as-oracle rule, the pass/fail/skip distinction,
> the 1:1:1 emulator-fix-plus-unskip process, the independent-review
> requirement, and why all of that exists (the coverage-theatre audit).
> The process is mandatory for every test plan rewrite and every emulator
> fix that touches subsystem tests.

### FUSE Z80 opcode test suite

```bash
./build/test/fuse_z80_test build/test/fuse
```

Result: 1356/1356 pass (100%).

### Full regression test suite

Run the complete automated test suite (FUSE Z80 opcodes + screenshot tests):

```bash
bash test/00regression/regression.sh
```

This runs all tests in headless mode and compares screenshots to reference images.
See [doc/testing/REGRESSION-TEST-SUITE.md](doc/testing/REGRESSION-TEST-SUITE.md) for full details.

To update reference screenshots after intentional rendering changes:

```bash
bash test/00regression/generate-references.sh
```

### Headless mode

The `--headless` option runs without display/audio for automated testing:

```bash
./build/jnext --headless --machine 48k \
    --sdcard roms/nextzxos-1gb-fat32fix.img \
    --delayed-screenshot /tmp/test.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5
```

Key options:
- `--machine TYPE` — `48k`, `128k`, `plus3`, `next` (default)
- `--headless` — no display, no audio, runs at max speed
- `--sdcard FILE` — SD image with ROMs at `/MACHINES/NEXT/`. Optional; if omitted, falls back to `~/.jnext/sdcard/cspect-next-1gb-fixed.img` (the patched image; offers to download the canonical distribution `cspect-next-1gb.img` and produce that patched copy). `--sdcard-download-confirm` / `--sdcard-download-force` control that provisioning
- `--delayed-screenshot FILE` — save PNG screenshot after delay
- `--delayed-screenshot-time N` — delay in seconds (default 10)
- `--delayed-screenshot-frames N` — delay in frames (overrides `--delayed-screenshot-time`)
- `--delayed-screenshot-layers LIST` — layers composed into the screenshot: comma-separated `ula`, `layer2`, `sprites`, `tiles`, `all` (default `all`). An excluded layer is composed as if its hardware enable bit were clear, so the rest still follow NR 0x15 priority and the NR 0x4A fallback colour shows through. Excluding `ula` also removes the **border** (the ULA emits it)
- `--delayed-automatic-exit N` — exit emulator after N seconds
- `--load FILE` — load a NEX, TAP, or TZX file at startup
- `--rtc "YYYY-MM-DD HH:MM:SS"` — pin the RTC to a fixed date/time (deterministic boot screenshots; ISO `T` form also accepted)

Always use `timeout --kill-after=5s` when running non-headless for safety.

### Building demo/test programs (z88dk)

Test programs are in `demo/` and built with z88dk:

```bash
# Build all demos (NEX + TAP)
make -C demo all

# Build only NEX or TAP
make -C demo nex
make -C demo tap
```

### ROMs

Wave 0.3 (Task 8 Multiface plan, 2026-05-04) made the SD-card image the
canonical source for **all** ROMs jnext needs at runtime, mirroring real
ZX Spectrum Next hardware. There are two parts:

1. **FPGA boot ROM (`nextboot.rom`, 8 KB)** is silicon-baked: embedded
   into the jnext binary at link time via `objcopy --input-target=binary`
   in `src/core/CMakeLists.txt`. No CLI flag, no SD lookup. Mirrors the
   on-FPGA flash IPL of real Next hardware.

2. **All other ROMs** are extracted from the SD image (supplied via
   `--sdcard`, or the `~/.jnext/sdcard/` fallback) at canonical TBBlue
   paths via the host-side FAT32 reader in `src/core/sd_rom_extractor.{h,cpp}`:
   - `/MACHINES/NEXT/48.rom` (16 KB) — 48K BASIC
   - `/MACHINES/NEXT/128.rom` (32 KB combined) — 128K BASIC (split into 2 banks)
   - `/MACHINES/NEXT/plus3.rom` (64 KB combined) — +3 BASIC (split into 4 banks)
   - `/MACHINES/NEXT/enNxtmmc.rom` (8 KB) — DivMMC firmware
   - `/MACHINES/NEXT/enNextMf.rom` (8 KB) — Multiface firmware (Wave 1)

The runtime SPI/SD path (`src/peripheral/sd_card.cpp`) is independent
from the host-side extractor — it serves Z80 software at runtime via
block-level access. Wave 1 adds a Multiface ROM read of the same SD
image at init time.

### ZX Spectrum Next boot assets

jnext needs a NextZXOS SD image. Mount one explicitly via `--sdcard`, or omit
it and let jnext fall back to `~/.jnext/sdcard/cspect-next-1gb-fixed.img` (the
patched image; offering to download the canonical distribution image, kept as
`cspect-next-1gb.img`, and produce that FAT32-patched copy — see Task 27).

**Canonical NextZXOS test image: `roms/nextzxos-1gb-fat32fix.img`.**
The original `roms/nextzxos-1gb.img` uses 32 KB clusters on a 1 GB
partition and ends up with only 32 758 data clusters — below the FAT32
spec minimum of 65 525 — so tbblue.fw's FatFs (correctly, per spec)
rejects it as "not an FAT filesystem" (see
`project_nextzxos_task9_stagec.md` in memory for the full trace). The
`-fat32fix.img` variant uses 8 KB clusters (261 877 clusters, valid
FAT32) and is what subsequent work should target. CSpect's built-in SD
driver tolerates the under-clustered variant; ours doesn't, and there is
no reason to relax it — firmware-faithful is the right posture. Typical
boot invocation:

```bash
./build/jnext --machine next \
    --sdcard roms/nextzxos-1gb-fat32fix.img
```
