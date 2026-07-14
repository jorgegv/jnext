# Subsystem Compliance Test Dashboard

VHDL-derived compliance test suite for the JNEXT ZX Spectrum Next emulator. All expected values are derived exclusively from the FPGA VHDL hardware specification. Tests verify C++ implementation against authoritative hardware behavior.

## Status

| Subsystem             |     Live |     Pass |   Fail |    Skip |    Rate | Notes |
|-----------------------|---------:|---------:|-------:|--------:|--------:|--------------------------------------------------------|
| FUSE Z80              |     1356 |     1356 |      0 |       0 |    100% | 🟢 All tests pass. |
| Z80N CPU              |       85 |       85 |      0 |       0 |    100% | 🟢 All tests pass. |
| Rewind                |       32 |       22 |      0 |      10 |     68% | 🟡 Task 7r2: G66/G67 SS-VER + RB-FRAME skips added. |
| Copper                |       79 |       79 |      0 |       0 |    100% | 🟢 All tests pass. |
| Copper (integration)  |        2 |        2 |      0 |       0 |    100% | 🟢 All tests pass. |
| Memory/MMU            |      252 |      246 |      0 |       6 |     97% | 🟡 Task 57: G33 Phase 1 — BOOT-TAPESAVE-01..03 closed (TapSaver). |
| Memory/MMU (int)      |       24 |       24 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (bare)        |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| NextREG (integration) |      279 |      279 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input                 |      161 |      161 |      0 |       0 |    100% | 🟢 All tests pass. |
| Input (integration)   |       17 |       17 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC + Interrupts      |      132 |      132 |      0 |       0 |    100% | 🟢 All tests pass. |
| CTC (integration)     |       30 |       30 |      0 |       0 |    100% | 🟢 All tests pass. |
| Layer 2               |      134 |      134 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART + I2C/RTC        |       98 |       98 |      0 |       0 |    100% | 🟢 All tests pass. |
| UART (integration)    |       16 |       16 |      0 |       0 |    100% | 🟢 All tests pass. |
| DivMMC + SPI          |      140 |      140 |      0 |       0 |    100% | 🟢 All tests pass. |
| Multiface (core)      |       49 |       49 |      0 |       0 |    100% | 🟢 All tests pass. |
| SD Card               |       38 |       38 |      0 |       0 |    100% | 🟢 All tests pass. |
| Sprites               |      197 |      197 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor            |      181 |      181 |      0 |       0 |    100% | 🟢 All tests pass. |
| Compositor (int)      |        8 |        8 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video             |      106 |      106 |      0 |       0 |    100% | 🟢 All tests pass. |
| ULA Video (int)       |       12 |       12 |      0 |       0 |    100% | 🟢 All tests pass. |
| Floating Bus          |       34 |       34 |      0 |       0 |    100% | 🟢 All tests pass. |
| VideoTiming           |       30 |       30 |      0 |       0 |    100% | 🟢 All tests pass. |
| Contention            |       89 |       89 |      0 |       0 |    100% | 🟢 All tests pass. |
| I/O Port Dispatch     |      105 |      104 |      0 |       1 |     99% | 🟡 Tier 6: G45 EXPBUS-AND-01..04 → WONT (expansion bus + cartridge framework out of scope). |
| Audio (AY+DAC+Beeper) |      138 |      138 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (NextREG)       |       33 |       33 |      0 |       0 |    100% | 🟢 All tests pass. |
| Audio (port dispatch) |       21 |       21 |      0 |       0 |    100% | 🟢 All tests pass. |
| DMA                   |      150 |      150 |      0 |       0 |    100% | 🟢 All tests pass. |
| Tilemap               |       69 |       69 |      0 |       0 |    100% | 🟢 All tests pass. |
| NMI Source Pipeline   |       62 |       57 |      0 |       5 |     91% | 🟡 Task 8 Wave 1 (2026-05-04): all 7 MF-G48-* closed (-01 port table, -02/03/04 state machine, -05/07 +3 readback, -06 DivMMC retn AND-NOT mf_is_active). Remaining 5 SKIPs are BOOT-LOOP/LOGO/DOT + BYPASS-FAT/INI (G46/G47/G59/G60). |
| NMI (integration)     |        9 |        9 |      0 |       0 |    100% | 🟢 All tests pass. |
| Debugger Video Panel  |       80 |       80 |      0 |       0 |    100% | 🟢 Task 22a (2026-07-12): new suite. Panel-vs-compositor parity — rom_in_sram bank shift, bank-pinned ULA views, per-scanline replay, raw-VC→fb-row. |
| **Total**             | **4255** | **4217** |  **0** |  **38** | **99%** | 🟡 38 SKIPs total (Rewind 10, Memory/MMU 22, NMI pipeline 5, port dispatch 1); 0 FAILs. |

**SKIP:** Functionality that has been traced from VHDL to a test case, but still has not been developed/fixed in C++ code.
