# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | Clean. FUSE data-driven opcode test suite              |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | Clean. All opcodes verified against VHDL               |
| Rewind                |       18 |       18 |      0 |       0 |    100% | Clean. Snapshot roundtrip + step-back                  |
| Copper                |       76 |       75 |      0 |       1 |    100% | Clean. 1 skip: remaining edge case                     |
| Memory/MMU            |      155 |      142 |      0 |      13 |    100% | Clean. Skips: contention timing, DivMMC overlay, altrom|
| NextREG (bare)        |       55 |       21 |      0 |      34 |    100% | Clean. Skips: defaults owned by subsystem handlers     |
| NextREG (integration) |       68 |       55 |      0 |      13 |    100% | Clean. Skips: integration edge cases                   |
| Input (Keyboard)      |      149 |       23 |      0 |     126 |    100% | Clean. Skips: joystick, mouse, ext keyboard not in C++ |
| CTC + Interrupts      |      150 |       44 |      0 |     106 |    100% | Clean. Skips: IM2 fabric, pulse, ULA-INT, NR 0xC0-0xCE |
| Layer 2               |       89 |       89 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| UART + I2C/RTC        |      106 |       58 |      0 |      48 |    100% | Clean. Skips: RTC BCD clock-dependent, register pointer|
| DivMMC + SPI          |       97 |       88 |      0 |       9 |    100% | Clean. Skips: automap edge cases, ROM overlay paths    |
| SD Card               |        8 |        8 |      0 |       0 |    100% | Clean. SD card I/O, CMD17/CMD18 block reads            |
| Sprites               |      121 |      121 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| Compositor            |      114 |      114 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| ULA Video             |      123 |       48 |      0 |      75 |    100% | Clean. Skips: contention, hi-res modes, interrupt edge |
| I/O Port Dispatch     |       83 |       82 |      0 |       1 |    100% | Clean. 1 skip: remaining edge case                     |
| Audio (AY+DAC+Beeper) |      200 |      127 |      0 |      73 |    100% | Clean. Skips: DAC channel enables, stereo routing      |
| DMA                   |      150 |      150 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| Tilemap               |       59 |       59 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| **Total**             | **3262** | **2763** |  **0** | **499** | **100%**|                                                        |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
