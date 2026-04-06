# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Purpose

This repository contains the code for a ZX Spectrum Next emulator based on the official VHDL sources for the ZX Next FPGA core.

## Reference Files

- Emulator design plan: @EMULATOR-DESIGN-PLAN.md
- FPGA code analysis: @FPGA-REPO-ANALYSIS.md
- FPGA VHDL source (authoritative hardware spec): `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/`

## General guidance

- Use an Agent Team when working on different tasks
- Try to parallelize work on different agents for independent tasks (e.g. emulator, peripherals, GUI, tests, documentation, etc.)
- Skills needed:
  - C/C++ expert developer
  - VHDL expert
  - GUI developer expert in SDL/QT6

## Constraints for development

- Do not include Co-Authored-by headers in commit messages
- Keep commit messages terse but insightful
- When reading daily prompt files (in directory `.prompts`, they contains tasks for the daily work), always keep a Task Completion Status section in each of them. Update this section whenever a task is finished.
- When launching Agent Teams, the Manager agent should NOT write or touch any code
- When launching Agent Teams, each independent function should be worked on in a different branch, to avoid code trashing between agents. When code is ready on each branch, they should be merged to main. If merge problems occur, the agent responsible for fixing them is the one that tried to merge last, and it should try to fix them on their own branch.
- Agents should NOT write to the main branch, ever. Only on their own branches and worktrees!
- Update task status on the main plan whenever a task is finished
- When the user tells you to prepare for a session handvover, immediately save your memories

## Building

```bash
cmake --build build -j$(nproc) 2>&1 | tail -5
```

The build uses CMake with Qt6 UI enabled (`-DENABLE_QT_UI=ON`). The executable is at `build/jnext`.

## Testing

### FUSE Z80 opcode test suite

```bash
./build/test/fuse_z80_test build/test/fuse
```

Result: 1340/1356 pass (98.8%). See `doc/FUSE-Z80-TEST-SUITE-REPORT.md` for details on the 16 failures (all undocumented Z80 behaviors).

### Automated screenshot testing

Use `--delayed-screenshot` and `--delayed-automatic-exit` for unattended testing:

```bash
timeout --kill-after=5s 10s ./build/jnext \
    --machine-type 48k \
    --delayed-screenshot /tmp/test.png \
    --delayed-screenshot-time 3 \
    --delayed-automatic-exit 5
```

Key options:
- `--machine-type TYPE` — `48k`, `128k`, `plus3`, `pentagon`, `next` (default)
- `--roms-directory DIR` — ROM files location (default: `/usr/share/fuse`)
- `--delayed-screenshot FILE` — save PNG screenshot after delay
- `--delayed-screenshot-time N` — delay in seconds (default 10)
- `--delayed-automatic-exit N` — exit emulator after N seconds
- `--load FILE` — load a NEX file at startup

Always use `timeout --kill-after=5s` to ensure the emulator process is killed if it hangs.

### Testing machine boot

Verify each machine type boots to BASIC:

```bash
# 48K — expect "(c) 1982 Sinclair Research Ltd"
timeout --kill-after=5s 10s ./build/jnext --machine-type 48k \
    --delayed-screenshot /tmp/test-48k.png --delayed-screenshot-time 3 --delayed-automatic-exit 5

# 128K — expect 128K menu (Tape Loader / 128 BASIC / Calculator / 48 BASIC / Tape Tester)
timeout --kill-after=5s 12s ./build/jnext --machine-type 128k \
    --delayed-screenshot /tmp/test-128k.png --delayed-screenshot-time 5 --delayed-automatic-exit 8

# +3 — expect "(c) 1982 Amstrad"
timeout --kill-after=5s 12s ./build/jnext --machine-type plus3 \
    --delayed-screenshot /tmp/test-plus3.png --delayed-screenshot-time 5 --delayed-automatic-exit 8

# Pentagon — expect 128K menu with TR-DOS option
timeout --kill-after=5s 12s ./build/jnext --machine-type pentagon \
    --delayed-screenshot /tmp/test-pentagon.png --delayed-screenshot-time 5 --delayed-automatic-exit 8
```

### Loading and testing NEX demo programs

```bash
timeout --kill-after=5s 10s ./build/jnext \
    --load demo/floating_bus_test.nex \
    --machine-type 48k \
    --delayed-screenshot /tmp/test-fbus.png \
    --delayed-screenshot-time 3 --delayed-automatic-exit 5
```

### Comparing screenshots with reference images

Reference image: `test/img/boot-reference.img` — do NOT modify this file.

To compare a screenshot with a reference, use the Read tool to view both PNG files and visually compare. For automated comparison, use ImageMagick:

```bash
compare -metric AE /tmp/test.png test/img/reference.png /tmp/diff.png 2>&1
# Output is the number of different pixels (0 = identical)
```

### Building demo/test programs (z88dk)

Test programs are in `demo/` and built with z88dk:

```bash
cd demo
zcc +zxn -vn -startup=31 -clib=sdcc_iy -SO3 \
    program.c -o program -subtype=nex -create-app
```

Both `-subtype=nex` AND `-create-app` are required to produce a `.nex` file.

### ROMs

ROMs are loaded from `/usr/share/fuse/` by default (FUSE emulator package):
- 48K: `48.rom`
- 128K: `128-0.rom`, `128-1.rom`
- +3: `plus3-0.rom` through `plus3-3.rom`
- Pentagon: `128p-0.rom`, `128p-1.rom`

Override with `--roms-directory DIR`.
