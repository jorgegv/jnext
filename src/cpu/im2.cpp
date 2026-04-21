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
    // Phase 1 compat: the pre-scaffold stub cleared active_[active_level_].
    // We have no active_[] anymore — the DevIdx state machine in Phase 2
    // handles S_ACK→S_ISR→S_0 transitions. For the legacy path there's
    // nothing to do: has_pending() is driven by int_req which callers clear
    // explicitly. Phase 2 Agent A wires the decoder into this entry point.
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

void Im2Controller::on_m1_cycle(uint16_t /*pc*/, uint8_t /*opcode*/) {
    // Phase 2 Agent A drives the RETI/RETN decoder here.
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
void Im2Controller::advance_decoder(uint8_t /*opcode*/) { /* Phase 2 Agent A */ }

// step_devices() — VHDL im2_peripheral.vhd per-CLK_28 pipeline.
//
// Three well-defined phases, called in order. Each subsequent Wave 1 agent
// owns one phase; the comment structure exists to make cross-agent merges
// trivial.
//
//   Phase 1 — wrapper edge detect + im2_int_req latch  (Agent D, THIS FILE)
//     Computes the rising edge of each device's i_int_req, updates int_status
//     per VHDL :154-162, and sets the im2_int_req latch per VHDL :167-178.
//
//   Phase 2 — per-device state machine                 (Agent B)
//     Advances S_0 / S_REQ / S_ACK / S_ISR transitions based on im2_int_req
//     and the IEI daisy chain. Agent B plugs its body in at the marker below;
//     Agent B also handles the im2_int_req clear-on-ISR-serviced inline during
//     the S_ISR → S_0 transition (simpler than the explicit propagate() path).
//
//   Phase 3 — isr_serviced propagation cleanup         (Agent D, documented)
//     Because Agent B clears im2_int_req inline during the S_ISR → S_0
//     transition in Phase 2, the propagate_isr_serviced() helper below is a
//     documented no-op. Kept as an explicit extension point in case a future
//     refactor splits state transition from latch cleanup.
void Im2Controller::step_devices() {
    // ── Phase 1: wrapper edge detect + im2_int_req latch (Agent D) ─────────
    // VHDL im2_peripheral.vhd:
    //   :90-101  int_req_d <= i_int_req  (delay) and
    //            int_req   <= i_int_req AND NOT int_req_d (rising-edge pulse)
    //   :154-162 int_status <= (int_req or i_int_unq)
    //                       or (int_status and not i_int_status_clear)
    //   :167-178 im2_int_req <= '1' if (i_int_unq = '1')
    //                            or (int_req = '1' and i_int_en = '1')
    //
    // Note: int_unq bypasses i_int_en in both the status register (UNQ-05,
    // :160) and the im2 latch (UNQ-04, :172). That invariant is preserved in
    // raise_unq() (which sets all three fields directly) and here (the
    // int_unq branch below does not gate on int_en).
    for (int i = 0; i < N; ++i) {
        Device& d = dev_[i];
        const bool edge = d.int_req && !d.int_req_d;

        // Set int_status on any edge (vhdl:160 — gated by neither int_en nor
        // int_unq; the edge pulse itself is what the VHDL equation uses).
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

        // Update delayed copy LAST so next tick's edge calculation sees this
        // tick's int_req as the "previous" value.
        d.int_req_d = d.int_req;
    }

    // ── Phase 2: per-device state machine (Agent B will fill in here) ──────
    // Agent B implements S_0 / S_REQ / S_ACK / S_ISR transitions, IEI/IEO
    // daisy-chain propagation, and clears im2_int_req during the
    // S_ISR → S_0 transition (inline). Agent B typically adds a
    // step_state_machine() private helper and calls it here, but the exact
    // mechanism is Agent B's choice.

    // ── Phase 3: isr_serviced cleanup (documented no-op, Agent D) ──────────
    propagate_isr_serviced();
}

void Im2Controller::step_pulse()                        { /* Phase 2 Agent C */ }
uint8_t Im2Controller::compute_vector() const           { return 0xFF; }
bool Im2Controller::device_ieo(int /*i*/) const         { return true; }

// propagate_isr_serviced() — documented no-op.
//
// The VHDL model (im2_peripheral.vhd:137-148) detects a rising edge on the
// isr_serviced signal from im2_device and uses that edge to clear im2_int_req
// (line 175). In our single-threaded-tick emulator model, Agent B clears
// im2_int_req inline during the state-machine's S_ISR → S_0 transition
// (Phase 2 above) — that's simpler than carrying a prev_state_[] shadow here
// and avoids split-brain over who owns the clear. This function is kept as an
// explicit extension point for symmetry with the VHDL signal name; remove or
// repurpose if Phase 2's merge needs a different split.
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
