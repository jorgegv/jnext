# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | 5 bugs found and fixed                                 |
| Copper                |       66 |       66 |      0 |      10 |    100% | 10 integration-level rows deferred                     |
| Memory/MMU            |       66 |       66 |      0 |      77 |    100% | Contention, DivMMC overlay, altrom out of scope        |
| NextREG               |       17 |       17 |      0 |      47 |    100% | 47 rows belong to palette/MMU/Copper/config subsystems |
| Input (Keyboard)      |       23 |       23 |      0 |     126 |    100% | NR 0x05/0x0B reset defaults fixed                      |
| CTC + Interrupts      |       44 |       43 |      1 |     106 |     98% | ch3->ch0 ZC/TO wrap-around not implemented             |
| Layer 2               |       89 |       89 |      0 |       8 |    100% | SRAM +1 transform skipped (emulator layout differs)    |
| UART + I2C/RTC        |       58 |       58 |      0 |      48 |    100% | I2C false-STOP fixed, RTC BCD deferred                 |
| DivMMC + SPI          |       67 |       64 |      3 |      56 |     96% | SPI pipeline delay parked (SD boot risk)               |
| Sprites               |      115 |      115 |      0 |      10 |    100% | G4.XY-04 removed (impossible oracle)                   |
| Compositor            |      114 |       99 |     15 |       0 |     87% | L2 priority, border exception, stencil, sprite_en      |
| ULA Video             |       48 |       48 |      0 |      75 |    100% | Frame timing fixed                                     |
| I/O Port Dispatch     |       80 |       78 |      2 |       6 |     98% | Exclusive dispatch, VHDL decode, NR gating             |
| Audio (AY+DAC+Beeper) |      127 |      127 |      0 |      73 |    100% | AY envelope, Turbosound panning fixed                  |
| DMA                   |      121 |      121 |      0 |       0 |    100% | block_len=0, prescaler fixes merged                    |
| Tilemap               |       51 |       51 |      0 |       0 |    100% | Control-bit swap, rotate, 512-tile fixes merged        |
| **Total**             | **1171** | **1150** | **21** | **642** | **98%** |                                                        |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.