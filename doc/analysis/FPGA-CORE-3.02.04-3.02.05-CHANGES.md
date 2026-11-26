# FPGA cores 3.02.04 and 3.02.05: changes against 3.02.03

Analysis of 2026-11-26. Baseline: core 3.02.03, the one jnext emulates. It lists every change on
the `30204` and `30205` branches of the official FPGA repository, says what each one does, and
whether it reaches anything jnext emulates. Nothing here is decided.

It extends [FPGA-CORE-3.02.04-PI-DEBUG-CONTROL.md](FPGA-CORE-3.02.04-PI-DEBUG-CONTROL.md)
(2026-10-18), which covers the Pi debug control in depth (protocol, QEMU side). That part is not
repeated here. Since that document, `30205` gained five commits: the hotkey fix, HDMI v3, a
sprite-less anti-brick core and the boot-code archive.

## 0. Sources and conventions

Clone: `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA`, last fetched 2026-11-26 00:02.
Method: `git log` and `git diff` between the refs below, per file, plus `cores/zxnext/changelog.md`
and `cores/zxnext/nextreg.txt`.

| Ref | Tip | Date | Status |
|---|---|---|---|
| `master` (= `origin/master`) | b047011 | 2025-11-06 | 3.02.03. jnext's VHDL citations refer to this |
| `origin/30204` | dc8d764 | 2026-05-28 | 3.02.04, **released**: changelog entry, TBU and bitstreams for issue 2, 4, 5 in `tbumaker/` |
| `origin/30205` | 232ea8c | 2026-11-16 | 3.02.05, **unreleased work in progress**: no changelog entry, no bitstreams, NextREG 0x0E reads 0x05 |

- `master..origin/30204` is 36 commits; `origin/30204..origin/30205` is 10. `30205` contains all of `30204`. `master` has no commit that `30204` lacks, so the merge of `master` into `30205` (15e206d) adds nothing.
- File:line citations: `zxnext.vhd` etc. are under `cores/zxnext/src/`. "04:" means the file on `origin/30204`, "05:" on `origin/30205`, "03:" on `master`.
- Types: **feature**, **fix** (bug fix), **internal** (build, packaging, board top level, pin constraints, simulation-only).

### Build options (`g_option`), and which board gets what

3.02.04 adds a generic `g_option` (04:`zxnext.vhd:40-66`). It is set per board in the top level:

| bits | meaning | issue 2 (`X"0000"`) | issue 4, 5 (`X"FFF2"`) |
|---|---|---|---|
| 1:0 | CPU: 00 t80n, 01 t80n-complete, 10 t80n-debug, 11 complete+debug | 00 | 10 (debug wedge, **not** complete) |
| 4 | Pi SPI slave, VERSION/STATUS | 0 | 1 |
| 5-11 | Pi SYSTEM, NEXTREG_R, NEXTREG_W, SRAM_R/W, CPU pause/resume/step, BRAM_R/W, CPU_REG_R/W | 0 | 1 |
| 12 | tzx hotkeys (NMI+QWERT) | 0 | 1 |
| 13 | SRAM port C | 0 | 1 |

- Values: 04/05:`zxnext_top_issue2.vhd:41`, `zxnext_top_issue4.vhd:40` (05: `:41`), `zxnext_top_issue5.vhd:40` (05: `:41`).
- Until b534c56 (2026-05-11) issues 4 and 5 used `X"FFFF"`, i.e. the complete+debug CPU. The released 3.02.04 dropped "complete".
- 30205 also uses `g_board_issue >= 2` (issue 4, 5) to select HDMI v3 (05:`zxnext.vhd:1745`).
- jnext emulates issue 2 (`MachineType::ZXN_ISSUE2`, `src/core/emulator_config.h:151`). So everything gated by `g_option` or by `g_board_issue >= 2` is outside what jnext emulates today.

## 1. Summary

"jnext" column: **N/A** = not an emulated area, or a board jnext does not emulate; **matches** =
jnext already behaves like the newer core; **differs** = jnext has the 3.02.03 behaviour of an
emulated area.

| Ver | Change | Type | Boards | jnext |
|---|---|---|---|---|
| 04 | NextREG 0x0E = 0x04 | internal | all | differs: reports 0x03 (`nextreg.cpp:297`) |
| 04 | Pi debug control (SPI slave on GPIO 7-11, NR 0xA0 bit 1) | feature | 4, 5 | N/A (prior doc, #393) |
| 04 | NR 0xA0 bit 1 stored on every board, kept on soft reset, reads 0 without the slave | feature | all (visible on 4, 5) | matches on issue 2 (reads 0 either way) |
| 04 | CPU bus arbiter Pi/DMA, SRAM port C, BRAM and NextREG access arbitration for the Pi | feature | 4, 5 | N/A |
| 04 | Debug-wedge and "complete" T80N variants | feature | debug on 4, 5; complete on none | N/A; see 2.4 for a related pre-existing jnext divergence |
| 04 | NMI + Q/W/E/R/T tape hotkeys, read by the Pi | feature | 4, 5 | N/A |
| 04 | Membrane NMI+key hotkeys fire several times per press (regression, fixed in 05) | bug introduced | all | N/A: jnext's FSM never had it |
| 04 | New boot ROM: external/internal SD choice, settings in flash, multi-card SD init, NextZXOS 2.10, FZX font, GPL3 | feature + fixes | all | differs: jnext embeds the 3.02.03 boot code |
| 04 | Boot ROM overlay 0x0000-0x1FFF instead of 0x0000-0x3FFF | feature | all | differs (`mmu.h:1627-1637`) |
| 04 | NR 0x0A bit 5 (SD swap) changes only if bit 2 is written 1 | fix | all | differs (`emulator.cpp:2678-2681`) |
| 04 | SPI master: one transfer per port 0xEB access even if the strobe is held (DMA) | fix | all | matches (byte-level model) |
| 04 | Flash SPI traffic no longer appears on the SD SPI lines | fix | all (top levels) | matches (separate devices per select) |
| 04 | MD six-button pad must also show the three-button signature | fix | all | differs, unobservable with host gamepads |
| 04 | PS/2: expansion bus on/off F11/F12 -> F5/F6; F12 = second PS/2 module reset | feature | all | N/A (no PS/2 keymap model) |
| 04 | VGA test pattern on MD X+Y+Z | feature | 5 | N/A |
| 04 | Issue 2 separate HDMI/VGA builds in one TBU; TBU record gets a display field | internal | 2 | N/A |
| 04 | Issue 5: SD pin I/O standard, VGA sync delay, PLL power-up programming; issue 2/4 SD pins | fix | 5 (pins 2, 4) | N/A |
| 04 | nextreg.txt: NR 0x7F resets on hard reset only; "(may change)" dropped on NR 0x68 bit 2 | doc | - | matches (`nextreg.cpp:94-99`) |
| 05 | NextREG 0x0E = 0x05 | internal | all | differs |
| 05 | Hotkey multiple-edge regression fixed | fix | all | N/A |
| 05 | HDMI v3: frame-locked system clock per machine timing; NR 0x11 new modes, writable outside config mode with bit 7 | feature | 4, 5 | N/A (issue 2 path unchanged) |
| 05 | Issue 5 default: fixed 60 Hz HDMI; 50 Hz machines then run about 20% fast in wall time | feature | 5 | N/A |
| 05 | HDMI reset no longer triggered by a scandoubler change | internal | all | N/A |
| 05 | Anti-brick core without DMA and sprites | internal | AB core | N/A |
| 05 | `src/rom/tbblue.zip`: versioned boot code for emulators | feature | - | relevant: source of a GPL3 boot ROM |
| 05 | Sensitivity-list fix in `pi_debug_control*.vhd` | internal | 4, 5 | N/A |

## 2. 3.02.04 (`origin/30204`, released)

Changelog entry: 04:`changelog.md:19`. It lists BUILD, BOOT, STICKS, Z80, PI, TZX, PS/2. The
fixes in 2.7-2.9 are not in it.

### 2.1 Version

- 61b3fb5: `g_sub_version` `X"03"` -> `X"04"` in all three tops (04:`zxnext_top_issue2.vhd:38`). NR 0x01 stays 0x32.
- jnext: NR 0x01 = 0x32, NR 0x0E = 0x03 (`src/port/nextreg.cpp:230`, `:297`).

### 2.2 Pi debug control and its plumbing (issue 4, 5)

Commits c76753b, c229a60, da249dc, 7d5a44e, 5883c07, 8b2eba6, 3d63400. Protocol and the QEMU
side: see the prior document. What the integration touches in `zxnext.vhd`:

- **SPI slave** `serial/spi_slave.vhd` and `device/pi_debug_control/pi_debug_control_param.vhd`, generated when `g_option(4)` (04:`zxnext.vhd:3629-3825`).
- **GPIO 7-11 mux** (04:`zxnext.vhd:2568-2601`; 03: `:2281`, `:2293-2307`).
  - NR 0xA0 bit 1 = slave mode. It overrides bit 0 (master mode): GPIO 7, 8, 10, 11 become inputs and GPIO 9 drives MISO.
  - The master path's MISO is forced to 0 while slave mode is on (04:`:2593`).
- **NR 0xA0** (3d63400):
  - the write stores all 8 bits on every board (04:`zxnext.vhd:6070`);
  - the reset clears bits 7:2 and 0 but not bit 1 (04:`:5586-5587`; 03: `:5080` cleared all), so bit 1 survives a soft reset. A hard reset reloads the FPGA (multiboot, 04:`zxnext_top_issue2.vhd:1236`), which clears it;
  - the read is `... & '0' & (bit1 and g_option(4)) & bit0` (04:`:6723`; 03: `:6189`).
  - On issue 2, bit 1 reads 0 but still turns the pins around. nextreg.txt: "Internally the bit is set to avoid conflicts on gpio; must write 0 to disable completely".
- **Bus arbiter** `device/arbiter_dma.vhd` (04:`zxnext.vhd:2080-2108`). It shares the Z80 BUSRQ between the DMA and the Pi pause. Without `g_option(9)`, `z80_busrq_n <= dma_busrq_n` as before (03:`:1842`).
- **SRAM port C**: lowest-priority SRAM port for the Pi (A CPU/DMA > B Layer 2 > C), in the tops (04:`zxnext_top_issue2.vhd:1331-1543`), and `zxnext.vhd:1787-1803`.
- **BRAM** banks 5 and 7: the CPU port is shared with the Pi (04:`zxnext.vhd:7138-7338`).
  - With `g_option(10)` the bank 7 CPU read gains a request/schedule stage and a falling-edge data latch. The author marks it `-- ***** unexplainable as of now` (04:`:1479`, `:7313`), so the purpose of that latch is not documented.
  - Without `g_option(10)` (issue 2) the bank 5/7 paths are the 3.02.03 ones.
- **NextREG access**: Pi writes go after the copper and CPU (04:`:5267-5283`). Pi reads time-share the read mux with the CPU's port 0x253B read (04:`:6393-6410`).
- **Pi SYSTEM commands** are ORed into the F1/F4/F9/F10 hotkey inputs (04:`:6866`) and into the CTC channel 3 unqualified interrupt (04:`:2233`).
- **CPU registers** through the debug-wedge T80N (2.4).
- **jnext**: N/A. jnext has no GPIO pin model (`pi_spi0_en()` has no caller), and it resets all of NR 0xA0 (`src/core/emulator.cpp:635`), the 3.02.03 behaviour. On issue 2 that is indistinguishable from 3.02.04, because bit 1 reads back 0.

### 2.3 TZX hotkeys and the hotkey regression

2ef056a. `input/membrane/emu_fnkeys.vhd` gains a generic `ENABLE_FN_EXT` (= `g_option(12)`):

- **Feature (issue 4, 5)**: while the NMI button is held, the FSM also scans row A10 (Q W E R T) and outputs `o_fnkeys_ext` (04:`emu_fnkeys.vhd:170-223`, `:271-300`).
  - `zxnext.vhd` edge-detects them (04:`:6915-6927`) and feeds them only to the Pi debug control's STATUS hotkeys byte.
  - They have no effect inside the Next.
- **Regression (all boards)**: in both FSM variants the function-key latch is now cleared by an `else` that includes `S_MF_CHECK` (04:`emu_fnkeys.vhd:261-262`; 03: `:196-197` cleared only in `S_IDLE`).
  - While NMI + a digit is held, the loop A11 -> A12 -> CHECK -> A11 sets, then clears, the latch each pass.
  - `zxnext.vhd` edge-detects `hotkeys_0` at 28 MHz (04:`:6862-6883`), so one press gives repeated pulses: F7 scanlines and F8 CPU speed step several times.
  - Fixed in 3.02.05 (3.2). The released 3.02.04 bitstreams contain it. This is derived from the code and from the 3.02.05 commit message, not tested on hardware.
- **jnext**: N/A.
  - `src/input/emu_fnkeys.h:77-85` models the 3.02.03 seven-state FSM and emits each F-key side effect once, on entry to MF_DONE, so it never had the repeat.
  - There is no A10 state. It would be needed only with the Pi debug control.

### 2.4 Z80 variants

6d417a5, c76753b, c229a60, da249dc, 5883c07. `src/cpu/` is reorganised:

- `t80n/` is the old core, moved unchanged (rename, 0 lines changed).
- `t80n-debug/` is `t80n/` plus a register read/write port and an `o_debug_lastM` output. The ALU and microcode differ only in entity names, and `t80n_debug.vhd` adds only the debug writes and the read process. So the CPU shipped on issue 4/5 3.02.04 executes like the 3.02.03 one.
- `t80n-complete/` and `t80n-complete-debug/` add WZ (MEMPTR) tracking (`SetWZ`, `LDW`) and undocumented-flag fixes. From the diff against `t80n/`:
  - X/Y from WZ for BIT n,(HL)/(IX+d) (`t80n_alu_complete.vhd:293-299`);
  - X/Y for LD A,I / LD A,R, for IN r,(C), and for the CPI/CPD family;
  - N/C/H/P for the block I/O instructions.

  The header says SCF/CCF X/Y still fail the reference tests. No shipped build uses it: `src/cpu/readme.txt` says it is too large for KS1, and issue 4/5 use `"10"`.
- **jnext**: no change is needed to match the shipped CPU. One pre-existing divergence from the 3.02.03 VHDL showed up, though:
  - jnext's standard opcodes come from the vendored FUSE core, whose BIT n,(HL) takes X/Y from MEMPTR (`third_party/fuse-z80/z80_macros.h:207-213`);
  - the shipped T80N forces X=Y=0 for BIT n,(HL) (03:`cpu/t80n_alu.vhd:287-292`).

  jnext therefore behaves like `t80n-complete` there, not like the shipped core. I verified this for BIT n,(HL) only. For the other flag cases listed above I infer it from FUSE passing its test suite, which checks X/Y and MEMPTR; I did not check them instruction by instruction. The divergence is not caused by 3.02.04.

### 2.5 Boot ROM

6e695ec, faf9622, 01676e2, e52804d, 8fe233f, 00bc816, b534c56, bb80f66, a1ccea8: the 8 KB boot code
(`rom/bootrom.vhd`, and `rom/bootrom_ab.vhd` for the anti-brick core) is replaced.

- **Identity, verified by sha256 of the bytes in each branch's `bootrom.vhd`:**
  - `master` = `boot-30100.bin` in 05:`rom/tbblue.zip` = jnext's `roms/nextboot.rom` (`ee0b99c5...`);
  - `30204` and `30205` = `boot-30204.bin` (`33f04fd1...`).
- **What the commit messages and the changelog say it does:**
  - boot from the external or the internal SD card under user control, and keep settings in flash;
  - SD init that does not lock up some cards when two share the SPI bus (01676e2);
  - patch the firmware at run time "to get around board id check" (01676e2);
  - better error reporting (faf9622);
  - NextZXOS 2.10 support (00bc816);
  - use the NR 0x0A lock bit (2.7), a longer timeout, and a stack moved "to a safer location for large legacy firmwares" (a1ccea8).

  It is now z88dk-built code; the 3.02.03 one carries the old error strings ("Error opening TBBLUE.FW file"). I did not disassemble it, so its exact SD, flash and NextREG sequence is unknown.
- **Licence** (`tbblue.zip` README):
  - from 30204 the boot code is GPL3 and uses a freeware FZX font;
  - earlier boot code contains the Sinclair ROM font and is usable only under Amstrad's permission for non-commercial emulators.
- **jnext**: differs. It embeds the 3.02.03 code (`src/core/embed_rom.cmake`). Running `boot-30204.bin` would need:
  - the 8K overlay (2.6);
  - a second SD socket, which jnext already has (`emulator.cpp:7113-7114`);
  - the NR 0x0A lock (2.7);
  - possibly a flash device: jnext decodes the flash select (`src/peripheral/spi.cpp:143`) but attaches no device to it. Whether the new code tolerates an absent or blank flash is untested.

### 2.6 Boot ROM overlay reduced to 8K

13f1f1e, one line: the `cpu_di` mux takes the boot ROM when `cpu_a(15 downto 13) = "000"` (04:`zxnext.vhd:2143`), not `cpu_a(15 downto 14) = "00"` (03: `:1856`).

- 0x2000-0x3FFF now reads whatever the normal decode selects. In config mode that is the upper 8K of the NR 0x04 bank (04:`:3338-3339`); before, it read a mirror of the 8K ROM (the ROM is addressed by `cpu_a(12:0)`, 04:`:3497`).
- Writes are unaffected: the boot ROM was never in the write path.
- **jnext**: differs. `src/memory/mmu.h:213-219` and `:1627-1637` implement the 16K window with the mirror. This change and the new boot ROM go together: the 3.02.04 FPGA never runs the old boot code.

### 2.7 NR 0x0A SD swap lock

6395c28: in config mode, bit 5 (SD0/SD1 swap, new in 3.02.03) is written only when bit 2 of the same write is 1 (04:`zxnext.vhd:5702-5704`; 03: `:5192-5195`). The commit message says some older firmware flipped bit 5 by accident in config mode.

- Bit 2 is not stored and reads 0 (04:`:6446`).
- The boot ROM uses the lock from a1ccea8.
- **jnext**: differs. `src/core/emulator.cpp:2678-2681` applies bit 5 whenever config mode is on. I did not check whether the firmware jnext boots ever writes bit 5 = 1 in config mode, i.e. whether jnext shows the swap this lock prevents.

### 2.8 SPI master: one transfer per access

515ec76 ("Correct SPI wait signal to DMA"): a `spi_active` latch (04:`serial/spi_master.vhd:62`, `:82-94`).

- A transfer can now start only on a new assertion of the port 0xEB read/write strobe (03: `:82` restarted whenever the strobe was high at the last state).
- With the DMA, `spi_wait_n` stretches the I/O cycle until the byte ends. Reading the 3.02.03 code, the still-high strobe then began a second transfer in the same cycle. That mechanism is my reading; the commit message gives no detail.
- **jnext**: matches. `SpiMaster` exchanges exactly one byte per port access and models no wait (`src/peripheral/spi.h:94-105`).

### 2.9 Other always-built fixes

- **Flash/SD SPI line isolation** (0c06f07, all three tops): SCK/MOSI reach the SD pins only while flash is deselected, and the flash pins only while both SD selects are high (04:`zxnext_top_issue2.vhd:2232-2256`).
  - jnext: matches; each SPI device sees only its own select.
- **MD six-button identification** (c833fbd, 04:`input/md6_joystick_connector_x2.vhd:138-170`): a pad counts as six-button only if it also showed the three-button signature (pins 3 and 4 low) earlier in the same scan; 03: `:158`, `:161` tested pins 1 and 2 only. It fixes Atari digital paddles being taken for six-button pads.
  - jnext: differs in code, not in effect. `src/input/md6_connector_x2.cpp:194-207` has the 3.02.03 test.
  - Its model has no pin-level three-button signature: phase 4/5 latch START/A unconditionally (`:165-177`), so every host gamepad is a real MD pad and the result is the same.
- **PS/2** (6b33c4f):
  - keymap ROM: F5 (scan code 0x03) and F6 (0x0B) now carry the expansion-bus on/off function, F11 (0x78) and F12 (0x07) none (04:`input/keyboard/keymaps.vhd:109-139`);
  - releasing an unprogrammed F12 resets the PS/2 matrix, like PAUSE/BREAK (04:`input/keyboard/ps2_keyb.vhd:246-247`).
  - jnext: N/A. It drops PS/2 keymap writes (`emulator.cpp:2917-2927`), drives the matrix from host keys, and gives F5/F6 no expansion-bus side effect (`src/gui/main_window.cpp:3670-3682`).

### 2.10 Board-level and build changes

None of these reaches jnext.

| Commit | Change | Boards |
|---|---|---|
| e5368a4, 9819c9d, dc8d764 | `maketbu.c`: issue 2 VGA and HDMI bitstreams in one `TBBLUE.TBU`; a TBU record's board byte gains a display field (bits 5:4); `TBBLUE.HDMI.TBU` removed, `TBBLUE.AB.TBU` added | 2 |
| 801a060, bfc06d0 | SD lines to LVCMOS33 | 5, then 2 and 4 |
| 9ce564d | one clock of delay too many on VGA hsync/vsync removed | 5 |
| a12ecc3 | PLL missing its programming signal at power-up, so VGA did not display on the default setting | 5 |
| d4ae05e, 4ec6fc4 | VGA 24-bit test pattern while X+Y+Z is held on either MD pad (`video/vga/video_test_pattern.vhd`, 04:`zxnext_top_issue5.vhd:1722-1736`) | 5 |
| 231a330 | IP netlist regeneration ("See if this solves odd build issue for others") | 5 project |
| 6d417a5, c76753b, c229a60 | ISE/Vivado project files list the new sources | all |

- **Unclear:** `tbumaker/` also gained `zxnext-2-30200.bin` and `zxnext-4-30200.bin` (dc8d764; the issue 4 one is a byte-size-identical rename of `zxnext-4-30203.bin`). No commit message says what these are.

### 2.11 Documentation only

`nextreg.txt`:
- NR 0x7F user register: "soft reset = 0xff" -> "hard reset = 0xff". The VHDL is unchanged: the register has an initial value and no reset term in either branch (03:`zxnext.vhd:1216`). jnext already keeps it across a soft reset (`nextreg.cpp:94-99`, `:216`).
- NR 0x68 bit 2 loses "(may change)".
- NR 0x0A bit 2 and NR 0xA0 bit 1 are documented (2.7, 2.2).

## 3. 3.02.05 (`origin/30205`, unreleased, work in progress)

No changelog entry, no bitstreams. The latest commit is 2026-11-16, so it is still moving; recheck before relying on any of this.

### 3.1 Version

- 90f139e: `g_sub_version` `X"05"` in all three tops (05:`zxnext_top_issue2.vhd:38`).

### 3.2 Hotkey fix

991d9d4 removes `S_MF_CHECK` from both FSM variants. A12 now goes straight back to A11 (or A10) while NMI is held, so the latch is never cleared mid-press, and each activation gives one edge (05:`emu_fnkeys.vhd:63`, `:118-165`, `:167-217`).

- This repairs the 3.02.04 regression (2.3) and restores the 3.02.03 single-edge behaviour.
- jnext: N/A (2.3).

### 3.3 HDMI v3 (issues 4 and 5)

dd770ec, 232ea8c.

- **Clocks.** New `pll/A7-Issue-4-V3/` and `pll/A7-Issue-5-V3/`; the old `pll/A7/` and `pll/A7-Issue-5/` are deleted.
  - The system clock is reprogrammed per machine timing so that it is frame-locked to a fixed 27 MHz HDMI pixel clock (576p50 or 480p60).
  - Table at 05:`pll/A7-Issue-5-V3/clock_system.vhd:692-708`. The system clock ranges from 27.955200 MHz (50 Hz 48K) to 34.372028 MHz (Pentagon in 60 Hz HDMI).
  - VGA modes 0-6 keep their old clocks but no longer produce HDMI.
  - While the clocks relock, the system clocks are paused (`clock_system.vhd:21-35`).
- **NR 0x11, issue 4/5 only** (05:`zxnext.vhd:5797-5825`, read `:6558-6564`; `nextreg.txt` 0x11):
  - bits 4:0: `01xxx` = exact 50 and 60 Hz HDMI frames; `10xxx` = fixed 60 Hz HDMI, where 50 Hz machines run about 20% faster;
  - writable outside config mode when bit 7 = 1, without a reset. A config-mode write with bit 7 = 0 still resets;
  - the read-back is computed from the mode and NR 0x05.
  - Commit message: firmware 1.45 cannot set these modes.
- **Defaults** (`g_video_def`, 05:`zxnext_top_issue4.vhd:36`, `zxnext_top_issue5.vhd:36`):
  - issue 4 `"01000"` (HDMI 50/60 Hz);
  - issue 5 `"10000"` (fixed 60 Hz, with `g_enable_50_in_60 = '1'` and an 80K line buffer).
- **ULA:** `video/zxula_timing.vhd` gets an `i_hdmi_fixed_60` input and moves the `c_hdmi_*` window constants into their own process, with 60 Hz-frame variants (05:`:276-391`).
  - The diff touches no other constant: the ULA counters and timing are unchanged.
- **HDMI frame:** `video/hdmi/hdmi_frame.vhd` reworked; 232ea8c fixes "random snow" with the large 50-in-60 buffer.
- **All boards:** `video_timing_change` (which drives `o_HDMI_RESET`) no longer includes the scandoubler bit (05:`zxnext.vhd:7449`; 04: `:7346`). On issue 4/5 the HDMI reset is pulsed every vsync and the top level decides (05:`:7459-7464`).
- **Issue 2:**
  - NR 0x11 keeps the v2 path, byte for byte the 3.02.04 logic (05:`zxnext.vhd:5814-5824`);
  - top-level edits: video mode width 3 -> 5 bits;
  - the clock-buffer generate now keys on `g_video_inc(1)` instead of `g_video_inc(0)`, which is the same choice for the two shipped values `"10"` and `"01"` (05:`zxnext_top_issue2.vhd:1066`, `:1111`).
- **jnext**: N/A. Its NR 0x11 handler is the issue 2 one (`emulator.cpp:2397-2420`) and stays correct. If issue 4/5 were ever emulated:
  - NR 0x11 would need the v3 semantics;
  - the host pacing would need the per-mode clock, because a 50 Hz machine in issue 5's default mode produces 60 frames per wall-clock second.

### 3.4 Anti-brick (AB) core

- a8acb4e left DivMMC and Multiface out of the AB build (`g_machine_id = X"FA"`), and 2b42b0d left out the DMA.
- dd770ec then commented out the DivMMC/Multiface exclusion (05:`zxnext.vhd:4669-4680`, `:4825-4836`) and added a sprite exclusion (05:`:4892-4900`).
- Net state at the tip: the AB core has no DMA and no sprites (05:`:2046-2066`, `:4892`).
- jnext: N/A (it emulates the regular core, machine id 0x0A).

### 3.5 Boot code archive

228cf78 adds `src/rom/tbblue.zip`: `boot-30100.bin`, `boot-30200-ab.bin`, `boot-30204.bin`, `boot-30204-ab.bin`, the deprecated `boot-30204rc*.bin` ("retained for use by existing emulators"), a README with the licences (2.5) and the GPL3 text.

- jnext: relevant. It is an official, versioned source for the boot code of each core, and `boot-30100.bin` is byte-identical to `roms/nextboot.rom`.
- It is only on `30205`, not on `master`.

### 3.6 Minor

- dd770ec adds `i_spi_rx_di` to a process sensitivity list in both `pi_debug_control*.vhd`. It changes simulation only; synthesis is unaffected.
- 0b3b973: `nextreg.txt` whitespace "for the explorer parser".
- 15e206d, 667f70d: merges with no content of their own.

## 4. What jnext would need

Grouped by goal. Each item is a change to jnext only if the owner adopts that core version.

**A. Report and behave as 3.02.04 (or .05) on issue 2, the emulated board.** The always-built changes:
1. NR 0x0E = 0x04 (0x05 for 3.02.05).
2. Boot ROM `boot-30204.bin` together with the 0x0000-0x1FFF overlay (2.5, 2.6). Open questions:
   - does it need a flash device for its settings?
   - does NextZXOS boot with it under jnext's two SD sockets?

   Its GPL3 licence is cleaner than the current boot code's.
3. NR 0x0A: apply bit 5 only when bit 2 = 1 (2.7).
4. NR 0xA0: keep bit 1 across a soft reset and read it as 0. Nothing observable on issue 2; for the record only.
5. MD pad: the three-button condition, for fidelity only (2.9).
6. Membrane F-key FSM: nothing for issue 2. Do not reproduce the 3.02.04 repeat (2.3); 3.02.05 removed it.

Already matching: the SPI master fix (2.8), the flash/SD line isolation, NR 0x7F. Not applicable: PS/2 keymap, board pins, PLLs, the VGA test pattern, TBU packaging, the AB core.

**B. Emulate issue 4 or 5.** Everything gated by `g_option` or `g_board_issue >= 2`:
- the Pi debug control (prior document, #393; blocked on the QEMU SPI link);
- the NMI+QWERT row in the F-key FSM;
- NR 0x0F board id;
- for 3.02.05, the HDMI v3 NR 0x11 modes and per-mode wall-clock pacing.

The debug-wedge CPU needs nothing: it executes like the plain one. A core-by-board selector is #394.

**C. Not caused by any version, found on the way (owner's call).** jnext's undocumented-flag behaviour for BIT n,(HL), and probably the other cases in 2.4, follows FUSE and real Z80 silicon, not the shipped T80N, which every 3.02.0x build uses.

## 5. Uncertain or not verified

- The behaviour of the new boot code beyond its commit messages (2.5). It was not disassembled.
- The reason for the bank 7 read latch marked "unexplainable" by its author (2.2, issue 4/5 builds only).
- What `zxnext-2-30200.bin` / `zxnext-4-30200.bin` are (2.10).
- The 3.02.03 double SPI transfer under DMA (2.8): my reading of the code, not measured.
- The hotkey repeat in released 3.02.04 (2.3): derived from the code and the 3.02.05 commit message, not tested on hardware.
- Whether the firmware jnext boots writes NR 0x0A bit 5 in config mode (2.7).
- jnext vs T80N flag divergences other than BIT n,(HL) (2.4).
- 3.02.05 is moving. Anything in section 3 can change before release.
