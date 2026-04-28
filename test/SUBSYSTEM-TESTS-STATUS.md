# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | All tests pass.                                        |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | All tests pass.                                        |
| Rewind                |       28 |       18 |      0 |      10 |     64% | Task 7r2: G66/G67 SS-VER + RB-FRAME skips added.       |
| Copper                |       82 |       76 |      0 |       6 |     93% | Task 7r2: G65 ARB-G65-01 tied-edge added.              |
| Memory/MMU            |      178 |      137 |      0 |      41 |     77% | Task 7r2: G12/G16/G33-38/G57/G58 added.                |
| NextREG (bare)        |       63 |       21 |      0 |      42 |     33% | Task 7r2: G55/G56/G62/G64 (FT/CR/CFG/BYPASS-Q).        |
| NextREG (integration) |       74 |       73 |      0 |       1 |     99% | Task 7r2: G63 CFG-09-INT added.                        |
| Input                 |      152 |      139 |      0 |      13 |     91% | Task 7r2: G43/G72 (MOUSE-13..15 + IOMODE-11A).         |
| Input (integration)   |       17 |       11 |      0 |       6 |     65% | Task 7r2: G42/G44 (JOY-WIRE-02..04 + FE-04A).          |
| CTC + Interrupts      |      133 |      129 |      0 |       4 |     97% | Task 7r2: G49 NR-C0-02 promoted from WONT comment.     |
| CTC (integration)     |       21 |       12 |      0 |       9 |     57% | Unchanged (Task 7 round 1 baseline).                   |
| Layer 2               |      120 |      115 |      0 |       5 |     96% | Task 8 t1: G91/G02/G05/G09/G14 closed (driver-side).   |
| UART + I2C/RTC        |      102 |       92 |      0 |      10 |     90% | Task 7r2: G39 ESP-01..04 added.                        |
| UART (integration)    |       13 |       12 |      0 |       1 |     92% | Unchanged (Task 7 round 1 baseline).                   |
| DivMMC + SPI          |      110 |      107 |      0 |       3 |     97% | Task 8 t1: G123/G124/G125/G131/G137 closed (-7).       |
| SD Card               |       21 |        8 |      0 |      13 |     38% | Task 7r2: G40/G41 SD-10..15 + MMC-01..03 added.        |
| Sprites               |      178 |      168 |      0 |      10 |     94% | Task 7r2: G06/G13/G15 OVF + NR70 stubs added.          |
| Compositor            |      143 |      130 |      0 |      13 |     91% | Task 7r2: G04/G11/G26/G27 (PSCAN/UB/BLANK).            |
| Compositor (int)      |        2 |        2 |      0 |       0 |    100% | All tests pass.                                        |
| ULA Video             |       98 |       82 |      0 |      16 |     84% | Task 7r2: G07/G08/G10 (S5-PSL/S9-PSL/S17.x).           |
| ULA Video (int)       |       11 |        9 |      0 |       2 |     81% | Unchanged (Task 7 round 1 baseline).                   |
| Floating Bus          |       32 |       32 |      0 |       0 |    100% | All tests pass.                                        |
| VideoTiming           |       27 |       22 |      0 |       5 |     81% | Task 7r2: G71 VT-26 walkback added.                    |
| Contention            |       76 |       68 |      0 |       8 |     89% | Task 7r2: G50/G51/G53 (CT-DELAY/TURBO-08/FUSE-05).     |
| I/O Port Dispatch     |       87 |       83 |      0 |       4 |     95% | Task 7r2: G45 EXPBUS-AND-01..04 added.                 |
| Audio (AY+DAC+Beeper) |      141 |      132 |      0 |       9 |     93% | Task 7r2: G29/G30/G31 (MX-30 + AY-30..34 + SD-09).     |
| Audio (NextREG)       |       33 |       25 |      0 |       8 |     75% | Task 7r2: G73 NR-43 mixer-gate added.                  |
| Audio (port dispatch) |       21 |       16 |      0 |       5 |     76% | Unchanged (Task 7 round 1 baseline).                   |
| DMA                   |      152 |      150 |      0 |       2 |     98% | Unchanged (Task 7 round 1 baseline).                   |
| Tilemap               |       69 |       59 |      0 |      10 |     85% | Task 7r2: G06 TM-160..164 mode-flip rows added.        |
| NMI Source Pipeline   |       55 |       32 |      0 |      23 |     58% | Task 7r2: G46/G47/G48/G55/G59/G60 (BOOT/MF-G48).       |
| NMI (integration)     |        9 |        5 |      0 |       4 |     55% | Unchanged (Task 7 round 1 baseline).                   |
| **Total**             | **3684** | **3384** |  **0** | **300** |     92% |                                                        |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.

**Task 7 (2026-04-27) — 76 newly identified gaps (G87-G162) added as SKIP rows.**
Subsystems re-open with rates < 100% reflecting the audit; no FAILs.
Pre-Task 7 baseline was 3336/3336/0/0 (zero skips).
Task 7 round 1 added 133 new skip rows across 27 subsystems for the
KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md gap audit (G87-G162). No `src/`
modifications; pure plan-doc + skip-stub additions.

**Task 7 round 2 (2026-04-27) — 86 EXISTING gaps (G01-G86) added as SKIP rows.**
Round 2 added 167 additional skip rows across the same 27 suites for
the previously-existing gap entries G01-G86 in
KNOWN-FUNCTIONALITY-GAPS-AND-PLAN.md. No `src/` modifications; plan-doc
+ skip-stub additions only. New aggregate: 3684/3384/0/300.
