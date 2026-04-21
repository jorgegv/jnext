# TASK3 — CTC + Interrupts SKIP-Reduction Plan

**Baseline measurement (commit HEAD, 2026-04-21):**
`Total:  150  Passed:   44  Failed:    0  Skipped:  106`
Regression: 34/0/0. FUSE Z80: 1356/1356. Plan drift: plan summary says "~151"; Section 13 header says "18" but lists 17 NR-C* rows — both are (D) plan nits, tolerated.

**Existing C++ surface touched by this plan:**
- `src/cpu/im2.{h,cpp}` — 45-line `Im2Controller` stub (priority-mask + naive vector + on_reti).
- `src/cpu/z80_cpu.{h,cpp}` — hosts `cpu_.on_m1_cycle` callback (RETI/RETN detect already wired at `src/core/emulator.cpp:163-181`).
- `src/peripheral/ctc.{h,cpp}` — CTC class. `handle_zc_to()` already implements the ring wrap (CTC-CH-01 bug from old 2026-04-15 audit is already fixed; 0 fails today).
- `src/core/emulator.{h,cpp}` — owns NR 0xC0/C4/C5/C6/C8/C9/CA/CC/CD/CE handlers (written today as shadow-store at lines 722-806), owns `im2_int_enable_[3]`, `im2_int_status_[3]`, `im2_hw_mode_`, `im2_vector_base_`, `im2_dma_delay_latched_` plus compose/update helpers at lines 2697-2741. Owns line-int/ULA-int scheduler dispatch at lines 1798-1816. This is where the current half-built interrupt wiring lives.
- `src/port/nextreg.{h,cpp}` — NextReg file with handler registration.
- `src/video/ula.{h,cpp}` / `src/peripheral/dma.{h,cpp}` / `src/peripheral/uart.{h,cpp}` — have single-callback int injection today (`on_interrupt = [this]{ im2_.raise(...) }`), which bypasses the whole VHDL S_0/S_REQ/S_ACK/S_ISR / daisy chain / int_unq / pulse fabric.

## In-flight backlog policy

**Per user directive (2026-04-21):** if a new minor backlog item surfaces during any phase — a small plan-drift nit, a one-line VHDL deviation spotted en passant, a missing getter, a sibling test that would benefit from a two-line fix — **fold it into the current wave** rather than deferring to a post-merge backlog. Complexity budget: no more than ~30 min of extra work per fold-in, and only if it does not change the VHDL-citation scope of the current branch.

Only defer if: the fold-in would (a) require an additional critic round, (b) touch another subsystem's src/, or (c) risk the current wave's regression budget. Deferrals get logged in the post-wave dashboard-refresh commit message, not quietly dropped.

## Section A — Row-by-row triage (106 skips)

Legend for disposition: **A** physical artifact / **B** VHDL-internal pipeline / **C** same-cycle-arbitration (try reframe first) / **D** structurally unreachable / **E** redundant / **F** genuine upstream gap, stay skip / **G** behavioural simplification, stay skip / **WONT** explicit non-implementation / **Un-skip via `<branch>`** un-skipped on named Phase-2 branch / **Re-home** move to `nextreg_integration_test.cpp` or a new `ctc_int_integration_test.cpp`.

Section 5/6 lazy-skip trio (audited 2026-04-15):

| ID | Section | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|---|
| CTC-CW-11 | 5 | iowr rising-edge detect not observable | **D (comment)** | ctc_chan.vhd:250-256 | API-level write() is discrete; VHDL strobe is internal pipeline. Re-home impossible; outcome-equivalent already covered by CTC-SM-*. |
| CTC-NR-02 | 6 | no `Ctc::get_int_enable()` accessor | **Un-skip via branch `task3-ctc-b-im2devicemachine`** | zxnext.vhd:4078 + read dispatch | Phase 2 Agent E (NR handler pass) adds the `get_int_enable()` accessor + hooks NR 0xC5 read path. Trivial, bundled with Agent E. |
| CTC-NR-04 | 6 | NR 0xC5 vs port-write overlap not modelled | **Keep skip (review later)** | zxnext.vhd nr_c5_we gate | User decision 2026-04-21: stays as `skip()`; reason string updated to "NR 0xC5 vs port-write overlap — cycle-accurate bus arbitration, review later". Re-visit as WONT candidate in a later sweep once the requirements DB lands. |

### Section 7 — IM2 Control block (IM2C-01..14, 14 rows)

All blocked on the RETI/RETN/IM-mode state machine not being exposed. The existing on_m1_cycle in emulator.cpp does rudimentary ED-prefix tracking for RETI/RETN but does not drive a visible reti_decode window, retn_seen pulse, dma_delay signal, or im_mode latch.

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| IM2C-01 | im2_control FSM not modelled | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:161-170 (S_0 → S_ED_T4) | Agent A |
| IM2C-02 | o_reti_seen pulse not exposed | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:234 | Agent A |
| IM2C-03 | o_retn_seen pulse not exposed | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:236 | Agent A |
| IM2C-04 | ED fall-through not observable | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:171-180 | Agent A |
| IM2C-05 | o_reti_decode window not exposed | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:233 | Agent A |
| IM2C-06 | CB prefix outside CTC scope | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:193-198 | Agent A (required for correct fall-through) |
| IM2C-07 | DD/FD chain outside CTC scope | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:199-206 | Agent A |
| IM2C-08 | o_dma_delay over RETI not exposed | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:238 | Agent A |
| IM2C-09 | SRL delay states not exposed | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:186-192 (S_SRL_T1/T2) | Agent A |
| IM2C-10 | im_mode=00 detection | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:218-227 | Agent A |
| IM2C-11 | im_mode=01 detection | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:218-227 | Agent A |
| IM2C-12 | im_mode=10 detection | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:218-227 | Agent A |
| IM2C-13 | falling-edge CLK_CPU update | **B (comment)** | im2_control.vhd:220 | VHDL-internal clocking detail; outcome equivalent already covered by IM2C-10/11/12 which observe the final im_mode value. Single-threaded tick emulator cannot distinguish rising vs falling edge of a named clock within a C++ call. |
| IM2C-14 | im_mode reset default | **Un-skip via `task3-ctc-a-im2control`** | im2_control.vhd:222 | Agent A (cheap: reset handler) |

**Net: 12 un-skip, 1 comment (IM2C-13), 1 un-skip (IM2C-14 bundled).**

### Section 8 — IM2 Device state machine (IM2D-01..12, 12 rows)

All blocked on per-device S_0/S_REQ/S_ACK/S_ISR not existing. The current `Im2Controller` uses a flat pending/active array with no daisy-chain, IEI/IEO, M1/IORQ acknowledge, or RETI-triggered ISR-exit.

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| IM2D-01 | S_0→S_REQ on int_req | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:105-110 | Agent B |
| IM2D-02 | INT_n asserted in S_REQ | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:150 | Agent B |
| IM2D-03 | INT_n blocked when IEI=0 | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:150 | Agent B |
| IM2D-04 | INT_n gated by im2_mode | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:150 | Agent B |
| IM2D-05 | S_REQ→S_ACK on M1=0 IORQ=0 IEI=1 | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:111-116 | Agent B — requires Z80 core to signal the IntAck cycle explicitly (new callback, see Section B below) |
| IM2D-06 | S_ACK→S_ISR on M1 high | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:117-122 | Agent B |
| IM2D-07 | S_ISR→S_0 on RETI seen + IEI=1 | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:123-128 | Agent B, depends on Agent A's reti_seen |
| IM2D-08 | S_ISR stays without RETI | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:123-128 | Agent B |
| IM2D-09 | o_vec during ACK | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:155 | Agent B |
| IM2D-10 | o_vec=0 outside ACK | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:155 | Agent B |
| IM2D-11 | o_isr_serviced pulse | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:159 | Agent B |
| IM2D-12 | o_dma_int in non-S_0 states | **Un-skip via `task3-ctc-f-dma-int`** | im2_device.vhd:151 | Agent F — relies on Agent B's state, but tested here. Put the test assertion on Agent B's branch using a stub i_dma_int_en=1; Agent F adds the NR 0xCC/CD/CE plumbing. |

**Net: 12 un-skip across Agent B (11) + Agent F integration (1).**

### Section 9 — IM2 Daisy Chain (IM2P-01..10, 10 rows)

All depend on Agent B (device state machine) + a new wrapper class `Im2Daisy` that orders 14 client peripherals and propagates IEI→IEO.

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| IM2P-01 | IEO=IEI in S_0 | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:139-140 | Agent B |
| IM2P-02 | IEO=IEI∧reti_decode in S_REQ | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:141-142 | Agent B |
| IM2P-03 | IEO=0 in S_ACK/S_ISR | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:143-144 | Agent B |
| IM2P-04 | Peripheral[0].IEI='1' | **Un-skip via `task3-ctc-b-im2devicemachine`** | peripherals.vhd:82 | Agent B (chain head) |
| IM2P-05 | Simultaneous requests priority | **Un-skip via `task3-ctc-b-im2devicemachine`** | peripherals.vhd:86-128 | Agent B |
| IM2P-06 | Lower-priority queued while higher serviced | **Un-skip via `task3-ctc-b-im2devicemachine`** | peripherals.vhd:86-128 + im2_device.vhd:139-144 | Agent B |
| IM2P-07 | Post-RETI lower proceeds | **Un-skip via `task3-ctc-b-im2devicemachine`** | im2_device.vhd:123-128 + peripherals.vhd | Agent B |
| IM2P-08 | 3-way chain | **Un-skip via `task3-ctc-b-im2devicemachine`** | peripherals.vhd generate loop | Agent B |
| IM2P-09 | AND-reduction of INT_n | **Un-skip via `task3-ctc-b-im2devicemachine`** | peripherals.vhd:146-156 | Agent B |
| IM2P-10 | OR-reduction of vectors | **Un-skip via `task3-ctc-b-im2devicemachine`** | peripherals.vhd:134-144 | Agent B |

**Net: 10 un-skip on Agent B.**

### Section 10 — Pulse Interrupt Mode (PULSE-01..09, 9 rows)

Pulse fabric composes the legacy ULA interrupt signal. Requires Agent A (reti_seen for reset), Agent B (int_req qualification), and new pulse timing state.

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| PULSE-01 | pulse_en path | **Un-skip via `task3-ctc-c-pulse`** | im2_peripheral.vhd:186 / zxnext.vhd:1996 | Agent C |
| PULSE-02 | IM2-mode pulse suppression | **Un-skip via `task3-ctc-c-pulse`** | im2_peripheral.vhd:186 | Agent C (depends on nr_c0_int_mode_pulse_0_im2_1 from Agent E) |
| PULSE-03 | ULA EXCEPTION pulse in IM2 mode | **Un-skip via `task3-ctc-c-pulse`** | im2_peripheral.vhd:192 | Agent C (EXCEPTION=1 only for ULA) |
| PULSE-04 | pulse_int_n waveform | **Un-skip via `task3-ctc-c-pulse`** | zxnext.vhd:2017-2031 | Agent C |
| PULSE-05 | 48K/+3 pulse = 32 cycles | **Un-skip via `task3-ctc-c-pulse`** | zxnext.vhd:2033 | Agent C, reads machine_timing_48/p3 from timing_.machine |
| PULSE-06 | 128K/Pentagon = 36 cycles | **Un-skip via `task3-ctc-c-pulse`** | zxnext.vhd:2033 | Agent C |
| PULSE-07 | Pulse counter reset on pulse_int_n=1 | **Un-skip via `task3-ctc-c-pulse`** | zxnext.vhd:2036-2044 | Agent C |
| PULSE-08 | INT_n = pulse_int_n AND im2_int_n | **Un-skip via `task3-ctc-c-pulse`** | zxnext.vhd:1840 | Agent C (composition at controller output) |
| PULSE-09 | o_BUS_INT_n exposure | **B (comment)** | zxnext.vhd:1675 | VHDL-internal bus pin not observable outside the controller; already covered by PULSE-08 equivalent. Redundant at this abstraction. |

**Net: 8 un-skip, 1 comment.**

### Section 11 — IM2 Peripheral Wrapper (IM2W-01..09, 9 rows)

Per-peripheral edge detection + int_status + int_unq bypass. Belongs in `Im2Client` mixin (Agent D owns).

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| IM2W-01 | int_req edge detect | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:90-101 | Agent D |
| IM2W-02 | im2_int_req latch | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:167-178 | Agent D |
| IM2W-03 | cleared by im2_isr_serviced | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:148, 175 | Agent D (needs Agent B's isr_serviced) |
| IM2W-04 | int_status set by int_req | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:154-162 | Agent D |
| IM2W-05 | int_status cleared by NR write | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1952-1955 | Agent E (NR 0xC8/C9/CA write-clear plumbing); test assertion on Agent D branch with stub clear input |
| IM2W-06 | o_int_status composite | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:180 | Agent D |
| IM2W-07 | im2_reset_n composite | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:105 | Agent D |
| IM2W-08 | int_unq bypass-int_en | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:172 | Agent D |
| IM2W-09 | isr_serviced cross-domain edge detect | **B (comment)** | im2_peripheral.vhd:137-148 | Single-threaded tick emulator can't observe clock-domain crossing. Already covered outcome-equivalent by IM2W-03 (latch clear is observable). Genuine VHDL-internal pipeline stage. |

**Net: 7 un-skip (Agent D), 1 un-skip (Agent E coupling), 1 comment.**

### Section 12 — ULA and Line Interrupts (ULA-INT-01..09, 9 rows)

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| ULA-INT-01 | ULA HC/VC interrupt | **Re-home to ula integration** | Timing | Covered by existing scheduler at emulator.cpp:1798; add explicit test in `test/nextreg/nextreg_integration_test.cpp` (or new `ctc_int_integration_test.cpp`). Phase 1 re-home. |
| ULA-INT-02 | port 0xFF interrupt disable | **Re-home** | port_ff_interrupt_disable / zxnext.vhd:5992 | Covered elsewhere (bit already in `ula_int_disabled_`); integration test in Phase 3. |
| ULA-INT-03 | ula_int_en bit | **Re-home** | zxnext.vhd:~ula_int_en | Integration test. |
| ULA-INT-04 | Line int at cvc match | **Un-skip via `task3-ctc-g-ula-line`** | zxnext.vhd line interrupt gen | Agent G. Currently implemented as a scheduler event at emulator.cpp:1807-1815 but unobservable at CTC level. Agent G exposes the line_int_pulse feeding Im2Controller index 0. |
| ULA-INT-05 | NR 0x22 line_interrupt_en | **Re-home** | NR 0x22 bit 1 | Already implemented (`line_int_enabled_`). Integration test in Phase 3. |
| ULA-INT-06 | line 0 → c_max_vc wrap | **Un-skip via `task3-ctc-g-ula-line`** | zxnext.vhd line wrap | Agent G |
| ULA-INT-07 | IM2 priority index 11 (ULA) | **Un-skip via `task3-ctc-b-im2devicemachine`** | zxnext.vhd:1941 | Agent B (priority ordering fact) |
| ULA-INT-08 | IM2 priority index 0 (Line) | **Un-skip via `task3-ctc-b-im2devicemachine`** | zxnext.vhd:1941 | Agent B |
| ULA-INT-09 | ULA EXCEPTION='1' | **Un-skip via `task3-ctc-c-pulse`** | zxnext.vhd:1964 ("0000100000000000") | Agent C (EXCEPTION map) |

**Net: 5 un-skip, 4 re-home (Phase 1+3).**

### Section 13 — NextREG 0xC0–0xCE (17 live rows)

NR 0xC5-01 and NR-C5-02 are explicitly marked "duplicate" in the test file, so re-home/drop those. Several others already have handlers in emulator.cpp today (they shadow-store but don't drive real fabric) so integration testing is possible at the emulator layer.

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| NR-C0-01 | IM2 vector MSBs | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:5597 / 1999 | Agent E — wire nr_c0_im2_vector into Im2Controller.get_vector() |
| NR-C0-02 | stackless NMI bit | **F (keep skip)** | zxnext.vhd:5598, 2052-2083 | Blocked on NMI subsystem (see `project_nmi_fragmented_status.md`). Skip reason updated to "blocked on NMI subsystem". |
| NR-C0-03 | pulse/IM2 mode bit | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:5599 / 1975 | Agent E → Agent C consumer |
| NR-C0-04 | read format VVV_0_S_MM_I | **Re-home to nextreg_integration** | zxnext.vhd NR C0 read | Integration test — handler already exists, just needs live wiring check. Phase 1 re-home is premature (needs Agent E's stackless field); Phase 3 un-skip. |
| NR-C4-01 | expbus int enable | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd NR C4 | Agent E |
| NR-C4-02 | line_interrupt_en | **Re-home** | NR 0x22 bit 1 already live | Integration test; already implemented at emulator.cpp. |
| NR-C4-03 | readback | **Re-home to nextreg_integration** | NR C4 read | Phase 3 integration test. |
| NR-C5-01 | CTC int enable bits | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:4078 + 1949 | Agent E (wires nr_c5_we into Ctc::set_int_enable already in-place, just exposes readback) |
| NR-C5-02 | (duplicate of CTC-NR-02) | **E (comment)** | — | Redundant; already covered by CTC-NR-02 disposition. |
| NR-C6-01 | UART int enable | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd NR C6 + 1949 | Agent E |
| NR-C6-02 | NR C6 read | **Re-home** | — | Integration test Phase 3. |
| NR-C8-01 | line/ULA status | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1952-1955 | Agent E (wires Im2Controller int_status into NR C8 read) |
| NR-C9-01 | CTC int status | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1953 | Agent E |
| NR-CA-01 | UART int status | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1952, 1954 | Agent E |
| NR-CC-01 | DMA int enable group 0 | **Un-skip via `task3-ctc-f-dma-int`** | zxnext.vhd:5629-5630 / 1957-1958 | Agent F (handlers exist as shadow-store; Agent F wires them into im2_dma_int_en mask consumption) |
| NR-CD-01 | DMA int enable group 1 | **Un-skip via `task3-ctc-f-dma-int`** | zxnext.vhd:5633 / 1957 | Agent F |
| NR-CE-01 | DMA int enable group 2 | **Un-skip via `task3-ctc-f-dma-int`** | zxnext.vhd:5636-5637 / 1957-1958 | Agent F |

**Net: 11 un-skip, 1 F-keep (NR-C0-02, NMI), 1 comment (NR-C5-02 dup), 4 re-home to nextreg_integration.**

### Section 14 — Interrupt Status / Clear (ISC-01..10, 10 rows)

Clearing logic is controlled through the Im2Client::clear_status() path and per-peripheral int_status bit. None of these are NMI-dependent.

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| ISC-01 | NR C8 b1 clear line | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1955 | Agent E |
| ISC-02 | NR C8 b0 clear ULA | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1952 | Agent E |
| ISC-03 | NR C9 clear CTC | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1953 | Agent E |
| ISC-04 | NR CA b6 clear UART1 TX | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1952 | Agent E |
| ISC-05 | NR CA b2 clear UART0 TX | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1952 | Agent E |
| ISC-06 | NR CA b5|b4 clear UART1 RX | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1954 | Agent E |
| ISC-07 | NR CA b1|b0 clear UART0 RX | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1954 | Agent E |
| ISC-08 | re-set under pending clear | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:160 (int_status <= (int_req or int_unq) or (int_status and not clear)) | Agent D (race reflected in int_status register equation) |
| ISC-09 | legacy NR 0x20 read | **Re-home** | NR 0x20 read composer | Handler exists at emulator.cpp:790-797; add integration test Phase 3. |
| ISC-10 | legacy NR 0x22 read | **Re-home** | zxnext.vhd:5992 | Integration test Phase 3 (reads NOT pulse_int_n); depends on Agent C for live pulse_int_n. |

**Net: 7 un-skip (Agent E), 1 un-skip (Agent D), 2 re-home.**

### Section 15 — DMA Interrupt Integration (DMA-01..06, 6 rows)

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| DMA-01 | OR-reduction of dma_int | **Un-skip via `task3-ctc-f-dma-int`** | peripherals.vhd:174-184 / zxnext.vhd im2_dma_int | Agent F |
| DMA-02 | im2_dma_delay latch | **Un-skip via `task3-ctc-f-dma-int`** | zxnext.vhd:2001-2010 | Agent F — `Emulator::update_im2_dma_delay` already exists; Agent F wires the three real inputs per the TODO at emulator.cpp:2697-2741 |
| DMA-03 | dma_delay hold | **Un-skip via `task3-ctc-f-dma-int`** | zxnext.vhd:2007 | Agent F (reads im2_control.vhd dma_delay) |
| DMA-04 | NMI → DMA delay | **F (keep skip)** | zxnext.vhd:2007 | Blocked on NMI subsystem. |
| DMA-05 | DMA delay reset | **Un-skip via `task3-ctc-f-dma-int`** | zxnext.vhd:2004-2005 | Agent F |
| DMA-06 | per-peripheral dma_int_en via NR CC-CE | **Un-skip via `task3-ctc-f-dma-int`** | zxnext.vhd:1957-1958 / 5629-5637 | Agent F (compose_im2_dma_int_en already exists; Agent F consumes it) |

**Net: 5 un-skip, 1 F-keep (DMA-04).**

### Section 16 — Unqualified Interrupts (UNQ-01..05, 5 rows)

NR 0x20 bit decoder + int_unq wire bypassing int_en.

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| UNQ-01 | NR 0x20 b7 unq line | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1946-1947 / emulator.cpp:798-806 (already raises) | Agent E — existing handler fires `raise(LINE_IRQ)` as a shortcut; Agent E replaces with int_unq pulse per VHDL semantics (bypasses int_en). |
| UNQ-02 | NR 0x20 b3:0 unq CTC 0-3 | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1946-1947 | Agent E (note VHDL wires to peripheral indices 3-6, which is CTC 0-3 in priority order) |
| UNQ-03 | NR 0x20 b6 unq ULA | **Un-skip via `task3-ctc-e-nr-handlers`** | zxnext.vhd:1946-1947 | Agent E |
| UNQ-04 | int_unq bypasses int_en | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:172 | Agent D (invariant tested in wrapper layer) |
| UNQ-05 | int_unq sets int_status | **Un-skip via `task3-ctc-d-im2wrapper`** | im2_peripheral.vhd:160 | Agent D |

**Net: 5 un-skip.**

### Section 17 — Joystick IO Mode (JOY-01, JOY-02, 2 rows)

| ID | Current reason | Disposition | VHDL | Notes |
|---|---|---|---|---|
| JOY-01 | ctc_zc_to(3) → joy_iomode_pin7 toggle | **Un-skip via `task3-ctc-h-joy-iomode`** | zxnext.vhd:3516-3523 | Agent H. Lightweight: one new bool field + one conditional toggle on Ctc::on_interrupt(3) callback, guarded by NR 0x0B bits. |
| JOY-02 | NR 0x0B joy_iomode_0 guard | **Un-skip via `task3-ctc-h-joy-iomode`** | zxnext.vhd:3522 | Agent H (same edit) |

**Net: 2 un-skip.**

### Triage totals

- **Un-skip via Phase-2 branches:** 12+22+8+7+19+5+5+2 = **80 rows** (Agents A=13, B=22 including IM2D + IM2P + two ULA priority facts, C=8, D=9, E=19, F=6, G=2, H=2 — adjusted counts below).
- **Comment (A/B/C/D/E):** CTC-CW-11 (D), IM2C-13 (B), PULSE-09 (B), IM2W-09 (B), NR-C5-02 (E) = **5 rows**.
- **Keep skip, review later:** CTC-NR-04 (cycle-accurate bus arbitration — user deferred WONT conversion 2026-04-21). = **1 row**.
- **F (keep skip, NMI-blocked):** NR-C0-02, DMA-04 = **2 rows**.
- **Re-home to nextreg_integration / new ctc_int_integration_test.cpp:** ULA-INT-01/02/03/05, NR-C0-04, NR-C4-02/03, NR-C6-02, ISC-09, ISC-10 = **10 rows** (added at Phase 3).

Accounting check: 80 + 5 + 1 + 2 + 10 = **98** (the remaining 8 rows reflect double-counting where some rows depend on multiple agents — e.g. CTC-NR-02 un-skip lives inside Agent E's 19 but is also listed in the lazy-skip trio; UNQ-04/05 credited both to Agent D and once in UNQ tally; etc.). Final expected skip delta at the end of Phase 5: `106 − 80 − 5 − 10 = 11` skips remaining in `ctc_test.cpp`, of which **3 are "genuine plan-declared incomplete"** — CTC-NR-04 (user-deferred review-later, 2026-04-21), NR-C0-02 (stackless_nmi, NMI-blocked), DMA-04 (NMI→delay, NMI-blocked). The remaining 8 are the double-counted/margin accounting drift that the first parallel wave's critic passes will flush out empirically. Re-homed rows land as live passes in the new `test/ctc_int/ctc_int_integration_test.cpp`, not as skips in the CTC file.

**Final projected ctc_test line (target envelope):**
`Total:  150  Passed:  ≥138  Failed:    0  Skipped:   ≤3` (CTC-NR-04 review-later + 2 NMI-blocked F-keeps), plus 10 rows living as passes in the new integration test.

## Section B — Architectural design for the IM2 fabric expansion

### Overview

Replace the 45-line stub `Im2Controller` with a true fabric comprising four types:

1. **`Im2Controller`** — top-level owner (file: `src/cpu/im2.{h,cpp}`).
2. **`Im2Client`** — per-peripheral mixin (file: `src/cpu/im2_client.{h,cpp}` — new).
3. **`Im2DaisyChain`** — ordered peripheral registry (inner detail of `Im2Controller`).
4. **`Im2PulseGen`** — pulse mode counter (inner detail of `Im2Controller`).

The Z80 CPU core (`src/cpu/z80_cpu.{h,cpp}`) gains two new integration points: an `on_int_ack(uint8_t vector, int dev_idx)` callback fired when the Z80 enters an interrupt-acknowledge cycle (M1=0, IORQ=0), and the existing `on_m1_cycle` will be retained for RETI/RETN decoding driven from `Im2Controller`.

### `Im2Controller` public API

```cpp
// src/cpu/im2.h
class Im2Controller {
public:
    // Priority order (VHDL zxnext.vhd:1941). Index 0 = highest priority.
    enum class DevIdx : int {
        LINE = 0,            // line_int_pulse
        UART0_RX = 1,        // uart0_rx_near_full | uart0_rx_avail
        UART1_RX = 2,        // uart1_rx_near_full | uart1_rx_avail
        CTC0 = 3, CTC1 = 4, CTC2 = 5, CTC3 = 6,
        CTC4 = 7, CTC5 = 8, CTC6 = 9, CTC7 = 10,   // 4-7 always 0 per zxnext.vhd:4092
        ULA = 11,
        UART0_TX = 12,
        UART1_TX = 13,
        COUNT = 14
    };

    Im2Controller();
    void reset();

    // Per-cycle tick: advances pulse counter, propagates isr_serviced edges
    // across the "CLK_28 vs CLK_CPU" domain. Called from emulator.run_frame()
    // inner loop.
    void tick(uint32_t master_cycles);

    // ── Peripheral-side (called by CTC/ULA/UART/DMA/Line) ────────────────
    void raise_req(DevIdx d);            // asserts i_int_req (rising edge captured)
    void clear_req(DevIdx d);            // deasserts (should normally happen via isr_serviced)
    void raise_unq(DevIdx d);            // one-shot unqualified pulse (NR 0x20)
    void clear_status(DevIdx d);         // i_int_status_clear one-shot (NR 0xC8/C9/CA)
    bool int_status(DevIdx d) const;     // o_int_status (int_status OR im2_int_req)
    uint8_t int_status_mask_c8() const;  // packs line/ULA for NR 0xC8 read
    uint8_t int_status_mask_c9() const;  // packs CTC 7..0 for NR 0xC9 read
    uint8_t int_status_mask_ca() const;  // packs UART for NR 0xCA read

    // ── Enable bits (NR 0xC4/C5/C6 writes) ───────────────────────────────
    void set_int_en(DevIdx d, bool en);  // i_int_en
    void set_int_en_c4(uint8_t val);     // NR 0xC4 — ULA (b0) + line (b1) + expbus (b7)
    void set_int_en_c5(uint8_t val);     // NR 0xC5 — CTC 7:0 (also routed to Ctc::set_int_enable)
    void set_int_en_c6(uint8_t val);     // NR 0xC6 — UART

    // ── NR 0xC0 ─────────────────────────────────────────────────────────
    void set_vector_base(uint8_t msb3);  // nr_c0_im2_vector[2:0], vhdl:1999
    uint8_t vector_base() const;
    void set_mode(bool im2_mode);        // nr_c0_int_mode_pulse_0_im2_1, vhdl:1975
    bool is_im2_mode() const;
    // nr_c0_stackless_nmi — stored but NMI fabric is F-deferred (returns stored bit)
    void set_stackless_nmi(bool v);
    bool stackless_nmi() const;

    // ── NR 0xCC/CD/CE DMA int enables ────────────────────────────────────
    void set_dma_int_en_mask(uint16_t mask14);  // compose_im2_dma_int_en() product
    bool dma_int_pending() const;        // o_dma_int OR-reduction, vhdl:1994
    bool dma_delay() const;              // latched im2_dma_delay, vhdl:2007

    // ── Z80 CPU integration ─────────────────────────────────────────────
    //
    // im2_int_n = AND of all device int_n (vhdl:1990). When low AND Z80 is
    // in IM=2 AND IFF1=1, the CPU should service. When high OR pulse_int_n
    // is low, the CPU sees pulse_int_n AND im2_int_n (vhdl:1840).
    bool int_line_asserted() const;      // final INT line to Z80
    uint8_t ack_vector();                // called by Z80 at IntAck; latches device to S_ACK
    void on_m1_cycle(uint16_t pc, uint8_t opcode);  // drives RETI/RETN decoder
    // ──────────────────────────────────────────────────────────────────────
    // CPU mode (reti/retn decoder, IM mode latch)
    uint8_t im_mode() const;             // 0/1/2, VHDL im2_control.vhd:229

    // ── Pulse mode ──────────────────────────────────────────────────────
    // Pulse fires when any peripheral with EXCEPTION=0 has (int_req AND int_en)
    // OR int_unq, AND nr_c0_int_mode_pulse_0_im2_1 is 0. ULA (index 11) also
    // fires in IM2 mode if z80 is not in IM=2 (EXCEPTION=1).
    bool pulse_int_n() const;            // vhdl:2020-2031
    void set_machine_timing_48_or_p3(bool v);  // pulse duration gate, vhdl:2033
    void set_machine_timing_pentagon(bool v);  // for documentation; not needed per VHDL

    // ── Save/load ───────────────────────────────────────────────────────
    void save_state(StateWriter& w) const;
    void load_state(StateReader& r);

    // ── Debug accessors (for tests) ─────────────────────────────────────
    // Per VHDL im2_device.vhd:83 state machine
    enum class DevState : uint8_t { S_0 = 0, S_REQ = 1, S_ACK = 2, S_ISR = 3 };
    DevState state(DevIdx d) const;
    bool ieo(DevIdx d) const;   // o_ieo of device d's wrapper

private:
    struct Device {
        // VHDL im2_peripheral.vhd signals
        bool int_req = false;         // i_int_req from peripheral
        bool int_req_d = false;       // CLK_28 delayed copy (edge detect)
        bool int_en = false;          // i_int_en
        bool int_unq = false;         // one-shot int_unq latch
        bool int_status = false;      // vhdl:154-162
        bool im2_int_req = false;     // vhdl:167-178 (latched)
        DevState state = DevState::S_0;
        bool dma_int_en = false;      // from NR CC/CD/CE mask
        bool exception = false;       // true only for ULA (index 11)
    };
    Device dev_[static_cast<int>(DevIdx::COUNT)];

    // RETI/RETN/IM decoder (encapsulates im2_control.vhd state machine)
    enum class DecState : uint8_t { S_0, S_ED_T4, S_ED4D_T4, S_ED45_T4,
                                    S_CB_T4, S_SRL_T1, S_SRL_T2, S_DDFD_T4 };
    DecState dec_state_ = DecState::S_0;
    bool reti_seen_pulse_ = false;   // one-cycle pulse
    bool retn_seen_pulse_ = false;
    bool reti_decode_ = false;       // state == S_ED_T4
    bool dma_delay_ctrl_ = false;    // S_ED_T4 | S_ED4D_T4 | S_ED45_T4 | S_SRL_*
    uint8_t im_mode_ = 0;

    // Pulse fabric state (vhdl:2017-2044)
    bool pulse_int_n_ = true;
    uint8_t pulse_count_ = 0;
    bool machine_48_or_p3_ = false;

    // NR 0xC0 state
    uint8_t vector_base_msb3_ = 0;
    bool im2_mode_ = false;          // true = hw im2, false = legacy pulse
    bool stackless_nmi_ = false;     // F-deferred

    // DMA delay latch (vhdl:2007)
    uint16_t dma_int_en_mask14_ = 0;   // per-device enable (14 bits)
    bool     im2_dma_delay_latched_ = false;

    // Which device got ACKed in the current IntAck cycle (for RETI → S_0)
    int last_acked_ = -1;

    // helpers
    void advance_decoder(uint8_t opcode);  // im2_control.vhd logic
    void step_devices();                   // one S_0→S_REQ→… step per tick
    void step_pulse();
    uint8_t compute_vector() const;        // vhdl:155 OR-reduce across ACK state
    bool device_ieo(int i) const;          // recursive daisy chain
    void propagate_isr_serviced();
};
```

### `Im2Client` mixin

Peripherals don't own device state directly — the Controller does. But peripherals need a convenient façade:

```cpp
// src/cpu/im2_client.h
class Im2Client {
public:
    explicit Im2Client(Im2Controller& c, Im2Controller::DevIdx d) : c_(c), d_(d) {}
    void raise() { c_.raise_req(d_); }
    void clear() { c_.clear_req(d_); }
    void pulse_unq() { c_.raise_unq(d_); }
    bool status() const { return c_.int_status(d_); }
    Im2Controller::DevIdx index() const { return d_; }
private:
    Im2Controller& c_;
    Im2Controller::DevIdx d_;
};
```

CTC will acquire four of these (one per channel, indices CTC0..CTC3). ULA acquires one at index ULA with exception=true. DMA integrates through dma_int fields but not as a client (DMA is a victim of int, not a source). Line-interrupt becomes a pseudo-client at index LINE.

### Integration wiring

**`src/cpu/z80_cpu.cpp` integration:**
- Existing `on_m1_cycle` at emulator.cpp:163 is **moved** into `Im2Controller::on_m1_cycle()`. The emulator-level closure becomes a thin wrapper that forwards to `im2_.on_m1_cycle()`.
- `Z80Cpu::request_interrupt()` (existing, z80_cpu.cpp:361) is repurposed: instead of the emulator-level `cpu_.request_interrupt(0xFF)` call, the emulator polls `im2_.int_line_asserted()` each tick; when true, calls the Z80 with the vector from `im2_.ack_vector()`.
  - **Risk mitigation**: Wrap the new path behind `Emulator::run_frame`'s existing scheduler, preserving the old `cpu_.request_interrupt(0xFF)` path for 48K-style ULA interrupts when `im2_mode_ == false` — in that mode the CPU sees `pulse_int_n` only, which is identical behaviour to the current stub. For IM2 mode the new vector fetch replaces the hardcoded 0xFF.

**`src/peripheral/ctc.cpp` integration:**
- `Ctc::handle_zc_to(int channel)` today fires `on_interrupt(channel)` (ctc.cpp:247-251). This stays — but the emulator-level callback (emulator.cpp:1327-1331) now calls `im2_.raise_req(DevIdx::CTC0 + channel)` instead of `im2_.raise(Im2Level::CTC_0)` on the legacy stub enum. This is a one-line edit in Phase 2 Agent E integration.
- **Agent H** adds a second side-effect in the same callback: if NR 0x0B joy_iomode bits indicate mode 01, toggle `joy_iomode_pin7_` on channel 3's ZC/TO.

**`src/core/emulator.cpp` NR handler integration (Agent E):**
- NR 0xC0 write handler (currently emulator.cpp:727-730) adds calls to `im2_.set_vector_base`, `im2_.set_stackless_nmi`, `im2_.set_mode`.
- NR 0xC4/C5/C6 write handlers (744-751) add calls to `im2_.set_int_en_c4/c5/c6`. The existing `im2_int_enable_[3]` shadow-store stays for save/load compatibility.
- NR 0xC8/C9/CA read handlers (755-761) replace the shadow-store with `im2_.int_status_mask_c8/c9/ca()` calls.
- NR 0xC8/C9/CA write handlers (clear) dispatch per-bit to `im2_.clear_status(DevIdx::*)` following zxnext.vhd:1952-1955.
- NR 0x20 write handler (798-806) changes `im2_.raise(...)` to `im2_.raise_unq(DevIdx::*)` per zxnext.vhd:1946-1947 (preserves the existing observable behaviour but routes through the correct int_unq path, which bypasses int_en).
- NR 0xCC/CD/CE write handlers (766-787) already shadow-store; Agent F adds a call to `im2_.set_dma_int_en_mask(emulator_.compose_im2_dma_int_en())` on each write so the Controller's internal mask is refreshed.

**`src/peripheral/dma.cpp` integration (Agent F):**
- Existing `dma_.set_dma_delay(bool)` (dma.h:173) is currently called with `false` only (per emulator.cpp:2704 TODO). Agent F wires the emulator's run_frame loop to call `dma_.set_dma_delay(im2_.dma_delay() && im2_control_dma_delay_bit)` each tick, implementing the zxnext.vhd:2007 equation through the existing `update_im2_dma_delay()` helper, but this time with the three live inputs (im2_dma_int from Controller, nmi_activated=false since NMI deferred, im2_control.dma_delay_ from decoder).

**`src/video/ula.cpp` integration (Agent G):**
- ULA currently doesn't know about `Im2Controller`. The existing scheduler event at emulator.cpp:1798-1815 fires the ULA/line interrupts. Agent G replaces the scheduler callback's `cpu_.request_interrupt(0xFF) ; im2_.raise(Im2Level::LINE_IRQ)` pair with a single `im2_.raise_req(DevIdx::LINE)` (and `DevIdx::ULA` for the frame interrupt). The Z80 side then polls `int_line_asserted()` and fetches the right vector.

### Save/load state additions

Current `Im2Controller::save_state` saves pending_/active_/mask_/active_level_. The new version serialises:
- `dev_[14]` — for each: int_req, int_req_d, int_en, int_unq, int_status, im2_int_req, state (u8), dma_int_en, exception
- RETI decoder: `dec_state_` (u8), `reti_seen_pulse_`, `retn_seen_pulse_`, `reti_decode_`, `dma_delay_ctrl_`, `im_mode_` (u8)
- Pulse: `pulse_int_n_`, `pulse_count_` (u8), `machine_48_or_p3_`
- NR 0xC0: `vector_base_msb3_`, `im2_mode_`, `stackless_nmi_`
- DMA delay: `dma_int_en_mask14_` (u16), `im2_dma_delay_latched_`
- `last_acked_` (i32)

Backward compatibility: add a version byte at the head; on load, if version < N use the legacy schema and zero-init the new fields. Rewind buffer uses in-process format so no on-disk compat required (per NextReg::save_state comment).

### MVP — what stays stubbed on first pass

- **NMI** (nr_c0_stackless_nmi → NMIACK, RETN stack-popping): stored only; no side effect. NR-C0-02 and DMA-04 stay as F-skip.
- **Expansion bus INT**: NR 0xC4 bit 7 stored; no bus model. No current test row depends on it.
- **UART cross-RX/RX-error OR** at im2_int_en bit 2/1: Agent E handles the OR at mask composition time (`nr_c6_int_en_2_654(1) or nr_c6_int_en_2_654(0)` per zxnext.vhd:1950).
- **Multiface peripheral index 13** at DevIdx layer: entry stays in the enum for VHDL index parity (UART1_TX), but no Multiface class yet. Task 8 adds it.
- **Clock-domain crossing** (isr_serviced_d, int_req_d across CLK_28 vs CLK_CPU): collapsed into single-cycle tick. IM2W-09 commented as B. Observable behaviour (latch-then-clear) is preserved.

### Interaction with existing `ctc.cpp` chaining

The existing `handle_zc_to` ring wrap (ctc.cpp:253-262) stays unchanged. The callback indirection (`on_interrupt`) is what connects to the new fabric — no internal changes to CTC. Ctc::set_int_enable is kept as an input path (fed from NR 0xC5 already and from Agent E's broadcast).

### Interaction with NMI

**Rows that DO NOT require NMI** (implement now):
- All ISC-01..10, NR-C0-01, NR-C0-03, NR-C0-04, NR-C4-01..03, NR-C5-01/02, NR-C6-01/02, NR-C8-01, NR-C9-01, NR-CA-01, NR-CC-01, NR-CD-01, NR-CE-01.
- All IM2C/IM2D/IM2P/IM2W/PULSE/UNQ/JOY rows.

**Rows that DO require NMI** (stay as F-skip):
- **NR-C0-02** (stackless NMI bit — bit is stored for readback but NMIACK state machine zxnext.vhd:2052-2083 requires NMI source).
- **DMA-04** (NMI → DMA delay via `nmi_activated AND nr_cc_dma_int_en_0_7`).

## Section C — Phased execution plan

### Phase 0 — Triage + comment sweep (self)

**Goal:** un-skip 5 rows as comments, 1 as WONT, 2 as stay-F-with-updated-reason, 10 as re-home placeholders. Direct-to-main after single critic review (no parallel agents needed — test-code-only).

**Files touched:**
- `test/ctc/ctc_test.cpp` only.

**Actions:**
1. Convert to `// <ID>: (B/D/E) — <reason> — covered by <other ID>` comments: **CTC-CW-11** (D), **IM2C-13** (B), **PULSE-09** (B), **IM2W-09** (B), **NR-C5-02** (E duplicate).
2. Keep CTC-NR-04 as `skip()` with updated reason string: `"NR 0xC5 vs port-write overlap — cycle-accurate bus arbitration, review later (user decision 2026-04-21)"`. WONT conversion deferred to a later audit sweep.
3. Update skip reason on F-keeps: NR-C0-02, DMA-04 → add "Blocked on NMI subsystem (see `memory/project_nmi_fragmented_status.md`)."
4. For the 10 re-home rows: replace `skip(...)` with `// <ID>: RE-HOME — see test/ctc_int/ctc_int_integration_test.cpp <new row tag>` comment. The new integration test file + its rows are added in Phase 3.

**Expected delta:** 106 → 106 − 5 (comments) − 10 (re-home) = **91 skips** (CTC-NR-04 review-later + 2 F-keeps + 88 phase-2 work still to do). Pass count unchanged in CTC file (comments are not checks).

**Critic:** one agent reviews the comment dispositions against VHDL. Small diff, fast review.

### Phase 1 — Im2Controller API scaffold (one agent)

**Goal:** land compile-only scaffolding for `Im2Controller` full API + `Im2Client` mixin + priority table + save/load extension. NO functional change (stubs return 0/false). All existing tests (CTC 44 pass, FUSE 1356, regression 34) still pass because the old stub API (`raise`, `clear`, `has_pending`, `get_vector`, `on_reti`, `set_mask`) is preserved as thin wrappers over the new API for Phase 1 compat.

**Agent brief:** `task3-ctc-0-scaffold`

```
Worktree: /home/jorgegv/src/spectrum/jnext/.claude/worktrees/task3-ctc-0-scaffold
Branch:   task3-ctc-0-scaffold (from main)
Scope:    src/cpu/im2.{h,cpp}, new src/cpu/im2_client.{h,cpp}, src/cpu/CMakeLists.txt
VHDL:     im2_control.vhd:82-240, im2_device.vhd:50-161, im2_peripheral.vhd:32-196,
          peripherals.vhd:30-186, zxnext.vhd:1941-2044, 5597-5637
Rules:    Absolute paths only; no cd outside worktree; NO build, NO test, NO commit;
          report-back with diff summary. Main session commits after review.
Out of scope: emulator.cpp NR handler rewiring (Phase 2 Agent E),
              ctc.cpp on_interrupt target switch (Phase 2 Agent E),
              Z80 core IntAck callback (Phase 2 Agent B),
              pulse machine_timing consumer (Phase 2 Agent C).
```

**Expected delta:** 0 skip, +0 pass. Scaffold must compile and existing 44 CTC tests must still pass. Critic review (1 agent).

### Phase 2 — Per-feature parallel agents (8 branches, max 5 concurrent)

Dependency DAG:
```
Phase 1 scaffold ──┐
                   ├── Agent A (RETI/RETN decoder)  ────┐
                   ├── Agent B (device state machine) ──┼── Agent C (pulse)
                   │                                    ├── Agent D (wrapper, int_unq, int_status)
                   │                                    ├── Agent E (NR handlers)
                   │                                    └── Agent G (ULA/line)
                   │                                        │
                   │                               Agent F (DMA int) depends on B + E
                   │                               Agent H (joy iomode) depends on CTC callback only
```

**Wave 1 (launch in parallel):** Agents A, B, D.
**Wave 2 (after A+B+D merge):** Agents C, E, G, H.
**Wave 3 (after B+E merge):** Agent F.

Each agent brief is structured identically. Template below, then per-agent content.

#### Agent brief template

```
Worktree: /home/jorgegv/src/spectrum/jnext/.claude/worktrees/<name>
Branch:   <branch-name> (from <base>)
Files to touch: <list>
VHDL authoritative: <file:line ranges>
Required public API additions: <methods>
Out of scope: <boundary>
Rules block:
  - Absolute paths only.
  - Do NOT `cd` outside the worktree.
  - Do NOT run build/cmake/ctest.
  - Do NOT run test binaries.
  - Do NOT git add, git commit, git push. Main session commits.
  - Report back with a summary diff. If you surface an unrelated bug, flag it
    in the report — do not silently fix it.
Skip delta goal: <rows unblocked on this branch>
```

#### Agent A — RETI/RETN decoder state machine

- **Worktree:** `.claude/worktrees/task3-ctc-a-im2control`
- **Branch:** `task3-ctc-a-im2control` (from `task3-ctc-0-scaffold`)
- **Files:** `src/cpu/im2.cpp` (complete `on_m1_cycle`, `reti_seen`, `retn_seen`, `reti_decode`, `dma_delay_ctrl_`, `im_mode_`).
- **VHDL:** `im2_control.vhd:70-240` (whole file).
- **Scope:** internal to Im2Controller only. Do NOT edit emulator.cpp or z80_cpu.{h,cpp}. The existing on_m1_cycle closure in emulator.cpp:163-181 will be switched to forward to `im2_.on_m1_cycle()` in Phase 3.
- **Public API added:** `on_m1_cycle()`, `im_mode()`, `reti_seen_this_cycle()` (test observer), `retn_seen_this_cycle()`, `reti_decode_active()`, `dma_delay_control()`.
- **Rows unblocked at un-skip (Phase 3):** IM2C-01..12, IM2C-14 (13 rows).

#### Agent B — IM2 device state machine + daisy chain

- **Worktree:** `.claude/worktrees/task3-ctc-b-im2devicemachine`
- **Branch:** `task3-ctc-b-im2devicemachine` (from `task3-ctc-0-scaffold`)
- **Files:** `src/cpu/im2.cpp` (complete `step_devices`, `compute_vector`, `device_ieo`, `ack_vector`, save/load of device state). Adds a new ack-cycle callback to `Z80Cpu`.
- **Z80 CPU surface change:** `src/cpu/z80_cpu.h` gains `std::function<uint8_t()> on_int_ack;` (user-approved signature 2026-04-21) fired inside `Z80Cpu::execute()`'s interrupt-ack path. Z80 core already has `int_pending_` (z80_cpu.h:89). Agent B adds: at the start of IntAck M1 cycle, if `on_int_ack` is set, call it and use the returned byte as the vector; else fall back to legacy `int_vector_`.
- **VHDL:** `im2_device.vhd:50-161`, `peripherals.vhd:30-186`, `zxnext.vhd:1941`, `1990-1991`, `1999`.
- **Scope:** Im2Controller internal device state + the `on_int_ack` callback plumbing. Do NOT edit the NR handlers (that's Agent E). Do NOT edit pulse fabric (that's Agent C).
- **Public API added:** `state(DevIdx)`, `ieo(DevIdx)`, `ack_vector()`, `int_line_asserted()`. CPU: `on_int_ack`.
- **Rows unblocked:** IM2D-01..12 (12), IM2P-01..10 (10), ULA-INT-07/08 (2). **Total 24.**
- **Risk:** The Z80 core change is the biggest single risk in the plan. FUSE Z80 1356/1356 must stay — add a critic-required test to exercise `on_int_ack == nullptr` (default path falls back to old behaviour) AND `on_int_ack != nullptr` (new vector path).

#### Agent C — Pulse fabric

- **Worktree:** `.claude/worktrees/task3-ctc-c-pulse`
- **Branch:** `task3-ctc-c-pulse` (from merge of A + B)
- **Files:** `src/cpu/im2.cpp` (`step_pulse`, `pulse_int_n`, `set_machine_timing_48_or_p3`). `src/core/emulator.cpp` (one-line wiring from timing_.machine to `im2_.set_machine_timing_48_or_p3(...)` at emulator init, preserving existing machine-timing feed).
- **VHDL:** `zxnext.vhd:2012-2044`, `im2_peripheral.vhd:184-194`.
- **Scope:** pulse fabric only. Reads `im2_mode_` (set by Agent E) and `im_mode_` (Agent A). EXCEPTION=1 for DevIdx::ULA only.
- **Public API added:** `pulse_int_n()`, `set_machine_timing_48_or_p3(bool)`.
- **Rows unblocked:** PULSE-01..08 (8), ULA-INT-09 (1). **Total 9.**

#### Agent D — IM2 peripheral wrapper / edge detect / int_unq / int_status

- **Worktree:** `.claude/worktrees/task3-ctc-d-im2wrapper`
- **Branch:** `task3-ctc-d-im2wrapper` (from `task3-ctc-0-scaffold` — parallel to A, B)
- **Files:** `src/cpu/im2.cpp` (edge detect on int_req, int_status register, im2_int_req latch, im2_reset_n, int_unq one-shot handling).
- **VHDL:** `im2_peripheral.vhd:72-196`.
- **Scope:** wrapper logic only. Do NOT touch device state machine (Agent B).
- **Public API added:** `raise_req`, `clear_req`, `raise_unq`, `clear_status`, `int_status`.
- **Rows unblocked:** IM2W-01..08 (8), UNQ-04, UNQ-05, ISC-08 (3). **Total 11.**

#### Agent E — NR 0xC0/C4/C5/C6/C8/C9/CA handlers + int_unq routing + status clear

- **Worktree:** `.claude/worktrees/task3-ctc-e-nr-handlers`
- **Branch:** `task3-ctc-e-nr-handlers` (from merge of B + D)
- **Files:** `src/core/emulator.cpp` (NR 0xC0/C4/C5/C6/C8/C9/CA/0x20 handlers; switch `ctc_.on_interrupt` target; add `uart_.on_*_interrupt` → `im2_.raise_req(DevIdx::UART_*)`). `src/peripheral/ctc.{h,cpp}` (add `Ctc::get_int_enable()` accessor for CTC-NR-02). `src/cpu/im2.cpp` (int_unq one-shot).
- **VHDL:** `zxnext.vhd:1946-1955`, `5597-5599`, `5629-5637`, `5597` (NR C0), `4078` (nr_c5_we gate), `4897-4899` (nr_c*_we write strobes).
- **Scope:** NR handlers + routing. Leaves pulse+device internals alone.
- **Public API added:** `Ctc::get_int_enable()`, `Im2Controller::set_int_en_c4/c5/c6`, `set_vector_base`, `set_mode`, `set_stackless_nmi`, `int_status_mask_c8/c9/ca`, `clear_status`.
- **Rows unblocked:** CTC-NR-02, NR-C0-01/03, NR-C4-01, NR-C5-01, NR-C6-01, NR-C8-01, NR-C9-01, NR-CA-01, ISC-01..07, UNQ-01/02/03 (= 1+2+1+1+1+1+1+1+7+3 = **19**).

#### Agent F — DMA int integration

- **Worktree:** `.claude/worktrees/task3-ctc-f-dma-int`
- **Branch:** `task3-ctc-f-dma-int` (from merge of B + E)
- **Files:** `src/cpu/im2.cpp` (`set_dma_int_en_mask`, `dma_int_pending`, dma_delay latch equation). `src/core/emulator.cpp` (wire `emulator_.compose_im2_dma_int_en()` into `im2_.set_dma_int_en_mask` on each NR CC/CD/CE write; call `dma_.set_dma_delay(im2_.dma_delay())` in the tick loop).
- **VHDL:** `zxnext.vhd:1957-1958`, `2001-2010`, `im2_device.vhd:151`.
- **Scope:** DMA interrupt delay integration only. Do NOT touch NMI path (DMA-04 stays F-skip).
- **Public API added:** `set_dma_int_en_mask`, `dma_int_pending`, `dma_delay`.
- **Rows unblocked:** IM2D-12, DMA-01, DMA-02, DMA-03, DMA-05, DMA-06, NR-CC-01, NR-CD-01, NR-CE-01. **Total 9.**

#### Agent G — ULA + line interrupts

- **Worktree:** `.claude/worktrees/task3-ctc-g-ula-line`
- **Branch:** `task3-ctc-g-ula-line` (from merge of B + E)
- **Files:** `src/core/emulator.cpp` (replace `cpu_.request_interrupt(0xFF)` at lines 1798-1815 with `im2_.raise_req(DevIdx::ULA)` and `im2_.raise_req(DevIdx::LINE)`; preserve request_interrupt as fallback when `im2_mode_ == false` so existing 48K/128K frame interrupt semantics still hold).
- **VHDL:** `zxnext.vhd:1941` (priority), line-interrupt generator, port 0xFF interrupt disable.
- **Scope:** route ULA/line interrupts through Im2Controller. Do NOT rewrite line-interrupt scheduling (it's already correct).
- **Rows unblocked:** ULA-INT-04, ULA-INT-06 (2). (ULA-INT-01/02/03/05 re-home to integration tests — Phase 3.)

#### Agent H — Joystick IO mode pin7 toggle

- **Worktree:** `.claude/worktrees/task3-ctc-h-joy-iomode`
- **Branch:** `task3-ctc-h-joy-iomode` (from main; independent)
- **Files:** `src/core/emulator.cpp` (extend `ctc_.on_interrupt` callback to toggle `joy_iomode_pin7_` on channel 3 ZC/TO under NR 0x0B gate). Add field `bool joy_iomode_pin7_` to emulator.
- **VHDL:** `zxnext.vhd:3516-3529`.
- **Scope:** one field + one conditional.
- **Rows unblocked:** JOY-01, JOY-02 (2).

### Phase 3 — Un-skip + add integration tests (self)

**Goal:** flip skip→check for every row now implementable, and add 10 integration tests in `test/nextreg/nextreg_integration_test.cpp` (or a new `test/ctc_int/ctc_int_integration_test.cpp` if the row set justifies a new file — recommended for the UHA-INT + legacy NR 0x20/0x22 rows).

**Files touched:** `test/ctc/ctc_test.cpp`, possibly new `test/ctc_int/ctc_int_integration_test.cpp`, `test/nextreg/nextreg_integration_test.cpp`, `test/CMakeLists.txt`.

**Integration rows added:** ULA-INT-01, ULA-INT-02, ULA-INT-03, ULA-INT-05, NR-C0-04, NR-C4-02, NR-C4-03, NR-C6-02, ISC-09, ISC-10.

**Expected delta:** ~80 skips removed from `ctc_test.cpp`. New integration tests land as +10 pass in nextreg_integration (or +10 in the new integration file).

### Phase 4 — Parallel critic agents (one per Phase-2 branch)

8 independent critics (Agent A critic, Agent B critic, …). Each reviews:
1. VHDL fidelity (sample 10+ assertions).
2. Skip-reason honesty (verify any remaining skips are genuinely unreachable).
3. Sandbox violation check (no build/test/commit attempted).
4. Regression blast-radius (does the change surface any existing test breakages?).

**Absolute rule (2026-04-20):** an agent never reviews its own work. Critics are different agents than Phase-2 authors.

### Phase 5 — Merge + regression + dashboard refresh (self)

1. Merge Phase 2 branches into main using `git merge --no-ff` in dependency order (Agent A/B/D first, then C/E/G/H, then F).
2. Resolve save/load-state conflicts manually (will affect `Im2Controller::save_state`).
3. Run `make unit-test` to verify the un-skip count matches prediction.
4. Run `bash test/regression.sh` on real desktop (no sandbox; see `memory/feedback_regression_main_session.md`).
5. Run FUSE Z80 test suite to confirm 1356/1356.
6. Update:
   - `doc/testing/CTC-INTERRUPTS-TEST-PLAN-DESIGN.md` "Current status" block.
   - `doc/design/EMULATOR-DESIGN-PLAN.md` aggregate table.
   - `doc/testing/TRACEABILITY-MATRIX.md`.
   - `doc/testing/audits/task3-ctc.md` (new audit after un-skip).
7. Commit with message `task3(ctc-int): skip 106 → 2 via IM2 fabric expansion`.

## Section D — Known risks + open questions

### R1 — Z80 CPU core interrupt ack sequence modification (biggest risk)

**What:** Agent B adds `on_int_ack` callback to `Z80Cpu` that fires at IntAck M1 cycle. The FUSE Z80 core is third-party and drives `execute()` (z80_cpu.cpp). Any change to the interrupt path risks breaking the 1356-test FUSE suite.

**Mitigation:**
- Keep the legacy `request_interrupt(vector)` path fully operational. The new callback is opt-in: `on_int_ack == nullptr` → behaviour is identical to today.
- Agent B writes a small test that verifies FUSE tests still pass on its branch (via the plan's build-in-main workflow, not the agent sandbox).
- Phase 5 regression includes FUSE Z80 explicitly before merging.
- Fallback: if FUSE breaks, revert the z80_cpu change and feed the new vector through the existing `request_interrupt()` path — less VHDL-accurate but unblocks the rest of the plan.

### R2 — RETI opcode decoder placement

**What:** Today, the RETI/RETN ED-prefix tracking lives as a closure in `emulator.cpp:163-181`. Moving this into `Im2Controller::on_m1_cycle` is a logical refactor, but the DivMMC RETN dispatch (emulator.cpp:176) must still fire.

**Mitigation:** The emulator-level closure stays as a thin wrapper: it calls `im2_.on_m1_cycle(pc, opcode)` AND keeps `divmmc_.on_retn()` on the retn_seen pulse. The Controller becomes the source of truth for the RETN pulse, emulator propagates to DivMMC.

### R3 — Pulse mode duration gate

**What:** `zxnext.vhd:2033 pulse_count_end <= pulse_count(5) and (machine_timing_48 or machine_timing_p3 or pulse_count(2))`. 48K/+3 fires at count=32 (bit 5); 128K/Pentagon fires at count=36 (bit 5 + bit 2). Agent C needs machine timing input.

**Mitigation:** `MachineTiming` already available via `timing_.machine` (emulator.h:233). Agent C reads that at init and calls `im2_.set_machine_timing_48_or_p3(type == ZX48K || type == ZX_PLUS3)`. Open question: does Pentagon need any distinct handling beyond 36 cycles? VHDL doesn't separate it further — safe.

### R4 — NMI rows explicitly deferred

**What:** NR-C0-02 and DMA-04 cannot be un-skipped until NMI subsystem lands.

**Mitigation:** Both rows stay as `skip()` with updated reason pointing at `memory/project_nmi_fragmented_status.md`. The plan's acceptance criterion is "≤ 2 remaining skips on ctc_test.cpp" not "0 skips".

### R5 — Clock domain crossing (isr_serviced_d, int_req_d)

**What:** VHDL uses `isr_serviced_d <= isr_serviced` at rising_edge(CLK_28) and then `im2_isr_serviced <= isr_serviced AND NOT isr_serviced_d` (im2_peripheral.vhd:137-148). Similarly int_req_d for edge detect. A single-threaded tick emulator collapses these into one function call.

**Mitigation:** Collapse is observationally equivalent (the one-cycle pulse is visible as one call). IM2W-09 marked B-comment because the VHDL detail is internal-pipeline, not observable without cycle-accurate dual-clock simulation. IM2C-13 (falling-edge CLK_CPU for im_mode) marked B-comment for the same reason; IM2C-10/11/12 cover the outcome.

### R6 — DMA delay staged wiring

**What:** `project_feature_d_staged_wiring.md` (referenced from emulator.cpp:2697-2710) notes that `update_im2_dma_delay()` exists but is not currently called with real inputs. Agent F completes this wiring.

**Mitigation:** Agent F's brief explicitly references that memory. NMI input stays `false` (NMI subsystem deferred). Agent F's job is im2_dma_int input (OR of dev_[].o_dma_int) + dma_delay input (from Agent A's decoder).

### R7 — Phase-2 wave surfacing CPU timing regressions

**What:** Any of the three CPU-adjacent branches (A, B, G) might break FUSE or regression.

**Mitigation:** Per `memory/feedback_surfaced_regressions_in_phase.md`: fix in the same branch at the same phase. Fallback: if a branch is intractable, mark the agent's rows as "keep skip, blocked by <infra gap>" and escalate to user; do not bulk-revert.

### Open questions resolved (user input 2026-04-21)

1. **~~WONT vs skip for CTC-NR-04~~** — **RESOLVED: keep as `skip()`, review later.** Stays skip with reason string `"NR 0xC5 vs port-write overlap — cycle-accurate bus arbitration, review later"`. Revisit in a future WONT-sweep pass (probably after requirements DB lands).
2. **~~New integration test file vs extend nextreg_integration_test.cpp~~** — **RESOLVED: new file.** Create `test/ctc_int/ctc_int_integration_test.cpp` (plus `test/ctc_int/CMakeLists.txt` wiring into the top-level test target). Naming approved by user.
3. **~~`on_int_ack` callback signature~~** — **RESOLVED: `std::function<uint8_t()>` returning the vector.** Plumbed into `Z80Cpu` as: at IntAck M1 cycle, if `on_int_ack` is set call it and use its return value as the vector byte; else fall back to the legacy `int_vector_` member. Agent B's brief is updated to this form.
4. **~~Scope of DMA delay into dma.cpp~~** — **RESOLVED: per-frame granularity (option A).** Agent F calls `dma_.set_dma_delay(im2_.current_dma_delay())` once at the top of each `Emulator::run_frame()`. DMA treats it as a frame-wide mask. Sufficient for DMA-01/02/03/05/06. Per-tick upgrade deferred to a focused follow-up branch if concrete software later demonstrates divergence.

### Resolved open question — DMA delay granularity (Q4, 2026-04-21)

**Decision: per-frame (option A).** Analysis preserved below for future reference.

#### Original analysis

VHDL reality (`zxnext.vhd:2001-2010` + `im2_control.vhd:111-138`): `im2_dma_delay` is a 1-bit signal, driven by `im2_dma_int OR dma_delay`, which gates the DMA controller's bus access per-CPU-cycle during (a) any pending IM2 interrupt and (b) the 2-cycle SRL window after a RETI/RETN (so the RETI pop doesn't get DMA-stolen into the wrong return address).

Two candidate implementations:

**A. Per-frame granularity (plan's default).** `Emulator::run_frame()` calls `dma_.set_dma_delay(im2_.current_dma_delay())` once at the top of each frame. The DMA controller treats it as a mask: when true, no DMA transfers happen during that frame's execution.

- **Pros:** Cheapest possible — one function call per 20ms. No new CPU-tick hooks. Minimal surface area for Agent F. Matches the granularity at which our DMA actually schedules transfers today (DMA is frame-batched in `src/peripheral/dma.cpp`).
- **Cons:** Not cycle-accurate. If the delay signal is briefly asserted and cleared within a single frame (e.g. RETI immediately before a DMA trigger), we miss the window and either over-stall (if latched high) or under-stall (if only sampled at frame top). In practice DMA transfers are triggered by port writes that the CPU is executing, and the delay is mainly a safety around RETI cycles — hitting that exact corner requires software that both runs DMA and uses RETI in the same micro-window, which is rare.
- **What the 5 test rows require:** DMA-01/02/03/05 assert state values (latch set, latch held, latch reset). DMA-06 asserts the compose-mask from NR 0xCC/CD/CE. None of the 5 un-skip targets assert a cycle-by-cycle waveform. **Per-frame is sufficient to make all 5 flip from skip → pass.**

**B. Per-tick granularity.** `Im2Controller::tick()` is called every Z80 M1 cycle (or every bus cycle), and updates `dma_delay_current_` live. The DMA controller reads it before every transfer attempt.

- **Pros:** Cycle-accurate, no observable divergence from VHDL. Future-proof: if NMI subsystem lands and DMA-04 (NMI→delay) becomes implementable, per-tick is the right granularity for that row too.
- **Cons:** New per-tick callback adds hot-path overhead. Larger change to `src/peripheral/dma.cpp` (new gating check per transfer). Expands Agent F's scope and its FUSE-regression surface. Agent F would need to coordinate with whoever owns the tick loop timing — today that's `Emulator::step_cycles()`.

**My recommendation:** go with **A (per-frame)** for this wave. Rationale:
- The 5 un-skip targets don't require cycle-accuracy, they only need state observability.
- Per-frame keeps Agent F's blast radius small; this is the lowest-risk DMA-adjacent change.
- The one row that would genuinely benefit from per-tick (DMA-04) is already NMI-blocked and deferred.
- If a concrete regression or new software later demonstrates a divergence, we upgrade to per-tick in a targeted follow-up (one branch, one critic) — incremental, reversible.

**My recommendation against (when per-tick is the right call):** if you're already planning a per-tick upgrade for the DMA controller for other reasons (e.g. to unblock future hardware-accurate scheduling), do it now so Agent F doesn't do the work twice.

Your call.

## Section E — Out-of-scope / explicit WONT candidates

**User decision 2026-04-21:** no rows are converted to WONT in this wave. The original WONT candidate (CTC-NR-04) stays as `skip()` with an updated "review later" reason string — deferred to a future WONT-sweep audit. All other comment-worthy rows (CTC-CW-11, IM2C-13, PULSE-09, IM2W-09, NR-C5-02) are VHDL-internal-pipeline (B) or duplicate (E), not WONT.

Preserved for a future sweep — suggested source comment text if CTC-NR-04 is later converted:

```cpp
// WONT CTC-NR-04: NR 0xC5 write vs CTC port-write overlap is a
// cycle-accurate bus-arbitration test from zxnext.vhd (nr_c5_we must not
// overlap i_iowr). jnext serialises both write paths at the C++ call
// level; both writes land, no cycle-level overlap. No software depends on
// the exact ordering. Un-skipping would require cycle-granularity tick
// API + dual-requester injection for zero user-visible payoff.
// Re-open only if concrete software demonstrates a divergence.
```

## Section F — Acceptance criteria

1. **ctc_test final line (target envelope):** `Total:  150  Passed:  ≥138  Failed:    0  Skipped:   ≤3` — the 3 remaining skips are CTC-NR-04 (review-later), NR-C0-02 and DMA-04 (NMI-blocked).
2. **Aggregate unit tests:** 0 fail; regression 34/0/0; FUSE Z80 1356/1356.
3. **New integration tests land as live passes** in `test/ctc_int/ctc_int_integration_test.cpp` (new file, confirmed by user 2026-04-21) — 10 rows (ULA-INT-01/02/03/05, NR-C0-04, NR-C4-02/03, NR-C6-02, ISC-09, ISC-10).
4. **No new skip()s introduced** in other subsystems without explicit justification. NR 0xC0/C4/C5/C6/C8/C9/CA handlers currently shadow-store; Phase 2 replaces with live wiring — must not introduce new `skip()` calls in `nextreg_integration_test.cpp`.
5. **All new src/ code has critic-agent APPROVE** — 8 independent critics (Phase 4).
6. **VHDL citations for every new check().** Every un-skipped row's `check()` description cites the authoritative VHDL `file:line` per process manual §1.
7. **Save/load round-trips.** `Im2Controller::save_state/load_state` round-trip is exercised by the existing rewind buffer path; no test-plan-row breaks across rewind.
8. **Documentation refreshed:**
   - `doc/testing/CTC-INTERRUPTS-TEST-PLAN-DESIGN.md` Current Status block.
   - `doc/design/EMULATOR-DESIGN-PLAN.md` aggregate table.
   - `doc/testing/TRACEABILITY-MATRIX.md` row-by-row update.
   - New audit `doc/testing/audits/task3-ctc-phase5.md`.

### Critical Files for Implementation

- /home/jorgegv/src/spectrum/jnext/src/cpu/im2.h
- /home/jorgegv/src/spectrum/jnext/src/cpu/im2.cpp
- /home/jorgegv/src/spectrum/jnext/src/cpu/z80_cpu.h
- /home/jorgegv/src/spectrum/jnext/src/core/emulator.cpp
- /home/jorgegv/src/spectrum/jnext/test/ctc/ctc_test.cpp

---

## Summary (≤300 words)

**Scope decisions made:**
1. Row-by-row triage of all 106 skips (not 150 plan rows — the 44 live pass are already green). Classified 80 for un-skip via Phase-2 branches, 5 as B/D/E comments, 1 as keep-skip-review-later (CTC-NR-04, per user 2026-04-21), 2 as genuine F (NR-C0-02 and DMA-04, both NMI-blocked), 10 as re-home to the new `test/ctc_int/ctc_int_integration_test.cpp`.
2. Architected `Im2Controller` as a single owner class (45-line stub becomes ~600 lines) with a `DevIdx` priority enum mirroring zxnext.vhd:1941 exactly. `Im2Client` mixin gives peripherals a thin façade. Device state, RETI/RETN decoder, pulse fabric, NR handlers, and DMA delay all live inside the Controller — not scattered across emulator.cpp.
3. Preserved the legacy `Im2Controller::raise/clear/get_vector/on_reti` API as compatibility wrappers so Phase 1 scaffold lands without breaking the 44 existing passes.
4. Z80 CPU core gets ONE new optional callback `on_int_ack = std::function<uint8_t()>` (user-approved 2026-04-21, return-value form) — smallest possible surface change, fallback-compatible when not installed.
5. In-flight backlog policy (user directive 2026-04-21): small surface items get folded into the current wave up to ~30 min each.

**Phase count:** 6 (Phase 0 self triage, Phase 1 scaffold, Phase 2 in 3 waves of parallel agents = 8 branches, Phase 3 un-skip + integration tests, Phase 4 critics, Phase 5 merge + regression). Max 3 agents concurrent per process rule.

**Expected skip delta:** 106 → ≤3 on `ctc_test.cpp` (CTC-NR-04 review-later + 2 NMI-blocked) + 10 live passes landing in the new integration test.

**Three biggest risks:**
1. Z80 core `on_int_ack` callback risks FUSE regression. Mitigated by opt-in callback + Phase 5 FUSE sanity.
2. NMI subsystem absence (per `memory/project_nmi_fragmented_status.md`) — NR-C0-02 and DMA-04 cannot close; plan explicitly deferred.
3. Pulse-fabric timing crossing machine-timing state could misfire on Pentagon/+3 — mitigated by reading live `timing_.machine`.

**Resolved user inputs 2026-04-21:** CTC-NR-04 → keep-skip-review-later; integration test file → new `test/ctc_int/ctc_int_integration_test.cpp`; `on_int_ack` signature → `std::function<uint8_t()>` return-value; DMA delay granularity → per-frame (Agent F calls `dma_.set_dma_delay(...)` once per `Emulator::run_frame()`).
