# Known Functionality Gaps and Proposed Implementation Plan

Authored 2026-04-26. Aggregates known-but-deferred functionality gaps
across the JNEXT ZX Spectrum Next emulator. Sourced from 4 parallel
section scans (A: Video & GUI; B: Audio & peripherals; C:
CPU/memory/boot; D: Test infra) consolidated by an independent
reviewer pass.

This document is **descriptive**, not prescriptive — items here are
candidates for future sessions, prioritised by user impact. Each item
links back to its authoritative plan / issue / memory reference where
one exists; items without prior documentation are flagged as such.

## Methodology

- Sources scanned by the 4 section authors: `doc/design/`,
  `doc/testing/`, `doc/issues/`, `.prompts/` (recent 6 sessions),
  project memory index, source greps for TODO/FIXME/PHASE-B/`// G:`,
  `CHANGELOG`, `FEATURES.md`, `TODO.md`, plus 16 subsystem test-plan
  docs.
- Items already shipped per `FEATURES.md` / `ChangeLog` excluded from
  the main list (see appendix).
- Items already covered by passing unit / integration tests excluded.
- Cross-section overlaps merged into single G-IDs; combined sources
  cited.
- WONT items moved to a separate appendix.
- Reviewer ran source / test greps to verify each "X is missing"
  claim; errors found in the section drafts are flagged in the
  Surprises appendix at the bottom.

## Summary table

Effort: H = >2 sessions, M = 1–2 sessions, L (small) = <1 session.
Priority: High / Medium / Low based on user-visibility and blast
radius.

**Display column** (added 2026-04-27 per Task 4): `Y` marks items whose
user-visible effect lands on the rendered emulator output (pixel content,
saved screenshot, on-screen overlay, or display chain like CRT-filter /
scale / framerate). Display-affecting items are sorted to the top of the
table — Task 4 prompt assigns them maximum priority — ranked internally
by `Priority`. Non-display items follow, ranked the same way. The `Cat`
section letters (A/B/C/D) still index the per-section detail below.

**Subsystems column** lists the production subsystem(s) primarily
affected. Names align with the 32 unit-test suite names in the dashboard
where possible.

| ID  | Item                                                       | Subsystems                       | Cat | Display | User-visible impact                                        | Effort | Priority |
|-----|------------------------------------------------------------|----------------------------------|-----|---------|------------------------------------------------------------|--------|----------|
| G01 | LoRes mode (NR 0x15 bit 7) + scroll                        | LoRes, NextREG                   | A   | Y       | parallax.nex broken; LoRes demos broken                    | M      | High     |
| G02 | Per-scanline NR 0x15 (LoRes/sprite/priority) replay        | Compositor, Sprites, NextREG     | A   | Y       | parallax/Beast Copper layer splits flat                    | L      | High     |
| G03 | Per-scanline Layer 2 X/Y scroll (NR 0x16/17/71)            | Layer2, NextREG                  | A   | Y       | L2 parallax effects render flat                            | L      | High     |
| G12 | Nirvana-class memory-write multiplexers (Ram::write)       | Ram, ULA, Compositor             | A   | Y       | 48K demoscene multicolour effects broken                   | H      | High     |
| G17 | Parallax.nex "two-copies" mystery (post-LoRes)             | Sprites, Layer2                  | A   | Y       | parallax fully usable depends on this                      | M      | High     |
| G24 | Main-window settings persistence (size/scale/CRT/speed)    | GUI/MainWindow                   | A   | Y       | every launch resets user preferences                       | L      | High     |
| G46 | NextZXOS boot ladder (firmware-faithful + bypass)          | DivMMC, NMI, Boot, ULA           | B,C | Y       | NextZXOS does not reach BASIC / dot-command shell          | H      | High     |
| G106| Line-interrupt scheduler off-by-one + target=0 wrap        | Emulator, VideoTiming            | A   | Y       | Line interrupts fire one line late; target=0 misfires      | L      | High     |
| G163| Line-int schedule not re-evaluated on mid-frame NR 0x22/0x23/0xC4 | Emulator, VideoTiming            | A   | Y       | parallax.nex chained line-IRQ chain swallowed; 69% black L2 | L     | High     |
| G04 | Per-scanline transparency replay (NR 0x14/4B/4C)           | Compositor, Sprites, Tilemap     | A   | Y       | sky/foreground transparency-key swaps render flat          | L      | Medium   |
| G05 | Per-scanline clip-window replay (NR 0x18-0x1B)             | Compositor, NextREG              | A   | Y       | split-screen / picture-in-picture demos blocked            | M      | Medium   |
| G11 | Per-scanline NR 0x68 other bits (stencil, ULA+, blend)     | Compositor, ULA, NextREG         | A   | Y       | mid-frame ULA+ / blend-mode flips render flat              | L      | Medium   |
| G13 | Per-scanline sprite-attribute multiplexing                 | Sprites, Port                    | A   | Y       | 128+ effective-sprites demos render wrong                  | M      | Medium   |
| G50 | Contention `delay()` runtime wiring (Phase 2)              | Memory/Contention, CPU           | C,D | Y       | cycle-accurate contention wrong on +3 / Pentagon / Next    | L      | Medium   |
| G58 | MMU shadow-screen routing (TASK-MMU-SHADOW-SCREEN)         | MMU, ULA                         | C,D | Y       | 128K shadow-screen double-buffer renders wrong             | L      | Medium   |
| G83 | Profiling/benchmark mode + 400% speed bottleneck           | GUI/Speed, Profiling             | D   | Y       | speed-control >200% observably broken; no perf data        | M      | Medium   |
| G91 | NR 0x44 priority bits 7:6 dropped — L2 promotion dead      | Palette, Layer2, Compositor      | A   | Y       | L2 palette-priority promotion is no-op despite passing tests | L    | Medium   |
| G96 | NR 0x35-0x39 vs 0x75-0x79 increment-semantic divergence    | Sprites, NextREG                 | A   | Y       | NR-mirror sprite stream lands in wrong slots               | M      | Medium   |
| G102| ULAnext (NR 0x42/0x43 b0) palette-encoding runtime path absent | ULA, Palette                 | A   | Y       | ULAnext programs see plain 16-colour output, not 256       | H      | Medium   |
| G103| ULA+ (port 0xBF3B/0xFF3B) palette-encoding runtime path absent | ULA, Palette                 | A   | Y       | ULA+ programs cannot drive their 64-entry palette window   | M      | Medium   |
| ~~G108~~| ~~NR 0x69 bits 6/5:0 + NR 0x22 b2 + NR 0xC4 b0 → port_ff_reg~~ | ~~NextREG, ULA, MMU, Compositor~~    | ~~A,C~~ | ~~Y~~       | ~~Timex-mode set via NR aliases + ULA-int-disable via aliases dropped~~ | ~~L~~ | ~~Medium~~ |
| G117| Copper executes per Z80 instr, not per 28 MHz cycle        | Copper, CPU                      | A   | Y       | Dense Copper bursts under-run; possible parallax.nex factor | M     | Medium   |
| G141| FUSE in-opcode contention macros inert (zero-filled)       | Contention, CPU                  | C   | Y       | M1 fetch + no-MREQ contention dropped; 48K demos off ~7T per opcode | M | Medium |
| G142| NR 0x07 cpu_speed deferred bus-idle commit not modelled    | Contention, CPU                  | C   | Y       | Turbo flip applies immediately; should defer to bus-idle   | M      | Medium   |
| G06 | Per-scanline NR 0x6B / NR 0x70 (TM/L2 mode)                | Tilemap, Layer2, NextREG         | A   | Y       | mixed-resolution / rolling-mode demos blocked              | M      | Low      |
| G07 | Per-scanline port 0xFF Timex screen mode                   | ULA, Timex                       | A   | Y       | Timex mid-frame split demos render flat                    | M      | Low      |
| G08 | Per-scanline NR 0x26 / NR 0x27 ULA scroll                  | ULA, NextREG                     | A   | Y       | non-square-tile scroll demos                               | L      | Low      |
| G09 | Per-scanline NR 0x12 / NR 0x13 L2 active bank              | Layer2, NextREG                  | A   | Y       | exotic per-line page-flip demos blocked                    | L      | Low      |
| G10 | Per-scanline active-palette select (NR 0x43/6B)            | Palette, NextREG                 | A   | Y       | latent palette-bank-flip demos                             | L      | Low      |
| G14 | Layer 2 enable/write-paging per-scanline (port 0x123B)     | Layer2, Port                     | A   | Y       | latent L2-on-stripe effects                                | L      | Low      |
| G15 | Sprite-pattern reload mid-frame (port 0x5B)                | Sprites, Port                    | A   | Y       | niche >64-pattern animation                                | M      | Low      |
| G16 | Beast.nex residual: NEX-loader bank-5 collision            | NEX loader, ULA                  | A   | Y       | cosmetic only                                              | L      | Low      |
| G18 | Screenshot vertical scaling for 80x32 / 640x256 modes      | Screenshot, GUI                  | A   | Y       | squished screenshots in 640-mode                           | L      | Low      |
| G21 | Raster / ULA-read indicator overlay                        | GUI, Debugger                    | A   | Y       | developer diagnostic overlay                               | L      | Low      |
| G26 | Compositor open questions (NR 0x68 mode 01 / L2 promote)   | Compositor                       | A   | Y       | latent edge in modes 110/111                               | L      | Low      |
| G27 | Compositor `rgb_blank_n_6` pipeline edge test              | Compositor                       | A   | Y       | cosmetic edge; coverage gap                                | L      | Low      |
| G51 | Contention NextREG dispatch + NR 0x07/0x08 hc(8) (Phase 3) | Contention, NextREG              | C   | Y       | turbo-mode contention edges                                | M      | Low      |
| G52 | Contention Phase-4 screenshot rebaseline                   | Contention, Test/Screenshot      | C   | Y       | noisy 48K/128K/+3 screenshot rebaseline pass               | L      | Low      |
| G65 | CPU/Copper cycle-accurate NR-write priority                | CPU, Copper, NextREG             | C   | Y       | ARB-* tests order stimulus manually; latent                | H      | Low      |
| G77 | Reopened-suite skips: Compositor NR 0x68 + MMU shadow      | Compositor, MMU                  | D   | Y       | plan-doc backlog; G58 is the runtime side                  | M      | Low      |
| G92 | port 0x123B cpu_do(4)=1 offset mode ignored                | MMU, Layer2, Port                | A   | Y       | L2 offset-mode workflow corrupts enable state              | L      | Low      |
| G93 | Compositor L2 priority not pixel-doubled in 640 mode       | Compositor, Layer2               | A   | Y       | Latent: right-pixel artefacts under L2 native 640 + priority | L    | Low      |
| G94 | LoRes radastan (NR 0x6A) absent — distinct from G01        | LoRes, NextREG                   | A   | Y       | radastan-mode programs do not render                       | M      | Low      |
| G95 | NR 0x09 bit 4 sprite_tie not wired                         | Sprites, NextREG                 | A,D | Y       | Mirror+attr index drift; mis-targeted sprite writes        | L      | Low      |
| G98 | Tilemap text-mode RGB transparency check missing           | Tilemap, Compositor              | A   | Y       | Text-mode tilemap pixels with NR 0x14 RGB render opaque    | L      | Low      |
| G100| Tilemap per-line scroll snapshot caps at line 320          | Tilemap, NextREG                 | A   | Y       | Vblank-DMA-streamed scroll writes silently dropped         | L      | Low      |
| G101| Tilemap pixel_textmode_o flag not exposed to compositor    | Tilemap, Renderer                | A   | Y       | Precondition for G98; precludes per-pixel text-mode        | L      | Low      |
| G104| HI_RES (Timex 512×192) renders at 256 px (half-resolution) | ULA, Renderer                    | A   | Y       | Timex hi-res text/programs render at half horizontal res   | M      | Low      |
| G105| HI_RES 6-bit border palette-group encoding not modelled    | ULA, Palette                     | A   | Y       | HI_RES border colour wrong under ULAnext/ULA+ palettes     | L      | Low      |
| G164| set_screen_mode decoded mode from port_ff(5:3) not (2:0)   | ULA, NextREG                     | A   | Y       | Real Timex programs writing port_ff(2:0) couldn't reach HI_RES | L  | Low      |
| G165| HI_RES display ink/paper used independent bits not border_clr_tmx | ULA, Palette              | A   | Y       | Timex hi-res monochrome rendered with wrong colours + no BRIGHT | L | Low      |
| G166| Renderer::ula_border_[] per-pixel border-active never written | ULA, Renderer, Compositor   | A   | Y       | Latent: NR 0x68 stencil + ULA-disabled border_exc paths read false | L | Low |
| ~~G167~~| ~~ULAnext / ULA+ encoder dispatch absent in HI_RES display path~~ | ~~ULA, Palette~~                | ~~A~~   | ~~Y~~       | ~~HI_RES under ULAnext/ULA+ palette renders wrong inner colours~~ | ~~L~~   | ~~Low~~      |
| G109| NR 0x64 cu_offset not applied to line-int compare          | Emulator, Copper, VideoTiming    | A   | Y       | NR 0x64 + line-IRQ raster split misaligned                 | L      | Low      |
| G132| F-key state machine + 50-60/cpu-speed/scandouble hotkeys absent | Keyboard, GUI, NextREG, VideoTiming | A,B | Y  | Host F1/F2/F3/F4/F7/F8 keys do nothing emulator-side       | M      | Low      |
| G144| port 0x123B map_shadow bit 3 — read/write-over wrong bank  | MMU, Layer2                      | A   | Y       | L2 double-buffering via 0x123B b3 writes wrong bank        | L      | Low      |
| G146| port_fd_conflict_wr — Soundrive Mode 2 vs paging-port write conflict | MMU, Audio             | A   | Y       | NR 0x84 b2 SD2 + low-byte 0xF1/0xF9 write mis-handled vs paging | L | Low      |
| G147| F8/F3/F5/F6 host hotkeys to NR 0x07 / 50-60 / expbus unwired | Input, NextREG, Contention, Clock, Video | A | Y  | Users cannot cycle CPU speed / 50-60 / scandouble via F-keys | M    | Low      |
| G33 | Tape SAVE (write to TAP/TZX/WAV)                           | Tape                             | B   |         | Phase 1 SAVE→TAP shipped (Task 57); MIC→TZX + WAV writer absent | M | Medium   |
| G34 | `.z80` snapshot loader                                     | Snapshot                         | B   |         | most-popular legacy snapshot format unsupported            | M      | High     |
| G35 | Snapshot save (.sna out / .szx out / .nex out) wired       | Snapshot, GUI                    | B   |         | cannot save mid-game state to file                         | M      | High     |
| G36 | TZX Direct-Recording (DeciLoad 0x15)                       | Tape                             | B   | Y       | fixed Task 57: monotonic tape clock + ZOT pause edge       | M      | High     |
| G42 | Joystick / gamepad host wiring (Kempston/Sinclair/MD)      | Joystick, SDL, GUI               | B   |         | gamepad / USB joystick unusable; keyboard-only             | M      | High     |
| G66 | Save-state schema versioning + per-subsystem framing       | Save-state                       | C,D |         | ANY save_state field reorder corrupts older snapshots      | M      | High     |
| G126| NR 0x05 mode change does not propagate to MembraneStick    | Joystick, MembraneStick, NextREG | B   |         | Joy mode switch leaves membrane fold pinned to defaults    | L      | High     |
| G32 | DAC continuous-buzz playback artefact                      | DAC, Audio                       | B   |         | audible quality degradation on DAC software                | H      | Medium   |
| G37 | WAV DeciLoad real-time loading                             | Tape, WAV                        | B   | Y       | fixed Task 57: monotonic clock + sub-sample interpolation  | M      | Medium   |
| G38 | DSK / +3 disk image loading + uPD765 FDC                   | FDC, Disk                        | B   |         | all +3 disk software unrunnable                            | H      | Medium   |
| G43 | Kempston Mouse host wiring                                 | Mouse, SDL                       | B   |         | Art Studio Next, mouse demos unusable                      | M      | Medium   |
| G47 | NextZXOS post-boot regression / dot-command surface        | Test, Boot                       | B   |         | no automation for NextZXOS-native software regressions     | L      | Medium   |
| G48 | Multiface peripheral (Task 8) + RETN-alias band-aid        | Multiface, NMI, DivMMC           | B,C |         | no NMI freeze/cheat menu; 8 DivMMC + Copper rows skipped   | M      | Medium   |
| G56 | NextReg `regs_[]` shadow-store systemic bug (option a partial — option b pending) | NextREG                          | C,D |         | per-NR read_handlers landed for ~24 NRs; `NextReg::write` contract unfixed | M | Medium |
| G59 | NextZXOS bypass-tbblue-fw boot path                        | Boot, NextREG, MMU               | C   |         | pragmatic instant-boot mitigation for G46                  | H      | Medium   |
| G67 | Rewind buffer pre-allocated bound + assertion              | Rewind                           | C   |         | fixed Task 60b: bounds+sentinels; mismatched slots dropped | L      | Medium   |
| G69 | Traceability matrix structurally stale + extractor         | Test/Matrix                      | D   |         | audit / theatre-detection get wrong numbers                | M      | Medium   |
| G74 | No CI pipeline; regression depends on dev discipline       | CI, Test                         | D   |         | visual regressions can slip past PR review                 | M      | Medium   |
| G75 | Regression tolerance hard-zero; perceptual diff missing    | Test/Regression                  | D   |         | spurious diff failures; no incremental change signal       | M      | Medium   |
| G80 | Headless-mode host-time leakage audit                      | Test, Determinism                | D   |         | regression flake risk; D14/G76 dependency                  | M      | Medium   |
| G87 | IM2 RETI/RETN decoder cannot see ED 2nd byte               | CPU, IM2                         | C   |         | IM2 daisy-chain locks up after first ISR (latent)          | M      | Medium   |
| G89 | Z80N block-repeat ops non-interruptible                    | CPU, Z80N                        | C   |         | Long LDIRX/LDDRX miss frame INT; music/scheduler skew      | L      | Medium   |
| G107| ULA-int scheduler ignores per-machine c_int_h/c_int_v      | Emulator, VideoTiming            | A   |         | Pentagon/+3 frame INT off-position; raster demos diverge   | L      | Medium   |
| G110| audio_mixer.exc_i speaker-exclusive gate not enforced      | Mixer, NextREG, Beeper           | B   |         | EAR/MIC contribute to host audio in speaker-only mode      | L      | Medium   |
| G123| NR 0x0A bit 4 (divmmc_automap_en) not wired                | DivMMC, NextREG                  | C   |         | NR 0x0A toggle of automap silently dropped                 | L      | Medium   |
| G124| NR 0x83 b0 not propagated to DivMmc::set_port_io_enable    | DivMMC, NextREG, MMU             | C   |         | NR 0x83 bit-0 clear leaves DivMMC ROM/automap mapped       | L      | Medium   |
| G127| NR 0x05 User-Defined + NR 0x28-0x2B joymap absent          | NextREG, Joystick, MembraneStick | B   |         | Custom joystick→key remap silently has no effect           | M      | Medium   |
| G128| port 0x37 missing NR 0x82 bit-7 io_en gate                 | Joystick, NextREG, Port          | B   |         | NR 0x82 b7 clear still returns joystick byte on port 0x37  | L      | Medium   |
| G129| port_1f_hw_en / port_37_hw_en mode-conditional decode missing | Joystick, Port, FloatingBus  | B   |         | "Is Kempston attached?" probes get wrong answer            | L      | Medium   |
| G151| Z80N NEXTREG opcode mutates nr_register — VHDL preserves   | CPU/Z80N, NextREG, Port-Dispatch | C,D |         | Persistent 0x243B select clobbered by inline Z80N pokes    | L      | Medium   |
| G152| Host F1/F4/F9/F10 hotkeys not wired to NMI source / reset  | GUI, NMI, NextREG, Reset         | C   |         | User cannot trigger NMI/reset from keyboard (GUI menu only) | L     | Medium   |
| G153| NR 0x02 reset_type[2:0] FSM and read-back missing          | NextREG, NMI, Boot, DivMMC       | C   |         | reset_type-conditional firmware paths take wrong branch    | L      | Medium   |
| G154| NR 0x80-0x89 expbus / port-enable readbacks partial        | NextREG, Boot, Port-enable       | C   |         | Firmware reading NR 0x82-0x89 gets raw shadow byte         | M      | Medium   |
| G19 | Save screenshot in `.SCR` format                           | Screenshot, GUI                  | A   |         | developer workflow gap                                     | L      | Low      |
| G20 | Auto-named screenshots (no dialog)                         | Screenshot, GUI                  | A   |         | workflow friction                                          | L      | Low      |
| G22 | ASM-only clipboard copy in disassembly panel               | Debugger                         | A   |         | dev workflow                                               | L      | Low      |
| G23 | Redefinable / preset debugger keybindings                  | Debugger                         | A   |         | usability for users from other emulators                   | M      | Low      |
| G25 | Debugger window stickiness to main window                  | GUI/Debugger                     | A   |         | debugger floats freely on main-window drag                 | L      | Low      |
| G28 | Layer 2 G9-06 column-pipeline observable                   | Layer2 (test only)               | A   |         | test coverage only                                         | L      | Low      |
| G29 | Pi I2S real audio emulation upgrade                        | I2S, Audio                       | B   |         | I2S contribution silent (no published Z80 software uses)   | H      | Low      |
| G30 | AY GPIO ports (PORTA / PORTB)                              | AY, Audio                        | B   |         | vintage AY-GPIO software (keymux/lightgun/MIDI) silent     | M      | Low      |
| G31 | DAC per-clock write-priority model (SD-09)                 | DAC, Audio                       | B   |         | edge: Specdrum + Covox at high rates slightly off          | M      | Low      |
| G39 | ESP-01 / Wi-Fi UART bridge                                 | UART, ESP                        | B   |         | NextZXOS networking and multiplayer Z80 software silent    | H      | Low      |
| G40 | SD card command coverage (CMD9/10/13/16/23/25 etc.)        | SD card                          | B   |         | CSD/CID probes silent; multi-block writers fall back       | M      | Low      |
| G41 | MMC card support (vs SDHC only)                            | SD/MMC                           | B   |         | raw-MMC software (rare) won't init                         | L      | Low      |
| G44 | Keyboard issue-2 EAR/MIC composition                       | Keyboard                         | B   |         | issue-2 16K tape-loading detection edge                    | L      | Low      |
| G45 | Expansion bus / cartridge framework (FE-05 / ROMCS)        | Expansion                        | B   |         | Interface 1/2, Multiface (ext), Currah µSpeech absent      | H      | Low      |
| G49 | NR 0xC0 stackless-NMI execution (CTC NR-C0-02)             | CPU, IM2                         | C   |         | NMI-PUSH suppression edge — minimal real-world impact      | H      | Low      |
| G53 | FUSE-table retirement decision                             | Contention, CPU                  | C   |         | two contention paths post-Phase-2; divergence risk         | L      | Low      |
| G54 | Contention port_7ffd_active term (CT-IO-05/06)             | Contention, Port                 | C   |         | 128K/+3 port-contention edge                               | L      | Low      |
| G55 | NR 0xD8 IO-trap (FDC NMI source) — stub                    | NMI Source                       | C   |         | +3 floppy-trap NMI edge (rare)                             | L      | Low      |
| G57 | MMU `current_rom_bank()` — three documented gaps           | MMU                              | C   |         | 48K-DivMMC edge; altrom mask; port_1ffd-bit-2 gating       | L      | Low      |
| G60 | config.ini / menu.ini / menu.def parsing                   | Boot, Config                     | C   |         | NextZXOS user-config UX once bypass mode lands             | M      | Low      |
| G61 | Z80N undocumented RETN-alias coverage edge                 | Z80N (test)                      | C   |         | test gap protecting C01 band-aid removal                   | L      | Low      |
| G62 | NR 0x03 soft-reset config_mode preservation question       | NextREG                          | C   |         | edge between reset and first NR 0x03 write                 | L      | Low      |
| G63 | NR 0x03 machine-type latch read-back                       | NextREG                          | C   |         | subset of G56 specifically for NR 0x03                     | L      | Low      |
| G64 | NR 0x06/keymap & altROM 0x06/0x07 layout (bypass deps)     | Boot, NextREG                    | C   |         | open VHDL questions blocking G59                           | L      | Low      |
| G68 | Rewind sub-frame granularity                               | Rewind                           | C   |         | step Back stops at frame boundaries only (WONT-leaning)    | H      | Low      |
| G70 | Requirements DB (SQLite proposal)                          | Test infra                       | D   |         | plan/matrix/dashboard drift remains grep-gymnastics        | M      | Low      |
| G71 | `VideoTiming` pulse-counter surface is test-only           | VideoTiming (test surface)       | D   |         | two state stores for one VHDL signal; blocks 3 test rows   | M      | Low      |
| G72 | UART pin-7 / IoMode UART-mode injectors not fed            | UART, Input                      | D   |         | pin-7 multiplex unit-correct but not driven at runtime     | L      | Low      |
| G73 | Audio I2S has zero runtime wiring                          | I2S, Audio                       | D   |         | I2S-source NextREGs silent (no consumer in production)     | L      | Low      |
| G76 | RZX determinism long-form regression                       | RZX, Test                        | D   |         | long captures may desync from hidden host-time leaks       | M      | Low      |
| G78 | Agent worktree-stale-base helper (harness)                 | Dev tooling                      | D   |         | parallel-wave merge-overhead; not user-facing              | L      | Low      |
| G79 | Test-output uniformity lint                                | Test infra                       | D   |         | new suite with wrong summary string drops out of dashboard | L      | Low      |
| G81 | DEVELOPMENT-SESSIONS doc currency                          | Docs                             | D   |         | effort accounting under-reports                            | L      | Low      |
| G82 | Z80N matrix Summary row cosmetic mismatch                  | Test/Matrix                      | D   |         | matrix says "0 in-test, 30 missing"; reality 85/85         | L      | Low      |
| G84 | Integration-test design doc missing                        | Test docs                        | D   |         | each integration suite reinvents fixture conventions       | M      | Low      |
| G85 | Lint baseline tautology coverage stops at substring        | Test lint                        | D   |         | reviewer attention catches what lint doesn't               | M      | Low      |
| G86 | FEATURES.md "Accurate memory contention" overclaim         | Docs                             | D   |         | user expectation vs reality — narrative gap                | L      | Low      |
| G88 | NMI does not capture PC into NR 0xC2/0xC3                  | CPU, NextREG, NMI                | C   |         | NMI inspector tooling reads 0xFF instead of last NMI PC    | L      | Low      |
| G90 | 28 MHz turbo SRAM-read wait state not modelled             | CPU, Memory, NextREG             | C   |         | Turbo-mode timing 7% fast on read-heavy code               | M      | Low      |
| G97 | NR 0x19 / 0x1A read handlers absent                        | Sprites, NextREG, ULA            | A,D |         | NR 0x19/0x1A reads return last-write byte, not indexed clip | L     | Low      |
| ~~G99~~ | ~~NR 0x6E/0x6F bit 6 reserved-bit mask missing on read~~       | ~~Tilemap, NextREG~~                 | ~~A,D~~ |         | ~~Software validating writes by read-back sees bit 6 toggle~~  | ~~L~~      | ~~Low~~      |
| G111| DAC channels not held at 0x80 when nr_08_dac_en=0          | DAC, NextREG, Mixer              | B   |         | DAC-disable leaves residual level instead of silencing     | L      | Low      |
| G112| NR 0x2C/0x2D/0x2E read-back exposes Pi I2S input           | NextREG, I2S, Audio              | B   |         | NR 0x2C/2E reads return regs_[] noise, not I2S samples     | L      | Low      |
| G113| NR 0xA2 Pi I2S control register completely unwired         | NextREG, I2S, Audio              | B   |         | mute/dir/channel-enable bits don't gate I2S Mixer path     | L      | Low      |
| G114| NR 0x84 DAC-port-pair enables (5 of 7 bits) not enforced   | DAC, NextREG, Port               | B   |         | DAC writes hit even when NR 0x84 masked them               | L      | Low      |
| G115| TurboSound::reset() over-clears NR 0x08/0x09 state on AY reset | TurboSound, NextREG          | B   |         | psg_mode=11 toggle loses turbosound_en/stereo/mono         | L      | Low      |
| G116| NR 0x61/0x62 Copper read handlers absent                   | NextREG, Copper                  | A,D |         | Copper write-pointer read-back returns last-write, not autoinc | L  | Low      |
| G118| Copper instruction RAM cleared on soft reset (VHDL preserves) | Copper                        | A   |         | Soft reset wipes Copper program; menu re-runs lose payload | L      | Low      |
| G119| CTC on_interrupt gated at peripheral, not fabric edge      | CTC, IM2                         | C   |         | int_en toggled between ZC/TO drops the prior pulse         | L      | Low      |
| G120| CTC prescaler cleared on running TC reload (vs VHDL preserve) | CTC                           | C   |         | Mid-stream TC reload restarts prescaler from 0             | L      | Low      |
| G121| Pulse-mode 32/36-cycle gate not updated on NR 0x03 timing change | IM2, NextREG               | C   |         | Pulse-INT width wrong if NR 0x03 timing flipped post-boot  | L      | Low      |
| G122| DMA 14 MHz dma_d_p_s rising-edge read latch unmodelled     | DMA                              | C   |         | Edge-of-burst sequencing differs from VHDL at turbo        | M      | Low      |
| G125| NR 0x06 bits 7/5 (hotkey enables) not stored / acted on    | NextREG, Hotkey                  | C   |         | F3/F8 NR-side hotkey gates inert; wrong reset defaults     | L      | Low      |
| G130| Kempston-mouse port_1f alias (Soundrive DAC override) missing | Joystick, Mouse, Port         | B   |         | Pentagon Soundrive 1.05 reads of 0xDF return 0x00          | L      | Low      |
| G131| NR 0x0A bits 7:6/bit 5 not gated on nr_03_config_mode      | NextREG, SPI/SD, Mouse           | C   |         | Stray bit-5 outside config_mode flips SD-card mapping      | L      | Low      |
| G133| Keyboard tick_scan + cancel_extended_entries not driven from production | Keyboard           | B   |         | 1-scan shift hysteresis + extended-key cancel not running  | L      | Low      |
| G134| UART RX request-mask asymmetry (near_full vs avail) not modelled | UART, IM2                | B   |         | NR 0xC6=0x20 (near-full only) sees spurious per-byte INTs  | L      | Low      |
| G135| NR 0xA0 Pi peripheral enable bits not honoured             | UART, NextREG, GPIO              | B   |         | UART1/Pi bridge always on; bit-routing never gates         | L      | Low      |
| G136| SPI Flash CS (cpu_do=0x7F) ignored — config-mode-gated select absent | SPI, NextREG          | B   |         | Firmware reading core-loader flash gets all 0xFF           | M      | Low      |
| G137| SPI master o_spi_wait_n (DMA wait) not surfaced            | SPI, DMA, Contention             | B   |         | DMA-via-SPI loaders complete in 0 cycles, not 16           | L      | Low      |
| G138| NR 0xA0 bit 3 — Pi I2C-1 routing onto GPIO 2/3 unmodelled  | I2C, NextREG, GPIO               | B   |         | I2C1 wired-AND active even when bit 3 clear                | L      | Low      |
| G139| I2C 24LCxx EEPROM device unmodelled — only DS1307 attached | I2C                              | B   |         | tbblue config EEPROM reads/writes NACK                     | L      | Low      |
| G140| Boot ROM overlay: 8 KB → 16 KB mirror at 0x0000-0x3FFF     | MMU, Boot                        | C   |         | Boot-ROM reads at 0x2000-0x3FFF fall through wrongly       | L      | Low      |
| G143| port 0xEFF7 missing NR 0x85 b2 (port_eff7_io_en) gate      | MMU, Port                        | C   |         | NR 0x85 b2 clear still lets EFF7 paging-mode flips land    | L      | Low      |
| G145| port 0x123B read-back surface absent (returns 0xFF)        | MMU, Layer2, Port                | C,D |         | Software probing L2 control regs reads 0xFF                | L      | Low      |
| G148| port_dffd_reg_6 not stored — Multiface readback truncated  | MMU, Multiface                   | C   |         | DFFD bit 6 reads back 0; affects Multiface state inspection | L     | Low      |
| G149| NR write-only registers leak last-written byte on read     | NextREG                          | D   |         | Reads of write-only NRs return last write, not 0x00        | L      | Low      |
| G150| NR 0xFF write commits ULA/TM palette entry at bf3b-indexed slot | NextREG, Palette            | A   |         | Confirmed observable side-channel for ULA+ legacy palette poke | L  | Low      |
| G155| NEX loader doesn't honour ram_required field               | NEX loader, RAM                  | C   |         | NEX needing >installed RAM corrupts banks silently         | L      | Low      |
| G156| NEX loader ignores loading_bar/delay/start_delay/colour    | NEX loader, GUI                  | A   |         | Per-bank loading bar / inter-bank delays unrendered        | M      | Low      |
| G157| Boot ROM overlay size mismatch silently truncates          | Boot, MMU                        | C   |         | Wrong-sized boot ROM blob silently miscompiles             | L      | Low      |
| G158| SD card image hot-plug / unmount not exposed at runtime    | SD, GUI                          | C   |         | User cannot swap SD images mid-session                     | L      | Low      |
| G159| SD card CRC validation absent (CMD0 0x95 hard-coded path)  | SD                               | D   |         | Future CRC-checking tooling silently passes                | L      | Low      |
| G160| SD CMD13 (SEND_STATUS) returns generic R1 fall-through, not R2 | SD                           | D   |         | Hosts probing card status hang on missing 2nd byte         | L      | Low      |
| G161| RTC 12h-mode hours register snapshot overwrites bit 6 / AM-PM | RTC                           | B   |         | host snapshot silently flips RTC back to 24h mode          | L      | Low      |
| G162| NMI iotrap strobe consumed but never propagated to MF assert | NMI Source, NextREG, Port, FDC | C   |         | port_2FFD/3FFD trap path silently silent (FDC NMI dead)    | L      | Low      |
| G168| port_7ffd_reg vs port_7ffd_dat half-cycle phase            | MMU, NextREG, CPU                | C   |         | Sub-instruction phase divergence; invisible per-instr      | M      | Low      |
| G169| Generic VHDL `*_q` registered signals — half-cycle phase   | MMU, CPU, NextREG                | C   |         | Same family as G168; multi-site                            | M      | Low      |
| G170| DivMMC automap `*_q` falling-edge sub-cycle pipeline       | DivMMC, MMU, CPU                 | C   |         | Overlay drop ~3 i_CLK_28 later vs 1-clk in VHDL            | M      | Low      |
| G171| VHDL-impossible same-cycle Z80 OUT-OUT to DivMMC port pair | DivMMC, Port, CPU                | C   |         | Theoretical edge: back-to-back OUT clobber mid-handler     | M      | Low      |
| G172| SDHC vs SDSC dual-mode address translation (HCS gate)       | DivMMC, SD                       | C   |         | CMD17/18/24 always block-addressed; SDSC byte-mode absent  | H      | Low      |
| G173| DD/FD-prefixed Z80N opcode dispatch via XY_State           | CPU, Z80N                        | C   |         | `DD ED <Z80N>` routes via Alternate (EXX) not XY_State     | M      | Low      |

168 entries. Display-affecting rows: 53 (top of table). Non-display: 115.

---

## A. End-user emulation experience — Video & GUI

### G01. LoRes mode (NR 0x15 bit 7) + scroll registers + clip
- **What**: VHDL `lores.vhd` defines a 128×96 chunky 256-colour layer
  fed from physical bank 5+0x0000 (12 288 bytes), gated by NR 0x15
  bit 7. NEX-loader handles `SCREEN_LORES = 0x04`
  (`src/core/nex_loader.cpp:189-198`); renderer half deferred at
  `src/core/emulator.cpp:456`. NR 0x32 (X scroll) and NR 0x33 (Y
  scroll) have no `set_write_handler` (verified by grep). NR 0x1A
  clip-window already wired.
- **User impact**: parallax.nex broken; any LoRes demo blocked.
- **Source ref**: `doc/issues/PARALLAX-NEX-INVESTIGATION.md`;
  `doc/issues/BEAST-NEX-INVESTIGATION.md` "Parallax — separate
  finding"; `EMULATOR-DESIGN-PLAN.md:767`.
- **Test coverage today**: zero passing rows reference `lores`.
- **Dependencies**: foundational. NR 0x14 transparency + NR 0x68
  bit 3 ulap_en already wired.
- **Proposed**: author `doc/design/TASK-LORES-PLAN.md` — `Lores`
  class modelled on Layer2; wire NR 0x15 bit 7, NR 0x32/0x33,
  NR 0x1A; bare + integration tests + critic.
- **Effort**: M.

### G02. Per-scanline NR 0x15 (LoRes/sprite/priority) replay
- **What**: Mid-frame Copper-driven NR 0x15 toggles (LoRes-on /
  sprites-off split) collapse to last-value-wins.
- **User impact**: parallax / sprite-LoRes / Beast-style Copper
  layer-flip demos render incorrectly.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A row 3.
- **Coverage today**: none — frame-end render only.
- **Dependencies**: G01 (otherwise no observable effect).
- **Proposed**: log-pattern clone of `TASK-PER-SCANLINE-PALETTE-PLAN`.
- **Effort**: L.

### G03. Per-scanline Layer 2 X/Y scroll replay (NR 0x16 / 0x17 / 0x71)
- **What**: Renderer reads scroll regs once per frame; Beast.nex
  writes NR 0x16 1818 times/5 s — most per-frame, but per-line is
  the parallax driver.
- **User impact**: any L2 parallax effect renders flat.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A row 1;
  `src/core/emulator.cpp:398-413`.
- **Coverage today**: per-frame in `layer2_test`; per-line untested.
- **Dependencies**: independent of G01.
- **Proposed**: log-pattern clone.
- **Effort**: L.

### G04. Per-scanline transparency replay (NR 0x14 / 0x4B / 0x4C)
- **What**: Mid-frame transparency-key swap (sky vs foreground)
  collapses to last-write.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A
  rows 4–5; `TASK-PER-SCANLINE-PALETTE-PLAN.md` "Out of scope".
- **Coverage today**: per-frame only (compositor BL group).
- **Dependencies**: independent; demo-driven.
- **Effort**: L (3 sub-items).

### G05. Per-scanline clip-window replay (NR 0x18-0x1B)
- **What**: Rotating-index 4-write clip-window registers don't
  replay per scanline — non-trivial because of rotating state.
- **User impact**: split-screen / picture-in-picture demos blocked.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A row 6.
- **Coverage today**: per-frame only.
- **Dependencies**: independent.
- **Proposed**: snapshot all 4 X1/X2/Y1/Y2 per layer per line; care
  for rotating-register state.
- **Effort**: M.

### G06. Per-scanline NR 0x6B tilemap control + NR 0x70 L2 mode
- **What**: Mid-frame mode flip changes width path (40/80, 256/320/
  640) — reroutes renderer dispatch.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A
  rows 7–8.
- **Effort**: M.

### G07. Per-scanline port 0xFF Timex screen mode replay
- **What**: Mid-frame Timex STANDARD/HI_COLOUR/HI_RES switch
  reroutes ULA path.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A.
- **Effort**: M.

### G08. Per-scanline ULA hardware scroll NR 0x26 / NR 0x27
- **What**: Functionality landed during ULA closure 2026-04-23 but
  per-scanline replay was not in scope.
- **Coverage today**: per-frame in `ula_test` S5.x.
- **Effort**: L.

### G09. Per-scanline NR 0x12 / NR 0x13 L2 active bank
- **What**: Per-line page-flipping for double-buffered scroll
  (exotic).
- **Effort**: L.

### G10. Per-scanline active-palette select (NR 0x43 b1-3 + NR 0x6B b4)
- **What**: Mid-frame palette-bank flip independent of palette
  CONTENT writes; out-of-scope of landed PALETTE plan.
- **Effort**: L.

### G11. Per-scanline NR 0x68 bits other than bit 7
- **What**: Bit 7 (ULA enable) per-scanline landed in UDIS-01/02.
  Bit 0 (stencil), bits 6:5 (blend mode UDIS-03), bit 3 (ULA+ gate)
  landed flat-frame only.
- **User impact**: any Copper-driven mid-frame blend-mode toggle
  renders flat.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat A;
  `TASK-COMPOSITOR-ULA-BLEND-MODE-PLAN.md` (UDIS-03 closed).
- **Effort**: L.

### G12. Nirvana-class memory-write multiplexers (`Ram::write` hook) [closed]
- **Status: CLOSED (Task 54 merge, 2026-07-14).** The remaining
  `bifrost.tap` finding was solved by Task 54 (contention stretch table
  had the wrong PERIOD + `AttributeMux::hc_fetch()` latch instants were
  early and parity-blind): BIFROST is pixel-identical to real FUSE and
  Nirvana renders correctly. Locked by the bifrost/nirvana/nirvanap
  regression rows on 48K/128K/+3 (Tasks 55 + 59b). The Next-mode
  column-18 nit was adjudicated hardware-consistent (Task 59,
  G12-NIRVANA-STATUS-2026-07-13.md §10). Full history below.
- **What**: Renderer reads ULA pixel/attribute bytes from physical
  bank 5/7 at frame-end. **Nirvana**, **BIFROST*2**, **multicolour**
  demos rewrite the same attribute byte multiple times per frame
  timed to the beam — frame-end render sees only the last value.
- **User impact**: **major** for the Spectrum demo scene. Whole
  class of classic per-scanline attribute-multiplexed demos render
  wrong.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat B.
- **Mechanism landed (task8-nirvana branch)**: a per-scanline
  change-log + replay class, `AttributeMux` (`src/memory/
  attribute_mux.{h,cpp}`), mirroring the established pattern already
  used by `PaletteManager`/`Layer2`/`Sprites`/`Ula` scroll+palsel.
  `Mmu::write()` has a dedicated, always-on hot-path detector
  (two array/member loads + up to 3 integer compares on every
  plain-RAM-slot write) that keys on `slots_[slot]` == the bank-5/
  bank-7 attribute page and the 768-byte attribute sub-range; the
  expensive part (appending to the per-frame log) only runs once the
  mux is armed. `Ula::attr_vram_read()` (`src/video/ula.cpp`) consumes
  it: when armed, attribute reads go through
  `mmu.attr_mux5()/attr_mux7().current(offset)` (the per-scanline
  reconstruction) instead of a direct RAM read.
  - **A generic `Ram::set_write_observer()` approach (Phase A) was
    tried first and then REMOVED.** It stashed CPU-side beam position
    (`Mmu::set_write_beam_pos`, wired from `z80_cpu.cpp`) and fired a
    `std::function` callback on every `Ram::write()`. It was never
    actually consumed by the shipping renderer (Phase B built its own
    independent detector directly in `Mmu::write()` instead, since the
    generic observer didn't cover the dedicated `bank5_vram_`/
    `bank7_bram_` dual-port buffers Next machines use and would have
    cost a `std::function` indirection on every RAM write once
    anything registered on it). Two `mmu_test` rows (G12-MUX-01/02)
    tested this unused plumbing — coverage theatre, since nothing in
    production called it. Removed entirely (`Ram::set_write_observer`/
    `clear_write_observer`/`has_write_observer`/`notify_write`, the
    `Mmu::set_write_beam_pos` CPU-side stash) and G12-MUX-01/02
    rewritten to test the mechanism that actually ships.
- **Arm-condition history — two heuristics tried, both had real bugs**:
  1. **Total attribute-range writes/frame ≥ 1536**: false-armed on
     `beast.nex`, which legitimately writes ~1500 *different*
     attribute cells per frame as ordinary content (later found to be
     an inaccurate characterisation — see below — but the corruption
     was real: 25000+/190000+ pixel diff against the pinned
     `beast-demo`/`layers-beast-ula` references).
  2. **Per-byte repeat count ≥ 4 within one frame** (the version an
     independent reviewer APPROVE-WITH-NITS'd): fixed the beast.nex
     false-arm, but had a genuine **false-negative** — a cell racing
     only 2 colour bands (repeat count 2) never reaches 4 and renders
     flat and silently wrong, indistinguishable from "working as
     designed" except the picture is wrong. Replaced by:
  3. **Positional gate (current, `Mmu::attr_mux_write_still_relevant_()`)
     — zero false negatives by construction**: arms on the FIRST write
     to an attribute byte that lands while the beam is inside the
     active display AND at or before the end of that byte's own
     8-scanline character-row span this frame — i.e. any write that
     could still change what this frame renders for that cell. No
     repeat count. Mutation-tested: the OLD repeat≥4 heuristic was
     reproduced exactly (temporary scratch mutation) and confirmed to
     fail the 2-band case (`mmu_test` `G12-MUX-01`); the new gate
     passes it. `G12-MUX-02` proves out-of-display writes never arm
     regardless of repeat count; `G12-MUX-10` proves the mux stays
     transparent (byte-identical to unarmed rendering) for a
     non-racing single-write-per-frame byte once armed by unrelated
     racing content.
- **Blocking finding (2026-07-13, this fixup round) — beast.nex is
  NOT a stale reference, it exposes a real pre-existing bug**: with
  heuristic 3 armed, `beast-demo`/`layers-beast-ula` diverge from
  their pinned references by 25077 px (0.077%). Root-caused with a
  scratch diagnostic build (temporarily forced `Ula::attr_vram_read()`
  to compute and compare both the armed and direct value on every
  call): beast.nex enables ULA shadow-screen (port 0x7FFD bit 3) but
  never changes the Timex screen-mode register (port 0xFF), so
  `mode_` stays `STANDARD`. `Ula::render_frame`/`render_scanline`
  derive the `alt` flag passed into `attr_vram_read()` from
  `attr_row_base >= 0x7800`, which is driven by **Timex screen mode**
  (`STANDARD` vs `STANDARD_1`) — **not** by shadow-screen state. VHDL
  `video/zxula.vhd:191` (`screen_mode_s <= i_port_ff_reg(2 downto 0)
  when i_ula_shadow_en = '0' else "000"`) and `zxnext.vhd:6649-6656`
  (`ula_bank_do <= vram_bank5_do1 when ula_vram_shadow = '0' else
  vram_bank7_do`) both show bank selection (5 vs 7) is driven
  **purely by the 7FFD shadow-screen signal**, decoupled from Timex
  mode — Timex mode only selects the addressing layout *within*
  whichever bank shadow-screen has already chosen, and is explicitly
  forced to standard layout when shadow is active. So for beast.nex
  (shadow=on, Timex mode=STANDARD), the VHDL-correct bank is 7, but
  `attr_vram_read()`'s `alt` (Timex-derived) says bank 5 — reading the
  WRONG plane once armed. This conflation predates this fixup round
  (landed in commit `92a56627`, "wire Ula render path to consume
  AttributeMux (STANDARD mode)") and was invisible before because
  `Ula::vram_read()` (the pre-existing pixel/unarmed-attribute path)
  correctly uses `vram_use_bank7_` (driven by shadow-screen alone) for
  bank choice, ignoring Timex mode entirely — only the newer
  `attr_vram_read()` armed path threads the wrong signal.
  Confirmed via a clean-code A/B: the untouched pre-fixup code
  (`.claude/worktrees/task8-nirvana-review` @ `edcdb89e`, still using
  heuristic 2) renders beast.nex byte-identical (0 px diff) to the
  pinned reference, because heuristic 2 never arms on beast.nex's one
  incidental attribute write. **The pinned reference is correct; the
  divergence is a real rendering bug**, not something a reference
  regen would fix.
  - **STOP condition per task instructions — not fixed in this round.**
    Fixing `attr_vram_read()`'s bank-selection wiring (thread
    `vram_use_bank7_` instead of/alongside the Timex-derived `alt`) is
    a Phase-B change, out of this fixup's scope (Problems 1+2 only).
    Left for the task lead to schedule as a follow-up before G12 can
    merge — the positional arm-condition fix (Problem 2) is itself
    correct and zero-false-negative, but shipping it as-is regresses
    `beast-demo`/`layers-beast-ula` by exposing this bug.
- **Round 3 (2026-07-13) — arm/gate REMOVED (user decision) + the
  bank-selection bug above FIXED; status upgraded to mostly-resolved,
  one open finding remains (see below).**
  - **No more arm/gate, by explicit user decision**, recorded
    verbatim: *"do not gate it. Do it at all times, and we'll try to
    optimize it later in the general optimization and profiling pass.
    5% penalty can be assumed (for the moment)."* Measured
    (`.prompts/2026-07-13.md` "Task 8 / Nirvana"): gated vs
    always-armed cost is statistically indistinguishable (headless
    beast.nex, 2500 frames, release, 3+ runs each side: main 38.5-40.6s,
    gated 39.8-42.8s, always-armed 42.7s) — the gate bought nothing
    measurable. `attr_mux_armed_`/`attr_mux_arm_next_frame_`/
    `attr_mux_write_still_relevant_()`/`kAttrMuxDispY`/`kAttrMuxDispH`
    deleted from `Mmu`; `Mmu::write()`'s detector now unconditionally
    calls `record_write()` for every write in the 768-byte attribute
    sub-range (still gated on address range — that's "is this relevant
    data at all", not a heuristic). `Ula::attr_vram_read()` no longer
    branches on an armed flag.
  - **The beast.nex bank-selection bug (documented above) is FIXED**:
    `attr_vram_read()`'s bank choice (mux5 vs mux7) now follows
    `vram_use_bank7_` — the exact same shadow-screen-driven signal
    `vram_read()` already used for pixels — instead of the Timex-mode-
    derived `alt`. `alt` still selects which 768-byte sub-window is
    read (STANDARD vs STANDARD_1 addressing), an orthogonal axis;
    STANDARD_1's genuine Timex alt-file range (bank 5 upper 8K, page
    0x0B) is untracked by `AttributeMux` and unaffected — the mux is
    only ever consulted for `!alt` (0x5800-based) addressing.
    `beast-demo`/`layers-beast-ula` are back to **0 pixel diff**
    against their pinned references (confirmed: full regression suite,
    63/63 pass).
  - **A second, unrelated fix was needed to ship always-on safely**:
    `AttributeMux::started()` — NOT a racing heuristic, a plain
    lifecycle guard. Consulting the mux unconditionally broke 14 tests
    (`ula_test`, `ula_integration_test`, `debugger_video_panel_test`)
    that construct a bare `Ula`+`Mmu` and write attribute bytes
    straight into `Ram`, bypassing `Mmu::write()`'s detector and never
    calling `Mmu::attr_mux_start_frame()` — so `AttributeMux::current_`
    stayed all-zero (default-constructed) and reads came back black
    instead of falling through to the real byte. `started()` is true
    once `start_frame()` has been called at least once (true for the
    entire life of a real running emulator, since production always
    calls it once per frame before any CPU execution); `attr_vram_read()`
    falls through to the plain read when false, reproducing pre-G12
    behaviour exactly for any caller that never opts into the
    per-frame lifecycle.
  - **New discriminative test** (`G12-MUX-09`, mmu_test): a frame with
    some cells written mid-frame and others left untouched — untouched
    cells must show real RAM content unchanged. This is the assertion
    whose absence let the round-2 bank-selection bug ship (reading the
    WRONG bank's baseline looked identical to "no fall-through" from
    the outside). Mutation-tested: temporarily forcing
    `AttributeMux::start_frame()` to ignore its baseline argument makes
    this row FAIL (0x00 instead of the real 0x99); reverting makes it
    PASS. `mmu_test` G12-MUX group: 10 rows → 9 (arm-latency-specific
    rows removed, one new row added); `test/unit-tests.conf` updated
    251 → 250.
  - **Open finding, NOT resolved — `bifrost.tap`'s title screen shows
    a busy, speckled colour pattern in several icons that could not be
    conclusively distinguished from genuine dense multicolour racing
    given available tooling** (no reference-emulator cross-check was
    performed; CSpect/ZEsarUX are present on the dev machine but were
    not used due to time budget). Diagnostic evidence gathered: the
    write log for one frame (2592 attribute-range writes, `bifrost.tap`
    running on plain 48K — shadow-screen and Timex mode are not in play
    here, so this is independent of the bank-selection fix above) is
    self-consistent when replayed in an independent Python
    reimplementation of `AttributeMux`'s exact algorithm (same
    picture, no cursor/ordering bug found); writes span framebuffer
    rows 42-184, i.e. well into the ACTIVE DISPLAY period, not confined
    to border/vblank as `attribute_mux.h`'s own header comment assumes
    ("True Nirvana demos always write the full 32-byte row during the
    horizontal border/blanking period before that row's first fetch").
    If `bifrost.tap` intentionally races attribute writes continuously
    across the whole active-display period (a plausible reading of
    "BIFROST*2" — true per-pixel-row multicolour, not just border-time
    redraw), jnext's per-scanline-not-per-T-state CPU execution
    granularity may be attributing some writes to the wrong side of a
    fetch boundary — a CPU/scheduler-level timing question, beyond
    `Ula`/`Mmu`, that this round did not investigate further.
    `nirvana.tap`'s brick-wall texture (unambiguous, visually confirmed
    two-tone red/yellow banding vs. flat red pre-G12) and `beast.nex`
    (0 px diff) both work correctly, so this is not a blanket failure
    of the mechanism — it is scoped to whatever `bifrost.tap`
    specifically does. Needs a CSpect/ZEsarUX cross-check before further
    action.
- **Coverage today**: `test/mmu/mmu_test.cpp` group `G12-MUX` (9
  rows, `G12-MUX-01`..`09`), all passing with real mutation evidence.
  `demo/nirvana_demo` (racing-the-beam verification demo, visually
  verified via manual headless screenshots per its landing commit;
  not yet wired into `test/00regression/regression_tests.conf` as an
  automated regression row). `test/00regression/tap/{bifrost,nirvana,
  nirvanap}.tap` (added to main 2026-07-13, commit `efd4769d`) were
  run manually against this round's build — see the open finding
  above; not wired into any regression manifest (no authorisation to
  add a reference image yet).
- **Dependencies**: none blocking merge for the `beast.nex`/pinned-
  reference acceptance bar; the `bifrost.tap` finding above is a
  follow-up investigation, not a regression against anything currently
  covered by an automated test.
- **Effort**: remaining work (bifrost.tap investigation, if pursued) —
  M (needs a reference-emulator cross-check and possibly a CPU/
  scheduler-level timing investigation, not a quick Mmu/Ula fix).

### G13. Per-scanline sprite-attribute multiplexing
- **What**: Sprite attrs (port 0x57, NR 0x75-0x79) read at frame-end;
  mux demos rewrite slot X/Y mid-frame to draw two visually-distinct
  sprites from one slot.
- **User impact**: any 128+ effective-sprites demo. Possibly partly
  contributes to parallax.nex side-by-side artefact.
- **Source ref**: `PER-SCANLINE-DISPLAY-STATE-AUDIT.md` Cat C row 1;
  `PARALLAX-NEX-INVESTIGATION.md` required-work table row 5.
- **Effort**: M.

### G14. Layer 2 enable / write-paging per-scanline (port 0x123B)
- **What**: Mid-frame L2-enable toggle for "L2 only on rows N-M".
- **Effort**: L.

### G15. Sprite-pattern reload mid-frame (port 0x5B uploads)
- **What**: Mid-frame pattern-RAM upload for animation beyond 64-
  pattern cap. Niche.
- **Effort**: M.

### G16. Beast.nex residual: NEX-loader bank-5 collision [closed]
- **Status: CLOSED 2026-05-04** (Tier B). `NexLoader::zero_bank5_screen_pages()`
  helper inline-in-header pre-zeroes the full 16 KB of pages 10+11 at the
  start of `apply()` before any SCREEN_LAYER2/ULA/LORES/HIRES ingest.
  ULA-only NEX screens (6912 B) no longer leave the upper 9472 B of bank 5
  with stale RAM contents. BOOT-NEX-07 in `mmu_test` flipped from skip → PASS
  with a discriminative test (pre-pollute with 0xCC, write 0xAA payload over
  6912 B, assert residual=0x00). beast-demo regression PASS at 0 pixel diff
  (no reference regen). Aggregate +1 PASS / -1 SKIP.
- **What was originally observed**: Beast main render RESOLVED via shadow-screen fix; small
  attribute leak remained in some paths consuming bank 5 past offset 0x1AFF.
- **Source ref**: `BEAST-NEX-INVESTIGATION.md` §"Verdict".

### G17. Parallax.nex "two-copies" mystery (post-LoRes)
- **What**: After G01-G02 land, side-by-side duplication may persist
  (PARALLAX-NEX-INVESTIGATION root-cause shortlist #3 — NOT
  explained by missing LoRes alone). Likely sprite X-wrap or L2-
  width handling.
- **Source ref**: `PARALLAX-NEX-INVESTIGATION.md` required-work table.
- **Dependencies**: G01 + G02 must come first.
- **Effort**: M.

### G18. Screenshot vertical scaling for 80x32 / 640x256 modes
- **What**: 640px-wide framebuffer saved as 640×256 with non-square
  pixels (1:2). Real screen aspect requires ×2 vertical scale.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1152`.
- **Proposed**: width-branch in `EmulatorWidget::save_screenshot`.
- **Effort**: L.

### G19. Save screenshot in `.SCR` format
- **What**: PNG only; no Spectrum-native 6912-byte bitmap+attr
  export. Common in ZX-scene tooling.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1156`.
- **Effort**: L.

### G20. Auto-named screenshots (no dialog)
- **What**: `Save Screenshot...` always prompts. Add fast path with
  auto-generated timestamp/sequence name.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1157`.
- **Effort**: L.

### G21. Raster / ULA-read indicator overlay
- **What**: Live overlay marking current raster position (vc/hc) +
  ULA-reading flag — diagnostic for beam-racing / Copper / Nirvana.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1171`.
- **Effort**: L.

### G22. Disassembly panel: ASM-only clipboard copy
- **What**: ASM panel copy includes addr / opcode / labels. Add
  selectable ASM-only copy.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1170`.
- **Effort**: L.

### G23. Redefinable / preset debugger keybindings
- **What**: F5/F6/F7/F8/F11/Ctrl+S hardcoded. Add presets ("borland",
  "cspect", "zesarux") and/or per-action remap.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1155`.
- **Effort**: M.

### G24. Main-window settings persistence (size/scale/CRT/speed/etc.)
- **What**: Debugger window persists position via
  `QSettings("JNEXT", "Debugger")`. Main emulator window does NOT
  persist position, scale (2×/3×/4×), CRT filter, fullscreen,
  machine type, or speed. Verified `QSettings` not used in
  `src/gui/main_window.{h,cpp}`.
- **User impact**: every launch resets user prefs.
- **Source ref**: `debugger_window.cpp:56,397`; project memory
  `project_emulator_phase1_status`.
- **Proposed**: `QSettings("JNEXT", "MainWindow")` save in
  `closeEvent`, restore in `show()`. Standard Qt idiom (~20 lines).
- **Effort**: L.

### G25. Debugger window stickiness to main window
- **What**: Debugger position saved / restored but does not track
  main-window drag in real time.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1213`; `TODO.md` Debugger.
- **Effort**: L.

### G26. Compositor open questions — ula_blend_mode 01 swap + L2 promotion
- **What**: `COMPOSITOR-TEST-PLAN-DESIGN.md` §Open Questions records
  6 honest semantic questions about NR 0x68 blend modes 110/111
  (`mix_top` vs `mix_bot` swap on `tm_pixel_below_2` looks inverted)
  and L2 promotion priority. Tests encode VHDL as-is; clarification
  needed from FPGA team.
- **Source ref**: `COMPOSITOR-TEST-PLAN-DESIGN.md:619-664`.
- **Dependencies**: needs FPGA-team confirmation.
- **Effort**: L (once oracle clarification available).

### G27. Compositor `rgb_blank_n_6` vs `rgb_blank_n` delay edge
- **What**: Stage-3 blanking uses pipelined `_6` version; drift
  would show as one-pixel edge artefact.
- **Source ref**: `COMPOSITOR-TEST-PLAN-DESIGN.md:659-663`.
- **Effort**: L.

### G28. Layer 2 G9-06 column-pipeline observable
- **What**: `hc_eff <= hc + 1` VHDL pipeline signal sat as a weak-
  case doc comment; should become observable per-column assertion if
  cycle-accurate refactor lands.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1159`.
- **Effort**: L (gated on cycle-accurate refactor).
- **Status (2026-05-03h)**: WONT. **Correction to original framing**: G117 (Copper cycle-accurate scheduler) is ALREADY CLOSED via `copper_integration_test G117-MPC-01` (commit `5745801` per `PARALLAX-NEX-INVESTIGATION.md:327`) — so G28 is NOT gated on G117. The actual gating dependency is **pixel-granular Layer 2 renderer** refactor (`Layer2::render_scanline` is currently scanline-granular, one chunk per row). Pixel-granular L2 rendering is architectural-grade (touches the renderer hot path every scanline, significant performance impact). Practical demand essentially zero in jnext-target software (NextZXOS + modern Spectrum Next demos). Test G9-G28-01 in `layer2_test` converted from `skip()` to `// WONT G9-G28-01` with rewritten comment block clarifying the corrected gating. Revisit only if a specific demo demonstrates per-pixel L2 timing sensitivity that cannot be resolved through other means. Branch `layer2-g28-wont` (`6a01b49`).
- **Status (2026-05-03h)**: WONT (pixel-granular Layer 2 renderer out of scope). The original "gated on cycle-accurate refactor" wording suggested G117 (Copper cycle-accurate scheduler) — but G117 is already CLOSED (parallax.nex investigation, commit `5745801`; `copper_integration_test` row `G117-MPC-01` PASSES). The actual gating dependency is a SEPARATE architectural change: **pixel-granular Layer 2 rendering**. Today's L2 renderer is scanline-granular (one full scanline rendered as a chunk), so VHDL `layer2.vhd:148` `hc_eff <= hc + 1` folds into the address formula and cannot be observed standalone. A pixel-granular L2 renderer would touch the renderer hot path on every scanline of every frame — a bigger architectural change than G117 was, with significant performance impact. Practical demand for per-pixel L2 timing observability in jnext-target software (NextZXOS user software + modern Spectrum Next demos) is essentially zero; the use case is "demo-scene cycle-counting effects within a single L2 scanline", virtually nonexistent in actual released software. Revisit only if a specific demo or program demonstrates per-pixel L2 timing sensitivity that cannot be resolved through other means. `test/layer2/layer2_test.cpp` row G9-G28-01 converted from `skip()` to `// WONT G9-G28-01` comment per `feedback_wont_taxonomy.md`. Branch `layer2-g28-wont`.

### G91. NR 0x44 priority bits 7:6 dropped — L2 priority promotion never fires [closed]
- **Status: CLOSED 2026-05-03** by G179 Issue #3 (commits `af59e12 → ec365db`). Two halves to the original gap, both now fixed:
  1. **Palette side** (`palette.cpp:240-252` claimed `val & 0x01`): already corrected ahead of G179. `palette.cpp:295` extracts `(val >> 6) & 0x03` on the second NR 0x44 byte and stores into the 2-bit `layer2_priority_[bank][idx]` slot per VHDL `zxnext.vhd:4920, :7025-7039`.
  2. **Renderer side** (`renderer.cpp` fills `layer2_priority_[]` with false every row): closed by G179 Issue #3. `Layer2::render_scanline` gained a `bool* priority_dst = nullptr` parameter; each opaque emit writes `priority_dst[x] = palette.layer2_priority_high(colour_idx)`. All three resolution modes propagate (res-0 256→640 doubled, res-1 320→640 doubled, res-2/3 native 640 per-nibble). Renderer wires `layer2_priority_.data()`; debugger panel passes nullptr.
- **Test coverage**: `test/layer2/layer2_test.cpp` Group 11 (G11-00..06) — narrow, wide-320, native-640, transparency, nullptr default. Per-scanline NR 0x44 priority replay was already wired by `PaletteManager::change_log_` infrastructure.
- **What was originally observed**: VHDL `zxnext.vhd:4920` captures `nr_palette_priority <= nr_wr_dat(7 downto 6)` on NR 0x44 second-write; stored in palette RAM at `:7025` and consumed at `:7039-7050` (`layer2_priority_2 <= layer2_prgb_1(9)`).
- **User impact (was)**: any L2 program using palette-bit-9 to promote pixels above sprites/ULA/TM rendered with the priority bit as no-op; compositor's L2-priority branch was dead in production despite 18+ test rows passing on synthesised inputs.
- **Source ref**: Wave-1 layer2-lores-compositor (NEW-L2-1); reviewer APPROVE.

### G92. port 0x123B cpu_do(4)=1 offset mode ignored
- **What**: VHDL `zxnext.vhd:3914-3923` bifurcates 0x123B writes on `cpu_do(4)`. With bit 4=1, bits 2:0 latch into `port_123b_layer2_offset` and feed `layer2_active_bank_offset` at `:2966-2967`. jnext `mmu.cpp:183-202` has no bit-4 branch; every write is the cpu_do(4)=0 path. Cross-bucket dup with NEW-MMU-3.
- **User impact**: software using offset-shift workflow (segment + offset writes) overwrites L2 enable/wr_en state, dropping display until base 0x123B re-issued.
- **Source ref**: Wave-1 layer2 (NEW-L2-2) + Wave-2 memory (NEW-MMU-3); both APPROVE; reviewers note plausible parallax.nex relevance pending disassembly check.
- **Coverage today**: none.
- **Dependencies**: G144 (map_shadow bit 3) and G145 (read-back) ship together — same composition formula.
- **Effort**: L-M.

### G93. Compositor layer2_priority_ not pixel-doubled-aware on native 640
- **What**: `renderer.cpp:194-201` doubles `layer2_line_` and `layer2_priority_` only inside the `if (!(layer2.enabled() && resolution() >= 2))` branch. In native 640 mode the priority array is neither doubled nor populated — once G91 lands, right-pixel priority will fail.
- **User impact**: latent until G91 lands; right-pixel artefacts under L2 native 640 + palette priority.
- **Source ref**: Wave-1 layer2 (NEW-CMP-2); reviewer APPROVE.
- **Coverage today**: not exercised.
- **Dependencies**: must land alongside or immediately after G91.
- **Effort**: L.

### G94. LoRes radastan sub-mode (NR 0x6A) absent — distinct from G01
- **What**: VHDL `lores.vhd` mode_i='1' selects radastan (128×96 4-bit, dfile via port 0xFF bit 0 XOR `nr_6a_lores_radastan_xor`); NR 0x6A holds radastan/xor/palette_offset (`zxnext.vhd:1203-1205,5032-5034,5456-5458`). jnext: `grep` for `nr_6a|radastan` returns no matches; G01 plan scope omits this surface.
- **User impact**: radastan-mode programs (niche but real) won't render even after G01 lands.
- **Source ref**: Wave-1 layer2 (NEW-LR-1); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: requires G01 / TASK-LORES-PLAN to land first; treat as scope extension within that plan, not standalone.
- **Effort**: M.

### G95. NR 0x09 bit 4 sprite_tie not wired
- **What**: VHDL `zxnext.vhd:5187,1123,4352`; `sprites.vhd:60,594-612,653-654` ties NR 0x34 mirror_sprite_q to port 0x303B's attr_index. jnext `emulator.cpp:436-451,1707-1720` set NR 0x09 handlers with no `sprites_.set_mirror_tie(...)`; method does not exist.
- **User impact**: programs relying on the unified mirror+attr-index path see writes land in the wrong sprite slot.
- **Source ref**: Wave-1 sprites-tilemap (NEW-SPR-1); reviewer APPROVE.
- **Coverage today**: G1.AT-11 stub.
- **Dependencies**: must be implemented together with G96 (VHDL attr_num_change/mirror_num_change interlock).
- **Effort**: L.
- Also relevant to section D.

### G96. NR 0x35-0x39 vs 0x75-0x79 increment-semantics divergence
- **What**: VHDL `zxnext.vhd:4855-4877,4916` — `nr_sprite_mirror_inc <= nr_sprite_mirror_we AND nr_wr_reg(6)`. NR 0x35-0x39 (bit 6=0) MUST NOT increment; NR 0x75-0x79 (bit 6=1) increments after EVERY byte. NR 0x34 doesn't increment. jnext `emulator.cpp:577-580,847-850` dispatches both ranges to same `sprites_.write_attr_byte`, which uses port-0x57-style attr_slot_ and increments only after byte 4 (or byte 3 without extended) — opposite of VHDL.
- **User impact**: NR-mirror sprite stream lands in wrong slots; 0x75 streams stick on one slot; 0x35-0x39 sees unintended increments.
- **Source ref**: Wave-1 sprites (NEW-SPR-2) + Wave-2 NextREG (NEW-NR-4 — strict subset, REJECTED for fold). Reviewer APPROVE.
- **Coverage today**: G1.AT-09/10/12.
- **Dependencies**: bundle with G95 (attr_num_change/mirror_num_change interlock).
- **Effort**: M.

### G97. NR 0x19 (sprite clip) and NR 0x1A (ULA clip) read handlers absent
- **What**: VHDL `zxnext.vhd:5956-5970` — `port_253b_dat` 4-way mux on nr_19_sprite_clip_idx / nr_1a_ula_clip_idx. jnext `emulator.cpp:514-533` registers WRITE only; NR 0x18/0x1B have read handlers (compare `:504-511`/`:548-555`).
- **User impact**: reads of NR 0x19/0x1A return raw last-write byte, not indexed clip register.
- **Source ref**: Wave-1 sprites (NEW-SPR-3); reviewer APPROVE — concrete subset of G56.
- **Coverage today**: subset of G56 systemic.
- **Dependencies**: copy NR 0x18/0x1B reader pattern; reads must NOT advance idx (matches NR 0x18/0x1B).
- **Effort**: L.
- Also relevant to section D.

### G98. Tilemap text-mode RGB transparency check missing
- **What**: VHDL `zxnext.vhd:7109` — `tm_transparent <= '1' when (tm_pixel_en_2='0') or (tm_pixel_textmode_2='1' and tm_rgb_2(8:1) = transparent_rgb_2) or (tm_en_2='0')`. jnext `renderer.cpp:286` checks alpha=0 only; no NR 0x14 RGB compare; no per-pixel textmode flag (precondition G101).
- **User impact**: text-mode tilemap pixels with the global-transparent RGB render opaque; ULA does not show through where it should. TM-44/TM-93/TM-94 currently `skip()` for this exact reason.
- **Source ref**: Wave-1 sprites-tilemap (NEW-TM-1); reviewer APPROVE.
- **Coverage today**: 3 skip rows; bundle with G101 fix.
- **Dependencies**: G101 (per-pixel textmode flag).
- **Effort**: L.

### G99. NR 0x6E / NR 0x6F bit 6 reserved-bit mask missing on read-back [closed]
- **Status: CLOSED 2026-05-03** by G56 cluster E (commit `b1606fb`). The VHDL-faithful `& 0xBF` mask was already present in `Tilemap::get_map_base_read()` / `get_def_base_read()` since commit `e375456` (2026-04-28); cluster E added explicit `set_read_handler(0x6E)` / `set_read_handler(0x6F)` wiring + comment attribution + VHDL-cited test rows in `nextreg_integration_test.cpp` group `G56-CR-Cluster-E`.
- **What was originally observed**: VHDL `zxnext.vhd:6108,6111` — bit 6 forced 0 on read. jnext `tilemap.h:51,55` `get_map_base_raw()` returned raw byte; no NR 0x6E/0x6F read handlers were registered, so reads fell through to `regs_[]` and bit 6 leaked through write-mask divergence.
- **Test coverage**: `G56-CR-NR6E-FF` (write 0xFF → read 0xBF) + `G56-CR-NR6E-RT` (round-trip) + matching pair for NR 0x6F in `nextreg_integration_test.cpp`.

### G100. Tilemap per-line scroll snapshot caps at line 320
- **What**: `tilemap.h:76-87` uses `std::array<uint16_t, 320>` with `if (line >= 0 && line < 320)` guard. Mid-vblank scroll writes (line ≥ 320) silently dropped from snapshot; canonical fix mirrors `SpriteEngine::start_frame` catch-up at `sprites.cpp:119-144`.
- **User impact**: latent — only matters when a demo DMA-streams NR 0x30/0x31 across vblank.
- **Source ref**: Wave-1 sprites-tilemap (NEW-TM-7); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: none.
- **Effort**: L.

### G101. Tilemap pixel_textmode_o flag not exposed to compositor
- **What**: VHDL `tilemap.vhd:62, 443` exposes `pixel_textmode_o`; consumed at `zxnext.vhd:7072,7109`. jnext `Tilemap` keeps `text_mode_` as a global flag (`tilemap.h:138`), no per-pixel emission; `renderer.h:188` has only `tm_pixel_below_`.
- **User impact**: precondition for G98; without the flag, mid-frame text/standard mode mix via Copper NR 0x6B can't be honoured per-pixel.
- **Source ref**: Wave-1 sprites-tilemap (NEW-TM-8); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: bundle with G98.
- **Effort**: L.

### G102. ULAnext (NR 0x42/0x43 b0) palette-encoding runtime renderer integration absent [closed]
- **Status: CLOSED 2026-05-01** (13-commit chain on `feature/g102-g105-ulanext-palette` ending at `455e286`). Collapsed jnext's two-mirror ULA palette (legacy 16-entry + wider 256-entry) into a single 256-entry × 2-bank store matching VHDL `palette_utm` (`zxnext.vhd:6960`). Standard ULA renderer now computes 8-bit `ula_pixel` per VHDL `zxula.vhd:543-553` and reads the wider palette at the full 8-bit address — no fold, no 16-entry mirror. `kUlaPalette` constant deleted; legacy storage / accessors / save-load / baseline tier all removed; debugger updated to show 32 std-ULA palette entries; ~60 test sites reframed to query the wider palette via VHDL-faithful `ula_pixel` addresses. INT-ULANEXT-02 promoted to live check covering ULAnext renderer + bank isolation. **Validated on real ZX Next hardware via beast.nex** (which writes paper colours via the documented `PAPER_INDEX equ 16` idiom; pre-fix renderer broke beast — magenta-sky regression caught by user GUI inspection + hardware verification). Independent reviewer APPROVE. Regression 33/0/0 on main, every demo 0 pixel diff including beast/copper/parallax/tilemap.

### G103. ULA+ (port 0xBF3B/0xFF3B) palette-encoding runtime path absent
- **What**: VHDL `zxula.vhd:531-541` `i_ulap_en='1'` 8-bit pixel with bits[7:6]="11"; `zxnext.vhd:4525-4538` `port_bf3b_ulap_index` (low 6 bits when mode_group="00"). jnext `ula.cpp:75` has encoder (S7.01-S7.06 pass); `emulator.cpp:1937-1941` writes only `set_ulap_mode(top 2 bits)` — index stub.
- **User impact**: ULA+ programs cannot drive their 64-entry palette window; index writes silently discarded.
- **Source ref**: Wave-1 ula (NEW-ULA-2); reviewer APPROVE.
- **Coverage today**: encoder unit-tested; runtime path absent.
- **Dependencies**: shares infrastructure with G102/G105.
- **Effort**: M.

### G104. HI_RES (Timex 512×192) renders at 256 px (half-resolution) [closed]
- **Status: CLOSED 2026-05-02** via the 8-phase G104 plan (`doc/design/G104-HI-RES-512-CANONICAL-FRAMEBUFFER-PLAN.md`); plus an ink/paper VHDL drift addressed in G179 Issue #2 (G165, see below). Final main HEAD `b631b94`. Aggregate 3763/3590/0/173; regression 34/0/0 with 27 PNG references regenerated at canonical 640×512.
- **What landed**: canonical 640-wide framebuffer end-to-end (`Renderer::FB_WIDTH = 640`, `DISP_X = 64`, `DISP_W = 512`); ULA / Layer2 / Tilemap / Sprites all emit native 640 (with VHDL-faithful pixel-doubling at the hardware-shift-register level for std-ULA / 256-mode L2 / 40-col tilemap). HI_RES Timex emits true 512 active pixels via byte-interleaved s0/s1 emission per VHDL `zxula.vhd:389` (8 px from screen-0 col N, then 8 px from screen-1 col N, MSB-first). The pre-existing comment at `ula.cpp:945-953` had described a bit-granularity interleave that contradicted VHDL — also fixed.
- **GUI / output**: vertical 2× scaling at the GUI prescale step (in-memory 640×256 → displayed 640×512) for CRT-faithful 4:3 geometry; PNG screenshots and FFmpeg video recorder emit 640×512 (vertical-doubled at export).
- **What was originally observed**: VHDL `zxula.vhd:389-395` shift_reg_32 untouched in hi-res (14 MHz pixel clock when `screen_mode(2)='1'`). jnext `ula.cpp:646+` discarded every alternate hi-res pixel; comment at `:635-640` documented the 256-pixel approximation.
- **User impact (was)**: Timex hi-res text/programs rendered at half horizontal resolution; 512-column text was unreadable.
- **Test coverage**: `test/ula/ula_test.cpp` S5.10 / S5.10b (byte-interleave geometry with 0xAA/0x55 + 0xF0/0x0F distinct stimuli); S5.10c (HI_RES border row 640-cell width). S3.09-S3.13 (border-flag plumbing, G179 Issue #4).
- **Source ref**: Wave-1 ula (NEW-ULA-3); reviewer APPROVE.

### G105. HI_RES 6-bit border palette-group encoding not modelled [closed]
- **Status: CLOSED 2026-05-01** (part of the G102 13-commit chain; primary commit `a3dc90a` then VHDL-faithful for std ULA in `e0c9970` after the single-mirror collapse). HI_RES border `border_clr_tmx` now computed as the 8-bit `0x10 | (attr(6)<<3) | (~paper&7)` per VHDL `zxula.vhd:419 + :543-553` and looked up in the wider 256-entry palette. The std-ULA fallback that used to truncate to 3-bit-paper became natural under the single-mirror design — no "documented limitation." S5.11 promoted to live check covering ULAnext + ULA+ + std-ULA paths. Independent reviewer APPROVE.

### G106. Line-interrupt scheduler off-by-one + target=0 wrap not applied — CLOSED 2026-04-28 (task8-t1-videotiming)
- **What**: VHDL `zxula_timing.vhd:563-583`: `int_line_num = c_max_vc` when `i_int_line=0`; else `target-1`; pulse on `(hc_ula==255) and (cvc==int_line_num)`. jnext `emulator.cpp:2540-2550` schedules at `frame_cycle + line_int_value_ * master_cycles_per_line` with no transform — helper `VideoTiming::int_line_num()` exists at `timing.h:178-186` but is never read.
- **User impact**: line interrupts fire one full line late; target=0 silently misfires at frame top instead of `c_max_vc` (last line).
- **Source ref**: Wave-1 ula (NEW-ULA-5); reviewer APPROVE — high impact.
- **Coverage today**: G71 reframes from "academic cleanup" to user-visible bug.
- **Dependencies**: shares fix with G71 / G107.
- **Effort**: L.

### G107. ULA-frame-interrupt scheduler ignores per-machine c_int_h / c_int_v — CLOSED 2026-04-28 (task8-t1-videotiming)
- **What**: VHDL `zxula_timing.vhd:155 (Pentagon),:189 (+3),:233 (60 Hz),:265 (48K)` give different (hc,vc) per machine. jnext `emulator.cpp:2523` uses `tstates_per_line * 8` machine-blind. `VideoTiming::int_position()` correct but unused by scheduler.
- **User impact**: Pentagon/+3 frame-INT off-position by ≤1 line; T-state-counted demos misalign.
- **Source ref**: Wave-1 ula (NEW-ULA-6); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: shares fix with G71 / G106.
- **Effort**: L.

### G108. NR 0x69 bits 6/5:0 + NR 0x22 b2 + NR 0xC4 b0 → port_ff_reg fan-out absent [closed]
- **Status: CLOSED 2026-05-03**.
- **What was originally observed**: VHDL `zxnext.vhd:3617-3622` updates `port_ff_reg(5:0)` from NR 0x69 bits 5:0; `port_ff_reg(6)` from NR 0x22 bit 2; `port_ff_reg(6)` from `NOT nr_wr_dat(0)` on NR 0xC4. Three earlier waves left the picture inconsistent: by 2026-04 the three NR handlers in `src/core/emulator.cpp` already updated `Emulator::port_ff_reg_` correctly, but none of them propagated the change to `Ula::screen_mode_reg_`. So a Timex program switching mode via NR 0x69 (rather than port 0xFF) saw `port_ff_reg_` updated but the renderer never re-decoded — the screen mode in `Ula::mode_` stayed at whatever the last port-FF write had left.
- **User impact (was)**: NextZXOS firmware and Timex software switching screen modes via NR 0x69 had no rendered effect. NR 0xC4 bit 0 ULA-int-disable mirror via the inverted-polarity bit was also silent on the renderer side (it remained correctly observed by the int-controller path, but didn't ripple through Ula).
- **Fix**: each of the three NR handlers (`emulator.cpp:847` for NR 0x22, `:1106` for NR 0x69, `:1234` for NR 0xC4) now calls `renderer_.ula().set_screen_mode(port_ff_reg_)` after updating `port_ff_reg_`. `Ula::set_screen_mode` re-decodes mode_/alt_file_ from bits 2:0 and appends a per-scanline G07 change-log entry — keeping the per-scanline replay symmetric with port-FF and the other NR-side writes.
- **VHDL ref**: `zxnext.vhd:3610-3635` (the elsif-priority chain), `zxula.vhd:191` (`screen_mode_s <= i_port_ff_reg(2:0)`), `zxula.vhd:419 + 426-427` (HI_RES paper from bits 5:3).
- **Test coverage**: `nextreg_integration_test` group `G108-PortFF-Fanout` with 7 rows: G108-NR69-MODE (NR 0x69 = HI_RES + paper=5 → port_ff_reg(5:0) + Ula::screen_mode_reg_ + mode bits 2:0 = 110), G108-NR69-PRESERVE-BIT7 (NR 0x69 fan-out only touches bits 5:0; bit 6 from prior port-FF write preserved), G108-NR22-INTDIS-SET / -CLR (NR 0x22 bit 2 sets/clears port_ff_reg(6)), G108-NRC4-INTDIS-SET / -CLR (NR 0xC4 bit 0 inverted polarity), G108-PORTFF-WINS (direct port-FF write supersedes accumulated NR-side fan-out — full byte replace).
- **Source ref**: Wave-1 layer2 (NEW-CMP-1) + Wave-1 ula (NEW-ULA-7) + Wave-2 NextREG (NEW-NR-2). Closure followed G164 (which made `set_screen_mode` VHDL-correct on bits 2:0) — no longer pointless to plumb the fan-out into the renderer.

### G109. NR 0x64 cu_offset not applied to line-int comparison — CLOSED 2026-04-28 (task8-t1-videotiming)
- **What**: VHDL `zxula_timing.vhd:577` compares against `cvc` (offset-adjusted Copper VC, reload from `'0' & i_cu_offset` at `:455-466`). jnext `emulator.cpp:2540-2550` schedules at raw `vc`. Copper internal compares are correct.
- **User impact**: NR 0x64 ≠ 0 + line-IRQ raster split misaligned.
- **Source ref**: Wave-1 ula (NEW-ULA-8) + Wave-1 copper (NEW-COP-2). Cross-bucket dup; same gap.
- **Coverage today**: none.
- **Dependencies**: shares scheduler refactor with G106/G107.
- **Effort**: L.

### G163. Line interrupt schedule does not re-evaluate on mid-frame NR 0x22 / NR 0x23 / NR 0xC4 (bit 1 mirror) writes — CLOSED 2026-04-30
- **What**: VHDL `zxula_timing.vhd:577` fires the line-int pulse every cycle when `(hc_ula==255 AND cvc==int_line_num)` — fully dynamic. jnext scheduled the line-int ONCE per frame in `Emulator::run_frame()`; the NR 0x22 / NR 0x23 / NR 0xC4 write handlers updated `VideoTiming` state but did not re-schedule. Demos that chain line interrupts mid-frame (writing a new target into NR 0x23 from inside the line-IRQ handler) silently lost every chained re-arm — only the FIRST line-int per frame fired. NR 0xC4 bit 1 is a hardware mirror of NR 0x22 bit 1 (both write the same `nr_22_line_interrupt_en` flip-flop at zxnext.vhd:5607-5610, which feeds `i_inten_line` of the comparator at :6752), so the same defect applies on every NR 0xC4 write.
- **Driver demo**: `parallax.nex` (Phase B disassembly 2026-04-30, bank 6 `z88dk-dis -mz80n`). The demo's per-frame IRQ handler at offset `0x062E` writes NR 0x23 thirteen times per frame (`target += 0x10`, lines 198, 244, 4, 20, 36, 52, 68, 84, 100, 116, 132, 148, 164, then stops). Each chained line-IRQ DMA-pages alternate L2 source banks (0x1D / 0x1E / 0x21 / 0x22) into slot 7 and copies pixel data into them. With 12/13 IRQs swallowed those banks stayed zero, producing the 69%-black L2 output observed in `--compositor-trace` at frame 250.
- **User impact**: any demo that uses chained line interrupts to drive mid-frame state (palette rotation, L2 source-bank flip, raster splits via NR 0x14/0x15/0x16) renders flat. parallax.nex was the most prominent affected title.
- **Source ref**: `doc/issues/PARALLAX-NEX-INVESTIGATION.md`; memory note `project_parallax_line_int_root_cause.md`.
- **Fix**: introduced `Emulator::reschedule_line_interrupt()` (Shape B with generation counter — strict superset of "compare target at fire time" because same-target rewrites also produce a fresh schedule). Wired from four call sites: NR 0x22 write handler (`emulator.cpp:864`), NR 0x23 write handler (`emulator.cpp:875`), NR 0xC4 write handler (mirror of NR 0x22 bit 1, `emulator.cpp:1241`), and frame-start in `run_frame()`. The lambda captures the gen by-value and no-ops at fire time when the captured value differs from `line_int_schedule_gen_`. Out-of-range or already-passed targets are handled per Shape B (roll forward by one frame for the parallax 8-bit ADD-0x10 wrap; bump-and-return for disable; bump-and-return for offset >= master_cycles_per_frame).
- **VHDL reference**: `zxula_timing.vhd:577` (fire predicate, fully dynamic), `:566-570` (target=0 → c_max_vc; target=N → N-1), `zxnext.vhd:5607-5610` (NR 0x22 bit 1 / NR 0xC4 bit 1 mirror → `nr_22_line_interrupt_en`), `:6752-6753` (FF feeds `i_inten_line` / `i_int_line`).
- **Status**: closed by this commit.
- **Test coverage**: `videotiming_test` Section 8 — VT-G163-MIDRETARGET-01 (mid-frame retarget to still-future line: 2 fires same frame), VT-G163-WRAP-02 (retarget to already-passed line: deferred to next frame; total 1+1 across two run_frame calls), VT-G163-DISABLE-03 (NR 0x22 bit 1 cleared mid-frame: pending fire no-ops via gen-check; zero fires), VT-G163-C4-DISABLE-04 (same disable semantics driven through NR 0xC4 bit 1 mirror).

### G116. NR 0x61 / NR 0x62 Copper read handlers absent
- **What**: VHDL `zxnext.vhd:6083-6087` returns nr_copper_addr / mode + addr_msb. jnext `Copper::read_reg_0x61/0x62` exist (`copper.h:76-79`, `copper.cpp:238-244`) but `emulator.cpp:635-644` registers no read handlers; reads fall through to regs_[].
- **User impact**: Copper development tools / NextDoc API see stale data for nr_copper_addr.
- **Source ref**: Wave-1 copper (NEW-COP-1); reviewer APPROVE.
- **Coverage today**: subset of G56 pattern but local 2-line wiring.
- **Dependencies**: cheap.
- **Effort**: L.
- Also relevant to section D.

### G117. Copper executes per Z80 instruction, not per 28 MHz cycle
- **What**: VHDL `device/copper.vhd:54-119` runs at `i_CLK_28` rising edge — at most one MOVE/WAIT advance per 28 MHz cycle. jnext `copper.cpp:75-155` is called once per Z80 instruction (`emulator.cpp:2791-2797,2991-2996`). Dense Copper bursts in a single instruction window collapse to one step.
- **User impact**: tilemap-class effects with 32 MOVEs/scanline run as if 32× slower; documented as a contributor to parallax.nex visual divergence.
- **Source ref**: Wave-1 copper (NEW-COP-3); reviewer APPROVE — distinct from G65.
- **Coverage today**: PARALLAX-NEX-INVESTIGATION.md hypothesis.
- **Dependencies**: cross-link with G65 (NR-write priority); under fully cycle-accurate scheduler the two converge.
- **Effort**: M-H.

### G118. Copper instruction RAM cleared on soft reset (VHDL preserves)
- **What**: VHDL `zxnext.vhd:3959-3996` — `copper_inst_msb_ram` / `copper_inst_lsb_ram` are dpram2 with no reset port; only `nr_copper_addr` and `nr_62_copper_mode` cleared. jnext `copper.cpp:51-52` calls `instructions_.fill(0)` on reset.
- **User impact**: soft-reset menus that re-run a Copper program implicitly lose the payload; software must re-load.
- **Source ref**: Wave-1 copper (NEW-COP-4); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: remove `instructions_.fill(0)` from reset.
- **Effort**: L.

### G132. F-key state machine + 50-60/cpu-speed/scandouble hotkey edge model absent
- **What**: VHDL `input/membrane/emu_fnkeys.vhd:53-202` 7-state FSM consuming i_button_m1_n / i_button_reset_n + membrane → `o_fnkeys[10:1]`. jnext: `grep` for `i_SPKEY_FUNCTION`/`emu_fnkeys` returns no hits; only F9/F10 NMI source pulses wired.
- **User impact**: pressing host-mapped F1/F2/F3/F4/F7/F8 does nothing emulator-side (GUI menus provide equivalents — power-user/kiosk loss).
- **Source ref**: Wave-2 input (NEW-KB-2); reviewer APPROVE — Display=Y (F2 scandouble + F3 50/60 + F7 scanline weight).
- **Coverage today**: none.
- **Dependencies**: distinct from G125 / G147 / G152 (those are NR-side / host-key dispatch).
- **Effort**: M.
- Also relevant to section B.

### G144. port 0x123B map_shadow bit 3 — read/write-over uses wrong bank
- **What**: VHDL `zxnext.vhd:2968` selects `nr_13_layer2_shadow_bank` when `port_123b_layer2_map_shadow='1'`. jnext `mmu.cpp:183-202` records active bank only; `mmu.h:141-156` reads `l2_bank_` always.
- **User impact**: L2 double-buffering via 0x123B bit-3 (instead of NR 0x12/0x13 swap) writes wrong bank.
- **Source ref**: Wave-2 memory-mmu (NEW-MMU-4); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: bundle with G92, G145.
- **Effort**: L.

### G146. port_fd_conflict_wr — Soundrive Mode 2 vs paging-port write conflict [closed]
- **Status: CLOSED 2026-07-14** (Task 57, branch `task57-sd2`). `PortDispatch`
  gained a `decline_write()` fall-through (a write handler whose io_en decode
  gate is off passes the OUT to the next-most-specific matching handler,
  mirroring the VHDL parallel decodes), and the SD2 0xF1/0xF9 handlers in
  `emulator.cpp` decline when NR 0x84 bit 2 is clear. With the gate SET the
  SD2 handlers (mask 0x00FF) already out-specify the paging handlers, so the
  7FFD/DFFD/1FFD/3FFD write (incl. +3 motor bit and 3FFD FDC-trap strobe,
  which live inside those handlers) is fully suppressed and the byte goes to
  the Soundrive channel — exactly `zxnext.vhd:2708, 2718-2720, 2725,
  2775-2778`. Rows: `audio_port_dispatch_test` SD2-01/SD2-02 (re-homed from
  `mmu_test` Cat 19 — bare-Mmu tier can't reach port dispatch).
- **What (original, corrected)**: VHDL `zxnext.vhd:2708,2718-2720` suppresses
  port_7ffd/dffd/1ffd/3ffd writes when the OUT's low byte is 0xF1/0xF9
  (full-8-bit lsb decode) AND SD2 is enabled. jnext dropped such writes with
  SD2 *disabled* (the real bug — dispatch swallowed them) and note: the
  original entry's "0xF1FD/0xF9FD writes" wording was wrong (those have low
  byte 0xFD); colliding addresses are e.g. 0x7FF1/0xDFF9/0x1FF1.
- **Source ref**: Wave-2 memory (NEW-MMU-6); reviewer APPROVE.
- **Dependencies**: gated by NR 0x84 bit 2 (`port_dac_sd2_ABCD_*_io_en`,
  `internal_port_enable(18)`), not bit 1 as originally recorded.
- **Effort**: L.

### G147. F8/F3/F5/F6 host hotkeys to NR 0x07 / 50-60 / expbus unwired [closed]
- **Status: CLOSED** (landed with the G132 emu_fnkeys work,
  `src/input/emu_fnkeys.cpp`): F2/F3/F7/F8 rising-edge dispatch, each
  gated on its `nr_06_hotkey_*_en` bit per VHDL (F3 → hotkey_5060,
  zxnext.vhd:6342; F8 → cpu-speed cycle). The F3 path writes through
  NR 0x05 bit 2 and, since Task 56 (2026-07-14), actually re-derives
  video timing at the frame edge.
- **What (original)**: VHDL `zxnext.vhd:5790-5791,6342-6347` increments `nr_07_cpu_speed` from F8; F3 toggles 50/60Hz; gated by `nr_06_hotkey_*_en`. jnext had no hotkey path at audit time.
- **Source ref**: Wave-2 memory (NEW-MMU-7); reviewer APPROVE.

### G150. NR 0xFF write commits ULA/TM palette entry at bf3b-indexed slot
- **What**: VHDL `zxnext.vhd:6957`: `nr_ulatm_we <= (nr_palette_we and not (nr_43_palette_write_select(1) xor sel(0))) or nr_ff_we`. NR 0xFF writes the ULA/TM palette RAM at `(0, sel(2), 1, 1, port_bf3b_ulap_index)` with the value derived from `nr_wr_dat`. jnext: no NR 0xFF write_handler; `regs_[0xFF]` storage only.
- **User impact**: ULA+ legacy palette poke side-channel silent.
- **Source ref**: Wave-2 NextREG (NEW-NR-5); reviewer APPROVE WITH REVISION (drop "may be cargo-cult" hedge — confirmed observable).
- **Coverage today**: none.
- **Dependencies**: G102/G103 palette infrastructure.
- **Effort**: L.

### G156. NEX loader ignores loading_bar/loading_delay/start_delay/colour [closed]
- **Status: CLOSED 2026-07-13** (Task 8b, two independent reviews). NEX V1.1+
  header fields at offsets 130-133 (`loading_bar`, `loading_bar_colour`,
  `loading_delay`, `start_delay`) — jnext's `nex_loader.cpp` parsed but
  ignored all four; now honoured against the reference loader,
  `tbblue/src/asm/nexload/nexload.asm` (this is a file-format convention,
  not VHDL — nexload.asm is the oracle here, not the FPGA source).
- **What was originally observed**: `nex_loader.cpp:89-92` read the four
  bytes into `NexHeader` but never acted on them — no bar drawn, no delay
  frames honoured.
- **Fix**: `NexLoader::render_progress_mark()` replicates nexload.asm:616-621
  `progress` — writes a real 4-byte, colour-verbatim mark into physical bank
  11 (MMU page 23) at a bank-slot-index-derived address, drawn for all 112
  `kBankOrder` slots (present or not) when `loading_bar != 0`.
  `NexLoader::inter_bank_delay_frames()` / `boot_hold_frames()` replicate
  nexload.asm's delay semantics (nexload.asm:541,612-614: 109 post-early
  slot-iterations x `loading_delay`, gated on `screen_flags != 0`;
  nexload.asm:575-577: unconditional `start_delay`). `apply()` computes the
  total and calls the new `Emulator::set_boot_hold_frames()`;
  `Emulator::run_frame()` gained a `boot_hold_frames_remaining_ > 0` branch
  (peer to the existing DMA-stall branch) that skips CPU instruction
  fetch/execute for that many frames while rendering/audio/scheduler still
  run every frame, matching nexload.asm's own DI raster-wait.
- **Residual limitation (by design, not a follow-up bug)**: the loading bar
  is written to VRAM **correctly** but is **NOT observable on a bare
  `--load file.nex`** (CLI or GUI File>Load). Layer 2 starts disabled
  (`Layer2::reset()` sets `enabled_ = false`) and neither `nexload.asm` nor
  `NexLoader::apply()` ever writes NR 0x69 bit 7 — on real hardware the bar
  is only visible because whatever screen was already showing (e.g. the
  NextZXOS file browser) had Layer 2 on before nexload.asm ran. This was
  proven end-to-end with a throwaway scratch harness that explicitly forced
  Layer 2 on before drawing the bar (see the Task 8b commit history for the
  screenshot). A future reader hitting "the bar doesn't show up" on a plain
  `--load` should read this note before re-diagnosing it as a bug.
- **Test coverage**: `test/mmu/mmu_test.cpp` BOOT-NEX-03/04/05/06 (pure
  `render_progress_mark`/`inter_bank_delay_frames`/`boot_hold_frames`
  assertions, mutation-tested); `test/mmu/mmu_integration_test.cpp`
  G156-HOLD-01..09 (drives the real `Emulator::run_frame()` hold branch:
  decrement-per-frame, PC/R frozen while held, positive-control resume once
  the hold ends, `save_state`/`load_state` round-trip taken mid-hold).
  Independent review round 1 found the first four rows alone insufficient
  (stubbing the `run_frame()` branch dead left the whole suite green) —
  the G156-HOLD rows close that gap and were mutation-tested against the
  same stub.
- **Source ref**: Wave-2 nmi-boot-sd-rtc (NEW-BOOT-4). Two independent
  reviews: round 1 REJECT (missing `run_frame()` branch coverage), round 2
  APPROVE.
- **Dependencies**: none remaining.
- **Effort**: M (as estimated).

### G164. jnext `Ula::set_screen_mode` decoded mode bits from port_ff(5:3) instead of VHDL port_ff(2:0) [closed]
- **Status: CLOSED 2026-05-03** by G179 Issue #1 (commits `9d885d8 → 40cf581`).
- **What was originally observed**: `set_screen_mode` (`src/video/ula.cpp:147` pre-fix) computed `mode_bits = (port_val >> 3) & 0x07` per a deliberate jnext convention documented inline at lines 141-146 — contradicts VHDL `zxula.vhd:191` (`screen_mode_s <= i_port_ff_reg(2 downto 0)`).
- **User impact (was)**: real Timex programs writing the SCLD-spec mode bits to port_ff(2:0) silently fell into STANDARD mode forever; HI_RES was unreachable from real software, only from test fixtures using the buggy bits-5:3 convention.
- **Fix**: `mode_bits = port_val & 0x07` per VHDL. `Ula::set_shadow_screen_en` mask flipped `0x07` → `0xF8` so the gated bits are 2:0 (not 5:3). 25+ test fixtures + debugger CPU-panel mode-label decoder updated to the bits-2:0 convention. Plan-doc `doc/design/G104-HI-RES-512-CANONICAL-FRAMEBUFFER-PLAN.md` Section A.4 marks the convention drift resolved.
- **Test coverage**: full `test/ula/` + `test/ula/ula_integration_test.cpp` re-baselined; existing S5.x tests now exercise the right code paths.
- **Dependency for**: G108 (NR 0x69/0x22/0xC4 → port_ff_reg fan-out) is a prerequisite hop. Now that `set_screen_mode` is VHDL-correct, plumbing the fan-out from NR handlers becomes meaningful.

### G165. HI_RES display ink/paper used port_ff bits 2:0/5:3 independently instead of VHDL `border_clr_tmx` [closed]
- **Status: CLOSED 2026-05-03** by G179 Issue #2 (commits `822b10b → d23f306`). Complementary to G105 which closed the HI_RES *border* row encoding; G165 closes the HI_RES *display row* ink/paper derivation.
- **What was originally observed**: `Ula::render_display_line_hires` (`src/video/ula.cpp:935+` pre-fix) treated port_ff bits 2:0 as ink and bits 5:3 as paper, INDEPENDENTLY — contradicts VHDL `zxula.vhd:419, 426-427`. Per VHDL the entire HI_RES display attr_reg is loaded with `border_clr_tmx = "01" & ~port_ff(5:3) & port_ff(5:3)`: bit 6 = BRIGHT = 1, paper bits 5:3 = `~port_ff(5:3)`, ink bits 2:0 = `port_ff(5:3)`. So **ink = port_ff(5:3) (BRIGHT)**, **paper = ~port_ff(5:3) & 0x07 (BRIGHT)** — NOT independent fields.
- **User impact (was)**: Timex hi-res monochrome programs picking specific port_ff(5:3) paper colours rendered with wrong colours; the BRIGHT bit was always off; ink and paper could be set to arbitrary independent pairs that real hardware cannot produce.
- **Fix**: synthesize attr per VHDL: `0x40 | ((~paper_color & 7) << 3) | (paper_color & 7)`, where `paper_color = (screen_mode_reg_ >> 3) & 0x07`. HI_RES strip-border (left/right of a display row) now derives from the same `paper_argb` (paper cycle of `border_clr_tmx`) — port_fe is NOT routed to HI_RES borders per VHDL.
- **Test coverage**: S5.10 / S5.10b / S5.10c rewrites lock in: distinct paper_color stimuli; expected ink = bright(port_ff(5:3)); expected paper = bright(~port_ff(5:3) & 7).
- **Source ref**: surfaced during G104 Phase 2 reviewer pass; logged in `project_g104_closed_canonical_640.md` then promoted to G179.

### G166. `Renderer::ula_border_[]` per-pixel border-active flag never written [closed]
- **Status: CLOSED 2026-05-03** by G179 Issue #4 (commits `dbb5d94 → fececef`).
- **What was originally observed**: `Renderer::ula_border_[]` (`src/video/renderer.h:362`) is a per-pixel `std::array<bool, FB_WIDTH>` consumed by the compositor at `renderer.cpp:392` for the `border_exc` flag (NR 0x68 stencil + ULA-disabled paths). Pre-fix: zero-filled at frame start (`renderer.cpp:142` `std::fill_n(..., false)`) and never updated by any ULA renderer. Compositor always saw `false`.
- **User impact (was)**: latent — the `border_exc` consumer paths are themselves gated by G26 (FPGA-team Open Questions on NR 0x68 modes 011/100/101). Not visibly breaking any current demo, but blocking G26 from being meaningfully testable.
- **Fix**: `Ula::render_scanline` gained `bool* border_dst = nullptr` parameter, threaded through 4 internal renderers (`render_display_line` STANDARD/STANDARD_1, `render_display_line_hicolour`, `render_display_line_hires`, `render_border_line`). Border-fill loops (left strip [0..63], right strip [576..639], full top/bottom border row [0..639]) write `border_dst[x] = true`; display-area cells stay at the caller's pre-fill `false`. Renderer wires `ula_border_.data()`; debugger video panel passes nullptr.
- **VHDL ref**: `zxula.vhd:415` (`border_active <= i_phc(8) or border_active_v`), `:567` (`o_ula_border <= border_active`), consumed by `zxnext.vhd:7256, 7266, 7278` for the ULA-vs-sprite border exception in modes 011/100/101.
- **Test coverage**: S3.09 / S3.10 / S3.11 / S3.12 / S3.13 in `test/ula/ula_test.cpp` (display row border strips, top-border row, bottom-border row, nullptr default-arg back-compat, HI_COLOUR display row).
- **Source ref**: surfaced during G104 Phase 2 reviewer pass; logged in `project_g104_closed_canonical_640.md` then promoted to G179.

### G167. ULAnext / ULA+ encoder dispatch is NOT wired into HI_RES display path [closed]
- **Status: CLOSED 2026-05-03**.
- **What was originally observed**: VHDL `zxula.vhd:485-554` defines three encoder paths — std-ULA (`:543-553`), ULAnext (`:492-528`), ULA+ (`:531-541`). Per `zxula.vhd:426-427` the HI_RES display attr_reg is `border_clr_tmx & border_clr_tmx`, which then flows through whichever encoder is active per `ulanext_en` / `ulap_en`. The HI_RES border row went through encoder dispatch correctly (closed by G105) but `Ula::render_display_line_hires` only called the std-ULA encoder helpers (`std_ula_ink_pixel` / `std_ula_paper_pixel`) — it did NOT consult `ulanext_en_` / `ulap_en_` and route through `compute_ulanext_pixel` / the ULA+ encoder formula like the other renderer paths do.
- **User impact (was)**: a HI_RES program running under ULAnext or ULA+ palette mode (firmware-poked palette entries at `ula_pixel` slots that the std-ULA encoder doesn't reach) saw wrong colours in the display area only — the border (closed by G105) rendered correctly, but the inner 512-px display did not. Latent: no current demo exercises HI_RES with ULAnext/ULA+ enabled.
- **Fix**: `render_display_line_hires` now folds the same encoder dispatch as `render_display_line` / `render_display_line_hicolour` / `render_border_line`: ULAnext via `compute_ulanext_pixel(pixel_en, border, hires_attr)` for ink/paper/strip-border slots; ULA+ via the `(pg<<4) | (1<<3) | low3` low6 formula (sm2=1 forces ula_pixel(3)=1 in both ink and paper cycles); std-ULA fall-through preserves pre-G167 behaviour. Strip-border (left/right of a HI_RES display row) now consumes the encoder's border path instead of unconditionally re-using the std-ULA paper colour. `hires_attr` bit 7 = 0 by construction so the flash XOR (`zxula.vhd:470`) cannot fire — no flash gate needed.
- **VHDL ref**: `zxula.vhd:485-554` (encoder dispatch end-to-end), `zxula.vhd:419 + 426-427` (HI_RES `border_clr_tmx` synthesis), `zxula.vhd:531-541` (ULA+ encoder with sm2 forcing ula_pixel(3)=1 in HI_RES), `zxnext.vhd:6981` (palette read).
- **Test coverage**: S5.12 (HI_RES + ULAnext: format=0x07; distinct palette pokes at ink_idx=0x06 / paper_idx=0x89 / border_idx=0x81; negative gate via std-ULA fall-through) and S5.13 (HI_RES + ULA+: pg=1, ink_low6=0x1E / paper_low6=0x19; negative gate via std-ULA fall-through) in `test/ula/ula_test.cpp`.
- **Source ref**: surfaced during G179 Issue #2 reviewer pass (Reviewer B, 2026-05-03).

---

## B. End-user emulation experience — Audio, I/O & peripherals

### G29. Pi I2S real audio emulation (stub upgrade)
- **What**: `src/audio/i2s.{h,cpp}` only latches a 10-bit sample pair
  on demand; no host-driven sample stream during real emulation.
- **User impact**: Pi I2S audio software (rare; not used by
  NextZXOS) silent on the I2S contribution.
- **Source ref**: `TASK3-AUDIO-SKIP-REDUCTION-PLAN.md` backlog 1.
- **Proposed**: driver producing samples at 48 kHz / I2S clock; fed
  by `--i2s-input file.wav` or host capture.
- **Effort**: H.
- **Status (2026-05-03g)**: WONT (until `--i2s-input` driver lands). MX-30 in `audio_test` converted to `// WONT MX-30` comment. Branch `tier6-audio-cluster`.

### G30. AY GPIO ports (PORTA / PORTB)
- **What**: AY R14/R15 (port A/B I/O) emulation absent. AyChip stores
  reg values but no `port_a_i / port_b_i` injection / read fan-in.
- **User impact**: Vintage 128K software using AY as GPIO (keyboard
  mux, lightgun, MIDI, multifaces) silent.
- **Source ref**: `TASK3-AUDIO` backlog 2; AY-30..34 (5 G-skips).
- **Proposed**: `AyChip` GPIO surface mirroring `ym2149.vhd`; single
  dummy `IoBus` consumer in `Emulator`.
- **Effort**: M.
- **Status (2026-05-03g)**: WONT. AY GPIO unused by jnext target software. AY-30..AY-34 in `audio_test` converted to `// WONT` comments. Revisit when jnext adds vintage AY-as-GPIO peripheral support (Currah µSpeech, AY MIDI, AY-as-keyboard-mux). Branch `tier6-audio-cluster`.

### G31. DAC per-clock write-priority model (SD-09)
- **What**: Multiple Soundrive/Covox aliases targeting same DAC
  channel within one frame collapse to last-write-wins; VHDL has
  per-CLK_28 if/elsif priority.
- **Effort**: M (re-evaluate when scanline-level audio refactor lands).
- **Status (2026-05-03g)**: WONT. SD-09 in `audio_test` converted to `// WONT SD-09` comment. Revisit when scanline-level audio refactor lands. Branch `tier6-audio-cluster`.

### G32. DAC continuous-buzz playback artefact
- **What**: Soundrive DAC demo produces continuous low-frequency
  buzz alongside expected tone, even with DI + 28 MHz + pure-asm
  timing. Reproduces in ZEsarUX.
- **Source ref**: `EMULATOR-DESIGN-PLAN` §11; `TODO.md`.
- **Coverage today**: no automated audio-spectrum regression — buzz
  invisible to tests.
- **Proposed**: profile mixer DAC sampling cadence vs scanline vs
  CLK_28; compare with FUSE+CSpect; consider per-sample (44.1 kHz)
  DAC tap with linear interpolation; add spectral-FFT regression.
- **Effort**: H.

### G33. Tape SAVE (write to TAP / TZX / WAV)
- **What**: ROM SAVE-BYTES trap + EAR/MIC → file write entirely
  absent. `tap_loader` / `tzx_loader` are read-only. Verified — no
  matches for `SAVE_BYTES` / `tape_saver` / `TapSaver` in `src/`.
- **User impact**: cannot save BASIC programs / data — major gap vs
  every legacy emulator.
- **Proposed**: Phase 1 trap-based SAVE→TAP (ROM 0x04C2); Phase 2
  analogue MIC→TZX 0x10/0x11; Phase 3 WAV writer.
- **Effort**: M.
- **Status: Phase 1 CLOSED (Task 57, 2026-07-14)**. `TapSaver`
  (`src/core/tap_saver.{h,cpp}`) landed: SA-BYTES ROM trap at `0x04C2`
  (entry bytes verified against the extracted 48.rom; A=flag, IX=start,
  DE=length per the Complete Spectrum ROM Disassembly; same gating +
  pop-return-address exit as the LD-BYTES fast-load trap), armed via
  `--tape-save FILE` (append semantics; inactive without the flag).
  Blocks are standard TAP (LE length = payload+2, flag, payload, XOR
  checksum). Unblocked `mmu_test` BOOT-TAPESAVE-01..03 (byte-array
  fixtures + TapLoader::parse_blocks round-trip). Foreign-reader
  verified: a stub-driven `SAVE` produced a TAP that real FUSE 1.6.0
  auto-loaded and ran ("Program: SAVETEST" / "0 OK, 10:1").
  **Scope caveat**: the trap captures ONLY saves routed through the 48K
  ROM SA-BYTES routine (48K BASIC; 128K/+3 when they page 48K BASIC in
  for tape ops). Custom savers that bit-bang MIC directly are Phase 2
  territory. The independent review caught a false-fire class — a plain
  PC gate triggered 10× during an ordinary NextZXOS boot (other ROMs
  execute at 0x04C2 too), corrupting the file and the boot — fixed by
  the ROM-identity signature gate (`TapSaver::sa_bytes_rom_present`),
  covered by `mmu_integration_test` MMU-G33-TRAP-01..03 and the
  `tape-save-boot-func` regression row (NextZXOS boot with --tape-save
  armed: pixel-identical boot, zero blocks).
  **STILL OPEN**: Phase 2 (analogue MIC→TZX 0x10/0x11) and Phase 3
  (WAV writer); GUI menu integration for `--tape-save` is a follow-up.

### G34. `.z80` snapshot loader
- **What**: SNA + SZX supported; `.z80` (most-popular legacy
  snapshot) absent.
- **Source ref**: `TODO.md` "Z80 file format loading";
  `EMULATOR-DESIGN-PLAN.md` Phase 11.
- **Proposed**: `Z80Loader` supporting V1/V2/V3 headers, compressed
  / uncompressed pages, full register restore + 128K paging.
- **Effort**: M.
- **Status: CLOSED (Task 13b)**. `Z80Loader` (`src/core/z80_loader.{h,cpp}`)
  landed: v1 (30-byte header, RLE with `00 ED ED 00` end marker or raw
  49152-byte dump), v2 (23-byte extended header) and v3 (54/55-byte
  extended header), 48K and 128K page-number tables, full register
  restore (incl. AF', R bit-7 quirk, IM, IFF1/IFF2), border, and 128K
  paging (port 0x7FFD from header byte 0x23). Wired into `--load`/bare-arg
  CLI dispatch, the Qt file dialog, and `Emulator::load_z80()`. Unblocked
  `mmu_test` BOOT-Z80-01..04 (`test/mmu/mmu_test.cpp`). One documented
  ambiguity: hardware-mode byte value 3 (offset 34) is disputed between
  v2/v3 across sources; jnext treats it as 48K-class in both (see
  `Z80Loader::load_from_buffer()` comment) since neither MGT nor SamRam
  is emulated. `.z80` **saving** is out of scope (not requested; no other
  loader in this codebase has a matching saver either, e.g. NEX/RZX-play).

### G35. Snapshot save (.sna out / .szx out / .nex out) wired [partial]
- **Status: PARTIAL 2026-05-04** (Tier B). `.sna` save WIRED to GUI:
  File → Save S&napshot... (Ctrl+Shift+S) opens QFileDialog, calls
  `SnaSaver::save(*emulator_)`, writes via QFile. Auto-appends `.sna` if
  user omits it. Error dialogs on open-fail / short-write. BOOT-SNAPSAVE-01
  + BOOT-SNAPSAVE-04 closed in `mmu_test` (closed via `// CLOSED 2026-05-04`
  comment block per BOOT-FDC precedent — mmu_test intentionally avoids full
  Emulator construction). **STILL OPEN**: `.szx` saver (BOOT-SNAPSAVE-02)
  and `.nex` saver (BOOT-SNAPSAVE-03) — neither writer exists yet.
- **Proposed (remainder)**: add `SzxSaver` mirroring `szx_loader.cpp`;
  optional `.nex` saver. Both queued under Task 13b.
- **Effort**: M (remainder).

### G36. TZX Direct-Recording (DeciLoad 0x15) — CLOSED (Task 57, 2026-07-14)
- **What**: TZX 0x15 blocks with DeciLoad 12k8 (77 T-states/sample)
  failed in real-time mode.
- **Root cause (two independent bugs, both measured)**:
  1. **Frame-relative tape clock** — `begin_new_frame()` zeroes the
     FUSE `tstates` counter every frame (f3665f25, for the contention
     hc/vc derivation), but that same counter was fed to ZOT's
     absolute-timeline `edge_clock`. The moment an edge landed past a
     frame boundary, `cpu_clocks >= edge_clock` could never become
     true again — ALL real-time TZX playback froze (not just 0x15;
     nothing covered `--tape-realtime`, so it broke silently).
     Fixed by `Emulator::monotonic_tstates()` (completed-frames base +
     live FUSE counter, still mid-instruction-live).
  2. **ZOT pause swallowed the block-terminating edge** — entering
     `TZX_PHASE_PAUSE` forced `level = 0` immediately; when a block's
     final pulse ended low, the terminating edge (toggle to 1) was
     overwritten and the ROM timed out on the last bit of EVERY block
     (measured: LD-BYTES error at exact end-of-data). Fixed by holding
     the final level ~1 ms, then dropping low — an empirically-derived
     heuristic validated by real loads (NOT libspectrum behaviour;
     libspectrum treats the pause start as an ordinary toggle edge).
     Plus: DIRECT samples are SET, not toggled — the caller-side
     toggle no longer inverts a 0x15 block's final sample level.
- **Note**: with both fixes, the 0x15 decode itself needed no change —
  ZOT's per-sample DIRECT handler was already faithful.
  The doc's "FUSE handles the same file" was NOT reproducible headless
  (FUSE 1.6.0 enters the DeciLoad loader but decodes zero bytes in
  110 s, traps on or off); jnext now loads Xevious end-to-end.
- **Coverage**: `xevious-deciload` screenshot row (full game to menu,
  `--tape-realtime`, deterministic); mmu_test BOOT-DECI-01/02.
- **Source ref**: `doc/issues/deciload-tzx/DECILOAD-TZX-LOADING.md`.

### G37. WAV DeciLoad real-time loading — CLOSED (Task 57, 2026-07-14)
- **What**: Same DeciLoad 12k8 turbo class failed when sourced from a
  `.wav`.
- **Root cause**: shared G36 cause #1 (frame-relative clock froze all
  WAV playback: `current <= start → 0` forever) PLUS a WAV-specific
  one: stepwise per-sample thresholding quantised every edge to the
  79.4 T sample grid (44.1 kHz); DeciLoad's short/long pulse classes
  are ~60 T apart, and ~25% of the Dizzy WAV's long pulses quantised
  into the wrong class (loader `RST 0`). Fixed by linear sub-sample
  interpolation of the threshold crossing (8.8 fixed point) — the
  analog crossing instant the hardware EAR Schmitt trigger sees.
  Measured: quantised longs smeared to 476–556 T; interpolated widths
  cluster cleanly at ~490–590 T. Dizzy WAV loads to the title screen.
- **Coverage**: mmu_test BOOT-DECI-03/04 (04 is the interpolation
  discriminator: a probe between the interpolated crossing and the
  grid edge).

### G38. DSK / +3 disk image loading + uPD765 FDC
- **What**: No floppy emulation. +3 has built-in FDC used by
  NextZXOS for tape→disk software.
- **Source ref**: `TODO.md` "DSK file format loading";
  `EMULATOR-DESIGN-PLAN.md` Phase 11.
- **Proposed**: integrate permissive-license uPD765 model
  (e.g. FUSE's wd_fdc) + DSK reader; gate behind `--machine plus3`.
- **Effort**: H.

### G39. ESP-01 / Wi-Fi UART bridge
- **What**: UART 0 wired to "ESP" but no AT-command parser / TCP
  socket bridge.
- **User impact**: NextZXOS-side networking apps + multiplayer Z80
  software can't talk to anything.
- **Proposed**: stub AT command set + TCP socket bridge; optional
  `--esp-bridge HOST:PORT`.
- **Effort**: H.
- **Status (2026-05-03g)**: WONT (until `--esp-bridge` feature added). ESP-01..ESP-04 in `uart_test` converted to `// WONT` comments. Branch `tier6-uart-cluster`.

### G40. SD card command coverage gaps
- **What**: `SdCardDevice` supports CMD0/1/8/12/17/18/24/55/58 +
  ACMD41. Missing: CMD9 (SEND_CSD), CMD10 (SEND_CID), CMD13 (STATUS),
  CMD16 (BLOCKLEN), CMD23 (BLOCK_COUNT), CMD25 (WRITE_MULTIPLE),
  ACMD22/23/51.
- **Source ref**: `src/peripheral/sd_card.cpp:259-285`.
- **Proposed**: read-side first (CMD9/10/13/16/23); defer multi-
  block write (CMD25) until a writer client appears.
- **Effort**: M.

### G41. MMC card support (vs SDHC only)
- **What**: SD model targets SDHC byte-addressed; MMC uses CMD1 +
  sector addressing only.
- **Effort**: L.

### G42. Joystick / gamepad host wiring (Kempston/Sinclair/MD pads)
- **What**: `Joystick`, `KempstonMouse`, `Md6ConnectorX2`,
  `MembraneStick` classes exist + protocol-tested. Verified — no
  `Joystick::inject`, `set_joy_left`, `set_joy_right` calls in
  `src/platform/sdl_input.cpp` or `src/gui/main_window.cpp`. SDL
  `SDL_INIT_GAMECONTROLLER` IS initialised but no events dispatched
  to `Joystick`.
- **User impact**: gamepad / USB joystick unusable — keyboard-only.
- **Proposed**: SDL gamepad event poll + dispatch; Qt Gamepad
  fallback; GUI menu Joystick→Mode (Kempston/Sinclair1/2/Cursor/MD/
  UserDef); save mapping in `QSettings`. Includes wiring
  `Md6ConnectorX2::set_raw_left/right()` and `MembraneStick`
  composers (production wiring of a test-ready surface).
- **Effort**: M.

### G43. Kempston Mouse host wiring
- **What**: `KempstonMouse` class with X/Y/buttons/wheel + DPI
  exists; no SDL `SDL_MOUSEMOTION` / `_WHEEL` / `_BUTTON*`
  translation.
- **User impact**: Art Studio Next, mouse demos, GUI ports cannot
  drive cursor.
- **Proposed**: `Ctrl+M` toggles capture; feed deltas into
  `KempstonMouse::inject_delta`; DPI from NR 0x0A.
- **Effort**: M.

### G44. Keyboard issue-2 EAR/MIC composition
- **What**: FE-04 row F-skipped — no analogue EAR/MIC plumbing
  distinct from issue-3.
- **User impact**: issue-2 16K tape-loading detection edge; rare.
- **Proposed**: `set_machine_issue(int)` on Beeper + Keyboard
  composer; only active for `--machine 48k --issue 2`.
- **Effort**: L.

### G45. Expansion bus / cartridge framework (FE-05 + ROMCS)
- **What**: FE-05 F-skipped — no expansion-bus model. VHDL composes
  port 0xFE bit 5 + ROMCS via expansion-bus tap.
- **User impact**: Interface 1/2, external Multiface, Currah µSpeech,
  ZX Printer, Beta Disk all absent.
- **Proposed**: defer; v1.2+ feature behind `--cartridge FILE.{rom,
  kit}`.
- **Effort**: H.
- **Status (2026-05-03f)**: WONT (out of scope until cartridge framework lands). NR 0x86–0x89 expansion-bus mask-AND terms are inert by design in jnext: no expansion-bus aggregator and no cartridge model (Interface 1/2, Multiface, Currah µSpeech, ZX Printer, Beta Disk all absent). Tier-6 closure: `test/port/port_test.cpp` rows EXPBUS-AND-01..04 converted from `skip()` to `// WONT` comments per `feedback_wont_taxonomy.md`. Revisit when `--cartridge FILE.{rom,kit}` feature is added (G45 v1.2+). Branch `tier6-port-wont`.

### G46. NextZXOS boot ladder (firmware-faithful + bypass) [merged]
- **What**: **Single multi-blocker root entry.** NextZXOS boot via
  real `TBBLUE.FW` reaches `enNextZX.rom` and stalls. Discrete
  blockers:
  - **(a)** RETI/RETN-alias band-aid in
    `src/core/emulator.cpp:251-264` (KNOWN DIVERGENCE) — VHDL
    `divmmc.vhd:131` "delayed-off" clear path is the proper fix
    [orig C01].
  - **(b)** RAM-test outer-loop infinite-loop hang — firmware cycles
    112 RAM banks via NR 0x56, ~208 passes/bank in 15 s, never
    exits. Possibly NR 0x1E/0x1F (active video line) timing skew
    [orig C02].
  - **(c)** Missing logo + earlier loader log lines (real Next shows
    4-entry log + diagonal colour bars; jnext shows enNextZX.rom
    only). Diagnosis: rendering gap, not peripheral [orig C03].
- **User impact**: NextZXOS does not reach BASIC / dot-command
  shell; cannot run NextZXOS-native software.
- **Source ref**: `NEXTZXOS-BOOT-INVESTIGATION.md` (full chrono);
  `TODO.md` "NextZXOS Boot (v1.1)".
- **Coverage today**: no end-to-end boot regression test.
- **Proposed**: parallel (a) firmware-faithful — model
  `divmmc.vhd:131` properly + remove RETN-alias band-aid + RAM-loop
  RE; (b) **G59 bypass** ships value sooner.
- **G153 plausibility (Task 6 / NEW-NMI-2)**: Reviewer note —
  NR 0x02 reset_type FSM affects SPI-Flash-CS at first power-on
  (`port_e7_reg <= 0x7F` requires `reset_type(2)='1' OR
  config_mode='1'`). `--boot-rom roms/nextboot.rom` flow bypasses
  the FPGA-Flash path entirely, so plausibility is **moderate, not
  "likely"**. Schedule a port-0xE7-write trace at the G46 stall
  window before promoting G153 to G46 contributor.
- **NEW-I2C-2 link explicitly DROPPED**: Task 6 Serial reviewer
  confirmed via TBBlue firmware source + boot investigation doc that
  24LCxx EEPROM-NACK does NOT contribute to G46(b) RAM-test loop.
  The corresponding new gap (G139) is scheduled as standalone I2C
  peripheral coverage, NOT as a G46 unblocker.
- **Effort**: H.
- **Status (2026-05-03h) — G46(a) CLOSED**: Proper VHDL `divmmc.vhd:131` delayed-off automap_held clear via 1-M1-cycle delay register inside `DivMmc` (`retn_pending_clear_`), driven from `Im2Controller::retn_seen_this_cycle()` via new `DivMmc::on_m1_retn_delay(bool)` callback fired on EVERY M1. The pre-G87 RETN-alias band-aid was already retired by G87 commit `31e2720` earlier today; this work completes G46(a) by adding the proper delayed-off shape on top of the now-correct ED 45 detection. Tests DM-RETN-PROPER-01 (positive: full Im2 FSM ED 45 → automap_held clears one M1 later) + DM-RETN-PROPER-02 (negative: 8 alias cases incl. standalone 0x45 + ED 4D/55/5D/65/6D/75/7D, automap_held survives all). Branch `divmmc-retn-proper` (`94d4d0a`). G46(b) RAM-test loop hang and G46(c) missing logo are SEPARATE blockers — NextZXOS boot still won't complete.
- **Status (2026-05-04) — G46(c) CLOSED**: Cold-boot DivMMC automap was firing at PC=0x0000 because `DivMmc::set_enabled(bool)` flipped both `port_io_enable_` AND `nr_0a_4_enable_` from the `--divmmc-rom` startup path; VHDL `zxnext.vhd:1126` defaults `nr_0a_divmmc_automap_en` to '0' (only firmware writing NR 0x0A bit 4 should set it). With NR 0xB8 default 0x83 (RST 0 entry enabled, all-delayed timing), automap was active for the whole boot ROM phase — which the G87 alias-firing band-aid had been masking by clearing `automap_held` whenever tbblue.fw normal code happened to execute an undocumented RETN-alias byte. Fix: `set_enabled(true)` now flips only `port_io_enable_`. Discriminative regression test `NA-01b` added to divmmc_test (probes cold-boot equivalent state from `set_enabled(true)` alone, asserts no automap activates). Tbblue.fw splash logo now renders cleanly at f100 (3 colors, big blue "TBBlue"), spacebar prompt clean at f200, four-line ROM loader log clean at f250-f275. Boot now progresses past splash into the original G46(b) RAM-test loop. Commits `e9cbfd2` (`divmmc-enable-faithful-defaults`) + `d79d24b` (post-merge `nmi_test` fixture restore). divmmc_test 110/110, nmi_test 56/56, nmi_integration_test 9/9, full unit suite 33/0/0.
- **Status (2026-05-04) — G46(b) shape unchanged**: After G46(c) landed, post-soft-reset boot reaches enNextZX.rom RAM-test pass-1 loop at PC=0x0139-0x013D (NEXTREG 0x56,A then NEXTREG 0x57,A — bank-switching slot 6/7 RAM banks for memory test). Trace shows firmware DOES reach pass 2 (PC 0x0196-0x01D7) and post-RAM-test init (0x0207-0x0217, 0x5B48 in slot 2) over a few seconds wall clock, but never reaches BASIC welcome screen. Same exit-predicate gap as the journal's 2026-04-25 analysis: "writes NR 0x56 with every even value from 0x00 to 0xDE, then wraps back to 0x00 and restarts ≈208 passes per bank in 15 s". Hypothesised NR 0x1E/0x1F (active video line) timing skew or peripheral status register that firmware polls but we don't update correctly. **Next investigation step**: RE the exit condition of the RAM-test outer loop in `enNextZX.rom` 0x0130-0x016C and 0x018E-0x01C9, identify which polled value our emulator returns wrong.

### G47. NextZXOS post-boot regression / dot-command surface [closed]
- **Status: CLOSED 2026-07-14** (Task 57). Screenshot regression row
  `boot-nextzxos-dotls` (`test/00regression/regression_tests.conf`)
  boots NextZXOS natively, drives the main menu with injected
  keypresses (SPACE → DOWN → ENTER into "Command Line"), types `.ls`
  + ENTER and pins the SD-root directory listing at frame 800 (settled
  on the `scroll?` prompt; listing verified against the image's FAT32
  root via mtools — 22 files, matching names/sizes; capture
  byte-identical across repeated runs). Enabler:
  `--delayed-keypress-frames` key names extended to named cursors
  (up/down/left/right = CAPS SHIFT + 7/6/5/8), punctuation (`.` `,`
  `;` `:` via their SYMBOL SHIFT compounds) and explicit
  `sym+<char>` / `caps+<char>` compounds; unknown key names now fail
  loudly at startup instead of being silently dropped at injection.
  nmi_test BOOT-DOT-01 converted from skip() to a
  COVERED-AT-regression-tier comment (nmi_test 58 → 57 rows).
- **What was originally observed**: Once boot lands (G46), no
  automated test or CLI surface for "load NextZXOS, run dot command,
  screenshot result".
- **Source ref**: `NEXTZXOS-BOOT-INVESTIGATION.md`; bypass plan
  §"Acceptance".
- **Dependencies**: G46 (boot landed 2026-07-10).
- **Effort**: L.

### G48. Multiface peripheral (Task 8) [closed]
- **Status: CLOSED 2026-05-04** via Task 8 Wave 1 (5 mini-merges:
  B1 core class + B2 port dispatch + B3 +3 readback mux + E MMU overlay
  + F-gate DivMMC retn AND-NOT mf_is_active). All 7 MF-G48-* SKIPs in
  `nmi_test` closed (-01 port table, -02/03/04 state machine, -05/07
  +3 readback mux, -06 DivMMC retn gate). New `Multiface` class at
  `src/peripheral/multiface.{h,cpp}` mirrors VHDL `multiface.vhd` 1-197
  end-to-end: 4 internal flip-flops (nmi_active / invisible / mf_enable /
  port_io_dly), mode dispatch (MF1/128/+3 from NR 0x0A b7:6), 8K ROM (loaded
  from SD `/MACHINES/NEXT/enNextMf.rom`) + 8K RAM. NmiSource live MF feedback
  replaces the always-false stubs. F9 hotkey + new "NMI" toolbar button drive
  `multiface_.button_press()` alongside `nmi_source_.strobe_mf_button()`.
  multiface_test 48/48/0/0 (MF-CORE 12 + MF-PORT 16 + MF-MUX 10 + MF-OVL 10).
  Regression 33/0/0; firmware boot MD5 unchanged. Manual F9-press smoke test
  (paging in MF menu during NextZXOS boot) deferred — unit-test live-wiring
  coverage (MF-INT-01/02 + MF-OVL-09 priority test) compensates.
- **What was originally observed**: Task 8 fully scoped, unstarted.
  `src/peripheral/multiface.{h,cpp}` did not exist.
  `NmiSource::set_mf_is_active(false)` / `set_mf_nmi_hold(false)`
  were stubs.
- **Blocks**: 8 DivMMC NM-class rows + Copper ARB-06 + Port-Dispatch
  NR82-02; `enNextMf.rom` is on SD but never paged in.
- **User impact**: no NMI freeze/cheat menu via F5 or NR 0x02
  software trigger.
- **Source ref**: `TASK-8-MULTIFACE-PLAN.md`.
- **Proposed**: execute Task 8 plan §5 (Branches B/E/F); add Edit→NMI
  GUI affordance + F-key.
- **Implementation surface (Task 6 audit)** — `multiface.vhd` 197-line
  peripheral entity + `zxnext.vhd:2611-2616, 2730-2733, 4277-4322`
  integration. Concretely: mode-decoded port table per
  `nr_0a_mf_type` (MF +3=00 ports 0x3F/0xBF; MF 128=01/10 ports
  0xBF/0x3F; MF 48=11 ports 0x9F/0x1F); `mf_a_0066`/`mf_is_active`/
  `mf_mem_en`/`mf_port_en` signals; `port_io_dly` edge detector
  (`multiface.vhd:122-131`); INVISIBLE FF (`:152-163`); MF +3-only
  port 0x1FFD/0x7FFD readback mux on `cpu_a(15:12)`
  (`zxnext.vhd:4310-4322`).
- **NR 0x0A bits 7:6 plumbing (Task 6 / NEW-MF-2)** — `nr_0a_mf_type
  <= nr_wr_dat(7 downto 6)` (`zxnext.vhd:5193`); jnext
  `emulator.cpp:447-451` does not forward; comment at line 446 admits
  the gap. Land in the same patch as G123 (NR 0x0A bit 4 plumbing).
- **NR 0x0A bit 4 plumbing** — covered by G123 separately; land
  together since both are NR 0x0A handler extensions.
- **DivMMC RETN-seen gating (Task 6 / NEW-DM-3)** — VHDL
  `zxnext.vhd:4111`: `divmmc_retn_seen <= z80_retn_seen_28 and not
  mf_is_active`. jnext `emulator.cpp:275-283` fires
  `divmmc_.on_retn()` unconditionally. Once Multiface lands, a
  Multiface NMI handler issuing RETN would spuriously clear DivMMC's
  automap_held / button_nmi latches — track as **Day-1 invariant**
  for G48 closure.
- **port 0xDFFD bit 6 storage (Task 6 / G148 sibling)** — Multiface
  readback at `zxnext.vhd:4314` consumes `port_dffd_reg_6` which
  jnext discards (`mmu.cpp:332`). G148 is the MMU-side fix; surface
  it here so Multiface readback is correct on first landing.
- **+3 readback decode on cpu_a(15:12)** —
  `zxnext.vhd:4310-4322` `mf_port_dat` mux conditional on `mf_mode`;
  jnext currently has no port-prefix-decoded MF read path.
- **Effort**: M.

### G110. audio_mixer.exc_i speaker-exclusive gate not enforced
- **What**: VHDL `audio/audio_mixer.vhd:80-81` `ear/mic <= … when … and exc_i='0'`. `zxnext.vhd:6504/6514` ties `beep_spkr_excl` (NR 0x06 b6 AND NR 0x08 b4) into `exc_i`. jnext `mixer.cpp:28-29` adds EAR/MIC unconditionally; `Emulator::beep_spkr_excl()` exposed but Mixer never reads it.
- **User impact**: in speaker-only mode (NR 0x06 b6 + NR 0x08 b4 set) line-out doubles vs hardware.
- **Source ref**: Wave-1 audio (NEW-AUD-1); reviewer APPROVE 6/6.
- **Coverage today**: BP-13 / MX-22 verify the signal at composite level only.
- **Dependencies**: const-ref pattern matching `Mixer::set_i2s_source()`.
- **Effort**: L.
- **Status (2026-05-03f)**: CLOSED. `Mixer::set_exc_i(bool)` setter added; `generate_sample()` zeroes `ear`/`mic`/`tape_ear` when `exc_i_=true`. Wired from NR 0x06 + NR 0x08 write_handler tails AND post-reset in `Emulator::init()` (after the underlying state members are reset to false). Test MX-23 closed (PASS). Branch `g110-mixer-exci` (`938e0da`), merged as `dc7136c`.

### G111. DAC channels not held at 0x80 when nr_08_dac_en=0
- **What**: VHDL `audio/soundrive.vhd:69-78`+`zxnext.vhd:6436` (`reset_i => reset or not nr_08_dac_en`) — DAC channels latch to 0x80 while disabled. jnext `emulator.cpp:1674` only sets `dac_enabled_` flag; per-port handlers gate writes but never reset existing values.
- **User impact**: DAC-disable leaves residual non-silent level instead of falling to silence.
- **Source ref**: Wave-1 audio (NEW-AUD-2); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: literal `dac_.reset()` on 1→0 transition.
- **Effort**: L.
- **Status (2026-05-03f)**: CLOSED. NR 0x08 write_handler captures pre-assignment `dac_enabled_`, calls `dac_.reset()` on `was_enabled && !new_enabled` (the 1→0 edge only). `Dac::reset()` already sets all 4 channels to 0x80 per `dac.cpp:6-12`. Test SD-19 closed (PASS). Branch `g111-dac-reset` (`2f380d0`), merged as `d3ba51b`.

### G112. NR 0x2C/0x2D/0x2E read-back exposes Pi I2S input
- **What**: VHDL `zxnext.vhd:6006-6015` — reads return `pi_audio_L/R(9 downto 2)` and latch the low 2 bits into nr_2d_i2s_sample. jnext `emulator.cpp:1722-1739` wires only write handlers (DAC mirrors); reads return regs_[] noise.
- **User impact**: Z80 polling NR 0x2C/2E to capture I2S samples reads garbage.
- **Source ref**: Wave-1 audio (NEW-AUD-3); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: distinct from G29 (source-side stub).
- **Effort**: L.
- **Status (2026-05-03f)**: CLOSED. New `Emulator::nr_2d_i2s_sample_` shadow latch (uint8_t, save-state appended, EOF-tolerant for older saves). NR 0x2C / NR 0x2E read handlers return `i2s_.left()/right() >> 2` and latch low 2 bits pre-shifted into the byte's [7:6]. NR 0x2D read returns the latch raw (already pre-shifted). Tests NR-33 + NR-34 closed (PASS). Branch `g112-nr2c-2d-2e-reads` (`280d4ce`), merged as `f7e9be1`.

### G113. NR 0xA2 Pi I2S control register completely unwired
- **What**: VHDL `zxnext.vhd:1242,2283-2290,5564,6192` — NR 0xA2 stores 8 bits with bit fan-out (enL/enR/inout/muteL/muteR/ear). `grep` over `src/` returns no NR 0xA2 wiring.
- **User impact**: mute/direction/channel-enable bits never gate the Mixer's I2S contribution; readback returns 0.
- **Source ref**: Wave-1 audio (NEW-AUD-4); reviewer APPROVE with cross-ref to G73.
- **Coverage today**: none.
- **Dependencies**: standalone fix; G73 broader runtime.
- **Effort**: L.
- **Status (2026-05-03f)**: CLOSED — landed in single commit with G73 (they collapse: VHDL gating happens upstream of audio_mixer at `pi_audio_L/R` derivation). I2s class now stores `nr_a2_ctl_` byte and exposes gated `pi_audio_L/R()` per `zxnext.vhd:2358-2359` (en/mute/ear/cross-channel mux). NR 0xA2 write/read handlers in `Emulator`; read mask `(c & 0xDD) | 0x02` (b5=0, b1=1 fixed). NR 0x2C/0x2E reads + Mixer now consume the gated value. Tests NR-40+NR-41+NR-42+NR-43 all closed (PASS). Branch `g113-g73-nra2-i2s-gate` (`043192a`).

### G114. NR 0x84 DAC-port-pair enables (5 of 7 bits) not enforced
- **What**: VHDL `zxnext.vhd:2429-2435` 7-bit DAC-port-pair enable mask. jnext `emulator.cpp:1500-1567` honours bits 0/2/5 only; bits 1/3/4/6/7 ignored.
- **User impact**: NR 0x84 port-pair masks have no effect; DAC writes hit even when masked.
- **Source ref**: Wave-1 audio (NEW-AUD-5); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: orthogonal to G31 (per-clock priority).
- **Effort**: L.
- **Status (2026-05-03g)**: CLOSED. All 7 NR 0x84 bits now gate per VHDL `zxnext.vhd:2429-2435`: b0 ay (existing), b1 SD1 (1F/0F/4F/5F), b2 SD2 (F1/F3/F9/FB), b3 Profi (3F/5F), b4 Covox (0F/4F), b5 mono FB (existing), b6 GS (B3), b7 SpecDrum write (DF). Bonus catch: existing test stub had b1→SD2 mislabeled; corrected to b1→SD1 per VHDL. Tests IO-13..IO-17 closed (PASS). Branch `tier6-audio-cluster`.

### G115. TurboSound::reset() over-clears NR 0x08/0x09 state on AY reset
- **What**: VHDL `audio/turbosound.vhd:118-138` synchronous reset clears only `ay_select`/`pan`; enabled/stereo_mode/mono_mode are external NR-driven inputs. jnext `turbosound.cpp:10-20` zeroes all five fields. NR 0x06 psg_mode=11 path calls `turbosound_.reset()` unconditionally.
- **User impact**: psg_mode=11 toggle loses NR 0x08 b1 (turbosound_en), b5 (ABC/ACB), and NR 0x09 mono settings.
- **Source ref**: Wave-1 audio (NEW-AUD-6); reviewer APPROVE.
- **Coverage today**: none; recommend assertion test.
- **Dependencies**: split TurboSound::reset() into full vs ay-only.
- **Effort**: L.
- **Status (2026-05-03g)**: CLOSED. `TurboSound::reset_ay_only()` added to clear only `ay_select`/`pan` (matching VHDL `:118-138` synchronous-reset clause); full `reset()` still used by hard-reset path. NR 0x06 psg_mode=11 callsite now uses `reset_ay_only()`. Tests TS-60+TS-61 closed (PASS). Branch `tier6-audio-cluster`.

### G126. NR 0x05 mode change does not propagate to MembraneStick
- **What**: VHDL `membrane_stick.vhd:117-149` joy_type drives keymap-region selector. jnext `emulator.cpp:456-458` forwards NR 0x05 to `joystick_.set_nr_05(v)` only; `MembraneStick::set_mode()` never called from production. `joystick.cpp:26-50` has no MembraneStick reference.
- **User impact**: switching joy modes via NR 0x05 doesn't redirect membrane fold; joy0 inputs vanish from membrane while joy1 keeps default mapping.
- **Source ref**: Wave-2 input (NEW-JOY-1); reviewer APPROVE — High priority.
- **Coverage today**: tests use `MembraneStick::set_mode()` directly.
- **Dependencies**: internal cross-link, distinct from G42 host wiring.
- **Effort**: L.

### G127. NR 0x05 User-Defined + NR 0x28-0x2B joymap programming absent
- **What**: VHDL `zxnext.vhd:5157` `"111"` user-defined; `:6294-6324` NR 0x28-0x2B keymap_sel + addr + data write process; `membrane_stick.vhd:172-183` SDP-RAM for User-Defined keymap loaded from `keyjoy_64_6.coe`. jnext: no NR 0x28/0x29/0x2A/0x2B handlers; `membrane_stick.cpp:101-104` flags "111" as no-op.
- **User impact**: NextZXOS Joystick Calibration tool / homebrew custom remap silently has no effect.
- **Source ref**: Wave-2 input (NEW-JOY-2); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: distinct from G64 (PS/2 keymap WONT).
- **Effort**: M (SDP-RAM analogue + COE → C++ converter risk).

### G128. port 0x37 missing NR 0x82 bit-7 io_en gate
- **What**: VHDL `zxnext.vhd:2408,2675` gates port 0x37 on `port_37_io_en <= internal_port_enable(7)`. jnext `emulator.cpp:1877-1879` registers handler with no `cached(0x82) & 0x80` check; mirror of 0x1F at `:1871-1876`.
- **User impact**: NR 0x82 b7 clear still returns joystick byte on port 0x37.
- **Source ref**: Wave-2 input (NEW-JOY-3); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: one-line gate.
- **Effort**: L.

### G129. port_1f_hw_en / port_37_hw_en mode-conditional decode missing
- **What**: VHDL `zxnext.vhd:2454-2455,2674-2675` — `port_1f_hw_en <= joyL_1f_en or joyR_1f_en` only when joy mode is Kempston1/MD3Left. jnext `joystick.cpp:99-103` documents floating-bus headline but the gate is bit-6 io_en, not mode-conditional hw_en.
- **User impact**: port 0x1F when both joys are Sinclair2/Cursor returns 0x00 (no-buttons Kempston) instead of floating bus 0xFF; "Is Kempston attached?" probes get the WRONG answer.
- **Source ref**: Wave-2 input (NEW-JOY-4); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: distinct from G42.
- **Effort**: L.

### G130. Kempston-mouse port_1f alias (Soundrive DAC override) missing
- **What**: VHDL `zxnext.vhd:2674` enables the port_1f path also via port_df_lsb when `port_dac_mono_AD_df_io_en=1 AND port_mouse_io_en=0`. jnext `emulator.cpp:1563-1567` registers the 0xDF read but returns 0x00 unconditionally.
- **User impact**: Pentagon/ATM Soundrive 1.05 reads of 0xDF (Kempston joy when mouse disabled) return 0x00.
- **Source ref**: Wave-2 input (NEW-MS-1); reviewer APPROVE WITH NIT (also gated on Kempston1 mode).
- **Coverage today**: none.
- **Dependencies**: niche.
- **Effort**: L.

### G133. Keyboard tick_scan + cancel_extended_entries not driven from production
- **What**: VHDL `membrane.vhd:178-191` — matrix_state_ex_0/1 advance every membrane scan-cycle. jnext `keyboard.cpp:312/334` defines `tick_scan()` / `cancel_extended_entries()` but production `emulator.cpp` does not invoke them.
- **User impact**: 1-scan shift hysteresis edge cases differ; cancel-extended-entries hook unreachable until G48 lands.
- **Source ref**: Wave-2 input (NEW-KB-3); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: tick call per frame.
- **Effort**: L.

### G134. UART RX request-mask asymmetry not modelled
- **What**: VHDL `zxnext.vhd:1941-1944`: `uart0/1_rx_near_full or (uart0/1_rx_avail and not nr_c6_int_en_2_*(1))`. jnext `uart.cpp:626-630` fires on every byte unconditionally; `im2.cpp:383-392` ORs mask in enable path only, not request shape.
- **User impact**: NR 0xC6=0x20 (near-full only) sees spurious per-byte interrupts.
- **Source ref**: Wave-2 serial (NEW-UART-1); reviewer APPROVE.
- **Coverage today**: none; distinct from G39/G72.
- **Dependencies**: small request-mask refactor.
- **Effort**: L.
- **Status (2026-05-03g)**: CLOSED. `Emulator::on_rx_interrupt` callback gates request as `near_full || (avail && !mask_bit)` per VHDL `:1941-1944`. UART0 mask = NR 0xC6 bit 1; UART1 = bit 5. Test INT-07 closed (PASS, re-homed to `uart_integration_test`). Branch `tier6-uart-cluster`.

### G135. NR 0xA0 Pi peripheral enable bits not honoured
- **What**: VHDL `zxnext.vhd:1241,2278-2281,5080`: `pi_uart_en <= bit(4)`, `pi_i2c1_en <= bit(3)`, `pi_uart_rxtx <= bit(5)`, etc. Reset default 0x00 → all off. jnext: no NR 0xA0 handler; UART1/I2C1/SPI0 routing always on.
- **User impact**: probes for "is Pi attached?" inconsistent; benign on default boots.
- **Source ref**: Wave-2 serial (NEW-UART-3); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: distinct from G39 (AT bridge), G72 (pin-7), G73 (I2S).
- **Effort**: L.
- **Status (2026-05-03g)**: CLOSED (also covers G138). NR 0xA0 control byte stored in Emulator with bit fan-out per VHDL `zxnext.vhd:2278-2281`; setters wired into UART1 + I2C1 routing. Read mask `(c & 0x39)` per VHDL `:6189` (b5,b4,b3,b0 pass-through, others 0). Save-state appended at end-of-Emulator-stream (EOF-tolerant). Tests NR_A0-01/02/03 (PASS in `uart_integration_test` — re-homed from `uart_test`) + I2C-13 (PASS in `uart_test`). Branch `tier6-uart-cluster`.

### G136. SPI Flash CS (cpu_do=0x7F) ignored — config-mode-gated select absent
- **What**: VHDL `zxnext.vhd:3315-3320`: `cpu_do=0x7F AND (config_mode='1' OR reset_type(2)='1')` → port_e7_reg=0x7F → spi_ss_flash_n asserted. jnext `spi.cpp:73-77` literally documents the omission ("Flash select … not modelled at this level"); cpu_do=0x7F → decoded=0xFF (all-deselected).
- **User impact**: NextZXOS firmware reading core-loader flash gets all 0xFF; firmware-update tooling fails.
- **Source ref**: Wave-2 serial (NEW-SPI-1); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: needs Flash device backend (even all-0xFF stub).
- **Effort**: M.
- **Status (2026-05-03h)**: WONT (out of scope until on-FPGA Flash emulation lands). Closing as PASS would require emulating the on-FPGA SPI Flash chip itself (FPGA core image storage + TBBLUE.FW firmware + tbblue config + Flash device backend with command/data state machine + persistence) — i.e. emulating Flash updates on the real Next, not running the FPGA core's behavior. Same principle as G45 (expansion bus) and G29 (Pi I2S host capture). Adjacent SS-09 row (already PASS) covers the safety case: write 0x7F outside `config_mode` → all-deselected (0xFF). `divmmc_test` row SS-08 converted from `skip()` to `// WONT SS-08` comment per `feedback_wont_taxonomy.md`. Revisit when jnext gains a `--flash-image FILE` mode for FPGA-Flash emulation (FPGA core update tooling, firmware-update programs, tbblue config persistence). Branch `divmmc-ss08-wont`.

### G137. SPI master o_spi_wait_n (DMA wait) not surfaced
- **What**: VHDL `serial/spi_master.vhd:56,177` `o_spi_wait_n <= state_idle or state_last_d` consumed by DMA at `zxnext.vhd:3297` (16-cycle separation). jnext `spi.cpp:99-127` byte exchange instantaneous; no wait_n accessor.
- **User impact**: DMA-via-SPI loaders complete in 0 cycles instead of ~16; cycle-accurate timing wrong for SD-via-DMA.
- **Source ref**: Wave-2 serial (NEW-SPI-2); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: distinct from G50/G51 (CPU contention).
- **Effort**: L.

### G138. NR 0xA0 bit 3 — Pi I2C-1 routing onto GPIO 2/3 unmodelled
- **What**: VHDL `zxnext.vhd:2280, 2309-2318` — `pi_i2c1_en <= nr_a0_pi_peripheral_en(3)` gates GPIO mux. jnext `i2c.cpp:171-185` has the AND-gate but no NR 0xA0 wiring.
- **User impact**: I2C1 wired-AND active even when bit 3 clear; dormant today (no Pi).
- **Source ref**: Wave-2 serial (NEW-I2C-1); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: subset of G135 register but separable.
- **Effort**: L.
- **Status (2026-05-03g)**: CLOSED jointly with G135. `I2cController` now carries `pi_i2c1_en_` mirror state set by Emulator after NR 0xA0 writes (re-synced on save-state load). Pre-existing test I2C-11 setup updated to explicitly open the gate (`set_pi_i2c1_en(true)`) before exercising the wired-AND assertion. Test I2C-13 closed (PASS). Branch `tier6-uart-cluster`.

### G139. I2C 24LCxx EEPROM device unmodelled — only DS1307 attached
- **What**: I2C bus is bit-banged; tbblue board attaches both DS1307 RTC at 0x68 and a 24LC256 EEPROM (typical 0x50/0x57) for tbblue config. jnext `i2c.cpp` only registers `I2cRtc`; no EEPROM device class.
- **User impact**: tbblue firmware paths reading the 24LC256 see NACK; degraded-mode boot.
- **Source ref**: Wave-2 serial (NEW-I2C-2); reviewer REVISE — drop G46 link, drop priority Low.
- **Coverage today**: none.
- **Dependencies**: standalone peripheral coverage gap; not currently a confirmed G46 contributor.
- **Effort**: L.
- **Status (2026-05-03g)**: WONT (until tbblue.fw boot path requires the EEPROM contents). I2C-14 in `uart_test` converted to `// WONT I2C-14` comment. Branch `tier6-uart-cluster`.

### G161. RTC 12h-mode hours register snapshot overwrites bit 6 / AM-PM
- **What**: DS1307 12h-mode (bit 6=1) requires AM/PM bit 5 in hours register. jnext `i2c.cpp:111` writes `regs_[2] = to_bcd(t->tm_hour)` unconditionally — every `start()` snapshot overwrites the 12h-mode bit + AM/PM bit.
- **User impact**: software polling RTC in 12h mode silently sees 24h-mode encoding after each snapshot.
- **Source ref**: Wave-2 nmi-boot-sd-rtc (NEW-RTC-3); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: tiny branch on `mode_12h_`.
- **Effort**: L.
- **Status (2026-05-03g)**: CLOSED. `i2c.cpp` `start()` snapshot now branches on `mode_12h_` and re-encodes hours in 12h-AM/PM form (bit 6=1, bit 5=PM). Edge cases verified: hour 0 → 12 AM, hour 12 → 12 PM, hour 13 → 1 PM. Test RTC-18 closed (PASS). Branch `tier6-uart-cluster`.

---

## C. CPU, memory, firmware, boot

### G49. NR 0xC0 stackless-NMI execution (CTC NR-C0-02)
- **What**: `src/cpu/im2.h:85,182`: `stackless_nmi_` is F-deferred.
  Wave D was CUT from NMI plan — patching FUSE Z80 core for
  NMI-PUSH suppression risks the 1356-row regression.
- **User impact**: nil unless software relies on stack-less NMI
  vector return-address inspection (rare).
- **Effort**: H.

### G50. Contention `delay()` runtime wiring (Phase 2) [closed]
- **Status: CLOSED.** The runtime wiring landed with G141's closure
  (2026-05-01): all in-opcode contention goes through
  `ContentionModel::contention_tick()` from `src/cpu/z80_cpu.cpp`
  (legacy FUSE tables deleted — see G53). The remaining timing errors
  in that path were fixed later: the window was displaced 64 scanlines
  (Task 50, 2026-07-13 — raw vs ULA display-relative counters) and the
  stretch table had the wrong period (`pattern[hc&7]` repeated the 8-T
  table every 4 T; Task 54, 2026-07-14 — corrected to the 16-entry
  `[hc&0xF]` tables incl. the +3 variant). Evidence: contention_test
  97/97, BIFROST pixel-identical to real FUSE (48K/128K/+3 regression
  rows).
- **What (original, now obsolete)**: `ContentionModel::delay(hc, vc)`
  had no runtime caller; `z80_cpu.cpp` used FUSE's `ula_contention[]`.
- **Dependencies**: G51, G54 remain downstream (commit-edge and
  port_7ffd_active refinements).

### G51. Contention NextREG NR 0x07/0x08 hc(8) commit edge (Phase 3)
- **What**: NR 0x07 / NR 0x08 must land on
  `ContentionModel::set_cpu_speed()` /
  `set_contention_disable()` respecting `hc(8)` commit edge at
  `zxnext.vhd:5822-5823`. Test row CT-TURBO-06 unblocks 4 rows.
- **Dependencies**: G50.
- **Effort**: M.

### G52. Contention Phase-4 screenshot rebaseline [closed-as-noop]
- **Status: NO-OP 2026-05-01.** After G141 + G53 landed, full regression suite ran 33/0/0 with NO drifted references. The cascade described in the original entry did not materialise empirically: NEXT-mode demos default to contention disabled (NR 0x08 b6 / NR 0x07 ≥ 1); 48K demos that DO run with contention on (boot-48k, floating-bus, contention-test, tap-demo) capture past the active raster window or the per-frame timing shift falls within sub-pixel rendering tolerance. No images rebaselined.

### G53. FUSE-table retirement decision [closed]
- **Status: CLOSED 2026-05-01** (commit `f11f023`, part of `feature/g141-g53-fuse-contention`). With G141 routing all in-opcode contention through `ContentionModel::contention_tick()`, the legacy FUSE consumer is dead. Deleted: `z80_build_contention_tables`, `z80_set_page_contended`, the four extern table definitions (`memory_map_read`, `memory_map_write`, `ula_contention`, `ula_contention_no_mreq`), and their declarations in `third_party/fuse-z80/fuse_z80_shim.h`. CT-FUSE-05 asserts the single-source invariant via exact-match equality (gate-OFF totals == un-contended baseline). Independent reviewer APPROVE.

### G54. Contention port_7ffd_active term (CT-IO-05/06)
- **What**: Bare-class `port_contend()` does not consume
  `port_7ffd_active` — gated by full machine-timing-128/-p3 +
  `port_7ffd_io_en` (NR 0x82 b1) + valid `port_7ffd` decode. Calling
  with `cpu_a == 0x7FFD` returns odd-bit term only.
- **Source ref**: `src/memory/contention.h:73-79`.
- **Effort**: L.

### G55. NR 0xD8 IO-trap (FDC NMI source) — stub
- **What**: NR 0xD8/D9/DA FDC IO-trap surface — comprehensive, not just the
  NR 0xD8 enable bit. VHDL `zxnext.vhd:3866-3898, 6268-6272`:
  `nr_d8_io_trap_fdc_en` enables the trap; `nr_d9_iotrap_write` reads
  back the CPU write data on the trapped port; `nr_da_iotrap_cause`
  is a 2-bit field encoding which trap fired (01=2FFD-rd, 10=3FFD-rd,
  11=3FFD-wr); composed into NR 0x02 bit 4 (`nr_02_iotrap`). jnext:
  no NR 0xD8/D9/DA write or read handlers; `NmiSource::strobe_iotrap()`
  exists but `iotrap_strobe_pending_` is consumed and never propagated
  to `nmi_assert_mf` (Task 6 audit / NEW-NMI-3, tracked as G162 — pair
  with G55 in one patch). NR 0x02 bit 4 always zero (per G153 missing
  reset_type FSM).
- **User impact**: +3 floppy-trap NMI edge (rare).
- **Pairing**: G55 + G162 (runtime dead-end on iotrap_strobe_pending_)
  land in one patch. Both required for FDC NMI path to fire.
- **Effort**: L.

### G56. NextReg `regs_[]` shadow-store systemic bug — **PARTIAL CLOSURE 2026-05-03 (option a)**

**Status (2026-05-03)**: Option (a) per-NR `read_handlers` landed for the entire Task 6 Wave 2 audit list (~24 NRs across 5 clusters). The systemic `NextReg::write` contract — option (b) — is **NOT** yet rewritten; this remains the actual G56 closure work and is parked for a fresh session per user direction. The per-NR work surfaced + fixed two latent bugs in the same scope: NR 0x05 reset default `0x40 → 0x41` (VHDL `nr_05_scandouble_en := '1'` per `zxnext.vhd:1303`), and NR 0x06 bit 2 (`ps2_mode`) write was not gated on `nr_03_config_mode` (VHDL `:5167-5169`); both fixed in cluster A. Cluster E also closes G99 (NR 0x6E/0x6F bit 6 reserved-zero mask attribution; the actual mask was already in `Tilemap::get_*_read()` accessors since 2026-04-28 commit `e375456`).

**What's done (option a)**:

| Cluster | Commit | NRs covered | Pass delta |
|---------|--------|-------------|------------|
| A | `0eb502a` | 0x05, 0x06 (+ NR 0x05 reset fix, NR 0x06 ps2_mode write gate) | +9 |
| B | `677625b` (+ `1b562d5` save/load fix) | 0x09, 0x0A, 0x0B, 0x10, 0x15, 0x34 | +24 +1 |
| C | `fd8f805` | 0x22, 0x23 | +10 |
| D | `cb29394` | 0x40, 0x43, 0x4C, 0x69, 0x6A, 0x6B, 0x6C | +14 |
| E | `b1606fb` | 0x6E, 0x6F, 0x70, 0x71, 0x80, 0x81 (+ G99 attribution) | +13 |

Aggregate +71 PASS rows in `nextreg_integration_test`. Final unit-test sweep: 3843/3670/0/173 across 33/33 suites. Regression 34/0/0 byte-identical.

**What remains (option b)** — the actual G56 closure, parked for a fresh session:

The contract bug at `src/port/nextreg.cpp:117-123` is unchanged: `NextReg::write` still does `regs_[reg] = val;` unconditionally before the write_handler runs. Any future NR added with a write-mask handler but no matching read_handler will silently re-introduce the divergence pattern. Option (b) re-architects the contract — either (b1) handler returns the canonical masked byte and `NextReg::write` stores that, or (b2) handlers explicitly call `NextReg::store(reg, masked)`. Higher blast radius (must audit ~40+ existing handlers) but solves the contract systemically. Option (b) does NOT replace option (a) entirely: live-state and dynamic registers (NR 0x05 eff_overlays, NR 0x10 SPKEY_BUTTONS, NR 0x22 pulse_int_n, NR 0x40 autoinc, NR 0x68 ulap_en, NR 0x69 cross-subsystem composition) still need read_handlers regardless. So a fully-closed G56 is option (b) + retain ~9 of the per-NR read_handlers from this session.

**Original audit reference (kept for context):**


- **What**: `NextReg::write` (`src/port/nextreg.cpp:114`) stores the raw 8-bit
  pre-handler-dispatch byte. Handlers that mask write bits do NOT
  propagate the mask to `regs_[]`. Reads return the raw byte for any
  NR without a `set_read_handler`. The Task 6 Wave 2 audit of all
  ~110 NR read-mux entries (`zxnext.vhd:5878-6289`) vs the ~36
  read-handlers in jnext today identifies the divergent set:

  NR **0x05** (line 5897 — joy0/joy1/scandouble/5060 interleaved with
  `eff_*` overlays); NR **0x06** (5900 — psg_mode + hotkey enables);
  NR **0x09** (5909); NR **0x0A** (5912); NR **0x0B** (5915); NR
  **0x10** (5924); NR **0x15** (5939); NR **0x22** (5992 — bit 7
  dynamic pulse_int_n); NR **0x23**; NR **0x34**; NR **0x40** (6035 —
  palette idx with autoinc state); NR **0x43** (6044); NR **0x4C**
  (6056 — bits 7:4 always 0); NR **0x68** (6093 — bit 4 from
  port_ff3b_ulap_en, NOT NR 0x68 bit 4); NR **0x69** (6096 — bit 6
  from port_7ffd_shadow, bits 5:0 from port_ff_reg(5:0)); NR **0x6A**
  (6098); NR **0x6B** (6101 — bit 7 from nr_6b_tm_en); NR **0x6C**
  (6104); NR **0x6E** (6107 — bit 6 always 0); NR **0x6F** (6110 —
  bit 6 always 0); NR **0x70** (6113 — bits 7:6 always 0); NR **0x71**
  (6116 — bits 7:1 always 0); NR **0x80** (6122); NR **0x81** (6125 —
  bit 7 from i_BUS_ROMCS_n, bit 2 always 0). At least 16 confirmed
  divergences; possibly more once palette autoinc + dynamic
  pulse_int_n are factored. Two NR 0x6E/0x6F sub-rows are tracked as
  the concrete G99 (reserved-bit mask). The discrete two-register
  NR 0x19/0x1A read-handler pattern is tracked as G97 (not part of
  G56 fix).
- **Source ref**: memory `project_systemic_nextreg_shadow_store.md`.
- **Proposed**: per-register read_handlers (low risk, opportunistic)
  OR systemic NextReg::write rework (higher blast radius). Add VHDL-
  oracled read-back rows to `nextreg_test.cpp`.
- **Design fork (Task 6 audit / Open Q3)**: two implementation
  strategies are viable —
  (a) **Per-NR read_handlers** (current pattern from NR 0x12/0x13/
      0x07/0x08/0xC4): replicate ~16 lambdas pulling from the
      authoritative subsystem mirror. Localised; low blast radius;
      pattern already proven.
  (b) **Re-architect `NextReg::write`**: route handler-returned masked
      value into `regs_[]` so reads of unhandled NRs see the masked
      state. Higher blast radius but solves any future NR additions
      without per-NR work. User to decide; (a) recommended near-term.
- **Effort**: M.

### G57. MMU `current_rom_bank()` — three documented gaps [closed]
- **Status: CLOSED 2026-04-28** (task8-t1w2-mmu-v2) — added `Mmu::sram_rom3()` accessor that composes the VHDL `sram_rom3` signal per machine type with NR 0x8C altrom locks factored in (`zxnext.vhd:2985,2990,3000,3004`). The legacy `current_rom_bank()` accessor is unchanged (callers still use it for 4-way ROM-bank reporting); `sram_rom3()` is the VHDL-faithful replacement for "is bank 3 currently active?" gates (DivMMC ROM3-conditional automap, etc.). Test rows ROM-10/11/12 in `mmu_test` flipped to PASS with discriminative coverage of the 48K hardwire, the NR 0x8C lock_rom1 factor, and the +3-vs-ZXN port_1ffd(2) handling.
- **What**: `src/memory/mmu.h:530-545`:
  - 48K-mode `sram_rom3` hardwire (zxnext.vhd:2985) — we report
    bank 0 regardless of machine type. Impact nil today (DivMMC
    tests in Next mode).
  - NR 0x8C altrom factor (zxnext.vhd:3138) ignored.
  - Next-mode port_1ffd bit 2 normally gated by NR 0x82 bit 3;
    direct port_1ffd write on Next mode could spuriously claim ROM3.
- **Coverage today**: ROM-10/11/12 in `mmu_test`.
- **Effort**: L.

### G58. MMU shadow-screen routing (TASK-MMU-SHADOW-SCREEN-PLAN)
- **What**: Plan authored 2026-04-23. 0x7FFD bit-3 shadow-screen
  toggle wiring + 2 test rows P7F-16/17 + screenshot rebaseline.
  Note: P7F-16/17 have already landed live (verified in recent
  commits `b8d3ea6`); the residual is the broader plan rows for
  NR-driven shadow-screen handling. Confirm scope with reviewer.
- **User impact**: 128K games using shadow-screen for double-
  buffering still render incorrectly in some paths.
- **Source ref**: `TASK-MMU-SHADOW-SCREEN-PLAN.md`.
- **Effort**: L.

### G59. NextZXOS bypass-tbblue-fw boot path [WONT]
- **What**: `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` — fully-scoped
  4-branch plan (CLI / SRAM populate / synthetic RESET_SOFT + post-
  firmware NR state / host-side FAT32 reader). Provides instant-boot
  NextZXOS bypassing tbblue.fw — mitigates G46 if firmware-faithful
  proves intractable.
- **Dependencies**: 8 open VHDL/state questions (Q1-Q8) need pre-
  implementation answers (G62, G63, G64).
- **Effort**: H (Branches 1-3; Branch 4 optional).
- **Status (2026-07-13) — WONT**: firmware-faithful boot did NOT prove
  intractable — native NextZXOS boot was achieved 2026-07-10 (v0.94.0,
  see G46). The bypass had already been implemented as
  `--bypass-tbblue-fw` (Task 18, 2026-05-17) as a stopgap before that,
  but with the real boot path working it was **removed from `src/`
  entirely on 2026-07-11 by explicit user decision** — native
  firmware-faithful boot is the only supported path. This entry no
  longer describes "unstarted, effort H" work; it describes a feature
  that was built, then deliberately deleted. See
  `EMULATOR-DESIGN-PLAN.md` Phase 11 ("the flag and its C++ route were
  REMOVED per user decision") and the historical design retained for
  reference only in `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md`. Do not
  re-file this work without a new reason firmware-faithful boot has
  regressed.

### G60. config.ini / menu.ini / menu.def parsing [WONT]
- **What**: Bypass plan §4 Cons: ~29 user customisations
  (50/60Hz, scandoubler, joystick mapping, DivMMC/MF enables,
  turbosound, DAC, mouse DPI) parsed by firmware today; bypass
  skips. v1 hard-coded defaults; v2 needs JNEXT-side parser.
- **Dependencies**: G59 lands first.
- **Effort**: M.
- **Status (2026-07-13) — WONT**: this item existed solely as a
  dependency of G59 ("Dependencies: G59 lands first"). G59 is
  retired WONT (see above) — the bypass path it would have completed
  no longer exists in `src/`. No independent justification remains
  for a config.ini/menu.ini/menu.def parser. Do not re-file without a
  new, G59-independent reason.

### G61. Z80N undocumented RETN-alias coverage edge
- **What**: Test gap — `fuse_z80_test` covers Z80 base; `z80n_test`
  covers 30 Z80N extensions; nothing pins **Z80 ED-undocumented**
  behaviour for the 6 RETN aliases (0x55/5D/65/6D/75/7D). A
  discriminative row would catch a future regression in G46(a)'s
  band-aid removal.
- **Effort**: L.

### G62. NR 0x03 soft-reset `config_mode` preservation question
- **What**: `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` Q1: VHDL behaviour
  of `nr_03_config_mode` across soft reset vs `src/port/nextreg.cpp:
  47-51` which sets `nr_03_config_mode_ = true` unconditionally on
  reset(). If real-VHDL preserves config_mode across soft reset
  (no reset branch), our re-assert is a divergence.
- **Effort**: L (1-hour VHDL check + possible fix).

### G63. NR 0x03 machine-type latch read-back
- **What**: Q2: VHDL nr_03 machine-type latches to a separate
  signal (zxnext.vhd:5137); `NextReg::reset()` zeroes
  `regs_[0x03]`, losing machine-type read-back. Subset of G56 for
  NR 0x03.
- **Effort**: L.

### G64. Bypass plan open VHDL questions (keymap / altROM 0x06,0x07)
- **What**: Q3-Q8 in `FUTURE-NEXTZXOS-BYPASS-TBBLUE-FW.md` —
  keymap.bin programmatic state if Z80 reads NR 0x28-0x2B back;
  altROM page layout in enNextZX.rom blob.
- **Dependencies**: G59.
- **Effort**: L (documentation + blob inspection).

### G65. CPU/Copper cycle-accurate NR-write priority
- **What**: VHDL `zxnext.vhd:4769-4777` enforces Copper-wins
  priority on same-28-MHz-cycle NR writes with CPU deferred. JNEXT
  serialises — priority implicit in tick-loop order, not enforced.
  ARB-01/02/03 tests order stimulus manually.
- **Cross-link (Task 6 audit / G117)**: G117 — *Copper executes per
  Z80 instruction, not per 28 MHz cycle* — describes a distinct but
  related defect. G65 assumes both surfaces are clocked but quibbles
  over who wins the bus on a tied edge. G117 is about granularity:
  the Copper does not execute often enough at all, regardless of CPU
  writes. The two converge under a true cycle-accurate Copper
  scheduler; for now keep both entries — if a single cycle-accurate
  refactor lands them, they may be `[merged]`-style closed together.
- **Effort**: H (cycle-accurate scheduler refactor).

### G66. Save-state schema versioning + per-subsystem framing [merged C20+D01+D02]
- **What**: `src/core/saveable.h` exposes `StateWriter` / `StateReader`
  as raw byte streams. No magic, no schema version, no per-subsystem
  framing. Verified — no `SCHEMA_VERSION` / `schema_version` /
  `SaveStateVersion` matches in `src/`. Field order silently couples
  to source order; `i2c.cpp:361` flags this manually as "Must mirror
  save_state field order exactly". `divmmc.cpp:368` already documents
  silent-deserialise hazard ("pre-NA-03 snapshots do not carry them").
- **User impact**: ANY change to subsystem `save_state` field order /
  type / count silently corrupts older snapshots and may
  crash/UB on rewind reload. Affects rewind ring buffer +
  Save/Load Snapshot menu (once G35 lands).
- **Source ref**: `src/core/saveable.h:13-100`; per-subsystem
  hand-rolled ordering.
- **Coverage today**: `rewind_test.cpp` round-trips current build
  only; no cross-version compat.
- **Proposed**: 4-byte magic + `SCHEMA_VERSION u32` head; per-
  subsystem `tag(u16) + length(u32) + payload` framing; reject load
  on magic mismatch; registered migrators on version mismatch.
  Update RZX-embedded SNA path (`sna_saver.cpp`). Add per-subsystem
  save→load round-trip lock test.
- **Effort**: M.

### G67. Rewind buffer pre-allocated bound + assertion — CLOSED (Task 60b, 2026-07-15)
- **What**: `src/debug/rewind_buffer.{h,cpp}`: ring of `max_frames *
  snapshot_bytes`. `snapshot_bytes` computed once at construction;
  if subsystem `save_state` widens, bound goes stale silently and
  writes overflow into next slot. Verified Apr 19 — `rewind_test.
  cpp:224 snap_size < 2 MB` had to be widened to 3 MB on Ram-2 MB
  bump. Combined with G66, schema drift here causes corrupt
  restores instead of clean rejects.
- **Resolution (Task 60b)**: `StateWriter`/`StateReader` now enforce
  bounds (sticky `overflow()` / `out_of_bounds()` flags; out-of-slot
  writes suppressed, out-of-buffer reads zero-filled);
  `take_snapshot` refuses to publish any slot where the written size
  differs from construction-time `snapshot_bytes_` (loud error, slot
  dropped). Per-subsystem sentinels in `Emulator::save_state`/
  `load_state` additionally name the first desynced subsystem on
  restore. Rows RB-FRAME-01..03 un-skipped in `rewind_test` (+ SW-BND/
  SR-BND/SENT rows). G66 (schema versioning for cross-version
  snapshots) remains open and separate.

### G68. Rewind sub-frame granularity
- **What**: Snapshots at frame boundaries only; rewind cannot stop
  at arbitrary T-state. Listed in `EMULATOR-DESIGN-PLAN.md` Phase 8
  Step 4 as "frame snapshots ring buffer" — design choice. WONT-
  leaning unless a user asks.
- **Effort**: H if pursued.

### G87. IM2 RETI/RETN decoder cannot see ED-prefix second byte
- **What**: VHDL `device/im2_control.vhd:158-209,233-238` requires `ifetch_fe_t3` per M1-fetched byte (incl. ED 0x4D / 0x45 follow-byte). jnext `Z80Cpu::on_m1_cycle` fires once per `execute()` with the FIRST byte (`z80_cpu.cpp:388,418`); decoder enters S_ED_T4 on ED but next call carries first byte of the next instruction. `Im2Controller::advance_decoder` (`im2.cpp:531-595`) never sees real RETI/RETN; spurious pulses on `LD C,A`/`LD B,L` after any ED-prefix.
- **User impact**: latent today (default pulse mode early-returns at `im2.cpp:186`); flips to High once NR 0xC0 b0 set — IM2 daisy-chain locks up after the first ISR.
- **Source ref**: Wave-1 cpu (NEW-CPU-1); reviewer APPROVE.
- **Coverage today**: G48 DivMMC band-aid is the same root with different consumer; G49 stackless-NMI execution; G61 RETN-alias test gap. None covers this.
- **Dependencies**: per-byte M1 hook from FUSE core OR pre-decode ED 4D / ED 45 in `z80_cpu.cpp`.
- **Effort**: M.
- **Status (2026-05-03d)**: ED-prefix path closed. `Z80Cpu::execute()` now fires `on_m1_cycle` on BOTH bytes of the ED-prefix sequence (`z80_cpu.cpp:480-481, 507-508`), the im2 decoder FSM advances S_ED_T4 → S_ED4D_T4 / S_ED45_T4 correctly, and the legacy `prev_ed`+RETN-alias band-aid in `emulator.cpp` was retired.
- **Follow-up — DD/FD/CB prefix per-byte M1 delivery** (also tracked as Task 2 audit V15-CPU-NIT-02 / class-(d) D10): still missing for the non-ED prefix paths in `z80_cpu.cpp:514-525, 527-528`. The im2 decoder FSM has `S_DDFD_T4` and `S_CB_T4` states (`im2_control.vhd:158-209`) that would benefit from per-byte delivery the same way the ED path now does. The SKIP rows targeted by G87 were RETI/RETN only, so these prefixes were left alone — but any future SKIP/audit rows that depend on FSM advancement through DD/FD/CB will need this same per-byte fix. VHDL exposes per-T-state prefetch hooks; FUSE Z80 (jnext's base Z80 core) only exposes a first-byte M1 hook, so the fix requires either modifying upstream FUSE (CLAUDE.md forbids) OR a per-T-state shim layer. Effort: S for ED-path-style mirror; H if the full FUSE-boundary per-T-state hook is needed.

### G88. NMI does not capture PC into NR 0xC2 / NR 0xC3
- **What**: VHDL `zxnext.vhd:2050-2085` latches `nr_c2/c3_retn_address_lsb/msb` on `Z80N_command_s = NMIACK_LSB/MSB AND cpu_wr_n='0'` regardless of stackless mode. Read at `:6232-6236`. jnext: no NR 0xC2/0xC3 handlers; `fuse_z80_nmi()` pushes PC but does not propagate. Cross-bucket dup: NEW-CPU-2 = NEW-IM2-1 — kept once.
- **User impact**: software polling NR 0xC2/0xC3 to inspect last-NMI PC reads 0xFF; Multiface-style cheat menus and DivMMC NMI handlers can't display "broken at PC=…".
- **Source ref**: Wave-1 cpu (NEW-CPU-2) + Wave-1 copper (NEW-IM2-1); reviewer APPROVE both.
- **Coverage today**: none.
- **Dependencies**: distinct from G49 (stackless EXECUTION) and G56 (write-side shadow store).
- **Effort**: L.

### G89. Z80N block-repeat ops (LDIRX/LDDRX/LDPIRX/LDIRSCALE) non-interruptible
- **What**: VHDL `cpu/t80n_mcode.vhd:2098-2138, 1953-1991, 2188-2226` re-decode opcode each iteration via I_BT, sampling INT/NMI on inter-iteration M1 boundary. jnext `z80n_ext.cpp:352-427` runs each repeat as closed C `for` loop; no INT/NMI sampling between iterations. 65 536-iteration LDIRX blocks ~244 ms (12 frames at 50 Hz).
- **User impact**: Z80N IM2-driven music drivers running LDIRX during a frame miss INT silently; behaviour diverges from hardware.
- **Source ref**: Wave-1 cpu (NEW-CPU-3); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: standalone — replace `for` with PC-=2 single-iteration body per opcode.
- **Effort**: L.
- **Status (2026-05-03e)**: CLOSED. `src/cpu/z80n_ext.cpp` rewritten one-iteration-per-`execute()` with `regs.PC = (regs.PC - 2) & 0xFFFF` if BC≠0; `Z80Cpu::execute()` samples INT at the top of the call (`z80_cpu.cpp:419-449`) so the rewind restores standard Z80-LDIR-style inter-iteration INT sampling. BC=0 → 65536 iterations preserved via underflow. Sibling fix landed in same commit: LDDRX DE-direction (VHDL `t80n_mcode.vhd:2250` `IncDec_16 <= "0101"` increments DE; old jnext was decrementing). Coverage: 5 PASS rows in `test/ctc_interrupts/ctc_interrupts_test.cpp` IM2-Decoder-Gaps group (PULSE-G89-01..04 per-opcode iter+rewind + PULSE-G89-INT combined INT-sample). Test data: `test/z80n/tests.expected` 6 entries refreshed (R 02→04 for per-iter R-bumps; edbc_basic also DE 9fff→a003, mem a000→a002). Branch `g89-z80n-block-move-int-sample` (`b33eb07`), merged as `607a55a`. Full regression 34/0/0 byte-identical.

### G90. 28 MHz turbo SRAM-read wait state not modelled
- **What**: VHDL `zxnext.vhd:3171-3181` at `cpu_speed = "11"` drives `sram_wait_n <= '0'` on every SRAM read. ULA+ palette readback wait `:4583` is the same pattern at port-read time. jnext: no `sram_wait` references; `ContentionModel` is gated OFF at 28 MHz per `:4481`. Cross-bucket dup with NEW-CONT-3.
- **User impact**: turbo-mode timing is 7% fast on read-heavy code; benchmarks/RZX determinism / AY chip timing under turbo diverge.
- **Source ref**: Wave-1 cpu (NEW-CPU-4) + Wave-2 memory (NEW-CONT-3); reviewer APPROVE both — same gap.
- **Coverage today**: none (28 MHz is rarely the timing-baseline).
- **Dependencies**: requires SRAM-page detection in FUSE callback path; gates on cpu_speed=3.
- **Effort**: M.

### G119. CTC on_interrupt gated at peripheral, not fabric edge
- **What**: VHDL `zxnext.vhd:1941` carries raw ZC/TO; int_en AND happens at `im2_peripheral.vhd:172`. jnext `ctc.cpp:251-282` only calls `on_interrupt` when channel int_enabled. If int_en flips between ZC/TO, the prior pulse is lost.
- **User impact**: race-the-edge int_en toggle observable.
- **Source ref**: Wave-1 copper (NEW-CTC-1); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: mirror UART RX/TX pattern — raise unconditionally at source.
- **Effort**: L.
- **Status (2026-05-03g)**: CLOSED. `src/peripheral/ctc.cpp` raises `on_interrupt` UNCONDITIONALLY at the ZC/TO source edge; `Im2Controller::step_devices()` Phase 1 (im2.cpp:711-732) keeps the gate at the latch level (`int_req && int_en`) per VHDL `im2_peripheral.vhd:172`. Test IM2W-G119-01 closed (PASS). Branch `tier6-ctc`.

### G120. CTC prescaler cleared on running TC reload (vs VHDL preserve)
- **What**: VHDL `device/ctc_chan.vhd:131-141` clears p_count only on `reset_soft='1'`. jnext `ctc.cpp:41` clears `prescaler_` on every TC write incl. running-reload (S_RUN_TC → S_RUN).
- **User impact**: mid-stream TC reload restarts prescaler from 0 — next ZC/TO up to one prescaler period late.
- **Source ref**: Wave-1 copper (NEW-CTC-2); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: gate on RESET/RESET_TC state.
- **Effort**: L.
- **Status (2026-05-03g)**: CLOSED. `prescaler_` clear in `ctc.cpp` now gated on `state_ == State::RESET_TC` per VHDL `ctc_chan.vhd:117` (`reset_soft = state /= S_RUN and state /= S_RUN_TC`). Mid-stream TC reload preserves prescaler. Test CTC-TM-G120-01 closed (PASS). Branch `tier6-ctc`.

### G121. Pulse-mode 32/36-cycle gate not updated on NR 0x03 timing change
- **What**: VHDL `zxnext.vhd:2033-2042` `pulse_count_end` depends on `machine_timing_48 OR _p3` from `nr_03_machine_timing` (`:5132-5145`). jnext: `Im2Controller::set_machine_timing_48_or_p3` called once at `Emulator::reset_machine` only.
- **User impact**: software switching NR 0x03 timing post-boot sees wrong pulse-INT width; tape loaders timing INT response misalign.
- **Source ref**: Wave-1 copper (NEW-IM2-2); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: one extra call inside NR 0x03 handler.
- **Effort**: L.
- **Status (2026-05-03g)**: CLOSED. NR 0x03 write_handler in `src/core/emulator.cpp` now calls `Im2Controller::set_machine_timing_48_or_p3` after each write per VHDL `zxnext.vhd:2033-2042` (idempotent). Test PULSE-G121-01 closed (PASS). Branch `tier6-ctc`.

### G122. DMA 14 MHz dma_d_p_s rising-edge read latch unmodelled
- **What**: VHDL `device/dma.vhd:158-181` defines turbo-only `dma_rw_extend` and rising-edge `dma_d_p_s` selected when `turbo_i="10"`. jnext `dma.cpp:686-696` reads source byte without the rising-vs-falling latch difference.
- **User impact**: edge-of-burst sequencing differs; nil for simple memory-to-memory.
- **Source ref**: Wave-1 copper (NEW-DMA-1); reviewer APPROVE.
- **Coverage today**: documented in DMA test plan §6 deviations.
- **Dependencies**: per-cycle rd/wr_n strobe shape.
- **Effort**: M.

### G123. NR 0x0A bit 4 (divmmc_automap_en) not wired
- **What**: VHDL `zxnext.vhd:1126,5196,4112` — `divmmc_automap_reset <= '1' when port_divmmc_io_en='0' or nr_0a_divmmc_automap_en='0'`. jnext `emulator.cpp:447-451` decodes bits 5/3/1:0 only; setter `DivMmc::set_nr_0a_4_enable()` exists at `divmmc.h:113` but never called.
- **User impact**: NR 0x0A toggle of automap silently dropped; demos disabling automap via 0x0A still trap on RST.
- **Source ref**: Wave-2 divmmc (NEW-DM-1); reviewer APPROVE.
- **Coverage today**: in-source comment at `:446` flags it.
- **Dependencies**: one-line plumbing.
- **Effort**: L.

### G124. NR 0x83 b0 not propagated to DivMmc::set_port_io_enable
- **What**: VHDL `zxnext.vhd:2412,4112,4147` — `port_divmmc_io_en` feeds both `divmmc_mod.i_en` (gates ROM/RAM overlay) and `divmmc_automap_reset`. jnext `emulator.cpp:1850/1854` consults the bit at port-0xE3 dispatch only; `DivMmc::set_port_io_enable` has zero callers; `set_enabled(true)` from boot is the only setter — so port_io_enable_ stays true forever after boot.
- **User impact**: NR 0x83 b0 clear leaves DivMMC ROM mapped (when conmem set) and automap firing.
- **Source ref**: Wave-2 divmmc (NEW-DM-2); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: NR 0x82-0x85 write handler suite.
- **Effort**: L.

### G125. NR 0x06 bits 7/5 (hotkey enables) not stored / acted on
- **What**: VHDL `zxnext.vhd:1107-1108,5162-5169` — bit 7 hotkey_cpu_speed_en, bit 5 hotkey_5060_en, bit 2 ps2_mode (config_mode-gated). Reset defaults `'1'/'1'`. jnext `emulator.cpp:1591-1626` decodes bits 6/4/3/1:0 only; reset zeroes regs_[0x06] (wrong default 0x00 vs 0xA0).
- **User impact**: F3/F8 NR-side hotkey gates inert; reset default wrong (latent).
- **Source ref**: Wave-2 divmmc (NEW-PER-1) + Wave-2 input (NEW-KB-1). Same gap; bundled.
- **Coverage today**: none.
- **Dependencies**: pairs with G132 (F-key FSM) + G147 (host F-key dispatch).
- **Effort**: L.

### G131. NR 0x0A bits 7:6/bit 5 not gated on nr_03_config_mode
- **What**: VHDL `zxnext.vhd:5191-5198`: bits 7:6 (mf_type) and bit 5 (sd_swap) only update under `nr_03_config_mode='1'`. jnext `emulator.cpp:447-451` writes bit 5 unconditionally to `spi_.set_sd_swap`.
- **User impact**: stray bit-5 outside config_mode flips SD-card mapping.
- **Source ref**: Wave-2 input (NEW-MS-2); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: distinct from G48 / G56.
- **Effort**: L.

### G140. Boot ROM overlay 8 KB → 16 KB mirror at 0x0000-0x3FFF [closed]
- **Status: CLOSED 2026-04-28** (task8-t1-mmu) — `Mmu::read` now gates `addr < 0x4000` and masks `addr & 0x1FFF` so the upper 8 KB mirrors the lower 8 KB through the 16 KB span. Test rows BOOT-OVL-01/02 in `mmu_test` flipped to PASS.
- **What**: VHDL `zxnext.vhd:1856,3199-3204` — `bootrom_en` gates the overlay on `cpu_a(15:14)='00'` (full 16K); `bootrom_mod` uses `cpu_a(12:0)` (13-bit) so upper 8K mirrors lower 8K. jnext `mmu.h:114-117` overlays only for `addr < boot_rom_size_` (8192).
- **User impact**: nextboot.rom is exactly 8 KB; future bootrom_ab variant or test stimulus reading 0x2000-0x3FFF expects the mirror.
- **Source ref**: Wave-2 memory (NEW-MMU-1); reviewer APPROVE.
- **Coverage today**: BOOT-OVL-01/02 in `mmu_test`.
- **Dependencies**: change condition + modulo index.
- **Effort**: L.

### G141. FUSE in-opcode contention macros inert (memory_map_read[]/ula_contention[] zero-filled) [closed]
- **Status: CLOSED 2026-05-01** (`feature/g141-g53-fuse-contention`, commits `9ea5fc4` + `f11f023` + `d9adbd9`). FUSE `contend_read`/`contend_read_no_mreq`/`contend_write_no_mreq` are now implemented as C functions in `src/cpu/z80_cpu.cpp` calling `ContentionModel::contention_tick()`; `CORETEST` is set on `third_party/fuse-z80/fuse_z80_core.c` (single TU containing all FUSE opcode files via `#include`) so all macro-call sites resolve to the function-override path. M1 fetch + no-MREQ tail + data cycles all consult the same VHDL-faithful gate. CT-FUSE-01/02/05 now `check()`; CT-FUSE-03/04 retired (port contention covered by data-path CT-IO-01..09 + CT-INT-01). fuse_z80_test holds 1356/1356.
- **Coverage today**: contention_test 74/74/0/0 (was 76/71/0/5).
- Independent reviewer APPROVE.

### G142. NR 0x07 cpu_speed deferred bus-idle commit not modelled
- **What**: VHDL `zxnext.vhd:5796-5828` — `cpu_speed <= nr_07_cpu_speed` only on `cpu_mreq_n='1' AND cpu_iorq_n='1' AND cpu_m1_n='1' AND dma_holds_bus='0'` (bus-idle). jnext `emulator.cpp:322-326` synchronously calls `clock_.set_cpu_speed` and `contention_.set_cpu_speed` on NR 0x07 write.
- **User impact**: turbo flip applies immediately in jnext; in VHDL it defers to next bus-idle. Demos bracketing a turbo flip with a contended access expect contention to still apply at 3.5 MHz on that access.
- **Source ref**: Wave-2 memory (NEW-CONT-2); reviewer APPROVE — distinct from G51 (different commit edge).
- **Coverage today**: none; parallax.nex turbo experiment (memory handover) exposed it.
- **Dependencies**: shadow/effective pair like NR 0x08 b6; reviewer notes Clock should defer too for symmetry.
- **Effort**: M.

### G143. port 0xEFF7 missing port_eff7_io_en gate [closed; NR-mapping corrected 2026-05-04]
- **Status: CLOSED 2026-04-28** (task8-t1-mmu) — gate landed in `src/core/emulator.cpp`. Test row EF7-06 RE-HOMED from `mmu_test` to integration tier (port-level cross-NR observation; not a pure MMU API surface).
- **Correction 2026-05-04 (Tier A SKIP-reduction)**: original fix gated against NR 0x84 b2 (incorrect). VHDL `zxnext.vhd:2392` shows `internal_port_enable <= (nr_85 & nr_84 & nr_83 & nr_82)` so bit 26 sits in the nr_85 range — the correct gate is **NR 0x85 bit 2**, matching `doc/testing/IO-PORT-DISPATCH-TEST-PLAN-DESIGN.md:191` and the existing `port_test.cpp` NR85-02 row. Gate corrected in `src/core/emulator.cpp` (port 0xEFF7 handler) and integration test row added in `test/mmu/mmu_integration_test.cpp` (MMU-EF7-IO-EN-00..02).
- **What**: VHDL `zxnext.vhd:2604,2441,2392` — `port_eff7_io_en <= internal_port_enable(26)` = NR 0x85 bit 2.
- **User impact**: prior to the 2026-05-04 correction, NR 0x84 b2 was incorrectly used as the EFF7 gate; software clearing the actual NR 0x85 b2 still saw paging-mode flips land. Now VHDL-faithful.
- **Source ref**: Wave-2 memory (NEW-MMU-2); reviewer APPROVE; mapping correction surfaced via Tier A SKIP-reduction follow-on review.
- **Coverage today**: integration tier — `test/mmu/mmu_integration_test.cpp` MMU-EF7-IO-EN-01/02 (discriminative gate-closed/gate-open pair).
- **Dependencies**: one-line gate.
- **Effort**: L.

### G145. port 0x123B read-back surface absent (returns 0xFF)
- **What**: VHDL `zxnext.vhd:3933` — `port_123b_dat <= segment & "00" & map_shadow & rd_en & enable & wr_en`. jnext `emulator.cpp:1178` registers nullptr read; reads fall through to PortDispatch default 0xFF.
- **User impact**: software probing L2 control register reads 0xFF; resume-time firmware paths may misbehave.
- **Source ref**: Wave-2 memory (NEW-MMU-5); reviewer REVISE — bundle with G92/G144 (composition formula returns the very fields those add).
- **Coverage today**: none.
- **Dependencies**: ship together with G92/G144.
- **Effort**: L.
- Also relevant to section D.

### G148. port_dffd_reg_6 not stored — Multiface readback truncated [closed]
- **Status: CLOSED 2026-04-28** (task8-t1w2-mmu-v2) — `Mmu` now stores cpu_do(6) in a dedicated `port_dffd_reg_6_` flip-flop alongside the 5-bit `port_dffd_reg_`, mirrors the VHDL hard-reset clause, and exposes a `port_dffd_reg_6()` accessor for the future Multiface +3 read-mux. DFF-09 in `mmu_test` flipped to PASS. Save/load_state extended at the tail (binary backward-compatibility break confined to the existing Wave-2 trailing block).
- **What**: VHDL `zxnext.vhd:877,3694,4314` — port 0xDFFD bit 6 stored separately in `port_dffd_reg_6`; consumed by Multiface mux. jnext `mmu.cpp:332` masks to bits 4:0; comment at `mmu.h:328-333` admits.
- **User impact**: Multiface readback of DFFD bit 6 returns 0; affects paging-state inspection (debugger / snapshot).
- **Source ref**: Wave-2 memory (NEW-MMU-8); reviewer APPROVE — sibling of G48.
- **Coverage today**: DFF-09 in `mmu_test`.
- **Dependencies**: G48 Multiface plan can pull this in (now reads via `Mmu::port_dffd_reg_6()`).
- **Effort**: L.

### G151. Z80N NEXTREG opcode mutates nr_register (selected_) — VHDL preserves
- **What**: VHDL `zxnext.vhd:4739-4745` — Z80N requester injects (reg, val) directly without mutating `nr_register`. jnext `z80n_ext.cpp:218-238` (NEXTREG_NN/NEXTREG_A) issues two `out()` calls — first to 0x243B (clobbers `NextReg::selected_`), then 0x253B.
- **User impact**: software pattern (select R via 0x243B, do Z80N NEXTREG R',v inline pokes, then read 0x253B) round-trips R' instead of R.
- **Source ref**: Wave-2 NextREG (NEW-PD-1); reviewer APPROVE — high impact, SEL-05 already names this.
- **Coverage today**: SEL-05 in NEXTREG-TEST-PLAN-DESIGN.md defers to fuse_z80_test/z80n_test — coverage unverified.
- **Dependencies**: bypass port_dispatch in z80n_ext.cpp; call `nextreg_.write(reg, val)` directly. Faithfully bypasses gates per VHDL (CPU-internal path).
- **Effort**: L.
- Also relevant to section D.

### G152. Host F1/F4/F9/F10 hotkeys not wired to NMI source / reset [merged]
- **What**: VHDL `zxnext.vhd:6340-6349,6370-6371,2090-2091`: F1=hard reset, F4=soft reset, F9=hotkey_m1 (MF NMI), F10=hotkey_drive (DivMMC). jnext `gui/main_window.cpp:94-105` translates F1-F10 to SDL scancodes only — no NMI / reset consumer. Test injectors at `emulator.h:328-329` exist.
- **User impact**: user cannot trigger NextZXOS soft-reset (F4) / hard-reset (F1) / Multiface freeze (F9) / DivMMC button (F10) from keyboard.
- **Source ref**: Wave-2 nmi-boot (NEW-NMI-1, NEW-BOOT-2 — same gap, bundled).
- **Coverage today**: nmi_test rows HK-06/07/08/09 (Task 8 t1 closure 2026-04-28).
- **Closure (Task 8 t1)**: `Emulator::on_hotkey_f1_hard_reset / f4_soft_reset / f9_mf_nmi / f10_divmmc_nmi` dispatchers + `gui/main_window.cpp` keyPress/Release wiring. F4 honours `nr_03_config_mode` gate per VHDL:6370.
- **Dependencies**: distinct from G42 (joystick), G46 (boot ladder), G48 (MF).
- **Effort**: L.
- **Status (2026-05-03h)**: integration-test side closed. nmi_integration_test HK-06/07/08/09-INT PASS rows added — exercise end-to-end dispatch through the existing `on_hotkey_fN_*` API: F9 latches `nmi_source().nmi_mf()` and CPU services NMI (PC ∈ 0x0066-0x006F); F10 same with `port_divmmc_io_en` pre-opened; F4 with `nr_03_config_mode=0` is no-op (PC stays, reset_type unchanged) and with `nr_03_config_mode=1` advances reset_type 0b100→0b010 per VHDL `:1306`; F1 is ungated and triggers full hard-reset. Test-only commit (no src/ changes). Branch `g152-nmi-integration` (`6816a33`).

### G153. NR 0x02 reset_type[2:0] FSM and read-back missing [merged]
- **What**: VHDL `zxnext.vhd:1306,1732-1739,5891` — 3-bit shift register defaults `"100"` at power-on, advances `'0' & rt(2) & (rt(1) or rt(0))` on soft_reset rising edge. Bits 1:0 in NR 0x02 readback. Reviewer corrects: `:3319` consumer is SPI-Flash-CS, NOT DivMMC. jnext: `nmi_source.nr_02_read()` returns bits 3/2 only; reset_type permanently 0.
- **User impact**: tbblue.fw reset_type-conditional firmware paths take wrong branch; SPI-Flash-CS never armed at first power-on (latent — `--boot-rom` bypasses Flash path).
- **Source ref**: Wave-2 nmi-boot (NEW-NMI-2); reviewer APPROVE w/ revised framing.
- **Coverage today**: nmi_test rows RST-04, NR02-07, NR02-08 (Task 8 t1 closure 2026-04-28).
- **Closure (Task 8 t1)**: `NmiSource::reset_type_` modelled (init `"100"`, `strobe_soft_reset()` advances per VHDL:1736, saturates at `001`); surfaced via `nr_02_read()` bits 1:0 per VHDL:5891. NR 0x02 bit 0 write + `Emulator::on_hotkey_f4_soft_reset` strobe the FSM. FSM has no reset branch in VHDL — preserved across hard/soft reset by NOT touching `reset_type_` in `NmiSource::reset()`.
- **Dependencies**: distinct from G56/G62/G63.
- **Effort**: L.

### G154. NR 0x80-0x89 expbus / port-enable readbacks partial
- **What**: VHDL `zxnext.vhd:5508-5522,6138,6150,5061-5067` — NR 0x82-0x89 expose port-enable masks + reset_type bits via 0x253B reads. NR 0x89 bit 7 inverts (clear-to-0xFF when reset_type=0). jnext: NR 0x82-0x85 stored in regs_[]; NR 0x80/0x86-0x89 not initialised; only NR 0x85 has read handler.
- **User impact**: firmware reading NR 0x82-0x89 gets raw shadow byte, not VHDL packing `reset_type|"000"|enable[3:0]`.
- **Source ref**: Wave-2 nmi-boot (NEW-BOOT-1); reviewer APPROVE.
- **Coverage today**: superset of G56's enumerated NRs.
- **Dependencies**: per-register read_handlers + NR 0x89 inverted-reset semantics.
- **Effort**: M.

### G155. NEX loader doesn't honour ram_required field [closed]
- **Status: CLOSED 2026-04-28** (task8-t1-mmu) — `NexLoader::apply` rejects oversize NEX via inline `ram_required_kb` / `ram_required_fits` static helpers in `src/core/nex_loader.h`. Test rows BOOT-NEX-01/02 in `mmu_test` flipped to PASS.
- **What**: NEX V1.1+ spec — `ram_required` byte at offset 8 (0=768K/1=1792K/2=2048K). jnext `nex_loader.cpp:81` parses but no consumer; loader silently proceeds.
- **User impact**: NEX needing >installed RAM gets corrupted bank load instead of a clear error.
- **Source ref**: Wave-2 nmi-boot (NEW-BOOT-3); reviewer APPROVE w/ Display downgrade (default 2048 KB rarely triggers; warning fix is right shape, not display-band).
- **Coverage today**: BOOT-NEX-01/02 in `mmu_test`.
- **Dependencies**: distinct from G16.
- **Effort**: L.

### G157. Boot ROM overlay size mismatch silently truncates [closed]
- **Status: CLOSED 2026-04-28** (task8-t1-mmu) — `Mmu::set_boot_rom` materialises an 8 KB internal buffer with zero-pad/truncate + warn diagnostic on size mismatch. Test row BOOT-OVL-03 in `mmu_test` flipped to PASS.
- **What**: VHDL `zxnext.vhd:3199-3204` hardwires `cpu_a(12:0)` = 13-bit = 8 KB span. jnext `mmu.h:113-117` overlays only `addr < boot_rom_size_`; reads beyond fall through to next overlay (NOT real-hardware-faithful).
- **User impact**: edge case — wrong-sized custom boot ROM blob silently miscompiles, no diagnostic.
- **Source ref**: Wave-2 nmi-boot (NEW-BOOT-5); reviewer APPROVE.
- **Coverage today**: BOOT-OVL-03 in `mmu_test`.
- **Dependencies**: validate at load + clamp to 0x2000 with zero-fill.
- **Effort**: L.

### G158. SD card image hot-plug / unmount not exposed at runtime
- **What**: Real Next supports SD removal via CD/CS detect. `SdCardDevice::mount`/`unmount` exist but only one-time call at `emulator.cpp:2197-2200`; no GUI menu.
- **User impact**: user cannot swap SD images mid-session; common workflow gap.
- **Source ref**: Wave-2 nmi-boot (NEW-SD-1); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: 1 menu item + 1 emulator method.
- **Effort**: L.

### G162. NMI iotrap strobe consumed but never propagated to MF assert [merged]
- **What**: VHDL `zxnext.vhd:3835-3837` — `nmi_sw_gen_mf <= nmi_gen_nr_mf or nmi_gen_iotrap`. jnext `NmiSource::strobe_iotrap()` exists at `nmi_source.cpp:124-127` but `iotrap_strobe_pending_` is consumed and discarded at `:384` — never OR'd into MF assert. No port 0x2FFD/0x3FFD trap-decode handler. NR 0xD8 has zero handlers.
- **User impact**: +3 floppy-trap NMI path completely silent; FDC software targeting traps sees nothing.
- **Source ref**: Wave-2 nmi-boot (NEW-NMI-3); reviewer APPROVE — distinct from G55 (test-row gap only).
- **Coverage today**: nmi_test rows MF-G162-01, MF-G162-01b (companion gate row), MF-G162-02 (Task 8 t1 closure 2026-04-28).
- **Closure (Task 8 t1)**: (a) `iotrap_strobe_pending_` OR'd into `NmiSource::nmi_assert_mf()` with NR 0x06 bit 3 gate honoured. (b) NR 0xD8 bit 0 (`nr_d8_io_trap_fdc_en`) storage + handlers in Emulator. (c) port 0x2FFD/0x3FFD trap-decode handlers (mask `0xF003`) strobe `NmiSource::strobe_iotrap()` only when NR 0xD8 bit 0 is on; `port_2ffd_rd / port_3ffd_rd / port_3ffd_wr` follow VHDL:3835. NR 0xDA `nr_da_iotrap_cause` latch + NR 0x02 bit 4 readback remain unmodelled (see G55 expansion).
- **Dependencies**: pair with G55 broadening.
- **Effort**: L.

### G168. port_7ffd_reg vs port_7ffd_dat half-cycle phase
- **What**: VHDL splits 7FFD into two flip-flops — `port_7ffd_reg` latched at the write edge, `port_7ffd_dat` re-latched on the next clock for downstream consumers. jnext models a single synchronous value.
- **User impact**: Observable only at sub-instruction granularity (mid-instruction bus reads). Zero impact on per-instruction-correct boot trajectories. Latent on every test path that asserts at instruction boundaries.
- **Source ref**: Task 2 boot-critical audit Pass-14 catalogue (class-d), re-confirmed Pass-24/25.
- **Coverage today**: none — no test asserts mid-instruction phase.
- **Dependencies**: requires a CPU half-cycle execution model; same model unlocks G169 + G170 + G174.
- **Effort**: M (architectural — CPU clock model rework).

### G169. Generic VHDL `*_q` registered signals — half-cycle phase
- **What**: Many VHDL signals have a one-cycle delayed `_q` shadow used for edge detection. jnext computes most edges combinationally at instruction boundaries; the per-cycle `_q` register is collapsed.
- **User impact**: Same family as G168 — invisible at instruction granularity, observable only in cycle-precise stimulus.
- **Source ref**: Task 2 audit Pass-14 catalogue (class-d), re-confirmed Pass-24/25.
- **Coverage today**: none.
- **Dependencies**: same half-cycle CPU model as G168 + G170 + G174.
- **Effort**: M (multi-site once half-cycle model exists).

### G170. DivMMC automap `*_q` falling-edge sub-cycle pipeline (V12-DIVMMC-07)
- **What**: VHDL has falling-edge `divmmc_automap_*_q` shadows that gate the one-cycle automap overlay enable. jnext drops the overlay at the next M1 fetch (~3 i_CLK_28 later) vs the 1-clk VHDL behaviour.
- **User impact**: Functionally equivalent — overlay drops before the returned-to instruction in both jnext and VHDL. No observable divergence at instruction granularity.
- **Source ref**: Task 2 audit Pass-12 DivMMC (V12-DIVMMC-07), re-confirmed every subsequent pass.
- **Coverage today**: divmmc_test rows cover the instruction-granularity outcome; no sub-cycle row.
- **Dependencies**: half-cycle CPU model (shared with G168/G169/G174).
- **Effort**: M.

### G171. VHDL-impossible same-cycle Z80 OUT-OUT to DivMMC port pair (V12-DIVMMC-08)
- **What**: Two `OUT (port),A` instructions on consecutive ticks targeting the DivMMC port pair would require the port handler to accept back-to-back writes within a single cycle. jnext and VHDL both serialize via CPU edges; theoretical edge case where the second OUT could clobber mid-handler state if the model collapsed the gap.
- **User impact**: NextZXOS boot never issues OUT-OUT to the same DivMMC port pair within a single tick window. Theoretical only.
- **Source ref**: Task 2 audit Pass-12 DivMMC (V12-DIVMMC-08), re-confirmed every subsequent pass.
- **Coverage today**: none.
- **Dependencies**: same half-cycle CPU model.
- **Effort**: M.

### G172. SDHC vs SDSC dual-mode address translation (HCS gate) — V24-DIVMMC-02
- **What**: CMD17/18/24 in `sd_card.cpp:659/706/746` unconditionally multiply `cmd_arg()` by 512. SD spec § 4.7.4 says SDSC mode (HCS=0 in OCR per V17-DIVMMC-01) uses byte addresses, SDHC mode (HCS=1) uses block addresses.
- **User impact**: TBBlue / NextZXOS / FatFs always set HCS=1 → zero boot-path impact. Loading from a legacy SDSC card image would silently misaddress.
- **Source ref**: Task 2 audit Pass-24 DivMMC (V24-DIVMMC-02), Pass-25 re-verified.
- **Coverage today**: V17-DIVMMC-01 OCR CCS bit reflects HCS correctly; no end-to-end SDSC integration test.
- **Dependencies**: 5 architectural surfaces — CMD17/18/24 address gate + CMD16 length-reject + CMD9 CSD v1.0/v2.0 switch + CMD18 multi-block past-EOF byte math + new SDSC test fixtures.
- **Effort**: H.

### G173. DD/FD-prefixed Z80N opcode dispatch via XY_State (V15-CPU-NIT-01)
- **What**: jnext routes Z80N opcodes through the Alternate (EXX) decoder path; VHDL's t80n uses XY_State dispatch for DD/FD-prefixed Z80N. The dispatch shape differs structurally. Related but distinct from the G87 follow-up (which is about per-byte `on_m1_cycle` delivery during DD/FD/CB prefix advancement, not the Z80N opcode dispatch path itself).
- **User impact**: Zero — no software in the wild uses `DD ED <Z80N>` or `FD ED <Z80N>` sequences.
- **Source ref**: Task 2 audit Pass-15 CPU (V15-CPU-NIT-01), Pass-19/20/21/22/23/24/25 re-confirmed.
- **Coverage today**: none — defended as VHDL-faithful in spirit but not in dispatch path.
- **Dependencies**: Z80N decoder rework — multi-file refactor. Share half-cycle infrastructure with G87 DD/FD follow-up.
- **Effort**: M.

---

## D. Test / verification infrastructure

### G69. Traceability matrix structurally stale + extractor multi-file aware
- **What**: `doc/testing/TRACEABILITY-MATRIX.md` Summary still
  references "1796 plan rows / 1361 pass / 307 skip / 128 missing"
  while dashboard is 3326/3210/0/116 across 32 suites. ~10
  subsystems have no Summary row (NMI Source Pipeline,
  audio_nextreg, sd_card, compositor_integration, ula_integration,
  ctc_interrupts, floating_bus, videotiming, contention,
  nextreg_integration). Z80N row says `missing` for all 30 opcodes
  despite 85/85 in dashboard.
- **User impact**: any audit / theatre-detection / plan-drift check
  uses wrong numbers.
- **Source ref**: `doc/testing/TRACEABILITY-MATRIX.md:1-39`.
- **Proposed**: extend `refresh-traceability-matrix.pl` to (a) accept
  `subsystem → [test_files]` mapping, (b) recognise `// RE-HOME:`/
  `// COVERED AT`/`// TRACKED AT` comments, (c) accept sub-letter
  rows. Add Summary rows for the 10 missing suites. CI check on
  Summary-vs-binary drift.
- **Effort**: M.

### G70. Requirements DB (SQLite proposal — queued)
- **What**: `doc/design/REQUIREMENTS-DATABASE.md` proposes a SQLite
  `test/requirements.db` populated from plans + test source + VHDL
  citations, with priority/blocker tags + `comment-rehome`. Queued
  since 2026-04-20 behind SKIP-reduction.
- **User impact**: queries like "all skip rows blocking NextZXOS
  boot" remain grep-gymnastics. Plan + matrix + dashboard + skip-
  string + `.prompts/` backlog still drift vs each other.
- **Dependencies**: G69 extractor improvements; `// RE-HOME:` /
  `// COVERED AT` adoption across all 16 subsystems (currently only
  NextREG + ULA fully use it).
- **Effort**: M (v1) + M (v2).

### G71. `VideoTiming` pulse-counter surface is test-only dead code — CLOSED 2026-04-28 (task8-t1-videotiming)
- **What**: `src/video/timing.h:97-134` and `timing.cpp:53-94`
  exposed only via test getters; `Emulator` scheduler still owns
  `line_int_enabled_` / `line_int_value_` directly. Two state
  stores for one VHDL signal.
- **User impact**: Reframed by Task 6 audit (NEW-ULA-5 / NEW-ULA-6): the original
  "purely academic cleanup" classification is no longer accurate.
  User-visible bugs G106 (line-interrupt scheduler off-by-one +
  target=0 wrap not applied) and G107 (per-machine c_int_h/c_int_v
  ignored) are exactly the user-impact justification for routing the
  Emulator scheduler through `VideoTiming::next_int_pos()` /
  `int_line_num()`. Recommend Priority **Low → Medium**.
- **Proposed**: drop the two `Emulator` fields; route scheduler
  through `VideoTiming::next_int_pos()` / `set_line_interrupt_*`.
- **Cross-link**: G106 (off-by-one + target=0 wrap) and G107
  (per-machine fire offset) are the observable user-visible bugs the
  architectural cleanup also fixes. Land G106/G107 tactically by
  calling the helpers directly, then close G71 once the scheduler is
  fully rerouted through VideoTiming.
- **Effort**: M.

### G72. UART pin-7 / IoMode UART-mode injectors not fed at runtime
- **What**: 2026-04-24 input re-audit added UART-mode + injectors
  `set_uart0_tx`, `set_uart1_tx`, `set_joy_left_bit5`,
  `set_joy_right_bit5`. Production wiring is the documented follow-up.
- **Proposed**: Emulator per-tick callback pulling
  `Uart::tx_line()` and `Joystick::bit5_*()` into `IoMode`.
- **Effort**: L.

### G73. Audio I2S has zero runtime wiring
- **What**: `src/audio/i2s.{h,cpp}` exists as stub; only the test-
  programmatic `Emulator::i2s()` / `I2s::set_sample()` hook reaches
  it. No NextREG / port write routes data to it. Practical impact
  nil — no Z80 software uses it. Distinct from G29 (which is the
  source-side stub upgrade).
- **Effort**: L.
- **Status (2026-05-03f)**: CLOSED — landed jointly with G113 (single commit `043192a`, branch `g113-g73-nra2-i2s-gate`). Mixer's I2S contribution now reads `i2s_->pi_audio_L/R()` (gated by NR 0xA2 bits per VHDL `zxnext.vhd:2358-2359` upstream-of-mixer pattern), not raw `i2s_->left()/right()`. Test NR-43 (Mixer gates I2S contribution) closed (PASS) — set sample, toggle NR 0xA2 mute, observe pcm_L/R delta of 511 in 13-bit space (= 2044 in int16 after ×4 scaling). G29 (source-side stub) remains separate.

### G74. No CI pipeline; regression depends on dev discipline
- **What**: No `.github/` directory; verified absent.
  `EMULATOR-DESIGN-PLAN.md:1120,1137` flags CI integration TODO.
- **User impact**: visual regressions can slip past PR review (has
  happened before).
- **Proposed**: GitHub Actions Linux job with Qt6 + SDL +
  ImageMagick + xvfb; cache build dir; run unit-test + regression
  under xvfb. Pre-commit hook for matrix drift (G69).
- **Effort**: M.

### G75. Regression tolerance hard-zero; perceptual diff missing
- **What**: `test/regression.sh:21-22` uses `JNEXT_TEST_TOLERANCE=0`.
  No SSIM / ΔE perceptual diff. RZX has no roundtrip-replay
  regression. FFmpeg MP4 has no integrity test.
- **Proposed**: `JNEXT_TEST_PERCEPTUAL` toggle for SSIM /
  ΔE2000. Add `rzx-roundtrip` row that records fixed input, plays
  back, asserts byte-identical screenshot. Add `mp4-roundtrip`.
- **Effort**: M.

### G76. RZX determinism long-form regression
- **What**: RZX recording captures IN values + per-frame
  instruction count. No regression stresses long-form playback for
  drift — any subsystem with hidden host-time dep silently desyncs.
- **Dependencies**: G66 (RZX embeds SNA snapshot).
- **Proposed**: 30-sec RZX of known demo, re-play headless,
  screenshot at frame 1500 must match baseline byte-for-byte.
- **Effort**: M.

### G77. Reopened-suite skips: Compositor NR 0x68 + MMU shadow-screen
- **What**: Meta entry tracking the re-home pipeline. ULA Phase-4
  re-homed 5 rows back into Compositor / MMU plans (intentionally
  re-opening previously-green suites). Compositor NR 0x68 + MMU
  shadow-screen plans still PENDING (G58 is the runtime side).
  Closed plans: Floating Bus (2026-04-25), VideoTiming (2026-04-26),
  Contention Phase-1 (in-flight).
- **Source ref**: `feedback_rehome_to_owner_plan.md`;
  `EMULATOR-DESIGN-PLAN.md:1086-1117`.
- **Effort**: M each (tracked as G58 + this).

### G78. Agent worktree-stale-base launcher helper
- **What**: When launching ≥3 parallel Agent worktrees rapidly, some
  branch from a CACHED older main tip. Standing-mitigation: every
  parallel-agent prompt includes "rebase onto current main". Helper
  script would automate.
- **Source ref**: `feedback_agent_worktree_stale_base.md`.
- **Effort**: L (script helper); harness fix is upstream.

### G79. Test-output uniformity lint
- **What**: `Makefile` dashboard parses `Total: ... Passed: ...
  Failed: ... Skipped: ...` summary line. No lint / CI assertion
  enforces format on new suites.
- **Source ref**: `feedback_uniform_test_output.md`.
- **Proposed**: `test/lint-summary-line.sh` greps every `*_test.cpp`
  for required regex; runs in pre-commit + CI.
- **Effort**: L.

### G80. Headless-mode host-time leakage audit
- **What**: `--frames-instead-of-seconds` switch (commit `1e2f498`)
  decoupled one host-time leak. Other potential leaks (RTC, audio
  mixer sample-rate phases, joystick poll, mouse delta, video-rec
  wall-clock-stamping, RZX pacing) have no audit.
- **Proposed**: "deterministic mode" assertion under `--headless`
  panicking on `std::chrono::system_clock` / `gettimeofday` calls
  in subsystem code; convert to emulated / injected fixture clock.
- **Dependencies**: G76.
- **Effort**: M.

### G81. DEVELOPMENT-SESSIONS doc currency
- **What**: `doc/DEVELOPMENT-SESSIONS.md` ends at `24/4`. Sessions
  2026-04-25 (Floating Bus close, beast.nex resolved, splash bisect)
  and 2026-04-26 (VideoTiming close, Contention Phase-1 partial)
  not appended. Required current at version-bump per CLAUDE.md.
- **Effort**: L.

### G82. Z80N matrix Summary row cosmetic mismatch
- **What**: Z80N runs via FUSE-style data-driven runner;
  `UNIT-TEST-PLAN-EXECUTION.md` §6a marks Z80N permanently `missing`
  per row in the matrix. Today matrix shows `0 in-test, 30
  missing` while dashboard shows 85/85. Misleads anyone consulting
  the matrix.
- **Dependencies**: G69 extractor.
- **Proposed**: matrix Summary row keeps `Pass=85, Fail=0, Skip=0`
  from dashboard with footnote.
- **Effort**: L.

### G83. Profiling/benchmark mode + 400% speed bottleneck
- **What**: `doc/design/PROFILING-OPTIMIZATION-PLAN.md` written but
  unstarted. Phase A (`--benchmark N` + `--profile`) has zero
  deliverables. Concrete known regression: "400% speed only reaches
  ~75 FPS with 100% CPU instead of expected 200 FPS"
  (`EMULATOR-DESIGN-PLAN.md:1127`).
- **User impact**: speed-control >200% observably broken; no perf
  data to fix it. No perf regression detection in CI.
- **Effort**: M (Phase A only).

### G84. Integration-test design doc missing
- **What**: 8+ integration suites already exist but no unifying
  design doc covers cross-subsystem scenarios, fixture conventions,
  tier boundaries.
- **Source ref**: `EMULATOR-DESIGN-PLAN.md:1119`.
- **Proposed**: `doc/testing/INTEGRATION-TEST-PLAN-DESIGN.md`
  covering fixture shape (Emulator-fixture vs subsystem-fixture),
  tier boundaries, cross-subsystem scenarios.
- **Dependencies**: G70 Requirements DB tier column would surface
  duplicate / missing integration coverage.
- **Effort**: M.

### G85. Lint-baseline tautology coverage stops at substring match
- **What**: `test/lint-assertions.sh` rejects raw `check(x, true,
  ...)`, `|| true`, `a == b || a != b`. Baseline-locked classes
  not caught: `check(x, x, ...)` where `x` was just assigned by
  setter under test; equality between two outputs of same getter;
  `regs_[reg]` to itself via shadow-store path.
- **Proposed**: extend lint with libclang AST pass detecting
  `check(EXPR, EXPR, ...)` literal equality and `check(F(), F(),
  ...)` same-side-effect calls.
- **Effort**: M.

### G86. FEATURES.md "Accurate memory contention" overclaim
- **What**: `FEATURES.md:7` states "Accurate memory contention for
  48K, 128K, +3, and Pentagon timing models". Reality per G50:
  cycle-accurate contention is wrong on +3, Pentagon, Next turbo;
  +3 today runs the 48K pattern; ContentionModel::delay is not
  wired. Honest narrative gap with users.
- **User impact**: user expectation vs reality.
- **Proposed**: soften FEATURES.md text once G50 is honest; or
  qualify with "approximate (FUSE-table-based; full VHDL fidelity
  in progress)".
- **Effort**: L (text tweak); blocked on a defensible state.

### G149. NR write-only registers leak last-written byte on read (default → 0x00)
- **What**: VHDL `zxnext.vhd:5878-6289` read-mux `when others => port_253b_dat <= (others => '0');`. jnext `nextreg.cpp:101-110` returns `regs_[reg]` whenever no read_handler; write-only NRs (0x04, 0x29, 0x2A, 0x2B, 0x35-0x39, 0x60, 0x63, 0x75-0x79) leak the last-written byte. Distinct from G56 (which is composed-read divergence on NRs *with* read entries).
- **User impact**: software probing NR 0x04 / 0x60 / 0x63 / 0x29 etc. sees jnext "remembering" writes.
- **Source ref**: Wave-2 NextREG (NEW-NR-1); reviewer APPROVE.
- **Coverage today**: distinct from G56 (different defect class).
- **Dependencies**: gate on `has_read_entry_[]` mask OR uniform `return 0x00` lambda for write-only set.
- **Effort**: L.

### G159. SD card CRC validation absent (CMD0 0x95 hard-coded path)
- **What**: SD spec — card MUST validate CMD0 CRC; CMD59 toggles general CRC. jnext `sd_card.cpp:253-283` ignores `cmd_buf_[5]`; no CMD59 handler.
- **User impact**: nil for current NextZXOS / esxdos; future image tooling exercising CRC silently passes.
- **Source ref**: Wave-2 nmi-boot (NEW-SD-2); reviewer APPROVE.
- **Coverage today**: none.
- **Dependencies**: distinct from G40 (command coverage) / G41 (MMC).
- **Effort**: L.

### G160. SD CMD13 (SEND_STATUS) returns generic R1 fall-through, not R2
- **What**: SD spec — CMD13 returns R2 (2 bytes). jnext `sd_card.cpp:280-281` falls into `default:` and emits a single R1 byte; hosts wait for the second byte and hang.
- **User impact**: any host code probing card-status (`.cardinfo` etc) gets stuck.
- **Source ref**: Wave-2 nmi-boot (NEW-SD-3); reviewer APPROVE — G40 lists CMD13 but not the R2-vs-R1 shape distinction.
- **Coverage today**: none.
- **Dependencies**: distinct from G40.
- **Effort**: L.

---

## E. Cross-cutting dependencies

A simplified graph of the highest-impact dependencies:

```
G46 NextZXOS boot  ──── (a) G48 Multiface + RETN-alias proper fix
                  ├──── (b) RAM-test loop RE
                  ├──── (c) Logo + early-loader render gap
                  └──── G59 bypass path (mitigation, ships sooner)
                              └── G60 config.ini parser
                              └── G62/G63/G64 VHDL questions answered
G47 NextZXOS post-boot regression  ←── G46

G01 LoRes  ←── parallax.nex bringup
G01  ──── G02 NR 0x15 per-line replay
G01 + G02 ──── G17 parallax "two-copies" mystery

G03/G04/G05/G06/G07/G08/G09/G10/G11 per-scanline replays
   ←── all clones of TASK-PER-SCANLINE-PALETTE-PLAN (landed 2026-04-25)
G12 Nirvana memory-mux  ←── architectural; demo-driven plan needed

G50 Contention Phase-2 (in flight) ──── G51, G52, G53, G54
G50 ──── G86 FEATURES.md honesty

G66 Save-state schema versioning ──── G35 Save Snapshot menu
                                  ──── G67 rewind buffer assertion
                                  ──── G76 RZX long-form regression

G42 Joystick host wiring ←── pulls G43 mouse, G24 settings persist (mapping)
G44 issue-2 Beeper independent

G69 Traceability extractor ──── G70 Requirements DB ──── G82 Z80N row
G74 CI pipeline             ──── G69 + G79 lint

G36 TZX DeciLoad ──── G37 WAV DeciLoad

G48 Multiface ──── 8 NM rows + Copper ARB-06 + NR82-02 unblock
G56 NextReg shadow-store ──── G63 NR 0x03 latch
```

Active session work (Phase 2 contention) is concurrent with this
audit and lands part of G50 today.

---

## F. Suggested next-session priorities

If a user were to schedule the next 3 sessions, the highest-leverage
bundles are:

**Session-1 — NextZXOS boot, pragmatic path**
- G59 NextZXOS bypass-tbblue-fw Branches 1-3 (CLI + SRAM populate +
  synthetic RESET_SOFT). Ships value sooner than firmware-faithful
  fix. Mitigates G46(b) + G46(c) for the user-visible "give me a
  prompt" goal. Pre-work: G62/G63/G64 quick VHDL answers.

**Session-2 — Save-state safety + GUI persistence**
- G66 schema versioning (M, foundational; unblocks G35 + G76).
- G67 rewind-buffer assertion (L; cheap, deserves to ship with G66).
- G24 main-window settings persistence (L; every-launch user pain).
- G35 Save Snapshot (M; standard emulator feature). 4 items, 1
  session, broad user impact.

**Session-3 — Joystick / gamepad + LoRes**
- G42 Joystick / gamepad host wiring (M).
- G43 Kempston Mouse host wiring (M).
- G01 LoRes mode + scroll (M; unblocks parallax).
- G02 per-scanline NR 0x15 replay (L; unblocks parallax half).
  These four items unblock the most-frequently-noticed UX gaps
  (no-pad and no-LoRes) plus a parked investigation.

**Top-10 display-priority list** (revised 2026-04-27 per Task 6 — items
with display impact lead, ordered by Priority, then user-leverage):

1. **G46 NextZXOS boot ladder** — gates the entire NextZXOS UX *and*
   has a display-rendering gap (logo + early loader); display+UX.
2. **G106 Line-interrupt scheduler off-by-one + target=0 wrap** —
   line interrupts fire one full line late and target=0 misfires;
   high-impact display defect with cheap fix.
3. **G01 LoRes mode (NR 0x15 b7)** — unblocks parallax.nex and any
   LoRes demo; foundational for the parallax investigation.
4. **G91 NR 0x44 priority bits 7:6 dropped** — L2 palette-priority
   promotion is no-op despite passing tests; clean fix unlocks a
   documented L2-priority surface.
5. **G117 Copper executes per Z80 instr (parallax suspect)** —
   Copper bursts under-run when packed into a single Z80 instruction;
   plausible parallax.nex contributor, distinct from G65.
6. **G02 Per-scanline NR 0x15 replay** — Copper layer-splits flat
   without it; cheap (L) follow-on once G01 lands.
7. **G03 Per-scanline Layer 2 X/Y scroll replay** — L2 parallax
   renders flat; cheap log-pattern clone.
8. **G141 FUSE in-opcode contention macros inert** — M1 + no-MREQ
   contention dropped per opcode; 48K timing-driven demos (rasterbars,
   tape decoders) drift several T-states per contended opcode.
9. **G12 Nirvana-class memory-write multiplexers** — whole class of
   48K demoscene multicolour effects render wrong; large but plan now.
10. **G126 NR 0x05 mode change → MembraneStick** — non-display,
    but joystick mode switches silently leave membrane fold pinned;
    high-priority correctness bug for any joy-mode-switching software.

A non-display-priority alternative pick (preserved for reference): the
original list led with G46, G42, G33, G24, G66, G35, G34, G36, G01, G12
— display-leverage was already implicit in 4/10 items but not the
ranking criterion.

---

## Appendix A: Already-implemented (excluded from main list)

Items earlier docs flag as gaps but are verifiably closed (verified
by source / test / commit grep during this audit):

- **Per-scanline palette replay (NR 0x40/0x41/0x44)** — landed
  2026-04-25 (`TASK-PER-SCANLINE-PALETTE-PLAN.md`). Beast.nex sky
  gradient now correct.
- **Floating-bus subsystem** — closed 2026-04-25; 26→0 skips. +3
  port 0x0FFD surface live.
- **VideoTiming subsystem** — closed 2026-04-26; 22→0 skips,
  per-machine `int_position` + 60Hz toggle. **Section 7
  (production-scheduler wiring) closed 2026-04-28** on
  `task8-t1-videotiming` (Task 8 Wave 1) — G71 (drop
  `Emulator::line_int_enabled_/_value_` shadow), G106 (line-int
  off-by-one + target=0 wrap), G107 (frame-int per-machine
  c_int_h/c_int_v), G109 (NR 0x64 cu_offset in line-int compare).
  Suite now 27/27/0/0.
- **Contention Phase 1** — partial closure 2026-04-26; 28/68 rows
  live (Phase-A enable-gate + port_contend). G50 Phase-2 in flight
  in this session.
- **NMI source pipeline** — closed end-to-end 2026-04-24; 32 plan
  rows, 5 integration rows.
- **Beast.nex shadow-screen rendering** — RESOLVED 2026-04-25 via
  `vram_use_bank7_` wiring + per-scanline palette feature.
- **MMU page ≥0xE0 RAM-slot** (orig C15) — closed via floating-bus
  Phase 0 (commit `19ca74e`); `Mmu::rebuild_ptr` now nulls slot
  ≥2/page ≥0xE0, returns 0xFF, drops writes per VHDL.
- **MMU port-0x7FFD bit-3 shadow-screen** — P7F-16/17 landed live
  recently (commit `b8d3ea6`). Partial overlap with G58 — confirm
  residual scope.
- **Compositor UDIS-01/02/03** — landed 2026-04-24 (NR 0x68 bit 7
  ULA-en + bits 6:5 blend mode flat-frame).
- **`--delayed-screenshot-frames` CLI** — already present at
  `src/main.cpp:42,130` (orig A23 partly obsolete).
- **MD6 connector tick** — `md6_.tick(master_cycles)` IS called
  from `src/core/emulator.cpp:2746` (orig D07 was wrong; verified).
  D08 host-side feeder is still missing (folded into G42).
- **G140 Boot ROM 8 KB → 16 KB mirror** — closed 2026-04-28
  (task8-t1-mmu); `Mmu::read` gates `addr<0x4000` and masks
  `addr & 0x1FFF` per VHDL `zxnext.vhd:3199-3204`. mmu_test rows
  BOOT-OVL-01/02 PASS.
- **G155 NEX loader ram_required honoured** — closed 2026-04-28
  (task8-t1-mmu); `NexLoader::apply` rejects oversize NEX via inline
  `ram_required_kb` / `ram_required_fits` helpers in
  `src/core/nex_loader.h`. mmu_test rows BOOT-NEX-01/02 PASS.
- **G157 Boot ROM overlay size mismatch** — closed 2026-04-28
  (task8-t1-mmu); `Mmu::set_boot_rom` zero-pads / truncates to 8 KB
  with warn diagnostic. mmu_test row BOOT-OVL-03 PASS.
- **G148 port_dffd_reg_6 not stored** — closed 2026-04-28
  (task8-t1w2-mmu-v2); `Mmu` latches cpu_do(6) into a dedicated
  `port_dffd_reg_6_` flip-flop alongside the 5-bit `port_dffd_reg_`,
  per VHDL `zxnext.vhd:877,3686-3689,3694`. Accessor
  `port_dffd_reg_6()` ready for the future Multiface +3 read-mux
  (VHDL `:4314`). mmu_test row DFF-09 PASS.
- **G57 MMU `current_rom_bank()` three documented gaps** — closed
  2026-04-28 (task8-t1w2-mmu-v2); added `Mmu::sram_rom3()` accessor
  composing the VHDL `sram_rom3` signal per machine type with NR
  0x8C altrom locks factored in (`zxnext.vhd:2985,2990,3000,3004`).
  mmu_test rows ROM-10/11/12 PASS with discriminative coverage.

---

## Appendix B: WONT items (out of scope by user decision)

Explicit "we will NOT implement this" decisions, captured per the
WONT taxonomy (`feedback_wont_taxonomy.md`):

- **NextBUS expansion-bus emulation** (`expbus_en` / `expbus_speed`,
  zxnext.vhd:5816-5820) — `src/memory/contention.cpp:70-77`
  documents WONT. NMI plan also leaves `set_expbus_nmi_n()` stub.
  `hotkey_expbus_freeze` (zxnext.vhd:2166) inherits.
- **PS/2 keyboard / mouse protocol** — `EMULATOR-DESIGN-PLAN.md`
  §3.1 marks `ps2_*.vhd` as `no` — replaced by SDL/Qt key events.
- **AY I_SEL_L /16 clock divider unreachable** (B03) — VHDL hard-
  ties I_SEL_L='1' (`turbosound.vhd:164`); defensive only.
- **UART TX FIFO write-pulse edge detection** (B06) — sub-instruction
  granularity; impossible at Z80-instruction scope.
- **UART prescaler LSB readback** (BAUD-02/03) — write-only per VHDL.
- **DUAL-05 UART pin-routing assertion tautological** — pins don't
  exist in software.
- **DS1307 NVRAM 0x08-0x3F** — no NextZXOS firmware path uses it.
- **T-state-accurate mid-scanline mutation** — out-of-scope of
  per-scanline replay; would need sub-scanline interleave.
- **CTC NR-C5-04 bus-arbitration** (C25) — kept as `skip()` per user
  decision 2026-04-21; revisit as WONT once requirements DB lands.
- **UART RX framing/parity/break errors → CTC** — VHDL
  `serial/uart.vhd:391-392, 409-410` declares `o_Rx_*_err`/
  `o_Rx_*_err_break` outputs; `zxnext.vhd:3391-3392, 3409-3410` ties
  them `open` in the FPGA itself with comment "to ctc". The hardware
  does not connect them. Faithful emulation is to leave the wire
  open. (Source: Task 6 / NEW-UART-2; reviewer REVISE → WONT.)
- **DS1307 OUT / SQWE / RS[1:0] control bits** — DS1307 datasheet
  §"Control Register" defines bit 7=OUT, bit 4=SQWE, bits[1:0]=RS[1:0]
  driving an SQW/OUT pin. Real Next does not route the SQW/OUT pin to
  the FPGA (only SDA/SCL pinned to `i2c_int_*`); bits have no
  observable effect. jnext stores the byte verbatim for read-back via
  `i2c.cpp:78-80` default branch — that is sufficient. Same
  justification class as the existing DS1307 NVRAM 0x08-0x3F WONT.
  (Source: Task 6 / NEW-RTC-1; reviewer APPROVE WONT.)
- **ExpBus NMI latch gates `expbus_eff_en` / `expbus_eff_disable_mem`**
  — VHDL `zxnext.vhd:2089` `nmi_assert_expbus <= '1' when expbus_eff_en
  = '1' and expbus_eff_disable_mem = '0' and i_BUS_NMI_n = '0'`.
  jnext `NmiSource::nmi_assert_expbus` (`nmi_source.cpp:169-179`)
  models only `i_BUS_NMI_n`; the upstream gates are not consulted.
  Inherits the existing NextBUS WONT (`expbus_en` / `expbus_speed` /
  `set_expbus_nmi_n` stub / `hotkey_expbus_freeze`); this entry
  closes the latch-side coverage hole on the same WONT lineage.
  (Source: Task 6 / NEW-NMI-6; reviewer APPROVE WONT.)
- **NR 0x06 bit 2 (`nr_06_ps2_mode`) write-side storage** — VHDL
  `zxnext.vhd:1111, 5167-5169` stores under `nr_03_config_mode='1'`.
  PS/2 keyboard / mouse protocol is already WONT (existing Appendix B
  line). Inherits the protocol WONT but flagged here so that future
  NR-readback tests can assert the bit IS round-trippable through
  `regs_[]` — this is a documented sub-correctness gap, not a
  protocol implementation. (Source: Task 6 / NEW-PER-2; reviewer
  REVISE → fold into PS/2 WONT bullet with caveat.)

---

## Appendix C: Source coverage / methodology

Of 116 raw entries authored by the 4 parallel section agents
(A: 32, B: 26, C: 32, D: 26):

- **Already-implemented / re-classified out**: 9 (per-scanline
  palette, Floating Bus, VideoTiming, NMI pipeline, beast.nex
  resolution, MMU page ≥0xE0, MMU shadow P7F-16/17, Compositor
  UDIS-01/02/03, `--delayed-screenshot-frames`).
- **Already test-covered (full or substantial)**: A04 per-frame L2
  scroll has live rows; G50 Contention Phase-1 already has 28/68
  live rows (gap is the residual). These are kept as gap entries
  describing the residual.
- **Cross-section duplicates merged**:
  - C20 (save-state SCHEMA_VERSION) + D01 (D01 same) + D02 (field
    order) → **G66**.
  - B25 NextZXOS boot + C02 RAM-test + C03 missing-logo + C17
    bypass-tbblue-fw → **G46** (firmware-faithful root) **+ G59**
    (bypass mitigation) — split because they ship in different
    sessions. C18 config.ini → G60 (downstream of G59).
  - C01 RETN-alias band-aid + C04 Multiface stub + C19 NMI MF stubs
    + C24 enNextMf.rom load → **G48**.
  - C13 NextReg shadow-store + D11 same → **G56**. C29 NR 0x03
    latch is the NR-0x03-specific subset → **G63**.
  - C28 NR 0x03 config_mode preservation question kept separate as
    G62 because it answers a Q1 of the bypass plan.
  - D07 MD6 tick gap **dropped** — verified MD6 IS tick'd in
    production (`emulator.cpp:2746`). The host-side feeder for MD6
    is folded into G42.
  - D08 Joystick GUI feeder folded into G42.
  - A11 ULA+ wiring audit folded into G11 (NR 0x68 other bits).
  - C32 `.z80` snapshot loader + B12 same → **G34**.
  - B16 DSK + FDC and the implicit "+3 tape→disk" → **G38**.
  - D16 + D17 reopened-suite skips → **G77** (with G58 as runtime
    side).
- **Final unique gap entries** (after Task 6 expansion 2026-04-27):
  **162** (A: 56, B: 39, C: 46, D: 21, with cross-references for
  multi-letter Cat values).
- **Active session note**: G50 contention `delay()` Phase-2 is
  in-flight in this session; entry describes the residual after
  Phase 2 lands. G77 / G58 reopen notes assume Compositor NR 0x68
  + MMU shadow-screen plans are still in their "PENDING" state.

The 4 section drafts at
`/home/jorgegv/src/spectrum/jnext/.claude/worktrees/agent-{aa8d6e16,
a3be1106,afc8d8a5,ab323d9f}/doc/issues/gaps-T4-{A,B,C,D}-*.md` are
the authoritative source for citations not surfaced here.
