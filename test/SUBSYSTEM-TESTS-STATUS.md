# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | Clean. FUSE data-driven opcode test suite              |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | Clean. All opcodes verified against VHDL               |
| Rewind                |       18 |       18 |      0 |       0 |    100% | Clean. Snapshot roundtrip + step-back                  |
| Copper                |       66 |       66 |      0 |      10 |    100% | Clean. Skips: NR 0x64 offset, cycle-accurate bus, NMI  |
| Memory/MMU            |       66 |       66 |      0 |      77 |    100% | Clean. Skips: contention timing, DivMMC overlay, altrom|
| NextREG (bare)        |       17 |       17 |      0 |      47 |    100% | Clean. Skips: defaults owned by subsystem handlers     |
| NextREG (integration) |       11 |       11 |      0 |       1 |    100% | Clean. 1 skip: RST-09 clip read cycling                |
| Input (Keyboard)      |       23 |       23 |      0 |     126 |    100% | Clean. Skips: joystick, mouse, ext keyboard not in C++ |
| CTC + Interrupts      |       44 |       44 |      0 |     106 |    100% | Clean. Skips: IM2 fabric, pulse, ULA-INT, NR 0xC0-0xCE|
| Layer 2               |       89 |       89 |      0 |       8 |    100% | Clean. Skips: SRAM +1 transform (emulator layout)      |
| UART + I2C/RTC        |       58 |       58 |      0 |      48 |    100% | Clean. Skips: RTC BCD clock-dependent, register pointer|
| DivMMC + SPI          |       67 |       67 |      0 |      56 |    100% | Clean. Skips: automap edge cases, ROM overlay paths    |
| Sprites               |      115 |      115 |      0 |      10 |    100% | Clean. Skips: overtime, border-clip, anchor-H latch    |
| Compositor            |      114 |      114 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| ULA Video             |       48 |       48 |      0 |      75 |    100% | Clean. Skips: contention, hi-res modes, interrupt edge |
| I/O Port Dispatch     |       80 |       80 |      0 |       6 |    100% | Clean. Skips: Pentagon 0xDFFD, expbus gating           |
| Audio (AY+DAC+Beeper) |      127 |      127 |      0 |      73 |    100% | Clean. Skips: DAC channel enables, stereo routing      |
| DMA                   |      121 |      121 |      0 |      35 |    100% | Clean. Skips: burst/continuous timing, edge cases      |
| Tilemap               |       51 |       51 |      0 |      18 |    100% | Clean. Skips: stencil mode, UDG, below-ULA priority    |
| **Total**             | **2556** | **2556** |  **0** | **696** | **100%**|                                                        |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
