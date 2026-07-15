# ACCUMULATED DEVELOPMENT TIME

| Date   | ETA  | Task                                                                                                 |
|--------|------|------------------------------------------------------------------------------------------------------|
| 18/3   | 1h   | Project plan                                                                                         |
| 19/3   | 4h   | Phases 1,2,3                                                                                         |
| 20/3   | 4h   | Phases 3,3.5,4,5                                                                                     |
| 21/3   | 3h   | Phases 3,4,5                                                                                         |
|        | 2h   | Phase 3: Graphics Debugging                                                                          |
|        | 1h   | Phase 6: QT GUI                                                                                      |
| 22/3   | 2h   | Phase 6:QT GUI                                                                                       |
|        | 1h   | Phase 5: DAC Sound debugging                                                                         |
|        | 4.5h | Phase 7: Debugger Implementation                                                                     |
| 28/3   | 4h   | Phase 7: Debugger implementation                                                                     |
| 29/3   | 2h   | Phase 10: DivMMC/SPI debugging                                                                       |
|        | 6h   | Phase 10: NextZXOS boot debugging                                                                    |
| 31/3   | 4h   | Phase 10: NextZXOS boot debugging                                                                    |
| 5/4    | 3h   | Phase 7.8: test suite                                                                                |
| 6/4    | 8h   | Phase 7.8: new video modes, new file formats                                                         |
|        | 2h   | Phase 7.8: SNA/SZX/WAV loaders, README                                                               |
| 7/4    | 8h   | Phase 8: magic BP/port, RZX, video recording, General UI, Debugger enhancements, bug fixes           |
| 8/4    | 4h   | Phase 8: Video panel redesign and Subpanels, fixes                                                   |
| 9/4    | 1.5h | Phase 8: Backward execution design, implementation plan                                              |
|        |      | Phase 11: Scriptable debugger design, implementation plan                                            |
| 10/4   | 4h   | Phase 8: Backward execution GUI (Step Back, Frame Back, rewind slider, status bar, sync)             |
|        | 1h   | Public release preparation                                                                           |
| 11/4   | 4h   | Phase 9: Bug fixes: palette index, 4-bit sprites, floating bus, ULA clip, tilemap 80-col             |
| 12/4   | 8h   | Phase 9: FUSE 100%, Z80N test suite (78/78), 15 VHDL test plans, Copper+MMU test runners             |
| 13/4   | 4h   | Phase 9: Z80 timing fix (3 root causes), contention, internal Z80N core feasibility plan             |
| 14/4   | 3h   | Phase 9: Task 5 Steps 1-2 (lint, 4 plan/test fixes +54, 8 real emulator bugs documented)             |
|        | 2h   | Phase 9: Task 5 Step 5 Phase 1 (6 theatre subsystem plans rebuilt from VHDL + reviews)               |
|        | 3h   | Phase 9: Task 5 Step 5 Phase 2 (6 theatre test suites rewritten, 2 waves, 89.7% honest)              |
|        | 1h   | Phase 9: Task 5 Step 6 traceability matrix + UNIT-TEST-PLAN-EXECUTION doc + reorg                    |
| 15/4   | 10h  | Phase 9: 9 older suites refactored, matrix refresh, 4 emu fixes, traceability docs                   |
|        | 6h   | Phase 9: Full 1788-row audit (3 waves), remediation, matrix 1010/89/689/0                            |
| 16/4   | 9h   | Phase 9: Tasks 2+3 DONE, Task 1 19/21 items resolved, compositor+port dispatch fixes                 |
| 17/4   | 7h   | Phase 9: v0.92.0+v0.93.0, Task 3 SKIP reduction (7 subsystems), DivMMC+SPI 4-phase plan              |
| 18/4   | 6h   | Phase 2: MMU+NextREG un-skips (B/D1/D2/E), Option C 0xFF sentinel, NR 0x8E bit-3 gate                |
| 19/4   | 4h   | Task (c): Ram 1792→2048 KB fix, DivMMC 0x0066 automap gate on button_nmi (VHDL:120)                  |
| 20/4   | 2h   | Task 3: NextREG bare+int (21/0/0 + 73/0/0), MMU (148/0/0), Port (83/0/0) closed. WONT taxo           |
|        |      | NR 0x12/0x13 readback bug fix, NR 0x82 b2→0xDFFD gate, Makefile dashboard hand-off fix               |
| 21/4   | 3h   | Task 3: CTC+Interrupts plan CLOSED Phase 0→5 (13 agents, 11 critics, 37 commits)                     |
|        |      | Im2Controller fabric: 45-line stub → ~800 lines (decoder, device SM, daisy, pulse, NR, DMA)          |
|        |      | ctc_test 106→5 skips (+84 pass), new ctc_interrupts_test 10/10. Aggregate 3222/2886/0/336            |
|        |      | NextZXOS boot retest: all ROMs load banner, boots to 48K mode (was blue-stripes DivMMC-IM1)          |
| 22/4   | 3h   | Task 3 Input plan CLOSED Phase 0→4 (9 parallel agents Wave 1+2, 24 commits)                          |
|        |      | input_test 149/23/0/126 → 139/133/0/6 + new input_integration_test. CTC zc_to bug fix                |
| 23/4   | 3h   | Task 3 ULA Video plan CLOSED (Phases 0-3, 5 waves A-E) + Post-Input backlog closed (29 cmt)          |
|        |      | ula_test 123/48/0/75 → 81/81/0/0 + new ula_integration_test. 29 F-skips re-homed                     |
| 24/4   | 7h   | Task 3 UART+I2C + Audio + Input-re-audit + NMI + 4-subsys + UDIS-01/02/03 ALL CLOSED (131)           |
|        |      | Compositor 125/125/0/0 ZERO skips. 5 emu bugs. Aggregate 3326/3210/0/116 across 32 suites            |
| 25/4   | 7h   | Task 3 floating_bus closed (26→0, 4 branches). Beast.nex 100% (shadow-screen + per-line palette)     |
|        |      | NextZXOS splash bisect + band-aid. Parallax investigation (PARKED on LoRes). 3339/3249/0/90          |
| 26/4   | 8h   | videotiming (22→0) + contention (68→0) closed. **ZERO SKIPS REPO-WIDE** 3336/3336/0/0                |
|        |      | Task 4 86-gap plan + Task 5 beast bottom-band fix + parallax debugging deep dive                     |
| 27/4   | 7h   | Tasks 4-7 e2e: gaps reprioritised + Task 6 +76 NEW Gxx + Task 7 +300 SKIP rows (r1+r2)               |
|        |      | Traceability matrix overhaul (32 suites + 7 int sub-sections). README pruned. 3684/3384/0/300        |
| 28/4   | 7h   | SKIP-reduction wave 1: 4 high-priority subsystems closed (~55 SKIPs across NextREG/MMU/Port/CTC)     |
|        |      | Wave 2+3 partial: +60 SKIPs across audio/video/serial subsystems. Aggregate ~3700/3500/0/190         |
| 29/4   | 5h   | Per-scanline VBlank flush across 6 demos; Im2Controller integration into Emulator tick loop          |
|        |      | Render-path consistency fixes + turbo retest. Aggregate ~3700/3550/0/150                             |
| 30/4   | 6h   | 5 NMI/copper/timing fixes landed; parallax line-int root cause traced via firmware disassembly       |
|        |      | Top+bottom parallax strips scrolling (with residual artifacts). Aggregate 3763/3590/0/173            |
| 1/5    | 7h   | Parallax 100% fixed: 3 coord-space bugs cancelling in render pipeline (line-tag offset deep dive)    |
|        |      | Vblank-top fix + medium-priority subsystems closed; rendering refactor preparation                   |
| 2/5    | 8h   | Canonical 640px framebuffer + HI_RES byte-interleave (8-phase refactor); 4-issue rendering wave      |
|        |      | 27 screenshot references regenerated (pixel-equivalence proven first); GUI scale 1x/2x/3x options    |
| 3/5    | 10h  | NextREG schema audit (write-handler contract, 9 read_handlers dropped) + SD card protocol cleanup    |
|        |      | CTC interrupts 2 waves + Audio NR + low-prio tier (5-agent parallel) + DivMMC fix; 22 G-tasks closed |
| 4/5    | 8h   | Task 8 Multiface plan Wave 0+1 (SD-ROM foundation, MF core class) + Tier A/B SKIP reduction         |
|        |      | All ROMs now sourced from SD image; nextboot.rom embedded at link time. Dashboard 3850 rows          |
| 6/5    | 6h   | G46(b) NextZXOS boot investigation: SD protocol traced, bank topology decoded, band-aid proven       |
|        |      | EOD-5/6/7 chain; TBBLUE logo reached under experiments                                               |
| 7/5    | 5h   | G46(b): DZRP connector to CSpect + differential debugging methodology established                    |
| 8/5    | 8h   | G46(b): NR $03 machine_type commit fix + MMU/clock/CMD12 soft-reset fixes (EOD-9..13)                |
|        |      | Loader-log bisect; soft-reset state preservation audited                                             |
| 9/5    | 6h   | Task 2 verify-audit passes 1-8: NextREG schema, DivMMC RETN, SPI CS decode waves                     |
| 10/5   | 7h   | Task 2 verify-audit passes 9-19 (5-agent parallel): enumeration-table mandate established            |
|        |      | Hundreds of VHDL-conformance fixes with discriminative tests                                         |
| 11/5   | 5h   | Task 2 audit continuation + convergence rules (zero-findings subsystems skipped)                     |
| 14/5   | 5h   | Task 14 NextZXOS boot test-strategy analysis; G46(b) EOD-26 sram_rom candidates narrowed             |
| 15/5   | 9h   | G46(b)-v2 EOD-30i chain (6 handovers): 5-flag boot reaches supervisor RST $20; IPL self-corruption   |
|        |      | root-caused to config-mode LDIR; CSpect plugin captures established                                  |
| 16/5   | 8h   | L2 320x256 bank-stride fix + esxDOS stub + Kempston Y fix + GUI cursor; regression 30 → 40           |
|        |      | Tasks 14/15/16/17 closed; 9 NEX games extracted for testing                                          |
| 17/5   | 9h   | Task 18 --bypass-tbblue-fw lands: NextZXOS boots to idle loop (banner missing). Symmetric-trace      |
|        |      | infra (TraceLog + CSpectFullTrace). Task 19 instant TAP + Task 20 inverted-attributes fix            |
| 29/5   | 5h   | Task 21 CPU T-state profiler: --profile + per-physical-address counters + z88dk heatmap script       |
| 8/7    | 2h   | Issue #4 build fixes (Debian 12 cstddef, nextboot.rom tracked, noexecstack); github-cli skill        |
| 9/7    | 4h   | zx_go comparison: refutes G46(b) L6/L7 conclusions; boot verified in zx_go with our image + ROM      |
| 10/7   | 7h   | **NextZXOS NATIVE BOOT ACHIEVED**: bank-7 BRAM/alt-ROM aliasing root-caused via symmetric trace      |
|        |      | 3-round independent review (BLOCKER found+fixed); merged to main; v0.94.0                            |
| 11/7   | 9h   | v0.95.0 batch (multi-agent): bank-5 VRAM, --rtc, boot regressions, SPI/Multiface conformance,        |
|        |      | SD default location + built-in download (FatFs+curl+SHA256), --sd-card→--sdcard.                     |
| 12/7   | 5h   | Audio: beeper clicking = audio underruns (== issue #7); paced emulation on the audio clock.          |
|        |      | Mixer now integrates per sample (de-aliased). The v0.95.x DC-blocker "fix" reverted.                 |
| 12/7   | 6h   | Tasks 32/35/37: declared-suite manifests + row-count pins + harness self-test (26 cases) —           |
|        |      | the harness could silently run fewer tests than it claimed. Two suites ran for the first time.       |
| 12/7   | 4h   | Task 40 (beast.nex): the debugger was altering the machine it observed (stale NextREG cache          |
|        |      | → peek(); paused frame restarted not resumed). 2 of the 4 reported symptoms were not bugs.           |
| 13/7   | 10h  | Task 50: contention window displaced 64 scanlines (FUSE headless as T-state oracle).                 |
|        |      | Task 54 SOLVED Nirvana/BIFROST: wrong stretch period + early mux; pixel-exact vs FUSE. v0.97.0.      |
| 14/7   | 7h   | Agent-team day, 8 reviewed branches (3 REJECT). 55/59b BIFROST/NIRVANA pinned 48k/128k/plus3;        |
|        |      | 56/58 NR 0x05 frame-latched; 57 CLOSED (SD2, tape-save→TAP, DeciLoad 3 root causes). Skips 21→10.    |
| 15/7   | 12h  | Task 27: emulation DOUBLED — boot-nextzxos 57.4→114.8M T/s, 202fps@400% (closes the 75fps gap).      |
|        |      | 14 reviewed branches, profile-guided. Issue #8 crash; 60a/60b; 13a→0 skips; v0.99.0 ChangeLog.       |
| TOTAL: | 367h |                                                                                                      |
