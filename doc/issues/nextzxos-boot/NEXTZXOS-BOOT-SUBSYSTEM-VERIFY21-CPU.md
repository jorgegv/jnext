# Pass-21 audit report — CPU + Z80N + IM2 subsystem

Branch `task2/verify21-cpu-z80n-im2`. Audit conducted off integration HEAD `94d41ed`, blind to prior verify reports until completion.

## Summary

- **Findings**: 1 class-(c) — `V21-IM2-01` (IM2 fabric `int_line_asserted` / `ack_vector` / `on_reti` / `S_ISR→S_0` transition do not gate on `i_im2_mode = z80_im_mode(1)`, i.e. `Im2Controller::im_mode_ == 2`). Boot-realistic but narrow window — supervisor normally executes `ED 5E` (IM 2) before any IM2 fabric activity, so the bug is dormant in production NextZXOS boot. Documented for VHDL fidelity + future ZX-Next software that flips IM modes mid-handler.
- **Re-verification of Pass-19 / Pass-20 fixes**: all clean — no residual bugs in V19-IM2-01/02/03/04, V19R-CPU-01, V20-IM2-01, V20R-CPU-NIT-01 or V20R-CPU-NIT-02.
- **Z80N opcode re-audit**: all opcodes VHDL-faithful per careful re-reading. BSLA/BSRA/BSRL/BSRF/BRLC strict-UB-free verified; MEMPTR strobes correct (ADD_*_NN sets both lo+hi, PUSH_NN lo-only, LDPIRX lo=0xB7); IncDecZ shadow propagation correct across DD/FD/ED prefix chains; F-flag composition (S/Z/X/Y/H/N/P/C + Q) verified per opcode against VHDL ALU + I_BT override blocks.
- **IM2 sequencing**: `step_pulse → step_devices → step_dma_delay → clear int_unq → clear int_req` ordering verified; auto-clear of int_req correctly synthesises 1-cycle pulse semantic (V19R-CPU-01 holds).
- **CPU /INT polling architecture (V19+V20 cleanup)**: stable. IM2-mode poll at `:5789` gates on `is_im2_mode() && int_line_asserted()` (now `int_line_asserted()` itself gates on `im_mode_==2`). Pulse-mode poll at `:5857-5862` uses falling-edge detection (`!cur && prev`) with `prev_pulse_int_n_` persisted in save/load (V20R-CPU-NIT-01). Legacy `cpu_.request_interrupt(0xFF)` calls in ULA/LINE scheduler callbacks dropped (V20R-CPU-NIT-02).
- **HALT + INT resume**: verified (FUSE `PC++` + `R++` on HALT-exit; on_m1_cycle fires per re-fetched HALT).
- **R-register update on prefix bytes**: verified (FUSE bumps R for ED inner byte at opcodes_base.c:1075; jnext Z80N path does explicit `z80.r += 2`).
- **Block-instruction INT resume (G89)**: verified (LDIRX/LDDRX/LDPIRX/LDIRSCALE rewind PC by 2 on BC≠0).
- **Contention model**: verified (G53/G141 routes all FUSE in-opcode contention sites through `ContentionModel::contention_tick()`).

### Test results

| Suite | Pre-fix | Post-fix |
|---|---|---|
| ctest (38 suites) | 38/38 | 38/38 |
| FUSE Z80 (1356 opcodes) | 1356/1356 | 1356/1356 |
| cpu_int_pulse_test | 11/11 | 11/11 |
| cpu_z80n_im2_regressions_test | 45/45 | 46/46 (new V21-IM2-01 row) |
| ctc_test | 132/132 | 132/132 |
| ctc_interrupts_test | 30/30 | 30/30 |
| regression.sh | 33/0/0 | 33/0/0 |

## Enumeration table

Coverage scope per Pass-19/Pass-20 plus full re-verification of all prior fixes. Granularity:
- one row per Z80N opcode × {F-flag, MEMPTR/WZ strobes, R/T-states, side-effects} where applicable;
- one row per IM2 state-machine transition + Im2Level entry + IM2 source + register handler;
- one row per FUSE Z80 integration hook;
- one row per save_state schema field touching CPU/IM2 state.

| Surface (file:line) | C++ behaviour summary | VHDL oracle (file:line) | Match | Notes |
|---|---|---|---|---|
| z80n_ext.cpp:142 SWAPNIB | nibble swap of A; no F; T=8 | t80n.vhd:702-704 + t80n_mcode.vhd:1761 | ✓ | Q stays 0 (pre-dispatch) |
| z80n_ext.cpp:151 MIRROR_A | bit-reverse A; no F; T=8 | t80n.vhd:706-708 | ✓ |  |
| z80n_ext.cpp:164 TEST_N | AND r,n flags; H=1, C=0, N=0; T=11 | t80n_mcode.vhd:1779-1788 default ALU_Op=AND | ✓ | Q=f (F-write op); regs.Q tracked |
| z80n_ext.cpp:190 BSLA_DE_B | uint32_t shift, mask 0xFFFF; T=8 | t80n.vhd:987-993 shift_left(unsigned 16-bit, B[4:0]) | ✓ | V17-Z80N-01a; UB-free for shift≥16 |
| z80n_ext.cpp:209 BSRA_DE_B | branched UB-free arith shift; T=8 | t80n.vhd:1006-1014 shift_right(signed 17-bit), bit 16=sign | ✓ | V17-CPU-NIT-04 |
| z80n_ext.cpp:244 BSRL_DE_B | regs.DE >> shift (uint16); T=8 | t80n.vhd:1006-1014, bit 16=IR(0)=0 | ✓ | int-promotion well-defined |
| z80n_ext.cpp:252 BSRF_DE_B | branched UB-free fill-1 shift; T=8 | t80n.vhd:1006-1014, bit 16=IR(0)=1 | ✓ | V17-Z80N-01b |
| z80n_ext.cpp:279 BRLC_DE_B | rotate-left B[4:0] mask + mod 16; T=8 | t80n.vhd:1022-1028 rotate_left(unsigned 16-bit, B[4:0]) | ✓ | rot=16 case `DE>>16` valid via int-promotion |
| z80n_ext.cpp:290 MUL_DE | uint16(D) * uint16(E); T=8 | t80n.vhd:729-735 | ✓ | no F write |
| z80n_ext.cpp:299 ADD_HL_A | HL += A; F.C cleared; T=8 | t80n.vhd:778-783 F.C <= reg_temp_t(16)=pre-zero | ✓ | Pass-10 fix |
| z80n_ext.cpp:324 ADD_DE_A | DE += A; F.C cleared; T=8 | same as ADD_HL_A | ✓ |  |
| z80n_ext.cpp:337 ADD_BC_A | BC += A; F.C cleared; T=8 | same | ✓ |  |
| z80n_ext.cpp:350 ADD_HL_NN | HL += nn (operand via fuse_z80_readbyte); MEMPTR=nn; T=16 | t80n_mcode.vhd:1872-1878 LDZ+LDW | ✓ | Pass-4 MEMPTR; Pass-6 operand contention |
| z80n_ext.cpp:371 ADD_DE_NN | analogous | same | ✓ |  |
| z80n_ext.cpp:387 ADD_BC_NN | analogous | same | ✓ |  |
| z80n_ext.cpp:403 PUSH_NN | push hh,ll big-endian; MEMPTR-lo only; T=23 | t80n_mcode.vhd:1928,1938 LDZ at M1+M3, LDW never | ✓ | Pass-8 WZ-lo |
| z80n_ext.cpp:438 OUTINB | (HL)→port BC, HL++, no B--; extended-M1 via IR; T=16 | t80n_mcode.vhd:2521-2531 TStates="101" extended | ✓ | V12-CPU-NIT-02 |
| z80n_ext.cpp:477 NEXTREG_NN | OUT 0x243B,reg; OUT 0x253B,val; T=20 | t80n_mcode.vhd:1668-1683 Z80N_data_o bypasses IORQ | ✓ |  |
| z80n_ext.cpp:499 NEXTREG_A | OUT 0x243B,reg; OUT 0x253B,A; T=17 | t80n_mcode.vhd:1690-1707 | ✓ |  |
| z80n_ext.cpp:515 PIXELDN | composite (b&R&C)+1 with H[7:5] preserved | t80n.vhd:900-921 | ✓ | V11-CPU-02 fix |
| z80n_ext.cpp:566 PIXELAD | "010" & D[7:6]&D[2:0] in H, D[5:3]&E[7:3] in L | t80n.vhd:939-947 | ✓ |  |
| z80n_ext.cpp:578 SETAE | A = 0x80 >> (E&7) | t80n.vhd:923-937 | ✓ |  |
| z80n_ext.cpp:588 JP_C | port-read BC → PC[13:6]; PC[5:0]=0; PC[15:14] preserved; T=12 | t80n.vhd:980-983 + t80n_mcode.vhd:1837-1848 | ✓ | Pass-8 T=12 (no spec-wiki +1 idle) |
| z80n_ext.cpp:610 LDIX | (HL)→(DE) gated on A; HL++,DE++,BC--; I_BT flags; T=16 | t80n_mcode.vhd:2098-2138 + t80n.vhd:1277-1289 | ✓ | Pass-3/6/7/9 fixes |
| z80n_ext.cpp:662 LDWS | (HL)→(DE); L++; D++; I_BT flags on D-result | t80n_mcode.vhd:2141-2181 | ✓ | Pass-8/9 IncDecZ |
| z80n_ext.cpp:730 LDDX | HL--, DE++; I_BT flags | t80n_mcode.vhd:2230-2256 | ✓ |  |
| z80n_ext.cpp:764 LDIRX | repeat LDIX; PC rewind on BC≠0; G89 INT shape | t80n_mcode.vhd:2095-2138 MCycles="100" | ✓ |  |
| z80n_ext.cpp:825 LDDRX | repeat LDDX | same | ✓ |  |
| z80n_ext.cpp:872 LDPIRX | pattern fill HL[15:3]\|DE[2:0]; HL fixed; I_BT flags with ALU_Q=B\|temp; MEMPTR-lo=0xB7 | t80n_mcode.vhd:1953-1991 + LDZ at M1 | ✓ | Pass-10 flags; V18R-CPU-NIT-01 MEMPTR |
| z80n_ext.cpp:960 LDIRSCALE | repeat LDIX with I_BT flags (scale commented in VHDL) | t80n_mcode.vhd:2188-2226 | ✓ |  |
| z80n_ext.cpp:1008 LOOP | NOP-equivalent (FPGA stub) | unimplemented in FPGA | ✓ |  |
| z80_cpu.cpp:407 execute() entry sync | sync_fuse_from_regs | n/a | ✓ |  |
| z80_cpu.cpp:416 NMI dispatch | fuse_z80_nmi + on_nmi_servicing | Z80 std NMI | ✓ | G88 NR 0xC2/C3 latch |
| z80_cpu.cpp:425 NMI saved_pc capture | `(PC+1) & 0xFFFF` when halted | VHDL captures stacked addr | ✓ | matches FUSE pre-PC++ |
| z80_cpu.cpp:451 INT pulse-expiry | unconditional drop after 32/36T | zxnext.vhd:2017-2033 pulse_count_end | ✓ | V18R-CPU-01 |
| z80_cpu.cpp:471 INT iff1 gate | if iff1 → consider IntAck | std Z80 | ✓ |  |
| z80_cpu.cpp:494 EI-grace gate | skip on_int_ack on tstates == interrupts_enabled_at | FUSE convention | ✓ | Pass-8 |
| z80_cpu.cpp:510 on_int_ack callback | im2_.ack_vector() | im2_device.vhd:155 + zxnext.vhd:1999 | ✓ |  |
| z80_cpu.cpp:539 Z80N detection | peek ED+ext; dispatch execute_z80n | bypasses FUSE undefined-ED | ✓ |  |
| z80_cpu.cpp:543 ED FF magic breakpoint | early-return 8T, no on_m1_cycle, no R bump | jnext-only debug feature | ⚠ | R-bump skipped; debug-only side effect, not observable to running code |
| z80_cpu.cpp:559 ED prefix on_m1_cycle | fire for ED then ext byte | im2_control.vhd ifetch_fe_t3 per byte | ✓ | G87 |
| z80_cpu.cpp:595 Z80N M1 contention | contend_read(pc,4) + contend_read(pc+1,4) | per FUSE+VHDL | ✓ | Pass-5 |
| z80_cpu.cpp:601 R-reg update | +2 (ED prefix + ext byte) | per VHDL refresh | ✓ |  |
| z80_cpu.cpp:634 Q+iff2_read reset | pre-dispatch hygiene | FUSE convention | ✓ | Pass-4 |
| z80_cpu.cpp:680 non-Z80N ED M1 callback | fire ED + ext byte | im2_control.vhd FSM | ✓ |  |
| z80_cpu.cpp:690 ED block-xfer IncDecZ | BC!=0 polarity | t80n.vhd:1361-1366 | ✓ | Pass-9 |
| z80_cpu.cpp:700 DD 01 magic breakpoint | early-return 8T | jnext-only debug feature | ⚠ | same as ED FF |
| z80_cpu.cpp:744 DD/FD/CB on_m1_cycle walk | walk prefix chain | im2_control.vhd per-fetch | ✓ | Pass-9 |
| z80_cpu.cpp:836 inner-opcode lookup for IncDecZ | walk DD/FD chain to inner | t80n.vhd ISet stays "00" | ✓ | V14-CPU-NIT-01 |
| z80_cpu.cpp:866 IncDecZ update DJNZ | F.Z polarity (B==0 post-dec → 1) | t80n.vhd:1359 | ✓ | V13-CPU-01 |
| z80_cpu.cpp:899 IncDecZ update ED block xfer | BC!=0 polarity | t80n.vhd:1361-1366 | ✓ | Pass-9 |
| z80_cpu.cpp:906 IncDecZ update INC/DEC BC | BC!=0 polarity | t80n.vhd:1361-1366 | ✓ | V14-CPU-01 |
| z80_cpu.cpp:925 request_interrupt | int_pending+vector+tstates stamp + counter | legacy API + V20R-CPU-NIT-02 obs counter | ✓ |  |
| z80_cpu.cpp:931 request_nmi | nmi_pending=true | legacy API | ✓ |  |
| z80_cpu.cpp:935 save_state schema | regs+MEMPTR+Q+iff2_read+interrupts_enabled_at+int_pending+vector+stamp | persistence | ✓ | Pass-3/4 |
| z80_cpu.cpp:935 save_state IncDecZ | NOT persisted (intentional) | n/a | ✓ | documented |
| z80_cpu.cpp:140 sync_regs_from_fuse | mirror FUSE z80 struct into Z80Registers | n/a | ✓ | Pass-3 MEMPTR+Q included |
| z80_cpu.cpp:343 sync_fuse_from_regs | push Z80Registers into FUSE | n/a | ✓ |  |
| z80_cpu.cpp:1033 z80_set_contention_runtime | install ContentionModel + Mmu | G53/G141 | ✓ |  |
| im2.h:14 Im2Level enum | 14 entries | scaffold legacy | ✓ |  |
| im2.h:33 DevIdx enum | LINE=0..UART1_TX=13 | zxnext.vhd:1941 priority order | ✓ |  |
| im2.h:46 DevState enum | S_0..S_ISR | im2_device.vhd:83 | ✓ |  |
| im2.h:179 DecState enum | S_0..S_DDFD_T4 | im2_control.vhd:158-209 | ✓ |  |
| im2.cpp:66 tick() ordering | step_pulse → step_devices → step_dma_delay → clear int_unq → clear int_req | V19R-CPU-01 + V19-IM2-03 | ✓ | **Pass-19 fix verified** |
| im2.cpp:141 int_req auto-clear | clear at end of tick | V19R-CPU-01 synthesises 1-cycle pulse | ✓ | **Pass-19 fix verified** |
| im2.cpp:163 to_devidx bridge | maps Im2Level → DevIdx | scaffold | ✓ |  |
| im2.cpp:191 raise(Im2Level) | no-op for DMA/DIVMMC/MULTIFACE; else route | V18R-CPU-02 | ✓ |  |
| im2.cpp:224 clear(Im2Level) | mirror of raise | V18R-CPU-02 | ✓ |  |
| im2.cpp:241 has_pending() | legacy mask + int_req walk | unobserved in prod | ✓ | dead code (documented) |
| im2.cpp:248 get_vector() | legacy `i*2` walk | unobserved in prod | ✓ | dead code |
| im2.cpp:261 on_reti() legacy | walk dev_[], clear S_ISR with IEI snapshot | im2_device.vhd:123-128 | ✓ | **V21-IM2-01 fix: gate also on im_mode_==2** |
| im2.cpp:340 on_retn() | no-op (consumed by divmmc/mmc only) | im2_control.vhd doesn't route into im2_device | ✓ |  |
| im2.cpp:358 raise_req(DevIdx) | sets int_req=true | per-device level | ✓ |  |
| im2.cpp:363 clear_req(DevIdx) | clears int_req | per-device | ✓ |  |
| im2.cpp:376 raise_unq(DevIdx) | one-shot int_unq + int_status + im2_int_req | im2_peripheral.vhd:160,172 | ✓ |  |
| im2.cpp:90 int_unq end-of-tick clear | one-cycle pulse semantic | nr_20_we VHDL :1946-1947 | ✓ | **V19-IM2-03 fix verified** |
| im2.cpp:388 clear_status(DevIdx) | clears int_status only | im2_peripheral.vhd:160 vs :175 | ✓ |  |
| im2.cpp:397 int_status(DevIdx) | int_status \|\| im2_int_req | im2_peripheral.vhd:180 | ✓ |  |
| im2.cpp:409 int_status_mask_c8 | bits {LINE,ULA} | zxnext.vhd:6247-6248 | ✓ |  |
| im2.cpp:423 int_status_mask_c9 | bits 7:0 = CTC7..CTC0 | zxnext.vhd:6250-6251 | ✓ | CTC4..CTC7 hard-zero per :4092 |
| im2.cpp:446 int_status_mask_ca | UART pack with RX-bit duplication | zxnext.vhd:6253-6254 | ✓ | duplication of RX bit per VHDL |
| im2.cpp:474 set_int_en(DevIdx,bool) | per-device | per-device gate | ✓ |  |
| im2.cpp:483 set_int_en_c4 | bit 1 → LINE; bit 0 NOT touched (port_ff owner) | zxnext.vhd:5607-5610 (bit 1) + :3621-3622 (bit 0 fanout) | ✓ | V19-IM2-01 |
| im2.cpp:493 set_int_en_c5 | bits 7:0 → CTC7..CTC0 | zxnext.vhd:4078 nr_c5_we | ✓ |  |
| im2.cpp:514 set_int_en_c6 | UART RX/TX fanout (bit5 OR bit4 → UART1_RX etc.) | zxnext.vhd:5615-5617 + :1950 | ✓ |  |
| im2.cpp:528 set_vector_base | 3-bit msb | zxnext.vhd:1999 | ✓ |  |
| im2.cpp:533 set_mode | im2_mode_ | nr_c0_int_mode_pulse_0_im2_1 | ✓ |  |
| im2.cpp:536 set_stackless_nmi | store-only | nr_c0_stackless_nmi (F-deferred) | ✓ |  |
| im2.cpp:551 set_dma_int_en_mask | mask14 fanned to dev_[i].dma_int_en | zxnext.vhd:1957-1958 | ✓ |  |
| im2.cpp:561 dma_int_pending | OR-reduction | im2_device.vhd:151 | ✓ |  |
| im2.cpp:579 dma_delay | returns latch | zxnext.vhd:2001-2010 | ✓ |  |
| im2.cpp:590 set_nmi_activated / set_nr_cc_dma_int_en_0_7 | NMI dma-delay inputs | zxnext.vhd:2007 second OR term | ✓ |  |
| im2.cpp:598 int_line_asserted | walk dev_[] for S_REQ + IEI=1 in im2_mode + **im_mode_==2** | im2_device.vhd:150 + peripherals AND-reduction | ✓ | **V21-IM2-01 fix**: now gates on im_mode_==2 (= VHDL z80_im_mode(1)) |
| im2.cpp:633 ack_vector | walk priority → first IEI-clear S_REQ → S_ACK; return composed vector; gated on im2_mode + **im_mode_==2** | im2_device.vhd:111-116, :155; zxnext.vhd:1999 | ✓ | **V21-IM2-01 fix**: now gates on im_mode_==2 |
| im2.cpp:670 on_m1_cycle decoder | clear seen pulses; advance FSM; latch reti_decode/dma_delay_ctrl | im2_control.vhd:158-240 | ✓ |  |
| im2.cpp:739 advance_decoder FSM | S_0/S_ED_T4/S_ED4D_T4/S_ED45_T4/S_CB_T4/S_SRL_T1/S_SRL_T2/S_DDFD_T4 | im2_control.vhd:158-209 | ✓ |  |
| im2.cpp:752 IM-mode decode | bit4 AND bit3 → IM2; bit4 & NOT bit3 → IM1; else IM0 | im2_control.vhd:218-227 | ✓ |  |
| im2.cpp:802 S_CB_T4 → S_0 | unconditional | VHDL :193-198 | ✓ |  |
| im2.cpp:811 S_DDFD_T4 → S_0 on ED | VHDL-faithful (no special-case for ED after DDFD) | im2_control.vhd:200-203 | ✓ | V11-CPU-01 |
| im2.cpp:855 step_devices | Phase 1 edge detect; Phase 2 SM with IEI snap; Phase 3 no-op | im2_peripheral.vhd + im2_device.vhd | ✓ |  |
| im2.cpp:872 im2_reset_n gate | `im2_mode_` (V17-CPU-01) | im2_peripheral.vhd:105 | ✓ |  |
| im2.cpp:950 step_state_machine_with_iei S_REQ→S_ACK | done in ack_vector() (CPU IntAck side) | im2_device.vhd:111-116 | ✓ | V21-IM2-01 gate now in ack_vector |
| im2.cpp:1007 step_state_machine_with_iei S_ISR→S_0 | reti_seen + iei + **im_mode_==2** | im2_device.vhd:123-128 | ✓ | **V21-IM2-01 fix**: now gates on im_mode_==2 |
| im2.cpp:1063 step_pulse | OR-reduce pulse_en; pulse_int_n_ FSM 32/36 counter | zxnext.vhd:2017-2031 | ✓ |  |
| im2.cpp:1073 ULA exception path | pulse fires when im2_mode AND NOT im2 OR NOT im2_mode | im2_peripheral.vhd:192 | ✓ |  |
| im2.cpp:1079 non-exception path | pulse fires only when NOT im2_mode | im2_peripheral.vhd:186 | ✓ |  |
| im2.cpp:1149 step_dma_delay | dma_int OR (nmi_active AND nr_cc_b7) OR (self-hold AND dma_delay) | zxnext.vhd:2001-2010 | ✓ |  |
| im2.cpp:1156 compute_vector | (base<<5) \| (idx<<1) | zxnext.vhd:1999 | ✓ |  |
| im2.cpp:1178 device_ieo | iterative IEO walk from device 0 | im2_device.vhd:136-146 | ✓ |  |
| im2.cpp:1204 propagate_isr_serviced | no-op (inline in SM) | im2_peripheral.vhd:137-148 collapsed | ✓ | documented |
| im2.cpp:1226 save_state | dev_[] + decoder + pulse + NR0xC0 + DMA + ACK + legacy mask | persistence | ✓ |  |
| im2.cpp:1266 load_state | mirror save | persistence | ✓ |  |
| emulator.cpp:111 prev_pulse_int_n_ init | reset to true | V20 falling-edge shadow | ✓ | V20R-CPU-NIT-01 also in save/load |
| emulator.cpp:254 init() ULA int_en seed | from port_ff_reg(6) | V19-IM2-02 init | ✓ |  |
| emulator.cpp:261 init() LINE int_en seed | false (matches VHDL reset) | V19-IM2-01 init | ✓ |  |
| emulator.cpp:359 init() machine timing 48/+3 fanout | both cpu_ and im2_ | zxnext.vhd:2033 | ✓ |  |
| emulator.cpp:660 cpu_.on_m1_cycle | im2_.on_m1_cycle + RETI/RETN handling + divmmc + MF | im2_control.vhd FSM consumers | ✓ |  |
| emulator.cpp:716 cpu_.on_int_ack | im2_.ack_vector() | priority-chain vector | ✓ |  |
| emulator.cpp:1850 NR 0x22 write handler | ula_int_disabled + line_int_en + IM2-fabric LINE int_en + port_ff_reg(6) | zxnext.vhd:5297, :3619-3620 | ✓ | V19-IM2-01/02 + V12-NMP-01 |
| emulator.cpp:1887 NR 0x22 → IM2 ULA int_en | (port_ff_reg & 0x40) == 0 | zxnext.vhd:6711 | ✓ | V19-IM2-02 |
| emulator.cpp:2738 NR 0xC0 write handler | vector_base + stackless_nmi + im2_mode | zxnext.vhd:5092,:5597 | ✓ |  |
| emulator.cpp:2766 NR 0xC4 write handler | set_int_en_c4 + video_timing_.set_line_int_enable + port_ff_reg(6) NOT polarity + IM2 ULA int_en + expbus | zxnext.vhd:5607-5610 + :3621-3622 | ✓ | V12-NMP-01 + V19-IM2-01/02 |
| emulator.cpp:2837 NR 0xC5 write handler | im2_.set_int_en_c5 + ctc_.set_int_enable | zxnext.vhd:4078 | ✓ |  |
| emulator.cpp:2850 NR 0xC6 write handler | im2_.set_int_en_c6 | zxnext.vhd:5615-5617 | ✓ |  |
| emulator.cpp:2863 NR 0xC8 write handler | clear_status LINE/ULA on bits 1/0 | zxnext.vhd | ✓ |  |
| emulator.cpp:2878 NR 0xC9 write handler | clear_status CTC7..CTC0 | zxnext.vhd | ✓ |  |
| emulator.cpp:2898 NR 0xCA write handler | clear_status UART positions | zxnext.vhd | ✓ |  |
| emulator.cpp:2912 NR 0xCC/CD/CE write handler | set_dma_int_en_mask + set_nr_cc_dma_int_en_0_7 (bit 7 of CC) | zxnext.vhd:1957-1958 | ✓ |  |
| emulator.cpp:2953 NR 0x20 read handler | pack int_status LINE/ULA/CTC0..CTC3 | zxnext.vhd | ✓ |  |
| emulator.cpp:2963 NR 0x20 write handler | raise_unq for LINE/ULA/CTC0..CTC3 per bit layout | zxnext.vhd:1946-1947 | ✓ |  |
| emulator.cpp:3327 port 0xFF write handler | port_ff_reg = val + ula_int_disabled + IM2 ULA int_en + screen_mode | zxnext.vhd:3615-3616 + :6711 | ✓ | V12-NMP-01 + V19-IM2-02 + V17-NMP-03 |
| emulator.cpp:4668 ctc_.on_interrupt | raise_req(CTCx) | per-channel int_req | ✓ | V20-IM2-01 poll covers CPU notify |
| emulator.cpp:4709 dma_.on_interrupt | no-op (V18R-CPU-02) | DMA is INT victim | ✓ |  |
| emulator.cpp:4717 uart_.on_tx_interrupt | raise_req(UART*_TX) | per-channel | ✓ | V20-IM2-01 poll covers CPU notify |
| emulator.cpp:4722 uart_.on_rx_interrupt | raise_req(UART*_RX) with avail-gate | zxnext.vhd:1941-1944 | ✓ | V20-IM2-01 poll covers CPU notify |
| emulator.cpp:5386 dma_.set_dma_delay | per-frame from im2_.dma_delay() | dma_delay latch | ✓ |  |
| emulator.cpp:5443 FRAME-INT scheduler | raise_req(ULA) only (legacy CPU notify dropped) | scheduler | ✓ | V20R-CPU-NIT-02 |
| emulator.cpp:5692 set_nmi_activated push | from NmiSource | dma_delay second term | ✓ |  |
| emulator.cpp:5693 im2_.tick | per-tick fabric advance | clk_cpu rising | ✓ |  |
| emulator.cpp:5789 IM2-mode INT poll | int_line_asserted → request_interrupt(0xFE) | zxnext.vhd:1840 im2_int_n | ✓ | V19-IM2-04 (gate now correct via V21-IM2-01) |
| emulator.cpp:5857-5862 pulse-mode INT poll | falling-edge pulse_int_n → request_interrupt(0xFF) | zxnext.vhd:1840 pulse_int_n | ✓ | V20-IM2-01 + V20R-CPU-NIT-01 |
| emulator.cpp:6300 reset() prev_pulse_int_n_ | reset to true | V20 shadow | ✓ |  |
| emulator.cpp:6716-6727 LINE-INT scheduler (`reschedule_line_interrupt()`) | raise_req(LINE) only | scheduler | ✓ | V20R-CPU-NIT-02 |
| emulator.cpp:6807 save_state | im2_.save_state + nr_02_bus_reset + prev_pulse_int_n_ | persistence | ✓ | V20R-CPU-NIT-01 appended |
| emulator.cpp:7327 load_state prev_pulse_int_n_ | eof-tolerant read | V20R-CPU-NIT-01 | ✓ |  |

## Findings

### V21-IM2-01 — IM2 fabric `int_line_asserted` / `ack_vector` / `on_reti` / `S_ISR→S_0` do not gate on `i_im2_mode` [class-(c) — FIXED]

**Description.** Per VHDL `zxnext.vhd:1973-1974`:

```
i_im2_mode           => z80_im_mode(1),     -- "1 if z80 is in im 2 mode"
i_mode_pulse_0_im2_1 => nr_c0_int_mode_pulse_0_im2_1,
```

The `i_im2_mode` input fed to `im2_peripheral` (and through it to `im2_device`) is **separate** from `i_mode_pulse_0_im2_1`. The first reflects the Z80's actual IM mode (bit 1 of the 2-bit `im_mode` decoded by `im2_control` from `ED 5E`/`ED 7E`); the second reflects the NR 0xC0 b0 mode select.

`im2_device.vhd` gates on **`i_im2_mode`** at multiple points:
- `:150` — `o_int_n <= '0' when state = S_REQ and i_iei = '1' and i_im2_mode = '1'` — the aggregate `im2_int_n` line driving the Z80 /INT pin.
- `:112` — `S_REQ → S_ACK` requires `i_m1_n='0' and i_iorq_n='0' and i_iei='1' and i_im2_mode='1'`.
- `:124` — `S_ISR → S_0` on RETI requires `i_reti_seen='1' and i_iei='1' and i_im2_mode='1'`.

(The reset-hold gate at `im2_peripheral.vhd:105` uses `i_mode_pulse_0_im2_1`, NOT `i_im2_mode` — so the device FSM does run in IM2-fabric mode regardless of `z80_im_mode`. The `i_im2_mode` gating only affects the **output** to the CPU and the **transitions out of S_REQ / S_ISR**.)

**Pre-fix.** `Im2Controller::int_line_asserted()` (`im2.cpp:605`) and `Im2Controller::ack_vector()` (`im2.cpp:632`) gated only on `im2_mode_` (the `i_mode_pulse_0_im2_1` shadow). `Im2Controller::on_reti()` (`im2.cpp:281`) and `Im2Controller::step_state_machine_with_iei()` S_ISR branch (`im2.cpp:1005`) had no `i_im2_mode` gate at all.

Pre-fix consequences in IM2-fabric mode (`nr_c0_int_mode_pulse_0_im2_1='1'`) with the Z80 still in IM=0 or IM=1:
1. **`int_line_asserted()` returns true** when any device reaches S_REQ with IEI=1 — the poll at `emulator.cpp:5789` then calls `cpu_.request_interrupt(0xFE)`. The CPU services the interrupt **via its current IM mode** (IM=0 / IM=1 → `RST $38`), which real hardware never reaches because `im2_int_n` would have stayed HIGH in VHDL.
2. **`ack_vector()` advances S_REQ → S_ACK** and returns a composed vector — but if the CPU is in IM=0/1, FUSE's `fuse_z80_interrupt` ignores the vector and uses `PC=0x0038`. The device sits in S_ACK from a `RST $38` IRET (which is `C9` = RET, NOT `ED 4D` = RETI) and the daisy chain is left dangling — `S_ACK → S_ISR` on the next tick, and S_ISR holds indefinitely because the `RET` returned without firing the RETI decoder.
3. **`on_reti()` clearing S_ISR regardless of Z80 IM mode** — pre-fix a RETI executed during an IM=0/1 transient could clear an S_ISR device that VHDL would keep latched. Symmetric problem on the modern path (`step_state_machine_with_iei` S_ISR branch).

**Boot-realistic probability.** Low but not zero:
- Real NextZXOS supervisor sets `IM 2` (ED 5E) early in boot before flipping NR 0xC0 b0 to 1.
- However, the FPGA reset puts `z80_im_mode = "00"` (IM 0) AND NR 0xC0 b0 = 0 (pulse mode). Any boot path that flips NR 0xC0 b0 BEFORE the supervisor executes `ED 5E` sees the bug. Most NextZXOS supervisors execute `ED 5E` first (since IM 2 is required for the vectored fabric to be useful at all), but a custom boot ROM or third-party loader that does `OUT (NextReg),0xC0,1` and then runs INT-enabled code before executing `ED 5E` would observe a spurious `RST $38` dispatch.
- Also: any code path that flips Z80 IM mode mid-handler (e.g. an ISR that ends with `IM 1; EI; RETI` to disable IM2 dispatch but keep INT enabled) would observe the asymmetric "transition gates" — devices that should remain latched per VHDL would clear in jnext.

**Fix.** `src/cpu/im2.cpp`:
- `int_line_asserted()` (line 605): add `if (im_mode_ != 2) return false;` after the `im2_mode_` gate.
- `ack_vector()` (line 632): add `if (im_mode_ != 2) return 0xFF;` after the `im2_mode_` gate.
- `on_reti()` (line 281): add `if (im_mode_ != 2) return;` early-out after the `im2_mode_` gate.
- `step_state_machine_with_iei()` S_ISR branch (line 1005): add `&& im_mode_ == 2` to the existing `reti_seen_pulse_ && iei` clear gate.

Tests updated:
- `test/ctc/ctc_test.cpp`: `fresh(Im2Controller&)` helper now feeds `ED 5E` post-reset to set `im_mode_=2` (the precondition for daisy-chain tests). PULSE-03 and ULA-INT-09 explicitly override with `ED 46` (IM 0) to exercise the EXCEPTION-pulse path's "CPU not in IM=2" branch.
- `test/cpu/cpu_z80n_im2_regressions_test.cpp`: V11-CPU-01 / Pass-10 / V19-IM2-03 / V19R-CPU-01 tests pre-feed `ED 5E` before driving the daisy chain.
- `test/ctc_interrupts/ctc_interrupts_test.cpp`: ULA-INT-V19-IM2-01 and ULA-INT-V19-IM2-04 pre-feed `ED 5E` via `emu.im2().on_m1_cycle()` directly so the test exercises the post-V21 gate.

**Test.** New `V21-IM2-01-INT-LINE-GATED-ON-IM-MODE-VHDL-150-1974` in `test/cpu/cpu_z80n_im2_regressions_test.cpp` exercises three sub-cases on a fresh `Im2Controller` in IM2-fabric mode (`set_mode(true)`), with LINE int_en set and `raise_req(LINE)`:

| sub-case | `im_mode_` driver | expected (post-fix) | observed (pre-fix) |
|---|---|---|---|
| (a) | no IM instruction (im_mode_=0) | int_line=false, ack=0xFF, state=S_REQ | int_line=true, ack=0x00, state=S_ACK |
| (b) | `ED 56` (IM 1) (im_mode_=1) | int_line=false, ack=0xFF, state=S_REQ | int_line=true, ack=0x00, state=S_ACK |
| (c) | `ED 5E` (IM 2) (im_mode_=2) [positive control] | int_line=true, ack=0x00 (base=0,idx=0), state=S_ACK | int_line=true, ack=0x00, state=S_ACK (unchanged) |

Sub-case (c) is the positive control — verifies the gate doesn't over-mask the legitimate IM=2 path.

Discriminative verification: reverted the two gate lines in `int_line_asserted` and `ack_vector` (commented out) → test failed with sub-cases (a)+(b) showing the pre-fix observable. Restored → test passes.

**Commit.** `171c51e` — `fix(task2-pass21-cpu): V21-IM2-01 — IM2 int_line / ack_vector gate on im_mode_==2 per VHDL i_im2_mode`.

**VHDL oracle citations.**
- `zxnext.vhd:1973-1974` — `i_im2_mode => z80_im_mode(1)` wiring.
- `zxnext.vhd:1905-1906` — `o_im_mode => z80_im_mode` from im2_control decoder.
- `im2_device.vhd:150` — `o_int_n` gates on `i_im2_mode`.
- `im2_device.vhd:111-116` — S_REQ → S_ACK gates on `i_im2_mode`.
- `im2_device.vhd:123-128` — S_ISR → S_0 gates on `i_im2_mode`.
- `im2_control.vhd:218-227` — `im_mode` decoded from ED `46/4E/66/6E` (IM 0), ED `56/76` (IM 1), ED `5E/7E` (IM 2).

## Re-verification of Pass-19 / Pass-20 fixes

Per Pass-21 prompt: re-verify Pass-19 + Pass-20 fixes don't have residual bugs.

- **V19-IM2-01 (NR 0x22 b1 + NR 0xC4 b1 → dev_[LINE].int_en)**: clean. Both writers update IM2 fabric; init seeds correct reset value (false). VHDL :5297 + :5610 + :1950 + :6711 wiring verified row-by-row.
- **V19-IM2-02 (port_ff_reg(6) → dev_[ULA].int_en)**: clean. All three writers (port-FF write, NR 0x22 b2 fanout, NR 0xC4 b0 NOT-fanout) update IM2 fabric. Init seeds from port_ff_reg(6) state. VHDL :6711 + :3614-3622 verified.
- **V19-IM2-03 (int_unq one-shot semantic)**: clean. End-of-tick clear matches VHDL nr_20_we one-cycle pulse semantic. NR 0x20 write handler's bit-layout matches VHDL :1946-1947 (LINE←b7, ULA←b6, CTC0..CTC3←b0..b3).
- **V19-IM2-04 (int_line_asserted polling)**: clean per V19, **enhanced by V21-IM2-01** (the gate is now correctly `im2_mode_ && im_mode_==2`).
- **V19R-CPU-01 (int_req 1-cycle pulse synthesis)**: clean. Sequencing `step_pulse → step_devices → step_dma_delay → clear int_unq → clear int_req` is correct.
- **V20-IM2-01 (pulse-mode CPU /INT poll on pulse_int_n falling edge)**: clean. Polled at `emulator.cpp:5857-5862`. Edge detection via `prev_pulse_int_n_` shadow.
- **V20R-CPU-NIT-01 (`prev_pulse_int_n_` persistence in save/load)**: clean. Write at `:7080`, eof-tolerant read at `:7327`. Reset paths reset shadow to true.
- **V20R-CPU-NIT-02 (drop legacy ULA/LINE `request_interrupt(0xFF)` callbacks)**: clean. FRAME-INT scheduler at `:5443` now only does `raise_req(ULA)`. LINE-INT scheduler at `:6716-6727` similarly. The V20 pulse-mode poll is the sole driver of pulse-mode /INT. `request_interrupt_count_` observable counter wired and verified.

## Z80N opcode re-audit details

Per Pass-21 prompt: be thorough on Z80N opcodes. Full re-read of all 31 Z80N opcodes against `t80n.vhd:702-1028` (the BSLA/BSRA/BSRF/BSRL/BRLC/SETAE/PIXELDN/PIXELAD inline computation block) + `t80n_mcode.vhd:1761-2570` (mcode dispatch + MCycles/TStates).

Findings — **NO new bugs**. All 31 opcodes match VHDL semantically + T-state-wise + flag-wise:

| Opcode | Re-verified against | Notes |
|---|---|---|
| SWAPNIB | t80n.vhd:702-704 | Q stays 0 |
| MIRROR_A | t80n.vhd:706-708 | Q stays 0 |
| TEST_N | t80n_mcode.vhd:1779-1788 default ALU_Op=AND | H=1, P=parity, X/Y from result, A preserved |
| BSLA_DE_B | t80n.vhd:987-993 + V17-Z80N-01a | shift>=16 → 0 (numeric_std width) |
| BSRA_DE_B | t80n.vhd:1006-1014 + V17-CPU-NIT-04 | shift>=16 → sign fill (0xFFFF if MSB else 0) |
| BSRL_DE_B | t80n.vhd:1006-1014 (bit16=IR(0)=0) | shift>=16 → 0 (int-promotion) |
| BSRF_DE_B | t80n.vhd:1006-1014 (bit16=IR(0)=1) + V17-Z80N-01b | shift>=16 → 0xFFFF |
| BRLC_DE_B | t80n.vhd:1022-1028 rotate_left mod 16 | rot==16 handled via int-promotion |
| MUL_DE | t80n.vhd:729-735 | no F write |
| ADD_HL/DE/BC_A | t80n.vhd:778-783 | Pass-10: F.C forced 0 |
| ADD_HL/DE/BC_NN | t80n_mcode.vhd:1872-1878 LDZ+LDW | MEMPTR=nn |
| PUSH_NN | t80n_mcode.vhd:1928,1938 | WZ-lo only |
| OUTINB | t80n_mcode.vhd:2521-2531 | 1T extended-M1 via IR (V12-CPU-NIT-02) |
| NEXTREG_NN/A | t80n_mcode.vhd:1668-1707 | Bypass IORQ; OUT to 0x243B/0x253B |
| PIXELDN | t80n.vhd:900-921 | V11-CPU-02: H[7:5] preserved |
| PIXELAD | t80n.vhd:939-947 | "010" base address |
| SETAE | t80n.vhd:923-937 | 0x80 >> (E&7) |
| JP_C | t80n.vhd:980-983 + t80n_mcode.vhd:1837-1848 | T=12 (Pass-8) |
| LDIX | t80n_mcode.vhd:2098-2138 + I_BT block | flags per VHDL I_BT override |
| LDWS | t80n_mcode.vhd:2141-2181 | flags from D+1 ALU + I_BT override + IncDecZ override |
| LDDX | t80n_mcode.vhd:2230-2256 | DE++ (yes, increment per VHDL!), HL-- |
| LDIRX/LDDRX/LDPIRX/LDIRSCALE | shared MCycles="100" | G89 PC rewind |

**Subtle re-check items**:
- BRLC with rot=16: `regs.DE << 0 | regs.DE >> 16` — `uint16_t >> 16` after integer promotion to `int` (32-bit) yields 0; final result = `regs.DE | 0 = regs.DE`. Matches VHDL `rotate_left(x, 16) = x`. Correct via accident-of-promotion.
- BSRL with shift=16..31: `regs.DE >> shift` after promotion to `int` is well-defined for shift<32 (shift count < width of promoted type). Result is 0 for any shift ≥ 16 on a 16-bit value. Matches VHDL signed 17-bit shift_right with bit 16=0.
- LDPIRX MEMPTR-lo set to 0xB7 per V18R-CPU-NIT-01.
- OUTINB does NOT update MEMPTR — VHDL t80n_mcode.vhd:2517-2545 doesn't set LDZ/LDW for OUTINB. Matches.
- All Z80N opcodes that write F also set Q=F (FUSE convention for SCF/CCF X/Y).
- All non-F-writing Z80N opcodes leave Q=0 (set at dispatch hygiene).

## IM2 architecture re-audit details

Per Pass-21 prompt: pay special attention to V19+V20 IM2 architecture cleanup, look for `level-vs-pulse` antipatterns in adjacent IM2 wiring, look for `request_interrupt(0xFF)` call sites not covered by polling.

**`request_interrupt` audit** (`grep -rn request_interrupt src/`):
- `src/cpu/z80_cpu.cpp:925` — implementation, increments observability counter.
- `src/core/emulator.cpp:5790` — V19-IM2-04 IM2-mode poll, gated on `is_im2_mode() && int_line_asserted()` (post-V21: implicitly also on im_mode_==2).
- `src/core/emulator.cpp:5860` — V20-IM2-01 pulse-mode poll, gated on `!is_im2_mode() && !cur_pulse_int_n && prev_pulse_int_n_` (falling edge).
- **No other call sites**. Legacy ULA/LINE scheduler callbacks dropped in V20R-CPU-NIT-02. CTC `on_interrupt`, UART `on_tx_interrupt`/`on_rx_interrupt` all go through `raise_req(DevIdx)` exclusively — CPU notification comes from the post-tick polls.

**Level-vs-pulse semantic audit**:
- `i_int_req` model: jnext stores `int_req` as a level, auto-cleared at end of tick (V19R-CPU-01). The edge `int_req && !int_req_d` matches VHDL `int_req` synthesis at im2_peripheral.vhd:101. Verified.
- `pulse_int_n` model: registered FSM at `step_pulse()`, level signal with 32/36-cycle gate. Polling uses falling-edge detection — symmetric with VHDL `falling_edge(i_CLK_28)` registered process at zxnext.vhd:2019.
- `im2_int_n` model: combinational aggregate of per-device `o_int_n`. `int_line_asserted()` recomputes per call (level-based). Polling at `:5790` is level-based — re-stamps `int_pending_` until IntAck. Verified harmless because once acked, the responsible device advances S_REQ→S_ACK and the aggregate drops.
- `reti_seen` / `retn_seen` / `int_unq`: all one-cycle pulses, properly synthesised + consumed within tick + cleared at end of tick. Verified.

**Save/load schema audit**: prev_pulse_int_n_ persisted (V20R-CPU-NIT-01, eof-tolerant). FUSE-internal `interrupts_enabled_at` + `iff2_read` persisted (Pass-4). MEMPTR + Q persisted (Pass-3). IncDecZ intentionally NOT persisted (Pass-9 — append-stream invariant; worst-case single-LDWS observability bounded). NO new schema fields needed for V21-IM2-01 (the fix changes only logic, no new state).

## Tests passing

Final test results post-V21-IM2-01 fix:

```
ctest 38/38
FUSE Z80 1356/1356
cpu_int_pulse_test 11/11
cpu_z80n_im2_regressions_test 46/46 (+1 vs Pass-20)
ctc_test 132/132
ctc_interrupts_test 30/30
regression.sh 33/0/0
```
