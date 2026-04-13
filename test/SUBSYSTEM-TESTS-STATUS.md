# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |    Tests |     Pass |    Fail |    Rate | Notes                                                       |
|-----------------------|---------:|---------:|--------:|--------:|-------------------------------------------------------------|
| Z80N CPU              |       78 |       78 |       0 |    100% | 5 bugs found and fixed                                      |
| Copper                |       69 |       69 |       0 |    100% | 6 integration-level tests deferred                          |
| Memory/MMU            |       83 |       79 |       4 |     95% | ROM slot register value, +3 special config 3, L2 write-over |
| NextREG               |       63 |       49 |      14 |     78% | Reset defaults not initialized to VHDL values               |
| Input (Keyboard)      |       71 |       71 |       0 |    100% |                                                             |
| CTC + Interrupts      |       49 |       48 |       1 |     98% | ch3->ch0 ZC/TO wrap-around not implemented                  |
| Layer 2               |       61 |       61 |       0 |    100% |                                                             |
| UART + I2C/RTC        |       69 |       60 |       9 |     87% | I2C protocol sequencing, RTC register reads                 |
| DivMMC + SPI          |       76 |       72 |       4 |     95% | mapram OR-latch, bits 5:4 mask, 0x3Dxx automap              |
| Sprites               |       48 |       48 |       0 |    100% |                                                             |
| Compositor            |       74 |       74 |       0 |    100% |                                                             |
| ULA Video             |      109 |       90 |      19 |     83% | Screen address formula, attribute paper encoding            |
| I/O Port Dispatch     |       78 |       78 |       0 |    100% |                                                             |
| Audio (AY+DAC+Beeper) |      100 |       94 |       6 |     94% | AY envelope hold, Turbosound default panning                |
| DMA                   |       94 |       59 |      35 |     63% | Transfer execution, port B address programming              |
| Tilemap               |       57 |       49 |       8 |     86% | Tile rendering, scrolling verification                      |
| **Total**             | **1179** | **1079** | **100** | **92%** |                                                             |
