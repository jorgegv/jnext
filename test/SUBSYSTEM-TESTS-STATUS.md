# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | 🟢 All tests pass. |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | 🟢 All tests pass. |
| CPU INT pulse         |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| CPU/Z80N IM2 regr.    |       56 |       56 |      0 |       0 |    100% | 🟢 All tests pass. |
| Rewind                |      252 |      252 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper                |       82 |       82 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper (integration)  |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU            |      252 |      252 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU (int)      |       68 |       68 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (bare)        |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (integration) |      349 |      349 |      0 |       0 |    100% | 🟢 All tests pass. |
| esxDOS stub           |      159 |      159 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input                 |      344 |      344 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input (integration)   |       30 |       30 |      0 |       0 |    100% | 🟢 All tests pass. |
| Phantom Typist        |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC + Interrupts      |      134 |      134 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC (integration)     |       88 |       88 |      0 |       0 |    100% | 🟢 All tests pass. |
| Layer 2               |      153 |      153 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART + I2C/RTC        |      103 |      103 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART (integration)    |       50 |       50 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 socket transport |      188 |      188 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 AT command engine |      344 |      344 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 jnext UART adapter |       30 |       30 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 jnext policy + wiring |      101 |      101 |      0 |       0 |    100% | 🟢 All tests pass. |
| DivMMC + SPI          |      148 |      148 |      0 |       0 |    100% | 🟢 All tests pass. |
| divmmc_integration_test |        6 |        6 |      0 |       0 |    100% | 🟢 All tests pass. ⚠ TODO: add "divmmc_integration_test" to the label map in refresh-subsystem-status.sh. |
| Multiface (core)      |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card               |       87 |       87 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD ROM Extractor      |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD File Add (GH #269) |       72 |       72 |      0 |       0 |    100% | 🟢 All tests pass. |
| FAT32 Image           |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card Provisioner   |       64 |       64 |      0 |       0 |    100% | 🟢 All tests pass. |
| Warm start (GH #234)  |       39 |       39 |      0 |       0 |    100% | 🟢 All tests pass. |
| Snapshot container + descriptor (.jns, GH #27) |      295 |      295 |      0 |       0 |    100% | 🟢 All tests pass. |
| Snapshot SD identity (.jns, GH #27 S7) |       37 |       37 |      0 |       0 |    100% | 🟢 All tests pass. |
| Sprites               |      212 |      212 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor            |      237 |      237 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor (int)      |       50 |       50 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video             |      136 |      136 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video (int)       |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Floating Bus          |       59 |       59 |      0 |       0 |    100% | 🟢 All tests pass. |
| VideoTiming           |       64 |       64 |      0 |       0 |    100% | 🟢 All tests pass. |
| Contention            |      160 |      160 |      0 |       0 |    100% | 🟢 All tests pass. |
| I/O Port Dispatch     |      132 |      132 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (AY+DAC+Beeper) |      160 |      160 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (NextREG)       |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (port dispatch) |       23 |       23 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (pacing)        |       50 |       50 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (device fill)   |       39 |       39 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (capture)       |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (host gain)     |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (subsystem gains) |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| Present cadence       |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| Render-skip policy    |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Screenshot output (PNG/.SCR) |       24 |       24 |      0 |       0 |    100% | 🟢 All tests pass. |
| Emulator Boot         |       67 |       67 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (Apply Policy) |       20 |       20 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Attach |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Pointer Capture       |       12 |       12 |      0 |       0 |    100% | 🟢 All tests pass. |
| Frame-deadline scheduler |       44 |       44 |      0 |       0 |    100% | 🟢 All tests pass. |
| Frame-tick sequencer (wiring) |      112 |      112 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tick-delivery stats   |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Achieved-speed report |       36 |       36 |      0 |       0 |    100% | 🟢 All tests pass. |
| Host key minimum-hold latch |       69 |       69 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging               |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging (gate)        |       27 |       27 |      0 |       0 |    100% | 🟢 All tests pass. |
| CLI options / docs    |       19 |       19 |      0 |       0 |    100% | 🟢 All tests pass. |
| Video recorder (ffmpeg cmd) |       33 |       33 |      0 |       0 |    100% | 🟢 All tests pass. |
| NEX loader (screen ingest) |      147 |      147 |      0 |       0 |    100% | 🟢 All tests pass. |
| NEX loader (V1.3)     |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| Extended NEX streaming |       44 |       44 |      0 |       0 |    100% | 🟢 All tests pass. |
| TAP loader (container) |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| TZX loader (container) |       91 |       91 |      0 |       0 |    100% | 🟢 All tests pass. |
| Snapshot IM latch (NR 0xC0) |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| DMA                   |      160 |      160 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap               |       88 |       88 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap raster splits |       12 |       12 |      0 |       0 |    100% | 🟢 All tests pass. |
| LoRes                 |       48 |       48 |      0 |       0 |    100% | 🟢 All tests pass. |
| LoRes (integration)   |        2 |        2 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI Source Pipeline   |       77 |       77 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI (integration)     |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Atic Atac NMI         |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| Raw binary --inject   |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Profiler              |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Resume Guard          |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Step Out     |       50 |       50 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger persistent BPs |       18 |       18 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger I/O Watchpoints |       25 |       25 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger BP Enable/Disable |       23 |       23 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger resume step-off |       19 |       19 |      0 |       0 |    100% | 🟢 All tests pass. |
| Raster State (beam + ULA fetch) |       86 |       86 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (AppConfig) |       66 |       66 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio gain configuration |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio gain Preferences |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Present count (widget) |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 status cell (GUI) |       15 |       15 |      0 |       0 |    100% | 🟢 All tests pass. |
| NEX V1.3 GUI warning dialog |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| RZX Menus             |       15 |       15 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI load-failure dialogs |       15 |       15 |      0 |       0 |    100% | 🟢 All tests pass. |
| Esc/BREAK + fullscreen routing |        6 |        6 |      0 |       0 |    100% | 🟢 All tests pass. |
| Host hotkeys on Alt (Ctrl to guest) |       37 |       37 |      0 |       0 |    100% | 🟢 All tests pass. |
| Main-window menu mnemonics |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| Shifted symbols reach the guest |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Window scale + fullscreen geometry |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Quit runs closeEvent cleanup |        7 |        7 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (Apply) |       53 |       53 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Video Panel  |      106 |      106 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Audio Panel  |       15 |       15 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Quit Gate    |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger persist. BP (GUI) |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Inspection Reads |       18 |       18 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Sizing |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Growing |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Accelerators |        8 |        8 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Menus        |       45 |       45 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Disasm Copy  |       33 |       33 |      0 |       0 |    100% | 🟢 All tests pass. |
| **Total**             | **8783** | **8783** |  **0** |   **0** | **100%**| 🟢 All tests pass. |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
