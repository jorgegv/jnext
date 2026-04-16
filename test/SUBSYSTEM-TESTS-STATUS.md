# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| Z80N CPU              |       78 |       78 |      0 |       0 |    100% | 5 bugs found and fixed                                 |
| Copper                |       66 |       66 |      0 |      10 |    100% | 10 integration-level rows deferred                     |
| Memory/MMU            |       66 |       66 |      0 |      77 |    100% | Contention, DivMMC overlay, altrom out of scope        |
| NextREG               |       17 |       17 |      0 |      47 |    100% | 47 rows belong to palette/MMU/Copper/config subsystems |
| Input (Keyboard)      |       23 |       21 |      2 |     252 |     91% | NR 0x05/0x0B reset defaults missing                    |
| CTC + Interrupts      |       44 |       43 |      1 |     106 |     98% | ch3->ch0 ZC/TO wrap-around not implemented             |
| Layer 2               |       95 |       89 |      6 |       2 |     94% | SRAM bank +1 transform, multi-bank page source         |
| UART + I2C/RTC        |       58 |       58 |      0 |      48 |    100% | I2C false-STOP, RTC BCD/register-pointer deferred      |
| DivMMC + SPI          |       67 |       53 |     14 |      56 |     79% | SPI pipeline delay, SS collapse, mapram latch          |
| Sprites               |      116 |      115 |      1 |      10 |     99% | y_msb at y=256 boundary                                |
| Compositor            |      114 |       88 |     26 |       0 |     77% | Transparency, stencil, L2 priority, border exceptions  |
| ULA Video             |       48 |       47 |      1 |      75 |     98% | Contention/timing/interrupt rows out of scope          |
| I/O Port Dispatch     |       80 |       62 |     18 |       6 |     78% | Kempston, Pentagon, +3 ROM, AY gating                  |
| Audio (AY+DAC+Beeper) |      127 |      121 |      6 |      73 |     95% | AY envelope hold, Turbosound panning, DAC enable       |
| DMA                   |      121 |      117 |      4 |       0 |     97% | block_len=0, prescaler in continuous mode              |
| Tilemap               |       51 |       38 |     13 |       0 |     75% | Strip map row offset, textmode pixel/index addressing  |
| **Total**             | **1171** | **1079** | **92** | **762** | **92%** |                                                        |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.