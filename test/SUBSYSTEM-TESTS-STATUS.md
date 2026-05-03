# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | 🟢 All tests pass. |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | 🟢 All tests pass. |
| Rewind                |       28 |       18 |      0 |      10 |     64% | 🟡 Task 7r2: G66/G67 SS-VER + RB-FRAME skips added. |
| Copper                |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper (integration)  |        2 |        2 |      0 |       0 |    100% | 🟢 G117-MPC-01 + G65-PRI-01 (cycle-accurate Copper + tied-edge). |
| Memory/MMU            |      178 |      146 |      0 |      32 |     82% | 🟡 Task 8 t1+W2: G140/G155/G157 + G148 + G57 closed; G143 RE-HOME. |
| NextREG (bare)        |       21 |       21 |      0 |       0 |    100% | 🟢 Task 7r2: G55/G56/G62/G64 (FT/CR/CFG/BYPASS-Q). |
| NextREG (integration) |      179 |      179 |      0 |       0 |    100% | 🟢 Task 7r2: G63 CFG-09-INT added. |
| Input                 |      161 |      161 |      0 |       0 |    100% | 🟢 Task 7r2: G43/G72 (MOUSE-13..15 + IOMODE-11A). |
| Input (integration)   |       17 |       17 |      0 |       0 |    100% | 🟢 Task 7r2: G42/G44 (JOY-WIRE-02..04 + FE-04A). |
| CTC + Interrupts      |      133 |      129 |      0 |       4 |     96% | 🟡 Task 7r2: G49 NR-C0-02 promoted from WONT comment. |
| CTC (integration)     |       21 |       12 |      0 |       9 |     57% | 🟡 Unchanged (Task 7 round 1 baseline). |
| Layer 2               |      134 |      133 |      0 |       1 |     99% | 🟡 Task 8: L2-G17-01 retired (G164v2); G9-G28-01 cycle-accurate deferred. |
| UART + I2C/RTC        |      102 |       92 |      0 |      10 |     90% | 🟡 Task 7r2: G39 ESP-01..04 added. |
| UART (integration)    |       13 |       12 |      0 |       1 |     92% | 🟡 Unchanged (Task 7 round 1 baseline). |
| DivMMC + SPI          |      110 |      107 |      0 |       3 |     97% | 🟡 Task 8 t1: G123/G124/G125/G131/G137 closed (-7). |
| SD Card               |       21 |        8 |      0 |      13 |     38% | 🟡 Task 7r2: G40/G41 SD-10..15 + MMC-01..03 added. |
| Sprites               |      197 |      197 |      0 |       0 |    100% | 🟢 Task 7r2: G06/G13/G15 OVF + NR70 stubs added. |
| Compositor            |      144 |      142 |      0 |       2 |     98% | 🟡 Task 8 W1+W2: G27/G93/G04-01/G11×3 closed; G108×3 + G04-02/03 re-homed. |
| Compositor (int)      |        8 |        8 |      0 |       0 |    100% | 🟢 Task 8 t1: G108 PFF×4 added. |
| ULA Video             |      107 |      107 |      0 |       0 |    100% | 🟢 G104 closed (S5.10 HI_RES 512px byte-interleave); G105 HI_RES border closed. |
| ULA Video (int)       |       12 |       12 |      0 |       0 |    100% | 🟢 Task 8: G102 ULAnext palette runtime closed. |
| Floating Bus          |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| VideoTiming           |       37 |       37 |      0 |       0 |    100% | 🟢 Task 8 W1: G71/G106/G107/G109 closed (zero skips). |
| Contention            |       74 |       74 |      0 |       0 |    100% | 🟢 All tests pass. |
| I/O Port Dispatch     |       87 |       83 |      0 |       4 |     95% | 🟡 Task 7r2: G45 EXPBUS-AND-01..04 added. |
| Audio (AY+DAC+Beeper) |      141 |      132 |      0 |       9 |     93% | 🟡 Task 7r2: G29/G30/G31 (MX-30 + AY-30..34 + SD-09). |
| Audio (NextREG)       |       33 |       25 |      0 |       8 |     75% | 🟡 Task 7r2: G73 NR-43 mixer-gate added. |
| Audio (port dispatch) |       21 |       16 |      0 |       5 |     76% | 🟡 Unchanged (Task 7 round 1 baseline). |
| DMA                   |      152 |      150 |      0 |       2 |     98% | 🟡 Unchanged (Task 7 round 1 baseline). |
| Tilemap               |       69 |       69 |      0 |       0 |    100% | 🟢 Task 7r2: G06 TM-160..164 mode-flip rows added. |
| NMI Source Pipeline   |       56 |       43 |      0 |      13 |     76% | 🟡 Task 8 W1+W2: G153/G162/G152 closed; Z80-04 RE-HOMED to CTC plan (G88). |
| NMI (integration)     |        9 |        5 |      0 |       4 |     55% | 🟡 Unchanged (Task 7 round 1 baseline). |
| **Total**             | **3819** | **3689** |  **0** | **130** | **96%** | 🟡  |

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
