# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | 🟢 All tests pass. |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | 🟢 All tests pass. |
| CPU INT pulse         |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| CPU/Z80N IM2 regr.    |       52 |       52 |      0 |       0 |    100% | 🟢 All tests pass. |
| Rewind                |       66 |       66 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper                |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper (integration)  |        3 |        3 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU            |      250 |      250 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU (int)      |       59 |       59 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (bare)        |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (integration) |      284 |      284 |      0 |       0 |    100% | 🟢 All tests pass. |
| esxDOS stub           |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input                 |      198 |      198 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input (integration)   |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| Phantom Typist        |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC + Interrupts      |      132 |      132 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC (integration)     |       45 |       45 |      0 |       0 |    100% | 🟢 All tests pass. |
| Layer 2               |      134 |      134 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART + I2C/RTC        |      100 |      100 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART (integration)    |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| DivMMC + SPI          |      146 |      146 |      0 |       0 |    100% | 🟢 All tests pass. |
| Multiface (core)      |       55 |       55 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card               |       38 |       38 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD ROM Extractor      |       26 |       26 |      0 |       0 |    100% | 🟢 All tests pass. |
| FAT32 Image           |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card Provisioner   |       46 |       46 |      0 |       0 |    100% | 🟢 All tests pass. |
| Sprites               |      197 |      197 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor            |      196 |      196 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor (int)      |        8 |        8 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video             |      106 |      106 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video (int)       |       12 |       12 |      0 |       0 |    100% | 🟢 All tests pass. |
| Floating Bus          |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| VideoTiming           |       37 |       37 |      0 |       0 |    100% | 🟢 All tests pass. |
| Contention            |       97 |       97 |      0 |       0 |    100% | 🟢 All tests pass. |
| I/O Port Dispatch     |      106 |      106 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (AY+DAC+Beeper) |      138 |      138 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (NextREG)       |       33 |       33 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (port dispatch) |       23 |       23 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (pacing)        |       22 |       22 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging               |       10 |       10 |      0 |       0 |    100% | 🟢 All tests pass. |
| Logging (gate)        |        3 |        3 |      0 |       0 |    100% | 🟢 All tests pass. |
| DMA                   |      150 |      150 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap               |       69 |       69 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI Source Pipeline   |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI (integration)     |        9 |        9 |      0 |       0 |    100% | 🟢 All tests pass. |
| Profiler              |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| Resume Guard          |       11 |       11 |      0 |       0 |    100% | 🟢 All tests pass. |
| GUI Preferences (AppConfig) |       45 |       45 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Video Panel  |       83 |       83 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Audio Panel  |       15 |       15 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Quit Gate    |        5 |        5 |      0 |       0 |    100% | 🟢 All tests pass. |
| **Total**             | **4765** | **4765** |  **0** |   **0** | **100%**| 🟢 All tests pass. |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
