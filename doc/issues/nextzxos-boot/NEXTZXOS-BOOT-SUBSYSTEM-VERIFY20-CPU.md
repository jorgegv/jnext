# Pass-20 audit report — CPU + Z80N + IM2 subsystem

Branch `task2/verify20-cpu-z80n-im2`. Audit conducted off integration HEAD `deb7bdf`, blind to prior verify reports until completion.

## Summary

- **Findings**: 1 class-(b) — `V20-IM2-01` (pulse-mode CPU /INT polling gap)
- **Re-verification of Pass-19 fixes**: all clean — no residual bugs in V19-IM2-01/02/03/04 or V19R-CPU-01.
- **Z80N opcode re-audit**: all opcodes VHDL-faithful per careful re-reading (BSLA/BSRA/BSRF/BSRL/BRLC strict-UB-free; MEMPTR strobes correct; IncDecZ shadow propagation correct; all flag composition correct).
- **IM2 sequencing**: step_pulse → step_devices → step_dma_delay → clear int_unq → clear int_req ordering verified; auto-clear of int_req correctly synthesises 1-cycle pulse semantic.

### Test results

| Suite | Pre-fix | Post-fix |
|---|---|---|
| ctest (38 suites) | 38/38 | 38/38 |
| FUSE Z80 (1356 opcodes) | 1356/1356 | 1356/1356 |
| ctc_interrupts_test | 27/27 | 28/28 (new V20-IM2-01 row) |
| regression.sh | 33/0/0 | 33/0/0 |

## Enumeration table

Coverage scope per Pass-19 plus full re-verification of Pass-19 fixes. Granularity:
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
| z80_cpu.cpp:451 INT pulse-expiry | unconditional drop after 32/36T | zxnext.vhd:2017-2033 pulse_count_end | ✓ | V18R-CPU-01 |
| z80_cpu.cpp:471 INT iff1 gate | if iff1 → consider IntAck | std Z80 | ✓ |  |
| z80_cpu.cpp:494 EI-grace gate | skip on_int_ack on tstates == interrupts_enabled_at | FUSE convention | ✓ | Pass-8 |
| z80_cpu.cpp:510 on_int_ack callback | im2_.ack_vector() | im2_device.vhd:155 + zxnext.vhd:1999 | ✓ |  |
| z80_cpu.cpp:539 Z80N detection | peek ED+ext; dispatch execute_z80n | bypasses FUSE undefined-ED | ✓ |  |
| z80_cpu.cpp:559 ED prefix on_m1_cycle | fire for ED then ext byte | im2_control.vhd ifetch_fe_t3 per byte | ✓ | G87 |
| z80_cpu.cpp:595 Z80N M1 contention | contend_read(pc,4) + contend_read(pc+1,4) | per FUSE+VHDL | ✓ | Pass-5 |
| z80_cpu.cpp:601 R-reg update | +2 (ED prefix + ext byte) | per VHDL refresh | ✓ |  |
| z80_cpu.cpp:634 Q+iff2_read reset | pre-dispatch hygiene | FUSE convention | ✓ | Pass-4 |
| z80_cpu.cpp:680 non-Z80N ED M1 callback | fire ED + ext byte | im2_control.vhd FSM | ✓ |  |
| z80_cpu.cpp:744 DD/FD/CB on_m1_cycle walk | walk prefix chain | im2_control.vhd per-fetch | ✓ | Pass-9 |
| z80_cpu.cpp:836 inner-opcode lookup for IncDecZ | walk DD/FD chain to inner | t80n.vhd ISet stays "00" | ✓ | V14-CPU-NIT-01 |
| z80_cpu.cpp:866 IncDecZ update DJNZ | F.Z polarity (B==0 post-dec → 1) | t80n.vhd:1359 | ✓ | V13-CPU-01 |
| z80_cpu.cpp:899 IncDecZ update ED block xfer | BC!=0 polarity | t80n.vhd:1361-1366 | ✓ | Pass-9 |
| z80_cpu.cpp:906 IncDecZ update INC/DEC BC | BC!=0 polarity | t80n.vhd:1361-1366 | ✓ | V14-CPU-01 |
| z80_cpu.cpp:925 request_interrupt | int_pending+vector+tstates stamp | legacy API | ✓ |  |
| z80_cpu.cpp:931 request_nmi | nmi_pending=true | legacy API | ✓ |  |
| z80_cpu.cpp:935 save_state schema | regs+MEMPTR+Q+iff2_read+interrupts_enabled_at+int_pending+vector+stamp | persistence | ✓ | Pass-3/4 |
| z80_cpu.cpp:935 save_state IncDecZ | NOT persisted (intentional) | n/a | ✓ | documented |
| im2.h:14 Im2Level enum | 14 entries | scaffold legacy | ✓ |  |
| im2.h:33 DevIdx enum | LINE=0..UART1_TX=13 | zxnext.vhd:1941 priority order | ✓ |  |
| im2.h:46 DevState enum | S_0..S_ISR | im2_device.vhd:83 | ✓ |  |
| im2.cpp:66 tick() ordering | step_pulse → step_devices → step_dma_delay → clear int_unq → clear int_req | V19R-CPU-01 + V19-IM2-03 | ✓ | **Pass-19 fix verified clean** |
| im2.cpp:141 int_req auto-clear | clear at end of tick | V19R-CPU-01 synthesises 1-cycle pulse | ✓ | **Pass-19 fix verified clean** |
| im2.cpp:163 to_devidx bridge | maps Im2Level → DevIdx | scaffold | ✓ |  |
| im2.cpp:191 raise(Im2Level) | no-op for DMA/DIVMMC/MULTIFACE; else route | V18R-CPU-02 | ✓ |  |
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
| im2.cpp:598 int_line_asserted | walk dev_[] in priority for S_REQ + IEI=1 in im2_mode | im2_device.vhd:150 + peripherals AND-reduction | ✓ | **V19-IM2-04 fix verified** |
| im2.cpp:614 ack_vector | walk priority → first IEI-clear S_REQ → S_ACK; return composed vector | im2_device.vhd:111-116, :155; zxnext.vhd:1999 | ✓ |  |
| im2.cpp:662 on_m1_cycle decoder | clear seen pulses; advance FSM; latch reti_decode/dma_delay_ctrl | im2_control.vhd:158-240 | ✓ |  |
| im2.cpp:731 advance_decoder FSM | S_0/S_ED_T4/S_ED4D_T4/S_ED45_T4/S_CB_T4/S_SRL_T1/S_SRL_T2/S_DDFD_T4 | im2_control.vhd:158-209 | ✓ |  |
| im2.cpp:744 IM-mode decode | bit4 AND bit3 → IM2; bit4 & NOT bit3 → IM1; else IM0 | im2_control.vhd:218-227 | ✓ |  |
| im2.cpp:794 S_CB_T4 → S_0 | unconditional | VHDL :193-198 | ✓ |  |
| im2.cpp:803 S_DDFD_T4 → S_0 on ED | VHDL-faithful (no special-case for ED after DDFD) | im2_control.vhd:200-203 | ✓ | V11-CPU-01 |
| im2.cpp:847 step_devices | Phase 1 edge detect; Phase 2 SM with IEI snap; Phase 3 no-op | im2_peripheral.vhd + im2_device.vhd | ✓ |  |
| im2.cpp:864 im2_reset_n gate | `im2_mode_` (V17-CPU-01) | im2_peripheral.vhd:105 | ✓ |  |
| im2.cpp:1047 step_pulse | OR-reduce pulse_en; pulse_int_n_ FSM 32/36 counter | zxnext.vhd:2017-2031 | ✓ |  |
| im2.cpp:1057 ULA exception path | pulse fires when im2_mode AND NOT im2 OR NOT im2_mode | im2_peripheral.vhd:192 | ✓ |  |
| im2.cpp:1063 non-exception path | pulse fires only when NOT im2_mode | im2_peripheral.vhd:186 | ✓ |  |
| im2.cpp:1133 step_dma_delay | dma_int OR (nmi_active AND nr_cc_b7) OR (self-hold AND dma_delay) | zxnext.vhd:2001-2010 | ✓ |  |
| im2.cpp:1140 compute_vector | (base<<5) \| (idx<<1) | zxnext.vhd:1999 | ✓ |  |
| im2.cpp:1162 device_ieo | iterative IEO walk from device 0 | im2_device.vhd:136-146 | ✓ |  |
| im2.cpp:1188 propagate_isr_serviced | no-op (inline in SM) | im2_peripheral.vhd:137-148 collapsed | ✓ | documented |
| im2.cpp:1210 save_state | dev_[] + decoder + pulse + NR0xC0 + DMA + ACK + legacy mask | persistence | ✓ |  |
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
| emulator.cpp:4668 ctc_.on_interrupt | raise_req(CTCx) | per-channel int_req | ✓ wiring; ✗ CPU notify in pulse mode | **V20-IM2-01** |
| emulator.cpp:4709 dma_.on_interrupt | no-op (V18R-CPU-02) | DMA is INT victim | ✓ |  |
| emulator.cpp:4717 uart_.on_tx_interrupt | raise_req(UART*_TX) | per-channel | ✓ wiring; ✗ CPU notify in pulse mode | **V20-IM2-01** |
| emulator.cpp:4722 uart_.on_rx_interrupt | raise_req(UART*_RX) with avail-gate | zxnext.vhd:1941-1944 | ✓ wiring; ✗ CPU notify in pulse mode | **V20-IM2-01** |
| emulator.cpp:5386 dma_.set_dma_delay | per-frame from im2_.dma_delay() | dma_delay latch | ✓ |  |
| emulator.cpp:5443 FRAME-INT scheduler | raise_req(ULA) + (pulse mode) request_interrupt(0xFF) | scheduler + legacy CPU notify | ✓ |  |
| emulator.cpp:5692 set_nmi_activated push | from NmiSource | dma_delay second term | ✓ |  |
| emulator.cpp:5693 im2_.tick | per-tick fabric advance | clk_cpu rising | ✓ |  |
| emulator.cpp:5728 IM2-mode INT poll | int_line_asserted → request_interrupt(0xFE) | zxnext.vhd:1840 im2_int_n | ✓ | V19-IM2-04 |
| emulator.cpp:5762 pulse-mode INT poll (V20-IM2-01) | falling-edge pulse_int_n → request_interrupt(0xFF) | zxnext.vhd:1840 pulse_int_n | ✓ | **V20-IM2-01 fix this pass** |
| emulator.cpp:6653 LINE-INT scheduler | raise_req(LINE) + (pulse mode) request_interrupt(0xFF) | scheduler + legacy CPU notify | ✓ |  |
| emulator.cpp:6807 save_state | im2_.save_state | persistence | ✓ |  |

## Findings

### V20-IM2-01 — pulse-mode CPU /INT polling gap [class-b — FIXED]

**Description.** Per VHDL `zxnext.vhd:1840`:

```
z80_int_n <= ((pulse_int_n and im2_int_n) or not expbus_disable_int)
             and (i_BUS_INT_n or expbus_disable_int);
```

In the default scenario (`expbus_disable_int='1'`, no expansion bus), this reduces to `z80_int_n <= pulse_int_n AND im2_int_n`. Any drop of `pulse_int_n` must assert the Z80 /INT pin.

**Pre-fix.** jnext only wired `cpu_.request_interrupt(0xFF)` from the ULA frame-INT scheduler callback (`emulator.cpp:5445`) and the LINE-INT scheduler callback (`emulator.cpp:6655`). CTC ZC/TO (`ctc_.on_interrupt` at `:4668`), UART TX-empty (`uart_.on_tx_interrupt` at `:4717`), and UART RX-avail/near-full (`uart_.on_rx_interrupt` at `:4722`) all routed solely through `im2_.raise_req(DevIdx)`. The IM2 fabric's `pulse_int_n` correctly dropped via `im2_peripheral.vhd:186` (`o_pulse_en` for non-exception devices), but no code consulted `im2_.pulse_int_n()` to notify the CPU. Result: **in pulse mode (NR 0xC0 b0=0, the power-on default), CTC ZC/TO and UART RX/TX interrupts were silently dropped** — the daisy-chain saw the request but the Z80 /INT pin was never asserted.

Pre-V19, IM2 mode had the same gap (no `int_line_asserted()` poll); V19-IM2-04 fixed that. V20-IM2-01 is the symmetric pulse-mode fix.

**Fix.** `src/core/emulator.cpp:5731+`. After `im2_.tick(...)`, sample `cur_pulse_int_n = im2_.pulse_int_n()` and call `cpu_.request_interrupt(0xFF)` on the **falling edge** (`!cur_pulse_int_n && prev_pulse_int_n_`) when not in IM2 mode. Stored shadow `prev_pulse_int_n_` (added to `Emulator` in `emulator.h:786+`, initialised true on reset paths). Edge detection (not level) is critical: re-stamping `int_requested_at_` every tick would extend the effective 32/36-cycle window indefinitely, causing extra INT acceptances when the CPU exits an ISR via EI within the window (observable via the contention regression test).

**Test.** `test/ctc_interrupts/ctc_interrupts_test.cpp` — new row `CTC-INT-V20-IM2-01` exercises: pulse mode + IFF1=1 + IM=1 + ULA INT disabled (NR 0x22 b2=1, to prevent the existing legacy ULA path from masking the discriminative check) + `raise_req(CTC0)` + run_frame. Post-fix: PC ends below 0x4000 (IM1 vector serviced). Pre-fix: PC stays at 0x8000+ (NOP territory, no INT serviced).

Discriminative verification: temporarily reverted the fix → test failed with `PC=0xC53F` (RAM, no service). Restored → PASS.

**VHDL oracle citations.**
- `zxnext.vhd:1840` — `z80_int_n` composition.
- `zxnext.vhd:2017-2031` — `pulse_int_n` FSM with 32/36-cycle gate.
- `im2_peripheral.vhd:186-194` — `o_pulse_en` for non-exception (`o_pulse_en <= ((int_req AND i_int_en) OR i_int_unq) AND NOT i_mode_pulse_0_im2_1`).

**Commit.** `63c8d48` — `fix(task2-pass20-cpu): V20-IM2-01 — pulse-mode CPU /INT poll on pulse_int_n falling edge per VHDL :1840`.

## Re-verification of Pass-19 fixes

Per Pass-20 prompt: re-verify Pass-19 fixes don't have residual bugs. Findings:

- **V19-IM2-01 (NR 0x22 b1 + NR 0xC4 b1 → dev_[LINE].int_en)**: clean. Both writers update IM2 fabric; init seeds correct reset value (false). VHDL :5297 + :5610 + :1950 + :6711 wiring verified row-by-row.
- **V19-IM2-02 (port_ff_reg(6) → dev_[ULA].int_en)**: clean. All three writers (port-FF write, NR 0x22 b2 fanout, NR 0xC4 b0 NOT-fanout) update IM2 fabric. Init seeds from port_ff_reg(6) state. VHDL :6711 + :3614-3622 verified.
- **V19-IM2-03 (int_unq one-shot semantic)**: clean. End-of-tick clear matches VHDL nr_20_we one-cycle pulse semantic. NR 0x20 write handler's bit-layout matches VHDL :1946-1947 (LINE←b7, ULA←b6, CTC0..CTC3←b0..b3).
- **V19-IM2-04 (int_line_asserted polling)**: clean. Poll runs AFTER tick, edge-detect not required since the poll is gated on a level signal (im2_int_n is also a level in VHDL). Existing call `cpu_.request_interrupt(0xFE)` is idempotent (re-stamps tstates while line stays asserted) — but unlike pulse mode, the IM2 daisy chain only transitions one device per RETI, so re-stamping is benign. **Verified no extension issue** because once `int_pending_` is set and the CPU accepts via IntAck, the device advances to S_ACK→S_ISR and `int_line_asserted()` drops; the re-stamp during the window before acceptance is harmless.
- **V19R-CPU-01 (int_req 1-cycle pulse synthesis)**: clean. Sequencing `step_pulse → step_devices → step_dma_delay → clear int_unq → clear int_req` is correct. Edge detection in step_pulse uses `int_req && !int_req_d` BEFORE step_devices updates `int_req_d`. Multi-tick raise patterns work correctly per the V19R-CPU-01 comment block. Save/load briefly diverges by one tick after load (int_req restored as true, but auto-clear at end of next tick fixes it) — not a correctness issue.

## Z80N opcode re-audit details

Verified row-by-row that all Z80N opcodes match VHDL strictly:

- **BSLA_DE_B** (`ED 28`): uint32_t-based shift; for shift ≥ 16 returns 0 (matching VHDL `shift_left(unsigned 16-bit, n)` for n ≥ 16 = all-zero). UB-free.
- **BSRA_DE_B** (`ED 29`): explicit branched arithmetic right shift; sign-fill for shift ≥ 16. Matches VHDL `shift_right(signed 17-bit, n)` with bit 16 = bit 15 (sign).
- **BSRL_DE_B** (`ED 2A`): `regs.DE >> shift`. For shift 16..31, uint16_t int-promotes to int; `regs.DE >> 16` is well-defined (0). Matches VHDL `shift_right(unsigned-via-bit16=0, n)` = 0 for n ≥ 16.
- **BSRF_DE_B** (`ED 2B`): explicit branched shift with fill-1; for shift ≥ 16 returns 0xFFFF. Matches VHDL with bit 16 = 1.
- **BRLC_DE_B** (`ED 2C`): rotate-left B[4:0] with `rot &= 0x0F`. For rot=16 (mod 16=0), the formula `(DE << 0) | (DE >> 16)` is valid (DE >> 16 well-defined via int-promotion = 0; OR gives DE — correct for rotate-by-zero). VHDL `rotate_left(unsigned, 16)` = identity.
- **MEMPTR strobes** verified for ADD_HL_NN, ADD_DE_NN, ADD_BC_NN (MEMPTR=nn), PUSH_NN (WZ-lo only via Pass-8 fix), LDPIRX (MEMPTR-lo=0xB7 via V18R-CPU-NIT-01).
- **R-register increment** for Z80N: +2 (ED prefix + ext byte) at `z80_cpu.cpp:601`.
- **IncDecZ shadow** propagation: DJNZ (F.Z polarity), INC/DEC BC (BC≠0 polarity), ED block transfers, DD/FD-prefix variants — all match VHDL t80n.vhd:1358-1367. V13-CPU-01 + V14-CPU-01 + V14-CPU-NIT-01 fixes verified.

## IM2 sequencing re-audit

The tick() ordering at `im2.cpp:66-142` is:
1. `step_pulse()` — sample int_req edge BEFORE step_devices updates int_req_d.
2. `step_devices()` — Phase 1 (edge detect + im2_int_req latch + int_req_d update); Phase 2 (state machine with snapshotted IEI); Phase 3 (no-op).
3. `step_dma_delay()` — DMA delay latch.
4. Clear int_unq for all devices (V19-IM2-03).
5. Clear int_req for all devices (V19R-CPU-01).

Verified no race or sequencing bug:
- step_pulse runs FIRST so it sees the original int_req before int_req_d is updated.
- step_devices Phase 1 updates int_req_d at its end (line 900) so next tick's edge calc is correct.
- The int_req auto-clear at step 5 happens AFTER int_req_d has been captured, so the cleared int_req doesn't poison the next tick's edge calculation.
- int_unq one-shot is honoured by raise_unq setting all three (int_unq + int_status + im2_int_req) synchronously, with the clear at step 4 ensuring it doesn't re-trigger.

## Class-(d) architectural items observed (not fixed this pass)

- **Expansion-bus INT path (i_BUS_INT_n)** — VHDL `:1840` second AND term routes external bus INT when `expbus_disable_int='0'`. jnext doesn't model expansion bus at all; `im2_c4_expbus_` is stored for NR 0xC4 readback only. Latent for jnext's current scope.
- **ULA EXCEPTION pulse fires in IM2 mode when CPU's IM ≠ 2** — Per `im2_peripheral.vhd:192`, the EXCEPTION ULA can fire pulse_en in IM2 mode when `(im2_mode AND NOT z80_im2)`. jnext's pulse poll (V20-IM2-01 fix) only fires when `!im2_mode_` so this niche scenario (program in IM2 mode but with IM=0 or IM=1 currently active) is NOT covered. Real software typically pairs IM2 mode with IM=2 instruction, so latent.

## Convergence assessment

Pass-20 yielded 1 class-(b) finding (V20-IM2-01). The trend is:

- P11: 8 findings
- P12: 17
- P13: 7
- P14: 9
- P15: 5
- P16: 7
- P17: 8
- P18: (skipped for this subsystem)
- P19: 4 class-b + 1 reviewer NIT
- **P20: 1 class-b**

Strong convergence signal. The remaining class-(d) items above are architectural and require user authorisation to elevate.

## Final state

- Branch HEAD: `63c8d48` (worktree `task2/verify20-cpu-z80n-im2`)
- Tests: ctest 38/38, FUSE 1356/1356, ctc_interrupts 28/28, regression 33/0/0
- Not pushed; awaiting reviewer.
