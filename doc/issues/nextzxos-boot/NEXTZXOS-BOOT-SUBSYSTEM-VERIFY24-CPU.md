# NEXTZXOS Boot Subsystem — Pass-24 CONVERGENCE PRESSURE TEST — CPU + Z80N + IM2

**Pass:** 24 (CONVERGENCE PRESSURE TEST)
**Subsystems:** CPU + Z80N + IM2 (combined)
**Branch:** `task2/verify24-cpu-z80n-im2`
**Integration HEAD before audit:** `d8647df`
**Worktree:** `/home/jorgegv/src/spectrum/jnext/.claude/worktrees/task2-verify24-cpu-z80n-im2`
**Authoritative oracle:** `/home/jorgegv/src/spectrum/ZX_Spectrum_Next_FPGA/cores/zxnext/src/` (VHDL)
**Audited under-test files:**

- `src/cpu/z80_cpu.h`, `src/cpu/z80_cpu.cpp` (Z80 wrapper around FUSE core)
- `src/cpu/z80n_ext.h`, `src/cpu/z80n_ext.cpp` (Z80N opcode extensions)
- `src/cpu/im2.h`, `src/cpu/im2.cpp` (IM2 fabric controller — devices/peripheral/control)
- `src/cpu/im2_client.h` (IM2 client helper)
- IM2/CPU/Z80N wiring in `src/core/emulator.cpp` (on_m1_cycle / on_int_ack / on_nmi_servicing
  callbacks, pulse-mode + IM2-mode /INT polls, NR-side fan-out)

**Prior pass status (entering Pass-24):**

- Pass-23 CONVERGENCE: 210-row table, 0 findings, reviewer APPROVE-no-missed.
- Pass-22 (NMP+CPU): 0 CPU findings (CPU 209-row table).
- Pass-21 (CPU): V21-IM2-01 (S_REQ→S_ACK + S_ISR→S_0 + int_line_asserted gate on z80_im_mode(1)).
- Pass-20 (CPU): V20-IM2-01 (pulse-mode CPU /INT poll), V20R-CPU-NIT-01/02 (poll-stamp dedup).
- Pass-19 (CPU): V19-IM2-01..04 (im2 fabric refactor), V19R-CPU-01 (1-cycle-pulse int_req).
- Pass-22 (NMP): V22-IM2-01 (on_reti() must clear im2_int_req latch).

The Pass-24 directive emphasised cross-interaction across the V19-V23 IM2 architecture
refactor: on_reti / step_state_machine_with_iei symmetry, save/load schema completeness,
/INT polling paths (IM2 + pulse), Im2Level/DevIdx mapping, Z80N opcode stability.

This pass is a fresh blind re-audit; the underlying source has been read **without**
consulting the prior verify reports.

---

## Audit Workflow

1. Read all four CPU/Z80N/IM2 source files (`z80_cpu.{h,cpp}`, `z80n_ext.{h,cpp}`,
   `im2.{h,cpp}`, `im2_client.h`).
2. Read VHDL oracle: `t80n.vhd` (lines 985-1034 for BSLA/BSRA/BSRL/BSRF/BRLC,
   1163-1289 for ALU + I_BT block, 1358-1367 for IncDecZ latch), `t80n_mcode.vhd`
   (LDWS @ 2141, LDPIRX @ 1954, LDIRX @ 2095, LDIRSCALE @ 2188, LDDX/LDDRX @ 2231,
   OUTINB @ 2519), `im2_control.vhd` (158-238 FSM), `im2_device.vhd` (102-160
   state machine + IEO + o_vec + isr_serviced), `im2_peripheral.vhd` (88-196
   edge detect + im2_int_req latch + pulse_en + int_status), `zxnext.vhd`
   (1837-1840 z80_int_n composition, 1941-1958 im2_int_req/unq/en/dma_int_en/
   status_clear concat order, 1974-1996 peripherals instance, 1999 im2_vector
   composition, 2001-2044 im2_dma_delay + pulse_int_n + pulse_count, 2050-2086
   stackless NMI, 5092-5094 NR 0xC0 reset, 5597 NR 0xC0 write bit-3 fan-out).
3. Build a 220-row enumeration table covering every CPU/Z80N/IM2 mechanism.
4. Re-verify with discriminative spot checks against the cited VHDL lines —
   specifically the Pass-19..23 refactor and reviewer NIT fixes.
5. Run all required test invariants (FUSE 1356, ctest 38, regression 33/0/0).

Blindness rule: prior `VERIFY*-CPU.md` reports were not opened.

---

## Enumeration Table (220 rows)

Conventions:

- **Mech.**: short label describing what is verified.
- **jnext site**: file + line range (or function name) where jnext implements it.
- **VHDL oracle**: VHDL file + line(s).
- **Verdict**: `OK` = matches; `OK*` = matches with a documented intentional model
  choice (collapsed timing edge / scaffold-known-no-op / etc.); `FINDING-*` = bug.

| # | Mech. | jnext site | VHDL oracle | Verdict |
|---|-------|-----------|-------------|---------|
| 1 | Z80 register snapshot struct (`Z80Registers`) AF/BC/DE/HL + AF'/BC'/DE'/HL' + IX/IY/SP/PC | z80_cpu.h:5-23 | t80n.vhd RegsH/RegsL | OK |
| 2 | Hidden MEMPTR / WZ in `Z80Registers` | z80_cpu.h:9 | t80n.vhd (TmpAddr internal) | OK |
| 3 | I + R registers in `Z80Registers` | z80_cpu.h:10 | t80n.vhd (I + R internal) | OK |
| 4 | IFF1 + IFF2 + IM mode bytes | z80_cpu.h:11 | t80n.vhd (IFF1/2 + im_mode) | OK |
| 5 | `Q` shadow for SCF/CCF X/Y composition (FUSE convention) | z80_cpu.h:12 | FUSE Z80 core | OK |
| 6 | `halted` flag in `Z80Registers` | z80_cpu.h:13 | t80n.vhd Halt | OK |
| 7 | `IncDecZ` shadow latch (Pass-9 addition) | z80_cpu.h:14-22 | t80n.vhd:200, 1358-1367 | OK |
| 8 | `MemoryInterface` virtual read/write | z80_cpu.h:25-30 | n/a (host bridge) | OK |
| 9 | `IoInterface` virtual in/out | z80_cpu.h:32-37 | n/a (host bridge) | OK |
| 10 | `fuse_z80_tstates_ptr()` extern accessor | z80_cpu.h:40 | n/a (FUSE bridge) | OK |
| 11 | `request_interrupt(vector)` legacy entry point | z80_cpu.h:74, .cpp:925-932 | zxnext.vhd:1840 z80_int_n | OK |
| 12 | `request_nmi()` legacy entry point | z80_cpu.h:75, .cpp:934-936 | zxnext.vhd:2087-2110 | OK |
| 13 | `set_machine_timing_48_or_p3()` setter for /INT pulse window gate | z80_cpu.h:88-89 | zxnext.vhd:2033 pulse_count_end | OK |
| 14 | `on_contention` callback | z80_cpu.h:98 | zxula.vhd:582-600 | OK |
| 15 | `on_m1_prefetch` callback (DivMMC automap pre-fetch) | z80_cpu.h:101-102 | divmmc.vhd combinational | OK |
| 16 | `on_m1_cycle(pc, opcode)` post-M1 callback | z80_cpu.h:104-105 | im2_control.vhd ifetch_fe_t3 | OK |
| 17 | `on_int_ack()` IntAck vector callback | z80_cpu.h:107-117 | zxnext.vhd:1871, im2_device.vhd:155 | OK |
| 18 | `on_magic_breakpoint` callback (ED FF / DD 01) | z80_cpu.h:119-121 | n/a (debug) | OK |
| 19 | `on_nmi_servicing` callback (saved-PC capture, G88) | z80_cpu.h:123-129 | zxnext.vhd:2050-2085 + 6232-6236 | OK |
| 20 | `request_interrupt_count()` test observable (V20R-CPU-NIT-02) | z80_cpu.h:140-141 | n/a (test instrumentation) | OK |
| 21 | Default `machine_48_or_p3_ = true` so naked FUSE Z80 tests preserve 1356/1356 | z80_cpu.h:160 | n/a (default-true policy) | OK |
| 22 | FUSE singleton state — single CPU instance assumption | z80_cpu.cpp:11-12 | n/a | OK |
| 23 | Z80N opcode dispatch table init (`init_z80n_table`) | z80_cpu.cpp:280-316 | t80n_mcode.vhd Z80N opcodes | OK |
| 24 | Raw memory read (no timing, for opcode peek) | z80_cpu.cpp:123-125 | n/a | OK |
| 25 | Data memory read (contention + 3T) | z80_cpu.cpp:127-141 | zxula.vhd:582-600 + 3T data | OK |
| 26 | Data memory write (contention + 3T) | z80_cpu.cpp:143-157 | zxula.vhd:582-600 + 3T data | OK |
| 27 | Port read: 1T pre-IORQ + contention + 3T post-IORQ | z80_cpu.cpp:162-182 | zxula.vhd port_contend timing | OK |
| 28 | Port write: 1T pre-IORQ + contention + 3T post-IORQ | z80_cpu.cpp:184-196 | zxula.vhd port_contend timing | OK |
| 29 | `contend_read` CORETEST override (G141) | z80_cpu.cpp:240-250 | zxula.vhd:582-600 | OK |
| 30 | `contend_read_no_mreq` CORETEST override (G141) | z80_cpu.cpp:252-262 | zxula.vhd:582-600 | OK |
| 31 | `contend_write_no_mreq` CORETEST override (G141) | z80_cpu.cpp:264-274 | zxula.vhd:582-600 | OK |
| 32 | `mem_active_page_for(address)` derives from `Mmu::get_page(slot)` (LIVE NR$50..$57 mirror) | z80_cpu.cpp:111-115 | zxnext.vhd:2949-2956 + 4489 | OK |
| 33 | Register sync IN: `sync_regs_from_fuse` mirrors FUSE z80 struct → `Z80Registers` | z80_cpu.cpp:320-341 | n/a | OK |
| 34 | Register sync OUT: `sync_fuse_from_regs` writes back to FUSE z80 struct | z80_cpu.cpp:343-365 | n/a | OK |
| 35 | R-register split: low-7 stored in z80.r, hi-bit in z80.r7 | z80_cpu.cpp:334, 357-358 | t80n.vhd:R refresh | OK |
| 36 | `reset()` clears NMI/INT pending, IncDecZ=0 | z80_cpu.cpp:387-405 | t80n.vhd RegSet on reset | OK |
| 37 | `execute()` syncs s_mem/s_io ptrs before each op | z80_cpu.cpp:407-411 | n/a (callback wiring) | OK |
| 38 | NMI dispatch: pre-compute saved_pc for `on_nmi_servicing` (HALT +1 fix) | z80_cpu.cpp:415-434 | zxnext.vhd:2050-2085, fuse halt | OK |
| 39 | `int_pulse_tstates` = 32 (48K/+3) or 36 (128K/Pentagon/Next) | z80_cpu.cpp:451 | zxnext.vhd:2033 pulse_count_end | OK |
| 40 | `if (tstates - int_requested_at_ > int_pulse_tstates) int_pending_=false;` — VHDL-faithful unconditional drop | z80_cpu.cpp:452-470 | zxnext.vhd:2017-2033 pulse_int_n | OK (V18R-CPU-01) |
| 41 | EI-grace check `tstates == interrupts_enabled_at` BEFORE on_int_ack | z80_cpu.cpp:494-497 | t80n.vhd EI semantics | OK (Pass-8) |
| 42 | Vector source: `on_int_ack()` when installed, else `int_vector_` (legacy) | z80_cpu.cpp:510 | zxnext.vhd:1871 im2_vector / floating bus | OK |
| 43 | `fuse_z80_interrupt(vector)` invocation + accepted/reject handling | z80_cpu.cpp:512-518 | t80n.vhd IntE acceptance | OK |
| 44 | Z80N detection: ED + table lookup at PC+1 | z80_cpu.cpp:539-552 | t80n_mcode.vhd Z80N opcode block | OK |
| 45 | ED FF (magic breakpoint) handled before Z80N dispatch | z80_cpu.cpp:542-550 | n/a (debug) | OK |
| 46 | Z80N: `on_m1_cycle` fired on BOTH ED and ext bytes (G87) | z80_cpu.cpp:559-560 | im2_control.vhd:158-209 per-fetch | OK |
| 47 | Z80N: M1 contention via `contend_read(pc, 4)` and `contend_read(pc+1, 4)` (Pass-5) | z80_cpu.cpp:594-596 | zxula.vhd:582-600 + 4T M1 | OK |
| 48 | Z80N: PC advance by 2 (past ED + ext) | z80_cpu.cpp:599 | t80n.vhd PC sequencer | OK |
| 49 | Z80N: R register += 2 (ED + ext M1 fetches) | z80_cpu.cpp:601 | t80n.vhd R refresh per M1 | OK |
| 50 | Z80N: `z80.q = 0` pre-dispatch (FUSE Q hygiene, Pass-4) | z80_cpu.cpp:634 | FUSE Z80 core convention | OK |
| 51 | Z80N: `z80.iff2_read = 0` pre-dispatch (NMOS LD A,I/R quirk hygiene) | z80_cpu.cpp:635 | t80n.vhd LD A,I race | OK |
| 52 | Z80N: full return delta `(tstates - start_ts)` including contention stretch | z80_cpu.cpp:668 | t80n_mcode.vhd Z80N MCycles+TStates | OK |
| 53 | Non-Z80N ED: M1 callbacks for ED + ext byte (G87) | z80_cpu.cpp:680-681 | im2_control.vhd:158-209 | OK |
| 54 | Non-Z80N ED: `IncDecZ` shadow latch on `ED A0/A8/B0/B8/A1/A9/B1/B9` | z80_cpu.cpp:690-693 | t80n.vhd:1361-1366 (BC dec) | OK |
| 55 | DD 01 magic breakpoint (CSpect convention) | z80_cpu.cpp:698-708 | n/a (debug) | OK |
| 56 | Normal Z80: walk DD/FD prefix chain delivering M1 callback for EVERY byte (Pass-9) | z80_cpu.cpp:744-779 | im2_control.vhd:158-209 per-fetch | OK |
| 57 | DD/FD chain: ED inner inside DD/FD chain delivers ED + ext M1 events | z80_cpu.cpp:760-767 | im2_control.vhd S_DDFD_T4 → S_0 path | OK (V11-CPU-01) |
| 58 | DD/FD chain: CB inside DD/FD chain — only CB itself is M1 (displacement + op are data) | z80_cpu.cpp:768-770 | z80_ddfd.c case 0xcb | OK |
| 59 | Plain CB <op>: inner byte is M1 | z80_cpu.cpp:774-778 | t80n.vhd CB sequencer | OK |
| 60 | Inner-opcode walk for IncDecZ classification (V14-CPU-NIT-01) | z80_cpu.cpp:836-857 | t80n.vhd:513-531 + 1361-1366 | OK |
| 61 | `is_djnz` keyed on inner opcode 0x10 (DD/FD-prefixed DJNZ supported) | z80_cpu.cpp:858 | t80n.vhd:1358-1360 DJNZ latch | OK (V14-CPU-NIT-01) |
| 62 | `is_inc_bc` keyed on inner opcode 0x03 | z80_cpu.cpp:859 | t80n.vhd:1361-1366, DPair="00" | OK |
| 63 | `is_dec_bc` keyed on inner opcode 0x0B | z80_cpu.cpp:860 | t80n.vhd:1361-1366, DPair="00" | OK |
| 64 | DJNZ: `IncDecZ = ((B_post>>8) == 0) ? 1 : 0` (NOT `(B != 0)?1:0`) | z80_cpu.cpp:866-898 | t80n.vhd:1358-1360, F_Out(Flag_Z) | OK (V13-CPU-01) |
| 65 | ED block xfer (via DD/FD inner walk): `IncDecZ = (BC != 0) ? 1 : 0` | z80_cpu.cpp:899-905 | t80n.vhd:1361-1366 | OK |
| 66 | INC BC / DEC BC (plain + DD/FD-prefixed): `IncDecZ = (BC != 0) ? 1 : 0` | z80_cpu.cpp:906-921 | t80n.vhd:1361-1366 | OK (V14-CPU-01) |
| 67 | Cycles fallback: `(cycles > 0) ? cycles : 4` on FUSE return | z80_cpu.cpp:694 + 922 | t80n.vhd default M-cycle | OK |
| 68 | `request_interrupt()` increments `request_interrupt_count_` counter | z80_cpu.cpp:931 | n/a (V20R-CPU-NIT-02 test observable) | OK |
| 69 | `request_interrupt()` stamps `int_requested_at_ = tstates` (re-stamp idempotent) | z80_cpu.cpp:928 | zxnext.vhd:2017-2033 pulse fabric | OK |
| 70 | `save_state` writes regs (AF..PC, I, R, IFF1/2, IM, halted) | z80_cpu.cpp:938-950 | n/a (snapshot schema) | OK |
| 71 | `save_state` writes MEMPTR + Q (Pass-3 addition) | z80_cpu.cpp:957-958 | FUSE/VHDL undoc X/Y composition | OK |
| 72 | `save_state` writes `z80.interrupts_enabled_at` (Pass-4) | z80_cpu.cpp:982 | t80n.vhd EI grace | OK |
| 73 | `save_state` writes `z80.iff2_read` (Pass-4, NMOS LD A,I/R quirk) | z80_cpu.cpp:983 | t80n.vhd LD A,I race | OK |
| 74 | `save_state` writes nmi_pending/int_pending/int_vector/int_requested_at | z80_cpu.cpp:985-988 | n/a (snapshot schema) | OK |
| 75 | `load_state` round-trip symmetric with save_state | z80_cpu.cpp:991-1017 | n/a | OK |
| 76 | `load_state` does NOT re-sync FUSE struct (deferred to next execute()) | z80_cpu.cpp:1015-1016 | n/a (perf optimization) | OK |
| 77 | `z80_set_contention_runtime` wires `s_contention` + `s_contention_mmu` + machine_timing | z80_cpu.cpp:1033-1040 | zxula.vhd contention input ports | OK |
| 78 | IncDecZ NOT persisted in save/load (intentional — Pass-9 comment) | z80_cpu.cpp:959-967 | n/a (single-LDWS-LSB worst case) | OK* |
| 79 | Im2Level legacy enum size + numeric layout preserved | im2.h:14-19 | n/a (legacy compat) | OK |
| 80 | DevIdx enum: 14 slots, priority order matches VHDL zxnext.vhd:1941 | im2.h:33-43 | zxnext.vhd:1941 im2_int_req concat | OK |
| 81 | DevIdx priority: LINE=0 (LSB of im2_int_req concat) | im2.h:34 | zxnext.vhd:1941 `& line_int_pulse` | OK |
| 82 | DevIdx: UART0_RX=1, UART1_RX=2 | im2.h:35-36 | zxnext.vhd:1941-1943 | OK |
| 83 | DevIdx: CTC0..7 = 3..10 | im2.h:37-38 | zxnext.vhd:1941 `& ctc_zc_to(7..0)` | OK |
| 84 | DevIdx: ULA=11 (MSB of CTC concat, "EXCEPTION") | im2.h:39 | zxnext.vhd:1941 `& ula_int_pulse` | OK |
| 85 | DevIdx: UART0_TX=12, UART1_TX=13 (top two MSBs) | im2.h:40-41 | zxnext.vhd:1941 head of concat | OK |
| 86 | DevState enum {S_0, S_REQ, S_ACK, S_ISR} matches VHDL | im2.h:46 | im2_device.vhd:83 | OK |
| 87 | Per-device fields: int_req, int_req_d, int_en, int_unq, int_status, im2_int_req, state, dma_int_en, exception | im2.h:163-174 | im2_peripheral.vhd:74-84 + im2_device.vhd state | OK |
| 88 | DecState enum: S_0, S_ED_T4, S_ED4D_T4, S_ED45_T4, S_CB_T4, S_SRL_T1, S_SRL_T2, S_DDFD_T4 | im2.h:180-181 | im2_control.vhd:84 | OK |
| 89 | Constructor calls `reset()` | im2.cpp:16-18 | im2_*.vhd reset paths | OK |
| 90 | `reset()` clears all 14 devices to defaults | im2.cpp:20-23 | im2_device.vhd:94 + im2_peripheral.vhd:92-99 | OK |
| 91 | `reset()` marks ULA (index 11) as exception=true | im2.cpp:24-26 | zxnext.vhd:1964 EXCEPTION="0000100000000000" | OK |
| 92 | `reset()` clears decoder state + im_mode + reti_seen_count/retn_seen_count | im2.cpp:28-35 | im2_control.vhd reset | OK |
| 93 | `reset()` clears pulse fabric (pulse_int_n=true, pulse_count=0) | im2.cpp:37-39 | zxnext.vhd:2021-2022, 2038-2039 | OK |
| 94 | `reset()` clears NR 0xC0 state (vector_base_msb3=0, im2_mode=false, stackless_nmi=false) | im2.cpp:41-43 | zxnext.vhd:5092-5094 reset | OK |
| 95 | `reset()` clears DMA-delay latch + NMI-activated + nr_cc_dma_int_en_0_7 | im2.cpp:45-48 | zxnext.vhd:2004-2005 reset | OK |
| 96 | `reset()` clears last_acked + legacy_mask reset to 0xFFFF | im2.cpp:50-51 | n/a (legacy compat) | OK |
| 97 | `tick()` order: step_pulse → step_devices → step_dma_delay (edge-rules motivated) | im2.cpp:66-79 | im2_peripheral.vhd:90-101 edge timing | OK |
| 98 | `tick()` clears int_unq one-shots at end (V19-IM2-03) | im2.cpp:90 | zxnext.vhd:1946 `nr_20_we` is 1-cycle | OK |
| 99 | `tick()` clears int_req at end (V19R-CPU-01 1-cycle-pulse synthesis) | im2.cpp:141 | im2_peripheral.vhd:90-101 edge detect | OK |
| 100 | `to_devidx()` maps Im2Level to DevIdx | im2.cpp:163-184 | n/a (legacy bridge) | OK |
| 101 | `to_devidx()` maps DMA/DIVMMC/MULTIFACE to ULA (placeholder) | im2.cpp:178-180 | n/a (non-IM2 sources) | OK |
| 102 | `raise(Im2Level)`: DMA/DIVMMC/MULTIFACE are no-ops (V18R-CPU-02) | im2.cpp:191-217 | n/a (not part of IM2 fabric) | OK |
| 103 | `raise(Im2Level)`: routes via to_devidx for IM2 fabric levels | im2.cpp:217-222 | n/a (legacy bridge) | OK |
| 104 | `clear(Im2Level)`: same routing as raise (V18R-CPU-02) | im2.cpp:224-239 | n/a | OK |
| 105 | `has_pending()`: walks dev_[] AND legacy_mask | im2.cpp:241-246 | n/a (legacy API) | OK |
| 106 | `get_vector()`: legacy vector = 2*i | im2.cpp:248-255 | n/a (legacy fallback) | OK |
| 107 | `set_mask(mask)`: stores legacy_mask | im2.cpp:257-259 | n/a (legacy API) | OK |
| 108 | `on_reti()`: early return if NOT im2_mode (V21-IM2-01 + V18R guard) | im2.cpp:261-281 | im2_peripheral.vhd:105 im2_reset_n in pulse mode | OK |
| 109 | `on_reti()`: early return if im_mode != 2 (V21-IM2-01) | im2.cpp:286-289 | im2_device.vhd:124 i_im2_mode gate | OK |
| 110 | `on_reti()`: IEI snapshot uses `reti_decode_ \|\| reti_seen_pulse_` (Pass-10 simultaneity fix) | im2.cpp:312 | im2_control.vhd:233-234 + im2_device.vhd:142 | OK |
| 111 | `on_reti()`: builds IEI chain via {S_0: pass; S_REQ: iei && reti_decode; other: 0} | im2.cpp:313-327 | im2_device.vhd:136-146 IEO | OK |
| 112 | `on_reti()`: clears state for ALL devices in S_ISR with snapshotted iei=1 | im2.cpp:328-351 | im2_device.vhd:123-128 | OK |
| 113 | `on_reti()`: ALSO clears im2_int_req latch inline (V22-IM2-01) | im2.cpp:349 | im2_peripheral.vhd:175 im2_isr_serviced clear | OK |
| 114 | `on_retn()`: documented no-op (retn doesn't affect IM2 device FSM) | im2.cpp:367-369 | divmmc.vhd:108,126,139; im2_device only reads reti_seen | OK |
| 115 | `raise_req(DevIdx)`: sets int_req=true on indexed device | im2.cpp:385-387 | im2_peripheral.vhd i_int_req input | OK |
| 116 | `clear_req(DevIdx)`: sets int_req=false | im2.cpp:390-392 | im2_peripheral.vhd i_int_req level | OK |
| 117 | `raise_unq(DevIdx)`: sets int_unq + int_status + im2_int_req atomically (UNQ-04/05) | im2.cpp:403-408 | im2_peripheral.vhd:160, 172 (bypass int_en) | OK |
| 118 | `clear_status(DevIdx)`: clears ONLY int_status (NOT im2_int_req — per VHDL :175) | im2.cpp:415-418 | im2_peripheral.vhd:160 vs :175 | OK |
| 119 | `int_status(DevIdx)`: composite = int_status OR im2_int_req | im2.cpp:424-427 | im2_peripheral.vhd:180 o_int_status | OK |
| 120 | `int_status_mask_c8`: bit 1 = LINE, bit 0 = ULA | im2.cpp:436-441 | zxnext.vhd:6247-6248 | OK |
| 121 | `int_status_mask_c9`: bits 7..0 = CTC7..CTC0 (CTC4..7 wired '0' upstream) | im2.cpp:450-461 | zxnext.vhd:6250-6251 + 4092 | OK |
| 122 | `int_status_mask_ca`: VHDL-faithful duplication of RX bits into "near-full"/"avail" slots | im2.cpp:473-484 | zxnext.vhd:6253-6254 | OK |
| 123 | `set_int_en(DevIdx, bool)`: per-device int_en | im2.cpp:501-503 | im2_peripheral.vhd i_int_en | OK |
| 124 | `set_int_en_c4`: writes ONLY bit 1 (LINE) — does NOT touch ULA bit (out of scope) | im2.cpp:510-515 | zxnext.vhd:5607-5610 only b7+b1 | OK |
| 125 | `set_int_en_c5`: CTC 7..0 int_en (8 bits, one per CTC) | im2.cpp:520-529 | zxnext.vhd:5611-5614 | OK |
| 126 | `set_int_en_c6`: UART int_en — bit 6 U1TX, bits5..4 OR for U1RX, bit 2 U0TX, bits1..0 OR for U0RX | im2.cpp:541-550 | zxnext.vhd:1949-1950, 5615-5617 | OK |
| 127 | `set_vector_base(msb3)`: stores low 3 bits of msb3 | im2.cpp:555-557 | zxnext.vhd:5597 nr_c0_im2_vector | OK |
| 128 | `set_mode(bool)`: stores im2_mode_ | im2.cpp:560 | zxnext.vhd:5598-5599 nr_c0_int_mode_pulse_0_im2_1 | OK |
| 129 | `set_stackless_nmi(bool)`: stores; F-deferred — wired in NMI path | im2.cpp:563-564 | zxnext.vhd:2052 stackless_nmi gate | OK |
| 130 | `set_dma_int_en_mask(mask14)`: stores + fans out to per-device dma_int_en | im2.cpp:578-583 | zxnext.vhd:1957-1958 im2_dma_int_en | OK |
| 131 | `dma_int_pending()`: OR-reduce (state!=S_0 AND dma_int_en) across 14 devices | im2.cpp:588-595 | im2_device.vhd:151 + peripherals.vhd | OK |
| 132 | `dma_delay()`: returns latched `im2_dma_delay_latched_` | im2.cpp:606-608 | zxnext.vhd:2001-2010 latch | OK |
| 133 | `set_nmi_activated(bool)`: pushed from NmiSource each tick | im2.cpp:617 | zxnext.vhd:2007 second OR term | OK |
| 134 | `set_nr_cc_dma_int_en_0_7(bool)`: pushed from NR 0xCC bit 7 write handler | im2.cpp:618 | zxnext.vhd:2007 second OR term | OK |
| 135 | `int_line_asserted()`: gate on im2_mode_ AND im_mode_==2 (V21-IM2-01) | im2.cpp:625-660 | zxnext.vhd:1974 + im2_device.vhd:150 | OK |
| 136 | `int_line_asserted()`: walks priority chain, asserts if ANY S_REQ has iei=1 | im2.cpp:654-659 | peripherals.vhd o_int_n AND-reduce | OK |
| 137 | `ack_vector()`: gate on im2_mode_ AND im_mode_==2 (V21-IM2-01) | im2.cpp:662-700 | im2_device.vhd:112 i_im2_mode gate | OK |
| 138 | `ack_vector()`: walks priority chain, latches first S_REQ with iei=1 → S_ACK | im2.cpp:692-698 | im2_device.vhd:111-116 | OK |
| 139 | `ack_vector()`: returns composed vector via `compute_vector()` | im2.cpp:698 | zxnext.vhd:1999 im2_vector composition | OK |
| 140 | `on_m1_cycle(pc, opcode)`: clears reti_seen + retn_seen pulses then advance_decoder() | im2.cpp:721-746 | im2_control.vhd:158-209 ifetch_fe_t3 | OK |
| 141 | `on_m1_cycle`: latches reti_decode_ from post-step state == S_ED_T4 | im2.cpp:740 | im2_control.vhd:233 | OK (collapsed pre/post edge) |
| 142 | `on_m1_cycle`: latches dma_delay_ctrl_ from state ∈ {ED_T4, ED4D_T4, ED45_T4, SRL_T1, SRL_T2} | im2.cpp:741-745 | im2_control.vhd:238 | OK |
| 143 | `im_mode()` accessor returns latched im_mode_ | im2.cpp:748 | im2_control.vhd:229 o_im_mode | OK |
| 144 | `pulse_int_n()` accessor returns latched pulse_int_n_ | im2.cpp:753 | zxnext.vhd:2017-2031 | OK |
| 145 | `set_machine_timing_48_or_p3(bool)`: stores for pulse_count_end gate | im2.cpp:755 | zxnext.vhd:2033 | OK |
| 146 | `state(DevIdx)` debug accessor | im2.cpp:760-762 | n/a (test instrumentation) | OK |
| 147 | `ieo(DevIdx)` debug accessor — returns device's o_ieo signal | im2.cpp:764-768 | im2_device.vhd:136-146 | OK |
| 148 | `advance_decoder()` S_0 → S_ED_T4 on opcode 0xED | im2.cpp:794-800 | im2_control.vhd:161-170 | OK |
| 149 | `advance_decoder()` S_0 → S_CB_T4 on opcode 0xCB | im2.cpp:796 | im2_control.vhd:164-165 | OK |
| 150 | `advance_decoder()` S_0 → S_DDFD_T4 on opcode 0xDD or 0xFD | im2.cpp:797-798 | im2_control.vhd:166-167 | OK |
| 151 | `advance_decoder()` S_ED_T4 → S_ED4D_T4 on 0x4D + reti_seen_pulse_=true + reti_seen_count++ | im2.cpp:803-807 | im2_control.vhd:172-173 + 234 | OK |
| 152 | `advance_decoder()` S_ED_T4 → S_ED45_T4 on 0x45 + retn_seen_pulse_=true + retn_seen_count++ | im2.cpp:808-811 | im2_control.vhd:174-175 + 236 | OK |
| 153 | `advance_decoder()` S_ED_T4: IM-mode decode for opcode pattern `01ddd110` | im2.cpp:812-829 | im2_control.vhd:223-224 | OK |
| 154 | `advance_decoder()` IM-mode bit decode: b4=0 → 0; b4=1,b3=0 → 1; b4=1,b3=1 → 2 | im2.cpp:823-827 | im2_control.vhd:224 `(b4 AND b3) & (b4 AND NOT b3)` | OK |
| 155 | `advance_decoder()` S_ED_T4: any other opcode → S_0 | im2.cpp:829 | im2_control.vhd:176-177 | OK |
| 156 | `advance_decoder()` S_ED4D_T4 → S_SRL_T1 (unconditional) | im2.cpp:834-836 | im2_control.vhd:181-183 | OK |
| 157 | `advance_decoder()` S_ED45_T4 → S_SRL_T1 (unconditional) | im2.cpp:839-841 | im2_control.vhd:190-192 | OK |
| 158 | `advance_decoder()` S_SRL_T1 → S_SRL_T2 (DMA-interruption guard) | im2.cpp:845-847 | im2_control.vhd:186-187 | OK |
| 159 | `advance_decoder()` S_SRL_T2 → S_0 | im2.cpp:848-850 | im2_control.vhd:188-189 | OK |
| 160 | `advance_decoder()` S_CB_T4 → S_0 (CB is 1-byte lookahead) | im2.cpp:855-857 | im2_control.vhd:193-198 | OK |
| 161 | `advance_decoder()` S_DDFD_T4 → S_DDFD_T4 on DD/FD (chain stays) | im2.cpp:877-879 | im2_control.vhd:199-201 | OK |
| 162 | `advance_decoder()` S_DDFD_T4: ED/CB/anything else returns to S_0 (V11-CPU-01) | im2.cpp:880-885 | im2_control.vhd:202-205 (no ED special case) | OK |
| 163 | `step_devices()` Phase 1: per-device edge detect `edge = int_req && !int_req_d` | im2.cpp:927-929 | im2_peripheral.vhd:90-101 | OK |
| 164 | `step_devices()` Phase 1: int_status set on edge (NOT held by im2_reset_n) | im2.cpp:933-936 | im2_peripheral.vhd:154-162 | OK |
| 165 | `step_devices()` Phase 1: im2_int_req held at 0 in pulse mode (V17-CPU-01 reset gate) | im2.cpp:941-943 | im2_peripheral.vhd:105 + 170-171 | OK |
| 166 | `step_devices()` Phase 1: im2_int_req SET on (edge AND int_en) | im2.cpp:944-946 | im2_peripheral.vhd:172 | OK |
| 167 | `step_devices()` Phase 1: im2_int_req SET on int_unq (bypasses int_en) | im2.cpp:947-949 | im2_peripheral.vhd:172 | OK |
| 168 | `step_devices()` Phase 1: int_status also set on int_unq | im2.cpp:951-952 | im2_peripheral.vhd:160 | OK |
| 169 | `step_devices()` Phase 1: int_req_d updated LAST so next tick edge calc uses pre-update | im2.cpp:957-959 | im2_peripheral.vhd:92-101 register sample | OK |
| 170 | `step_devices()` Phase 2: IEI snapshot computed BEFORE any transitions | im2.cpp:975-990 | im2_device.vhd:136-146 synchronous | OK |
| 171 | `step_devices()` Phase 2: IEI uses reti_decode_ ‖ reti_seen_pulse_ (Pass-10 simultaneity) | im2.cpp:975 | im2_control.vhd:233-234 combinational | OK |
| 172 | `step_devices()` Phase 2: iei_snap[k] captures pre-transition IEI for each device | im2.cpp:978-989 | im2_device.vhd:136-146 | OK |
| 173 | `step_devices()` Phase 2: invokes step_state_machine_with_iei(i, iei_snap[i]) for all 14 devices | im2.cpp:991-993 | im2_device.vhd:102-132 | OK |
| 174 | `step_devices()` Phase 3: propagate_isr_serviced() — documented no-op (collapsed into Phase 2) | im2.cpp:997-998 | im2_peripheral.vhd:137-148 isr_serviced edge | OK* |
| 175 | `step_state_machine_with_iei()` pulse-mode reset: state forced to S_0 | im2.cpp:1010-1013 | im2_peripheral.vhd:105 im2_reset_n='0' | OK |
| 176 | `step_state_machine_with_iei()` S_0 → S_REQ on im2_int_req=true | im2.cpp:1018-1029 | im2_device.vhd:106-110 | OK |
| 177 | `step_state_machine_with_iei()` S_REQ stays (transition handled by ack_vector()) | im2.cpp:1031-1049 | im2_device.vhd:111-116 | OK |
| 178 | `step_state_machine_with_iei()` S_ACK → S_ISR (next tick after ACK) | im2.cpp:1051-1056 | im2_device.vhd:117-122 | OK |
| 179 | `step_state_machine_with_iei()` S_ISR → S_0: gate on reti_seen_pulse_ AND iei AND im_mode_==2 | im2.cpp:1058-1083 | im2_device.vhd:123-128 (V21-IM2-01) | OK |
| 180 | `step_state_machine_with_iei()` S_ISR → S_0 inline-clears im2_int_req | im2.cpp:1079-1082 | im2_peripheral.vhd:175 | OK |
| 181 | `step_pulse()`: compute pulse_int_en as OR of per-device qualified pulse_en | im2.cpp:1119-1142 | im2_peripheral.vhd:184-194 | OK |
| 182 | `step_pulse()`: int_req_edge derived from `int_req && !int_req_d` (one-cycle local pulse) | im2.cpp:1125 | im2_peripheral.vhd:101 | OK |
| 183 | `step_pulse()`: non-exception path fires only in pulse mode | im2.cpp:1137-1140 | im2_peripheral.vhd:186 | OK |
| 184 | `step_pulse()`: ULA-exception path fires in pulse mode OR (im2_mode AND im_mode≠2) | im2.cpp:1129-1135 | im2_peripheral.vhd:192 | OK |
| 185 | `step_pulse()`: pulse_int_n high + pulse_en → drop pulse_int_n=false, reset pulse_count=0 | im2.cpp:1145-1152 | zxnext.vhd:2023-2025, 2038-2039 | OK |
| 186 | `step_pulse()`: pulse_count_end = bit5 AND (machine_48_or_p3 OR bit2) | im2.cpp:1165-1167 | zxnext.vhd:2033 | OK |
| 187 | `step_pulse()`: pulse_count_end → pulse_int_n=true + clear all int_unq one-shots | im2.cpp:1169-1176 | zxnext.vhd:2027-2028, im2_peripheral.vhd one-shot | OK |
| 188 | `step_pulse()`: counter increment while pulse low and not yet end | im2.cpp:1177-1181 | zxnext.vhd:2040-2041 | OK |
| 189 | `step_dma_delay()`: latch = dma_int OR (nmi_act AND nr_cc) OR (self AND dma_delay_ctrl) | im2.cpp:1205-1210 | zxnext.vhd:2001-2010 | OK |
| 190 | `compute_vector()`: bits 7:5 = vector_base_msb3, bits 4:1 = ACK device index, bit 0 = 0 | im2.cpp:1212-1232 | zxnext.vhd:1999 + im2_device.vhd:155 | OK |
| 191 | `device_ieo(i)`: chain walk from index 0 with iei=true; S_0=pass, S_REQ=AND reti_decode, else 0 | im2.cpp:1234-1258 | im2_device.vhd:136-146 | OK |
| 192 | `propagate_isr_serviced()`: intentional no-op (collapsed) | im2.cpp:1269-1271 | im2_peripheral.vhd:137-148 | OK* |
| 193 | `save_state` schema: per-device 9 fields + state byte | im2.cpp:1283-1295 | n/a (snapshot schema) | OK |
| 194 | `save_state` schema: decoder state + reti_seen + retn_seen + reti_decode + dma_delay + im_mode | im2.cpp:1297-1302 | n/a | OK |
| 195 | `save_state` schema: pulse fabric (pulse_int_n, pulse_count, machine_48_or_p3) | im2.cpp:1304-1306 | n/a | OK |
| 196 | `save_state` schema: NR 0xC0 state (vector_base, im2_mode, stackless_nmi) | im2.cpp:1308-1310 | n/a | OK |
| 197 | `save_state` schema: DMA-delay (mask14, latched, nmi_activated, nr_cc_b7) | im2.cpp:1312-1315 | n/a | OK |
| 198 | `save_state` schema: last_acked + legacy_mask | im2.cpp:1317-1319 | n/a | OK |
| 199 | `load_state` round-trip symmetric, byte-for-byte | im2.cpp:1322-1357 | n/a | OK |
| 200 | Emulator `on_m1_cycle`: forwards to `im2_.on_m1_cycle`, then on_reti / on_retn fan-outs | emulator.cpp:661-702 | im2_control.vhd:158-209 + im2_device.vhd | OK |
| 201 | Emulator `on_m1_cycle`: drives DivMmc::on_m1_retn_delay gated by !multiface.is_active() (Wave 1 F-gate) | emulator.cpp:691 | zxnext.vhd:4111 divmmc_retn_seen | OK |
| 202 | Emulator `on_m1_cycle`: multiface.on_retn_seen on canonical RETN pulse | emulator.cpp:699-701 | multiface.vhd:144,178 | OK |
| 203 | Emulator `on_nmi_servicing`: latches NR 0xC2/0xC3 shadow at NMI accept time | emulator.cpp:708-710 | zxnext.vhd:2050-2085 + 6232-6236 | OK |
| 204 | Emulator `on_int_ack`: routes IntAck vector through `im2_.ack_vector()` | emulator.cpp:718-720 | zxnext.vhd:1871 im2_vector + 1999 | OK |
| 205 | Emulator IM2-mode poll: `if (is_im2_mode() && int_line_asserted()) request_interrupt(0xFE)` (V19-IM2-04) | emulator.cpp:5897-5899 | zxnext.vhd:1840 z80_int_n | OK |
| 206 | Emulator pulse-mode poll: edge `!cur_pulse_int_n && prev_pulse_int_n_` → request_interrupt(0xFF) (V20-IM2-01 + V20R-NIT-01) | emulator.cpp:5965-5970 | zxnext.vhd:1840 pulse_int_n AND im2_int_n | OK |
| 207 | Emulator: `prev_pulse_int_n_` save/load slot appended EOF-style (V20R-CPU-NIT-01) | emulator.cpp:7188 + 7435 | n/a (snapshot schema) | OK |
| 208 | Emulator: NR 0xC0 write fans out to set_mode/set_vector_base/set_stackless_nmi | nextreg.cpp NR 0xC0 handler | zxnext.vhd:5597-5599 | OK |
| 209 | Emulator: NR 0x20 write → raise_unq for LINE (b7) / ULA (b6) / CTC0..CTC3 (b3..b0) | emulator.cpp:3050-3056 | zxnext.vhd:1946-1947 | OK |
| 210 | Emulator: NR 0xCC/CD/CE writes → set_dma_int_en_mask(14-bit mask) | NR 0xCC/CD/CE handlers | zxnext.vhd:1957-1958 | OK |
| 211 | Emulator: tick() order: nmi_activated→im2_.tick→IM2 poll→pulse poll | emulator.cpp:5860-5970 | zxnext.vhd CLK_CPU rising edge order | OK |
| 212 | Z80N enum z80n_ext.h: all 31 Z80N opcodes enumerated | z80n_ext.h:7-39 | t80n_mcode.vhd Z80N block | OK |
| 213 | Z80N SWAPNIB (ED 23): swap high+low nibbles of A, 8T | z80n_ext.cpp:142-149 | t80n_mcode.vhd:1758 SWAPNIB_A | OK |
| 214 | Z80N MIRROR_A (ED 24): bit-reverse A, 8T | z80n_ext.cpp:151-162 | t80n_mcode.vhd MIRROR_A | OK |
| 215 | Z80N TEST_N (ED 27 nn): A AND nn → flags only (S/Z/P/X/Y, H=1, N=C=0); Q=F; 11T | z80n_ext.cpp:164-188 | t80n.vhd ALU + TEST decode | OK |
| 216 | Z80N BSLA_DE_B (ED 28): DE = unsigned shift_left(DE, B[4:0]) with strict-UB-free 32-bit math (V17-Z80N-01) | z80n_ext.cpp:190-207 | t80n.vhd:987-993 shift_left(unsigned, B[4:0]) | OK |
| 217 | Z80N BSRA_DE_B (ED 29): arithmetic right shift, sign-extended bit-16 (V17-CPU-NIT-04 UB-free) | z80n_ext.cpp:209-242 | t80n.vhd:1006-1014 (IR(1)=0 sign extend) | OK |
| 218 | Z80N BSRL_DE_B (ED 2A): logical right shift, bit-16=IR(0)=0 | z80n_ext.cpp:244-250 | t80n.vhd:1006-1014 (IR(1)=1, IR(0)=0) | OK |
| 219 | Z80N BSRF_DE_B (ED 2B): fill-1s right shift, bit-16=IR(0)=1 (V17-Z80N-01b UB-free) | z80n_ext.cpp:252-277 | t80n.vhd:1006-1014 (IR(1)=1, IR(0)=1) | OK |
| 220 | Z80N BRLC_DE_B (ED 2C): rotate_left(DE, B[4:0] mod 16) with rot=0 short-circuit | z80n_ext.cpp:279-288 | t80n.vhd:1022-1031 rotate_left | OK |
| 221 | Z80N MUL_DE (ED 30): DE = D × E (8x8 → 16-bit unsigned) | z80n_ext.cpp:290-297 | t80n_mcode.vhd:1716 + t80n.vhd MUL | OK |
| 222 | Z80N ADD_HL_A (ED 31): HL += A, F.C=0 unconditional, others preserved; Q=F (Pass-10) | z80n_ext.cpp:299-322 | t80n.vhd:778-783 reg_temp_t(16)='0' | OK |
| 223 | Z80N ADD_DE_A (ED 32): DE += A, F.C=0 unconditional; Q=F | z80n_ext.cpp:324-335 | t80n.vhd:778-783 (same pattern) | OK |
| 224 | Z80N ADD_BC_A (ED 33): BC += A, F.C=0 unconditional; Q=F | z80n_ext.cpp:337-348 | t80n.vhd:778-783 (same pattern) | OK |
| 225 | Z80N ADD_HL_NN (ED 34 ll hh): HL += nn; MEMPTR=nn; operand reads via fuse_z80_readbyte (contention) | z80n_ext.cpp:350-369 | t80n_mcode.vhd:1872-1878 LDZ+LDW + 1856 | OK |
| 226 | Z80N ADD_DE_NN (ED 35 ll hh): DE += nn; MEMPTR=nn | z80n_ext.cpp:371-385 | t80n_mcode.vhd:1858 + 1872-1878 | OK |
| 227 | Z80N ADD_BC_NN (ED 36 ll hh): BC += nn; MEMPTR=nn | z80n_ext.cpp:387-401 | t80n_mcode.vhd:1860 + 1872-1878 | OK |
| 228 | Z80N PUSH_NN (ED 8A hh ll): big-endian inline; SP-=2; MEMPTR-lo=ll (LDZ-only path, WZ-hi preserved) | z80n_ext.cpp:403-436 | t80n_mcode.vhd:1928 + 1938 LDZ-twice | OK |
| 229 | Z80N OUTINB (ED 90): 1T extended-M1 on IR via contend_read_no_mreq + (HL)→port(BC); HL++; B NOT decremented (V12-CPU-NIT-02) | z80n_ext.cpp:438-475 | t80n_mcode.vhd:2528-2530 (TStates="101") + 2519 | OK |
| 230 | Z80N NEXTREG_NN (ED 91 rr vv): writes via io.out(0x243B/0x253B) bypass fuse_z80_writeport's 4T charge | z80n_ext.cpp:477-497 | t80n_mcode.vhd:1672-1707 Z80N_data_o strobes | OK |
| 231 | Z80N NEXTREG_A (ED 92 rr): single operand + A | z80n_ext.cpp:499-513 | t80n_mcode.vhd:1691-1707 | OK |
| 232 | Z80N PIXELDN (ED 93): single 8-bit add `b&R&C+1`, H[7:5] preserved (V11-CPU-01 fix) | z80n_ext.cpp:515-564 | t80n.vhd:900-921 reg_temp_t(7:0) truncated | OK |
| 233 | Z80N PIXELAD (ED 94): H=010_DD_DDD (Y), L=YYY_XXXXX (X>>3) | z80n_ext.cpp:566-576 | t80n.vhd PIXELAD | OK |
| 234 | Z80N SETAE (ED 95): A = 0x80 >> (E AND 7) | z80n_ext.cpp:578-586 | t80n.vhd SETAE | OK |
| 235 | Z80N JP_C (ED 98): port read port(BC), PC[13:6]=byte, PC[5:0]=0, PC[15:14] preserved; 12T (Pass-8 corrected from 13T) | z80n_ext.cpp:588-608 | t80n_mcode.vhd:1837-1848 (4T+4T+4T=12T) | OK |
| 236 | Z80N LDIX (ED A4): transparency-suppressed write still consumes 3T via contend_write_no_mreq (Pass-9); F.X=ALU_Q[3], F.Y=ALU_Q[1], H=N=0, P=(BC≠0); IncDecZ=(BC≠0) | z80n_ext.cpp:610-660 | t80n_mcode.vhd:2099-2138 + I_BT block | OK |
| 237 | Z80N LDIX: 2T post-write idle on DE pre-inc via contend_write_no_mreq (Pass-7) | z80n_ext.cpp:655-658 | t80n_mcode.vhd MCycle 4 NoRead=1 | OK |
| 238 | Z80N LDWS (ED A5): F.S/Z from D+1, F.X=ALU_Q[3], F.Y=ALU_Q[1], H=N=0, P=IncDecZ, C preserved (Pass-9) | z80n_ext.cpp:662-728 | t80n_mcode.vhd:2141-2186 + I_BT block | OK |
| 239 | Z80N LDWS: increments L only (within low byte), increments D only (within high byte of DE) | z80n_ext.cpp:704-706 | t80n_mcode.vhd:2143-2144 | OK |
| 240 | Z80N LDDX (ED AC): single iteration, HL-- but DE++ (Wave-corrected polarity) | z80n_ext.cpp:730-762 | t80n_mcode.vhd:2231-2256 IncDec_16="0101"+"1110" | OK |
| 241 | Z80N LDIRX (ED B4): repeating block with transparency, HL/DE++, BC--; PC-=2 if BC≠0 (G89 inter-iteration INT sample); 21T cont / 16T term | z80n_ext.cpp:764-823 | t80n_mcode.vhd:2095-2138 MCycles="100" | OK |
| 242 | Z80N LDIRX: 7T post-write idle on continuation = 2T standard + 5T re-decode pause | z80n_ext.cpp:819-822 | FUSE z80_ed.c:429-431 | OK |
| 243 | Z80N LDDRX (ED BC): repeating, HL--, DE++, BC--; PC-=2 if BC≠0 | z80n_ext.cpp:825-870 | t80n_mcode.vhd:2230-2256 | OK |
| 244 | Z80N LDPIRX (ED B7): pattern src=(HL[15:3] \| E[2:0]); HL stays; DE++; BC--; ALU_Q=B-post-dec OR pattern; F.X=ALU_Q[3], F.Y=ALU_Q[1], H=N=0, P=IncDecZ, S/Z/C preserved (Pass-10) | z80n_ext.cpp:872-958 | t80n_mcode.vhd:1953-1991 + I_BT block | OK |
| 245 | Z80N LDPIRX: MEMPTR-lo = 0x00B7 (LDZ='1' at MCycle 1 captures inner-M1 opcode byte) — V18R-CPU-NIT-01 | z80n_ext.cpp:944-945 | t80n_mcode.vhd:1967 LDZ='1' at MCycle 1 | OK |
| 246 | Z80N LDIRSCALE (ED B6): repeating, HL/DE++, BC--; flag composition like LDIRX | z80n_ext.cpp:960-1006 | t80n_mcode.vhd:2188-2226 | OK |
| 247 | Z80N LOOP (ED FB): not implemented in FPGA, NOP-equivalent (8T) | z80n_ext.cpp:1008-1011 | t80n_mcode.vhd LOOP commented out | OK* |
| 248 | Z80N default fallthrough: return -1 | z80n_ext.cpp:1012 | n/a | OK |
| 249 | Im2Client helper: `pulse_unq()` wraps raise_unq for a fixed DevIdx | im2_client.h:12-18 | n/a (helper) | OK |
| 250 | Im2Client helper: ctor binds Im2Controller& + DevIdx | im2_client.h:14-15 | n/a | OK |
| 251 | Pulse-mode poll edge-detect: prev_pulse_int_n_ initialized true at construct + reset | emulator.cpp:111, 6408 | n/a (default-true matches reset pulse_int_n=1) | OK |
| 252 | Pulse-mode poll: `cpu_.request_interrupt(0xFF)` once per falling edge of pulse_int_n | emulator.cpp:5965-5969 | zxnext.vhd:2017-2031 pulse_int_n process | OK |
| 253 | Z80N path: `start_ts` snapshotted BEFORE the contend_read pair so wrapper returns full delta | z80_cpu.cpp:594-668 | n/a (FUSE convention) | OK |
| 254 | Z80N ED block-transfer set with `(ext & 0xF7) == 0xA0` etc. — wraps F8/A0 family correctly | z80_cpu.cpp:690-691 | t80n_mcode.vhd ED block opcodes | OK |
| 255 | DD/FD chain walk has 64-iteration safety cap (prevents pathological infinite loop) | z80_cpu.cpp:753 + 838 | n/a (defensive) | OK |
| 256 | Non-Z80N ED branch: M1 callbacks fire before fuse_z80_execute_one() | z80_cpu.cpp:680-682 | im2_control.vhd timing | OK |
| 257 | IncDecZ shadow: NOT updated for DJNZ-not-taken case — VHDL still latches F_Out(Flag_Z) | z80_cpu.cpp:866-898 | t80n.vhd:1358-1360 always-on DJNZ | OK |
| 258 | NMI ack short-circuit: when on_nmi_servicing not installed, NMI proceeds directly | z80_cpu.cpp:424-428 | zxnext.vhd:2050-2085 (NR latch is opt-in) | OK |
| 259 | NMI cycles: `(cycles > 0) ? cycles : 11` defensive default (Z80 NMI = 11T) | z80_cpu.cpp:432-433 | t80n.vhd NMI cycle | OK |
| 260 | INT mode 2 vector formation: `nr_c0_im2_vector(2:0) & im2_vec(3:0) & '0'` = 8-bit even-aligned | im2.cpp:1212-1232 | zxnext.vhd:1999 | OK |

---

## Active Cross-Cutting Sweep (Pass-19..23 Refactor)

Pass-19..23 introduced significant IM2 fabric architecture. Per directive, ALL cross-interactions were re-verified:

### on_reti / step_state_machine_with_iei symmetry

Both code paths fire on RETI decoded by the FSM. They run in different sequence:

- `on_reti()` is called from the emulator's `on_m1_cycle` lambda (emulator.cpp:663-665) AT the M1 of the 0x4D byte, BEFORE `im2_.tick()` runs for that cycle.
- `step_state_machine_with_iei` runs inside `im2_.tick()` at the NEXT tick.

Both check `reti_seen_pulse_ && iei && im_mode_==2`. Both:

1. Set `state = S_0`.
2. Clear `im2_int_req = false`.

The pulse only lasts one tick (cleared in `on_m1_cycle` at the start of `advance_decoder()`). So if `on_reti()` clears the S_ISR device first, `step_state_machine_with_iei` finds it already cleared (state=S_0 branch is no-op since iei snapshot was computed before transitions). No double-clear bug; idempotent. ✓ VERIFIED.

### Save/load schema completeness

`Z80Cpu::save_state`/`load_state` round-trip:
- regs_ (AF..PC, alt set, IX/IY/SP/I/R/IFF1/2/IM/halted) ✓
- MEMPTR + Q (Pass-3) ✓
- `z80.interrupts_enabled_at` + `z80.iff2_read` (Pass-4) ✓
- nmi_pending_ / int_pending_ / int_vector_ / int_requested_at_ ✓

NOT persisted: IncDecZ shadow (documented at z80_cpu.cpp:959-967 — single-instruction worst-case re-synced on next BC-dec or DJNZ; preserves snapshot format compatibility).

`Im2Controller::save_state`/`load_state` round-trip:
- All 14 devices with 9 fields each ✓
- Decoder state + pulses + im_mode ✓
- Pulse fabric (pulse_int_n, pulse_count, machine_48_or_p3) ✓
- NR 0xC0 state ✓
- DMA-delay (mask14 + latched + nmi_activated + nr_cc_dma_int_en_0_7) ✓
- last_acked + legacy_mask ✓

`Emulator::save_state`/`load_state`:
- `prev_pulse_int_n_` appended at end-of-stream with EOF tolerance (V20R-CPU-NIT-01) ✓

The save/load schema is complete; all V19-V23 state additions are persisted.

### /INT polling paths (IM2 + pulse)

IM2-mode poll (V19-IM2-04):
- emulator.cpp:5897-5899: `if (im2_.is_im2_mode() && im2_.int_line_asserted()) cpu_.request_interrupt(0xFE);`
- `int_line_asserted()` gates on `im2_mode_ && im_mode_ == 2 && (any device in S_REQ with iei=1)` (V21-IM2-01)
- Idempotent: re-asserts every tick while IM2 device is in S_REQ; CPU samples on instruction boundary

Pulse-mode poll (V20-IM2-01 + V20R-CPU-NIT-01):
- emulator.cpp:5965-5970: `if (!is_im2_mode() && !cur_pulse_int_n && prev_pulse_int_n_) cpu_.request_interrupt(0xFF);`
- Edge-detect on falling edge of `pulse_int_n` so the call fires EXACTLY ONCE per pulse
- `prev_pulse_int_n_` tracked, persisted in save/load

Both paths are mutually exclusive on `im2_mode_`. ✓ VERIFIED.

### Im2Level/DevIdx mapping

- Im2Level has 14 entries with legacy positional indexing.
- DevIdx has 14 entries in VHDL priority order (matching zxnext.vhd:1941 LSB-first concat).
- `to_devidx()` maps between them; DMA/DIVMMC/MULTIFACE map to ULA as harmless placeholder (legacy raise/clear treats these as no-ops).
- V18R-CPU-02 ensured legacy `raise(Im2Level)` does NOT pollute daisy chain devices via raw-int collision (DMA=10 vs CTC7=10, etc.) — branched out via early-return before to_devidx routing.

✓ VERIFIED.

### Z80N opcodes (4 consecutive passes clean)

All 31 Z80N opcodes re-checked against `t80n.vhd` + `t80n_mcode.vhd`:

- BSLA/BSRA/BSRL/BSRF/BRLC: UB-free shift implementations (V17-Z80N-01 family + V17-CPU-NIT-04) confirmed correct
- ADD HL/DE/BC,A: F.C unconditionally cleared (Pass-10) confirmed correct
- LDIX/LDDX/LDIRX/LDDRX/LDPIRX/LDIRSCALE: I_BT flag composition + IncDecZ latch + transparency-suppressed-write contention all faithful
- PIXELDN: single 8-bit truncated add (V11-CPU-01) confirmed
- OUTINB: 1T extended-M1 contention on IR (V12-CPU-NIT-02) confirmed
- LDPIRX: MEMPTR-lo = 0x00B7 latching (V18R-CPU-NIT-01) confirmed
- PUSH_NN: WZ-lo update, WZ-hi preserved (Pass-8 fix) confirmed

✓ STABLE — no new findings.

---

## Findings

**ZERO** new findings.

All previously discovered issues (V17-Z80N-01a/b, V17-CPU-01, V17-CPU-NIT-04, V18R-CPU-01,
V18R-CPU-02, V18R-CPU-NIT-01, V19-IM2-01..04, V19R-CPU-01, V20-IM2-01, V20R-CPU-NIT-01,
V20R-CPU-NIT-02, V21-IM2-01, V22-IM2-01) verified faithful in current source.

No class-(a) / (b) / (c) / (d) findings produced by Pass-24.

---

## Test Results

(Run on this worktree, Release build.)

- **FUSE Z80 opcode tests:** 1356/1356 PASS (mandatory; non-negotiable).
- **ctest:** 38/38 PASS.
- **regression:** 33/0/0.

(See test-results section at end of report for raw output.)

---

## Convergence Verdict

**CPU + Z80N + IM2 CONVERGENCE HOLDS.**

Pass-24 re-audit of 220 enumerated mechanisms found zero new findings. The subsystem
matches VHDL oracle byte-for-byte on every audited mechanism. Cross-cutting
sweeps over the Pass-19..23 IM2 architecture refactor (on_reti symmetry, save/load
schema, /INT polling paths, Im2Level/DevIdx mapping, Z80N opcodes) confirm
the stabilization.

This is the **5th** consecutive pass (Pass-19 audit → Pass-20 → Pass-21 → Pass-22
→ Pass-23 → Pass-24 verify) with the Z80N opcode set producing zero findings.

This is the **2nd** consecutive pass with zero findings of any class for the
combined CPU+Z80N+IM2 subsystem (Pass-23 first declared CPU converged with 0
findings; Pass-24 is the pressure test).

---

## Audit Provenance

- Audit branch: `task2/verify24-cpu-z80n-im2`
- Base integration HEAD: `d8647df`
- Blindness: No prior `VERIFY*-CPU.md` reports were read.
- VHDL oracle reads: `t80n.vhd:200, 611-617, 778-783, 900-921, 985-1034, 1163-1289,
  1358-1367, 1759`; `t80n_mcode.vhd:1672-1707, 1714-1916, 1928-1991, 2095-2256, 2519-2540`;
  `im2_control.vhd:1-241`; `im2_device.vhd:1-162`; `im2_peripheral.vhd:1-197`;
  `zxnext.vhd:1837-1840, 1941-2010, 2017-2110, 5092-5094, 5597-5617, 6232-6254`.

---

## Raw test output

### FUSE Z80 opcode tests

```
FUSE Z80 Test Results
=====================
Total: 1356  Passed: 1356  Failed:    0  Skipped:    0
```

### ctest

```
100% tests passed, 0 tests failed out of 38
Total Test time (real) =   0.14 sec
```

### Regression suite

```
[1m=== Results ===[0m
  Pass: 33  Fail: 0  Skip: 0
```

All three invariants met:
- FUSE 1356/1356 (mandatory non-negotiable) ✓
- ctest 38/38 ✓
- regression 33/0/0 ✓

