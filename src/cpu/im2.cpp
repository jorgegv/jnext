#include "im2.h"
#include "core/saveable.h"

// =============================================================================
// Phase 1 scaffold — compile-only expansion of Im2Controller.
//
// The legacy 6-method API (raise/clear/has_pending/get_vector/set_mask/on_reti)
// is preserved AS FUNCTIONALLY EQUIVALENT thin wrappers over the new DevIdx-
// based state. That keeps every current caller (emulator.cpp, ula.cpp,
// dma.cpp, uart.cpp, ctc callback) working without change.
//
// All new DevIdx-based entry points are present but stubbed; Phase 2 agents
// fill in the VHDL-faithful behaviour.
// =============================================================================

Im2Controller::Im2Controller() {
    reset();
}

void Im2Controller::reset() {
    for (int i = 0; i < N; ++i) {
        dev_[i] = Device{};
    }
    // ULA is the only "exception" device (it can fire even in IM2 mode),
    // per zxnext.vhd:2024-2031. Mark it so Phase 2 Agent C can rely on it.
    dev_[static_cast<int>(DevIdx::ULA)].exception = true;

    dec_state_       = DecState::S_0;
    reti_seen_pulse_ = false;
    retn_seen_pulse_ = false;
    reti_decode_     = false;
    dma_delay_ctrl_  = false;
    im_mode_         = 0;

    pulse_int_n_      = true;
    pulse_count_      = 0;
    machine_48_or_p3_ = false;

    vector_base_msb3_ = 0;
    im2_mode_         = false;
    stackless_nmi_    = false;

    dma_int_en_mask14_     = 0;
    im2_dma_delay_latched_ = false;

    last_acked_  = -1;
    legacy_mask_ = 0xFFFF;
}

// -----------------------------------------------------------------------------
// Tick — per-cycle advance.
//
// Phase 2 Agent B: advances the per-device IM2 state machine once per call.
// The wrapper/edge-detect layer (int_req_d, im2_int_req latching, int_status)
// lives in step_devices() below and is owned by Agent D; Agent B's state-
// machine half is folded into step_devices() via step_state_machine().
//
// The scaffold Em invariant says tick() is called from Emulator::run_frame.
// We treat each tick() as one "CLK_CPU rising edge" — VHDL im2_device.vhd:91
// drives state<=state_next on rising edge.
// -----------------------------------------------------------------------------
void Im2Controller::tick(uint32_t /*master_cycles*/) {
    // Pulse fabric runs BEFORE the wrapper/state-machine step because it
    // needs to observe the rising edge of int_req, which is defined (VHDL
    // im2_peripheral.vhd:101) as `int_req AND NOT int_req_d`. step_devices()
    // updates int_req_d at the end of its own first phase, destroying the
    // edge. Running step_pulse() first lets both halves sample the same
    // pre-update state, matching the VHDL synchronous-update semantic.
    step_pulse();
    step_devices();
}

// -----------------------------------------------------------------------------
// Legacy ↔ DevIdx bridge.
//
// Im2Level covers 14 slots; only 10 of them have a true DevIdx counterpart in
// the VHDL IM2 fabric. The remaining three (DMA, DIVMMC, MULTIFACE) live
// outside the fabric in the real hardware:
//   - DMA     — "victim" of INT, not a source (zxnext.vhd:2003-2008).
//   - DIVMMC  — NMI-driven, not IM2.
//   - MULTIFACE — NMI-driven (Task 8 will add a proper peripheral).
//
// For the scaffold we map them all to DevIdx::ULA as a harmless placeholder.
// This is OK because:
//   (a) raise/clear on ULA in Phase 1 is still only an in-memory bool flip;
//   (b) the legacy has_pending/get_vector path uses its OWN pending[] (held
//       in the device int_req fields below), masked by legacy_mask_ at the
//       original Im2Level positions — so the bridge mapping is never observed
//       through the legacy API.
// Flagged in agent report for Phase 2 review.
// -----------------------------------------------------------------------------
Im2Controller::DevIdx Im2Controller::to_devidx(Im2Level lvl) const {
    switch (lvl) {
        case Im2Level::LINE_IRQ:   return DevIdx::LINE;
        case Im2Level::FRAME_IRQ:  return DevIdx::ULA;
        case Im2Level::CTC_0:      return DevIdx::CTC0;
        case Im2Level::CTC_1:      return DevIdx::CTC1;
        case Im2Level::CTC_2:      return DevIdx::CTC2;
        case Im2Level::CTC_3:      return DevIdx::CTC3;
        case Im2Level::UART_RX_0:  return DevIdx::UART0_RX;
        case Im2Level::UART_RX_1:  return DevIdx::UART1_RX;
        case Im2Level::UART_TX_0:  return DevIdx::UART0_TX;
        case Im2Level::UART_TX_1:  return DevIdx::UART1_TX;
        case Im2Level::ULA_EXTRA:  return DevIdx::ULA;
        // Non-IM2 sources — placeholder mapping; see note above. The legacy
        // API path does NOT depend on this, so this is harmless.
        case Im2Level::DMA:        return DevIdx::ULA;
        case Im2Level::DIVMMC:     return DevIdx::ULA;
        case Im2Level::MULTIFACE:  return DevIdx::ULA;
        case Im2Level::COUNT:      break;
    }
    return DevIdx::ULA;
}

// -----------------------------------------------------------------------------
// Legacy API — preserves exact pre-scaffold behaviour.
// We keep a parallel "pending" view inside the Device records by keying on
// the ORIGINAL Im2Level index, then mask via legacy_mask_ / vector = 2*i.
// -----------------------------------------------------------------------------
void Im2Controller::raise(Im2Level level) {
    // Record on both sides:
    //   - legacy view: dev_ at the Im2Level index for has_pending/get_vector
    //   - new view:    raise_req on the mapped DevIdx for int_req propagation
    //
    // NOTE: dev_[] is sized N = DevIdx::COUNT == Im2Level::COUNT == 14, so we
    // can index it with either enum's raw int safely for the legacy side.
    int i = static_cast<int>(level);
    if (i >= 0 && i < N) {
        dev_[i].int_req = true;
    }
}

void Im2Controller::clear(Im2Level level) {
    int i = static_cast<int>(level);
    if (i >= 0 && i < N) {
        dev_[i].int_req = false;
    }
}

bool Im2Controller::has_pending() const {
    for (int i = 0; i < N; ++i) {
        if (dev_[i].int_req && (legacy_mask_ & (1u << i))) return true;
    }
    return false;
}

uint8_t Im2Controller::get_vector() const {
    for (int i = 0; i < N; ++i) {
        if (dev_[i].int_req && (legacy_mask_ & (1u << i))) {
            return static_cast<uint8_t>(i * 2);
        }
    }
    return 0xFF;
}

void Im2Controller::set_mask(uint16_t mask) {
    legacy_mask_ = mask;
}

void Im2Controller::on_reti() {
    // Phase 2 Agent B — device-side effect of a RETI opcode.
    //
    // VHDL im2_device.vhd:123-128:
    //     when S_ISR =>
    //        if i_reti_seen = '1' and i_iei = '1' and i_im2_mode = '1' then
    //           state_next <= S_0;
    //
    // Walk dev_[] in priority order; any device currently in S_ISR whose
    // daisy-chain IEI is high returns to S_0. In VHDL only one device can
    // be in S_ISR at a time in a non-nested ISR (IEI=0 downstream from an
    // S_ISR device), but nested IM2 handlers can land multiple devices in
    // S_ISR; the IEI=1 gate correctly resolves "only the innermost
    // (highest-priority-active) clears".
    //
    // Agent A surfaces reti_seen_pulse_ via the decoder; step_devices()
    // reads that pulse and does the same walk. This legacy entry point
    // remains because the emulator's on_m1_cycle forwarder still calls
    // im2_.on_reti() directly — keeping both paths converges on the same
    // end state.
    if (!im2_mode_) return;   // device SM is reset-held in pulse mode
    // Snapshot IEI across all devices BEFORE any transition so that clearing
    // a higher-priority S_ISR device does NOT cascade into the next device
    // in the same RETI (matches VHDL simultaneous-update semantic; only one
    // device clears per RETI).
    bool iei_snap[N];
    {
        bool iei = true;  // device 0's hard-wired IEI
        for (int k = 0; k < N; ++k) {
            iei_snap[k]       = iei;
            const Device& d = dev_[k];
            bool ieo;
            switch (d.state) {
                case DevState::S_0:   ieo = iei;                  break;
                case DevState::S_REQ: ieo = iei && reti_decode_;  break;
                default:              ieo = false;                break;
            }
            iei = ieo;
        }
    }
    for (int i = 0; i < N; ++i) {
        if (dev_[i].state == DevState::S_ISR && iei_snap[i]) {
            dev_[i].state = DevState::S_0;
        }
    }
}

// -----------------------------------------------------------------------------
// DevIdx peripheral-facing API — Agent D (wrapper) implementation.
//
// These mirror the VHDL i_int_req / i_int_unq / i_int_status_clear inputs of
// im2_peripheral.vhd and the o_int_status output (line 180). Edge detection
// and the im2_int_req latch are driven per-tick inside step_devices() wrapper
// phase — see that function for the CLK_28 pipeline model.
// -----------------------------------------------------------------------------

// raise_req(): peripheral asserts i_int_req (level active).
// The wrapper step will detect the rising edge and latch im2_int_req next tick.
// int_status is NOT set here directly — per VHDL:154-162 it is set from the
// edge-detected pulse "int_req" (line 101), which means the set happens in the
// wrapper step, not immediately on input assertion. See step_devices().
void Im2Controller::raise_req(DevIdx d) {
    dev_[static_cast<int>(d)].int_req = true;
}

// clear_req(): peripheral deasserts i_int_req. Normally paired with isr_serviced.
void Im2Controller::clear_req(DevIdx d) {
    dev_[static_cast<int>(d)].int_req = false;
}

// raise_unq(): unqualified one-shot pulse. Per VHDL im2_peripheral.vhd:160
// and :172, an unqualified request both sets int_status AND latches im2_int_req
// regardless of i_int_en (UNQ-04 / UNQ-05 invariants). We set all three here
// so the effect is visible the same cycle, matching the combinational portion
// of the VHDL equations (the registered settle is one CLK_28 later, but for our
// single-threaded tick model we collapse that).
//
// int_unq is cleared by the pulse fabric (Agent C, Wave 2) after the pulse
// fires — we do NOT clear it here.
void Im2Controller::raise_unq(DevIdx d) {
    Device& dv = dev_[static_cast<int>(d)];
    dv.int_unq     = true;    // one-shot latch
    dv.int_status  = true;    // vhdl:160  int_status <= (int_req or i_int_unq) | ...
    dv.im2_int_req = true;    // vhdl:172  bypasses i_int_en
}

// clear_status(): i_int_status_clear one-shot from NR 0xC8/C9/CA writes.
// Per VHDL im2_peripheral.vhd:160, i_int_status_clear clears ONLY the
// int_status register, NOT the im2_int_req latch (which is cleared only by
// im2_isr_serviced, per line 175). The software-visible composite o_int_status
// remains true while im2_int_req is latched — this matches VHDL exactly.
void Im2Controller::clear_status(DevIdx d) {
    Device& dv = dev_[static_cast<int>(d)];
    dv.int_status = false;
    // im2_int_req: INTENTIONALLY not cleared here. See VHDL :175.
}

// int_status(): o_int_status composite per VHDL im2_peripheral.vhd:180
// (o_int_status <= int_status OR im2_int_req). This is the bit exposed to
// software via NR 0xC8/C9/CA reads.
bool Im2Controller::int_status(DevIdx d) const {
    const Device& dv = dev_[static_cast<int>(d)];
    return dv.int_status || dv.im2_int_req;
}

uint8_t Im2Controller::int_status_mask_c8() const {
    // Phase 2 Agent E: pack line/ULA bits per zxnext.vhd:1952.
    return 0;
}

uint8_t Im2Controller::int_status_mask_c9() const {
    // Phase 2 Agent E: pack CTC 7..0 per zxnext.vhd:1953.
    return 0;
}

uint8_t Im2Controller::int_status_mask_ca() const {
    // Phase 2 Agent E: pack UART per zxnext.vhd:1954.
    return 0;
}

// -----------------------------------------------------------------------------
// Enable-bit registers (NR 0xC4/C5/C6) — Phase 1 stubs.
// -----------------------------------------------------------------------------
void Im2Controller::set_int_en(DevIdx d, bool en) {
    dev_[static_cast<int>(d)].int_en = en;
}

void Im2Controller::set_int_en_c4(uint8_t /*val*/) { /* Phase 2 Agent E */ }
void Im2Controller::set_int_en_c5(uint8_t /*val*/) { /* Phase 2 Agent E */ }
void Im2Controller::set_int_en_c6(uint8_t /*val*/) { /* Phase 2 Agent E */ }

// -----------------------------------------------------------------------------
// NR 0xC0 state — stored/returned.
// -----------------------------------------------------------------------------
void Im2Controller::set_vector_base(uint8_t msb3) {
    vector_base_msb3_ = static_cast<uint8_t>(msb3 & 0x07);
}
uint8_t Im2Controller::vector_base() const { return vector_base_msb3_; }

void Im2Controller::set_mode(bool im2_mode) { im2_mode_ = im2_mode; }
bool Im2Controller::is_im2_mode() const    { return im2_mode_; }

void Im2Controller::set_stackless_nmi(bool v) { stackless_nmi_ = v; }
bool Im2Controller::stackless_nmi() const    { return stackless_nmi_; }

// -----------------------------------------------------------------------------
// DMA int-enable — Phase 1 partial (storage only).
// -----------------------------------------------------------------------------
void Im2Controller::set_dma_int_en_mask(uint16_t mask14) {
    dma_int_en_mask14_ = static_cast<uint16_t>(mask14 & 0x3FFF);
}
bool Im2Controller::dma_int_pending() const { return false; }
bool Im2Controller::dma_delay() const       { return false; }

// -----------------------------------------------------------------------------
// Z80 integration — Phase 1 stubs.
// -----------------------------------------------------------------------------
bool Im2Controller::int_line_asserted() const {
    // VHDL im2_device.vhd:150 per-device output:
    //     o_int_n <= '0' when state = S_REQ and i_iei = '1' and i_im2_mode = '1'
    // VHDL peripherals.vhd:146-156 AND-reduces o_int_n across all 14 devices:
    //     o_int_n <= int_n(1) and int_n(2) ... and int_n(14)
    // Equivalent: the aggregate line is low (asserted) iff ANY device drives
    // its local o_int_n low — i.e., is in S_REQ with IEI high in IM2 mode.
    if (!im2_mode_) return false;
    for (int i = 0; i < N; ++i) {
        if (dev_[i].state != DevState::S_REQ) continue;
        bool iei = (i == 0) ? true : device_ieo(i - 1);
        if (iei) return true;
    }
    return false;
}

uint8_t Im2Controller::ack_vector() {
    // Called by Z80Cpu at the IntAck M1 cycle (i_m1_n='0' and i_iorq_n='0').
    //
    // VHDL im2_device.vhd:111-116 S_REQ transition:
    //     when S_REQ =>
    //        if i_m1_n='0' and i_iorq_n='0' and i_iei='1' and i_im2_mode='1' then
    //           state_next <= S_ACK;
    //
    // We walk dev_[] in priority order (index 0 = LINE = highest per VHDL
    // zxnext.vhd:1941) and latch the first device that would be acked —
    // at most one device can satisfy iei='1' because any higher-priority
    // S_REQ blocks the chain (im2_device.vhd:141-142: IEO=IEI and reti_decode
    // when state=S_REQ; reti_decode is typically 0 during IntAck, so the
    // first S_REQ breaks the chain).
    //
    // Vector: im2_device.vhd:155 drives o_vec = i_vec while state = S_ACK or
    // state_next = S_ACK. We compose at read time; the caller (CPU) latches
    // the byte immediately.
    if (!im2_mode_) return 0xFF;  // pulse mode: legacy int_vector_ drives
    for (int i = 0; i < N; ++i) {
        if (dev_[i].state != DevState::S_REQ) continue;
        bool iei = (i == 0) ? true : device_ieo(i - 1);
        if (!iei) continue;
        dev_[i].state = DevState::S_ACK;
        last_acked_   = i;
        return compute_vector();
    }
    return 0xFF;
}

// -----------------------------------------------------------------------------
// RETI/RETN/IM-mode decoder — VHDL im2_control.vhd:70-240 faithful.
//
// Each call corresponds to the rising edge of T4 following an M1 cycle (i.e.
// the VHDL `ifetch_fe_t3 = '1'` condition, lines 107 + 158). The emulator's
// Z80 core invokes this once per opcode fetch with the freshly read byte.
//
// We clear the single-cycle pulses (reti_seen / retn_seen) up front so the
// observer methods only return true for the tick in which a RETI/RETN opcode
// was just decoded. The decoder FSM is delegated to advance_decoder().
//
// Derived flat signals are latched after the FSM step:
//   - reti_decode_ is true while the decoder sits in S_ED_T4, matching
//     VHDL line 233 (o_reti_decode <= '1' when state = S_ED_T4).
//   - dma_delay_ctrl_ is true while the decoder is in any of the windows
//     where the VHDL asserts o_dma_delay (line 238):
//     {S_ED_T4, S_ED4D_T4, S_ED45_T4, S_SRL_T1, S_SRL_T2}.
// -----------------------------------------------------------------------------
void Im2Controller::on_m1_cycle(uint16_t /*pc*/, uint8_t opcode) {
    reti_seen_pulse_ = false;
    retn_seen_pulse_ = false;

    advance_decoder(opcode);

    // Latch the flat signals off the *new* dec_state_ — see VHDL 233/238.
    reti_decode_    = (dec_state_ == DecState::S_ED_T4);
    dma_delay_ctrl_ = (dec_state_ == DecState::S_ED_T4)
                   || (dec_state_ == DecState::S_ED4D_T4)
                   || (dec_state_ == DecState::S_ED45_T4)
                   || (dec_state_ == DecState::S_SRL_T1)
                   || (dec_state_ == DecState::S_SRL_T2);
}

uint8_t Im2Controller::im_mode() const { return im_mode_; }

// -----------------------------------------------------------------------------
// Pulse mode — Phase 1 stubs.
// -----------------------------------------------------------------------------
bool Im2Controller::pulse_int_n() const { return pulse_int_n_; }

void Im2Controller::set_machine_timing_48_or_p3(bool v) { machine_48_or_p3_ = v; }
void Im2Controller::set_machine_timing_pentagon(bool /*v*/) {
    // VHDL documents this as no-op; retained for API completeness.
}

// -----------------------------------------------------------------------------
// Debug accessors.
// -----------------------------------------------------------------------------
Im2Controller::DevState Im2Controller::state(DevIdx d) const {
    return dev_[static_cast<int>(d)].state;
}

bool Im2Controller::ieo(DevIdx d) const {
    // Public debug accessor — exposes the device's o_ieo signal, which is
    // the IEI input of the NEXT device in the priority chain.
    return device_ieo(static_cast<int>(d));
}

// -----------------------------------------------------------------------------
// Private helpers.
// -----------------------------------------------------------------------------
// -----------------------------------------------------------------------------
// advance_decoder — RETI/RETN/IM-mode finite-state machine.
//
// Mirrors the VHDL process at im2_control.vhd:158-210 exactly, driving
// `dec_state_` and setting:
//   - reti_seen_pulse_ on the edge INTO S_ED4D_T4  (vhdl:234)
//   - retn_seen_pulse_ on the edge INTO S_ED45_T4  (vhdl:236)
//   - im_mode_         on ED 46/4E/66/6E (IM 0), ED 56/76 (IM 1),
//                      ED 5E/7E (IM 2)              (vhdl:218-227)
//
// Each call represents one VHDL "ifetch falling-edge-of-T3" event — the
// emulator's tick granularity collapses the rising/falling edge distinction
// (IM2C-13 commented as B in the plan). Since our emulator only calls us at
// the end of an opcode fetch, every invocation is equivalent to the VHDL
// `ifetch_fe_t3 = '1'` branch in each state; for states with the "else"
// (stay) branch we simply do nothing new.
// -----------------------------------------------------------------------------
void Im2Controller::advance_decoder(uint8_t opcode) {
    switch (dec_state_) {

        // vhdl:161-170
        case DecState::S_0:
            if      (opcode == 0xED) dec_state_ = DecState::S_ED_T4;
            else if (opcode == 0xCB) dec_state_ = DecState::S_CB_T4;
            else if (opcode == 0xDD || opcode == 0xFD)
                                     dec_state_ = DecState::S_DDFD_T4;
            // else stay in S_0
            break;

        // vhdl:171-180 + IM-mode decode vhdl:218-227
        case DecState::S_ED_T4:
            if (opcode == 0x4D) {
                dec_state_       = DecState::S_ED4D_T4;
                reti_seen_pulse_ = true;     // vhdl:234
            } else if (opcode == 0x45) {
                dec_state_       = DecState::S_ED45_T4;
                retn_seen_pulse_ = true;     // vhdl:236
            } else {
                // IM-mode decode (vhdl:223-224):
                //   match opcode (7:6)="01" and (2:0)="110", i.e.
                //   ED 46/4E/66/6E → IM 0
                //   ED 56/76       → IM 1
                //   ED 5E/7E       → IM 2
                if ((opcode & 0xC7) == 0x46) {
                    // bit-decode per vhdl:224:
                    //   im_mode <= (bit4 AND bit3) & (bit4 AND NOT bit3)
                    // → IM 0 when bit4=0; IM 1 when bit4=1 & bit3=0;
                    //   IM 2 when bit4=1 & bit3=1.
                    const bool b4 = (opcode & 0x10) != 0;
                    const bool b3 = (opcode & 0x08) != 0;
                    if (!b4)        im_mode_ = 0;
                    else if (!b3)   im_mode_ = 1;
                    else            im_mode_ = 2;
                }
                dec_state_ = DecState::S_0;
            }
            break;

        // vhdl:181-183 — ED 4D path joins the shared SRL delay window
        case DecState::S_ED4D_T4:
            dec_state_ = DecState::S_SRL_T1;
            break;

        // vhdl:190-192 — ED 45 path joins the shared SRL delay window
        case DecState::S_ED45_T4:
            dec_state_ = DecState::S_SRL_T1;
            break;

        // vhdl:186-189 — "extra states prevent accumulation of return
        // addresses on the stack" (DMA-interruption guard).
        case DecState::S_SRL_T1:
            dec_state_ = DecState::S_SRL_T2;
            break;
        case DecState::S_SRL_T2:
            dec_state_ = DecState::S_0;
            break;

        // vhdl:193-198 — CB is a one-opcode-lookahead prefix; the next
        // fetched byte (the CB suboperation) unconditionally returns us
        // to S_0. im_mode is not affected here.
        case DecState::S_CB_T4:
            dec_state_ = DecState::S_0;
            break;

        // vhdl:199-206 — DD/FD prefix chain: DD/FD keeps us in S_DDFD_T4
        // (so DD FD DD ... stays here), an ED starts the RETI/RETN
        // lookahead, anything else returns to S_0.
        case DecState::S_DDFD_T4:
            if      (opcode == 0xDD || opcode == 0xFD) {
                // stay
            } else if (opcode == 0xED) {
                dec_state_ = DecState::S_ED_T4;
            } else {
                dec_state_ = DecState::S_0;
            }
            break;
    }
}

// step_devices() — VHDL im2_peripheral.vhd + im2_device.vhd per-tick pipeline.
//
// Three phases executed in order. Phase 1 (Agent D) runs the wrapper edge-
// detect + im2_int_req latch so Phase 2 sees this cycle's latched state.
// Phase 2 (Agent B) walks each device's state machine with IEI snapshotted
// pre-transition so cascading S_ISR → S_0 clears don't fire on the same
// tick. Phase 3 (Agent D) is a documented no-op because Agent B's state
// machine handles the im2_int_req clear inline at S_ISR → S_0.
//
// VHDL references:
//   Phase 1: im2_peripheral.vhd:90-101 (edge) + :154-162 (int_status) +
//            :167-178 (im2_int_req latch).
//   Phase 2: im2_device.vhd:102-132 (S_0/S_REQ/S_ACK/S_ISR) + :136-146
//            (IEO) + im2_peripheral.vhd:105 (reset held in pulse mode).
//   Phase 3: im2_peripheral.vhd:137-148 (isr_serviced edge-detect) — VHDL
//            semantics collapsed into the Phase-2 S_ISR → S_0 branch.
void Im2Controller::step_devices() {
    // ── Phase 1: wrapper edge detect + im2_int_req latch (Agent D) ─────────
    // int_unq bypasses i_int_en in both the status register (UNQ-05) and the
    // im2 latch (UNQ-04). raise_unq() also sets these fields directly so
    // combinational collapse is observationally equivalent.
    for (int i = 0; i < N; ++i) {
        Device& d = dev_[i];
        const bool edge = d.int_req && !d.int_req_d;

        // Set int_status on any edge (vhdl:160 — gated by neither int_en
        // nor int_unq; the edge pulse itself is what the VHDL equation
        // uses).
        if (edge) {
            d.int_status = true;
        }

        // Set im2_int_req latch: edge qualified by int_en, OR unqualified
        // (int_unq, which bypasses int_en per vhdl:172).
        if (edge && d.int_en) {
            d.im2_int_req = true;
        }
        if (d.int_unq) {
            d.im2_int_req = true;      // bypass int_en
            d.int_status  = true;      // vhdl:160 — unq also feeds int_status
            // int_unq is one-shot; cleared by the pulse fabric (Agent C,
            // Wave 2) after the pulse fires. Do NOT clear here.
        }

        // Update delayed copy LAST so next tick's edge calculation sees
        // this tick's int_req as the "previous" value.
        d.int_req_d = d.int_req;
    }

    // ── Phase 2: per-device state machine (Agent B) ────────────────────────
    // Snapshot per-device IEI derived from CURRENT (pre-transition) states
    // — preserves VHDL synchronous-update semantic: clearing a higher-
    // priority S_ISR device must NOT cascade into the next device in the
    // same tick.
    bool iei_snap[N];
    {
        bool iei = true;  // device 0's hard-wired IEI (zxnext.vhd:1984)
        for (int k = 0; k < N; ++k) {
            iei_snap[k]       = iei;
            const Device& d = dev_[k];
            bool ieo;
            switch (d.state) {
                case DevState::S_0:   ieo = iei;                  break;
                case DevState::S_REQ: ieo = iei && reti_decode_;  break;
                default:              ieo = false;                break;  // S_ACK / S_ISR
            }
            iei = ieo;
        }
    }
    for (int i = 0; i < N; ++i) {
        step_state_machine_with_iei(i, iei_snap[i]);
    }
    // Note: reti_seen_pulse_ is a one-cycle pulse owned by Agent A's
    // decoder; it's cleared on the next on_m1_cycle call.

    // ── Phase 3: isr_serviced cleanup (documented no-op, Agent D) ──────────
    propagate_isr_serviced();
}

void Im2Controller::step_state_machine_with_iei(int i, bool iei) {
    // Device-local state-machine half for device i. Mirrors
    // im2_device.vhd:102-132 (S_0/S_REQ/S_ACK/S_ISR).
    //
    // VHDL im2_peripheral.vhd:105 gates the device's reset:
    //     im2_reset_n <= i_mode_pulse_0_im2_1 and not i_reset;
    // In pulse mode (im2_mode_ == false), im2_reset_n == '0' holds the
    // state machine at S_0. We honour that here: no transitions in pulse
    // mode; Agent C's pulse path drives interrupts instead.
    if (!im2_mode_) {
        dev_[i].state = DevState::S_0;
        return;
    }

    Device& d = dev_[i];

    switch (d.state) {
        case DevState::S_0:
            // VHDL:106 — state_next <= S_REQ when i_int_req='1' and i_m1_n='1'.
            // Our tick model doesn't carry M1 directly; we approximate
            // "not in an IntAck cycle" by the absence of any device in S_ACK.
            // The real hardware can't transition S_0→S_REQ during an
            // IntAck cycle anyway because the device wouldn't be idle. In
            // practice `im2_int_req` is the latched output of the wrapper
            // (Agent D), driven true by int_unq OR (int_req AND int_en).
            if (d.im2_int_req) {
                d.state = DevState::S_REQ;
            }
            break;

        case DevState::S_REQ:
            // VHDL:112 — state_next <= S_ACK when
            //   i_m1_n='0' and i_iorq_n='0' and i_iei='1' and i_im2_mode='1'.
            // The S_REQ→S_ACK transition is taken synchronously inside
            // ack_vector() (the CPU-side entry point for the IntAck cycle).
            // Nothing to do here on a plain tick.
            //
            // If im2_int_req is deasserted while we're still in S_REQ
            // (e.g. enable bit cleared, or int_unq one-shot expired before
            // ack), we stay in S_REQ per VHDL (S_REQ has no deassert
            // fallback). The wrapper's isr_serviced pulse will clear the
            // latch only when we later reach S_ISR→S_0. This matches VHDL
            // behaviour: once a req is latched it persists until serviced.
            //
            // Note: iei is consumed by int_line_asserted()/ack_vector(),
            // both of which derive it fresh from device_ieo(); we don't
            // need it here.
            (void)iei;
            break;

        case DevState::S_ACK:
            // VHDL:117 — state_next <= S_ISR when i_m1_n='1' (i.e. the
            // cycle AFTER the IntAck M1). In our model ack_vector() latches
            // S_REQ→S_ACK in-cycle; the NEXT tick advances S_ACK→S_ISR.
            d.state = DevState::S_ISR;
            break;

        case DevState::S_ISR:
            // VHDL:123 — state_next <= S_0 when
            //   i_reti_seen='1' and i_iei='1' and i_im2_mode='1'.
            // Agent A owns reti_seen_pulse_; we consume it here.
            // (The legacy on_reti() entry point also triggers the same
            //  walk, so both RETI propagation paths converge.)
            if (reti_seen_pulse_ && iei) {
                d.state = DevState::S_0;
                // Clear the im2_int_req latch inline — VHDL
                // im2_peripheral.vhd:175 (clear via im2_isr_serviced).
                d.im2_int_req = false;
            }
            break;
    }
}

// -----------------------------------------------------------------------------
// step_pulse() — VHDL zxnext.vhd:2012-2044 + im2_peripheral.vhd:184-194.
//
// Pulse-mode INT generator. On a rising edge of any qualifying pulse source,
// drives pulse_int_n low and holds it low for a machine-specific duration:
//   - 48K and +3       : 32 CPU cycles (pulse_count bit 5 becomes 1)
//   - 128K and Pentagon: 36 CPU cycles (pulse_count bit 5 AND bit 2)
//   - Next (ZXN)       : tracks whatever machine_timing_* NR 0x03 selects;
//                        our set_machine_timing_48_or_p3(bool) toggles the
//                        shorter variant when the Next is in 48K/+3 timing,
//                        otherwise the 36-cycle variant applies.
//
// Per-device qualification (im2_peripheral.vhd:184-194):
//   non-exception (EXCEPTION='0'):
//     o_pulse_en = ((int_req AND i_int_en) OR i_int_unq) AND NOT im2_mode
//   exception (EXCEPTION='1', only ULA, zxnext.vhd:1964):
//     o_pulse_en = ((int_req AND i_int_en) OR i_int_unq)
//                    AND ((im2_mode AND NOT z80_im2) OR NOT im2_mode)
//
// Note: `int_req` in VHDL is the edge-detected local pulse
// (`i_int_req AND NOT int_req_d`), NOT the latched level. We reconstruct it
// here from dev_[].int_req vs dev_[].int_req_d, which works because this
// function runs before step_devices() updates int_req_d (see tick()).
//
// The peripheral block's `o_pulse_en` outputs are OR-reduced into the global
// `pulse_int_en` (peripherals.vhd). We compute that OR across the 14 device
// indices here.
//
// int_unq is a one-shot owned by Agent C (this function) — we clear it on
// all devices when a pulse terminates. Agent D's comment in step_devices()
// documents this hand-off ("cleared by the pulse fabric (Agent C, Wave 2)").
// -----------------------------------------------------------------------------
void Im2Controller::step_pulse() {
    // Compute pulse_int_en = OR-reduction of per-device o_pulse_en.
    bool pulse_en = false;
    for (int i = 0; i < N; ++i) {
        const Device& d = dev_[i];
        // `int_req` local edge per VHDL im2_peripheral.vhd:101.
        const bool int_req_edge = d.int_req && !d.int_req_d;
        const bool qualified    = (int_req_edge && d.int_en) || d.int_unq;
        if (!qualified) continue;

        if (d.exception) {
            // ULA path: fires in pulse mode always, or in IM2 mode when the
            // CPU isn't itself in IM=2 (VHDL im2_peripheral.vhd:192).
            if (!im2_mode_ || (im_mode_ != 2)) {
                pulse_en = true;
            }
        } else {
            // Non-exception path: fires only in pulse mode
            // (VHDL im2_peripheral.vhd:186 — AND NOT i_mode_pulse_0_im2_1).
            if (!im2_mode_) {
                pulse_en = true;
            }
        }
    }

    // pulse_int_n sequencer per zxnext.vhd:2017-2031.
    if (pulse_int_n_) {
        // INT idle (high). Any pulse source starts a new pulse.
        if (pulse_en) {
            pulse_int_n_ = false;
            pulse_count_ = 0;   // counter explicitly reset while pulse_int_n='1'
                                // per zxnext.vhd:2037-2042 (process holds it 0
                                // while pulse_int_n='1', increments otherwise).
        }
    } else {
        // INT low — count up; terminate at machine-specific width.
        // VHDL:
        //   pulse_count_end = pulse_count(5) AND (machine_timing_48 OR
        //                                         machine_timing_p3 OR
        //                                         pulse_count(2));
        // i.e. when bit 5 is set AND (48K-or-+3 OR bit 2 is set).
        //   - 48K / +3 : terminates at count == 32 (bit5 first set).
        //   - 128K / Pentagon / Next-default : needs bit5 AND bit2 → count == 36.
        // VHDL's process (line 2035-2044) increments pulse_count ONLY while
        // pulse_count_end='0'; after terminate the count freezes until the
        // next "pulse_int_n='1'" cycle resets it. We mirror exactly.
        const bool bit5 = (pulse_count_ & 0x20) != 0;
        const bool bit2 = (pulse_count_ & 0x04) != 0;
        const bool pulse_count_end = bit5 && (machine_48_or_p3_ || bit2);

        if (pulse_count_end) {
            pulse_int_n_ = true;
            // Counter reset happens naturally on the next tick (via the
            // pulse_int_n=='1' branch above) — mirrors VHDL line 2038.
            // Clear the int_unq one-shots across all devices: the unqualified
            // pulse has now fired and must not re-trigger. Wrapper-maintained
            // latches (int_status, im2_int_req) stay as they are.
            for (int k = 0; k < N; ++k) dev_[k].int_unq = false;
        } else {
            // Increment. uint8_t wrap is fine — termination gates at 32/36
            // which are well inside 0..255.
            pulse_count_ = static_cast<uint8_t>(pulse_count_ + 1);
        }
    }
}

uint8_t Im2Controller::compute_vector() const {
    // VHDL zxnext.vhd:1999 — im2_vector = nr_c0_im2_vector[2:0] & im2_vec[3:0] & '0'.
    //   Bits 7:5 = nr_c0_im2_vector[2:0]   (vector_base_msb3_)
    //   Bits 4:1 = im2_vec[3:0]            (index of S_ACK device)
    //   Bit    0 = '0'                     (vectors are even-aligned)
    //
    // VHDL im2_device.vhd:155 drives o_vec<=i_vec only while state = S_ACK.
    // Peripherals OR-reduce across all 14 devices (peripherals.vhd:134-144);
    // since at most one device is in S_ACK at a time, the OR is a plain
    // selection. When no device is in S_ACK, the OR-reduction is 0 — we
    // mirror that. ack_vector() returns 0xFF for the no-qualifying-device
    // fallback path; compute_vector() stays honest.
    uint8_t idx = 0;
    for (int i = 0; i < N; ++i) {
        if (dev_[i].state == DevState::S_ACK) {
            idx = static_cast<uint8_t>(i & 0x0F);
            break;
        }
    }
    return static_cast<uint8_t>(((vector_base_msb3_ & 0x07) << 5) | (idx << 1));
}

bool Im2Controller::device_ieo(int i) const {
    // VHDL im2_device.vhd:136-146 — IEO driven from state:
    //   S_0   : o_ieo <= i_iei                   (pass through when idle)
    //   S_REQ : o_ieo <= i_iei and reti_decode   (block unless RETI decode)
    //   other : o_ieo <= '0'                     (S_ACK / S_ISR block all)
    //
    // Chain from device 0 upward. Device 0's i_iei is hardwired to '1'
    // (peripherals.vhd:82 + zxnext.vhd:1984). Iterative walk; at most 14
    // devices so O(N) per lookup and O(N²)=196 ops per tick worst case is
    // negligible.
    if (i < 0 || i >= N) return false;
    bool iei = true;
    for (int k = 0; k <= i; ++k) {
        const Device& d = dev_[k];
        bool ieo;
        switch (d.state) {
            case DevState::S_0:   ieo = iei;                  break;
            case DevState::S_REQ: ieo = iei && reti_decode_;  break;
            default:              ieo = false;                break;  // S_ACK / S_ISR
        }
        if (k == i) return ieo;
        iei = ieo;
    }
    return iei;  // unreachable
}

// propagate_isr_serviced() — documented no-op.
//
// The VHDL model (im2_peripheral.vhd:137-148) detects a rising edge on the
// isr_serviced signal from im2_device and uses that edge to clear im2_int_req
// (line 175). In our single-threaded-tick emulator model, the state machine
// clears im2_int_req inline during the S_ISR → S_0 transition (see
// step_state_machine_with_iei above) — simpler than carrying a prev_state_[]
// shadow and avoids split-brain over who owns the clear. This function is
// kept as an explicit extension point for symmetry with the VHDL signal name.
void Im2Controller::propagate_isr_serviced() {
    // Intentionally empty — see function comment.
}

// -----------------------------------------------------------------------------
// Save / load.
//
// New schema, NO version byte — rewind buffers are in-process only and
// invalidated by any build that ships this scaffold. Pre-scaffold snapshots
// would deserialise as garbage; the user is expected to Reset after updating,
// which clears the rewind ring. This matches the style used by NextReg (see
// commit history for precedent).
// -----------------------------------------------------------------------------
void Im2Controller::save_state(StateWriter& w) const {
    // Devices.
    for (int i = 0; i < N; ++i) {
        const Device& dv = dev_[i];
        w.write_bool(dv.int_req);
        w.write_bool(dv.int_req_d);
        w.write_bool(dv.int_en);
        w.write_bool(dv.int_unq);
        w.write_bool(dv.int_status);
        w.write_bool(dv.im2_int_req);
        w.write_u8(static_cast<uint8_t>(dv.state));
        w.write_bool(dv.dma_int_en);
        w.write_bool(dv.exception);
    }
    // Decoder.
    w.write_u8(static_cast<uint8_t>(dec_state_));
    w.write_bool(reti_seen_pulse_);
    w.write_bool(retn_seen_pulse_);
    w.write_bool(reti_decode_);
    w.write_bool(dma_delay_ctrl_);
    w.write_u8(im_mode_);
    // Pulse.
    w.write_bool(pulse_int_n_);
    w.write_u8(pulse_count_);
    w.write_bool(machine_48_or_p3_);
    // NR 0xC0.
    w.write_u8(vector_base_msb3_);
    w.write_bool(im2_mode_);
    w.write_bool(stackless_nmi_);
    // DMA delay.
    w.write_u16(dma_int_en_mask14_);
    w.write_bool(im2_dma_delay_latched_);
    // ACK book-keeping.
    w.write_i32(last_acked_);
    // Legacy API mask.
    w.write_u16(legacy_mask_);
}

void Im2Controller::load_state(StateReader& r) {
    for (int i = 0; i < N; ++i) {
        Device& dv = dev_[i];
        dv.int_req      = r.read_bool();
        dv.int_req_d    = r.read_bool();
        dv.int_en       = r.read_bool();
        dv.int_unq      = r.read_bool();
        dv.int_status   = r.read_bool();
        dv.im2_int_req  = r.read_bool();
        dv.state        = static_cast<DevState>(r.read_u8());
        dv.dma_int_en   = r.read_bool();
        dv.exception    = r.read_bool();
    }
    dec_state_       = static_cast<DecState>(r.read_u8());
    reti_seen_pulse_ = r.read_bool();
    retn_seen_pulse_ = r.read_bool();
    reti_decode_     = r.read_bool();
    dma_delay_ctrl_  = r.read_bool();
    im_mode_         = r.read_u8();

    pulse_int_n_      = r.read_bool();
    pulse_count_      = r.read_u8();
    machine_48_or_p3_ = r.read_bool();

    vector_base_msb3_ = r.read_u8();
    im2_mode_         = r.read_bool();
    stackless_nmi_    = r.read_bool();

    dma_int_en_mask14_     = r.read_u16();
    im2_dma_delay_latched_ = r.read_bool();

    last_acked_  = r.read_i32();
    legacy_mask_ = r.read_u16();
}
