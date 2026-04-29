# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | <span style="background-color:#ccffcc">All tests pass.</span> |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | <span style="background-color:#ccffcc">All tests pass.</span> |
| Rewind                |       28 |       18 |      0 |      10 |     64% | <span style="background-color:#fff2cc">Task 7r2: G66/G67 SS-VER + RB-FRAME skips added.</span> |
| Copper                |       82 |       79 |      0 |       3 |     96% | <span style="background-color:#fff2cc">Task 7r2: G65 ARB-G65-01 tied-edge added.</span> |
| Memory/MMU            |      178 |      146 |      0 |      32 |     82% | <span style="background-color:#fff2cc">Task 8 t1+W2: G140/G155/G157 + G148 + G57 closed; G143 RE-HOME.</span> |
| NextREG (bare)        |       63 |       21 |      0 |      42 |     33% | <span style="background-color:#fff2cc">Task 7r2: G55/G56/G62/G64 (FT/CR/CFG/BYPASS-Q).</span> |
| NextREG (integration) |       79 |       78 |      0 |       1 |     98% | <span style="background-color:#fff2cc">Task 7r2: G63 CFG-09-INT added.</span> |
| Input                 |      161 |      161 |      0 |       0 |    100% | <span style="background-color:#ccffcc">Task 7r2: G43/G72 (MOUSE-13..15 + IOMODE-11A).</span> |
| Input (integration)   |       17 |       17 |      0 |       0 |    100% | <span style="background-color:#ccffcc">Task 7r2: G42/G44 (JOY-WIRE-02..04 + FE-04A).</span> |
| CTC + Interrupts      |      133 |      129 |      0 |       4 |     96% | <span style="background-color:#fff2cc">Task 7r2: G49 NR-C0-02 promoted from WONT comment.</span> |
| CTC (integration)     |       21 |       12 |      0 |       9 |     57% | <span style="background-color:#fff2cc">Unchanged (Task 7 round 1 baseline).</span> |
| Layer 2               |      120 |      118 |      0 |       2 |     98% | <span style="background-color:#fff2cc">Task 8 W1+W2: G91/G02/G05/G09/G14 + G92/G144/G145.</span> |
| UART + I2C/RTC        |      102 |       92 |      0 |      10 |     90% | <span style="background-color:#fff2cc">Task 7r2: G39 ESP-01..04 added.</span> |
| UART (integration)    |       13 |       12 |      0 |       1 |     92% | <span style="background-color:#fff2cc">Unchanged (Task 7 round 1 baseline).</span> |
| DivMMC + SPI          |      110 |      107 |      0 |       3 |     97% | <span style="background-color:#fff2cc">Task 8 t1: G123/G124/G125/G131/G137 closed (-7).</span> |
| SD Card               |       21 |        8 |      0 |      13 |     38% | <span style="background-color:#fff2cc">Task 7r2: G40/G41 SD-10..15 + MMC-01..03 added.</span> |
| Sprites               |      196 |      196 |      0 |       0 |    100% | <span style="background-color:#ccffcc">Task 7r2: G06/G13/G15 OVF + NR70 stubs added.</span> |
| Compositor            |      144 |      142 |      0 |       2 |     98% | <span style="background-color:#fff2cc">Task 8 W1+W2: G27/G93/G04-01/G11×3 closed; G108×3 + G04-02/03 re-homed.</span> |
| Compositor (int)      |        7 |        7 |      0 |       0 |    100% | <span style="background-color:#ccffcc">Task 8 t1: G108 PFF×4 added.</span> |
| ULA Video             |       98 |       96 |      0 |       2 |     97% | <span style="background-color:#fff2cc">Task 7r2: G07/G08/G10 (S5-PSL/S9-PSL/S17.x).</span> |
| ULA Video (int)       |       12 |       11 |      0 |       1 |     91% | <span style="background-color:#fff2cc">Unchanged (Task 7 round 1 baseline).</span> |
| Floating Bus          |       32 |       32 |      0 |       0 |    100% | <span style="background-color:#ccffcc">All tests pass.</span> |
| VideoTiming           |       27 |       27 |      0 |       0 |    100% | <span style="background-color:#ccffcc">Task 8 W1: G71/G106/G107/G109 closed (zero skips).</span> |
| Contention            |       76 |       71 |      0 |       5 |     93% | <span style="background-color:#fff2cc">Task 7r2: G50/G51/G53 (CT-DELAY/TURBO-08/FUSE-05).</span> |
| I/O Port Dispatch     |       87 |       83 |      0 |       4 |     95% | <span style="background-color:#fff2cc">Task 7r2: G45 EXPBUS-AND-01..04 added.</span> |
| Audio (AY+DAC+Beeper) |      141 |      132 |      0 |       9 |     93% | <span style="background-color:#fff2cc">Task 7r2: G29/G30/G31 (MX-30 + AY-30..34 + SD-09).</span> |
| Audio (NextREG)       |       33 |       25 |      0 |       8 |     75% | <span style="background-color:#fff2cc">Task 7r2: G73 NR-43 mixer-gate added.</span> |
| Audio (port dispatch) |       21 |       16 |      0 |       5 |     76% | <span style="background-color:#fff2cc">Unchanged (Task 7 round 1 baseline).</span> |
| DMA                   |      152 |      150 |      0 |       2 |     98% | <span style="background-color:#fff2cc">Unchanged (Task 7 round 1 baseline).</span> |
| Tilemap               |       69 |       69 |      0 |       0 |    100% | <span style="background-color:#ccffcc">Task 7r2: G06 TM-160..164 mode-flip rows added.</span> |
| NMI Source Pipeline   |       56 |       43 |      0 |      13 |     76% | <span style="background-color:#fff2cc">Task 8 W1+W2: G153/G162/G152 closed; Z80-04 RE-HOMED to CTC plan (G88).</span> |
| NMI (integration)     |        9 |        5 |      0 |       4 |     55% | <span style="background-color:#fff2cc">Unchanged (Task 7 round 1 baseline).</span> |
| **Total**             | **3729** | **3544** |  **0** | **185** | **95%** | <span style="background-color:#fff2cc"></span> |

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
