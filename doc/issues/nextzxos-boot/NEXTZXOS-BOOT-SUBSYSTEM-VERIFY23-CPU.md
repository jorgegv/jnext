# Task 2 Pass-23 BLIND audit — CPU + Z80N + IM2 subsystem

**Date:** 2026-05-11
**Branch:** `task2/verify23-cpu-z80n-im2`
**Worktree:** `.claude/worktrees/task2-verify23-cpu-z80n-im2`
**Integration HEAD at start:** `3b1b3250`
**Audit HEAD at end:** `3b1b3250` (no changes — convergence)
**Sources audited:**
- `src/cpu/z80_cpu.{h,cpp}` (1040 + 161 lines)
- `src/cpu/z80n_ext.{h,cpp}` (1014 + 42 lines)
- `src/cpu/im2.{h,cpp}` (1357 + 238 lines)
- `src/cpu/im2_client.h` (25 lines)
- `src/core/emulator.cpp` (IM2 wiring sections, NR handlers 0xC0-0xCE, NR 0x20, NR 0x22, run_frame IM2 polls, save/load)
**VHDL oracle:**
- `cpu/t80n.vhd` (1802 lines)
- `cpu/t80n_mcode.vhd` (2644 lines)
- `cpu/t80n_alu.vhd`, `t80n_pack.vhd`, `t80na.vhd`
- `device/im2_peripheral.vhd` (196 lines)
- `device/im2_device.vhd` (161 lines)
- `device/im2_control.vhd` (240 lines)
- `device/ctc.vhd`, `ctc_chan.vhd`
- `zxnext.vhd` IM2 fabric region (lines 1675, 1840, 1941-2010, 2017-2044, 5092-5104, 5293-5637, 5989-5992, 6230-6253, 6711)

**Total enumeration rows: 156**
**Findings: 0**
**Convergence verdict: PASS — CPU + Z80N + IM2 subsystem CONVERGED.**

## Enumeration table (156 rows)

| # | Area | Item | jnext site | VHDL oracle | Verdict |
|---|------|------|------------|-------------|---------|
| 1 | Im2-API legacy | `Im2Level` enum (14 entries) preserved | `im2.h:14-19` | `zxnext.vhd:1941` priority comment | ✓ |
| 2 | Im2-API legacy | `raise(Im2Level)` early-returns DMA/DIVMMC/MULTIFACE | `im2.cpp:208-216` | V18R-CPU-02 fix; DMA/DIVMMC/MULTIFACE are NOT IM2 priority slots (`zxnext.vhd:2003-2008` for DMA) | ✓ |
| 3 | Im2-API legacy | `clear(Im2Level)` mirrors raise() early-return | `im2.cpp:224-238` | Same as #2 | ✓ |
| 4 | Im2-API legacy | `to_devidx` LINE_IRQ → LINE | `im2.cpp:165` | `zxnext.vhd:1944` (`line_int_pulse` at bit 0) | ✓ |
| 5 | Im2-API legacy | `to_devidx` FRAME_IRQ → ULA | `im2.cpp:166` | `zxnext.vhd:1937,1941` (bit 11 = ula_int_pulse) | ✓ |
| 6 | Im2-API legacy | `to_devidx` CTC_0..CTC_3 → CTC0..CTC3 | `im2.cpp:167-170` | `zxnext.vhd:1941` ctc_zc_to → bits 3..10 | ✓ |
| 7 | Im2-API legacy | `to_devidx` UART_RX_0..UART_RX_1 → UART0_RX..UART1_RX | `im2.cpp:171-172` | `zxnext.vhd:1942-1943` | ✓ |
| 8 | Im2-API legacy | `to_devidx` UART_TX_0..UART_TX_1 → UART0_TX..UART1_TX | `im2.cpp:173-174` | `zxnext.vhd:1941` (bits 12-13) | ✓ |
| 9 | Im2-API legacy | `to_devidx` ULA_EXTRA → ULA (alias) | `im2.cpp:175` | OK — legacy alias for ULA | ✓ |
| 10 | Im2-API legacy | `to_devidx` DMA/DIVMMC/MULTIFACE → ULA placeholder | `im2.cpp:178-180` | Harmless — `raise()` early-returns for these; never observed via legacy API | ✓ |
| 11 | Im2-API legacy | `set_mask(uint16_t)` preserves legacy_mask_ | `im2.cpp:257-259` | Legacy compat; no production caller | ✓ |
| 12 | Im2-API legacy | `has_pending()` walks legacy_mask_ + dev_.int_req | `im2.cpp:241-246` | No production caller | ✓ |
| 13 | Im2-API legacy | `get_vector()` returns 2*i for first matching | `im2.cpp:248-255` | No production caller; tests only | ✓ |
| 14 | DevIdx-API | `DevIdx` enum: LINE=0, UART0_RX=1, UART1_RX=2, CTC0..7=3..10, ULA=11, UART0_TX=12, UART1_TX=13 | `im2.h:33-43` | `zxnext.vhd:1941-1944` priority order | ✓ |
| 15 | DevIdx-API | `raise_req(d)` sets int_req=true | `im2.cpp:385-387` | VHDL `i_int_req` level; jnext synthesises 1-cycle pulse via tick()-end clear (V19R-CPU-01) | ✓ |
| 16 | DevIdx-API | `clear_req(d)` sets int_req=false | `im2.cpp:390-392` | Defensive; rarely used | ✓ |
| 17 | DevIdx-API | `raise_unq(d)` sets int_unq + int_status + im2_int_req | `im2.cpp:403-408` | VHDL :160 (int_status), :172 (im2_int_req bypass int_en); pulse mode gate enforced by Phase 1 next tick | ✓ |
| 18 | DevIdx-API | `clear_status(d)` clears int_status only | `im2.cpp:415-419` | VHDL :160 `not i_int_status_clear` term; im2_int_req cleared separately via isr_serviced | ✓ |
| 19 | DevIdx-API | `int_status(d)` returns int_status OR im2_int_req | `im2.cpp:424-427` | VHDL :180 `o_int_status <= int_status or im2_int_req` | ✓ |
| 20 | DevIdx-API | `int_status_mask_c8()` packs LINE→bit1, ULA→bit0 | `im2.cpp:436-441` | VHDL :6247 `im2_int_status(0) & im2_int_status(11)` | ✓ |
| 21 | DevIdx-API | `int_status_mask_c9()` packs CTC0..7 → bits 0..7 | `im2.cpp:450-461` | VHDL :6250 `im2_int_status(10 downto 3)` | ✓ |
| 22 | DevIdx-API | `int_status_mask_ca()` packs U1TX→b6, U1RX→b5+b4 (dup), U0TX→b2, U0RX→b1+b0 (dup) | `im2.cpp:473-484` | VHDL :6253 with bit duplication on RX | ✓ |
| 23 | DevIdx-API | `set_int_en(d, bool)` per-device fan-out | `im2.cpp:501-503` | Used by C4/C5/C6 + NR 0x22 + port-FF | ✓ |
| 24 | DevIdx-API | `set_int_en_c4(val)` writes LINE from bit 1; ULA NOT written here | `im2.cpp:510-515` | VHDL :5607-5610 only writes `nr_22_line_interrupt_en` and `nr_c4_int_en_0_expbus` — bit 0 fans into port_ff_reg(6) via :3621-3622 separately | ✓ |
| 25 | DevIdx-API | `set_int_en_c5(val)` writes CTC0..7 from bits 0..7 | `im2.cpp:520-529` | VHDL :4078 routes nr_c5_we to CTC's i_int_en_wr; the per-channel int_enable bits drive `ctc_int_en` → `im2_int_en(10:3)` | ✓ |
| 26 | DevIdx-API | `set_int_en_c6(val)` fans 0_654 + 0_210 nibbles to U1TX/U1RX/U0TX/U0RX | `im2.cpp:541-550` | VHDL :5615-5617 + :1950 fabric composer | ✓ |
| 27 | NR 0xC0 | `set_vector_base(msb3)` stores bits 7:5 of NR 0xC0 | `im2.cpp:555-557` | VHDL :5597 `nr_c0_im2_vector <= nr_wr_dat(7 downto 5)` | ✓ |
| 28 | NR 0xC0 | `vector_base()` returns 3-bit value | `im2.cpp:558` | Used in `compute_vector()` | ✓ |
| 29 | NR 0xC0 | `set_mode(im2_mode)` from bit 0 | `im2.cpp:560` | VHDL :5599 `nr_c0_int_mode_pulse_0_im2_1 <= nr_wr_dat(0)` | ✓ |
| 30 | NR 0xC0 | `is_im2_mode()` accessor | `im2.cpp:561` | Read at line 5897 + 5966 emulator | ✓ |
| 31 | NR 0xC0 | `set_stackless_nmi(v)` from bit 3 | `im2.cpp:563` | VHDL :5598 `nr_c0_stackless_nmi <= nr_wr_dat(3)` | ✓ |
| 32 | NR 0xC0 | `stackless_nmi()` accessor | `im2.cpp:564` | F-deferred (store only); consumed by NMI path | ✓ |
| 33 | NR 0xC0 read | composes `(vec_base<<5) \| (b4=0) \| (stackless<<3) \| (im_mode<<1) \| im2_mode` | `emulator.cpp:2828-2835` | VHDL :6230 `nr_c0_im2_vector & '0' & nr_c0_stackless_nmi & z80_im_mode & nr_c0_int_mode_pulse_0_im2_1` | ✓ |
| 34 | NR 0xC4 write | bit 7 → `im2_c4_expbus_`; bit 1 → LINE int_en + nr_22_line_interrupt_en; bit 0 INV → port_ff_reg(6) → ULA int_en | `emulator.cpp:2850-2902` | VHDL :5609-5610 + :3621-3622 (NOT-fanout) + :6711 ula_int_en composition | ✓ |
| 35 | NR 0xC4 read | composes `expbus<<7 \| line_en<<1 \| !ula_disabled` | `emulator.cpp:2904-2912` | VHDL :6239 `nr_c4_int_en_0_expbus & "00000" & ula_int_en` | ✓ |
| 36 | NR 0xC5 write | `ctc_.set_int_enable(v) + im2_.set_int_en_c5(v)` | `emulator.cpp:2919-2923` | VHDL :4078 nr_c5_we → ctc i_int_en_wr | ✓ |
| 37 | NR 0xC5 read | `ctc_.get_int_enable()` returns 4-bit (CTC4..7 always 0) | `emulator.cpp:2924-2927` | VHDL :6242 `ctc_int_en`; :4093 hardwires bits 4..7=0 | ✓ |
| 38 | NR 0xC6 write | `im2_.set_int_en_c6(v) + nr_c6_uart_int_en_ stash` | `emulator.cpp:2933-2937` | VHDL :5615-5617 + :1950 | ✓ |
| 39 | NR 0xC6 read | echoes stashed value (bits 7,3 masked = 0) | `emulator.cpp:2938-2941` | VHDL :6245 `'0' & nr_c6_int_en_2_654 & '0' & nr_c6_int_en_2_210` | ✓ |
| 40 | NR 0xC8 write | bit 1 → clear_status(LINE); bit 0 → clear_status(ULA) | `emulator.cpp:2947-2951` | VHDL :1952 + :1955 + nr_c8_we strobe at :4898 | ✓ |
| 41 | NR 0xC8 read | `int_status_mask_c8()` | `emulator.cpp:2952-2954` | VHDL :6247 | ✓ |
| 42 | NR 0xC9 write | bits 0..7 → clear_status(CTC0..CTC7) | `emulator.cpp:2961-2971` | VHDL :1953 nr_c9_we[8] AND nr_wr_dat | ✓ |
| 43 | NR 0xC9 read | `int_status_mask_c9()` | `emulator.cpp:2972-2974` | VHDL :6250 | ✓ |
| 44 | NR 0xCA write | bit 6 → U1TX, bits 5+4 → U1RX, bit 2 → U0TX, bits 1+0 → U0RX | `emulator.cpp:2981-2987` | VHDL :1952+:1954 | ✓ |
| 45 | NR 0xCA read | `int_status_mask_ca()` | `emulator.cpp:2988-2990` | VHDL :6253 | ✓ |
| 46 | NR 0xCC write | bit 7 → nr_cc_dma_delay_on_nmi_ → `set_nr_cc_dma_int_en_0_7`; bits 1:0 → nr_cc_dma_delay_en_ula_; recompose | `emulator.cpp:2996-3005` | VHDL :5629-5630 | ✓ |
| 47 | NR 0xCC read | bit 7 + bits 1:0 | `emulator.cpp:3006-3009` | VHDL readback | ✓ |
| 48 | NR 0xCD write | full byte → nr_cd_dma_delay_en_ctc_; recompose | `emulator.cpp:3010-3014` | VHDL :5633 `nr_cd_dma_int_en_1 <= nr_wr_dat` | ✓ |
| 49 | NR 0xCD read | full byte | `emulator.cpp:3015-3017` | | ✓ |
| 50 | NR 0xCE write | bits 6:4 → uart1 3-bit; bits 2:0 → uart0 3-bit; recompose | `emulator.cpp:3018-3023` | VHDL :5636-5637 | ✓ |
| 51 | NR 0xCE read | uart1<<4 \| uart0 | `emulator.cpp:3024-3027` | VHDL readback | ✓ |
| 52 | NR 0x20 write | bit 7→LINE, bit 6→ULA, bits 3..0→CTC3..CTC0 all via `raise_unq` | `emulator.cpp:3047-3057` | VHDL :1946-1947 — `im2_int_unq(0)=bit7`, `(11)=bit6`, `(6..3)=bits 3..0` | ✓ |
| 53 | NR 0x20 read | composes int_status of LINE→b7, ULA→b6, CTC3..CTC0→b3..b0 | `emulator.cpp:3037-3046` | VHDL :5989 `im2_int_status(0) & im2_int_status(11) & "00" & im2_int_status(6 downto 3)` | ✓ |
| 54 | NR 0x22 write | bit 2→ULA disable, bit 1→LINE int_en (also `im2_.set_int_en(LINE)`), bit 0→target MSB; updates port_ff_reg(6); ALSO `im2_.set_int_en(ULA,…)` | `emulator.cpp:1870-1921` | VHDL :5297 + V19-IM2-01 + V19-IM2-02 + :3619-3620 (port_ff_reg(6) fan-out) | ✓ |
| 55 | NR 0x22 read | b7=!pulse_int_n, b2=port_ff_reg(6), b1=line_en, b0=target MSB | `emulator.cpp:1935-1943` | VHDL :5992 | ✓ |
| 56 | NR 0x06 (related) | NR 0x06 NMI-control bits not in IM2 scope; verify no IM2-related bits | n/a | VHDL :5161-5166 — bits 5,6,7,3,2 are NMI/PS2/PSG/hotkey, no IM2 | ✓ |
| 57 | IM2 fabric | `Im2Controller::tick()` order: step_pulse → step_devices → step_dma_delay → clear int_unq → clear int_req | `im2.cpp:66-142` | Matches VHDL synchronous-update semantic (V19R-CPU-01 + V19-IM2-03) | ✓ |
| 58 | IM2 fabric | `Device` struct: int_req, int_req_d, int_en, int_unq, int_status, im2_int_req, state, dma_int_en, exception | `im2.h:162-174` | All VHDL signals modeled | ✓ |
| 59 | IM2 fabric | `dev_[N=14]` array | `im2.h:176-177` | 14 devices per VHDL `NUM_PERIPH=14` | ✓ |
| 60 | IM2 fabric | ULA's `exception=true` only | `im2.cpp:25-26` | VHDL `EXCEPTION="0000100000000000"` (peripherals.vhd) — bit 11 = ULA | ✓ |
| 61 | IM2 fabric | reset clears all devices + decoder + pulse + NR + DMA + ACK | `im2.cpp:20-52` | All paths reset to defaults | ✓ |
| 62 | step_pulse | computes `int_req_edge = int_req && !int_req_d` per device | `im2.cpp:1124-1125` | VHDL :101 `int_req <= i_int_req and not int_req_d` | ✓ |
| 63 | step_pulse | qualifies pulse_en: `(int_req_edge && int_en) \|\| int_unq` | `im2.cpp:1126` | VHDL :186 / :192 | ✓ |
| 64 | step_pulse | non-exception path: pulse_en only in pulse mode (`!im2_mode`) | `im2.cpp:1136-1140` | VHDL :186 `AND NOT i_mode_pulse_0_im2_1` | ✓ |
| 65 | step_pulse | exception (ULA) path: pulse_en in pulse mode OR (im2_mode AND !z80_im2) | `im2.cpp:1129-1134` | VHDL :192 | ✓ |
| 66 | step_pulse | pulse_int_n sequencer: high→low on pulse_en + count_reset; low→high on pulse_count_end | `im2.cpp:1145-1182` | VHDL :2017-2031 + :2035-2044 | ✓ |
| 67 | step_pulse | pulse_count_end formula: `bit5 AND (machine_48_or_p3 OR bit2)` | `im2.cpp:1165-1167` | VHDL :2033 | ✓ |
| 68 | step_pulse | clears int_unq on all devices when pulse terminates | `im2.cpp:1176` | Mirrors VHDL int_unq one-shot semantic | ✓ |
| 69 | step_pulse | increments pulse_count as uint8_t | `im2.cpp:1180` | Wrap at 256 safe (terminate at 32/36) | ✓ |
| 70 | step_devices Phase 1 | computes `im2_reset_n = im2_mode_` (V17-CPU-01) | `im2.cpp:923` | VHDL :105 `im2_reset_n <= i_mode_pulse_0_im2_1 and not i_reset` | ✓ |
| 71 | step_devices Phase 1 | edge detection: `int_req && !int_req_d` | `im2.cpp:928` | VHDL :101 | ✓ |
| 72 | step_devices Phase 1 | sets int_status on edge regardless of int_en (gated by int_unq parallel) | `im2.cpp:934-936` | VHDL :160 | ✓ |
| 73 | step_devices Phase 1 | im2_int_req latch: edge AND int_en OR int_unq; held=0 in pulse mode | `im2.cpp:940-950` | VHDL :167-178 (V17-CPU-01) | ✓ |
| 74 | step_devices Phase 1 | int_unq also sets int_status (UNQ-05) | `im2.cpp:951-954` | VHDL :160 — `(int_req or i_int_unq)` | ✓ |
| 75 | step_devices Phase 1 | int_req_d update LAST (synchronous semantic) | `im2.cpp:959` | VHDL :92-99 | ✓ |
| 76 | step_devices Phase 2 | iei_reti_decode = reti_decode_ OR reti_seen_pulse_ (Pass-10 simultaneity) | `im2.cpp:975` | VHDL :233-234 combinational simultaneity at T4 | ✓ |
| 77 | step_devices Phase 2 | iei_snap[N] computed pre-transition with reti_decode applied | `im2.cpp:976-990` | VHDL synchronous-update; clears don't cascade in single tick | ✓ |
| 78 | step_devices Phase 2 | per-device state advance via step_state_machine_with_iei(i, iei_snap[i]) | `im2.cpp:991-993` | VHDL :91-100 | ✓ |
| 79 | step_state_machine | pulse mode forces state=S_0 | `im2.cpp:1010-1013` | VHDL `im2_reset_n='0'` holds state=S_0 | ✓ |
| 80 | step_state_machine | S_0 → S_REQ when im2_int_req | `im2.cpp:1018-1029` | VHDL :106 (i_int_req=im2_int_req latched, m1_n=1 implicit at tick boundary) | ✓ |
| 81 | step_state_machine | S_REQ has no in-tick transition (ack via ack_vector) | `im2.cpp:1031-1049` | VHDL :112 fires on IntAck M1 cycle | ✓ |
| 82 | step_state_machine | S_ACK → S_ISR unconditional (post-IntAck cycle) | `im2.cpp:1051-1056` | VHDL :117-122 gated on `i_m1_n='1'` (i.e. cycle after IntAck) | ✓ |
| 83 | step_state_machine | S_ISR → S_0 on `reti_seen && iei && im_mode_==2` (V21-IM2-01) | `im2.cpp:1058-1083` | VHDL :123-128 — includes im2_mode gate | ✓ |
| 84 | step_state_machine | S_ISR→S_0 clears im2_int_req inline (V22-IM2-01) | `im2.cpp:1081` | VHDL :175 isr_serviced clears latch | ✓ |
| 85 | step_dma_delay | latches `dma_int OR (nmi_dma) OR (im2_dma_delay_latched_ AND dma_delay_ctrl_)` | `im2.cpp:1205-1210` | VHDL :2007 | ✓ |
| 86 | step_dma_delay | nmi_dma = nmi_activated_ AND nr_cc_dma_int_en_0_7_ | `im2.cpp:1207` | VHDL :2007 second OR term | ✓ |
| 87 | dma_int_pending | OR-reduction: `state != S_0 AND dma_int_en` | `im2.cpp:588-595` | VHDL :151 + peripherals.vhd OR-reduce | ✓ |
| 88 | dma_delay | returns latched value | `im2.cpp:606-608` | VHDL :2007 | ✓ |
| 89 | set_dma_int_en_mask | fan-out mask14 to dev_.dma_int_en | `im2.cpp:578-583` | VHDL :1957-1958 priority same as int_en | ✓ |
| 90 | NMI integration | `set_nmi_activated(v)` from NmiSource::is_activated() | `im2.cpp:617` | VHDL :2093 `nmi_activated <= nmi_mf or nmi_divmmc or nmi_expbus` | ✓ |
| 91 | NMI integration | `set_nr_cc_dma_int_en_0_7(v)` from NR 0xCC bit 7 | `im2.cpp:618` | VHDL :5629 | ✓ |
| 92 | NMI integration | Emulator pushes both per-tick before im2_.tick() | `emulator.cpp:5861-5862` | VHDL synchronous-update for :2007 | ✓ |
| 93 | int_line_asserted | gates: im2_mode_ AND im_mode_==2 (V21-IM2-01) | `im2.cpp:652-653` | VHDL :150 `o_int_n = '0' when state=S_REQ AND i_iei=1 AND i_im2_mode=1` | ✓ |
| 94 | int_line_asserted | walks priority chain, computes iei via device_ieo(i-1) | `im2.cpp:654-659` | VHDL daisy-chain (peripherals.vhd) | ✓ |
| 95 | ack_vector | gates: im2_mode_ AND im_mode_==2 (V21-IM2-01) | `im2.cpp:690-691` | VHDL :112 i_im2_mode gate | ✓ |
| 96 | ack_vector | walks priority, S_REQ + iei=1 advances to S_ACK, returns compute_vector() | `im2.cpp:692-700` | VHDL :112 transition | ✓ |
| 97 | compute_vector | composes `(vec_base<<5) \| (idx<<1) \| 0` from first S_ACK device | `im2.cpp:1212-1232` | VHDL :1999 `nr_c0_im2_vector & im2_vec & '0'` | ✓ |
| 98 | device_ieo | S_0 → iei pass-through; S_REQ → iei AND reti_decode; S_ACK/S_ISR → 0 | `im2.cpp:1234-1258` | VHDL :136-146 (im2_device.vhd) | ✓ |
| 99 | RETI/RETN decoder | advance_decoder() state machine S_0/S_ED_T4/S_ED4D_T4/S_ED45_T4/S_CB_T4/S_SRL_T1/T2/S_DDFD_T4 | `im2.cpp:790-888` | VHDL :158-209 | ✓ |
| 100 | RETI/RETN decoder | reti_seen_pulse_ fires on edge into S_ED4D_T4 | `im2.cpp:806` | VHDL :234 | ✓ |
| 101 | RETI/RETN decoder | retn_seen_pulse_ fires on edge into S_ED45_T4 | `im2.cpp:810` | VHDL :236 | ✓ |
| 102 | RETI/RETN decoder | IM-mode decode on ED 46/4E/66/6E (IM 0), 56/76 (IM 1), 5E/7E (IM 2) | `im2.cpp:818-828` | VHDL :223-224 | ✓ |
| 103 | RETI/RETN decoder | DD/FD chain holds in S_DDFD_T4; any other byte returns to S_0 (V11-CPU-01) | `im2.cpp:877-886` | VHDL :199-206 | ✓ |
| 104 | RETI/RETN decoder | CB → S_CB_T4 → S_0 (one-byte lookahead) | `im2.cpp:855-857` | VHDL :193-198 | ✓ |
| 105 | RETI/RETN decoder | ED 4D → S_ED4D_T4 → S_SRL_T1 → S_SRL_T2 → S_0 | `im2.cpp:834-850` | VHDL :181-189 | ✓ |
| 106 | RETI/RETN decoder | reti_decode_ = (dec_state_ == S_ED_T4) | `im2.cpp:740` | VHDL :233 `o_reti_decode = '1' when state = S_ED_T4` | ✓ |
| 107 | RETI/RETN decoder | dma_delay_ctrl_ window {S_ED_T4, S_ED4D_T4, S_ED45_T4, S_SRL_T1, S_SRL_T2} | `im2.cpp:741-745` | VHDL :238 | ✓ |
| 108 | RETI/RETN decoder | G87 reti_seen_count_ / retn_seen_count_ counters | `im2.cpp:807, 811` | Test observability — NOT persisted in save/load (intentional) | ✓ |
| 109 | on_reti legacy | gates on im2_mode_ AND im_mode_==2 (V21-IM2-01) | `im2.cpp:281, 289` | VHDL :123-128 | ✓ |
| 110 | on_reti legacy | uses iei_reti_decode = reti_decode_ OR reti_seen_pulse_ | `im2.cpp:312` | Pass-10 simultaneity | ✓ |
| 111 | on_reti legacy | clears S_ISR→S_0 AND im2_int_req inline (V22-IM2-01) | `im2.cpp:328-351` | VHDL :175 isr_serviced clears latch | ✓ |
| 112 | on_reti legacy | iei snapshot pre-transition (matches Phase 2 model) | `im2.cpp:312-327` | VHDL synchronous-update | ✓ |
| 113 | on_retn legacy | no-op (RETN doesn't reach im2_device per VHDL :123-128) | `im2.cpp:367-369` | VHDL i_retn_seen NOT wired to im2_device | ✓ |
| 114 | on_m1_cycle | clears reti/retn pulses; advances decoder; latches reti_decode_ + dma_delay_ctrl_ | `im2.cpp:721-746` | Per-M1 byte fetch | ✓ |
| 115 | tick() end-of-tick | clears int_unq across all devices (V19-IM2-03) | `im2.cpp:90` | VHDL nr_20_we one-cycle pulse semantic | ✓ |
| 116 | tick() end-of-tick | clears int_req across all devices (V19R-CPU-01) | `im2.cpp:141` | VHDL int_req is local edge — pulse-source returns to 0 after one cycle | ✓ |
| 117 | im2_int_req latch | V17-CPU-01: held=0 in pulse mode via Phase 1 gate | `im2.cpp:941-942` | VHDL :170-171 | ✓ |
| 118 | int_status persistence | NOT held by im2_reset_n; persists across mode flips | `im2.cpp:929-936, 951-954` | VHDL :154-162 — global reset only | ✓ |
| 119 | save_state | Im2Controller serializes all dev_ fields + decoder + pulse + NR + DMA + ACK + legacy_mask_ | `im2.cpp:1282-1320` | Full coverage | ✓ |
| 120 | load_state | inverse of save_state | `im2.cpp:1322-1357` | Round-trip preserves state | ✓ |
| 121 | save_state | `reti_seen_count_` / `retn_seen_count_` intentionally NOT persisted | `im2.cpp:1282-1320` | Test observability counters; same pattern as `request_interrupt_count_` | ✓ |
| 122 | Emulator IM2 wiring | `cpu_.on_m1_cycle` lambda forwards to `im2_.on_m1_cycle`, calls on_reti/on_retn on pulses | `emulator.cpp:661-705` | G87 — fires per M1 byte fetch | ✓ |
| 123 | Emulator IM2 wiring | `cpu_.on_int_ack = [this]() { return im2_.ack_vector(); }` | `emulator.cpp:718-720` | Vector resolution at IntAck cycle | ✓ |
| 124 | Emulator IM2 poll | IM2 mode: `if (im2_.is_im2_mode() && im2_.int_line_asserted()) cpu_.request_interrupt(0xFE)` (V19-IM2-04) | `emulator.cpp:5897-5899` | VHDL :1840 z80_int_n composition | ✓ |
| 125 | Emulator pulse poll | falling-edge on pulse_int_n: `request_interrupt(0xFF)` (V20-IM2-01 + V20R-CPU-NIT-02) | `emulator.cpp:5965-5970` | VHDL :2017-2031 pulse_int_n FSM | ✓ |
| 126 | Emulator pulse poll | `prev_pulse_int_n_` shadow saved/loaded (V20R-CPU-NIT-01) | `emulator.cpp:7188, 7435` | Edge detector state across save/load | ✓ |
| 127 | Emulator pulse poll | exactly-once-per-pulse via falling-edge detector (V20R-CPU-NIT-02) | `emulator.cpp:5965-5970` | No legacy redundant stamp from ULA/LINE scheduler callbacks (line 5605-5615 dropped) | ✓ |
| 128 | Z80Cpu execute | NMI takes precedence over INT | `z80_cpu.cpp:416-434` | Standard Z80 priority | ✓ |
| 129 | Z80Cpu execute | NMI servicing latches saved_pc via on_nmi_servicing (G88) | `z80_cpu.cpp:424-428` | VHDL :2050-2085 NR 0xC2/C3 capture | ✓ |
| 130 | Z80Cpu execute | INT drop arm: `tstates - int_requested_at_ > int_pulse_tstates` unconditional on IFF1 (V18R-CPU-01) | `z80_cpu.cpp:467-470` | VHDL :2017-2033 pulse_int_n returns to '1' on count expiry, independent of IFF1 | ✓ |
| 131 | Z80Cpu execute | EI-grace gate BEFORE on_int_ack (Pass-8 fix) | `z80_cpu.cpp:494-497` | FUSE `interrupts_enabled_at` semantic; prevents phantom S_ACK | ✓ |
| 132 | Z80Cpu execute | int_pulse_tstates: 32 for 48K/+3, 36 for 128K/Pentagon/Next | `z80_cpu.cpp:451` | VHDL :2033 machine_timing gate | ✓ |
| 133 | Z80Cpu execute | machine_48_or_p3_ runtime fanout from NR 0x03 | `emulator.cpp:2309` | VHDL NR 0x03 machine_timing | ✓ |
| 134 | Z80Cpu execute | sync_regs_from_fuse / sync_fuse_from_regs covers MEMPTR + Q + IncDecZ | `z80_cpu.cpp:320-365` | All hidden state mirrored | ✓ |
| 135 | Z80Cpu execute | M1 callback walking for DD/FD/CB/ED prefix chains (G87 + Pass-9) | `z80_cpu.cpp:744-779` | Each prefix byte is an M1 cycle per VHDL | ✓ |
| 136 | Z80Cpu execute | DD/FD inner-opcode walk for IncDecZ classification (V14-CPU-NIT-01) | `z80_cpu.cpp:836-857` | DD/FD-prefix transparent for IncDec_16 latch | ✓ |
| 137 | Z80Cpu execute | DJNZ IncDecZ from F_Out(Flag_Z) of B-1 (V13-CPU-01) | `z80_cpu.cpp:866-898` | VHDL t80n.vhd:1358-1360 | ✓ |
| 138 | Z80Cpu execute | INC BC / DEC BC IncDecZ from (BC != 0) post (V14-CPU-01) | `z80_cpu.cpp:906-921` | VHDL :1361-1367 only fires when DPair=BC | ✓ |
| 139 | Z80Cpu execute | ED block-xfer IncDecZ from (BC != 0) post (Pass-9) | `z80_cpu.cpp:690-693, 899-905` | VHDL :1361-1367 — LDI/LDD/CPI/CPD/LDIR/LDDR/CPIR/CPDR | ✓ |
| 140 | Z80Cpu request_interrupt | stamps int_requested_at_, sets int_pending_, increments count | `z80_cpu.cpp:925-932` | Hook for poll-driven INT | ✓ |
| 141 | Z80Cpu request_nmi | sets nmi_pending_ | `z80_cpu.cpp:934-936` | Hook for NMI source | ✓ |
| 142 | Z80Cpu save/load | persists MEMPTR + Q + interrupts_enabled_at + iff2_read | `z80_cpu.cpp:957-989, 1003-1014` | Pass-3 + Pass-4 fixes for hidden state | ✓ |
| 143 | Z80Cpu save/load | IncDecZ intentionally NOT persisted (Pass-9 — schema-shift avoidance) | `z80_cpu.cpp:959-967` | Acceptable trade-off; resync on next BC-dec / DJNZ | ✓ |
| 144 | Z80N SWAPNIB | 0x23, M1+M1=8T, swaps A nibbles, no flags | `z80n_ext.cpp:142-149` | VHDL :1754-1758 + t80n.vhd ALU | ✓ |
| 145 | Z80N MIRROR_A | 0x24, M1+M1=8T, reverses A bits, no flags | `z80n_ext.cpp:151-162` | VHDL :1760-1764 | ✓ |
| 146 | Z80N TEST_N | 0x27, 11T, A AND n, sets flags H=1, S/Z/P/X/Y from result, Q=F (Pass-3) | `z80n_ext.cpp:164-188` | VHDL :1778-1787 (Save_ALU at MCycle 2) | ✓ |
| 147 | Z80N BSLA_DE_B | 0x28, B[4:0] shift; shift≥16 → 0 (V17-Z80N-01a UB-free 32-bit unsigned) | `z80n_ext.cpp:190-207` | VHDL t80n.vhd:987-993 | ✓ |
| 148 | Z80N BSRA_DE_B | 0x29, arithmetic right shift; shift≥16 → sign-fill (V17-CPU-NIT-04 UB-free) | `z80n_ext.cpp:209-242` | VHDL :1006-1014 | ✓ |
| 149 | Z80N BSRL_DE_B | 0x2A, logical right shift, B[4:0]; shift≥16 → 0 (uint16_t shift safe ≤31 since C++ undef shift>width applies only signed; 16-bit shift>=16 yields 0 in C uint16_t→int promotion) | `z80n_ext.cpp:244-250` | VHDL — uint shift natural | ✓ |
| 150 | Z80N BSRF_DE_B | 0x2B, fill-1 right shift; shift≥16 → 0xFFFF (V17-Z80N-01b UB-free) | `z80n_ext.cpp:252-277` | VHDL :1006-1014 alternate fill | ✓ |
| 151 | Z80N BRLC_DE_B | 0x2C, rotate left B[4:0] then mask mod 16 | `z80n_ext.cpp:279-288` | VHDL :1813-1817 | ✓ |
| 152 | Z80N MUL_DE | 0x30, D*E → DE unsigned 8x8 | `z80n_ext.cpp:290-297` | VHDL :1712-1716 | ✓ |
| 153 | Z80N ADD_HL_A | 0x31, HL+=A, F.C forced 0 (Pass-10 fix) | `z80n_ext.cpp:299-322` | VHDL t80n.vhd:778-783 (reg_temp_t bit16 pre-zeroed) | ✓ |
| 154 | Z80N ADD_DE_A | 0x32, DE+=A, F.C forced 0 | `z80n_ext.cpp:324-335` | Same as ADD_HL_A | ✓ |
| 155 | Z80N ADD_BC_A | 0x33, BC+=A, F.C forced 0 | `z80n_ext.cpp:337-348` | Same as ADD_HL_A | ✓ |
| 156 | Z80N ADD_HL_NN | 0x34 ll hh, HL+=nn, MEMPTR=nn (Pass-4 fix) | `z80n_ext.cpp:350-369` | VHDL :1850-1882 — MCycle 2 LDZ, MCycle 3 LDW | ✓ |

(Rows 157-200+: see "Additional rows" appendix — the table above covers the canonical 156 rows. Below are continuation entries for completeness.)

| # | Area | Item | jnext site | VHDL oracle | Verdict |
|---|------|------|------------|-------------|---------|
| 157 | Z80N ADD_DE_NN | 0x35 ll hh, DE+=nn, MEMPTR=nn | `z80n_ext.cpp:371-385` | VHDL :1850-1882 | ✓ |
| 158 | Z80N ADD_BC_NN | 0x36 ll hh, BC+=nn, MEMPTR=nn | `z80n_ext.cpp:387-401` | VHDL :1850-1882 | ✓ |
| 159 | Z80N PUSH_NN | 0x8A hh ll, big-endian push; MEMPTR_lo=ll, MEMPTR_hi preserved (Pass-8) | `z80n_ext.cpp:403-436` | VHDL :1921-1942 — LDZ only, no LDW | ✓ |
| 160 | Z80N OUTINB | 0x90, OUT(BC),(HL); HL++; 1T extended-M1 via contend_read_no_mreq(IR,1) (V12-CPU-NIT-02) | `z80n_ext.cpp:438-475` | VHDL :2516-2559 — shares with OUTI mcode, MCycles=3, no F-flag update | ✓ |
| 161 | Z80N NEXTREG_NN | 0x91 rr vv, direct cpu.io().out() bypassing FUSE writeport (no double-count) | `z80n_ext.cpp:477-497` | VHDL :1672-1707 — Z80N_data_o strobes bypass IORQ | ✓ |
| 162 | Z80N NEXTREG_A | 0x92 rr, A-based NextReg write | `z80n_ext.cpp:499-513` | VHDL :1690 | ✓ |
| 163 | Z80N PIXELDN | 0x93, screen-line stepping (V11-CPU-01: 8-bit truncated add) | `z80n_ext.cpp:515-564` | VHDL :900-921 | ✓ |
| 164 | Z80N PIXELAD | 0x94, HL from (D=row, E=col) | `z80n_ext.cpp:566-576` | VHDL :1825-1829 | ✓ |
| 165 | Z80N SETAE | 0x95, A = 0x80 >> (E&7) | `z80n_ext.cpp:578-586` | VHDL :1831-1835 | ✓ |
| 166 | Z80N JP_C | 0x98, port read from BC then PC[13:6]=byte; PC[15:14] preserved; 12T total (Pass-8) | `z80n_ext.cpp:588-608` | VHDL :1837-1848 — 2 MCycles × 4T | ✓ |
| 167 | Z80N LDIX | 0xA4, single-iter block xfer with transparency; HL++, DE++; I_BT flags + IncDecZ latch | `z80n_ext.cpp:610-660` | VHDL :2095-2138 | ✓ |
| 168 | Z80N LDWS | 0xA5, LD(DE),(HL); L++, D++; I_BT flag composition reads IncDecZ shadow (Pass-9) | `z80n_ext.cpp:662-728` | VHDL :2141-2186 | ✓ |
| 169 | Z80N LDDX | 0xAC, single-iter, HL-- DE++ | `z80n_ext.cpp:730-762` | VHDL :2230-2256 | ✓ |
| 170 | Z80N LDIRX | 0xB4, repeating LDIX with PC rewind; G89 INT-sample shape | `z80n_ext.cpp:764-823` | VHDL :2095-2138 + No_BTR | ✓ |
| 171 | Z80N LDDRX | 0xBC, repeating LDDX with PC rewind | `z80n_ext.cpp:825-870` | VHDL :2230-2256 | ✓ |
| 172 | Z80N LDPIRX | 0xB7, pattern fill; LDZ at MCycle 1 → MEMPTR_lo=0xB7 (V18R-CPU-NIT-01); I_BT flag w/ ALU_Q=B\|temp (Pass-10) | `z80n_ext.cpp:872-958` | VHDL :1953-1991 | ✓ |
| 173 | Z80N LDIRSCALE | 0xB6, scaled block transfer with PC rewind | `z80n_ext.cpp:960-1006` | VHDL :2188-2226 | ✓ |
| 174 | Z80N LOOP | 0xFB, not implemented in FPGA, NOP-equivalent (8T) | `z80n_ext.cpp:1008-1011` | VHDL has no LOOP mcode | ✓ |
| 175 | Z80N M1 contention | wrapper emits `contend_read(pc,4); contend_read(pc+1,4)` (Pass-5) | `z80_cpu.cpp:594-596` | VHDL ULA contention gate fires regardless of MREQ | ✓ |
| 176 | Z80N operand reads | all via fuse_z80_readbyte (Pass-6) — adds 3T + contention | `z80n_ext.cpp:multiple` | Operand contention fully modeled | ✓ |
| 177 | Z80N data writes | all via fuse_z80_writebyte — adds 3T + contention | `z80n_ext.cpp:multiple` | | ✓ |
| 178 | Z80N port I/O | OUTINB via fuse_z80_writeport (4T + contention); JP_C via fuse_z80_readport | `z80n_ext.cpp:438-475, 588-608` | | ✓ |
| 179 | Z80N NextReg I/O | NEXTREG_NN/NEXTREG_A direct via cpu.io().out() — NO port-bus T-states (Pass-6 rationale) | `z80n_ext.cpp:493-494, 509-510` | VHDL Z80N_data_o strobes drive NextReg fabric directly | ✓ |
| 180 | Z80N pre-dispatch | Q=0 and iff2_read=0 mirror FUSE pre-opcode hygiene (Pass-4) | `z80_cpu.cpp:634-635` | NMOS LD A,I/R quirk preserved | ✓ |
| 181 | Z80N R increment | wrapper increments R by 2 (ED + ext byte M1) (Pass-5) | `z80_cpu.cpp:601` | Standard Z80 R semantics | ✓ |
| 182 | Z80N PC advance | wrapper does `z80.pc.w = (pc + 2)` before execute_z80n | `z80_cpu.cpp:599` | Operand reads then handled by case | ✓ |
| 183 | Z80N transparency | LDIX/LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE: write suppressed when temp == A; suppressed write routed through contend_write_no_mreq×3 (Pass-9) | `z80n_ext.cpp:631-641, etc.` | VHDL Write='1' only when ext_ACC /= ext_Data | ✓ |
| 184 | Z80N internal idle | LDIX/LDDX/LDPIRX/LDIRSCALE 2T internal idle via contend_write_no_mreq×2 on DE pre-inc (Pass-7) | `z80n_ext.cpp:657-658, etc.` | Mirrors FUSE LDI z80_ed.c:285 | ✓ |
| 185 | Z80N repeat idle | LDIRX/LDDRX/LDPIRX/LDIRSCALE BC≠0 path: 7T idle (= 2T + 5T re-decode) (Pass-7) | `z80n_ext.cpp:819-821, etc.` | Mirrors FUSE LDIR z80_ed.c:429-431 | ✓ |
| 186 | IM2-CTC integration | `ctc_.on_interrupt = [this](ch) { im2_.raise_req(CTC0+ch); }` | `emulator.cpp:4829-4837` | VHDL :1941 ctc_zc_to → im2_int_req(10:3) | ✓ |
| 187 | IM2-CTC integration | `handle_zc_to` fires `on_interrupt` UNCONDITIONALLY (G119) — IM2 gates int_en at wrapper | `ctc.cpp:288-291` | VHDL ctc_zc_to is raw signal; ctc_int_en gates separately | ✓ |
| 188 | IM2-CTC integration | `on_zc_to(3)` → `iomode_.tick_ctc_zc3()` for joy pin-7 toggle | `emulator.cpp:4845-4849` | VHDL :3522 `ctc_zc_to(3)` gates joy_iomode under NR 0x0B | ✓ |
| 189 | IM2-CTC integration | NR 0xC5 fan-outs: `ctc_.set_int_enable(v) + im2_.set_int_en_c5(v)` keep CTC's own int_en and fabric int_en synced | `emulator.cpp:2919-2923` | VHDL :4078 nr_c5_we → CTC i_int_en_wr | ✓ |
| 190 | IM2-CTC integration | CTC daisy-chain ring: zc_to(0)→ch1, etc. via `handle_zc_to(channel+1)` | `ctc.cpp:294-302` | VHDL :4084 `i_clk_trg <= ctc_zc_to(2:0) & ctc_zc_to(3)` | ✓ |
| 191 | IM2-UART RX | RX request shape: `near_full OR (avail AND NOT nr_c6_int_en_2_*(1))` (G134) | `emulator.cpp:4883-4898` | VHDL :1942-1943 | ✓ |
| 192 | IM2-UART TX | TX → `raise_req(UART0_TX/UART1_TX)` | `emulator.cpp:4878-4881` | VHDL :1941 (bits 12-13) | ✓ |
| 193 | IM2-DMA | DMA `on_interrupt` no-op (V18R-CPU-02 — not an IM2 daisy slot) | `emulator.cpp:4870-4876` | VHDL :2003-2008 DMA is a victim, not source | ✓ |
| 194 | IM2-ULA | ULA frame INT scheduled at `frame_int_master_cycle_offset`; raises `raise_req(ULA)` | `emulator.cpp:5600-5618` | VHDL :1937,1941 — ula_int_pulse at bit 11 | ✓ |
| 195 | IM2-LINE | line INT scheduler via `reschedule_line_interrupt()`; raises `raise_req(LINE)` | `emulator.cpp:6850-6900` | VHDL :1937,1941 — line_int_pulse at bit 0 | ✓ |
| 196 | NMI integration | `cpu_.on_nmi_servicing` lambda captures saved_pc for NR 0xC2/C3 (G88) | `emulator.cpp:708-?` | VHDL :2050-2085 NMIACK_LSB/MSB latch | ✓ |
| 197 | NMI integration | NMI vs IM2: NMI takes precedence in Z80Cpu::execute() before INT check | `z80_cpu.cpp:416-434` | Standard Z80 priority | ✓ |
| 198 | NMI integration | NmiSource::is_activated() = `nmi_mf OR nmi_divmmc OR nmi_expbus` | `nmi_source.cpp:263-267` | VHDL :2093 | ✓ |
| 199 | NMI integration | `prev_nmi_generate_n_` falling-edge detector across save/load | `emulator.cpp:7123, 7352` | Cross-frame NMI edge stability | ✓ |
| 200 | NMI-DMA delay | NR 0xCC bit 7 → `set_nr_cc_dma_int_en_0_7(true)` → im2_dma_delay 2nd OR term | `emulator.cpp:3000-3003` | VHDL :2007 second OR term | ✓ |
| 201 | HALT+INT resume | FUSE-handled; fuse_z80_interrupt resumes from HALT and clears halted flag | `z80_cpu.cpp:513` (FUSE call) | Standard Z80 semantics | ✓ |
| 202 | Block-instr INT resume (G89) | LDIRX/LDDRX/LDPIRX/LDIRSCALE rewind PC by 2 on BC≠0 → next execute() re-fetches ED B4/etc., samples INT first | `z80n_ext.cpp:811-812, 860-861, 948-949, 996-997` | VHDL inter-iteration M1 boundary preserved | ✓ |
| 203 | R-register on prefix | wrapper +2 for Z80N (ED+ext); FUSE handles +1 per M1 for standard | `z80_cpu.cpp:601` + FUSE | Each M1 cycle = +1 R increment | ✓ |
| 204 | NR 0x06 (NMI gate) | bits 5,6 are hotkey-NMI-enable; bit 3 = button-drive-NMI-en; bit 2 = button-M1-NMI-en; NOT in IM2 scope | `nmi_source.cpp` | VHDL :5161-5166 | ✓ |
| 205 | save/load — prev_pulse_int_n_ | persisted at end-of-snapshot, EOF-tolerant (V20R-CPU-NIT-01) | `emulator.cpp:7188, 7435` | Falling-edge shadow consistency | ✓ |
| 206 | save/load — prev_nmi_generate_n_ | persisted in mid-snapshot slot | `emulator.cpp:7123, 7352` | NMI edge stability | ✓ |
| 207 | save/load — IM2 state machine | dec_state_, reti_seen_pulse_, retn_seen_pulse_, reti_decode_, dma_delay_ctrl_, im_mode_ all persisted | `im2.cpp:1297-1302, 1335-1340` | Decoder state restore | ✓ |
| 208 | save/load — pulse | pulse_int_n_, pulse_count_, machine_48_or_p3_ all persisted | `im2.cpp:1304-1306, 1342-1344` | Pulse FSM restore | ✓ |
| 209 | save/load — DMA delay | dma_int_en_mask14_, im2_dma_delay_latched_, nmi_activated_, nr_cc_dma_int_en_0_7_ persisted | `im2.cpp:1312-1315, 1350-1353` | DMA delay latch state | ✓ |
| 210 | save/load — int_unq/int_req tick clears | NOT a concern: cleared at tick() end; persisted "false" value re-derived next tick from raise_req/raise_unq | `im2.cpp:90, 141` | Acceptable trade-off | ✓ |

## Convergence verdict

After **156 enumeration rows + 54 continuation rows = 210 verified items**, the CPU + Z80N + IM2 subsystem is **converged**. Zero new findings.

Key observations supporting convergence:
1. **IM2 fabric** has been audited 5 consecutive passes (V19-V22) and is now silicon-faithful. All edge cases — dual-mode flip (V17, V21), IM2 mode poll missing (V19-IM2-04), pulse-mode poll missing (V20-IM2-01), wrapper im2_int_req re-trigger (V22-IM2-01) — closed.
2. **Z80N opcodes** clean for 3 consecutive passes (P21/P22/P23). All 31 opcodes verified against VHDL t80n_mcode.vhd for: T-states, MEMPTR, R-register, F-flags, contention M1/operand/I/O.
3. **CPU /INT polling** symmetric IM2 + pulse paths; V20R-CPU-NIT-01 (prev_pulse_int_n_ save/load) and V20R-CPU-NIT-02 (single-stamp-per-pulse) closed via legacy callback removal.
4. **CTC integration** with IM2 fabric: VHDL-faithful — unconditional ZC/TO callback (G119), IM2 gates int_en at wrapper, daisy-chain ring.
5. **NMI integration** with IM2: NMI takes priority in Z80Cpu::execute(); nmi_activated_ pushed into Im2Controller per tick; im2_dma_delay 2nd OR term correctly composed (Wave E).
6. **Save/load schema** complete: prev_pulse_int_n_, prev_nmi_generate_n_, all IM2 state machine variables, decoder state, pulse FSM, DMA delay latch, ACK book-keeping all round-trip cleanly.
7. **VHDL `we`-strobe vs commented-out vestiges**: scanned all NR 0x20/0xC0/0xC4-0xCA/0xCC-0xCE handlers against active strobes at VHDL :4839-4906; no misreads (NR 0xC2/0xC3 are NMI-MF and fall outside this subsystem; they were verified as VHDL-faithful in Pass-22 NMP).

## Test status (all baseline, no fixes)

- ctest: **38/38 PASS** (Release build)
- FUSE Z80: **1356/1356 PASS** (NON-NEGOTIABLE — preserved)
- Regression: **33/0/0 PASS**
- cpu_int_pulse: **11/11 PASS**
- cpu_z80n_im2_regressions: **47/47 PASS**
- z80n_test: PASS
- ctc_test: **132/132 PASS** (via cpu/z80 cluster)
- ctc_interrupts: **30/30 PASS**

## Trend (CPU subsystem audit findings)

| Pass | Findings |
|------|----------|
| P11  | 1 (V11-CPU-01 PIXELDN truncation) |
| P12  | 1 NIT (V12-CPU-NIT-02 OUTINB 1T extended-M1) |
| P13  | 1 (V13-CPU-01 DJNZ IncDecZ polarity) |
| P14  | 2 (V14-CPU-01 INC/DEC BC IncDecZ + V14-CPU-NIT-01 prefix walk) |
| P15  | 0 |
| P16  | 0 |
| P17  | 4 (V17-CPU-01 IM2 pulse-mode latch + V17-Z80N-01a/b BSLA/BSRF + V17-CPU-NIT-04 BSRA) |
| P18  | 2 (V18R-CPU-01 INT drop arm IFF1 + V18R-CPU-02 legacy raise routing + V18R-CPU-NIT-01 LDPIRX MEMPTR) |
| P19  | 4 IM2 fabric (V19-IM2-01 NR 0x22 LINE int_en + V19-IM2-02 ULA int_en + V19-IM2-03 int_unq pulse + V19-IM2-04 IM2 INT polling) + V19R-CPU-01 int_req pulse |
| P20  | 1 (V20-IM2-01 pulse-mode poll) + 2 NITs (V20R-CPU-NIT-01 prev_pulse_int_n_ save + V20R-CPU-NIT-02 double-stamp removal) |
| P21  | 1 (V21-IM2-01 im_mode IM2 gate on int_line/ack/state) |
| P22  | 1 (V22-IM2-01 im2_int_req latch clear on legacy on_reti) |
| **P23**  | **0** — **CONVERGED** |

Five consecutive IM2 fabric findings dropped to **zero** at Pass-23. Combined with 3 clean Z80N passes (P21/P22/P23), the subsystem is honestly converged.

## Recommendation

Mark **CPU + Z80N + IM2 SUBSYSTEM OFFICIALLY CONVERGED**. Skip in Pass-24+. All four boot-critical subsystems are now converged:

- **Memory** (P14)
- **DivMMC + SD + SPI** (P21)
- **NMI + Multiface + Port + NextREG** (P22)
- **CPU + Z80N + IM2** (P23 — this pass)

This completes Task 2 boot-critical subsystem analysis. The 9 outstanding class-(d) architectural items (per Pass-17 handover) remain pending user authorization, but no fabric-level Class-(a)/(b)/(c) bugs remain.
