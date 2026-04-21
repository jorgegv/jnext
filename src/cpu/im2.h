#pragma once
#include <cstdint>

class StateWriter;
class StateReader;

// Legacy enum, kept for backwards compatibility with emulator.cpp and peripherals.
// New code should use Im2Controller::DevIdx instead. The two enums map via
// Im2Controller's internal to_devidx() helper — see im2.cpp.
//
// NOTE (scaffold): this enum's numeric layout is preserved exactly as the pre-scaffold
// stub so that existing legacy mask/vector semantics (vector = 2*i) continue to work
// for any caller still passing uint16_t masks over Im2Level positions.
enum class Im2Level : int {
    FRAME_IRQ = 0, LINE_IRQ, CTC_0, CTC_1, CTC_2, CTC_3,
    UART_TX_0, UART_RX_0, UART_TX_1, UART_RX_1,
    DMA, DIVMMC, ULA_EXTRA, MULTIFACE,
    COUNT = 14
};

/// Im2Controller — IM2 interrupt fabric owner.
///
/// Scaffold (Phase 1) implementation: full API surface matching the VHDL
/// zxnext/im2_control/im2_device/im2_peripheral/peripherals files, but only
/// the legacy path (raise/clear/has_pending/get_vector/set_mask/on_reti) is
/// functionally wired. All new DevIdx-based entry points exist as stubs that
/// compile and do not affect behaviour — Phase 2 agents will implement them.
class Im2Controller {
public:
    // VHDL priority order (zxnext.vhd:1941). Index 0 = highest priority.
    // Slots 7-10 (CTC4..CTC7) are wired to constant 0 in the Next VHDL
    // (zxnext.vhd:4092) but we keep the slots for fabric parity.
    enum class DevIdx : int {
        LINE = 0,
        UART0_RX = 1,
        UART1_RX = 2,
        CTC0 = 3, CTC1 = 4, CTC2 = 5, CTC3 = 6,
        CTC4 = 7, CTC5 = 8, CTC6 = 9, CTC7 = 10,
        ULA = 11,
        UART0_TX = 12,
        UART1_TX = 13,
        COUNT = 14
    };

    // Per VHDL im2_device.vhd:83 state machine.
    enum class DevState : uint8_t { S_0 = 0, S_REQ = 1, S_ACK = 2, S_ISR = 3 };

    Im2Controller();
    void reset();

    // Per-cycle tick. Called from Emulator::run_frame inner loop.
    // Phase 1 stub — empty body; Phase 2 Agent B fills in device state machine.
    void tick(uint32_t master_cycles);

    // ── Legacy API (retained as compatibility wrappers; new code should use the
    //    DevIdx-based methods below) ─────────────────────────────────────────
    void raise(Im2Level level);
    void clear(Im2Level level);
    bool has_pending() const;
    uint8_t get_vector() const;
    void set_mask(uint16_t mask);
    void on_reti();

    // ── DevIdx-based API (peripheral-facing) ───────────────────────────────
    void raise_req(DevIdx d);            // asserts i_int_req (rising edge captured)
    void clear_req(DevIdx d);            // deasserts (normally via isr_serviced)
    void raise_unq(DevIdx d);            // one-shot unqualified pulse (NR 0x20)
    void clear_status(DevIdx d);         // i_int_status_clear one-shot (NR 0xC8/C9/CA)
    bool int_status(DevIdx d) const;     // o_int_status (int_status OR im2_int_req)
    uint8_t int_status_mask_c8() const;  // packs line/ULA for NR 0xC8 read
    uint8_t int_status_mask_c9() const;  // packs CTC 7..0 for NR 0xC9 read
    uint8_t int_status_mask_ca() const;  // packs UART for NR 0xCA read

    // ── Enable bits (NR 0xC4/C5/C6 writes) ─────────────────────────────────
    void set_int_en(DevIdx d, bool en);  // i_int_en
    void set_int_en_c4(uint8_t val);     // NR 0xC4 — ULA (b0) + line (b1) + expbus (b7)
    void set_int_en_c5(uint8_t val);     // NR 0xC5 — CTC 7:0
    void set_int_en_c6(uint8_t val);     // NR 0xC6 — UART

    // ── NR 0xC0 ────────────────────────────────────────────────────────────
    void set_vector_base(uint8_t msb3);  // nr_c0_im2_vector[2:0], vhdl:1999
    uint8_t vector_base() const;
    void set_mode(bool im2_mode);        // nr_c0_int_mode_pulse_0_im2_1, vhdl:1975
    bool is_im2_mode() const;
    void set_stackless_nmi(bool v);      // nr_c0_stackless_nmi; F-deferred (store only)
    bool stackless_nmi() const;

    // ── NR 0xCC/CD/CE DMA int enables ──────────────────────────────────────
    void set_dma_int_en_mask(uint16_t mask14);  // compose_im2_dma_int_en() product
    bool dma_int_pending() const;        // o_dma_int OR-reduction, vhdl:1994
    bool dma_delay() const;              // latched im2_dma_delay, vhdl:2007

    // ── Z80 CPU integration ────────────────────────────────────────────────
    //
    // im2_int_n = AND of all device int_n (vhdl:1990). When low AND Z80 is in
    // IM=2 AND IFF1=1, CPU services. When high OR pulse_int_n is low, the CPU
    // sees pulse_int_n AND im2_int_n (vhdl:1840).
    bool int_line_asserted() const;      // final INT line to Z80
    uint8_t ack_vector();                // called by Z80 at IntAck; latches device to S_ACK
    void on_m1_cycle(uint16_t pc, uint8_t opcode);  // drives RETI/RETN decoder
    uint8_t im_mode() const;             // 0/1/2 latch, VHDL im2_control.vhd:229

    // ── RETI/RETN decoder observers (test + emulator forwarder) ───────────
    //
    // Each is a one-cycle pulse / snapshot valid only within the call
    // following the triggering on_m1_cycle(). VHDL citations:
    //   - reti_seen  : im2_control.vhd:234  (state_next = S_ED4D_T4)
    //   - retn_seen  : im2_control.vhd:236  (state_next = S_ED45_T4)
    //   - reti_decode: im2_control.vhd:233  (state    = S_ED_T4)
    //   - dma_delay  : im2_control.vhd:238  (state ∈ {S_ED_T4,S_ED4D_T4,
    //                                        S_ED45_T4,S_SRL_T1,S_SRL_T2})
    bool reti_seen_this_cycle() const { return reti_seen_pulse_; }
    bool retn_seen_this_cycle() const { return retn_seen_pulse_; }
    bool reti_decode_active()   const { return reti_decode_;    }
    bool dma_delay_control()    const { return dma_delay_ctrl_; }

    // ── Pulse mode ─────────────────────────────────────────────────────────
    bool pulse_int_n() const;                  // vhdl:2020-2031
    void set_machine_timing_48_or_p3(bool v);  // pulse duration gate, vhdl:2033
    void set_machine_timing_pentagon(bool v);  // VHDL-documented as no-op

    // ── Debug accessors (for tests) ────────────────────────────────────────
    DevState state(DevIdx d) const;
    bool ieo(DevIdx d) const;   // o_ieo of device d's wrapper

    // ── Save/load ──────────────────────────────────────────────────────────
    void save_state(StateWriter& w) const;
    void load_state(StateReader& r);

private:
    struct Device {
        // VHDL im2_peripheral.vhd signals.
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

    static constexpr int N = static_cast<int>(DevIdx::COUNT);
    Device dev_[N];

    // RETI/RETN/IM decoder (encapsulates im2_control.vhd state machine).
    enum class DecState : uint8_t { S_0, S_ED_T4, S_ED4D_T4, S_ED45_T4,
                                    S_CB_T4, S_SRL_T1, S_SRL_T2, S_DDFD_T4 };
    DecState dec_state_       = DecState::S_0;
    bool     reti_seen_pulse_ = false;   // one-cycle pulse
    bool     retn_seen_pulse_ = false;
    bool     reti_decode_     = false;   // state == S_ED_T4
    bool     dma_delay_ctrl_  = false;   // S_ED_T4 | S_ED4D_T4 | S_ED45_T4 | S_SRL_*
    uint8_t  im_mode_         = 0;

    // Pulse fabric state (vhdl:2017-2044).
    bool    pulse_int_n_      = true;
    uint8_t pulse_count_      = 0;
    bool    machine_48_or_p3_ = false;

    // NR 0xC0 state.
    uint8_t vector_base_msb3_ = 0;
    bool    im2_mode_         = false;   // true = hw im2, false = legacy pulse
    bool    stackless_nmi_    = false;   // F-deferred

    // DMA delay latch (vhdl:2007).
    uint16_t dma_int_en_mask14_     = 0;
    bool     im2_dma_delay_latched_ = false;

    // Which device got ACKed in the current IntAck cycle (for RETI → S_0).
    int last_acked_ = -1;

    // Legacy-API compatibility state (mask of Im2Level bits).
    uint16_t legacy_mask_ = 0xFFFF;

    // Helpers — all stubs in Phase 1; filled in by Phase 2 agents.
    DevIdx to_devidx(Im2Level lvl) const;
    void advance_decoder(uint8_t opcode);
    void step_devices();
    void step_pulse();
    uint8_t compute_vector() const;
    bool device_ieo(int i) const;
    void propagate_isr_serviced();

    // Agent B state-machine half of step_devices().
    // Invoked from step_devices() AFTER Agent D's wrapper-layer step for
    // device i (edge detect → im2_int_req latch). Does ONLY the state-
    // machine transitions in im2_device.vhd:102-132 (S_0/S_REQ/S_ACK/S_ISR).
    //
    // `iei` is the snapshot of device i's i_iei input sampled at the start
    // of the tick, BEFORE any device transitions — mirrors VHDL's
    // synchronous-update rule (state_next computed from current states,
    // applied simultaneously at the next rising edge).
    void step_state_machine_with_iei(int i, bool iei);
};
