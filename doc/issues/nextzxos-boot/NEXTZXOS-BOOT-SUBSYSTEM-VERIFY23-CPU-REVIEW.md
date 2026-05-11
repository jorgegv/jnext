# Task 2 Pass-23 BLIND audit — CPU + Z80N + IM2 — INDEPENDENT REVIEW

**Date:** 2026-05-11
**Reviewer branch:** `task2/verify23-cpu-z80n-im2-reviewer`
**Worktree:** `.claude/worktrees/task2-verify23-cpu-z80n-im2-reviewer`
**Audit HEAD reviewed:** `b059f4b9`
**Audit report:** `doc/issues/nextzxos-boot/NEXTZXOS-BOOT-SUBSYSTEM-VERIFY23-CPU.md`

**Verdict: APPROVE — no missed findings. CPU + Z80N + IM2 OFFICIALLY CONVERGED at Pass-23.**

With Memory (P14), DivMMC (P21), NMI + MF + Port + NextREG (P22), and now CPU + Z80N + IM2 (P23) all converged, **Task 2 boot-critical subsystem audit is COMPLETE.**

---

## Review process

Build: clean Release build (`cmake --build build`) after restoring missing `roms/` symlink in the worktree.

Baseline tests:
- `ctest`: **38/38 PASS**
- FUSE Z80: **1356/1356 PASS** (NON-NEGOTIABLE)
- Regression suite: **33/0/0 PASS**
- `cpu_int_pulse_tests`: **PASS**
- `cpu_z80n_im2_regressions_tests`: **PASS**
- `ctc_tests`: **PASS**
- `ctc_interrupts_tests`: **PASS**

No regressions; no test fail-up appeared at the reviewer HEAD.

---

## Step 1 — Row-count validation

Audit claims 210 rows (156 canonical + 54 continuation appendix).
Independent count via `grep -c "^| [0-9]" ...VERIFY23-CPU.md` → **210**.
Max row index via `awk` → **210**.

**Row count: confirmed at 210.** No truncation; the table covers Z80N opcodes (31), IM2 fabric methods (~50), DevIdx slots × NR handlers, save/load schema, FUSE Z80 integration hooks, HALT/RETI/RETN/block-instr, CTC integration, NMI integration. The surface enumerated is appropriate for a final-pass convergence claim.

---

## Step 2 — 15-row spot check (random across all sections)

| Row | Claim | Cited VHDL | C++ site | Verdict |
|-----|-------|------------|----------|---------|
| 14  | `DevIdx` enum: LINE=0..UART1_TX=13 per priority | `zxnext.vhd:1941` + `peripherals.vhd:82+:105` | `im2.h:33-43` | ✓ Verified: VHDL bit 0 of `im2_int_req` = `line_int_pulse`; peripherals.vhd indexes I=1..14 with `ie(0)<=i_iei='1'` and `i_vector=>to_unsigned(I-1,VEC_BITS)`. C++ DevIdx 0 = LINE maps to VHDL device 1 = vector 0 = highest priority. |
| 22  | NR 0xCA pattern with bit dup on RX | `zxnext.vhd:6253-6254` | `im2.cpp:473-484` | ✓ Verified: VHDL `'0' & status(13) & status(2) & status(2) & '0' & status(12) & status(1) & status(1)` exactly matches C++ `u1tx→bit6, u1rx→bits5+4 (0x30), u0tx→bit2, u0rx→bits1+0 (0x03)`. |
| 53  | NR 0x20 read composition | `zxnext.vhd:5989-5990` | `emulator.cpp:3037-3046` | ✓ Verified: VHDL `status(0) & status(11) & "00" & status(6 downto 3)` matches C++ packing LINE→b7, ULA→b6, CTC3..0→b3..b0. |
| 64  | step_pulse non-exception path: pulse mode only | `im2_peripheral.vhd:186` | `im2.cpp:1136-1140` | ✓ Verified: VHDL `o_pulse_en <= ((int_req and i_int_en) or i_int_unq) and not i_mode_pulse_0_im2_1` matches C++ `if (!im2_mode_) pulse_en=true`. |
| 65  | step_pulse exception (ULA) path | `im2_peripheral.vhd:192` | `im2.cpp:1129-1134` | ✓ Verified: VHDL `... and ((i_mode_pulse_0_im2_1 and not i_im2_mode) or (not i_mode_pulse_0_im2_1))` matches C++ `if (!im2_mode_ \|\| im_mode_!=2)`. |
| 73  | im2_int_req latch held=0 in pulse mode | `im2_peripheral.vhd:170-171` | `im2.cpp:940-942` | ✓ Verified: VHDL `if im2_reset_n='0' then im2_int_req<='0'` with `im2_reset_n <= i_mode_pulse_0_im2_1 and not i_reset` matches C++ `const bool im2_reset_n=im2_mode_; if(!im2_reset_n) d.im2_int_req=false`. V17-CPU-01 closure correct. |
| 83  | S_ISR→S_0 gated on reti_seen & iei & im_mode_==2 | `im2_device.vhd:123-128` | `im2.cpp:1058-1083` | ✓ Verified: VHDL `if i_reti_seen='1' and i_iei='1' and i_im2_mode='1' then state_next<=S_0` matches C++ `if(reti_seen_pulse_ && iei && im_mode_==2)`. V21-IM2-01 closure correct. |
| 84  | S_ISR→S_0 clears im2_int_req inline | `im2_peripheral.vhd:175` | `im2.cpp:1081` | ✓ Verified: VHDL `im2_int_req <= im2_int_req and not im2_isr_serviced` with `o_isr_serviced='1' when state=S_ISR and state_next=S_0` matches C++ `d.im2_int_req=false` inline at S_ISR→S_0. V22-IM2-01 closure correct. |
| 97  | compute_vector composes vector | `zxnext.vhd:1999` | `im2.cpp:1212-1232` | ✓ Verified: VHDL `nr_c0_im2_vector & im2_vec & '0'` matches C++ `((vec_base&7)<<5) \| (idx<<1)`. |
| 119 | save_state field coverage | n/a | `im2.cpp:1282-1320` | ✓ Verified: per-device int_req/int_req_d/int_en/int_unq/int_status/im2_int_req/state/dma_int_en/exception; decoder dec_state_/reti_seen_pulse_/retn_seen_pulse_/reti_decode_/dma_delay_ctrl_/im_mode_; pulse pulse_int_n_/pulse_count_/machine_48_or_p3_; NR 0xC0 vector_base_msb3_/im2_mode_/stackless_nmi_; DMA dma_int_en_mask14_/im2_dma_delay_latched_/nmi_activated_/nr_cc_dma_int_en_0_7_; ACK last_acked_; legacy_mask_. Full coverage; no missing field. |
| 124 | IM2 mode INT poll | `zxnext.vhd:1840` | `emulator.cpp:5897-5899` | ✓ Verified: V19-IM2-04 — `if (im2_.is_im2_mode() && im2_.int_line_asserted()) cpu_.request_interrupt(0xFE)`. int_line_asserted internally gates on `im_mode_==2` (V21-IM2-01) so the composite poll is VHDL-faithful. |
| 125 | Pulse mode INT poll falling-edge | `zxnext.vhd:2017-2031` | `emulator.cpp:5965-5970` | ✓ Verified: V20-IM2-01 — `if (!im2_.is_im2_mode() && !cur_pulse_int_n && prev_pulse_int_n_) cpu_.request_interrupt(0xFF)`. prev_pulse_int_n_ shadow updated unconditionally after the check. |
| 130 | INT drop arm IFF1-independent | `zxnext.vhd:2017-2033` | `z80_cpu.cpp:467-470` | ✓ Verified: `if (tstates - int_requested_at_ > int_pulse_tstates) int_pending_=false` runs BEFORE the `iff1` gate at line 471. V18R-CPU-01 closure correct. |
| 147 | BSLA_DE_B 32-bit unsigned shift | `t80n.vhd:987-998` | `z80n_ext.cpp:190-207` | ✓ Verified: C++ uses `uint32_t v = static_cast<uint32_t>(regs.DE) << shift` with shift up to 31 — well-defined for unsigned 32-bit; result masked to 16 bits gives 0 for shift≥16 (matches VHDL `numeric_std.shift_left` truncated to 16 bits). V17-Z80N-01a closure correct. |
| 186 | CTC unconditional `on_interrupt` (IM2 gates at wrapper) | `zxnext.vhd:1941` + `ctc.cpp:288-291` | `emulator.cpp:4829-4837` | ✓ Verified: handle_zc_to fires `on_interrupt(channel)` regardless of channel int_enabled; IM2 fabric's int_en gate at wrapper layer (im2.cpp:944) correctly handles the AND. G119 closure correct. |

**15/15 spot-checks verified VHDL-faithful. No misreads detected.**

---

## Step 3 — 9-claim verification

| # | Claim | Verification | Verdict |
|---|-------|--------------|---------|
| 1 | on_reti vs step_state_machine_with_iei symmetry | Read both functions (im2.cpp:261-352 + 1058-1083). Both gate on `im2_mode_`+`im_mode_==2`, both use `iei_reti_decode = reti_decode_ \|\| reti_seen_pulse_`, both snapshot IEI pre-transition with same walker, both clear `dev_[i].state=S_0` AND `dev_[i].im2_int_req=false` at the S_ISR→S_0 transition. State variables touched: identical. | ✓ Symmetric |
| 2 | Save/load schema completeness | Read Im2Controller::save_state (im2.cpp:1282-1320) and load_state (1322-1357) — 14 devices × 9 fields + decoder × 6 + pulse × 3 + NR 0xC0 × 3 + DMA × 4 + ACK × 1 + legacy × 1 = 122+ fields. Read inverse correctness — every write_X has matching read_X in identical order. Emulator save/load (emulator.cpp:7188+7435) appends prev_pulse_int_n_ with EOF-tolerance. | ✓ Complete |
| 3 | CPU /INT polling symmetric | Read emulator.cpp:5897-5899 (IM2 poll: is_im2_mode AND int_line_asserted) and :5965-5970 (pulse poll: !is_im2_mode AND falling-edge on pulse_int_n). Falling-edge state shadow (prev_pulse_int_n_) maintained per-tick. Both paths drive request_interrupt with their respective vectors. | ✓ Symmetric |
| 4 | Im2Level → DevIdx mappings (14 slots) | Read im2.cpp:163-184 (to_devidx). LINE_IRQ→LINE, FRAME_IRQ→ULA, CTC_0..CTC_3→CTC0..3, UART_RX_0..1→UART0/1_RX, UART_TX_0..1→UART0/1_TX, ULA_EXTRA→ULA (alias), DMA/DIVMMC/MULTIFACE→ULA placeholder (early-return guards). All per VHDL :1941-1944 priority. V18R-CPU-02 raise() early-return guards prevent collision with DevIdx::CTC7/ULA/UART1_TX. | ✓ Clean |
| 5 | VHDL `we`-strobe vs commented vestiges | Grepped active write strobes (`nr_c[0-9a-f]_we`) in `zxnext.vhd`: nr_20/22/c2/c3/c4/c5/c8/c9/ca all asserted at :4846-4900. NR 0xC0/0xC4/0xC6/0xCC/0xCD/0xCE storage at :5596-5637 alongside commented-out duplicates (:5601-5605 for C2/C3, :5612-5613 for C5, etc.). Audit row 56 correctly identifies NR 0xC2/C3 as NMI scope (out of CPU+Z80N+IM2). Audit handles the layered active+vestige structure correctly — no P22 NMP-style misread. | ✓ No misread |
| 6 | 31 Z80N opcodes re-audit | Spot-checked: SWAPNIB (t80n.vhd:702), MIRROR_A (:706), PIXELDN (:900-921), PIXELAD (:939+), SETAE (:925-938), BSLA (:987-998), BSRA/BSRL/BSRF (:1001-1014), BRLC (:1022+). C++ implementations match VHDL bit-extractions. UB-free shifts verified for BSLA (uint32 + mask), BSRA (sign-fill branching), BSRF (1-fill branching), BSRL (promoted-int safe). | ✓ Clean |
| 7 | HALT+INT resume | Read z80_cpu.cpp:416-528. NMI: bumps PC+1 if halted before pushing (G88). INT: fuse_z80_interrupt(vector) is called from line 513; FUSE clears z80.halted on accept (standard semantics). sync_regs_from_fuse runs after acceptance. Save/load persists `regs_.halted` at z80_cpu.cpp:950+1002. | ✓ Correct |
| 8 | CTC integration | Read ctc.cpp:263-303 (handle_zc_to): fires `on_zc_to` (joy pin-7) and `on_interrupt` UNCONDITIONALLY (G119), then ring-cascades via channel+1. emulator.cpp:4829-4837 binds on_interrupt to `im2_.raise_req(CTC0+ch)`. NR 0xC5 keeps ctc_.set_int_enable AND im2_.set_int_en_c5 synced. Daisy-chain ring matches VHDL :4084. | ✓ Correct |
| 9 | NMI vs IM2 interactions | Read z80_cpu.cpp:416-434 (NMI precedence). NMI services BEFORE INT check. NMI servicing latches saved_pc via on_nmi_servicing for NR 0xC2/C3 (G88). im2_int_req latch unaffected by NMI servicing — VHDL behavior. NMI source's `is_activated()` pushed to im2_ at emulator.cpp:5861 every tick for VHDL:2007's second OR term. | ✓ Correct |

**All 9 key claims verified independently. No discrepancies.**

---

## Step 4 — Adjacent missed findings sweep

Probed for cross-interaction bugs between Pass-19..22 fixes:

- **V19R-CPU-01 (int_req end-of-tick clear) + V22-IM2-01 (on_reti latch clear)**: int_req auto-clear runs at end of tick(); on_reti latch clear runs synchronously when called from the on_m1_cycle lambda. Order of operations: cpu_.execute → im2_.tick (which calls clear at end). on_reti is called during cpu_.on_m1_cycle inside cpu_.execute(). So latch clear happens BEFORE int_req auto-clear. No race. ✓
- **V20R-CPU-NIT-02 (single-stamp per pulse) + V18R-CPU-01 (drop arm IFF1-independent)**: pulse poll re-stamps int_requested_at_ only on falling edge; drop arm clears after 32/36 tstates if no re-stamp. Combined: pulse INT fires exactly once per pulse, expires correctly even if EI/RETI happens just-too-late. ✓
- **Z80N block-instr resume (G89) + INT sampling at instruction boundary**: PC rewound to ED prefix; next cpu_.execute() samples INT (line 452 BEFORE opcode fetch at 530). VHDL inter-iteration M1 boundary preserved. ✓
- **NMI within IM2 cycle**: if NMI fires while device in S_REQ, IntAck never happens; device stays in S_REQ. Next post-NMI instruction re-attempts. VHDL: NMI is separate handler; device state machines run independently. ✓

No adjacent-bug interactions found.

---

## Step 5 — Cross-cutting families re-audit

- **Cache-leak / multi-writer fan-out**: ULA int_en has 3 writers (port_FF bit 6 NOT, NR 0x22 bit 2 NOT-via-port_ff_reg, NR 0xC4 bit 0 NOT-via-port_ff_reg). LINE int_en has 2 writers (NR 0x22 bit 1, NR 0xC4 bit 1). All synchronized into `im2_.set_int_en(DevIdx::*, …)`. ✓
- **WO-NR readback**: NR 0xC0/0xC4/0xC5/0xC6/0xC8/0xC9/0xCA/0xCC/0xCD/0xCE all have explicit read_handler matching VHDL composition. ✓
- **Default-FF / IFF semantics**: not applicable here (Z80 standard NMOS/CMOS IFF1/IFF2 quirks handled inside FUSE). ✓
- **IncDecZ shadow polarity**: rows 137-139 confirm V13-CPU-01 + V14-CPU-01 + Pass-9 fixes. Shadow re-pushed by sync_fuse_from_regs at every execute() prologue. ✓
- **Level-vs-pulse antipatterns**: scanned all `raise_req` / `raise_unq` / `int_req=true/false` sites. int_req level set by raise_req is auto-cleared at end of tick (V19R-CPU-01 — synthesizes VHDL pulse semantic). int_unq one-shot cleared at end of tick (V19-IM2-03). No remaining level-vs-pulse mismatches. ✓
- **load_state shadow re-push**: prev_pulse_int_n_ persisted (V20R-CPU-NIT-01); machine_48_or_p3 re-derived from loaded NR 0x03; ULA int_en re-derived from loaded port_ff_reg (port_ff handler re-runs at NR 0x22/0xC4 write). ✓

No cross-cutting issues missed.

---

## Final convergence determination

**CPU + Z80N + IM2 subsystem CONVERGED at Pass-23.**

Combined with prior convergences:
- **Memory** (P14)
- **DivMMC + SD + SPI** (P21)
- **NMI + Multiface + Port + NextREG** (P22)
- **CPU + Z80N + IM2** (P23) ← this pass

**All 4 boot-critical subsystems are now officially converged. Task 2 audit COMPLETE.**

The 9 class-(d) architectural items (per Pass-17/22 handovers) remain pending user authorization but are outside the scope of class-(a)/(b)/(c) bug-iteration. They represent VHDL-faithful re-architecturing rather than emulator-correctness gaps.

---

## High-confidence statement

I have reviewed the Pass-23 audit report at line-level detail. I:
- counted the 210 enumeration rows;
- spot-checked 15 rows across all sections against VHDL+C++ sources directly;
- independently verified each of the 9 key claims;
- swept for adjacent missed findings (cross-interactions between Pass-19..22 fixes);
- re-audited cross-cutting families (cache-leak, multi-writer, WO-NR, IncDecZ, level-vs-pulse, save/load shadow);
- ran the full test suite (ctest 38/38, FUSE 1356/1356, regression 33/0/0, targeted cpu_int_pulse/cpu_z80n_im2/ctc).

**Confidence: HIGH.** No findings missed by the audit. The convergence claim is sound. CPU + Z80N + IM2 honestly silicon-faithful at the level audited. Task 2 honestly complete.
