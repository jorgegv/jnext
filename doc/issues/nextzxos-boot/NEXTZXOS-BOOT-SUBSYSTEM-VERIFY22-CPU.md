# Pass-22 audit report — CPU + Z80N + IM2 subsystem

Branch `task2/verify22-cpu-z80n-im2`. Audit conducted off integration HEAD `4cffca6`, blind to prior verify reports until completion.

## Summary

- **Findings**: 1 class-(b) — `V22-IM2-01` (legacy `on_reti()` entry point omitted im2_int_req latch clear).
- **Re-verification of Pass-17/18/19/20/21 fixes**: all clean — no residual bugs in V17/V18/V19/V19R/V20/V20R/V21 fixes touching CPU + Z80N + IM2 surfaces.
- **Z80N opcode re-audit**: all 31 opcodes verified VHDL-faithful against t80n.vhd + t80n_mcode.vhd. T-states, F-flag composition, MEMPTR/WZ strobes, IncDecZ shadow latching, port-mode vs IORQ-bypass distinction all match the VHDL oracle.
- **IM2 fabric**: V22-IM2-01 surfaced as the sole remaining asymmetry — `Im2Controller::on_reti()` (legacy entry point, called by the Emulator's `on_m1_cycle` lambda) was correctly transitioning state S_ISR→S_0 on a RETI, but did NOT clear the `im2_int_req` latch as VHDL `im2_isr_serviced` does (`im2_peripheral.vhd:148,:175`). Pre-fix the stale latch survived to the next `tick()` and re-fired S_0→S_REQ, spuriously re-triggering the same interrupt right after RETI returned from the ISR. The parallel `step_state_machine_with_iei` S_ISR branch already cleared both state AND latch inline (per V21-IM2-01 history) — only the legacy `on_reti()` was missing the latch clear.

### Test results (Release build)

| Suite | Pre-fix | Post-fix |
|---|---|---|
| ctest (38 suites) | 38/38 | 38/38 |
| FUSE Z80 (1356 opcodes) | 1356/1356 | 1356/1356 |
| cpu_z80n_im2_regressions | 46/46 | 47/47 (+V22-IM2-01) |
| cpu_int_pulse | 11/11 | 11/11 |
| ctc_interrupts | 30/30 | 30/30 |
| ctc_test | 132/132 | 132/132 |
| z80n_test | 85/85 | 85/85 |
| regression.sh | 33/0/0 | 33/0/0 |

## Enumeration table

Coverage scope: every Z80N opcode × {F-flag, MEMPTR/WZ strobes, R/T-states, side-effects} where applicable; every IM2 state-machine transition + Im2Level entry + IM2 source + register handler; every CPU↔IM2 wiring site; FUSE Z80 integration hooks; save_state schema fields. Total ~152 rows (~31 Z80N + ~22 CPU integration + ~50 IM2 fabric + ~30 emulator NR/port wiring + ~14 device-state-machine transitions + ~5 save/load).

| Surface (file:line) | C++ behaviour summary | VHDL oracle (file:line) | Match | Notes |
|---|---|---|---|---|
| z80n_ext.cpp:142 SWAPNIB (ED 23) | nibble swap of A; no F; T=8 | t80n.vhd:702-704 + t80n_mcode.vhd:1757 | ✓ | Q stays 0 |
| z80n_ext.cpp:151 MIRROR_A (ED 24) | bit-reverse A; no F; T=8 | t80n.vhd:706-708 | ✓ |  |
| z80n_ext.cpp:164 TEST_N (ED 27) | AND r,n flags; H=1, C=0, N=0; T=11 (M1+M1+operand) | t80n_mcode.vhd:1778-1788 ALU_Op=AND, MCycles=2 | ✓ | Q=f tracked |
| z80n_ext.cpp:190 BSLA_DE_B (ED 28) | uint32_t shift, mask 0xFFFF; T=8 | t80n.vhd:987-993 shift_left(unsigned 16-bit, B[4:0]) | ✓ | V17-Z80N-01a UB-free |
| z80n_ext.cpp:209 BSRA_DE_B (ED 29) | branched UB-free arith shift; T=8 | t80n.vhd:1006-1014 shift_right(signed 17-bit), bit 16=sign | ✓ | V17-CPU-NIT-04 |
| z80n_ext.cpp:244 BSRL_DE_B (ED 2A) | regs.DE >> shift (uint16→int promotion); T=8 | t80n.vhd:1006-1014, bit 16=IR(0)=0 | ✓ | non-negative int >> 0..31 well-defined |
| z80n_ext.cpp:252 BSRF_DE_B (ED 2B) | branched UB-free fill-1 shift; T=8 | t80n.vhd:1006-1014, bit 16=IR(0)=1 | ✓ | V17-Z80N-01b |
| z80n_ext.cpp:279 BRLC_DE_B (ED 2C) | rotate-left B[4:0] mask + mod 16; T=8 | t80n.vhd:1022-1028 rotate_left(unsigned 16-bit, B[4:0]) | ✓ | mod-width via &0x0F |
| z80n_ext.cpp:290 MUL_DE (ED 30) | uint16(D) * uint16(E); T=8 | t80n.vhd:729-735 | ✓ | no F write |
| z80n_ext.cpp:299 ADD_HL_A (ED 31) | HL += A; F.C cleared; T=8 | t80n.vhd:778-783 F.C <= reg_temp_t(16)=pre-zero | ✓ | Pass-10 fix |
| z80n_ext.cpp:324 ADD_DE_A (ED 32) | DE += A; F.C cleared; T=8 | same as ADD_HL_A | ✓ |  |
| z80n_ext.cpp:337 ADD_BC_A (ED 33) | BC += A; F.C cleared; T=8 | same | ✓ |  |
| z80n_ext.cpp:350 ADD_HL_NN (ED 34) | HL += nn via fuse_z80_readbyte; MEMPTR=nn; T=16 | t80n_mcode.vhd:1850-1882 LDZ+LDW at MCycles 2,3 | ✓ | Pass-4 MEMPTR; Pass-6 operand contention |
| z80n_ext.cpp:371 ADD_DE_NN (ED 35) | analogous | same | ✓ |  |
| z80n_ext.cpp:387 ADD_BC_NN (ED 36) | analogous | same | ✓ |  |
| z80n_ext.cpp:403 PUSH_NN (ED 8A) | push hh,ll big-endian; MEMPTR-lo only; T=23 | t80n_mcode.vhd:1921-1949 LDZ at M1+M3 (lo only), LDW never | ✓ | Pass-8 WZ-lo |
| z80n_ext.cpp:438 OUTINB (ED 90) | (HL)→port BC, HL++, no B--; extended-M1 via IR; T=16 | t80n_mcode.vhd:2519-2559 MCycles=3, TStates="101" extended-M1 | ✓ | V12-CPU-NIT-02 |
| z80n_ext.cpp:477 NEXTREG_NN (ED 91) | OUT 0x243B,reg; OUT 0x253B,val; T=20 | t80n_mcode.vhd:1668-1683 Z80N_data_o bypasses IORQ | ✓ |  |
| z80n_ext.cpp:499 NEXTREG_A (ED 92) | OUT 0x243B,reg; OUT 0x253B,A; T=17 | t80n_mcode.vhd:1690-1707 | ✓ |  |
| z80n_ext.cpp:515 PIXELDN (ED 93) | composite (b&R&C)+1 with H[7:5] preserved | t80n.vhd:900-921 | ✓ | V11-CPU-02 fix |
| z80n_ext.cpp:566 PIXELAD (ED 94) | "010" & D[7:6]&D[2:0] in H, D[5:3]&E[7:3] in L | t80n.vhd:939-947 | ✓ |  |
| z80n_ext.cpp:578 SETAE (ED 95) | A = 0x80 >> (E&7) | t80n.vhd:923-937 | ✓ |  |
| z80n_ext.cpp:588 JP_C (ED 98) | port-read BC → PC[13:6]; PC[5:0]=0; PC[15:14] preserved; T=12 | t80n.vhd:980-983 + t80n_mcode.vhd:1837-1848 MCycles=2 | ✓ | Pass-8 T=12 |
| z80n_ext.cpp:610 LDIX (ED A4) | (HL)→(DE) gated on A; HL++,DE++,BC--; I_BT flags; T=16 | t80n_mcode.vhd:2095-2138 MCycles=4, No_BTR holds at MC3 | ✓ | Pass-3/6/7/9 fixes |
| z80n_ext.cpp:662 LDWS (ED A5) | (HL)→(DE); L++; D++; I_BT flags on D-result | t80n_mcode.vhd:2141-2181 MCycles=3 | ✓ | Pass-8/9 IncDecZ shadow |
| z80n_ext.cpp:730 LDDX (ED AC) | HL--, DE++; I_BT flags | t80n_mcode.vhd:2230-2256 | ✓ |  |
| z80n_ext.cpp:764 LDIRX (ED B4) | repeat LDIX; PC rewind on BC≠0; G89 INT shape; T=21 cont / 16 term | t80n_mcode.vhd:2095-2138 | ✓ |  |
| z80n_ext.cpp:825 LDDRX (ED BC) | repeat LDDX | same | ✓ |  |
| z80n_ext.cpp:872 LDPIRX (ED B7) | pattern fill HL[15:3]\|DE[2:0]; HL fixed; I_BT flags with ALU_Q=B\|temp; MEMPTR-lo=0xB7 | t80n_mcode.vhd:1953-1991 | ✓ | Pass-10 flags; V18R-CPU-NIT-01 MEMPTR |
| z80n_ext.cpp:960 LDIRSCALE (ED B6) | repeat LDIX with I_BT flags | t80n_mcode.vhd:2188-2226 | ✓ |  |
| z80n_ext.cpp:1008 LOOP (ED FB) | NOP-equivalent (FPGA stub) | unimplemented in FPGA | ✓ |  |
| z80_cpu.cpp:407 execute() entry sync | sync_fuse_from_regs | n/a | ✓ |  |
| z80_cpu.cpp:416 NMI dispatch | fuse_z80_nmi + on_nmi_servicing(saved_pc) | Z80 std NMI + zxnext.vhd:2060-2068 | ✓ | G88 NR 0xC2/C3 latch unconditional |
| z80_cpu.cpp:451 INT pulse-expiry | unconditional drop after 32/36T window | zxnext.vhd:2017-2033 pulse_count_end | ✓ | V18R-CPU-01 |
| z80_cpu.cpp:471 INT iff1 gate | if iff1 → consider IntAck | std Z80 | ✓ |  |
| z80_cpu.cpp:494 EI-grace gate | skip on_int_ack on tstates == interrupts_enabled_at | FUSE convention | ✓ | Pass-8 |
| z80_cpu.cpp:510 on_int_ack callback | im2_.ack_vector() | im2_device.vhd:155 + zxnext.vhd:1999 | ✓ |  |
| z80_cpu.cpp:539 Z80N detection | peek ED+ext; dispatch execute_z80n | bypasses FUSE undefined-ED | ✓ |  |
| z80_cpu.cpp:559 ED prefix on_m1_cycle | fire for ED then ext byte | im2_control.vhd ifetch_fe_t3 per byte | ✓ | G87 |
| z80_cpu.cpp:595 Z80N M1 contention | contend_read(pc,4) + contend_read(pc+1,4) | per FUSE+VHDL | ✓ | Pass-5 |
| z80_cpu.cpp:601 R-reg update | +2 (ED prefix + ext byte) | t80n.vhd:493 per refresh | ✓ |  |
| z80_cpu.cpp:634 Q+iff2_read reset | pre-dispatch hygiene | FUSE convention | ✓ | Pass-4 |
| z80_cpu.cpp:680-681 non-Z80N ED M1 | fire on_m1_cycle for ED+ext BEFORE execute | im2_control.vhd ifetch_fe_t3 per byte | ✓ |  |
| z80_cpu.cpp:744-779 prefix walk | walk DD/FD/DD ED/CB chain; fire M1 per byte | im2_control.vhd FSM per-byte | ✓ | Pass-9 chained-prefix delivery |
| z80_cpu.cpp:834-857 inner-opcode walk | discover inner op through DD/FD chain | for IncDecZ classification | ✓ | V14-CPU-NIT-01 |
| z80_cpu.cpp:866-921 IncDecZ post-execute | DJNZ → F_out(Z); INC/DEC BC → ID16≠0; ED-block → BC≠0 | t80n.vhd:1358-1367 | ✓ | V13/V14/V14-NIT-01 |
| z80_cpu.cpp:925 request_interrupt | int_pending_=true; stamp tstates; count++ | / | ✓ | V20R-CPU-NIT-02 counter |
| z80_cpu.cpp:934 request_nmi | nmi_pending_=true | / | ✓ |  |
| z80_cpu.cpp:938+991 save/load | regs + MEMPTR + Q + interrupts_enabled_at + iff2_read + int state | / | ✓ | Pass-3/4 |
| im2.h:14-19 Im2Level enum | 14 legacy slots (FRAME/LINE/CTC0-3/UART/DMA/DIVMMC/ULA_EXTRA/MULTIFACE) | / | ✓ | vector=2*i preserved |
| im2.h:33-43 DevIdx enum | 14 fabric slots in VHDL priority order (LINE=0..UART1_TX=13) | zxnext.vhd:1941 priority order | ✓ |  |
| im2.h:46 DevState enum | S_0/S_REQ/S_ACK/S_ISR | im2_device.vhd:83 | ✓ |  |
| im2.cpp:20 reset() | clears all dev_, dec_state, pulse, NR 0xC0, DMA delay, ACK, legacy mask | im2_*.vhd reset signals | ✓ |  |
| im2.cpp:66 tick() | step_pulse → step_devices → step_dma_delay → clear int_unq → clear int_req | per VHDL synchronous-update | ✓ | V19/V19R fixes |
| im2.cpp:163 to_devidx() | Im2Level→DevIdx bridge; DMA/DIVMMC/MULTIFACE → ULA (placeholder) | scaffold | ✓ | observed only via legacy path (no caller) |
| im2.cpp:191 raise() legacy | gates DMA/DIVMMC/MULTIFACE → no-op; others route via to_devidx | V18R-CPU-02 | ✓ |  |
| im2.cpp:224 clear() legacy | same | V18R-CPU-02 | ✓ |  |
| im2.cpp:241 has_pending() legacy | iterate dev_ int_req && mask | / | ✓ | unused in production |
| im2.cpp:248 get_vector() legacy | first dev_ int_req → vec=2*i | / | ✓ | unused |
| im2.cpp:257 set_mask() legacy | store legacy_mask_ | / | ✓ |  |
| **im2.cpp:261 on_reti() legacy** | **walk dev_, S_ISR→S_0 gated on iei+im_mode_==2; CLEARS state AND im2_int_req latch (V22-IM2-01 fix)** | im2_peripheral.vhd:148,:175 (im2_isr_serviced clears latch on edge) + im2_device.vhd:124 | ✓ | **V22-IM2-01 — Pass-22 class-(b) finding** |
| im2.cpp:348 on_retn() legacy | no-op (retn doesn't reach im2_device) | im2_control.vhd:236 + divmmc.vhd | ✓ |  |
| im2.cpp:366 raise_req() | set dev_[d].int_req=true | im2_peripheral.vhd:90-101 edge detector input | ✓ | level-as-pulse via V19R clear-end-of-tick |
| im2.cpp:371 clear_req() | dev_[d].int_req=false | / | ✓ | rarely used; for completeness |
| im2.cpp:384 raise_unq() | int_unq=true + int_status=true + im2_int_req=true | im2_peripheral.vhd:160,:172 UNQ-04/05 | ✓ | V19-IM2-03 one-shot |
| im2.cpp:396 clear_status() | clear int_status only (NOT im2_int_req) | im2_peripheral.vhd:160 status_clear gates int_status only | ✓ |  |
| im2.cpp:405 int_status() | int_status OR im2_int_req | im2_peripheral.vhd:180 o_int_status | ✓ |  |
| im2.cpp:417 int_status_mask_c8 | bit 1=LINE, bit 0=ULA | zxnext.vhd:6247-6248 | ✓ |  |
| im2.cpp:431 int_status_mask_c9 | bits 0..7=CTC0..CTC7 | zxnext.vhd:6250-6251 | ✓ |  |
| im2.cpp:454 int_status_mask_ca | bit 6=U1TX, bits 5,4=U1RX dup, bit 2=U0TX, bits 1,0=U0RX dup | zxnext.vhd:6253-6254 | ✓ | dup per VHDL concat |
| im2.cpp:482 set_int_en() | per-DevIdx setter | / | ✓ |  |
| im2.cpp:491 set_int_en_c4() | LINE = bit 1 (ULA handled by emulator port_ff fan-out) | zxnext.vhd:5607-5610 | ✓ |  |
| im2.cpp:501 set_int_en_c5() | CTC7..CTC0 = bits 7..0 | zxnext.vhd:1949 ctc_int_en | ✓ |  |
| im2.cpp:522 set_int_en_c6() | UART1TX=b6, UART1RX=b5|b4, UART0TX=b2, UART0RX=b1|b0 | zxnext.vhd:1950 nr_c6 fields | ✓ |  |
| im2.cpp:536 set_vector_base() | store msb3 | zxnext.vhd:5597 nr_c0_im2_vector(2..0) | ✓ |  |
| im2.cpp:541 set_mode() | store im2_mode_ | zxnext.vhd:5599 nr_c0_int_mode_pulse_0_im2_1 | ✓ |  |
| im2.cpp:544 set_stackless_nmi() | store stackless_nmi_ | zxnext.vhd:5598 nr_c0_stackless_nmi | ✓ | observed via NR 0xC0 read |
| im2.cpp:559 set_dma_int_en_mask() | fan out to dev_[i].dma_int_en | per VHDL OR-reduction on state≠S_0 | ✓ |  |
| im2.cpp:569 dma_int_pending() | OR (state≠S_0 AND dma_int_en) | im2_device.vhd:151 + peripherals OR | ✓ |  |
| im2.cpp:587 dma_delay() | return im2_dma_delay_latched_ | zxnext.vhd:2001-2010 | ✓ |  |
| im2.cpp:598 set_nmi_activated() | push from NmiSource::is_activated() per tick | zxnext.vhd:2007 nmi_activated input | ✓ |  |
| im2.cpp:599 set_nr_cc_dma_int_en_0_7() | push from NR 0xCC bit 7 write | zxnext.vhd:2007 nr_cc_dma_int_en_0_7 input | ✓ |  |
| im2.cpp:606 int_line_asserted() | gates on im2_mode_ AND im_mode_==2; walks dev_ S_REQ with IEI | im2_device.vhd:150 o_int_n AND-reduction across im2_int_n | ✓ | V21-IM2-01 |
| im2.cpp:643 ack_vector() | gates on im2_mode_ AND im_mode_==2; first S_REQ with IEI → S_ACK | im2_device.vhd:112 + 155 + zxnext.vhd:1999 | ✓ | V21-IM2-01 |
| im2.cpp:702 on_m1_cycle() | clears reti/retn pulses; advance_decoder | im2_control.vhd ifetch_fe_t3 | ✓ |  |
| im2.cpp:734 pulse_int_n() | latch read | zxnext.vhd:2017-2031 | ✓ |  |
| im2.cpp:736 set_machine_timing_48_or_p3 | store bool | zxnext.vhd:2033 machine_timing_48 / _p3 | ✓ | dynamic fan-out on NR 0x03 write |
| im2.cpp:741 state() | debug read | / | ✓ |  |
| im2.cpp:745 ieo() | debug daisy walk | im2_device.vhd:136-146 | ✓ |  |
| im2.cpp:771 advance_decoder() S_0 | ED/CB/DDFD branch | im2_control.vhd:161-170 | ✓ |  |
| im2.cpp:784 advance_decoder() S_ED_T4 | 4D→S_ED4D_T4+reti_pulse; 45→S_ED45_T4+retn_pulse; IM 0/1/2 latch; else S_0 | im2_control.vhd:171-180,:218-227 | ✓ | IM-bit decode per :224 |
| im2.cpp:815 advance_decoder() S_ED4D_T4 | → S_SRL_T1 | im2_control.vhd:181-183 | ✓ |  |
| im2.cpp:820 advance_decoder() S_ED45_T4 | → S_SRL_T1 | im2_control.vhd:190-192 | ✓ |  |
| im2.cpp:826 advance_decoder() S_SRL_T1/T2 | T1→T2→S_0 | im2_control.vhd:186-189 | ✓ |  |
| im2.cpp:836 advance_decoder() S_CB_T4 | → S_0 on any byte | im2_control.vhd:193-198 | ✓ |  |
| im2.cpp:858 advance_decoder() S_DDFD_T4 | DD/FD stay; else → S_0 | im2_control.vhd:199-206 | ✓ | V11-CPU-01 |
| im2.cpp:887 step_devices() Phase 1 | edge detect + int_status set + im2_int_req latch gated on im2_reset_n | im2_peripheral.vhd:90-101,:154-178 | ✓ | V17-CPU-01 pulse-mode reset hold |
| im2.cpp:943 step_devices() Phase 2 | snapshot IEI; per-device state machine | im2_device.vhd:102-132 | ✓ | Pass-10 reti_decode simultaneity |
| im2.cpp:982 step_state_machine_with_iei S_0 | im2_int_req → S_REQ | im2_device.vhd:106 | ✓ |  |
| im2.cpp:1012 step_state_machine_with_iei S_REQ | passive (ack_vector advances) | im2_device.vhd:111 | ✓ |  |
| im2.cpp:1032 step_state_machine_with_iei S_ACK | → S_ISR unconditionally next tick | im2_device.vhd:117 | ✓ |  |
| im2.cpp:1039 step_state_machine_with_iei S_ISR | reti_pulse && iei && im_mode_==2 → S_0 + clear im2_int_req | im2_device.vhd:123-128 + :175 | ✓ | V21-IM2-01 / V22-IM2-01 parallel |
| im2.cpp:1100 step_pulse() | pulse fabric: edge OR-reduction; bit-5+bit-2 termination gate | zxnext.vhd:2012-2044 | ✓ |  |
| im2.cpp:1186 step_dma_delay() | dma_int OR (nmi AND nr_cc_b7) OR (latch AND dma_delay_ctrl) | zxnext.vhd:2001-2010 | ✓ | Wave E |
| im2.cpp:1193 compute_vector() | (msb3<<5) \| (idx<<1) | zxnext.vhd:1999 | ✓ |  |
| im2.cpp:1215 device_ieo() | walk priority chain | im2_device.vhd:136-146 | ✓ |  |
| im2.cpp:1250 propagate_isr_serviced() | documented no-op (handled inline in S_ISR branch) | im2_peripheral.vhd:137-148 | ✓ |  |
| im2.cpp:1263 save_state() | device fields + decoder + pulse + NR 0xC0 + DMA + ACK + legacy mask | / | ✓ | reti/retn counts intentionally not persisted |
| im2.cpp:1303 load_state() | symmetric | / | ✓ |  |
| emulator.cpp:243 reset(): im2_c4_expbus_=true | NR 0xC4 bit 7 reset default '1' | zxnext.vhd:5096 | ✓ |  |
| emulator.cpp:361-362 init(): set_machine_timing_48_or_p3 | both Im2 and Z80Cpu | zxnext.vhd:2033 | ✓ | dynamic fan-out via NR 0x03 write |
| emulator.cpp:661-668 cpu_.on_m1_cycle lambda | im2_.on_m1_cycle + on_reti + on_retn forwarding | per VHDL combined wiring | ✓ |  |
| emulator.cpp:708 cpu_.on_nmi_servicing | nextreg_.set_nmi_return_address(saved_pc) | zxnext.vhd:2060-2068 NMIACK_LSB/MSB latch | ✓ | G88 |
| emulator.cpp:718 cpu_.on_int_ack | im2_.ack_vector() | im2_device.vhd:155 + zxnext.vhd:1999 | ✓ |  |
| emulator.cpp:1870-1922 NR 0x22 write | port_ff_reg(6); video_timing; im2_.set_int_en(LINE)+(ULA); reschedule | zxnext.vhd:5297,:5607-5610,:6711 | ✓ | V19-IM2-01/02 |
| emulator.cpp:1935 NR 0x22 read | !pulse_int_n & 0000 & port_ff_reg(6) & line_en & target_msb | zxnext.vhd:5992 | ✓ |  |
| emulator.cpp:2270-2313 NR 0x03 write | machine_timing update + Im2 + Z80Cpu pulse-window fan-out | zxnext.vhd:5126-5145 + 2033 | ✓ | G121 |
| emulator.cpp:2821 NR 0xC0 write | vector_base + stackless_nmi + mode (im2/pulse) | zxnext.vhd:5596-5599 | ✓ | im_mode bits 2:1 read-only |
| emulator.cpp:2828 NR 0xC0 read | vector_base<<5 \| stackless<<3 \| im_mode<<1 \| mode | zxnext.vhd:6230 | ✓ |  |
| emulator.cpp:2850 NR 0xC4 write | im2_.set_int_en_c4 + nr_22_line_int_en + port_ff_reg(6) (NOT bit0) + im2_.set_int_en(ULA) + im2_c4_expbus_ | zxnext.vhd:5607-5610,:3621-3622 | ✓ | V12-NMP-01 |
| emulator.cpp:2904 NR 0xC4 read | expbus_b7 + 00000 + line_en + (!ula_disabled) | zxnext.vhd:6239 | ✓ |  |
| emulator.cpp:2919 NR 0xC5 write | ctc_.set_int_enable + im2_.set_int_en_c5 | zxnext.vhd:1949,4078 | ✓ |  |
| emulator.cpp:2924 NR 0xC5 read | ctc_.get_int_enable | zxnext.vhd:6242 | ✓ |  |
| emulator.cpp:2933 NR 0xC6 write | im2_.set_int_en_c6 + nr_c6_uart_int_en_ | zxnext.vhd:5615-5617 | ✓ |  |
| emulator.cpp:2938 NR 0xC6 read | nr_c6_uart_int_en_ | zxnext.vhd:6245 | ✓ |  |
| emulator.cpp:2947 NR 0xC8 write | clear_status LINE/ULA | zxnext.vhd:1952-1955 bit 11/0 | ✓ |  |
| emulator.cpp:2952 NR 0xC8 read | int_status_mask_c8 | zxnext.vhd:6248 | ✓ |  |
| emulator.cpp:2961 NR 0xC9 write | clear_status CTC0..CTC7 | zxnext.vhd:1953 bits 10..3 | ✓ |  |
| emulator.cpp:2972 NR 0xC9 read | int_status_mask_c9 | zxnext.vhd:6251 | ✓ |  |
| emulator.cpp:2981 NR 0xCA write | UART clear per VHDL OR pattern | zxnext.vhd:1952-1954 | ✓ |  |
| emulator.cpp:2988 NR 0xCA read | int_status_mask_ca | zxnext.vhd:6254 | ✓ |  |
| emulator.cpp:2996 NR 0xCC write | dma_delay_on_nmi (b7) + dma_int_en mask + nr_cc_b7 push | zxnext.vhd:5628-5630 | ✓ | Wave E live |
| emulator.cpp:3010 NR 0xCD write | nr_cd_dma_int_en_1 + dma_int_en mask | zxnext.vhd:5632-5633 | ✓ |  |
| emulator.cpp:3018 NR 0xCE write | nr_ce_dma_int_en fields + dma_int_en mask | zxnext.vhd:5635-5637 | ✓ |  |
| emulator.cpp:3037 NR 0x20 read | im2_status: bit 7=LINE, bit 6=ULA, bits 3:0=CTC3..0 | zxnext.vhd:5989 | ✓ |  |
| emulator.cpp:3047 NR 0x20 write | raise_unq per VHDL bit map | zxnext.vhd:1946-1947 | ✓ |  |
| emulator.cpp:5547 run_frame top | dma_.set_dma_delay(im2_.dma_delay()) | per-frame sample | ✓ |  |
| emulator.cpp:5821 cpu_.execute() | one instruction | / | ✓ |  |
| emulator.cpp:5861 im2_.set_nmi_activated | push from NmiSource::is_activated() | zxnext.vhd:2007 | ✓ |  |
| emulator.cpp:5862 im2_.tick(master_cycles) | advance fabric | / | ✓ |  |
| emulator.cpp:5897 IM2-mode INT poll | request_interrupt(0xFE) when is_im2_mode() && int_line_asserted | zxnext.vhd:1840 | ✓ | V19-IM2-04 |
| emulator.cpp:5965 pulse-mode INT poll | request_interrupt(0xFF) on falling-edge of pulse_int_n in pulse mode | zxnext.vhd:1840 | ✓ | V20-IM2-01 + V20R-CPU-NIT-01/02 |
| emulator.cpp:7658 compose_im2_dma_int_en | 14-bit mask per VHDL :1957-1958 priority order | zxnext.vhd:1957-1958 | ✓ |  |

(The above table lists the principal surface rows. The full audit also covered all reset-default propagation, NR 0xC2/C3 read paths, save/load schema fields for both Im2Controller and Z80Cpu, the FUSE Z80 interrupts_enabled_at + iff2_read persistence, M1 callback delivery for chained DD/FD/DD ED/CB prefixes, IncDecZ shadow update for all relevant inner-opcode classifications, and the bidirectional NR 0x22 ↔ NR 0xC4 ↔ port-FF mirror chain.)

## Findings detail

### V22-IM2-01 — class-(b) — `on_reti()` must clear im2_int_req on S_ISR→S_0

**Source**: `src/cpu/im2.cpp` `Im2Controller::on_reti()` at line 261.

**VHDL oracle**:

- `im2_peripheral.vhd:148` — `im2_isr_serviced <= isr_serviced AND NOT isr_serviced_d` — one-cycle pulse on the rising edge of `isr_serviced`, which is `'1' when state = S_ISR and state_next = S_0`.
- `im2_peripheral.vhd:175` — `im2_int_req <= im2_int_req AND NOT im2_isr_serviced` — the latch IS CLEARED in lock-step with the S_ISR→S_0 transition.

**Pre-fix bug**: `on_reti()` walks dev_[] in priority order, finds devices in S_ISR with IEI=1 (qualifying for the standard RETI clear), and sets `dev_[i].state = DevState::S_0`. But it does NOT clear `dev_[i].im2_int_req`. The latch stays at the value set when the original raise_req() edge fired (in the wrapper's edge detect).

**Why it matters in production**: The Emulator's `on_m1_cycle` lambda (emulator.cpp:663-665) calls `im2_.on_reti()` directly after every RETI M1 byte. After this returns the device is in S_0 with `im2_int_req=true` still latched. Next cycle the Emulator calls `im2_.tick()` → `step_devices()`:

- Phase 1 (wrapper edge detect): no new edge (peripheral's int_req has already been cleared via V19R-CPU-01 auto-clear-at-end-of-tick). The latch is NOT force-cleared in IM2 mode because `im2_reset_n=true` (only pulse-mode reset clears it).
- Phase 2 (state machine): for the device just cleared from S_ISR to S_0, `case DevState::S_0:` checks `if (d.im2_int_req)`. Latch is stale-true → state transitions S_0→S_REQ.

The emulator's IM2-mode poll then sees `int_line_asserted()=true` and calls `request_interrupt(0xFE)` — the CPU accepts the SAME interrupt right after RETI returned from the ISR. The ISR re-enters with the peripheral in the same stale state as before. Real hardware does not re-enter because `im2_isr_serviced` clears the latch on the very edge that the state transitioned to S_0.

This was the only remaining asymmetry: the parallel path inside `step_state_machine_with_iei()` (im2.cpp:1058-1063) already cleared both state AND latch in lock-step on its own S_ISR→S_0 transition (per V21-IM2-01 history). The legacy `on_reti()` had been omitting the latch-clear since the Phase 1 scaffold; no prior pass caught it because the existing direct-Im2Controller tests do not call `on_reti()` (they rely on tick to clear S_ISR), and the production-emulator tests that DO go through the lambda did not check for a spurious re-trigger post-RETI.

**Fix** (im2.cpp:328-352): in the same loop that sets `state = S_0`, also set `im2_int_req = false`. Matches VHDL :175 lock-step semantic and the parallel `step_state_machine_with_iei` clear at im2.cpp:1062.

**Discriminative test**: `test_v22_im2_01_on_reti_clears_im2_int_req_latch` (test/cpu/cpu_z80n_im2_regressions_test.cpp:2970-3066). Drives LINE through `raise_req → tick → S_REQ → ack_vector → S_ACK → tick → S_ISR`, then issues `on_m1_cycle(ED+4D) + on_reti()` (mimicking the Emulator's lambda invocation), then `clear_status(LINE)` to strip the int_status side of the o_int_status composite, then `tick(1)` to simulate the next emulator advance. Pre-fix observes post-tick state=S_REQ with `int_status()=true` (latch stale-readable through the composite); post-fix observes state=S_0 and `int_status()=false`.

Verified failure WITHOUT the fix and pass WITH it.

## Re-verification details

- **Z80N opcodes** — full re-walk of all 31 implementations vs `t80n.vhd` + `t80n_mcode.vhd`. T-states, MEMPTR/WZ strobes, F-flag composition, IncDecZ shadow latching, port-mode vs IORQ-bypass distinction, M1-extended TStates="101" (OUTINB), prefix-chain delivery to im2_control all match.
- **IM2 fabric** — every method examined; gating on `im_mode_ == 2` correct for the 4 known sites (V21-IM2-01) and no additional sites discovered. No additional level-vs-pulse antipatterns beyond V19-IM2-03 (int_unq) and V19R-CPU-01 (int_req).
- **NR 0xC0..0xC9, 0xCA, 0xCC..0xCE** — all read/write semantics match VHDL.
- **Reset defaults** — NR 0xC4 bit 7 default '1' propagated via im2_c4_expbus_=true at reset (verified in emulator init :243).
- **NMI handling** — NR 0xC2/C3 unconditional capture on NMI servicing matches VHDL :2060-2068 (bit-7 stackless_nmi only gates the actual stackless behavior, not the latch).
- **HALT + INT resume** — handled by FUSE Z80 core; jnext's z80_cpu.cpp:425 correctly captures `(z80.pc.w + 1)` if halted, matching VHDL Halt_FF+1.
- **Block-instr INT resume (G89)** — verified for LDIRX/LDDRX/LDPIRX/LDIRSCALE via PC rewind on BC≠0. INT acceptance at next instruction boundary correctly re-fetches the same opcode.
- **R-register on prefix bytes** — VHDL t80n.vhd:493 increments R every M1 cycle; FUSE Z80 handles this internally for non-Z80N. Z80N path explicitly adds +2 to R for the ED+ext pair (z80_cpu.cpp:601). Multi-prefix chains handled by FUSE via repeated M1 cycles.

## Trend

Pass-17: 8 effective findings. Pass-18: ?. Pass-19: 4. Pass-20: 1. Pass-21: 0 (CPU). Pass-22: 1 (V22-IM2-01).

CPU/Z80N/IM2 audit appears to be approaching convergence — single class-(b) finding this pass, with full re-verification of prior 21 passes' fixes returning clean. Recommendation: one more pass to confirm convergence.

## Test invariants — final

- ctest: **38/38** (no regressions)
- FUSE Z80: **1356/1356** (NON-NEGOTIABLE — preserved)
- cpu_z80n_im2_regressions: **47/47** (was 46/46; +V22-IM2-01)
- cpu_int_pulse: **11/11**
- ctc_interrupts: **30/30**
- ctc_test: **132/132**
- z80n_test: **85/85**
- regression.sh: **33/0/0**

Final HEAD on `task2/verify22-cpu-z80n-im2`: commit `84fcc1c`.
