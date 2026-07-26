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
| Copper                |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper (integration)  |        3 |        3 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU            |      250 |      250 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU (int)      |       59 |       59 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (bare)        |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (integration) |      301 |      301 |      0 |       0 |    100% | 🟢 All tests pass. |
| esxDOS stub           |       46 |       46 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input                 |      323 |      323 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input (integration)   |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Phantom Typist        |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC + Interrupts      |      132 |      132 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC (integration)     |       48 |       48 |      0 |       0 |    100% | 🟢 All tests pass. |
| Layer 2               |      134 |      134 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART + I2C/RTC        |      100 |      100 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART (integration)    |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| DivMMC + SPI          |      146 |      146 |      0 |       0 |    100% | 🟢 All tests pass. |
| Multiface (core)      |       55 |       55 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card               |       45 |       45 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD ROM Extractor      |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| FAT32 Image           |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card Provisioner   |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| Sprites               |      209 |      209 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor            |      228 |      228 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor (int)      |        8 |        8 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video             |      122 |      122 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video (int)       |       14 |       14 |      0 |       0 |    100% | 🟢 All tests pass. |
| Floating Bus          |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| VideoTiming           |       37 |       37 |      0 |       0 |    100% | 🟢 All tests pass. |
| Contention            |      118 |      118 |      0 |       0 |    100% | 🟢 All tests pass. |
| I/O Port Dispatch     |      111 |      111 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (AY+DAC+Beeper) |      138 |      138 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (NextREG)       |       33 |       33 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (port dispatch) |       23 |       23 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (pacing)        |       43 |       43 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (capture)       |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (host gain)     |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (subsystem gains) |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| Present cadence       |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| Render-skip policy    |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Emulator Boot         |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (Apply Policy) |       20 |       20 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Window Attach |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Pointer Capture       |       12 |       12 |      0 |       0 |    100% | 🟢 All tests pass. |
| Frame-deadline scheduler |       38 |       38 |      0 |       0 |    100% | 🟢 All tests pass. |
| Frame-tick sequencer (wiring) |      103 |      103 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tick-delivery stats   |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging               |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging (gate)        |        3 |        3 |      0 |       0 |    100% | 🟢 All tests pass. |
| CLI options / docs    |       13 |       13 |      0 |       0 |    100% | 🟢 All tests pass. |
| Video recorder (ffmpeg cmd) |       33 |       33 |      0 |       0 |    100% | 🟢 All tests pass. |
| NEX loader (screen ingest) |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Extended NEX streaming |       28 |       28 |      0 |       0 |    100% | 🟢 All tests pass. |
| DMA                   |      156 |      156 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap               |       69 |       69 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap raster splits |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| LoRes                 |       48 |       48 |      0 |       0 |    100% | 🟢 All tests pass. |
| LoRes (integration)   |        2 |        2 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI Source Pipeline   |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI (integration)     |        9 |        9 |      0 |       0 |    100% | 🟢 All tests pass. |
| Atic Atac NMI         |        4 |        4 |      0 |       0 |    100% | 🟢 All tests pass. |
| Profiler              |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Resume Guard          |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (AppConfig) |       52 |       52 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio gain configuration |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio gain Preferences |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Present count (widget) |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Esc/BREAK + fullscreen routing |        6 |        6 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (Apply) |       27 |       27 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Video Panel  |       87 |       87 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Audio Panel  |       15 |       15 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Quit Gate    |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| **Total**             | **5688** | **5688** |  **0** |   **0** | **100%**| 🟢 All tests pass. |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
