# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | 🟢 All tests pass. |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | 🟢 Task 8 wave 2: LDDRX DE-direction fixed (was decrementing, VHDL says increments); 6 tests.expected entries refreshed. |
| Rewind                |       28 |       18 |      0 |      10 |     64% | 🟡 Task 7r2: G66/G67 SS-VER + RB-FRAME skips added. |
| Copper                |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper (integration)  |        2 |        2 |      0 |       0 |    100% | 🟢 G117-MPC-01 + G65-PRI-01 (cycle-accurate Copper + tied-edge). |
| Memory/MMU            |      178 |      146 |      0 |      32 |     82% | 🟡 Task 8 t1+W2: G140/G155/G157 + G148 + G57 closed; G143 RE-HOME. |
| NextREG (bare)        |       21 |       21 |      0 |       0 |    100% | 🟢 Task 7r2: G55/G56/G62/G64 (FT/CR/CFG/BYPASS-Q). |
| NextREG (integration) |      179 |      179 |      0 |       0 |    100% | 🟢 Task 7r2: G63 CFG-09-INT added. |
| Input                 |      161 |      161 |      0 |       0 |    100% | 🟢 Task 7r2: G43/G72 (MOUSE-13..15 + IOMODE-11A). |
| Input (integration)   |       17 |       17 |      0 |       0 |    100% | 🟢 Task 7r2: G42/G44 (JOY-WIRE-02..04 + FE-04A). |
| CTC + Interrupts      |      132 |      132 |      0 |       0 |    100% | 🟢 Tier 6: G119 fabric-edge int + G120 prescaler-preserve + G121 NR03-timing fan-out PASS; NR-C0-02 → WONT G49. |
| CTC (integration)     |       21 |       21 |      0 |       0 |    100% | 🟢 Task 8 wave 2: G89 LDIRX/LDDRX/LDPIRX/LDIRSCALE per-iter INT sample (4 iter+rewind + 1 INT-sample PASS rows). |
| Layer 2               |      133 |      133 |      0 |       0 |    100% | 🟢 Task 8: L2-G17-01 retired (G164v2); G9-G28-01 → WONT (pixel-granular L2 renderer out of scope; G117 Copper scheduler already closed separately). |
| UART + I2C/RTC        |       94 |       94 |      0 |       0 |    100% | 🟢 Tier 6: G134 RX request-mask + G161 RTC 12h-mode PASS; G138 NR 0xA0 b3 PASS; G39 ESP-01..04 + G139 24LC256 → WONT. |
| UART (integration)    |       16 |       16 |      0 |       0 |    100% | 🟢 Tier 6: G135 NR 0xA0 fan-out (NR_A0-01/02/03) + G134 INT-07 PASS rows added (re-homed from uart_test). |
| DivMMC + SPI          |      109 |      109 |      0 |       0 |    100% | 🟢 G46(a) DM-RETN-PROPER-01/02 PASS (delayed-off automap_held clear via 1-M1 register on Im2 retn_seen); G136 SS-08 → WONT (SPI Flash CS out of scope). |
| SD Card               |       15 |       15 |      0 |       0 |    100% | 🟢 Task 8: 7 PASS (CMD13/16/23 handlers + stale CMD24/CMD1 + hot-plug) + 6 WONT (G40/G41/G159: no firmware client). |
| Sprites               |      197 |      197 |      0 |       0 |    100% | 🟢 Task 7r2: G06/G13/G15 OVF + NR70 stubs added. |
| Compositor            |      143 |      143 |      0 |       0 |    100% | 🟢 Task 8: G26 closed (UB-G26-01 RE-HOME → UTB-40/41; UB-G26-02 mode-110 L2-priority real test). |
| Compositor (int)      |        8 |        8 |      0 |       0 |    100% | 🟢 Task 8 t1: G108 PFF×4 added. |
| ULA Video             |      107 |      107 |      0 |       0 |    100% | 🟢 G104 closed (S5.10 HI_RES 512px byte-interleave); G105 HI_RES border closed. |
| ULA Video (int)       |       12 |       12 |      0 |       0 |    100% | 🟢 Task 8: G102 ULAnext palette runtime closed. |
| Floating Bus          |       32 |       32 |      0 |       0 |    100% | 🟢 All tests pass. |
| VideoTiming           |       37 |       37 |      0 |       0 |    100% | 🟢 Task 8 W1: G71/G106/G107/G109 closed (zero skips). |
| Contention            |       74 |       74 |      0 |       0 |    100% | 🟢 All tests pass. |
| I/O Port Dispatch     |       83 |       83 |      0 |       0 |    100% | 🟢 Tier 6: G45 EXPBUS-AND-01..04 → WONT (expansion bus + cartridge framework out of scope). |
| Audio (AY+DAC+Beeper) |      134 |      134 |      0 |       0 |    100% | 🟢 Tier 6: G115 TurboSound::reset_ay_only split (TS-60+61 PASS); G29 MX-30 + G30 AY-30..34 + G31 SD-09 → WONT. |
| Audio (NextREG)       |       33 |       33 |      0 |       0 |    100% | 🟢 Task 8: G110 mixer exc_i + G111 DAC silence + G112 NR 0x2C/0x2D/0x2E reads + G113 NR 0xA2 fan-out + G73 Mixer Pi I2S gate. |
| Audio (port dispatch) |       21 |       21 |      0 |       0 |    100% | 🟢 Tier 6: G114 NR 0x84 bits 1/3/4/6/7 DAC port-pair gates (IO-13..17 PASS). Old test stub b1→SD2 corrected to b1→SD1 per VHDL. |
| DMA                   |      150 |      150 |      0 |       0 |    100% | 🟢 Tier 6: G122 13.7+13.8 → WONT (14 MHz dma_d_p_s + rw_extend). |
| Tilemap               |       69 |       69 |      0 |       0 |    100% | 🟢 Task 7r2: G06 TM-160..164 mode-flip rows added. |
| NMI Source Pipeline   |       56 |       43 |      0 |      13 |     76% | 🟡 Task 8 W1+W2: G153/G162/G152 closed; Z80-04 RE-HOMED to CTC plan (G88). |
| NMI (integration)     |        9 |        9 |      0 |       0 |    100% | 🟢 G152: HK-06/07/08/09-INT PASS (F9 MF NMI / F10 DivMMC NMI / F4 soft-reset gated by nr_03_config_mode / F1 hard-reset). |
| **Total**             | **3791** | **3736** |  **0** |  **55** | **98%** | 🟡  |

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
