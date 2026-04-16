# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | Clean. FUSE data-driven opcode test suite              |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | Clean. All opcodes verified against VHDL               |
| Copper                |       66 |       66 |      0 |      10 |    100% | Clean. Skips: NR 0x64 offset, cycle-accurate bus, NMI  |
| Memory/MMU            |       66 |       66 |      0 |      77 |    100% | Clean. Skips: contention timing, DivMMC overlay, altrom|
| NextREG               |       17 |       17 |      0 |      47 |    100% | Clean. Skips: defaults owned by subsystem handlers     |
| Input (Keyboard)      |       23 |       23 |      0 |     126 |    100% | Clean. Skips: joystick, mouse, ext keyboard not in C++ |
| CTC + Interrupts      |       44 |       43 |      1 |     106 |     98% | 1 fail: ch3->ch0 ZC/TO daisy-chain wrap not implemented|
| Layer 2               |       89 |       89 |      0 |       8 |    100% | Clean. Skips: SRAM +1 transform (emulator layout)      |
| UART + I2C/RTC        |       58 |       58 |      0 |      48 |    100% | Clean. Skips: RTC BCD clock-dependent, register pointer|
| DivMMC + SPI          |       67 |       64 |      3 |      56 |     96% | 3 fail: SPI MISO pipeline delay parked (SD boot risk)  |
| Sprites               |      115 |      115 |      0 |      10 |    100% | Clean. Skips: overtime, border-clip, anchor-H latch    |
| Compositor            |      114 |       99 |     15 |       0 |     87% | 15 fail: need test-side L2 priority, border, stencil   |
| ULA Video             |       48 |       48 |      0 |      75 |    100% | Clean. Skips: contention, hi-res modes, interrupt edge |
| I/O Port Dispatch     |       80 |       78 |      2 |       6 |     98% | 2 fail: +3 ROM-high MMU bug, 0xDF combinatorial route  |
| Audio (AY+DAC+Beeper) |      127 |      127 |      0 |      73 |    100% | Clean. Skips: DAC channel enables, stereo routing      |
| DMA                   |      121 |      121 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| Tilemap               |       51 |       51 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| **Total**             | **2527** | **2506** | **21** | **642** | **99%** |                                                        |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.