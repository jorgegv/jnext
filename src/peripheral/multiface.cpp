#include "peripheral/multiface.h"
#include "core/log.h"
#include "core/saveable.h"
#include <algorithm>
#include <cstring>

// ── Logger ────────────────────────────────────────────────────────────

static std::shared_ptr<spdlog::logger>& mf_log() {
    static auto l = [] {
        auto existing = spdlog::get("multiface");
        if (existing) return existing;
        auto logger = spdlog::stderr_color_mt("multiface");
        logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return logger;
    }();
    return l;
}

// ── Construction / reset ──────────────────────────────────────────────

Multiface::Multiface()
{
    rom_.fill(0xFF);
    ram_.fill(0x00);
    // Default mode: VHDL `nr_0a_mf_type` defaults "00" at hard reset
    // (zxnext.vhd:5191) → mode_p3.
    set_mode(0x00);
    reset(true);
}

void Multiface::reset(bool hard)
{
    // VHDL multiface.vhd:103 — `reset = reset_i OR NOT enable_i`. The
    // four clocked processes all use `if reset='1'` to load their reset
    // values (lines 125, 140-141, 154-156, 173-175). enabled_ itself is
    // not touched: enable_i is an external pin owned by the NR 0x83
    // gate.
    nmi_active_   = false;  // line 141
    invisible_    = true;   // line 156 (note: reset value '1', not '0')
    mf_enable_    = false;  // line 175
    port_io_dly_  = false;  // line 126
    fetch_66_live_ = false;
    mf_port_en_   = false;
    if (hard) {
        ram_.fill(0x00);
    }
}

// ── Wiring inputs ─────────────────────────────────────────────────────

void Multiface::set_enabled(bool en)
{
    bool prev = enabled_;
    enabled_ = en;
    if (prev && !en) {
        // VHDL line 103: enable_i='0' forces reset='1'. Drop all FFs
        // immediately so the public state reflects the held-reset
        // behaviour the next clock would produce. This mirrors the
        // DivMmc::set_enabled enabled→disabled-edge clear pattern.
        if (mf_log()->should_log(spdlog::level::debug)) {
            mf_log()->debug("disabled — forcing FFs to reset state");
        }
        nmi_active_   = false;
        invisible_    = true;   // VHDL reset value (line 156)
        mf_enable_    = false;
        port_io_dly_  = false;
        fetch_66_live_ = false;
        mf_port_en_   = false;
    }
}

void Multiface::set_mode(uint8_t mf_type)
{
    // multiface.vhd:105-118 — combinational decode.
    //   "00" -> mode_p3
    //   "11" -> mode_48 (= MF1)
    //   others ("01", "10") -> mode_128
    const uint8_t bits = mf_type & 0x03;
    mode_p3_  = (bits == 0x00);
    mode_48_  = (bits == 0x03);
    mode_128_ = !(mode_p3_ || mode_48_);
}

// ── Internal: one clock-edge pass ─────────────────────────────────────

void Multiface::clock_edge_(bool button,
                            bool port_en_rd,
                            bool port_en_wr,
                            bool port_dis_rd,
                            bool port_dis_wr,
                            bool a_0066,
                            bool m1_low,
                            bool mreq_low,
                            bool retn_seen)
{
    // VHDL line 103: `reset = reset_i OR NOT enable_i`. We have no
    // `reset_i` pulse here (it's hardware-only); enable_i high means we
    // run the processes normally, low means everything is held in reset.
    if (!enabled_) {
        nmi_active_   = false;
        invisible_    = true;
        mf_enable_    = false;
        port_io_dly_  = false;
        fetch_66_live_ = false;
        mf_port_en_   = false;
        return;
    }

    // Snapshot pre-edge values so all FF updates use the same "current
    // state" view (VHDL clocked processes all see the same Q value).
    const bool nmi_active_prev   = nmi_active_;
    const bool invisible_prev    = invisible_;
    const bool mf_enable_prev    = mf_enable_;
    const bool port_io_dly_prev  = port_io_dly_;

    // Combinational signals derived from current FF state and inputs.
    // multiface.vhd:135 — button_pulse <= button_i AND NOT nmi_active.
    const bool button_pulse = button && !nmi_active_prev;

    // multiface.vhd:165 — invisible_eff = invisible AND NOT mode_48.
    const bool invisible_eff = invisible_prev && !mode_48_;

    // multiface.vhd:169 — fetch_66 <= '1' when cpu_a_0066='1' AND
    // cpu_m1_n='0' AND nmi_active='1' else '0'. Note: depends on the
    // CURRENT (pre-edge) nmi_active, so a button press in the same cycle
    // as a 0x0066 fetch does not arm fetch_66 — exactly mirroring VHDL.
    const bool fetch_66 = a_0066 && m1_low && nmi_active_prev;

    // ── port_io_dly FF (multiface.vhd:122-131) ────────────────────────
    // Latches the OR of the four port_* inputs on every rising edge.
    port_io_dly_ = port_en_rd || port_en_wr || port_dis_rd || port_dis_wr;

    // ── nmi_active FF (multiface.vhd:137-148) ─────────────────────────
    // Priority: reset > button_pulse > clear-conditions.
    if (button_pulse) {
        nmi_active_ = true;
    } else {
        // Clear path: any of three conditions, gated by port_io_dly=0.
        // VHDL:
        //   cpu_retn_seen = '1'
        //   OR ((port_mf_enable_wr OR port_mf_disable_wr OR
        //        (port_mf_disable_rd AND mode_p3)) AND port_io_dly = '0')
        const bool port_clear = (port_en_wr ||
                                 port_dis_wr ||
                                 (port_dis_rd && mode_p3_)) &&
                                !port_io_dly_prev;
        if (retn_seen || port_clear) {
            nmi_active_ = false;
        }
    }

    // ── invisible FF (multiface.vhd:152-163) ──────────────────────────
    // Priority: reset > button_pulse > set-condition.
    if (button_pulse) {
        invisible_ = false;  // line 158
    } else {
        // Set path (line 159):
        //   ((port_mf_disable_wr AND mode_p3='0')
        //    OR (port_mf_enable_wr AND mode_p3='1')) AND port_io_dly='0'
        const bool inv_set = ((port_dis_wr && !mode_p3_) ||
                              (port_en_wr  &&  mode_p3_)) &&
                             !port_io_dly_prev;
        if (inv_set) {
            invisible_ = true;
        }
    }

    // ── mf_enable FF (multiface.vhd:171-184) ──────────────────────────
    // Priority cascade:
    //   1. reset                                           -> '0'
    //   2. fetch_66 AND cpu_mreq_n='0'                     -> '1'
    //   3. port_mf_disable_rd OR cpu_retn_seen             -> '0'
    //   4. port_mf_enable_rd                               -> NOT invisible_eff
    if (fetch_66 && mreq_low) {
        mf_enable_ = true;
    } else if (port_dis_rd || retn_seen) {
        mf_enable_ = false;
    } else if (port_en_rd) {
        mf_enable_ = !invisible_eff;
    }

    // ── Combinational outputs (computed AFTER FF updates) ─────────────
    // multiface.vhd:186 — `mf_enable_eff <= mf_enable OR fetch_66`.
    // We surface fetch_66 via fetch_66_live_ so is_mem_active() reflects
    // the OR within this cycle (the one-cycle bypass).
    fetch_66_live_ = fetch_66;
    update_mf_port_en_(port_en_rd);

    if (mf_log()->should_log(spdlog::level::trace)) {
        mf_log()->trace(
            "clk: btn={} pulse={} a66={} m1l={} mreql={} retn={} "
            "rd[{}/{}] wr[{}/{}] -> nmi={}->{} inv={}->{} mf={}->{} "
            "dly={}->{} fetch66={}",
            button, button_pulse, a_0066, m1_low, mreq_low, retn_seen,
            port_en_rd, port_dis_rd, port_en_wr, port_dis_wr,
            nmi_active_prev, nmi_active_,
            invisible_prev, invisible_,
            mf_enable_prev, mf_enable_,
            port_io_dly_prev, port_io_dly_,
            fetch_66);
    }
}

void Multiface::update_mf_port_en_(bool port_en_rd)
{
    // multiface.vhd:195 — combinational:
    //   mf_port_en_o = '1' when port_mf_enable_rd='1'
    //                  AND invisible_eff='0'
    //                  AND (mode_128='1' OR mode_p3='1')
    const bool inv_eff = invisible_ && !mode_48_;
    mf_port_en_ = port_en_rd && !inv_eff && (mode_128_ || mode_p3_);
}

// ── Producer/consumer hooks ───────────────────────────────────────────

void Multiface::button_press()
{
    // Single-cycle clock with button_i='1' and all other inputs idle.
    clock_edge_(/*button=*/true,
                /*port_en_rd=*/false,
                /*port_en_wr=*/false,
                /*port_dis_rd=*/false,
                /*port_dis_wr=*/false,
                /*a_0066=*/false,
                /*m1_low=*/false,
                /*mreq_low=*/false,
                /*retn_seen=*/false);
}

void Multiface::on_m1(uint16_t pc, bool mreq_low)
{
    // VHDL `cpu_a_0066_i` is the "address bus equals 0x0066" comparator;
    // `cpu_m1_n_i='0'` is asserted during the M1 fetch's memory cycle.
    // We invoke clock_edge_ with the appropriate input pulses.
    clock_edge_(/*button=*/false,
                /*port_en_rd=*/false,
                /*port_en_wr=*/false,
                /*port_dis_rd=*/false,
                /*port_dis_wr=*/false,
                /*a_0066=*/(pc == 0x0066),
                /*m1_low=*/true,
                /*mreq_low=*/mreq_low,
                /*retn_seen=*/false);
}

void Multiface::on_retn_seen()
{
    clock_edge_(/*button=*/false,
                /*port_en_rd=*/false,
                /*port_en_wr=*/false,
                /*port_dis_rd=*/false,
                /*port_dis_wr=*/false,
                /*a_0066=*/false,
                /*m1_low=*/false,
                /*mreq_low=*/false,
                /*retn_seen=*/true);
}

void Multiface::on_port_enable_rd(bool active)
{
    if (!active) return;
    clock_edge_(/*button=*/false,
                /*port_en_rd=*/true,
                /*port_en_wr=*/false,
                /*port_dis_rd=*/false,
                /*port_dis_wr=*/false,
                /*a_0066=*/false,
                /*m1_low=*/false,
                /*mreq_low=*/false,
                /*retn_seen=*/false);
}

void Multiface::on_port_enable_wr(bool active)
{
    if (!active) return;
    clock_edge_(/*button=*/false,
                /*port_en_rd=*/false,
                /*port_en_wr=*/true,
                /*port_dis_rd=*/false,
                /*port_dis_wr=*/false,
                /*a_0066=*/false,
                /*m1_low=*/false,
                /*mreq_low=*/false,
                /*retn_seen=*/false);
}

void Multiface::on_port_disable_rd(bool active)
{
    if (!active) return;
    clock_edge_(/*button=*/false,
                /*port_en_rd=*/false,
                /*port_en_wr=*/false,
                /*port_dis_rd=*/true,
                /*port_dis_wr=*/false,
                /*a_0066=*/false,
                /*m1_low=*/false,
                /*mreq_low=*/false,
                /*retn_seen=*/false);
}

void Multiface::on_port_disable_wr(bool active)
{
    if (!active) return;
    clock_edge_(/*button=*/false,
                /*port_en_rd=*/false,
                /*port_en_wr=*/false,
                /*port_dis_rd=*/false,
                /*port_dis_wr=*/true,
                /*a_0066=*/false,
                /*m1_low=*/false,
                /*mreq_low=*/false,
                /*retn_seen=*/false);
}

// ── ROM load ─────────────────────────────────────────────────────────

bool Multiface::load_rom_bytes(const uint8_t* data, size_t size)
{
    if (data == nullptr || size == 0) {
        mf_log()->error("load_rom_bytes: empty buffer");
        return false;
    }

    rom_.fill(0xFF);

    size_t to_copy = size;
    if (size < static_cast<size_t>(kRomSize)) {
        mf_log()->warn("Multiface ROM buffer short ({} bytes, expected {}), padding with 0xFF",
                       size, kRomSize);
    }
    if (size > static_cast<size_t>(kRomSize)) {
        mf_log()->warn("Multiface ROM buffer oversize ({} bytes, expected {}), truncating",
                       size, kRomSize);
        to_copy = kRomSize;
    }

    std::memcpy(rom_.data(), data, to_copy);

    mf_log()->info("loaded Multiface ROM from byte buffer ({} bytes)", to_copy);
    return true;
}

// ── Save / load state ────────────────────────────────────────────────

void Multiface::save_state(StateWriter& w) const
{
    // FF state.
    w.write_bool(enabled_);
    w.write_bool(nmi_active_);
    w.write_bool(invisible_);
    w.write_bool(mf_enable_);
    w.write_bool(port_io_dly_);
    // Mode (re-derivable from NR 0x0A but preserved here so a Multiface
    // restored standalone doesn't depend on NR 0x0A load order).
    w.write_bool(mode_p3_);
    w.write_bool(mode_128_);
    w.write_bool(mode_48_);
    // RAM contents (8 KB). ROM is reloaded fresh from SD each session.
    w.write_bytes(ram_.data(), ram_.size());
}

void Multiface::load_state(StateReader& r)
{
    enabled_     = r.read_bool();
    nmi_active_  = r.read_bool();
    invisible_   = r.read_bool();
    mf_enable_   = r.read_bool();
    port_io_dly_ = r.read_bool();
    mode_p3_     = r.read_bool();
    mode_128_    = r.read_bool();
    mode_48_     = r.read_bool();
    r.read_bytes(ram_.data(), ram_.size());
    fetch_66_live_ = false;
    mf_port_en_    = false;
}
