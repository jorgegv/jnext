# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | All tests pass.                                        |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | All tests pass.                                        |
| Rewind                |       18 |       18 |      0 |       0 |    100% | All tests pass.                                        |
| Copper                |       76 |       76 |      0 |       0 |    100% | All tests pass.                                        |
| Memory/MMU            |      150 |      150 |      0 |       0 |    100% | All tests pass.                                        |
| NextREG (bare)        |       21 |       21 |      0 |       0 |    100% | All tests pass.                                        |
| NextREG (integration) |       73 |       73 |      0 |       0 |    100% | All tests pass.                                        |
| Input                 |      139 |      139 |      0 |       0 |    100% | All tests pass.                                        |
| Input (integration)   |       11 |       11 |      0 |       0 |    100% | All tests pass.                                        |
| CTC + Interrupts      |      129 |      129 |      0 |       0 |    100% | All tests pass.                                        |
| CTC (integration)     |       12 |       12 |      0 |       0 |    100% | All tests pass.                                        |
| Layer 2               |       89 |       89 |      0 |       0 |    100% | All tests pass.                                        |
| UART + I2C/RTC        |       92 |       92 |      0 |       0 |    100% | All tests pass.                                        |
| UART (integration)    |       12 |       12 |      0 |       0 |    100% | All tests pass.                                        |
| DivMMC + SPI          |      100 |      100 |      0 |       0 |    100% | All tests pass.                                        |
| SD Card               |        8 |        8 |      0 |       0 |    100% | All tests pass.                                        |
| Sprites               |      121 |      121 |      0 |       0 |    100% | All tests pass.                                        |
| Compositor            |      117 |      114 |      0 |       3 |    100% | 3 UDIS skips: integration+Copper+BL blockers.          |
| ULA Video             |       81 |       81 |      0 |       0 |    100% | All tests pass.                                        |
| ULA Video (int)       |        8 |        8 |      0 |       0 |    100% | All tests pass.                                        |
| Floating Bus          |       26 |        0 |      0 |      26 |    100% | Scaffold suite; 26 skips awaiting implementation.      |
| VideoTiming           |       22 |        0 |      0 |      22 |    100% | Scaffold suite; 22 skips awaiting implementation.      |
| Contention            |       68 |        0 |      0 |      68 |    100% | Scaffold suite; 68 skips awaiting implementation.      |
| I/O Port Dispatch     |       83 |       83 |      0 |       0 |    100% | All tests pass.                                        |
| Audio (AY+DAC+Beeper) |      132 |      132 |      0 |       0 |    100% | All tests pass.                                        |
| Audio (NextREG)       |       25 |       25 |      0 |       0 |    100% | All tests pass.                                        |
| Audio (port dispatch) |       16 |       16 |      0 |       0 |    100% | All tests pass.                                        |
| DMA                   |      150 |      150 |      0 |       0 |    100% | All tests pass.                                        |
| Tilemap               |       59 |       59 |      0 |       0 |    100% | All tests pass.                                        |
| NMI Source Pipeline   |       32 |       32 |      0 |       0 |    100% | All tests pass.                                        |
| NMI (integration)     |        5 |        5 |      0 |       0 |    100% | All tests pass.                                        |
| **Total**             | **3316** | **3197** |  **0** | **119** | **100%**|                                                        |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
