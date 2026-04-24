#include "peripheral/nmi_source.h"
#include "core/saveable.h"

// NmiSource — central NMI arbiter subsystem.
//
// Phase 1 scaffold authored under TASK-NMI-SOURCE-PIPELINE-PLAN.md §Phase 1.
// Models `cores/zxnext/src/zxnext.vhd` lines 2089-2170 (+ 3830-3872 /
// 5891 for the NR 0x02 side). Consumer-feedback values for the MF side
// are stubbed `false` until Task 8 (Multiface) lands.

NmiSource::NmiSource()
{
    reset();
}

void NmiSource::reset()
{
    // VHDL:2120, 2149 — FSM defaults to S_NMI_IDLE on i_reset.
    state_ = State::Idle;

    // VHDL:2095-2105 — latches clear on i_reset.
    nmi_mf_     = false;
    nmi_divmmc_ = false;
    nmi_expbus_ = false;

    // VHDL power-on defaults for raw producer inputs.
    mf_button_     = false;
    divmmc_button_ = false;
    expbus_nmi_n_  = true;   // i_BUS_NMI_n idle = '1' (active-low)

    strobe_mf_button_pending_     = false;
    strobe_divmmc_button_pending_ = false;

    nmi_sw_gen_mf_         = false;
    nmi_sw_gen_divmmc_     = false;
    iotrap_strobe_pending_ = false;

    // VHDL:1109-1110, 1222 — NR 0x06 / NR 0x81 power-on bits all '0'.
    mf_enable_              = false;
    divmmc_enable_          = false;
    expbus_debounce_disable_ = false;
    config_mode_            = false;

    // Consumer feedback inputs reset to idle.
    mf_nmi_hold_     = false;
    mf_is_active_    = false;
    divmmc_nmi_hold_ = false;
    divmmc_conmem_   = false;

    // NR 0x02 readback-pending bits clear on reset.
    nr_02_pending_mf_     = false;
    nr_02_pending_divmmc_ = false;

    prev_wr_n_ = true;
}

// ---------------------------------------------------------------------
// Producer-side setters.
// ---------------------------------------------------------------------

void NmiSource::set_mf_button(bool v)
{
    mf_button_ = v;
}

void NmiSource::strobe_mf_button()
{
    // VHDL `hotkey_m1` is an edge pulse: held high for one cycle.
    // We model that by raising the input now and lowering it on the
    // following tick() so the combinational assert path sees the edge.
    mf_button_ = true;
    strobe_mf_button_pending_ = true;
}

void NmiSource::set_divmmc_button(bool v)
{
    divmmc_button_ = v;
}

void NmiSource::strobe_divmmc_button()
{
    divmmc_button_ = true;
    strobe_divmmc_button_pending_ = true;
}

void NmiSource::nr_02_write(uint8_t v)
{
    // VHDL:3830-3872 — NR 0x02 bit 3 = MF software NMI (`nmi_sw_gen_mf`),
    // bit 2 = DivMMC software NMI (`nmi_sw_gen_divmmc`). Writes are
    // one-shot strobes that OR into the producer lines and drive the
    // readback-pending bits that VHDL:5891 reports until the FSM
    // transitions through S_NMI_END.
    const bool req_mf     = (v & 0x08) != 0;
    const bool req_divmmc = (v & 0x04) != 0;
    if (req_mf) {
        nmi_sw_gen_mf_    = true;
        nr_02_pending_mf_ = true;
    }
    if (req_divmmc) {
        nmi_sw_gen_divmmc_    = true;
        nr_02_pending_divmmc_ = true;
    }
}

uint8_t NmiSource::nr_02_read() const
{
    // VHDL:5891 — readback layout. Only bits 3 / 2 are owned by NmiSource
    // (mf_pending / divmmc_pending, auto-cleared on S_NMI_END). Other
    // bits (bus_reset, iotrap, reset_type) are owned elsewhere and
    // composed by Emulator's NR 0x02 read handler; return only our
    // two bits here so the Phase-1 scaffold stays a bare producer API.
    uint8_t r = 0;
    if (nr_02_pending_mf_)     r |= 0x08;
    if (nr_02_pending_divmmc_) r |= 0x04;
    return r;
}

void NmiSource::strobe_iotrap()
{
    iotrap_strobe_pending_ = true;
}

void NmiSource::set_expbus_nmi_n(bool v)
{
    expbus_nmi_n_ = v;
}

// ---------------------------------------------------------------------
// Gate-register setters.
// ---------------------------------------------------------------------

void NmiSource::set_mf_enable(bool v)              { mf_enable_ = v; }
void NmiSource::set_divmmc_enable(bool v)          { divmmc_enable_ = v; }
void NmiSource::set_expbus_debounce_disable(bool v){ expbus_debounce_disable_ = v; }
void NmiSource::set_config_mode(bool v)            { config_mode_ = v; }

// ---------------------------------------------------------------------
// Consumer-feedback setters.
// ---------------------------------------------------------------------

void NmiSource::set_mf_nmi_hold(bool v)     { mf_nmi_hold_ = v; }
void NmiSource::set_mf_is_active(bool v)    { mf_is_active_ = v; }
void NmiSource::set_divmmc_nmi_hold(bool v) { divmmc_nmi_hold_ = v; }
void NmiSource::set_divmmc_conmem(bool v)   { divmmc_conmem_ = v; }

// ---------------------------------------------------------------------
// Combinational producers (VHDL:2089-2091).
// ---------------------------------------------------------------------

bool NmiSource::nmi_assert_mf() const
{
    // VHDL:2090 — (hotkey_m1 OR nmi_sw_gen_mf) AND nr_06_button_m1_nmi_en.
    return (mf_button_ || nmi_sw_gen_mf_) && mf_enable_;
}

bool NmiSource::nmi_assert_divmmc() const
{
    // VHDL:2091 — (hotkey_drive OR nmi_sw_gen_divmmc) AND
    //             nr_06_button_drive_nmi_en.
    return (divmmc_button_ || nmi_sw_gen_divmmc_) && divmmc_enable_;
}

bool NmiSource::nmi_assert_expbus() const
{
    // VHDL:2089 — expbus_eff_en AND NOT expbus_eff_disable_mem AND
    //             i_BUS_NMI_n = '0'. jnext has no expansion bus
    //             emulation today, so the upstream gates reduce to
    //             "pin asserted" only. Phase 1 models the pin + the
    //             NR 0x81 debounce-disable alone; the remaining expbus
    //             gates land in Wave C when ExpBus emulation arrives.
    //             i_BUS_NMI_n is active-low, so assert when == false.
    return !expbus_nmi_n_;
}

// ---------------------------------------------------------------------
// Outputs.
// ---------------------------------------------------------------------

bool NmiSource::is_activated() const
{
    // VHDL:2107 — nmi_activated = nmi_mf OR nmi_divmmc OR nmi_expbus.
    return nmi_mf_ || nmi_divmmc_ || nmi_expbus_;
}

NmiSource::Src NmiSource::latched() const
{
    if (nmi_mf_)     return Src::Mf;
    if (nmi_divmmc_) return Src::DivMmc;
    if (nmi_expbus_) return Src::ExpBus;
    return Src::None;
}

bool NmiSource::nmi_generate_n() const
{
    // VHDL:2164-2170 — /NMI asserted ('0') while the FSM holds the
    // request. S_NMI_FETCH drives the line low until the Z80 takes
    // the NMI; S_NMI_HOLD keeps it low while the consumer is still
    // claiming ownership. S_NMI_END releases the line; S_NMI_IDLE is
    // the resting state.
    //
    // In IDLE with a pending request, VHDL asserts /NMI for one cycle
    // as the FSM advances to FETCH. We model that by driving '0' in
    // IDLE when `is_activated()` is true AND the FSM will advance
    // this cycle (i.e. any latch is set).
    if (state_ == State::Fetch || state_ == State::Hold) return false;
    if (state_ == State::Idle  && is_activated())        return false;
    // VHDL zxnext.vhd:2168 — expbus debounce-disable fast path (FSM bypass).
    if (expbus_debounce_disable_ && nmi_assert_expbus()) {
        return false;
    }
    // S_NMI_END releases /NMI; VHDL reports `nmi_generate_n = 1` there.
    return true;
}

// ---------------------------------------------------------------------
// Observers.
// ---------------------------------------------------------------------

void NmiSource::observe_m1_fetch(uint16_t pc, bool m1, bool mreq)
{
    // VHDL:2135-2138 — FSM advances FETCH -> HOLD on M1 fetch at 0x0066.
    // Any other M1 fetch is ignored here; the DivMmc automap PC=0x0066
    // path keeps its own watcher in `DivMmc::check_automap`.
    if (state_ != State::Fetch) return;
    if (!m1 || !mreq)            return;
    if (pc != 0x0066)            return;
    state_ = State::Hold;
}

void NmiSource::observe_cpu_wr(bool wr_n)
{
    // VHDL:2149-2162 — FSM advances END -> IDLE on the rising edge of
    // cpu_wr_n (the Z80's post-handler stack-pop write cycle finishes).
    const bool rising = (wr_n && !prev_wr_n_);
    prev_wr_n_ = wr_n;
    if (state_ == State::End && rising) {
        state_ = State::Idle;
        // Latches already cleared in END; nothing to do.
    }
}

void NmiSource::observe_retn()
{
    // Optional END advance — the FSM already transitions via
    // `observe_cpu_wr`, so this is a no-op in Phase 1. Hook kept for
    // future Task-8 / stackless-NMI wiring.
}

// ---------------------------------------------------------------------
// FSM tick — combinational re-evaluation + state advance.
// ---------------------------------------------------------------------

void NmiSource::recompute_()
{
    // 1) config_mode force-clear (VHDL:2102-2105) — takes effect every
    //    cycle while the flag is held.
    if (config_mode_) {
        nmi_mf_     = false;
        nmi_divmmc_ = false;
        nmi_expbus_ = false;
        nr_02_pending_mf_     = false;
        nr_02_pending_divmmc_ = false;
        state_ = State::Idle;
        return;
    }

    // 2) Priority latch update (VHDL:2097-2105). Latches set combinationally
    //    from the assert signals with MF > DivMMC > ExpBus priority, and
    //    clear on S_NMI_END transition (handled below).
    //
    //    MF latch gating:
    //      set iff nmi_assert_mf AND NOT port_e3_reg(7)  (VHDL:2098)
    //    DivMMC latch gating:
    //      set iff nmi_assert_divmmc AND NOT mf_is_active AND NOT nmi_mf
    //    ExpBus latch gating:
    //      set iff nmi_assert_expbus AND NOT (nmi_mf OR nmi_divmmc)
    // VHDL:2106 — latch updates gate on `nmi_activated='0'`, which is
    // !is_activated() here (no latch currently set). Equivalent to
    // State::Idle in steady state, but matches VHDL verbatim.
    if (!is_activated()) {
        // VHDL zxnext.vhd:2107 — MF latch requires CONMEM=0 AND !divmmc_nmi_hold.
        if (nmi_assert_mf() && !divmmc_conmem_ && !divmmc_nmi_hold_) {
            nmi_mf_ = true;
        }
        if (nmi_assert_divmmc() && !mf_is_active_ && !nmi_mf_) {
            nmi_divmmc_ = true;
        }
        if (nmi_assert_expbus() && !nmi_mf_ && !nmi_divmmc_) {
            nmi_expbus_ = true;
        }
    }

    // 3) FSM advance. IDLE -> FETCH on any latch set.
    //    FETCH -> HOLD is driven by observe_m1_fetch().
    //    HOLD  -> END  on the matching consumer-feedback hold clearing.
    //    END   -> IDLE is driven by observe_cpu_wr() rising edge.
    switch (state_) {
    case State::Idle:
        if (is_activated()) state_ = State::Fetch;
        break;

    case State::Fetch:
        // FETCH -> HOLD advanced by observe_m1_fetch(); no time-based
        // transition here.
        break;

    case State::Hold: {
        // VHDL:2139-2148 — HOLD -> END when the selected consumer's
        // `hold` signal de-asserts. MF is selected when `nmi_mf` is set,
        // DivMMC otherwise (VHDL:2118).
        const bool hold = nmi_mf_ ? mf_nmi_hold_ : divmmc_nmi_hold_;
        if (!hold) state_ = State::End;
        break;
    }

    case State::End:
        // END clears the three request latches on entry (VHDL:2102-2105
        // via the FSM clear term). Readback-pending bits clear here too
        // per VHDL:5891 auto-clear semantics.
        nmi_mf_     = false;
        nmi_divmmc_ = false;
        nmi_expbus_ = false;
        nr_02_pending_mf_     = false;
        nr_02_pending_divmmc_ = false;
        // END -> IDLE transition handled in observe_cpu_wr().
        break;
    }
}

void NmiSource::tick(uint32_t master_cycles)
{
    // `master_cycles` is the 28 MHz sub-tick count since the last call.
    // The FSM is clocked on the Z80 edge, so we collapse the sub-tick
    // count to a single combinational update per call — the VHDL FSM
    // is idempotent under repeated evaluation of a steady input.
    (void)master_cycles;

    recompute_();

    // Consume the NR 0x02 one-shot software-NMI strobes. VHDL:3830-3872
    // describes `nmi_sw_gen_*` as single-cycle pulses that OR into the
    // assert signals; once the latch has captured them (above) we can
    // release the strobe so the next NR 0x02 write edge is observable.
    nmi_sw_gen_mf_     = false;
    nmi_sw_gen_divmmc_ = false;

    // Consume any pending one-cycle button strobes.
    if (strobe_mf_button_pending_) {
        mf_button_ = false;
        strobe_mf_button_pending_ = false;
    }
    if (strobe_divmmc_button_pending_) {
        divmmc_button_ = false;
        strobe_divmmc_button_pending_ = false;
    }

    iotrap_strobe_pending_ = false;
}

// ---------------------------------------------------------------------
// State persistence.
// ---------------------------------------------------------------------

void NmiSource::save_state(StateWriter& w) const
{
    // Producer inputs.
    w.write_bool(mf_button_);
    w.write_bool(divmmc_button_);
    w.write_bool(expbus_nmi_n_);
    w.write_bool(strobe_mf_button_pending_);
    w.write_bool(strobe_divmmc_button_pending_);
    w.write_bool(nmi_sw_gen_mf_);
    w.write_bool(nmi_sw_gen_divmmc_);
    w.write_bool(iotrap_strobe_pending_);

    // Gate flags.
    w.write_bool(mf_enable_);
    w.write_bool(divmmc_enable_);
    w.write_bool(expbus_debounce_disable_);
    w.write_bool(config_mode_);

    // Consumer feedback.
    w.write_bool(mf_nmi_hold_);
    w.write_bool(mf_is_active_);
    w.write_bool(divmmc_nmi_hold_);
    w.write_bool(divmmc_conmem_);

    // Latches.
    w.write_bool(nmi_mf_);
    w.write_bool(nmi_divmmc_);
    w.write_bool(nmi_expbus_);

    // FSM.
    w.write_u8(static_cast<uint8_t>(state_));

    // Readback-pending.
    w.write_bool(nr_02_pending_mf_);
    w.write_bool(nr_02_pending_divmmc_);

    // Edge tracking.
    w.write_bool(prev_wr_n_);
}

void NmiSource::load_state(StateReader& r)
{
    mf_button_                     = r.read_bool();
    divmmc_button_                 = r.read_bool();
    expbus_nmi_n_                  = r.read_bool();
    strobe_mf_button_pending_      = r.read_bool();
    strobe_divmmc_button_pending_  = r.read_bool();
    nmi_sw_gen_mf_                 = r.read_bool();
    nmi_sw_gen_divmmc_             = r.read_bool();
    iotrap_strobe_pending_         = r.read_bool();

    mf_enable_                     = r.read_bool();
    divmmc_enable_                 = r.read_bool();
    expbus_debounce_disable_       = r.read_bool();
    config_mode_                   = r.read_bool();

    mf_nmi_hold_                   = r.read_bool();
    mf_is_active_                  = r.read_bool();
    divmmc_nmi_hold_               = r.read_bool();
    divmmc_conmem_                 = r.read_bool();

    nmi_mf_                        = r.read_bool();
    nmi_divmmc_                    = r.read_bool();
    nmi_expbus_                    = r.read_bool();

    state_                         = static_cast<State>(r.read_u8());

    nr_02_pending_mf_              = r.read_bool();
    nr_02_pending_divmmc_          = r.read_bool();

    prev_wr_n_                     = r.read_bool();
}
