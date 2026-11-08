#pragma once
#include <cstddef>
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
namespace jnext { namespace save { class StateDesc; } }

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

    // Per-instruction tick. Called from Emulator::step_one_instruction().
    // The argument is the number of CPU T-states (i_CLK_CPU rising edges)
    // the just-completed Z80 instruction consumed at the active CPU speed;
    // it is the per-tick advance for the pulse-fabric counter
    // (zxnext.vhd:2035-2044). NOT 28 MHz master cycles. Tests that call
    // tick(1) mean "advance one CPU clock edge".
    void tick(uint32_t tstates_for_pulse);

    // ── GH #265 — exact timing (the Emulator's path) ──────────────────────
    //
    // tick(tstates) above resolves every request raised during an
    // instruction when that instruction ends, and times the /INT pulse in
    // whole instructions. The Emulator instead stamps each request with the
    // CLK_28 edge it happened on and resolves the fabric AT an edge: inside
    // an instruction (a status read at the IN's latch point, a NextREG
    // write at the edge it commits on) and at the instruction's end. Times
    // are master-cycle (CLK_28 edge) numbers on the Emulator's clock_.
    //
    // Pipeline modelled (all CLK_28 unless stated):
    //   * a request raised "at edge te" has i_int_req high during the cycle
    //     that edge starts, [te, te+1) — the edge detect int_req is high
    //     then (im2_peripheral.vhd:90-101);
    //   * int_status and im2_int_req are set at edge te+1 (:154-178);
    //   * pulse_int_n falls on the CLK_28 FALLING edge te+0.5
    //     (zxnext.vhd:2017-2031) and rises on the falling edge after
    //     pulse_count, advanced on CPU rising edges, reaches 32/36
    //     (:2033-2044): with E_1 the first CPU rising edge after the fall,
    //     /INT is low for CPU edges E_1 .. E_1 + (32|36 - 1)·d;
    //   * a device enters S_REQ on the first CPU rising edge after
    //     im2_int_req is set (im2_device.vhd:91-107).
    static constexpr uint64_t kNoTime = ~uint64_t{0};

    /// i_int_req pulsed high during the CLK_28 cycle edge @p at starts.
    void raise_req(DevIdx d, uint64_t at);
    /// i_int_unq (NR 0x20) pulsed high during the cycle edge @p at starts.
    void raise_unq(DevIdx d, uint64_t at);
    /// i_int_status_clear applied on edge @p at_edge: a status set on that
    /// same edge survives (int_status <= int_req OR ... OR (int_status AND
    /// NOT clear), im2_peripheral.vhd:160).
    void clear_status(DevIdx d, uint64_t at_edge);

    /// Resolve every pending request raised on an edge <= @p edge, in edge
    /// order: status and im2_int_req latches and the pulse. @p grid is a CPU
    /// rising edge (the start of the instruction in progress) and @p d the
    /// master cycles per T-state, placing the pulse's CPU edges. A request
    /// raised before @p grid (during a DMA burst, boot hold or parked slot,
    /// where no instruction runs) keeps its time for the status latch but
    /// its pulse starts at @p grid: the CPU does not see a pulse it could
    /// not have sampled, as before the exact timing (see the Emulator).
    void latch_edges_until(uint64_t edge, uint64_t grid, uint32_t d);

    /// Per-instruction tick with exact timing: latch_edges_until(slot_end),
    /// end a pulse whose last low edge has passed, then the state machines
    /// as tick(tstates). @p slot_start is the instruction's first edge (a
    /// CPU rising edge), @p slot_end its last. @p m1_cycles is the number of
    /// M1 (opcode fetch) cycles the instruction opened with, 4 T-states
    /// each: S_0 -> S_REQ needs i_m1_n = '1' (im2_device.vhd:106), which
    /// rules out the CPU edges ending T1 and T2 of each of them, and of the
    /// M1 that opens the next instruction.
    void tick(uint32_t tstates_for_pulse, uint64_t slot_start,
              uint64_t slot_end, uint32_t d, uint32_t m1_cycles);

    /// o_int_status as a CLK_28 register loaded on edge @p edge sees it:
    /// set by a request no later than edge - 2 (status set on edge <= edge-1).
    /// kNoTime = the latched value regardless of time.
    bool int_status(DevIdx d, uint64_t edge) const;
    uint8_t int_status_mask_c8(uint64_t edge) const;
    uint8_t int_status_mask_c9(uint64_t edge) const;
    uint8_t int_status_mask_ca(uint64_t edge) const;

    /// pulse_int_n low just before rising edge @p edge — what a register
    /// clocked on that edge (the T80's INT_s on a CPU edge, port_253b_dat
    /// for NR 0x22 bit 7) captures.
    bool pulse_low_before(uint64_t edge) const;
    /// The CPU rising edges of the current (or last) timed pulse at which
    /// INT_s is set: E_1 and E_N. Valid once a timed pulse has started.
    uint64_t pulse_first_edge() const { return pulse_e1_; }
    uint64_t pulse_last_edge() const  { return pulse_en_; }
    /// GH #265 — true when the end-of-instruction work has nothing to do: in
    /// pulse mode (no IM2-mode /INT line can be asserted), tick() would take
    /// its quiescent early-out (Task 27 C-IM2) even after
    /// set_nmi_activated(@p nmi_activated). Quiescent also means the pulse
    /// is high (compute_quiescent()) and no pulse start is waiting for
    /// take_pulse_started(): latch_edges_until() starts one only while
    /// resolving a request, and clears quiescent_ when it does. Emulator's
    /// per-instruction fast path; the tick it skips only stores intra-tick
    /// scratch (pulse_count_advance_, read solely by step_pulse() in tick()).
    bool slot_idle(bool nmi_activated) const {
        return quiescent_ && !reti_seen_pulse_ && !im2_mode_
            && nmi_activated == nmi_activated_;
    }
    /// True once per pulse started since the last call.
    bool take_pulse_started() {
        const bool s = pulse_started_;
        pulse_started_ = false;
        return s;
    }
    /// IM2 mode: the earliest CPU edge a device driving int_line_asserted()
    /// entered S_REQ on (its o_int_n is low from then, im2_device.vhd:150).
    /// kNoTime when the line is not asserted.
    uint64_t int_line_low_since() const;

    /// A CPU-speed change at CLK_28 edge @p now (an instruction boundary):
    /// the pulse counts CPU clock edges (zxnext.vhd:2035-2044), so the edges
    /// of a timed pulse still to come are re-placed at the new divisor @p d.
    /// Idempotent — the pulse remembers the divisor its edges are on.
    void set_cpu_divisor(uint64_t now, uint32_t d);

    /// The timing fields above, for the Emulator's appended snapshot block:
    /// five u64 per device, then the pulse's flag, three u64 and its u32
    /// divisor.
    static constexpr std::size_t kTimingStateBytes =
        static_cast<std::size_t>(DevIdx::COUNT) * 5 * sizeof(uint64_t)
        + sizeof(uint8_t) + 3 * sizeof(uint64_t) + sizeof(uint32_t);
    void save_timing(StateWriter& w) const;
    void load_timing(StateReader& r);

    /// GH #27 S3 — the SECOND field list (design §9.5(2)). These fields
    /// travel in the Emulator stream's `int_timing` block, not in this
    /// subsystem's own, so they need a declaration of their own.
    void describe_timing(jnext::save::StateDesc& d);
    void reset_timing();

    // ── Legacy API (retained as compatibility wrappers; new code should use the
    //    DevIdx-based methods below) ─────────────────────────────────────────
    void raise(Im2Level level);
    void clear(Im2Level level);
    bool has_pending() const;
    uint8_t get_vector() const;
    void set_mask(uint16_t mask);
    void on_reti();
    // VHDL: i_retn_seen is consumed by divmmc/mmc/reset paths only; the
    // im2_device.vhd state machine does NOT react to RETN (only RETI clears
    // S_ISR per :123-128). This entry point is therefore a no-op for fabric
    // state but is retained as a parallel hook to on_reti() for clarity at
    // the emulator's M1 lambda call site (G87). Side effects on the divmmc
    // are routed via DivMmc::on_retn() directly.
    void on_retn();

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
    void set_stackless_nmi(bool v);      // nr_c0_stackless_nmi; CPU bus path wired by Emulator
    bool stackless_nmi() const;

    // ── NR 0xCC/CD/CE DMA int enables ──────────────────────────────────────
    void set_dma_int_en_mask(uint16_t mask14);  // compose_im2_dma_int_en() product
    bool dma_int_pending() const;        // o_dma_int OR-reduction, vhdl:1994
    bool dma_delay() const;              // latched im2_dma_delay, vhdl:2007

    // ── NMI-activated DMA-delay path (Wave E, vhdl:2007 second term) ──────
    //
    //   im2_dma_delay <= im2_dma_int
    //                    OR (nmi_activated AND nr_cc_dma_int_en_0_7)
    //                    OR (im2_dma_delay AND dma_delay)
    //
    // Both inputs are pushed from Emulator:
    //   - set_nmi_activated(bool) is called per tick() before im2_.tick(),
    //     sourced from NmiSource::is_activated().
    //   - set_nr_cc_dma_int_en_0_7(bool) is called from the NR 0xCC write
    //     handler (bit 7 = nr_cc_dma_delay_on_nmi).
    // Both reset to false on reset().
    void set_nmi_activated(bool v);
    void set_nr_cc_dma_int_en_0_7(bool v);
    bool nmi_activated() const;
    bool nr_cc_dma_int_en_0_7() const;

    // ── Z80 CPU integration ────────────────────────────────────────────────
    //
    // im2_int_n = AND of all device int_n (vhdl:1990). When low AND Z80 is in
    // IM=2 AND IFF1=1, CPU services. When high OR pulse_int_n is low, the CPU
    // sees pulse_int_n AND im2_int_n (vhdl:1840).
    bool int_line_asserted() const;      // final INT line to Z80
    uint8_t ack_vector();                // called by Z80 at IntAck; latches device to S_ACK
    void on_m1_cycle(uint16_t pc, uint8_t opcode);  // drives RETI/RETN decoder
    uint8_t im_mode() const;             // 0/1/2 latch, VHDL im2_control.vhd:229
    /// Seed the IM latch for a loader that puts the CPU in an interrupt mode
    /// without executing an IM instruction (the decoder above is the latch's
    /// only other writer), so NR 0xC0 bits 2:1 and the IM2 gates agree with
    /// the CPU. Values above 2 are ignored.
    void set_im_mode(uint8_t mode);

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

    // G87 — cumulative pulse counters (test observability).
    // advance_decoder() bumps these on every transition into S_ED4D_T4 /
    // S_ED45_T4 (i.e. one increment per RETI/RETN actually decoded). Used
    // by ctc_interrupts_test IM2C-G87-01/02 to confirm the FSM advanced
    // through the ED + ext byte sequence; production behaviour is
    // unchanged.
    uint32_t reti_seen_count() const { return reti_seen_count_; }
    uint32_t retn_seen_count() const { return retn_seen_count_; }

    // ── Pulse mode ─────────────────────────────────────────────────────────
    bool pulse_int_n() const;                  // vhdl:2020-2031
    void set_machine_timing_48_or_p3(bool v);  // pulse duration gate, vhdl:2033
    /// Test-observability getter for the pulse-duration gate, mirroring
    /// Z80Cpu::machine_timing_48_or_p3(). The two consumers of VHDL :2033
    /// must stay in lock-step; without a getter on this side only half of
    /// that fan-out can be asserted (GH #232).
    bool machine_timing_48_or_p3() const { return machine_48_or_p3_; }

    // ── Debug accessors (for tests) ────────────────────────────────────────
    DevState state(DevIdx d) const;
    bool ieo(DevIdx d) const;   // o_ieo of device d's wrapper

    // ── Save/load ──────────────────────────────────────────────────────────
    void save_state(StateWriter& w) const;
    void load_state(StateReader& r);

    /// GH #27 S3 — the ONE field list (design §9.2).
    void describe_state(jnext::save::StateDesc& d);

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
        // GH #265 — exact timing (see latch_edges_until()).
        uint64_t req_at     = kNoTime; // edge of the pending int_req pulse
        uint64_t unq_at     = kNoTime; // edge of the pending int_unq pulse
        uint64_t status_at  = 0;       // edge int_status was set on
        uint64_t im2_req_at = 0;       // edge im2_int_req was set on
        uint64_t sreq_at    = 0;       // CPU edge the device entered S_REQ on
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
    uint32_t reti_seen_count_ = 0;       // G87 — test observability counter
    uint32_t retn_seen_count_ = 0;       // G87 — test observability counter

    // Pulse fabric state (vhdl:2017-2044).
    bool     pulse_int_n_         = true;
    uint8_t  pulse_count_         = 0;
    // EOD-30c — per-tick advance for pulse_count_, set by tick() from the
    // T-states (CPU-clock edges) consumed by the just-completed Z80
    // instruction. step_pulse() reads this to advance pulse_count_ by the
    // correct number of CPU cycles. Default 1 preserves the legacy
    // per-tick-call semantic used by ctc/nmi tests that pass tick(1).
    uint32_t pulse_count_advance_ = 1;
    bool     machine_48_or_p3_    = false;
    // GH #265 — a pulse started by latch_edges_until() is timed: it fell on
    // the falling edge after pulse_te_, and INT_s is set on CPU edges
    // pulse_e1_ .. pulse_en_. pulse_count_ is then kept only as a view.
    bool     pulse_timed_   = false;
    uint64_t pulse_te_      = 0;
    uint64_t pulse_e1_      = 0;
    uint64_t pulse_en_      = 0;
    uint32_t pulse_d_       = 0;       // divisor pulse_e1_/pulse_en_ are on (0: unknown)
    bool     pulse_started_ = false;   // transient, see take_pulse_started()
    // Set for the duration of a timed tick, read by step_pulse() (skipped)
    // and the S_0 -> S_REQ transition (stamps sreq_at).
    bool     timed_tick_      = false;
    uint64_t tick_grid_       = 0;
    uint64_t tick_end_        = 0;
    uint32_t tick_d_          = 8;
    uint32_t tick_m1_         = 0;
    /// The first CPU edge after @p edge at which im2_device.vhd:106 lets
    /// S_0 -> S_REQ happen (M1_n high), for the timed tick in progress.
    uint64_t sreq_edge_after(uint64_t edge) const;

    // NR 0xC0 state.
    uint8_t vector_base_msb3_ = 0;
    bool    im2_mode_         = false;   // true = hw im2, false = legacy pulse
    bool    stackless_nmi_    = false;   // consumed by Z80Cpu NMIACK/RETN bus path

    // DMA delay latch (vhdl:2007).
    uint16_t dma_int_en_mask14_     = 0;
    bool     im2_dma_delay_latched_ = false;
    // NMI-activated DMA-delay inputs (vhdl:2007 second OR term). Pushed from
    // Emulator: nmi_activated_ from NmiSource::is_activated(); nr_cc_dma_int_en_0_7_
    // from NR 0xCC bit 7 write handler.
    bool     nmi_activated_         = false;
    bool     nr_cc_dma_int_en_0_7_  = false;

    // Which device got ACKed in the current IntAck cycle (for RETI → S_0).
    int last_acked_ = -1;

    // Legacy-API compatibility state (mask of Im2Level bits).
    uint16_t legacy_mask_ = 0xFFFF;

    // Task 27 C-IM2 — quiescence cache for the tick() early-out.
    //
    // True ⟺ the tick() body past the early-out point (step_pulse +
    // step_devices + step_dma_delay + the int_unq/int_req end-of-tick
    // clears) is a provable state no-op, PROVIDED reti_seen_pulse_ is
    // false (that last condition is checked live in tick() because
    // on_m1_cycle() — a per-instruction decoder entry point — must not
    // invalidate this cache). See tick() for the full field-by-field
    // equivalence proof with VHDL citations, and compute_quiescent()
    // for the predicate.
    //
    // Invalidation rule: EVERY public mutator of fabric state consumed
    // by tick() sets quiescent_ = false (raise/clear/raise_req/
    // clear_req/raise_unq/clear_status/set_int_en(+c4/c5/c6)/set_mode/
    // set_dma_int_en_mask/set_nr_cc_dma_int_en_0_7/
    // set_machine_timing_48_or_p3/ack_vector/on_reti/reset/load_state).
    // The two per-instruction entry points are deliberate exceptions:
    //   - on_m1_cycle(): exempt — proof at the tick() early-out.
    //   - set_nmi_activated(): invalidates only on VALUE CHANGE (it is
    //     called once per instruction with an almost-always-unchanged
    //     sample; a value-identical store cannot alter any tick fixed
    //     point).
    // NOT serialized in save_state (derived cache); load_state resets
    // it to false so the first post-load tick recomputes it.
    bool quiescent_ = false;
    bool compute_quiescent() const;

    // Helpers — all stubs in Phase 1; filled in by Phase 2 agents.
    DevIdx to_devidx(Im2Level lvl) const;
    void advance_decoder(uint8_t opcode);
    void step_devices();
    void step_pulse();
    // im2_dma_delay latch (VHDL zxnext.vhd:2001-2010) — Agent F, Wave 3.
    // Runs after step_devices() in each tick(). See im2.cpp for details.
    void step_dma_delay();
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
