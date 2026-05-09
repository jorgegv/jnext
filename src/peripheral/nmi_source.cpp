#include "peripheral/nmi_source.h"
#include "core/saveable.h"

// NmiSource — central NMI arbiter subsystem.
//
// Phase 1 scaffold authored under TASK-NMI-SOURCE-PIPELINE-PLAN.md §Phase 1.
// Models `cores/zxnext/src/zxnext.vhd` lines 2089-2170 (+ 3830-3872 /
// 5891 for the NR 0x02 side). Task 8 (Multiface) wired the MF consumer-
// feedback inputs from `Multiface::is_nmi_hold()` / `is_active()`.
// Pass-9 added the NR 0x80 expansion-bus enable / disable_mem gates
// (VHDL:2089) so the ExpBus producer respects the full VHDL gate.

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
    // VHDL zxnext.vhd:369-371 — `expbus_eff_en` / `expbus_eff_disable_mem`
    // declared with initial '0'. The reset clause at zxnext.vhd:5800-5806
    // re-loads them from `expbus_en` / `expbus_disable_mem` (= NR 0x80 bits
    // 7 / 4). NR 0x80 itself starts at X"00" (zxnext.vhd:360) so both
    // effective signals come out '0' on reset. Pass-9 fix.
    expbus_eff_en_          = false;
    expbus_eff_disable_mem_ = false;
    config_mode_            = false;

    // Consumer feedback inputs reset to idle.
    mf_nmi_hold_     = false;
    mf_is_active_    = false;
    divmmc_nmi_hold_ = false;
    divmmc_conmem_   = false;

    // NR 0x02 readback-pending bits clear on reset.
    nr_02_pending_mf_     = false;
    nr_02_pending_divmmc_ = false;

    // VHDL zxnext.vhd:1306 — `signal nr_02_reset_type ... := "100"` is an
    // initial-value-only default (FPGA power-on). The advance process at
    // VHDL:1732-1739 has no `reset` branch, so the FSM is **not cleared**
    // on hard or soft reset — it persists across both. Do NOT touch
    // `reset_type_` here; the constructor initialises it to 0b100 once
    // and `strobe_soft_reset()` is the only legitimate mutator.

    prev_wr_n_ = true;

    // VHDL:2169-2170 — both button strobes are combinationally derived
    // from `nmi_state = S_NMI_IDLE`, so they are low at reset when the
    // FSM is in IDLE with no latch set.
    mf_button_strobe_     = false;
    divmmc_button_strobe_ = false;
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
    // VHDL zxnext.vhd:3829-3838 — `nmi_sw_gen_{mf,divmmc}` are pure
    // combinational pulses that fire whenever NR 0x02 is written with
    // the corresponding bit set, regardless of the FSM state.
    //
    // VHDL zxnext.vhd:3840-3864 — the *readback* latches
    // `nr_02_generate_mf_nmi` / `nr_02_generate_divmmc_nmi` (surfaced by
    // VHDL:5891 as bits 3/2 of the NR 0x02 read) follow a different
    // pattern:
    //   * SET when the request is accepted, i.e. the corresponding write
    //     bit is high AND `nmi_accept_cause = '1'` (= FSM in IDLE or
    //     FETCH per VHDL:2164).
    //   * CLEAR when NR 0x02 is written with the corresponding bit
    //     explicitly cleared (`nr_02_we = '1' AND nr_wr_dat(b) = '0'`).
    //   * RESET clears them.
    //
    // The latches are *not* cleared by the FSM at S_NMI_END (the FSM
    // signal is not in the process sensitivity list). An earlier
    // implementation auto-cleared at END, which diverged from VHDL.
    const bool req_mf     = (v & 0x08) != 0;
    const bool req_divmmc = (v & 0x04) != 0;
    const bool accept = (state_ == State::Idle) || (state_ == State::Fetch);

    // MF arm.
    if (req_mf) {
        nmi_sw_gen_mf_ = true;            // combinational pulse, always fires
        if (accept) nr_02_pending_mf_ = true;
    } else {
        // Bit 3 explicitly low — VHDL clear path (3847-3848).
        nr_02_pending_mf_ = false;
    }

    // DivMMC arm.
    if (req_divmmc) {
        nmi_sw_gen_divmmc_ = true;
        if (accept) nr_02_pending_divmmc_ = true;
    } else {
        // Bit 2 explicitly low — VHDL clear path (3860-3861).
        nr_02_pending_divmmc_ = false;
    }
}

uint8_t NmiSource::nr_02_read() const
{
    // VHDL:5891 — readback layout:
    //   bit 7   = nr_02_bus_reset           (owned by Emulator/NextReg)
    //   bits 6:5 = "00"                     (reserved)
    //   bit 4   = nr_02_iotrap              (owned by Emulator/NextReg)
    //   bit 3   = nr_02_generate_mf_nmi     (NmiSource — auto-clear @END)
    //   bit 2   = nr_02_generate_divmmc_nmi (NmiSource — auto-clear @END)
    //   bits 1:0 = nr_02_reset_type[1:0]    (NmiSource — VHDL:1732-1739)
    //
    // NmiSource owns bits 3/2 (FSM-driven) and 1:0 (reset_type FSM). The
    // bus_reset (bit 7) and iotrap (bit 4) fields belong to other VHDL
    // signals not yet modelled; they remain zero until their owning
    // peripheral lands. The Emulator NR 0x02 read handler may OR in
    // additional bits if those signals get wired later.
    uint8_t r = 0;
    if (nr_02_pending_mf_)     r |= 0x08;
    if (nr_02_pending_divmmc_) r |= 0x04;
    r |= static_cast<uint8_t>(reset_type_ & 0x03);
    return r;
}

void NmiSource::strobe_iotrap()
{
    iotrap_strobe_pending_ = true;
}

void NmiSource::strobe_soft_reset()
{
    // VHDL zxnext.vhd:1732-1739 — on `nr_02_soft_reset = '1'` rising edge,
    //   nr_02_reset_type <= '0' & rt(2) & (rt(1) OR rt(0))
    // i.e. shift-right-with-OR collapse: 100 -> 010 -> 001 -> 001
    // (saturates at 001 per VHDL:1732-1739). The FSM does not roll over.
    const uint8_t rt = reset_type_ & 0x07;
    const uint8_t b2 = 0;
    const uint8_t b1 = (rt >> 2) & 0x01;
    const uint8_t b0 = ((rt >> 1) & 0x01) | (rt & 0x01);
    reset_type_ = static_cast<uint8_t>((b2 << 2) | (b1 << 1) | b0);
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
void NmiSource::set_expbus_eff_en(bool v)          { expbus_eff_en_ = v; }
void NmiSource::set_expbus_eff_disable_mem(bool v) { expbus_eff_disable_mem_ = v; }
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
    // VHDL zxnext.vhd:2090 — (hotkey_m1 OR nmi_sw_gen_mf) AND
    //                        nr_06_button_m1_nmi_en.
    // VHDL zxnext.vhd:3837 — `nmi_sw_gen_mf <= nmi_gen_nr_mf OR
    //                        nmi_gen_iotrap`, so the iotrap strobe
    //                        (VHDL:3835, gated upstream by
    //                        `nr_d8_io_trap_fdc_en`) OR's into the same
    //                        producer line.
    return (mf_button_ || nmi_sw_gen_mf_ || iotrap_strobe_pending_) && mf_enable_;
}

bool NmiSource::nmi_assert_divmmc() const
{
    // VHDL:2091 — (hotkey_drive OR nmi_sw_gen_divmmc) AND
    //             nr_06_button_drive_nmi_en.
    return (divmmc_button_ || nmi_sw_gen_divmmc_) && divmmc_enable_;
}

bool NmiSource::nmi_assert_expbus() const
{
    // VHDL zxnext.vhd:2089 — verbatim:
    //   nmi_assert_expbus <= '1' when expbus_eff_en = '1'
    //                            and expbus_eff_disable_mem = '0'
    //                            and i_BUS_NMI_n = '0' else '0';
    //
    // Pass-9 verify-audit fix (Task 2 verify9-nmi-mf-port): pre-fix the
    // gate omitted `expbus_eff_en` / `expbus_eff_disable_mem` because
    // jnext has no expbus device and `i_BUS_NMI_n` (alias `expbus_nmi_n_`)
    // is permanently held idle ('1'), making the two missing gates
    // redundant in steady state. They are NOT redundant once a future
    // setter call to `set_expbus_nmi_n(false)` lights up the pin: the
    // NR 0x80 enable / disable_mem bits MUST gate the assertion per
    // VHDL — otherwise software that disables the expansion bus via
    // NR 0x80 bit 7=0 would still see ExpBus NMIs route through.
    // i_BUS_NMI_n is active-low, so assert when == false.
    return expbus_eff_en_ && !expbus_eff_disable_mem_ && !expbus_nmi_n_;
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
    // VHDL zxnext.vhd:2168 — verbatim formula:
    //   nmi_generate_n <= '0' when (nmi_state = S_NMI_IDLE and
    //                               nmi_activated = '1')
    //                          or nmi_state = S_NMI_FETCH
    //                          or (nr_81_expbus_nmi_debounce_disable = '1'
    //                              and nmi_assert_expbus = '1')
    //                     else '1';
    //
    // Verify-audit fix (Task 2 verify-nmi-mf-port): earlier code asserted
    // /NMI in HOLD as well as FETCH ("the FSM holds the request"), which
    // mis-citation. VHDL releases /NMI ('1') on the FETCH→HOLD edge so the
    // Z80 sees a clean edge for any subsequent NMI; HOLD only blocks the
    // FSM from advancing while the consumer is still latched. Functionally
    // identical for the falling-edge consumer in jnext (the Z80 latches
    // NMI on the edge, not the level), but spec-faithful matters.
    if (state_ == State::Fetch)                          return false;
    if (state_ == State::Idle  && is_activated())        return false;
    if (expbus_debounce_disable_ && nmi_assert_expbus()) return false;
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
    // cpu_wr_n. With our coarse per-tick granularity (and no `wr_n` plumbing
    // from the CPU) the END -> IDLE advance is now performed inside
    // `recompute_()` itself (see case `State::End`). This observer is
    // retained as a no-op for forward compatibility — if a future Wave
    // wires `wr_n` from the Z80 core, the rising-edge advance can be
    // re-enabled here without touching call sites.
    prev_wr_n_ = wr_n;
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
    // 1) config_mode force-clear (VHDL:2102-2105) — clears the three
    //    priority latches `nmi_mf` / `nmi_divmmc` / `nmi_expbus` and
    //    holds the FSM in IDLE while the flag is held.
    //
    //    Verify-audit fix (Task 2 verify-nmi-mf-port): the NR 0x02
    //    readback latches `nr_02_generate_mf_nmi` / `nr_02_generate_divmmc_nmi`
    //    (zxnext.vhd:3840-3864) live in DIFFERENT clocked processes
    //    that are clear ONLY by reset OR by an explicit NR 0x02 write
    //    with the corresponding bit cleared (lines 3847-3848, 3860-3861).
    //    Config mode does NOT appear in either process's clear cascade,
    //    so the readback bits MUST survive a config_mode entry. Earlier
    //    code wrongly cleared `nr_02_pending_{mf,divmmc}_` here.
    if (config_mode_) {
        nmi_mf_     = false;
        nmi_divmmc_ = false;
        nmi_expbus_ = false;
        state_ = State::Idle;
        return;
    }

    // 2) Priority latch update (VHDL:2097-2105). Latches set combinationally
    //    from the assert signals with MF > DivMMC > ExpBus priority, and
    //    clear on S_NMI_END transition (handled below).
    //
    //    MF latch gating (VHDL zxnext.vhd:2107):
    //      set iff nmi_assert_mf
    //              AND NOT port_e3_reg(7)   (= !divmmc_conmem_)
    //              AND NOT divmmc_nmi_hold
    //    DivMMC latch gating (VHDL zxnext.vhd:2110):
    //      set iff nmi_assert_divmmc AND NOT mf_is_active AND NOT nmi_mf
    //    ExpBus latch gating (VHDL zxnext.vhd:2113):
    //      set iff nmi_assert_expbus AND NOT (nmi_mf OR nmi_divmmc)
    //
    // Pass-9 verify-audit fix: the comment above previously omitted the
    // `divmmc_nmi_hold` term in the MF gate description (the code was
    // already correct).
    //
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
        // VHDL:2169-2170 — `nmi_mf_button` and `nmi_divmmc_button` are
        // combinationally TRUE while the respective priority latch is
        // set AND `nmi_state = S_NMI_IDLE`. In our collapsed model, the
        // latch was just set inside this `recompute_()` and we're about
        // to advance to FETCH. Capture the one-cycle strobe NOW, before
        // the state transition, so Emulator can observe it after
        // `NmiSource::tick()` returns. The strobe is cleared at the
        // next tick() entry.
        if (is_activated()) {
            if (nmi_mf_)     mf_button_strobe_     = true;
            if (nmi_divmmc_) divmmc_button_strobe_ = true;
            // nmi_expbus does not drive a button strobe in VHDL.
            state_ = State::Fetch;
        }
        break;

    case State::Fetch:
        // FETCH -> HOLD advanced by observe_m1_fetch(); no time-based
        // transition here.
        break;

    case State::Hold: {
        // VHDL zxnext.vhd:2118 —
        //   nmi_hold <= mf_nmi_hold     when nmi_mf     = '1'
        //          else divmmc_nmi_hold when nmi_divmmc = '1'
        //          else nmi_assert_expbus;
        // The ExpBus arm uses the *combinational producer* (the bus pin
        // / debounce-disable path), NOT divmmc_nmi_hold; the previous
        // collapsed `nmi_mf_ ? mf_nmi_hold_ : divmmc_nmi_hold_` was wrong
        // for the ExpBus latch.
        bool hold;
        if (nmi_mf_) {
            hold = mf_nmi_hold_;
        } else if (nmi_divmmc_) {
            hold = divmmc_nmi_hold_;
        } else {
            // nmi_expbus_ active — VHDL falls through to `nmi_assert_expbus`.
            hold = nmi_assert_expbus();
        }
        if (!hold) state_ = State::End;
        break;
    }

    case State::End:
        // END clears the three priority latches on entry (VHDL:2102-2105
        // via the FSM clear term). The NR 0x02 readback latches
        // `nr_02_generate_*_nmi` (VHDL:3840-3864) are NOT cleared here —
        // they survive until either reset or an explicit write-back
        // with the corresponding bit cleared. The earlier implementation
        // erroneously auto-cleared the readback bits at END.
        nmi_mf_     = false;
        nmi_divmmc_ = false;
        nmi_expbus_ = false;
        // END -> IDLE transition. VHDL zxnext.vhd:2143-2147:
        //   when others =>  -- S_NMI_END
        //     if cpu_wr_n = '1' then  -- do not transition until io write
        //                              -- cycle completes
        //       nmi_state_next <= S_NMI_IDLE;
        //
        // VHDL FSM advance is registered on i_CLK_CPU. A Z80 write-cycle
        // pulls cpu_wr_n='0' for at most three CPU clocks (T2/T3 of an
        // M3 OUT or memory-write cycle); between any two consecutive
        // Z80 instructions the bus is fully idle (no MREQ/IORQ active)
        // and cpu_wr_n is therefore high. NmiSource::tick() is invoked
        // from Emulator::run_frame() at instruction boundaries, never
        // mid-instruction, so by the time we re-enter `recompute_()`
        // here the previous NMI ISR's last bus cycle has completed and
        // cpu_wr_n='1' is guaranteed. The unconditional advance below
        // is thus VHDL-faithful at our per-instruction tick granularity:
        // we evaluate the FSM at exactly the same moments the VHDL would
        // (one i_CLK_CPU edge after END entry, when the gate is high).
        // Pass-9 verify-audit clarification: the previous comment
        // labelled this an "approximation"; it is in fact spec-faithful
        // for the per-instruction tick granularity.
        state_ = State::Idle;
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

    // VHDL:2169-2170 — the `nmi_*_button` outputs are asserted only for
    // the cycle the FSM is still in `S_NMI_IDLE` with the latch set.
    // Clear the strobes at tick entry so `recompute_()` can re-raise
    // them if the IDLE→FETCH transition happens this tick; any upstream
    // consumer reads them between two successive `tick()` calls.
    mf_button_strobe_     = false;
    divmmc_button_strobe_ = false;

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
    // Pass-9: NR 0x80 bit 7 / bit 4 latched gates (expbus_eff_en /
    // expbus_eff_disable_mem). Appended at the end of the gate-flag
    // group so older snapshots that lack the bytes still load via the
    // tail-of-format extension dance below.
    w.write_bool(expbus_eff_en_);
    w.write_bool(expbus_eff_disable_mem_);
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

    // Button strobes (VHDL:2169-2170). Normally low between ticks; saved
    // to keep the snapshot stream complete and future-proof.
    w.write_bool(mf_button_strobe_);
    w.write_bool(divmmc_button_strobe_);

    // VHDL:1306, 1732-1739 — `nr_02_reset_type` 3-bit FSM. Persist so
    // a snapshot taken after one or more soft resets reloads with the
    // same FSM advance position.
    w.write_u8(reset_type_);
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
    // Pass-9: NR 0x80 effective gate flags. See save_state() for the
    // append point. The snapshot ring is rewind-only (in-process), so
    // older snapshots are not a concern.
    expbus_eff_en_                 = r.read_bool();
    expbus_eff_disable_mem_        = r.read_bool();
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

    mf_button_strobe_              = r.read_bool();
    divmmc_button_strobe_          = r.read_bool();

    reset_type_                    = r.read_u8();
}
