# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | 🟢 All tests pass. |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | 🟢 All tests pass. |
| CPU INT pulse         |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| CPU/Z80N IM2 regr.    |       56 |       56 |      0 |       0 |    100% | 🟢 All tests pass. |
| Rewind                |      308 |      308 |      0 |       0 |    100% | 🟢 All tests pass. |
| Rewind call stack     |       20 |       20 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper                |       83 |       83 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper (integration)  |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU            |      265 |      265 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU (int)      |      124 |      124 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (bare)        |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (integration) |      379 |      379 |      0 |       0 |    100% | 🟢 All tests pass. |
| esxDOS stub           |      185 |      185 |      0 |       0 |    100% | 🟢 All tests pass. |
| esxDOS stub (symlinks, POSIX) |        6 |        6 |      0 |       0 |    100% | 🟢 All tests pass. |
| quick_snapshot_posix_test |        2 |        2 |      0 |       0 |    100% | 🟢 All tests pass. ⚠ TODO: add "quick_snapshot_posix_test" to the label map in refresh-subsystem-status.sh. |
| Input                 |      391 |      391 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input (integration)   |       46 |       46 |      0 |       0 |    100% | 🟢 All tests pass. |
| Phantom Typist        |       23 |       23 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC + Interrupts      |      134 |      134 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC (integration)     |       88 |       88 |      0 |       0 |    100% | 🟢 All tests pass. |
| Layer 2               |      174 |      174 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART + I2C/RTC        |      104 |      104 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART (integration)    |       42 |       42 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART (POSIX-only rows) |       49 |       49 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextPi launcher + TCP link (all OSes) |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 socket transport |      246 |      246 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 socket SIGPIPE (POSIX) |        2 |        2 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 AT command engine |      499 |      499 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 jnext UART adapter |       30 |       30 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 jnext policy + wiring |      127 |      127 |      0 |       0 |    100% | 🟢 All tests pass. |
| DivMMC + SPI          |      154 |      154 |      0 |       0 |    100% | 🟢 All tests pass. |
| divmmc_integration_test |        6 |        6 |      0 |       0 |    100% | 🟢 All tests pass. ⚠ TODO: add "divmmc_integration_test" to the label map in refresh-subsystem-status.sh. |
| Multiface (core)      |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card               |       87 |       87 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD ROM Extractor      |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD File Add (GH #269) |      150 |      150 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD File Add (POSIX-only rows) |       13 |       13 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD File Add (case-sensitive host) |        1 |        1 |      0 |       0 |    100% | 🟢 All tests pass. |
| FAT32 Image           |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card Provisioner   |      139 |      139 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card Provisioner (POSIX-only rows) |        2 |        2 |      0 |       0 |    100% | 🟢 All tests pass. |
| Warm start (GH #234)  |       43 |       43 |      0 |       0 |    100% | 🟢 All tests pass. |
| Snapshot container + descriptor (.jns, GH #27) |      299 |      299 |      0 |       0 |    100% | 🟢 All tests pass. |
| Snapshot SD identity (.jns, GH #27 S7) |       37 |       37 |      0 |       0 |    100% | 🟢 All tests pass. |
| Sprites               |      212 |      212 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor            |      246 |      246 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor (int)      |       51 |       51 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video             |      152 |      152 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video (int)       |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Floating Bus          |       59 |       59 |      0 |       0 |    100% | 🟢 All tests pass. |
| VideoTiming           |      113 |      113 |      0 |       0 |    100% | 🟢 All tests pass. |
| Contention            |      162 |      162 |      0 |       0 |    100% | 🟢 All tests pass. |
| I/O Port Dispatch     |      132 |      132 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (AY+DAC+Beeper) |      162 |      162 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (Pi I2S source, POSIX) |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (NextREG)       |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (port dispatch) |       23 |       23 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (pacing)        |       58 |       58 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (device fill)   |       39 |       39 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (capture)       |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (host gain)     |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (subsystem gains) |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| Present cadence       |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| Render-skip policy    |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Screenshot output (PNG/.SCR) |       24 |       24 |      0 |       0 |    100% | 🟢 All tests pass. |
| Emulator Boot         |      117 |      117 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (Apply Policy) |       20 |       20 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Attach |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Pointer Capture       |       94 |       94 |      0 |       0 |    100% | 🟢 All tests pass. |
| Frame-deadline scheduler |       47 |       47 |      0 |       0 |    100% | 🟢 All tests pass. |
| Frame-tick sequencer (wiring) |      116 |      116 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tick-delivery stats   |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Achieved-speed report |       36 |       36 |      0 |       0 |    100% | 🟢 All tests pass. |
| Host key minimum-hold latch |      174 |      174 |      0 |       0 |    100% | 🟢 All tests pass. |
| SDL Pointer Release (GH #365) |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| SDL window title (version) |        3 |        3 |      0 |       0 |    100% | 🟢 All tests pass. |
| SIGINT/SIGTERM quit request |       14 |       14 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging               |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging (gate)        |       27 |       27 |      0 |       0 |    100% | 🟢 All tests pass. |
| CLI options / docs    |       43 |       43 |      0 |       0 |    100% | 🟢 All tests pass. |
| Video recorder (ffmpeg cmd) |       33 |       33 |      0 |       0 |    100% | 🟢 All tests pass. |
| NEX loader (screen ingest) |      147 |      147 |      0 |       0 |    100% | 🟢 All tests pass. |
| NEX loader (V1.3)     |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| Extended NEX streaming |       44 |       44 |      0 |       0 |    100% | 🟢 All tests pass. |
| TAP loader (container) |       12 |       12 |      0 |       0 |    100% | 🟢 All tests pass. |
| TZX loader (container) |       91 |       91 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tape SAVE TZX/WAV (GH #89) |       59 |       59 |      0 |       0 |    100% | 🟢 All tests pass. |
| Snapshot IM latch (NR 0xC0) |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| snapshot_machine_test |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. ⚠ TODO: add "snapshot_machine_test" to the label map in refresh-subsystem-status.sh. |
| POK trainers (.pok, GH #381) |       61 |       61 |      0 |       0 |    100% | 🟢 All tests pass. |
| DMA                   |      185 |      185 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap               |      113 |      113 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap raster splits |       12 |       12 |      0 |       0 |    100% | 🟢 All tests pass. |
| LoRes                 |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| LoRes (integration)   |        2 |        2 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI Source Pipeline   |       77 |       77 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI (integration)     |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Atic Atac NMI         |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| Raw binary --inject   |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Profiler              |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Resume Guard          |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| src/debug Qt-free lint (GH #278) |       13 |       13 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Step Out     |       50 |       50 |      0 |       0 |    100% | 🟢 All tests pass. |
| Source maps + symbols |       53 |       53 |      0 |       0 |    100% | 🟢 All tests pass. |
| Source debugging (backend) |       80 |       80 |      0 |       0 |    100% | 🟢 All tests pass. |
| Source debugging (POSIX file types) |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Backend (GH #276) |     1469 |     1469 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger persistent BPs |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger I/O Watchpoints |       25 |       25 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger BP Enable/Disable |       23 |       23 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger resume step-off |       19 |       19 |      0 |       0 |    100% | 🟢 All tests pass. |
| Raster State (beam + ULA fetch) |       86 |       86 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger socket transport (GH #287) |       69 |       69 |      0 |       0 |    100% | 🟢 All tests pass. |
| DZRP remote debugger adapter (GH #12) |      162 |      162 |      0 |       0 |    100% | 🟢 All tests pass. |
| GDB RSP remote debugger adapter (GH #281) |       98 |       98 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger DSL front end (GH #26) |      260 |      260 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger DSL evaluator (GH #26) |       60 |       60 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger DSL engine (GH #26) |      123 |      123 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger session recorder (GH #20) |       35 |       35 |      0 |       0 |    100% | 🟢 All tests pass. |
| ZRCP remote debugger adapter (GH #280) |      216 |      216 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (AppConfig) |      161 |      161 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio gain configuration |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio gain Preferences |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Mouse Release Gesture (GH #365) |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Present count (widget) |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| frame_timer_test      |        6 |        6 |      0 |       0 |    100% | 🟢 All tests pass. ⚠ TODO: add "frame_timer_test" to the label map in refresh-subsystem-status.sh. |
| ESP-01 status cell (GUI) |       15 |       15 |      0 |       0 |    100% | 🟢 All tests pass. |
| NEX V1.3 GUI warning dialog |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| POK Trainers GUI dialog (GH #381) |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| RZX Menus             |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI load-failure dialogs |       24 |       24 |      0 |       0 |    100% | 🟢 All tests pass. |
| Esc/BREAK + fullscreen routing |        6 |        6 |      0 |       0 |    100% | 🟢 All tests pass. |
| Host hotkeys on Alt (Ctrl to guest) |       72 |       72 |      0 |       0 |    100% | 🟢 All tests pass. |
| Main-window menu mnemonics |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| Main-window Pause (Alt+U, Pause key) |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Joystick controller picker (Input menu, Preferences) |       14 |       14 |      0 |       0 |    100% | 🟢 All tests pass. |
| Shifted symbols reach the guest |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Window scale + fullscreen geometry |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Main-window title (version) |        6 |        6 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI light/dark scheme (GH #363) |       29 |       29 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD-card/NextPi download dialog (GH #358) |        9 |        9 |      0 |       0 |    100% | 🟢 All tests pass. |
| Quit runs closeEvent cleanup |        8 |        8 |      0 |       0 |    100% | 🟢 All tests pass. |
| Toolbar SVG icon set + icon lint |       28 |       28 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (Apply) |       85 |       85 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Video Panel  |      118 |      118 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Audio Panel  |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Quit Gate    |        8 |        8 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger persist. BP (GUI) |       14 |       14 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Inspection Reads |       18 |       18 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Sizing |       36 |       36 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Growing |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Accelerators |        8 |        8 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Key Bindings |       39 |       39 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Menus        |       53 |       53 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Disasm Copy  |       39 |       39 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Panels (GH #278) |       85 |       85 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Verbs (GH #278) |       75 |       75 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Source panel |       68 |       68 |      0 |       0 |    100% | 🟢 All tests pass. |
| **Total**             | **13547**| **13547**|  **0** |   **0** | **100%**| 🟢 All tests pass. |

**SKIP:** A row its suite could not exercise on the host that ran it. A SKIP fails the run (owner, 2026-10-06). A planned row whose feature does not exist yet is PLANNED in its `*-TEST-PLAN-DESIGN.md`, not a SKIP.
