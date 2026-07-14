# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | 🟢 All tests pass. |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | 🟢 All tests pass. |
| Rewind                |       35 |       25 |      0 |      10 |     71% | 🟡 Task 7r2: G66/G67 SS-VER + RB-FRAME skips added. |
| Copper                |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper (integration)  |        3 |        3 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU            |      250 |      250 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU (int)      |       59 |       59 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (bare)        |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (integration) |      280 |      280 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input                 |      161 |      161 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input (integration)   |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC + Interrupts      |      132 |      132 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC (integration)     |       30 |       30 |      0 |       0 |    100% | 🟢 All tests pass. |
| Layer 2               |      134 |      134 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART + I2C/RTC        |       98 |       98 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART (integration)    |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| DivMMC + SPI          |      140 |      140 |      0 |       0 |    100% | 🟢 All tests pass. |
| Multiface (core)      |       49 |       49 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card               |       38 |       38 |      0 |       0 |    100% | 🟢 All tests pass. |
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
| DMA                   |      150 |      150 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap               |       69 |       69 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI Source Pipeline   |       57 |       57 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI (integration)     |        9 |        9 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Video Panel  |       83 |       83 |      0 |       0 |    100% | 🟢 All tests pass. |
| **Total**             | **4338** | **4328** |  **0** |  **10** | **99%** | 🟡 10 SKIPs total, all Rewind (Task 13a / G66+G67 snapshot schema); 0 FAILs. Task 57 (2026-07-14) closed every mmu/nmi/port skip. |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
