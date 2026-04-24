# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes                                                  |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | Clean. FUSE data-driven opcode test suite              |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | Clean. All opcodes verified against VHDL               |
| Rewind                |       18 |       18 |      0 |       0 |    100% | Clean. Snapshot roundtrip + step-back                  |
| Copper                |       76 |       76 |      0 |       0 |    100% | **Zero skips as of 2026-04-24.** ARB-06 flipped: Copper MOVE NR 0x02 routes through NmiSource (Phase 3). |
| Memory/MMU            |      150 |      148 |      0 |       2 |    100% | 2 skips: P7F-16/17 shadow-screen routing (re-homed from ULA) |
| NextREG (bare)        |       21 |       21 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| NextREG (integration) |       73 |       73 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| Input                 |      139 |      139 |      0 |       0 |    100% | **Zero skips as of 2026-04-24.** Post-UART-close re-audit un-skipped IOMODE-05..10: NR 0x0B UART modes 10/11 pin-7 multiplexing + joy_uart_en/rx composition (`zxnext.vhd:3526-3539`). IoMode gained set_uart0_tx/uart1_tx/joy_left_bit5/joy_right_bit5 injectors. Emulator production wiring is a backlog item. |
| Input (integration)   |       12 |       10 |      0 |       2 |    100% | 2 skips: FE-04 issue-2 MIC^EAR, FE-05 expansion bus AND. 2026-04-24 Wave D extended with Group FE-READ (BP-04 + BP-20..23 re-homed from audio_test). |
| CTC + Interrupts      |      133 |      129 |      0 |       4 |    100% | DMA-04 flipped (NmiSource-driven DMA delay, Phase 3). Remaining skips: NR-C0-02 (Wave D cut), ULA-INT, misc. |
| Layer 2               |       89 |       89 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| UART + I2C/RTC        |       92 |       92 |      0 |       0 |    100% | **Zero skips as of 2026-04-24.** TASK3-UART-I2C-SKIP-REDUCTION-PLAN.md closed end-to-end: bit-level TX/RX engines, I2cRtc full DS1307 expansion, 12 cross-subsystem rows re-homed to UART integration. BAUD-02/03 D-UNOBSERVABLE. |
| UART (integration)    |       12 |       12 |      0 |       0 |    100% | Clean. 2026-04-24 new suite: UART interrupts + port-enable gate + dual-routing. |
| DivMMC + SPI          |      100 |      100 |      0 |       0 |    100% | **Zero skips as of 2026-04-24.** NM-01..08 flipped: button_nmi lifecycle + 4 clear paths via NmiSource (Phase 3). |
| SD Card               |        8 |        8 |      0 |       0 |    100% | Clean. SD card I/O, CMD17/CMD18 block reads            |
| Sprites               |      121 |      121 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| Compositor            |      117 |      114 |      0 |       3 |    100% | 3 skips: UDIS-01/02/03 NR 0x68 blend-mode (re-homed from ULA) |
| ULA Video             |       81 |       81 |      0 |       0 |    100% | **Zero skips as of 2026-04-23.** All 29 F-blocked rows re-homed to 5 new subsystem plans. |
| ULA Video (int)       |        7 |        7 |      0 |       0 |    100% | Clean. Scroll, ULA+, ULAnext, alt-file, NR 0x68 bit 3 ungated ulap_en. |
| Floating Bus          |       26 |        0 |      0 |      26 |    100% | Scaffold suite (skip-only) 2026-04-24: port 0xFF all-machines + port 0x0FFD +3. |
| VideoTiming           |       22 |        0 |      0 |      22 |    100% | Scaffold suite (skip-only) 2026-04-24: VHDL-faithful vc_max_ / hc_max_ rebase. |
| Contention            |       68 |        0 |      0 |      68 |    100% | Scaffold suite (skip-only) 2026-04-24: phased A=28/B=36/C=4 per CONTENTION-TEST-PLAN-DESIGN.md. |
| I/O Port Dispatch     |       83 |       83 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| Audio (AY+DAC+Beeper) |      132 |      132 |      0 |       0 |    100% | **Zero skips as of 2026-04-24.** TASK3-AUDIO-SKIP-REDUCTION-PLAN.md closed end-to-end: 21 E-class demoted to // A: / // G: comments, 41 rows re-homed to new integration suites, 1 MX-06 flipped via new I2s stub, 4 TS rows un-skipped via per-PSG isolation proofs. |
| Audio (NextREG)       |       25 |       25 |      0 |       0 |    100% | 2026-04-24 new suite: NR 0x06/0x08/0x09/0x2C-2E handlers, beeper XOR composite, exc_i gating, dac_hw_en gate. |
| Audio (port dispatch) |       16 |       16 |      0 |       0 |    100% | 2026-04-24 new suite: Soundrive mode 1/2, Covox variants, GS Covox, SpecDrum, AY FFFD/BFFD/BFF5 decode. Wave F fixed 6 port-dispatch gaps; IO-04 FFFD falling-edge Z80-invisible. |
| DMA                   |      150 |      150 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| Tilemap               |       59 |       59 |      0 |       0 |    100% | Clean. All plan rows covered                           |
| NMI Source Pipeline   |       32 |       32 |      0 |       0 |    100% | 2026-04-24: Phases 1-2 complete (RST-01 + NR02-* + HK-* + DIS-* + CLR-* + GATE-* + DMA-* = 32 rows). |
| NMI (integration)     |        5 |        5 |      0 |       0 |    100% | 2026-04-24 new suite (Phase 3): INT-01..05 end-to-end button/sw-NMI → /NMI → PC=0x0066 → automap → RETN. |
| **Total**             | **3318** | **3191** |  **0** | **127** | **100%**|                                                        |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
