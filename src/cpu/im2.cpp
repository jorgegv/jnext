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
// Tick — Phase 1 stub. Phase 2 Agent B fills in device state machine stepping.
// -----------------------------------------------------------------------------
void Im2Controller::tick(uint32_t /*master_cycles*/) {
    // intentionally empty
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
    // Phase 2 Agent A: decoder emits reti_seen_pulse_ synchronously via
    // on_m1_cycle(); the emulator-level lambda forwards that pulse to this
    // entry point. For the legacy path there is nothing to do — int_req is
    // cleared by callers on their own. Phase 2 Agent B will extend this
    // method to walk the device daisy chain (S_ISR → S_0 for the
    // acknowledged interrupter whose IEI is high, per im2_device.vhd:
    // 123-128) and the corresponding IEI/IEO propagation.
}

// -----------------------------------------------------------------------------
// DevIdx peripheral-facing API — Phase 1 stubs.
// -----------------------------------------------------------------------------
void Im2Controller::raise_req(DevIdx d) {
    dev_[static_cast<int>(d)].int_req = true;
}

void Im2Controller::clear_req(DevIdx d) {
    dev_[static_cast<int>(d)].int_req = false;
}

void Im2Controller::raise_unq(DevIdx d) {
    dev_[static_cast<int>(d)].int_unq = true;
}

void Im2Controller::clear_status(DevIdx d) {
    Device& dv = dev_[static_cast<int>(d)];
    dv.int_status   = false;
    dv.im2_int_req  = false;
}

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
    // Phase 2 Agent B wires the live INT line. For Phase 1 we return false;
    // the legacy path remains via emulator polling has_pending() / get_vector().
    return false;
}

uint8_t Im2Controller::ack_vector() {
    // Phase 2 Agent B: latch device to S_ACK, return compose_vector(). For
    // Phase 1 we return 0xFF (harmless default — legacy get_vector() still
    // drives the live interrupt).
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

bool Im2Controller::ieo(DevIdx /*d*/) const {
    // Phase 2 Agent B implements the daisy-chain propagation. For Phase 1
    // return true (pass-through), matching a fully-idle chain.
    return true;
}

// -----------------------------------------------------------------------------
// Private helpers — all stubs in Phase 1.
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
void Im2Controller::step_devices()                      { /* Phase 2 Agent B */ }
void Im2Controller::step_pulse()                        { /* Phase 2 Agent C */ }
uint8_t Im2Controller::compute_vector() const           { return 0xFF; }
bool Im2Controller::device_ieo(int /*i*/) const         { return true; }
void Im2Controller::propagate_isr_serviced()            { /* Phase 2 Agent B */ }

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
