# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | 🟢 All tests pass. |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | 🟢 All tests pass. |
| CPU INT pulse         |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| CPU/Z80N IM2 regr.    |       52 |       52 |      0 |       0 |    100% | 🟢 All tests pass. |
| Rewind                |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper                |       82 |       82 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper (integration)  |        7 |        7 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU            |      251 |      251 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU (int)      |       59 |       59 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (bare)        |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (integration) |      313 |      313 |      0 |       0 |    100% | 🟢 All tests pass. |
| esxDOS stub           |       46 |       46 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input                 |      334 |      334 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input (integration)   |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Phantom Typist        |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC + Interrupts      |      132 |      132 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC (integration)     |       54 |       54 |      0 |       0 |    100% | 🟢 All tests pass. |
| Layer 2               |      134 |      134 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART + I2C/RTC        |      100 |      100 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART (integration)    |       25 |       25 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 socket transport |      188 |      188 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 AT command engine |      269 |      269 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 jnext UART adapter |       30 |       30 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 jnext policy + wiring |       62 |       62 |      0 |       0 |    100% | 🟢 All tests pass. |
| DivMMC + SPI          |      146 |      146 |      0 |       0 |    100% | 🟢 All tests pass. |
| Multiface (core)      |       55 |       55 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card               |       50 |       50 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD ROM Extractor      |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| FAT32 Image           |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card Provisioner   |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| Sprites               |      209 |      209 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor            |      236 |      236 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor (int)      |        8 |        8 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video             |      122 |      122 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video (int)       |       14 |       14 |      0 |       0 |    100% | 🟢 All tests pass. |
| Floating Bus          |       37 |       37 |      0 |       0 |    100% | 🟢 All tests pass. |
| VideoTiming           |       43 |       43 |      0 |       0 |    100% | 🟢 All tests pass. |
| Contention            |      127 |      127 |      0 |       0 |    100% | 🟢 All tests pass. |
| I/O Port Dispatch     |      115 |      115 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (AY+DAC+Beeper) |      140 |      140 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (NextREG)       |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (port dispatch) |       23 |       23 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (pacing)        |       43 |       43 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (capture)       |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (host gain)     |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (subsystem gains) |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| Present cadence       |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| Render-skip policy    |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Emulator Boot         |       31 |       31 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (Apply Policy) |       20 |       20 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Attach |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Pointer Capture       |       12 |       12 |      0 |       0 |    100% | 🟢 All tests pass. |
| Frame-deadline scheduler |       38 |       38 |      0 |       0 |    100% | 🟢 All tests pass. |
| Frame-tick sequencer (wiring) |      103 |      103 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tick-delivery stats   |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Achieved-speed report |       36 |       36 |      0 |       0 |    100% | 🟢 All tests pass. |
| Host key minimum-hold latch |       69 |       69 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging               |       13 |       13 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging (gate)        |        3 |        3 |      0 |       0 |    100% | 🟢 All tests pass. |
| CLI options / docs    |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| Video recorder (ffmpeg cmd) |       33 |       33 |      0 |       0 |    100% | 🟢 All tests pass. |
| NEX loader (screen ingest) |       98 |       98 |      0 |       0 |    100% | 🟢 All tests pass. |
| NEX loader (V1.3)     |       78 |       78 |      0 |       0 |    100% | 🟢 All tests pass. |
| Extended NEX streaming |       28 |       28 |      0 |       0 |    100% | 🟢 All tests pass. |
| DMA                   |      157 |      157 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap               |       72 |       72 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap raster splits |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| LoRes                 |       48 |       48 |      0 |       0 |    100% | 🟢 All tests pass. |
| LoRes (integration)   |        2 |        2 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI Source Pipeline   |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI (integration)     |        9 |        9 |      0 |       0 |    100% | 🟢 All tests pass. |
| Atic Atac NMI         |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| Profiler              |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Resume Guard          |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Step Out     |       50 |       50 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger persistent BPs |       18 |       18 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (AppConfig) |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio gain configuration |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio gain Preferences |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Present count (widget) |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| ESP-01 status cell (GUI) |       15 |       15 |      0 |       0 |    100% | 🟢 All tests pass. |
| Esc/BREAK + fullscreen routing |        6 |        6 |      0 |       0 |    100% | 🟢 All tests pass. |
| Host hotkeys on Alt (Ctrl to guest) |       33 |       33 |      0 |       0 |    100% | 🟢 All tests pass. |
| Main-window menu mnemonics |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| Shifted symbols reach the guest |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Quit runs closeEvent cleanup |        7 |        7 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (Apply) |       40 |       40 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Video Panel  |       92 |       92 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Audio Panel  |       15 |       15 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Quit Gate    |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger persist. BP (GUI) |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Sizing |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Growing |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Accelerators |        8 |        8 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Menus        |       20 |       20 |      0 |       0 |    100% | 🟢 All tests pass. |
| **Total**             | **6838** | **6838** |  **0** |   **0** | **100%**| 🟢 All tests pass. |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
