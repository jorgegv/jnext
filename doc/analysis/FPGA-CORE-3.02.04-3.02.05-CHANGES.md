# FPGA cores 3.02.04 and 3.02.05: changes against 3.02.03

Analysis of 2026-11-26. Baseline: core 3.02.03, the one jnext emulates. It lists what changed on
the `30204` and `30205` branches of the official FPGA repository, focused on what a program
running in jnext or a jnext user would see, and what jnext would have to change. Changes that do
not reach anything jnext emulates are listed one line each at the end (section 5). Nothing here
is decided.

It extends [FPGA-CORE-3.02.04-PI-DEBUG-CONTROL.md](FPGA-CORE-3.02.04-PI-DEBUG-CONTROL.md)
(2026-10-18), which covers the Pi debug control in depth (protocol, QEMU side). That material is
not repeated here.

## Executive summary

Core 3.02.04 (released) replaces the boot ROM (GPL3, external/internal SD choice, settings in flash)
and shrinks its overlay to 0x0000-0x1FFF, adds the Pi debug control for issue 4 and 5, reports
NR 0x0E = 0x04, and fixes a few small hardware bugs. It also introduces a hotkey-repeat regression,
fixed in 3.02.05 (unreleased, work in progress), which adds the versioned boot code archive
`tbblue.zip` and HDMI work for issue 4/5. jnext emulates issue 2 and the 3.02.03 behaviour. On
that board the visible differences are the core version, the new boot code with its overlay and
the NR 0x0A lock; the Pi debug control is not emulated, and the hotkey regression and its fix do
not affect jnext. The Pi debug control (the Pi reading and writing the Next's memory) is analysed in
[FPGA-CORE-3.02.04-PI-DEBUG-CONTROL.md](FPGA-CORE-3.02.04-PI-DEBUG-CONTROL.md). Nothing needs
changing for 4A and 4B unless the owner adopts a newer core; two version-independent items (the
BIT n,(HL) flags and the F-key long-press timer default) are in 4C. Detail in sections 1 and 4.

| Finding | Impact | Core |
|---|---|---|
| NR 0x0E sub-version | programs reading NR 0x0E see 0x03, not 0x04 (0x05) | 3.02.04 (0x05 in 3.02.05) |
| New boot ROM | user sees new boot screens and SD choice; may need a flash device; cleaner GPL3 licence | 3.02.04 |
| Boot ROM overlay 8K | boot code reads 0x2000-0x3FFF through the normal mapping; jnext maps the 8K ROM over the whole 16K window | 3.02.04 |
| NR 0x0A bit 5 lock | firmware in config mode: jnext applies bit 5 without bit 2 | 3.02.04 |
| SPI master under DMA | none, jnext already matches | 3.02.04 |
| MD six-button detection | none in practice (host gamepads cannot trigger it); fidelity only | 3.02.04 |
| Hotkey repeat regression | none, jnext keeps the 3.02.03 behaviour (on a real Next NMI+F7/F8 would step several times, per the code, not tested on hardware) | 3.02.04, fixed in 3.02.05 |
| Pi debug control, NMI+QWERT tape hotkeys | not emulated; NextPi software on issue 4/5 only; see [the Pi debug analysis](FPGA-CORE-3.02.04-PI-DEBUG-CONTROL.md) | 3.02.04 |
| "Complete" Z80N | no change; shipped on no board; exposes a pre-existing BIT n,(HL) flag divergence | 3.02.04 |
| `tbblue.zip` boot code archive | relevant to jnext's build, as the official versioned boot code | 3.02.05 |
| F-key long-press timer | no user-visible effect today; jnext default is wrong (pre-existing; #578, section 4C) | all (3.02.03 on) |

Everything else (HDMI v3, anti-brick core, PS/2 keymap, pins, bitstreams) touches nothing jnext
emulates on issue 2 (section 5).

## 0. Sources and conventions

Clone: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA`, last fetched 2026-11-26 00:02.
Method: `git log` and `git diff` between the refs below, per file, plus `cores/zxnext/changelog.md`
and `cores/zxnext/nextreg.txt`.

| Ref | Tip | Date | Status |
|---|---|---|---|
| `master` (= `origin/master`) | b047011 | 2025-11-06 | 3.02.03. jnext's VHDL citations refer to this |
| `origin/30204` | dc8d764 | 2026-05-28 | 3.02.04, **released**: changelog entry, TBU and bitstreams for issue 2, 4, 5 |
| `origin/30205` | 232ea8c | 2026-11-16 | 3.02.05, **unreleased work in progress**: no changelog entry, no bitstreams |

- `master..origin/30204` is 36 commits; `origin/30204..origin/30205` is 10. `30205` contains all of `30204`, and `master` has nothing `30204` lacks.
- Citations: "03:", "04:", "05:" = the file on `master`, `origin/30204`, `origin/30205`, under `cores/zxnext/src/`. jnext paths are under `src/` of this repository.

**Boards.** 3.02.04 adds a build generic `g_option` (04:`zxnext.vhd:40-66`):
- issue 2 sets `X"0000"` (04/05:`zxnext_top_issue2.vhd:41`): none of the new modules;
- issue 4 and 5 set `X"FFF2"` (04:`zxnext_top_issue4.vhd:40`, `zxnext_top_issue5.vhd:40`): the Z80N with the debug wedge (not the "complete" one), every Pi debug command, the tzx hotkeys and SRAM port C.

jnext emulates issue 2 (`MachineType::ZXN_ISSUE2`, `core/emulator_config.h:151`). So everything
gated by `g_option`, or by `g_board_issue >= 2` in 3.02.05, is visible in jnext only if it ever
emulates issue 4 or 5.

## 1. Summary: changes that reach jnext

"jnext today": **differs** = jnext has the 3.02.03 behaviour; **matches** = jnext already behaves
like the newer core.

| Ver | Change | Type | Boards | Visible to | jnext today |
|---|---|---|---|---|---|
| 04 | NR 0x0E sub-version 0x04 (05: 0x05) | version | all | programs reading the core version | differs: 0x03 (`port/nextreg.cpp:297`) |
| 04 | New boot ROM: external/internal SD choice, settings in flash, multi-card SD init, NextZXOS 2.10; GPL3 | feature + fixes | all | the user at power-on | differs: 3.02.03 boot code |
| 04 | Boot ROM overlays 0x0000-0x1FFF only; 0x2000-0x3FFF shows the normal mapping | feature | all | boot code | differs (`memory/mmu.h:1627-1637`) |
| 04 | NR 0x0A bit 5 (SD swap) changes only when bit 2 of the same write is 1 | fix | all | firmware in config mode | differs (`core/emulator.cpp:2678-2681`) |
| 04 | SPI master: one transfer per port 0xEB access even while the DMA holds the strobe | fix | all | DMA to/from the SD card | matches |
| 04 | MD six-button pad must also show the three-button signature | fix | all | joystick detection | differs in code, not in effect |
| 04 | Membrane NMI+key hotkeys repeat within one press (regression) | bug introduced | all | the user (NMI+F7, NMI+F8 step several times) | not affected |
| 05 | Hotkey repeat fixed | fix | all | the user | not affected |
| 04 | Pi debug control (Pi reads/writes memory, NextREGs, CPU registers; pause/step) and NMI+QWERT tape hotkeys for the Pi | feature | 4, 5 | NextPi software | not emulated (#393) |
| 04 | "Complete" Z80N (MEMPTR, X/Y flags): built, shipped on no board | feature | none | nobody | no change; exposes a pre-existing jnext divergence (2.6) |
| 05 | `src/rom/tbblue.zip`: official versioned boot code for emulators | feature | - | jnext's build | relevant (2.1) |

## 2. 3.02.04 (`origin/30204`, released)

Changelog entry: 04:`changelog.md:19`. It includes the MD six-button fix (2.4). The NR 0x0A lock (2.3) and the SPI master fix (2.4) are not in it.

### 2.1 Version and boot ROM

- **Version** (61b3fb5): `g_sub_version` `X"04"` in all three tops. NR 0x01 stays 0x32. jnext reports 0x32/0x03 (`port/nextreg.cpp:230`, `:297`).
- **Boot ROM** (6e695ec, faf9622, 01676e2, e52804d, 8fe233f, 00bc816, b534c56, bb80f66, a1ccea8): the 8 KB code in `rom/bootrom.vhd` is replaced.
  - Identity, by sha256 of the bytes in each branch's `bootrom.vhd`: `master` = `boot-30100.bin` in 05:`rom/tbblue.zip` = jnext's `roms/nextboot.rom` (`ee0b99c5...`); `30204`/`30205` = `boot-30204.bin` (`33f04fd1...`).
  - Per the commit messages and the changelog:
    - the user chooses boot from the external or the internal SD card;
    - settings are kept in flash;
    - SD init no longer locks up some cards when two share the SPI bus (01676e2);
    - it patches the firmware at run time "to get around board id check" (01676e2);
    - better error reporting (faf9622) and NextZXOS 2.10 support (00bc816);
    - it uses the NR 0x0A lock (2.3), with a longer timeout and a stack moved "to a safer location for large legacy firmwares" (a1ccea8).
  - It is now z88dk-built code. I did not disassemble it, so its exact SD, flash and NextREG sequence is unknown.
  - Licence (`tbblue.zip` README): from 30204 the boot code is GPL3 with a freeware FZX font. Earlier boot code contains the Sinclair ROM font and is usable only under Amstrad's permission for non-commercial emulators.
  - 3.02.05 adds `tbblue.zip` (228cf78). It holds `boot-30100.bin`, `boot-30200-ab.bin`, `boot-30204.bin`, `boot-30204-ab.bin`, the deprecated `boot-30204rc.bin` and `boot-30204rc-ab.bin` ("retained for use by existing emulators"), `README.md` and `GPL3-LICENSE`. It is on `30205` only.
- **jnext impact**: the user would see the new boot screens and the SD choice. Running `boot-30204.bin` needs:
  - the 8K overlay (2.2);
  - a second SD socket, which jnext has (`core/emulator.cpp:7113-7114`);
  - the NR 0x0A lock (2.3);
  - possibly a flash device. jnext decodes the flash select (`peripheral/spi.cpp:143`) but attaches no device to it, and whether the new code tolerates an absent or blank flash is untested.

### 2.2 Boot ROM overlay reduced to 8K

13f1f1e: `cpu_di` takes the boot ROM when `cpu_a(15 downto 13) = "000"` (04:`zxnext.vhd:2143`), not `cpu_a(15 downto 14) = "00"` (03: `:1856`).
- Reads of 0x2000-0x3FFF now go through the normal decode (04:`:3323-3339`), in this order:
  1. the Multiface ROM/RAM if it is paged in;
  2. else the RAM page if MMU slot 1 maps RAM;
  3. else, in config mode, the upper 8K of the NR 0x04 bank (`:3338-3339`);
  4. else the ROM.

  Before, they read a mirror of the 8K ROM, which is addressed by `cpu_a(12:0)` (04:`:3497`).
- Writes are unaffected.
- **jnext**: `memory/mmu.h:213-219` and `:1627-1637` implement the 16K window with the mirror. This change and the new boot ROM go together: the 3.02.04 FPGA never runs the old boot code.

### 2.3 NR 0x0A SD-swap lock

6395c28: in config mode, bit 5 (the SD0/SD1 swap, new in 3.02.03) is written only when bit 2 of the same write is 1 (04:`zxnext.vhd:5702-5704`; 03: `:5192-5195`).
- Commit message: some older firmware flipped bit 5 by accident in config mode.
- Bit 2 is not stored and reads 0 (04:`:6446`).
- **jnext**: `core/emulator.cpp:2678-2681` applies bit 5 whenever config mode is on. Adopting the lock means also requiring `v & 0x04`. I did not check whether the firmware jnext boots ever writes bit 5 = 1 in config mode, i.e. whether jnext today shows the accidental swap this lock prevents.

### 2.4 SPI master and MD pads

- **SPI master** (515ec76, "Correct SPI wait signal to DMA"): a `spi_active` latch (04:`serial/spi_master.vhd:62`, `:82-94`) starts a transfer only on a new assertion of the port 0xEB strobe.
  - In 3.02.03 (03: `:82`), a strobe still high at the end of a byte started another one. That happens when the DMA's I/O cycle is stretched by `spi_wait_n`.
  - That mechanism is my reading of the code; the commit message gives no detail.
  - **jnext**: matches. `SpiMaster` exchanges exactly one byte per port access and models no wait (`peripheral/spi.h:94-105`).
- **MD six-button identification** (c833fbd, 04:`input/md6_joystick_connector_x2.vhd:138-170`): a pad counts as six-button only if it also showed the three-button signature (pins 3 and 4 low) earlier in the same scan. 3.02.03 tested pins 1 and 2 only (03: `:158`, `:161`). This fixes Atari digital paddles being taken for six-button pads.
  - **jnext**: `input/md6_connector_x2.cpp:194-207` has the 3.02.03 test.
  - jnext sets its six-button flag when UP and DOWN are both active at phase 8/9 (`:194-203`); the 3.02.04 rule would also require the three-button signature.
  - Its model has no pin-level three-button signature: phases 4/5 latch START/A unconditionally (`:165-177`).
  - A host gamepad cannot hold UP and DOWN together, so it is unaffected. Only a keyboard-mapped joystick could, so for programs the difference is practically invisible and adopting the rule is for fidelity.

### 2.5 Membrane hotkey regression

2ef056a (the QWERT tape-hotkey commit) changed `input/membrane/emu_fnkeys.vhd` for every board: the function-key latch is now cleared by an `else` that includes `S_MF_CHECK` (04:`emu_fnkeys.vhd:261-262`). 3.02.03 cleared it only in `S_IDLE` (03: `:196-197`).
- While NMI + a digit is held, the loop A11 -> A12 -> CHECK sets, then clears, the latch on each pass.
- `zxnext.vhd` edge-detects `hotkeys_0` (04:`:6862-6883`), so one press gives several pulses: F7 scanlines and F8 CPU speed step more than once.
- Fixed in 3.02.05 (3.1). Derived from the code and the 3.02.05 commit message, not tested on hardware.
- **jnext**: not affected and no change needed.
  - `input/emu_fnkeys.h:77-85` keeps the 3.02.03 states, including MF_CHECK.
  - Side effects fire on a rising edge of a `local_fnkeys_` bit while the FSM is in MF_ROW_A11/A12 (`input/emu_fnkeys.cpp:68-113`, called from `tick()` at `:274`; comment `:266-273`). The header prose at `emu_fnkeys.h:56-58` says MF_DONE and is stale.
  - Host F-keys reach it only through `simulate_mf_fkey_press()` (`emu_fnkeys.cpp:307-330`), which holds M1 for exactly one A11/A12 pass. With the 3.02.03 latch that gives one edge per press.

### 2.6 Z80N variants (and a pre-existing jnext divergence)

6d417a5 and the Pi debug commits add `cpu/t80n-complete/`, `cpu/t80n-debug/` and `cpu/t80n-complete-debug/`. The old core moves unchanged to `cpu/t80n/`.
- `t80n-debug` (issue 4/5) is `t80n` plus a register read/write port. Its ALU and microcode differ only in entity names, so it executes like the 3.02.03 core.
- `t80n-complete` adds MEMPTR tracking (`SetWZ`, `LDW`) and undocumented-flag fixes. From the diff against `t80n/`, in `cpu/t80n-complete/`:
  - X/Y from WZ for BIT n,(HL)/(IX+d) (`t80n_alu_complete.vhd:293-299`);
  - X/Y for LD A,I and LD A,R (`t80n_complete.vhd:1244-1245`, `:1259-1260`);
  - X/Y for IN r,(C) (`:1294-1295`);
  - X/Y for the `I_BC` (block compare) case (`:1337-1341`);
  - N/C/H/P for the `I_BTR` (block I/O) case (`:678-686`).

  The instruction names for `I_BC`/`I_BTR` come from the T80 signal naming; I did not trace them through the microcode.
  - No released build uses it: issue 4/5 used it until b534c56, and the release uses `"10"`.
- **jnext**: no change follows from the release. A divergence from the 3.02.03 VHDL showed up, though, and it is not caused by any version:
  - jnext's standard opcodes come from the vendored FUSE core, whose BIT n,(HL) takes X/Y from MEMPTR (`third_party/fuse-z80/z80_macros.h:207-213`);
  - every shipped T80N forces X=Y=0 there (03:`cpu/t80n_alu.vhd:287-292`).

  So for flags, jnext behaves like `t80n-complete` and like real Z80 silicon, not like the Next's CPU. I verified this for BIT n,(HL) only (FUSE `z80_cb.c:277-281` uses `BIT_MEMPTR`). For the other cases above, jnext's behaviour is **not verified**: it is an inference from FUSE passing its MEMPTR/X/Y test suite.

### 2.7 Pi debug control (issue 4, 5 only)

See the prior document for the protocol.
- What a jnext user would see: NextPi software could read and write the Next's memory, NextREGs and CPU registers, pause and single-step the CPU, and trigger resets, NMIs and a CTC channel 3 interrupt.
- On the Next, the NMI+QWERT tape hotkeys (04:`emu_fnkeys.vhd:170-223`, `zxnext.vhd:6915-6927`) reach only the Pi, through STATUS.
- NR 0xA0 bit 1 enables it. 3d63400 makes the bit:
  - stored on every board (04:`zxnext.vhd:6070`);
  - kept across a soft reset (04:`:5586-5587`; 03: `:5080` cleared all bits);
  - read as 0 where the slave is not built (04:`:6723`).

  On issue 2 a program reads 0 as before, so jnext's full NR 0xA0 reset (`core/emulator.cpp:635`) is indistinguishable there.
- On issue 2 the stored bit still switches the Pi GPIO 7-11 routing: GPIO 7/8 become inputs and the master-mode MISO is masked (04:`:2568-2600`). This is invisible in jnext, which attaches SPI devices only to the two SD selects (`core/emulator.cpp:7113-7114`).
- **jnext**: not emulated. #393, blocked on a QEMU SPI link.

## 3. 3.02.05 (`origin/30205`, unreleased, work in progress)

Latest commit 2026-11-16, no changelog entry, no bitstreams; recheck before relying on this.

### 3.1 Changes that reach jnext

- **Version** (90f139e): NR 0x0E = 0x05 in all three tops.
- **Hotkey fix** (991d9d4): `S_MF_CHECK` is removed from both FSM variants, so the latch is never cleared mid-press and each activation gives one edge (05:`emu_fnkeys.vhd:63`, `:118-165`, `:167-217`).
  - This restores the 3.02.03 behaviour, one edge per press, but not the 3.02.03 FSM: the M1 release is now tested in S_MF_ROW_A12, and the latch is also cleared in S_RESET_CHECK.
  - jnext keeps MF_CHECK; there is no observable difference (2.5).
- **Boot code archive** (228cf78): see 2.1.

Everything else on `30205` (HDMI v3, the AB core) is in section 5.

## 4. What jnext would need

Each item applies only if the owner adopts that core version.

**A. 3.02.04 (or .05) on issue 2, the emulated board.**
1. NR 0x0E = 0x04 (0x05).
2. `boot-30204.bin` together with the 0x0000-0x1FFF overlay (2.1, 2.2). Open questions: does it need a flash device, and does NextZXOS boot with it on jnext's two SD sockets? Its GPL3 licence is cleaner than the current boot code's.
3. NR 0x0A: apply bit 5 only when bit 2 = 1 (2.3).
4. MD pad three-button condition, for fidelity only (2.4).

Already matching: the SPI master fix, the hotkey behaviour (do not reproduce the 3.02.04 repeat), NR 0xA0 as seen on issue 2.

**B. Issue 4 or 5.**
- The Pi debug control and the NMI+QWERT row (2.7).
- NR 0x0F board id.
- For 3.02.05, HDMI v3's NR 0x11 modes and wall-clock pacing (section 5).

A core-by-board selector is #394.

**C. Independent of version (owner's call).**
- The BIT n,(HL) flag divergence in 2.6, and possibly the unverified other cases listed there.
- The F-key long-press timer:
  - In the VHDL, every top passes `CLOCK_EN_PERIOD_MS => 10` and `BUTTON_PERIOD_MS => 1000` (03:`zxnext_top_issue2.vhd:1961-1962`), and `PERIOD` = 1000 / 10 = 100 enable ticks (04:`input/membrane/emu_fnkeys.vhd:57`).
  - The enable `CLK_28_DEBOUNCE_EN` has a 9.36 ms period (04:`zxnext_top_issue2.vhd:1218`), so the real short/long-press threshold is about 936 ms.
  - jnext's `EmuFnKeys` defaults to 2000 ticks, and its header says "2000ms ... 1ms" (`input/emu_fnkeys.h:72-73`, `:88-89`, `:243`; used at `emu_fnkeys.cpp:128`). Both the default and the text are wrong; it should be 100 ticks of a 9.36 ms enable.
  - No user-visible effect today: only `simulate_mf_fkey_press` and the tests drive the FSM, and they never time a long press.

## 5. Not relevant to jnext

One line each: these touch nothing jnext emulates on issue 2 (video output encoders, clocks, pins, build).

- HDMI v3 on issue 4/5 (dd770ec, 232ea8c):
  - frame-locked per-mode system clock (05:`pll/A7-Issue-5-V3/clock_system.vhd:692-708`);
  - NR 0x11 gains bits 4:3 and is writable outside config mode with bit 7 (05:`zxnext.vhd:5797-5825`; the issue 2 path is unchanged, `:5814-5824`);
  - issue 5's default fixed-60 Hz mode runs 50 Hz machines about 20% fast;
  - the "snow" fix in `hdmi_frame.vhd`.
- `zxula_timing.vhd` (3.02.05): only the HDMI window constants move and gain 60 Hz variants (05:`:276-391`); the ULA counters and timing are unchanged.
- HDMI reset no longer pulsed on a scandoubler change (05:`zxnext.vhd:7449`).
- Anti-brick core (machine id 0xFA): DMA and sprites left out (2b42b0d, dd770ec); the DivMMC/Multiface exclusion of a8acb4e was commented out again.
- PS/2 keymap: expansion bus on/off moves from F11/F12 to F5/F6, and F12 becomes a second PS/2 reset (6b33c4f). jnext has no PS/2 keymap model (`core/emulator.cpp:2917-2927`), and its SDL frontend already sends host F5/F6 to the F-key FSM (`platform/sdl_app.cpp:205-211`).
- Flash/SD SPI pin isolation in the top levels (0c06f07). jnext's SPI devices are separate per select anyway.
- Pi debug plumbing inside the core: bus arbiter, SRAM port C, BRAM and NextREG arbitration. It includes a bank 7 read latch its author marks "unexplainable" (04:`zxnext.vhd:7313`), issue 4/5 builds only.
- Debug-wedge CPU register port (`cpu/readme.txt`), issue 4/5.
- VGA 24-bit test pattern on MD X+Y+Z (d4ae05e, 4ec6fc4), issue 5.
- Issue 5 board fixes: SD pins LVCMOS (801a060), one VGA sync delay too many (9ce564d), PLL power-up programming (a12ecc3).
- SD pins LVCMOS33 on issue 2/4 (bfc06d0).
- Issue 2 HDMI and VGA bitstreams in one `TBBLUE.TBU`, with a display field in the TBU record (e5368a4, 9819c9d). `TBBLUE.AB.TBU` added; `zxnext-2/4-30200.bin` added with no stated purpose (dc8d764).
- Issue 2 top in 3.02.05: video-mode width and the clock-buffer generate condition (same choice for the shipped `g_video_inc` values).
- IP netlists and project files (231a330 and others).
- Simulation-only sensitivity-list fix in `pi_debug_control*.vhd` (dd770ec).
- `nextreg.txt` text:
  - NR 0x7F "hard reset" (the VHDL is unchanged, and jnext already keeps it across a soft reset, `port/nextreg.cpp:94-99`);
  - NR 0x68 bit 2 loses "(may change)";
  - whitespace (0b3b973).

## 6. Uncertain or not verified

- The new boot code's behaviour beyond its commit messages (2.1). It was not disassembled.
- Whether jnext's firmware writes NR 0x0A bit 5 in config mode (2.3).
- The 3.02.03 double SPI transfer under DMA (2.4): read from the code, not measured.
- The hotkey repeat in released 3.02.04 (2.5): from code and commit message, not tested on hardware.
- jnext vs T80N flag divergences other than BIT n,(HL) (2.6).
- The purpose of the bank 7 "unexplainable" latch and of `zxnext-*-30200.bin` (section 5).
- 3.02.05 is moving; section 3 can change before release.
