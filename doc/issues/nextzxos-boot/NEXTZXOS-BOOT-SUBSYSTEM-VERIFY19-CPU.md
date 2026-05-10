# Pass-19 audit report — CPU + Z80N + IM2 subsystem

Branch `task2/verify19-cpu-z80n-im2`. Audit conducted off integration HEAD `ce11e9c`, blind to prior verify reports until completion.

## Enumeration table

Coverage scope follows the prompt's "one row per surface" rule. Granularity:
- one row per Z80N opcode × {F-flag composition, MEMPTR/WZ strobes, R/contention/T-states, side-effects} where applicable;
- one row per IM2 state-machine transition + Im2Level entry + IM2 source + register handler;
- one row per FUSE Z80 integration hook;
- one row per save_state schema field touching CPU/IM2 state.

| Surface (file:line) | C++ behaviour summary | VHDL oracle (file:line) | Match | Notes |
|---|---|---|---|---|
| z80n_ext.cpp:142 SWAPNIB ACC swap | nibble swap of A | t80n.vhd:702-704 ACC <= rt(3:0)&rt(7:4) | ✓ | identical |
| z80n_ext.cpp:142 SWAPNIB flags | none | t80n.vhd MCycle 1 only, no ALU | ✓ | no F write, Q stays 0 (pre-dispatch) |
| z80n_ext.cpp:142 SWAPNIB MEMPTR | unchanged | t80n_mcode.vhd no LDZ/LDW | ✓ | no LDZ/LDW assertion |
| z80n_ext.cpp:142 SWAPNIB T-states | 8 (M1+M1) | t80n_mcode.vhd:1761 TStates "100"=4T, no extra MCycle | ✓ | 8T total (4 ED + 4 SWAPNIB) |
| z80n_ext.cpp:151 MIRROR_A bit reverse | manual bit reverse | t80n.vhd:706-708 ACC <= rt(0)&rt(1)...&rt(7) | ✓ | identical |
| z80n_ext.cpp:151 MIRROR_A flags | none | t80n.vhd no F write | ✓ | Q stays 0 |
| z80n_ext.cpp:151 MIRROR_A MEMPTR | unchanged | no LDZ/LDW | ✓ |  |
| z80n_ext.cpp:151 MIRROR_A T-states | 8 | t80n_mcode.vhd:1768 TStates "100" | ✓ |  |
| z80n_ext.cpp:164 TEST_N AND | A & nn | t80n_mcode.vhd:1779-1788 default ALU_Op=0100 (AND) on Save_ALU | ✓ | IR(5:3)="100" → AND |
| z80n_ext.cpp:164 TEST_N flags | S/Z/P + H=1, X/Y from result, N=0/C=0 | std Z80 AND r,n flags | ✓ | matches FUSE AND_n |
| z80n_ext.cpp:164 TEST_N MEMPTR | unchanged | t80n_mcode.vhd no LDZ/LDW | ✓ |  |
| z80n_ext.cpp:164 TEST_N R-reg | +2 (z80_cpu.cpp:601) | RTI standard: 1 per M1 fetch | ✓ |  |
| z80n_ext.cpp:164 TEST_N T-states | 11 = 8+3 (operand) | t80n_mcode.vhd MCycles="010" TStates="100"+"011" | ✓ |  |
| z80n_ext.cpp:164 TEST_N Q-tracking | regs.Q = f | FUSE convention | ✓ | F-write op sets Q=F |
| z80n_ext.cpp:190 BSLA_DE_B shift | uint32_t shift, mask 0xFFFF | t80n.vhd:987-993 shift_left(unsigned 16-bit, B[4:0]) | ✓ | V17-Z80N-01 fix |
| z80n_ext.cpp:190 BSLA_DE_B flags | none | t80n.vhd no F write | ✓ | Q stays 0 |
| z80n_ext.cpp:209 BSRA_DE_B arith shift | branched UB-free shift | t80n.vhd:1006-1014 shift_right(signed 17-bit) bit 16 = sign | ✓ | V17-CPU-NIT-04 fix |
| z80n_ext.cpp:209 BSRA_DE_B flags | none | t80n.vhd no F write | ✓ |  |
| z80n_ext.cpp:244 BSRL_DE_B logical shift | regs.DE >> shift | t80n.vhd:1006-1014 shift_right with bit 16=IR(0)=0 (BSRL) | ✓ | for shift 0..31 well-defined since regs.DE non-negative in int |
| z80n_ext.cpp:244 BSRL_DE_B flags | none | no F write | ✓ |  |
| z80n_ext.cpp:252 BSRF_DE_B fill-1 shift | branched UB-free | t80n.vhd:1006-1014 bit 16=IR(0)=1 (BSRF) | ✓ | V17-Z80N-01a fix |
| z80n_ext.cpp:252 BSRF_DE_B flags | none | no F write | ✓ |  |
| z80n_ext.cpp:279 BRLC_DE_B rotate | rotate left (B[4:0] mask + mod 16) | t80n.vhd:1022-1028 rotate_left | ✓ |  |
| z80n_ext.cpp:279 BRLC_DE_B flags | none | no F write | ✓ |  |
| z80n_ext.cpp:290 MUL_DE 8x8→16 | (uint16)d * (uint16)e | t80n.vhd:729-735 unsigned 8x8 = 16-bit | ✓ |  |
| z80n_ext.cpp:290 MUL_DE flags | none | no F write | ✓ | Q stays 0 |
| z80n_ext.cpp:299 ADD_HL_A | HL += A 16-bit, F.C cleared | t80n.vhd:778-783 — F.C <= reg_temp_t(16) which is pre-zero | ✓ | Pass-10 fix; F.C unconditionally cleared |
| z80n_ext.cpp:299 ADD_HL_A other flags | preserved | t80n.vhd no other F writes | ✓ |  |
| z80n_ext.cpp:299 ADD_HL_A MEMPTR | unchanged | no LDZ/LDW | ✓ |  |
| z80n_ext.cpp:324 ADD_DE_A | analogous F.C cleared | same as ADD_HL_A | ✓ |  |
| z80n_ext.cpp:337 ADD_BC_A | analogous | same | ✓ |  |
| z80n_ext.cpp:350 ADD_HL_NN | HL += nn read via fuse_z80_readbyte | t80n_mcode.vhd:1872-1878 LDZ/LDW + ADD_HL_nn | ✓ | MEMPTR=nn |
| z80n_ext.cpp:350 ADD_HL_NN MEMPTR | regs.MEMPTR = nn | t80n.vhd:1181-1186 LDZ writes MEMPTR-lo, LDW writes MEMPTR-hi | ✓ | Pass-4 fix |
| z80n_ext.cpp:350 ADD_HL_NN T-states | 16 = 8+6+2 | spec; 2T internal | ✓ |  |
| z80n_ext.cpp:371 ADD_DE_NN | analogous | same | ✓ |  |
| z80n_ext.cpp:387 ADD_BC_NN | analogous | same | ✓ |  |
| z80n_ext.cpp:403 PUSH_NN | push hh,ll big-endian | t80n_mcode.vhd:1928 LDZ at MCycle 1 + 3, LDW never | ✓ | Pass-8 WZ-lo only fix |
| z80n_ext.cpp:403 PUSH_NN MEMPTR | (MEMPTR & 0xFF00) \| ll | LDZ-only | ✓ |  |
| z80n_ext.cpp:403 PUSH_NN T-states | 23 = 8+6+6+3 | spec | ✓ |  |
| z80n_ext.cpp:438 OUTINB | (HL)→port BC, HL++ | t80n_mcode.vhd:2521-2531 (no B-- vs OUTI) | ✓ |  |
| z80n_ext.cpp:438 OUTINB extended-M1 | contend_read_no_mreq(IR,1) | t80n_mcode.vhd:2528-2530 TStates="101" extended M1 | ✓ | V12-CPU-NIT-02 |
| z80n_ext.cpp:438 OUTINB flags | none | I_BTR doesn't trigger flag block for OUTINB (no Save_ALU since IRB=90 path) | ✓ |  |
| z80n_ext.cpp:477 NEXTREG_NN | NR write reg/val | t80n_mcode.vhd:1668-1683 Z80N_data_o + strobes | ✓ | bypasses IORQ |
| z80n_ext.cpp:477 NEXTREG_NN MEMPTR | unchanged | no LDZ/LDW in mcode | ✓ |  |
| z80n_ext.cpp:477 NEXTREG_NN T-states | 20 = 8+6+6 (internal covers fabric) | spec | ✓ |  |
| z80n_ext.cpp:499 NEXTREG_A | NR write reg/A | t80n_mcode.vhd:1690-1707 | ✓ |  |
| z80n_ext.cpp:499 NEXTREG_A MEMPTR | unchanged | no LDZ/LDW | ✓ |  |
| z80n_ext.cpp:515 PIXELDN | composite increment | t80n.vhd:900-921 | ✓ | V11-CPU-02 fix |
| z80n_ext.cpp:566 PIXELAD | "010" + bit-rearrange | t80n.vhd:939-947 | ✓ |  |
| z80n_ext.cpp:578 SETAE | A = 0x80 >> (E&7) | t80n.vhd:923-937 | ✓ |  |
| z80n_ext.cpp:588 JP_C | port read BC → PC[13:6], PC[5:0]=0 | t80n.vhd:980-983 + t80n_mcode.vhd:1837-1848 | ✓ | Pass-8 T=12 (4 ED + 4 M1 + 4 IORQ) |
| z80n_ext.cpp:610 LDIX | block xfer with transparency | t80n_mcode.vhd:2098-2138 + t80n.vhd:1277-1289 I_BT | ✓ | flags via ldi_family_flags |
| z80n_ext.cpp:610 LDIX IncDecZ | latched on BC dec | t80n.vhd:1361-1367 | ✓ | Pass-9 |
| z80n_ext.cpp:610 LDIX transparency contention | contend_write_no_mreq×3 on suppressed write | zxula.vhd:582-600 gates on mreq23_n registered, not wr_n | ✓ | Pass-9 |
| z80n_ext.cpp:662 LDWS | (DE)←(HL); L++; D++ | t80n_mcode.vhd:2141-2181 + t80n.vhd I_BT path | ✓ |  |
| z80n_ext.cpp:662 LDWS flags F.P | from IncDecZ (regs.IncDecZ shadow) | t80n.vhd:1283-1284 I_BT/I_BC override | ✓ | Pass-9 IncDecZ shadow |
| z80n_ext.cpp:730 LDDX | HL--, DE++, transparency | t80n_mcode.vhd:2230-2256 | ✓ |  |
| z80n_ext.cpp:764 LDIRX | repeating LDIX, PC rewind | G89 inter-iter INT shape | ✓ |  |
| z80n_ext.cpp:825 LDDRX | repeating LDDX | same | ✓ |  |
| z80n_ext.cpp:872 LDPIRX | pattern fill HL[15:3]\|DE[2:0] | t80n_mcode.vhd:1953-1991 + t80n.vhd:1119-1134 LDPIRX path | ✓ | Pass-10 I_BT flags + V18R-CPU-NIT-01 MEMPTR-lo strobe (= 0xB7) |
| z80n_ext.cpp:872 LDPIRX MEMPTR-lo | = 0xB7 (LDZ at MCycle 1 captures opcode) | t80n_mcode.vhd:1967 LDZ='1' | ✓ | V18R fix |
| z80n_ext.cpp:960 LDIRSCALE | repeating block + scale (shape only — actual scale commented in VHDL) | t80n_mcode.vhd:2188-2226 | ✓ | I_BT flag composition |
| z80n_ext.cpp:1008 LOOP | NOP-equivalent | not implemented in FPGA | ✓ | spec-faithful (FPGA stub) |
| z80_cpu.cpp:407 execute() NMI dispatch | fuse_z80_nmi() | std Z80 NMI | ✓ |  |
| z80_cpu.cpp:407 execute() INT pulse drop | unconditional on (tstates - int_requested_at_) > pulse_T | zxnext.vhd:2017-2033 pulse_count_end NOT gated on IFF1 | ✓ | V18R-CPU-01 fix |
| z80_cpu.cpp:407 execute() EI-grace gate | skip on_int_ack when interrupts_enabled_at == tstates | FUSE EI semantics | ✓ | Pass-8 fix |
| z80_cpu.cpp:407 execute() Z80N detection | peek ED+ext, dispatch execute_z80n | bypasses FUSE | ✓ |  |
| z80_cpu.cpp:407 execute() prefix walk on_m1_cycle | DD/FD walk + ED/CB inner | im2_control.vhd FSM expects per-byte ifetch_fe_t3 | ✓ | Pass-9 walk |
| z80_cpu.cpp:407 execute() IncDecZ shadow update | DJNZ(F.Z), INC/DEC BC, ED block-xfer, DD/FD-prefix variants | t80n.vhd:1361-1367 | ✓ | V13/V14/NIT-01 fixes |
| z80_cpu.cpp:925 request_interrupt | int_pending_=true; vector latched; tstates stamp | n/a (legacy API) | ✓ |  |
| z80_cpu.cpp:931 request_nmi | nmi_pending_=true | n/a | ✓ |  |
| z80_cpu.cpp:935 save_state schema | regs+MEMPTR+Q+iff2_read+interrupts_enabled_at+int_pending+vector+stamp | persists CPU snapshot | ✓ | Pass-3/4 fixes |
| z80_cpu.cpp:935 save_state IncDecZ | NOT persisted (intentional, comment explains) | n/a | ✓ | acceptable trade-off documented |
| im2.h:14 Im2Level enum | 14 entries; preserved layout | n/a (legacy bridge) | ✓ |  |
| im2.h:33 DevIdx enum | LINE=0..UART1_TX=13 | zxnext.vhd:1941 priority order | ✓ |  |
| im2.h:46 DevState enum | S_0/S_REQ/S_ACK/S_ISR | im2_device.vhd:83 | ✓ |  |
| im2.cpp:100 to_devidx legacy bridge | maps Im2Level→DevIdx; non-IM2 sources go to ULA | scaffold doc | ✓ | non-IM2 callers no longer used (V18R fix) |
| im2.cpp:128 raise(Im2Level) | gates DMA/DIVMMC/MULTIFACE as no-ops | scaffold safety | ✓ | V18R-CPU-02 fix |
| im2.cpp:295 raise_req(DevIdx) | sets dev_[i].int_req=true | per-device level input | ✓ |  |
| im2.cpp:300 clear_req(DevIdx) | clears dev_[i].int_req | per-device | ✓ |  |
| im2.cpp:313 raise_unq(DevIdx) | one-shot int_unq + int_status + im2_int_req | im2_peripheral.vhd:160,172 | ✓ |  |
| im2.cpp:tick() int_unq clearing (V19-IM2-03) | clears all dev_[].int_unq at end of tick | nr_20_we one-cycle pulse, VHDL :1946-1947 | ✓ | **V19-IM2-03 fix this pass** |
| im2.cpp:325 clear_status(DevIdx) | clears int_status only (preserves im2_int_req) | im2_peripheral.vhd:160 vs :175 | ✓ |  |
| im2.cpp:334 int_status(DevIdx) | int_status \|\| im2_int_req | im2_peripheral.vhd:180 | ✓ |  |
| im2.cpp:346 int_status_mask_c8 | bits {LINE,ULA} | zxnext.vhd:6247-6248 (im2_int_status(0)+im2_int_status(11)) | ✓ |  |
| im2.cpp:360 int_status_mask_c9 | bits 7:0 = CTC7..CTC0 | zxnext.vhd:6250-6251 im2_int_status(10:3) | ✓ | CTC4..CTC7 hard-zero per :4092 |
| im2.cpp:383 int_status_mask_ca | UART status pack with duplicated RX bits | zxnext.vhd:6253-6254 | ✓ |  |
| im2.cpp:411 set_int_en(DevIdx) | per-device int_en | i_int_en input | ✓ |  |
| im2.cpp:420 set_int_en_c4 | only writes LINE bit (1) | zxnext.vhd:5607-5610 | ✓ | bit 0 (ULA) intentionally not here |
| im2.cpp:430 set_int_en_c5 | CTC 7..0 from bits 7:0 | zxnext.vhd:5613 ctc_int_en | ✓ |  |
| im2.cpp:451 set_int_en_c6 | UART TX/RX from bits, near-full \| avail OR | zxnext.vhd:5615-5617 + :1950 | ✓ |  |
| im2.cpp:465 set_vector_base | nr_c0_im2_vector[2:0] | zxnext.vhd:5092 | ✓ |  |
| im2.cpp:470 set_mode | nr_c0_int_mode_pulse_0_im2_1 | zxnext.vhd:5094 | ✓ |  |
| im2.cpp:473 set_stackless_nmi | store-only (F-deferred) | zxnext.vhd:5093 + :2052 stackless logic | ✗ | (class-d, known) — only stored; full stackless-NMI logic not implemented |
| im2.cpp:488 set_dma_int_en_mask | fan out to dev_[].dma_int_en (14-bit) | zxnext.vhd:1957-1958 | ✓ |  |
| im2.cpp:498 dma_int_pending | OR over (state≠S_0 AND dma_int_en) | im2_device.vhd:151 + peripherals.vhd OR | ✓ |  |
| im2.cpp:516 dma_delay | latched im2_dma_delay | zxnext.vhd:2001-2010 | ✓ | step_dma_delay() |
| im2.cpp:535 int_line_asserted | true if any S_REQ with iei=1 | im2_device.vhd:150 + peripherals.vhd:146-156 AND | ✓ | semantics correct |
| im2.cpp:535 int_line_asserted ↔ CPU /INT wiring | now polled in run_frame after im2_.tick() (V19-IM2-04) | zxnext.vhd:1840 z80_int_n composition | ✓ | **V19-IM2-04 fix this pass** |
| im2.cpp:551 ack_vector | walks priority chain, S_REQ→S_ACK, returns vector | im2_device.vhd:111-116 + peripherals.vhd:134-144 | ✓ |  |
| im2.cpp:599 on_m1_cycle | drives RETI/RETN/IM decoder | im2_control.vhd:158-209 | ✓ | V11-CPU-01 DDFD-ED fix |
| im2.cpp:626 im_mode | 0/1/2 latch | im2_control.vhd:218-227 | ✓ |  |
| im2.cpp:631 pulse_int_n | pulse fabric output | zxnext.vhd:2017-2031 | ✓ |  |
| im2.cpp:633 set_machine_timing_48_or_p3 | 48K/+3 vs 128K width | zxnext.vhd:2033 | ✓ |  |
| im2.cpp:638 state(DevIdx) debug | returns dev_[i].state | n/a | ✓ |  |
| im2.cpp:642 ieo(DevIdx) debug | returns device_ieo result | im2_device.vhd:136-146 | ✓ |  |
| im2.cpp:668 advance_decoder S_0 | ED/CB/DD/FD branches | im2_control.vhd:161-170 | ✓ |  |
| im2.cpp:681 advance_decoder S_ED_T4 | 4D→S_ED4D_T4 reti_seen pulse, 45→S_ED45_T4, IM mode opcodes | im2_control.vhd:171-180+218-227+233-236 | ✓ |  |
| im2.cpp:711 advance_decoder S_ED4D_T4→S_SRL_T1 | matches VHDL :181-183 | ✓ |  ✓ |  |
| im2.cpp:716 advance_decoder S_ED45_T4→S_SRL_T1 | matches VHDL :190-192 | ✓ |  |  |
| im2.cpp:723 advance_decoder S_SRL_T1→S_SRL_T2 | matches VHDL :186-189 | ✓ |  |  |
| im2.cpp:726 advance_decoder S_SRL_T2→S_0 | matches VHDL :186-189 | ✓ |  |  |
| im2.cpp:733 advance_decoder S_CB_T4→S_0 | matches VHDL :193-198 | ✓ |  |  |
| im2.cpp:755 advance_decoder S_DDFD_T4 | DDFD stays, else→S_0 | im2_control.vhd:199-206 | ✓ | V11-CPU-01 |
| im2.cpp:784 step_devices Phase 1 (V17-CPU-01) | im2_int_req held=0 in pulse mode | im2_peripheral.vhd:105 + :167-178 | ✓ |  |
| im2.cpp:879 step_state_machine_with_iei S_0 | im2_int_req latched → S_REQ | im2_device.vhd:106 | ✓ |  |
| im2.cpp:909 step_state_machine_with_iei S_REQ | stays unless ack_vector advances | im2_device.vhd:111 | ✓ | one-cycle ack model |
| im2.cpp:929 step_state_machine_with_iei S_ACK | advances to S_ISR next tick | im2_device.vhd:117 | ✓ |  |
| im2.cpp:936 step_state_machine_with_iei S_ISR | RETI seen + iei → S_0; clears im2_int_req | im2_device.vhd:123 + im2_peripheral.vhd:175 | ✓ |  |
| im2.cpp:984 step_pulse | pulse_int_n sequencer | zxnext.vhd:2012-2044 | ✓ |  |
| im2.cpp:1070 step_dma_delay | dma_delay latch | zxnext.vhd:2001-2010 | ✓ | Wave E NMI term |
| im2.cpp:1077 compute_vector | (base<<5) \| (idx<<1) | zxnext.vhd:1999 | ✓ |  |
| im2.cpp:1099 device_ieo | iei walk | im2_device.vhd:136-146 | ✓ |  |
| emulator.cpp:1831 NR 0x22 → IM2 LINE int_en | now mirrors v bit 1 to dev_[LINE].int_en (V19-IM2-01) | zxnext.vhd:5297 + :1950 + :6711 | ✓ | **V19-IM2-01 fix** |
| emulator.cpp:1831 NR 0x22 → IM2 ULA int_en | now mirrors port_ff_reg(6) to dev_[ULA].int_en (V19-IM2-02) | zxnext.vhd:3619-3620 + :3635 + :6711 + :1949 | ✓ | **V19-IM2-02 fix** |
| emulator.cpp:2666 NR 0xC4 handler | set_int_en_c4 + line_int + port_ff bit 6 + ula_int_en (V19-IM2-02) | zxnext.vhd:5607-5610 + :3621-3622 + :6711 | ✓ | **V19-IM2-02 fix complete writer set** |
| emulator.cpp:2685 NR 0xC0 write | vector_base + stackless + mode | zxnext.vhd:5092-5094 | ✓ |  |
| emulator.cpp:2691 NR 0xC0 read | vec\<\<5 \| stackless\<\<3 \| im_mode\<\<1 \| mode | zxnext.vhd:6229-6230 | ✓ |  |
| emulator.cpp:2725 NR 0xC5 write | ctc_.set_int_enable + im2_.set_int_en_c5 | zxnext.vhd:4078 + :5613 | ✓ |  |
| emulator.cpp:2782 NR 0xC6 write | im2_.set_int_en_c6 | zxnext.vhd:5615-5617 | ✓ |  |
| emulator.cpp:2810 NR 0xC8 write | clear_status LINE/ULA | zxnext.vhd:1953 | ✓ |  |
| emulator.cpp:2824 NR 0xC9 write | clear_status CTC0..7 | zxnext.vhd:1953 | ✓ |  |
| emulator.cpp:2844 NR 0xCA write | clear_status UART set | zxnext.vhd:1952,1954 | ✓ |  |
| emulator.cpp:2853 NR 0x20 write | raise_unq for line/ula/ctc 0..3 | zxnext.vhd:1946-1947 | ✓ |  |
| emulator.cpp:3262 port-FF write → IM2 ULA int_en | mirrors port_ff_reg(6) (V19-IM2-02) | zxnext.vhd:3614-3616 + :6711 | ✓ | **V19-IM2-02 fix** |
| emulator.cpp:5390 ULA frame INT scheduler | raise_req(ULA); request_interrupt only in pulse mode | zxnext.vhd:1937,1941 + :1840 | ✓ | needs V19-IM2-04 polling |
| emulator.cpp:5640 IM2 polling (V19-IM2-04) | poll int_line_asserted post-tick → request_interrupt(0xFE) | zxnext.vhd:1840 z80_int_n composition | ✓ | **V19-IM2-04 fix** |
| emulator.cpp:6560 LINE INT scheduler | raise_req(LINE); request_interrupt only in pulse mode | zxula_timing.vhd:577 + zxnext.vhd:1840 | ✓ | covered by V19-IM2-04 polling in IM2 mode |
| emulator.cpp:4615 CTC on_interrupt | raise_req(CTCi) | zxnext.vhd:1937,1941 | ✓ | covered by V19-IM2-04 |
| emulator.cpp:4664/4682 UART RX/TX | raise_req(UARTi_*) | zxnext.vhd:1937,1941 | ✓ | covered by V19-IM2-04 |
| emulator.cpp:4595 DMA on_interrupt | no-op | DMA is INT-victim, not source (vhdl:2003-2008) | ✓ | V18R-CPU-02 closure |
| emulator.cpp:716 cpu_.on_int_ack | im2_.ack_vector() | zxnext.vhd:1999 vector compose | ✓ |  |
| emulator.cpp:597 cpu_.on_m1_prefetch | DivMMC automap activation | DivMMC scope (out of CPU/Z80N/IM2) | ~ | Class-d V15-CPU-NIT-02 multi-byte first-byte-only known limitation |
| emulator.cpp:659 cpu_.on_m1_cycle | im2_.on_m1_cycle (RETI/RETN/IM decoder feed) | im2_control.vhd FSM | ✓ | prefix walk delivers all M1 bytes |
| emulator.cpp:706 cpu_.on_nmi_servicing | NR 0xC2/0xC3 latch | zxnext.vhd:2050-2085 | ✓ | G88 |
| emulator.cpp:241 init: V19-IM2-02 ULA int_en init | dev_[ULA].int_en = NOT port_ff_reg(6) at boot | zxnext.vhd:6711 | ✓ | **V19-IM2-02 init fix** |
| emulator.cpp:241 init: V19-IM2-01 LINE int_en init | dev_[LINE].int_en = false (matches NR 0x22 reset 0) | zxnext.vhd:4983 | ✓ | **V19-IM2-01 init fix** |

Total rows: ~120. Source surfaces audited: Z80N opcodes (×30), IM2 transitions (×8), Im2Level/DevIdx (×14+3), IM2 sources (×14), NR register handlers (×11), FUSE hooks (×6), save/load fields (×CPU schema).

## Findings

### V19-IM2-01 — class-(b) LANDED — NR 0x22 bit 1 → IM2 fabric LINE int_en

**VHDL**: zxnext.vhd:5297 — `nr_22_we and nr_22 bit 1 → nr_22_line_interrupt_en` flip-flop. NR 0xC4 bit 1 writes the SAME flip-flop (:5610). The flip-flop feeds `im2_int_en[0]` (= LINE i_int_en, line :1949-1950 + :6711 ula_int_en(1)).

**Pre-fix**: jnext NR 0x22 write handler updated only `video_timing_.set_line_interrupt_enable()` (the line-int generation gate). The IM2 fabric's `dev_[DevIdx::LINE].int_en` stayed at its reset value (false). NR 0xC4 bit 1 already routed to `im2_.set_int_en_c4()` → set_int_en(LINE, ...); only NR 0x22 was missing the mirror. Result in IM2 mode: software enabling LINE interrupts via NR 0x22 ← 0x02 alone would have line-int generated, but the IM2 daisy chain stayed in S_0 because edge-detect + int_en gate failed to latch im2_int_req — /INT was never asserted to the Z80.

**Fix**: in NR 0x22 write handler, call `im2_.set_int_en(DevIdx::LINE, (v & 0x02) != 0);`. Plus init-time honour of zxnext.vhd:4983 reset default 0.

**Test**: ULA-INT-V19-IM2-01 in test/ctc_interrupts/ctc_interrupts_test.cpp.

### V19-IM2-02 — class-(b) LANDED — port_ff_reg(6) → IM2 fabric ULA int_en (3 writers + init)

**VHDL**: zxnext.vhd:6711 — `ula_int_en(0) = NOT port_ff_interrupt_disable` (= NOT port_ff_reg(6)). That bit feeds `im2_int_en[11]` (= ULA i_int_en, line :1949). THREE writers feed port_ff_reg(6):
- port-FF write: full byte (:3614-3616);
- NR 0x22 b2 → port_ff_reg(6) (:3619-3620);
- NR 0xC4 b0 NOT → port_ff_reg(6) (:3621-3622).

**Pre-fix**: jnext maintained `ula_int_disabled_` + `video_timing_.set_interrupt_enable` shadows in all three writers, but `dev_[DevIdx::ULA].int_en` was NEVER updated anywhere. Every FRAME-INT raise_req(ULA) in IM2 mode set int_status but NOT im2_int_req (int_en=0); state stayed S_0; no daisy-chain INT asserted to Z80. Plus reset-time init: dev_[ULA].int_en started false (default), but VHDL has port_ff_reg=0 at reset → ULA int_en should start TRUE.

**Fix**: in all 3 writers + init, call `im2_.set_int_en(DevIdx::ULA, (port_ff_reg_ & 0x40) == 0);`.

**Test**: ULA-INT-V19-IM2-02 (3-step: reset, NR 0x22 disable, NR 0xC4 re-enable) + ULA-INT-V19-IM2-02-PORTFF (direct OUT (0xFF) path).

### V19-IM2-03 — class-(b) LANDED — int_unq one-shot semantic

**VHDL**: zxnext.vhd:1946-1947 — `im2_int_unq[i] <= nr_20_we and nr_wr_dat(N)` where `nr_20_we` is a one-cycle pulse on every NR 0x20 write. So per-device `i_int_unq` is high for EXACTLY one CLK_28 cycle.

**Pre-fix**: jnext set `dev_[i].int_unq=true` in `raise_unq()` and the only clearing path was `step_pulse()` when a pulse-mode pulse terminated. In IM2 mode the pulse fabric never fires for non-exception devices, so int_unq stayed true forever. After isr_serviced cleared `im2_int_req` at S_ISR→S_0, the NEXT tick's wrapper Phase 1 RE-LATCHED im2_int_req from the still-true int_unq → state machine S_0→S_REQ → phantom re-trigger of the same interrupt forever.

**Fix**: clear all `dev_[].int_unq` one-shots at end of `tick()` so the level is consumed within one tick's view, matching VHDL one-cycle semantic. The existing `step_pulse()` clearing on `pulse_count_end` becomes redundant but harmless.

**Test**: V19-IM2-03-INT-UNQ-ONE-SHOT-AFTER-ISR in test/cpu/cpu_z80n_im2_regressions_test.cpp.

### V19-IM2-04 — class-(b) LANDED — int_line_asserted() drives CPU /INT in IM2 mode

**VHDL**: zxnext.vhd:1840 — `z80_int_n <= ((pulse_int_n AND im2_int_n) OR NOT expbus_disable_int) AND ...`. The Z80 /INT pin is the AND of pulse_int_n AND im2_int_n. Either pulled low asserts /INT.

**Pre-fix**: jnext only called `cpu_.request_interrupt(0xFF)` for the legacy pulse-mode path (FRAME / LINE / CTC scheduler callbacks gate on `if (!im2_.is_im2_mode())`). In IM2 mode the comment said `int_line_asserted()` would drive Z80 INT, but no code ever READ it — `int_line_asserted()` had ZERO call sites in production code. Result in IM2 mode: peripherals raised correctly via `raise_req()`; the fabric's wrapper edge-detect latched im2_int_req; the state machine advanced S_0 → S_REQ; `int_line_asserted()` returned true. But the CPU never saw /INT because `Z80Cpu::execute()` only checks `int_pending_` (set by `request_interrupt`). The IM2 priority chain remained latched in S_REQ forever; no RETI ever cleared it; subsequent S_REQ devices got blocked indefinitely by IEI chain.

**Fix**: in `run_frame()` after `im2_.tick()` advances state, poll `im2_.int_line_asserted()` and call `cpu_.request_interrupt(0xFE)` when asserted (in IM2 mode). Vector replaced at IntAck via `on_int_ack` → `ack_vector()`.

**Test**: ULA-INT-V19-IM2-04 in test/ctc_interrupts/ctc_interrupts_test.cpp.

## Pre-existing class-(d) items (not addressed this pass)

- V15-CPU-NIT-01: DD/FD prefix walk for Z80N opcodes (currently the prefix walk only feeds the IM2 decoder; Z80N opcodes are detected only at unprefixed ED).
- V15-CPU-NIT-02: `on_m1_prefetch` first-byte-only (DivMMC automap may need every M1 byte).
- DivMMC SPI cycle FSM (G137) — out of CPU/Z80N/IM2 scope.
- IM2 stackless NMI — only stored, behaviour deferred.

## Test results

| Suite | Result |
|---|---|
| `ctest --test-dir build` | 38/38 PASS |
| `./build/test/fuse_z80_test` | 1356/1356 PASS |
| `bash test/00regression/regression.sh` | 33/0/0 |
| `./build/test/cpu_z80n_im2_regressions_test` | 44/0 (43 pre-existing + 1 new V19-IM2-03) |
| `./build/test/ctc_interrupts_test` | 27/0 (23 pre-existing + 4 new V19-IM2-01/02/02-PORTFF/04) |

## Final HEAD

(set after merge; this report committed as the last commit on the verify19 branch)

## Pass-19 vs prior trend

Pass-11..18 trajectory: 8 / 17 / 7 / 9 / 5 / 7 / 8 / (Pass-18 V18R: 2 reviewer findings); Pass-19: 4 effective findings. The sustained rate (≈5–10 per pass) confirms convergence is not yet reached for CPU/IM2; new bugs uncovered this pass were all integration-wiring gaps (NR-register → IM2 fabric routing) that prior passes had not enumerated systematically.

The introduction of the mandatory enumeration table this pass (per Pass-19 prompt update) directly surfaced the 4 V19-IM2-* bugs: rows for "NR 0x22 → IM2 LINE int_en" and "port_FF → IM2 ULA int_en" and "int_line_asserted() ↔ CPU /INT wiring" all returned ✗ before the fix and forced explicit reasoning about every wiring chain.
