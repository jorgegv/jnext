# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | All tests pass.                                        |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | All tests pass.                                        |
| Rewind                |       18 |       18 |      0 |       0 |    100% | All tests pass.                                        |
| Copper                |       81 |       76 |      0 |       5 |     93% | Task 7: G116/G117/G118 SKIPs added.                    |
| Memory/MMU            |      150 |      137 |      0 |      13 |     91% | Task 7: G140/G143/G146/G148/G155/G156/G157 SKIPs.      |
| NextREG (bare)        |       31 |       21 |      0 |      10 |     67% | Task 7: G149/G151/G154 SKIPs added.                    |
| NextREG (integration) |       73 |       73 |      0 |       0 |    100% | All tests pass.                                        |
| Input                 |      148 |      139 |      0 |       9 |     93% | Task 7: G126/G127/G128/G129/G130/G132/G133 SKIPs.      |
| Input (integration)   |       13 |       11 |      0 |       2 |     84% | Task 7: G126/G147 integration SKIPs.                   |
| CTC + Interrupts      |      132 |      129 |      0 |       3 |     97% | Task 7: G119/G120/G121 SKIPs added.                    |
| CTC (integration)     |       21 |       12 |      0 |       9 |     57% | Task 7: G87/G88/G89/G90 SKIPs added.                   |
| Layer 2               |      105 |      100 |      0 |       5 |     95% | Task 7: G91/G92/G144/G145 SKIPs added.                 |
| UART + I2C/RTC        |       98 |       92 |      0 |       6 |     93% | Task 7: G134/G135/G138/G139/G161 SKIPs added.          |
| UART (integration)    |       13 |       12 |      0 |       1 |     92% | Task 7: G135 SKIP added.                               |
| DivMMC + SPI          |      108 |      100 |      0 |       8 |     92% | Task 7: G123/G124/G125/G131/G136/G137 SKIPs.           |
| SD Card               |       12 |        8 |      0 |       4 |     66% | Task 7: G158/G159/G160 SKIPs added.                    |
| Sprites               |      173 |      168 |      0 |       5 |     97% | Task 7: G95/G96/G97 SKIPs added.                       |
| Compositor            |      134 |      130 |      0 |       4 |     97% | Task 7: G93/G108 SKIPs added.                          |
| Compositor (int)      |        2 |        2 |      0 |       0 |    100% | All tests pass.                                        |
| ULA Video             |       85 |       82 |      0 |       3 |     96% | Task 7: G104/G105/G150 SKIPs added.                    |
| ULA Video (int)       |       11 |        9 |      0 |       2 |     81% | Task 7: G102/G103 SKIPs added.                         |
| Floating Bus          |       32 |       32 |      0 |       0 |    100% | All tests pass.                                        |
| VideoTiming           |       26 |       22 |      0 |       4 |     84% | Task 7: G106/G107/G109 SKIPs added.                    |
| Contention            |       73 |       68 |      0 |       5 |     93% | Task 7: G141/G142 SKIPs added.                         |
| I/O Port Dispatch     |       83 |       83 |      0 |       0 |    100% | All tests pass.                                        |
| Audio (AY+DAC+Beeper) |      134 |      132 |      0 |       2 |     98% | Task 7: G115 SKIPs added.                              |
| Audio (NextREG)       |       32 |       25 |      0 |       7 |     78% | Task 7: G110/G111/G112/G113 SKIPs added.               |
| Audio (port dispatch) |       21 |       16 |      0 |       5 |     76% | Task 7: G114 SKIPs added.                              |
| DMA                   |      152 |      150 |      0 |       2 |     98% | Task 7: G122 SKIPs added.                              |
| Tilemap               |       64 |       59 |      0 |       5 |     92% | Task 7: G98/G99/G100/G101 SKIPs added.                 |
| NMI Source Pipeline   |       42 |       32 |      0 |      10 |     76% | Task 7: G88/G125-cross/G152/G153/G162 SKIPs added.     |
| NMI (integration)     |        9 |        5 |      0 |       4 |     55% | Task 7: G152 integration SKIPs added.                  |
| **Total**             | **3517** | **3384** |  **0** | **133** |     96% |                                                        |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.

**Task 7 (2026-04-27) — 76 newly identified gaps (G87-G162) added as SKIP rows.**
Subsystems re-open with rates < 100% reflecting the audit; no FAILs.
Pre-Task 7 baseline was 3336/3336/0/0 (zero skips).
Task 7 added 133 new skip rows across 27 subsystems for the
KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md gap audit (G87-G162). No `src/`
modifications; pure plan-doc + skip-stub additions.
