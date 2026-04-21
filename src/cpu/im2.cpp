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
    // Agent A will also surface reti_seen_pulse_ via the decoder; step_devices()
    // reads that pulse and does the same walk. This legacy entry point remains
    // because emulator.cpp:170 still calls im2_.on_reti() directly on RETI
    // decode — keeping both paths converges on the same end state.
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

bool Im2Controller::ieo(DevIdx d) const {
    // Public debug accessor — exposes the device's o_ieo signal, which is
    // the IEI input of the NEXT device in the priority chain.
    return device_ieo(static_cast<int>(d));
}

// -----------------------------------------------------------------------------
// Private helpers.
// -----------------------------------------------------------------------------
void Im2Controller::advance_decoder(uint8_t /*opcode*/) { /* Phase 2 Agent A */ }

// step_devices — per-CLK_CPU-rising-edge tick.
//
// Shared between Agents B and D: Agent D owns the wrapper half (int_req_d
// edge detection, im2_int_req latching, int_status bit — see
// im2_peripheral.vhd:90-178). Agent B owns the device state-machine half
// (S_0/S_REQ/S_ACK/S_ISR transitions — see im2_device.vhd:102-132). The
// correct in-cycle ordering is: wrapper step FIRST (so im2_int_req reflects
// this cycle's input), THEN state-machine step. Agent D will extend this
// body to call its own wrapper step before the state-machine walk below.
//
// VHDL synchronous-update semantic: at a rising CLK edge, every device
// computes state_next from the CURRENT value of every signal, then all
// states update simultaneously. We preserve that by first snapshotting
// each device's IEI (derived from the CURRENT states), then applying
// transitions using those snapshots — guaranteeing that clearing device k
// from S_ISR→S_0 does NOT cascade into device k+1 in the same tick.
void Im2Controller::step_devices() {
    // <- Agent D inserts wrapper step loop here (edge detect → im2_int_req).

    // Snapshot per-device IEI derived from CURRENT (pre-transition) states.
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

    // Apply state-machine transitions using the snapshots.
    for (int i = 0; i < N; ++i) {
        step_state_machine_with_iei(i, iei_snap[i]);
    }
    // Note: reti_seen_pulse_ clearing is Agent A's contract (one-cycle pulse).
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
            }
            break;
    }
}

void Im2Controller::step_pulse()                        { /* Phase 2 Agent C */ }

uint8_t Im2Controller::compute_vector() const {
    // VHDL zxnext.vhd:1999 — im2_vector = nr_c0_im2_vector[2:0] & im2_vec[3:0] & '0'.
    //   Bits 7:5 = nr_c0_im2_vector[2:0]   (vector_base_msb3_)
    //   Bits 4:1 = im2_vec[3:0]            (index of S_ACK device)
    //   Bit    0 = '0'                     (vectors are even-aligned)
    //
    // VHDL im2_device.vhd:155 drives o_vec<=i_vec only while state = S_ACK
    // (or state_next = S_ACK, i.e. during the ack latch edge). Peripherals
    // OR-reduce across all 14 devices (peripherals.vhd:134-144); since at
    // most one device is in S_ACK at a time, the OR is a plain selection.
    //
    // When no device is in S_ACK, the OR-reduction is 0 — we mirror that
    // exactly. (The scaffold stub returned 0xFF, which would have been
    // observable via ack_vector() bailout; now ack_vector() returns 0xFF
    // for that case explicitly, and compute_vector() stays honest.)
    uint8_t idx = 0;
    for (int i = 0; i < N; ++i) {
        if (dev_[i].state == DevState::S_ACK) {
            idx = static_cast<uint8_t>(i & 0x0F);
            break;
        }
    }
    // If no device in S_ACK, idx stays 0 and the composed byte is just the
    // base<<5. Callers should only invoke compute_vector() from ack_vector()
    // or after confirming a device is acked.
    return static_cast<uint8_t>(((vector_base_msb3_ & 0x07) << 5) | (idx << 1));
}

bool Im2Controller::device_ieo(int i) const {
    // VHDL im2_device.vhd:136-146 — IEO driven from state:
    //   S_0   : o_ieo <= i_iei           (pass through when idle)
    //   S_REQ : o_ieo <= i_iei and reti_decode   (block unless during RETI decode)
    //   other : o_ieo <= '0'             (S_ACK / S_ISR block all downstream)
    //
    // Chain from device 0 upward. Device 0's i_iei is hardwired to '1'
    // (peripherals.vhd:82 + zxnext.vhd:1984).
    //
    // Note: we walk iteratively rather than recursively — 14 devices max.
    // The cost is O(N) per lookup; callers (int_line_asserted, ack_vector)
    // invoke device_ieo() O(N) times per tick, so O(N^2) per tick = 196
    // ops worst case. Negligible.
    if (i < 0 || i >= N) return false;
    bool iei = true;  // device 0's hard-wired IEI
    for (int k = 0; k <= i; ++k) {
        const Device& d = dev_[k];
        bool ieo;
        switch (d.state) {
            case DevState::S_0:   ieo = iei;                         break;
            case DevState::S_REQ: ieo = iei && reti_decode_;         break;
            case DevState::S_ACK: ieo = false;                       break;
            case DevState::S_ISR: ieo = false;                       break;
            default:              ieo = false;                       break;
        }
        if (k == i) return ieo;
        iei = ieo;  // device k+1's IEI = device k's IEO
    }
    return iei;  // unreachable
}

void Im2Controller::propagate_isr_serviced()            { /* Phase 2 Agent D */ }

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
