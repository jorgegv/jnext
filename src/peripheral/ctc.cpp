#include "peripheral/ctc.h"

#include <algorithm>
#include <cstdint>
#include "core/log.h"
#include "core/saveable.h"
#include "save/state_desc.h"
#include "save/state_desc_bin.h"

namespace {

// GH #27 S5 — the per-channel key tables for the §9.4 loop collapse in
// `Ctc::describe_state` / `describe_timing`.
//
// Four channels share one field list, so the keys carry the channel number:
// a `.jns` says `ch2_counter`, not a fourth anonymous `counter` that
// `JsonWriteDesc` would silently drop on top of the third.
//
// LITERALS, concatenated by the preprocessor, and NOT built at run time —
// `StateDesc::fail()` STORES the `const char*` it is handed rather than
// copying it, so a key formatted into a stack buffer would dangle in exactly
// the refusal path whose job is to name the field.
#define CTC_CHAN_KEYS(p)                                                   \
    { p "_control_int_en",   p "_control_counter", p "_control_prescale",  \
      p "_control_edge",     p "_control_trigger", p "_time_constant",     \
      p "_counter",          p "_prescaler",       p "_state",             \
      p "_clk_trg_prev" }

const char* const kChanKeys[4][10] = {
    CTC_CHAN_KEYS("ch0"), CTC_CHAN_KEYS("ch1"),
    CTC_CHAN_KEYS("ch2"), CTC_CHAN_KEYS("ch3"),
};

#undef CTC_CHAN_KEYS

// The chained-trigger delay of each channel, in the `int_timing` block.
const char* const kTrgDelayKeys[4] = {
    "ch0_trg_delay", "ch1_trg_delay", "ch2_trg_delay", "ch3_trg_delay",
};

// Enum name table (design §6.2, §9.4). The binary encoding stays the u8
// ordinal the stream has always carried; the NAME is what a `.jns` writes, so
// renumbering the FSM becomes a visible schema diff rather than a silent
// re-interpretation of old files. Ordinals are CtcChannel::State (ctc.h:116),
// which models `device/ctc_chan.vhd`.
const char* const kChanStateNameArr[] = {
    "reset",         // State::RESET
    "reset_tc",      // State::RESET_TC
    "trigger",       // State::TRIGGER
    "run",           // State::RUN
    "run_tc",        // State::RUN_TC
    "trigger_auto",  // State::TRIGGER_AUTO
};
const jnext::save::EnumNames kChanStateNames{
    kChanStateNameArr,
    sizeof(kChanStateNameArr) / sizeof(kChanStateNameArr[0])};

}  // namespace

// ─── CTC logger ───────────────────────────────────────────────────────

static std::shared_ptr<spdlog::logger>& ctc_log() {
    static auto l = [] {
        auto existing = spdlog::get("ctc");
        if (existing) return existing;
        auto logger = spdlog::stderr_color_mt("ctc");
        logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return logger;
    }();
    return l;
}

// ─── CtcChannel implementation ────────────────────────────────────────

void CtcChannel::reset() {
    control_int_en_   = false;
    control_counter_  = false;
    control_prescale_ = false;
    control_edge_     = false;
    control_trigger_  = false;
    time_constant_    = 0;
    counter_          = 0;
    prescaler_        = 0;
    state_            = State::RESET;
    clk_trg_prev_     = false;
}

void CtcChannel::write(uint8_t val) {
    // Determine write type based on state machine (mirrors ctc_chan.vhd)
    bool waiting_tc = (state_ == State::RESET_TC || state_ == State::RUN_TC);

    if (waiting_tc) {
        // This write is a time constant (ctc_chan.vhd:278-285). It reaches
        // t_count only through reset_soft (S_RESET_TC -> S_TRIGGER, :158-160)
        // or a ZC/TO reload (:161-162): written while the channel runs
        // (S_RUN_TC) the count carries on and the new constant is loaded at
        // the next ZC/TO — the Z80 CTC's documented "new time constant takes
        // effect at the next zero count".
        time_constant_ = val;
        if (state_ == State::RESET_TC) counter_ = val;

        // G120: prescaler clearing follows VHDL device/ctc_chan.vhd:131-141
        // — p_count is cleared only when reset_soft='1', and reset_soft is
        // 'state /= S_RUN and state /= S_RUN_TC' (line 117). So a TC write
        // from S_RESET_TC clears the prescaler (state was RESET_TC at the
        // tick of the write, reset_soft=1 → p_count=0); but a TC reload from
        // S_RUN_TC (mid-stream reconfigure while running) leaves p_count
        // alone. Clearing on every TC write makes the next ZC/TO up to one
        // prescaler period late after a running reload.
        if (state_ == State::RESET_TC) {
            prescaler_ = 0;
        }

        ctc_log()->debug("time constant = {:#04x} ({})", val, val == 0 ? 256 : val);

        if (state_ == State::RESET_TC) {
            // After loading TC from reset: S_RESET_TC always goes to S_TRIGGER
            // (ctc_chan.vhd:214-218), where a timer with D3=1 waits for its
            // trigger (:219-224) and anything else leaves for S_RUN on the
            // next edge (:225-226). A counter goes straight to RUN here: the
            // one edge it would spend in S_TRIGGER only matters for a trigger
            // arriving on exactly that edge.
            if (!control_counter_ && control_trigger_) {
                state_ = State::TRIGGER;
                ctc_log()->trace("state -> TRIGGER (waiting for CLK/TRG edge)");
            } else if (!control_counter_) {
                state_ = State::TRIGGER_AUTO;
                ctc_log()->trace("state -> TRIGGER (auto-start on the next edge)");
            } else {
                state_ = State::RUN;
                ctc_log()->trace("state -> RUN (auto-start)");
            }
        } else {
            // S_RUN_TC -> S_RUN: reload while running. Prescaler is preserved
            // (see G120 comment above).
            state_ = State::RUN;
            ctc_log()->trace("state -> RUN (TC reload while running)");
        }
        return;
    }

    if (val & 0x01) {
        // Control word
        bool soft_reset = (val & 0x02) != 0;
        bool tc_follows = (val & 0x04) != 0;

        // Detect edge selection change (counts as a clock edge per VHDL)
        bool new_edge = (val & 0x10) != 0;
        bool edge_changed = (new_edge != control_edge_);

        // Store control bits (bits 7:3)
        control_int_en_   = (val & 0x80) != 0;
        control_counter_  = (val & 0x40) != 0;
        control_prescale_ = (val & 0x20) != 0;
        control_edge_     = new_edge;
        control_trigger_  = (val & 0x08) != 0;

        ctc_log()->debug("control word={:#04x}: int={} mode={} prescale={} edge={} trigger={} reset={} tc_follows={}",
                         val, control_int_en_, control_counter_ ? "counter" : "timer",
                         control_prescale_ ? 256 : 16,
                         control_edge_ ? "rising" : "falling",
                         control_trigger_, soft_reset, tc_follows);

        if (soft_reset) {
            // Soft reset: stop counting. Out of S_RUN/S_RUN_TC reset_soft
            // holds p_count at 0 and reloads t_count from time_constant_reg on
            // every edge (ctc_chan.vhd:117, 134-139, 158-160): a stopped
            // channel reads back its time constant, not the count it stopped
            // at.
            if (tc_follows) {
                state_ = State::RESET_TC;
                ctc_log()->trace("state -> RESET_TC (soft reset + TC follows)");
            } else {
                state_ = State::RESET;
                ctc_log()->trace("state -> RESET (soft reset, no TC)");
            }
            prescaler_ = 0;
            counter_   = time_constant_;
            return;
        }

        // Not a soft reset: handle state transitions for TC follows
        if (tc_follows) {
            if (state_ == State::RESET) {
                state_ = State::RESET_TC;
                ctc_log()->trace("state -> RESET_TC (TC follows from RESET)");
            } else if (state_ == State::RUN) {
                state_ = State::RUN_TC;
                ctc_log()->trace("state -> RUN_TC (TC follows while running)");
            } else if (state_ == State::TRIGGER || state_ == State::TRIGGER_AUTO) {
                state_ = State::RESET_TC;
                ctc_log()->trace("state -> RESET_TC (TC follows from TRIGGER)");
            }
        }

        // Edge change counts as a trigger (VHDL: clk_edge_change)
        if (edge_changed && (state_ == State::TRIGGER || state_ == State::RUN)) {
            if (state_ == State::TRIGGER) {
                state_ = State::RUN;
                ctc_log()->trace("state -> RUN (edge change triggered start)");
            }
            if (control_counter_) {
                count_step();
            }
        }
    } else {
        // Interrupt vector write (bit 0 = 0)
        // The vector is not handled in this module (same as VHDL),
        // but we signal it.  In the real Z80 CTC only channel 0
        // accepts vectors; the top-level Ctc class can enforce this.
        ctc_log()->debug("vector write = {:#04x}", val);
    }
}

uint8_t CtcChannel::read() const {
    return counter_;
}

bool CtcChannel::tick() {
    // Timer mode only; counter mode uses trigger()
    if (control_counter_) return false;
    if (state_ == State::TRIGGER_AUTO) {
        // S_TRIGGER -> S_RUN (ctc_chan.vhd:225-226). reset_soft was still
        // '1' before this edge, so p_count is 0 after it and t_count holds
        // the constant (:134-139, :158-160).
        state_     = State::RUN;
        prescaler_ = 0;
        counter_   = time_constant_;
        return false;
    }
    if (state_ != State::RUN && state_ != State::RUN_TC) return false;

    // Advance prescaler (counts up, matches VHDL p_count)
    prescaler_++;

    // Check prescaler overflow
    bool prescaler_fire;
    if (!control_prescale_) {
        // Prescaler = 16: fire when low nibble wraps (every 16 ticks)
        prescaler_fire = (prescaler_ & 0x0F) == 0;
    } else {
        // Prescaler = 256: fire when full byte wraps
        prescaler_fire = (prescaler_ == 0);
    }

    if (prescaler_fire) {
        return count_step();
    }
    return false;
}

bool CtcChannel::trigger() {
    // In counter mode: each trigger decrements the counter
    if (control_counter_) {
        if (state_ != State::RUN && state_ != State::RUN_TC) return false;
        return count_step();
    }

    // In timer mode with trigger enabled: start counting on trigger
    if (state_ == State::TRIGGER) {
        state_ = State::RUN;
        prescaler_ = 0;
        ctc_log()->trace("trigger received, state -> RUN");
    }
    return false;
}

void CtcChannel::set_int_enable(bool en) {
    control_int_en_ = en;
}

bool CtcChannel::count_step() {
    // Decrement counter; check for underflow (ZC/TO)
    counter_--;
    if (counter_ == 0) {
        // ZC/TO: reload from time constant
        counter_ = time_constant_;
        // Guarded, like every per-event and per-port-read log call in this
        // file, for the should_log() reason given in PortDispatch::read
        // (src/port/port_dispatch.cpp): unguarded, a call with arguments is
        // an out-of-line call per ZC/TO or read with the level off (GH #244).
        if (ctc_log()->should_log(spdlog::level::trace))
            ctc_log()->trace("ZC/TO! reload={:#04x}", time_constant_);
        return true;
    }
    return false;
}

// ─── Ctc implementation ───────────────────────────────────────────────

Ctc::Ctc() {
    reset();
}

void Ctc::reset() {
    for (auto& ch : channels_) {
        ch.reset();
    }
    for (auto& t : trg_delay_) t = 0;
    ctc_log()->debug("CTC reset");
}

void Ctc::write(int channel, uint8_t val) {
    if (channel < 0 || channel > 3) return;
    ctc_log()->debug("write ch{} = {:#04x}", channel, val);
    channels_[channel].write(val);
}

uint8_t Ctc::read(int channel) const {
    if (channel < 0 || channel > 3) return 0xFF;
    uint8_t val = channels_[channel].read();
    if (ctc_log()->should_log(spdlog::level::trace))
        ctc_log()->trace("read ch{} = {:#04x}", channel, val);
    return val;
}

void Ctc::tick_events(uint32_t master_cycles) {
    // Task 27 C1: event-horizon loop, O(events) instead of
    // O(master_cycles * 4). Between events the only state change is
    // prescaler_ accumulation + counter_ decrement per prescaler wrap of the
    // running timers, which CtcChannel::advance() applies in closed form;
    // the edge an event happens on is run exactly. An event is:
    //   - a running timer's ZC/TO (ctc_chan.vhd:143-146 prescaler_clk,
    //     :162-170 zc_to);
    //   - a timer leaving its one S_TRIGGER edge (:220-226);
    //   - a chained trigger arriving (GH #265, below).
    //
    // GH #265 — a ZC/TO reaches the next channel of the ring
    // (zxnext.vhd:4084) through that channel's edge detector: o_zc_to is
    // zc_to_d, high for the cycle after the edge the count ran out on
    // (ctc_chan.vhd:173-182); clk_trg_d delays it one more edge, and
    // clk_trg_edge is `clk_trg_d and not i_clk_trg` (falling, D4=0) or
    // `i_clk_trg and not clk_trg_d` (rising, D4=1) (:121-127). The receiving
    // channel therefore counts (or, waiting in S_TRIGGER, starts) on the
    // second edge after the ZC/TO with a falling-edge trigger, the first
    // with a rising one — not on the same edge, as the per-cycle model did.
    uint32_t remaining = master_cycles;
    while (remaining > 0) {
        uint32_t d = UINT32_MAX;
        for (int ch = 0; ch < 4; ++ch) {
            const CtcChannel& c = channels_[ch];
            if (c.leaving_trigger()) d = 1;
            else if (c.timer_running()) d = std::min(d, c.cycles_to_zc());
            if (trg_delay_[ch]) d = std::min<uint32_t>(d, trg_delay_[ch]);
        }
        if (d == UINT32_MAX) {  // idle CTC: nothing can happen this span
            time_ += remaining;
            return;
        }
        if (d > remaining) {
            // Nothing happens within the span: closed-form advance.
            for (int ch = 0; ch < 4; ++ch) {
                if (channels_[ch].timer_running()) channels_[ch].advance(remaining);
                if (trg_delay_[ch]) trg_delay_[ch] -= static_cast<uint8_t>(remaining);
            }
            time_ += remaining;
            return;
        }
        // Jump to one edge before the earliest event (nothing happens in the
        // gap for ANY channel, since d is the minimum over all)...
        if (d > 1) {
            for (int ch = 0; ch < 4; ++ch) {
                if (channels_[ch].timer_running()) channels_[ch].advance(d - 1);
                if (trg_delay_[ch]) trg_delay_[ch] -= static_cast<uint8_t>(d - 1);
            }
            remaining -= d - 1;
            time_ += d - 1;
        }
        // ...then run the event edge. Every channel's register update on an
        // edge uses the values from before it, so the prescaler edges come
        // first and the chained triggers after (a timer a trigger starts on
        // this edge holds p_count at 0 on it, :134-139), and only then are
        // the edge's ZC/TOs passed on.
        ++time_;
        --remaining;
        bool fired[4] = {false, false, false, false};
        for (int ch = 0; ch < 4; ++ch) fired[ch] = channels_[ch].tick();
        for (int ch = 0; ch < 4; ++ch) {
            if (trg_delay_[ch] && --trg_delay_[ch] == 0) {
                if (channels_[ch].trigger()) fired[ch] = true;
            }
        }
        for (int ch = 0; ch < 4; ++ch) {
            if (fired[ch]) handle_zc_to(ch);
        }
    }
}

void Ctc::trigger(int channel) {
    if (channel < 0 || channel > 3) return;
    if (ctc_log()->should_log(spdlog::level::trace))
        ctc_log()->trace("external trigger ch{}", channel);
    if (channels_[channel].trigger()) {
        handle_zc_to(channel);
    }
}

void Ctc::set_int_enable(uint8_t mask) {
    for (int i = 0; i < 4; ++i) {
        channels_[i].set_int_enable((mask >> i) & 1);
    }
}

uint8_t Ctc::get_int_enable() const {
    // Mirror image of set_int_enable(): pack per-channel int_enabled()
    // into bits 0..3. Bits 4..7 are always zero (CTC 4..7 hardwired to
    // '0' per zxnext.vhd:4092). Consumed by the NR 0xC5 read path.
    uint8_t mask = 0;
    for (int i = 0; i < 4; ++i) {
        if (channels_[i].int_enabled()) mask |= static_cast<uint8_t>(1u << i);
    }
    return mask;
}

void Ctc::handle_zc_to(int channel) {
    // Unconditional ZC/TO callback — fires for every pulse regardless of
    // channel IRQ enable. Consumers: joy_iomode pin-7 toggle on ch3.
    // Kept separate from on_interrupt because hardware uses ctc_zc_to(3)
    // as a free-running clock for that mux even when CTC ch3 IRQ is off.
    if (on_zc_to) {
        on_zc_to(channel);
    }

    // G119: VHDL zxnext.vhd:1941 wires `ctc_zc_to` straight into `im2_int_req`
    // unconditionally — the i_int_en gate happens inside im2_peripheral.vhd:172
    // (im2_int_req latch is set on `(int_req AND i_int_en) OR int_unq`). The
    // CTC channel's own o_int_en (control_reg(7-3)) is routed SEPARATELY into
    // `ctc_int_en` (zxnext.vhd:1949 → im2_int_en) and is NOT used to gate
    // ctc_zc_to inside the device. Mirror that here: fire on_interrupt on
    // every ZC/TO regardless of channel int_enabled, so the IM2 fabric sees
    // the edge and applies the int_en AND at the wrapper layer (matching the
    // UART RX/TX pattern at uart.cpp:621-630). Without this, flipping int_en
    // between two ZC/TO pulses loses the prior pulse race-the-edge.
    if (on_interrupt) {
        if (ctc_log()->should_log(spdlog::level::debug))
            ctc_log()->debug("ch{} ZC/TO -> interrupt (unconditional, IM2 gates int_en)", channel);
        on_interrupt(channel);
    }

    // Daisy-chain: the next channel of the ring sees this pulse through its
    // edge detector (see tick()): VHDL zxnext.vhd:4084
    // i_clk_trg <= ctc_zc_to(2 downto 0) & ctc_zc_to(3),
    // ch0←ch3, ch1←ch0, ch2←ch1, ch3←ch2.
    const int next = (channel + 1) & 3;
    trg_delay_[next] = channels_[next].rising_edge_trigger() ? 1 : 2;
    if (ctc_log()->should_log(spdlog::level::trace))
        ctc_log()->trace("ch{} ZC/TO -> trigger ch{} in {} edge(s)", channel, next,
                         static_cast<int>(trg_delay_[next]));
}

// GH #27 S5 — the ONE field list for a channel (design §9.2). Declaration
// order IS the binary stream order, so it must not be disturbed: the
// byte-identity gate (§17.1) pins four of these back to back as the 40 bytes
// of block 12.
//
// `state_` is marshalled through a local `uint8_t` rather than bound with a
// `reinterpret_cast`. `enum class State` (ctc.h:116) has NO fixed underlying
// type, so it is `int`-wide: binding a `uint8_t&` to it would touch one byte
// of four, and which byte depends on the host's endianness. The local makes
// the width explicit, and it is the same idiom S3 used in `Im2Controller`.
void CtcChannel::describe_state(jnext::save::StateDesc& d, const char* const* k)
{
    d.boolean(k[0], control_int_en_);
    d.boolean(k[1], control_counter_);
    d.boolean(k[2], control_prescale_);
    d.boolean(k[3], control_edge_);
    d.boolean(k[4], control_trigger_);
    d.u8(k[5], time_constant_);
    d.u8(k[6], counter_);
    d.u8(k[7], prescaler_);
    uint8_t state = static_cast<uint8_t>(state_);
    d.enum8(k[8], state, kChanStateNames);
    state_ = static_cast<State>(state);
    d.boolean(k[9], clk_trg_prev_);
}

// GH #27 S5 — the ONE field list (design §9.2). The four channels' ten
// fields each, in `channels_` order: §9.4's "array elements inside loops"
// class, which COLLAPSES to one declaration per field rather than expanding
// to forty.
void Ctc::describe_state(jnext::save::StateDesc& d)
{
    for (int i = 0; i < 4; ++i) channels_[i].describe_state(d, kChanKeys[i]);
}

void Ctc::save_state(StateWriter& w) const
{
    jnext::save::save_via_desc(*this, w, /*machine_level=*/false);
}

void Ctc::load_state(StateReader& r)
{
    jnext::save::BinReadDesc d(r);
    describe_state(d);
    if (d.failed()) {
        // The only way this fires is an `enum8` ordinal the declaration does
        // not name — a stream and a build that disagree about the channel
        // FSM. The field keeps its pre-load value rather than taking a wrong
        // FSM state (§16.1: "a wrong FSM state is not a safe default"), the
        // stream stays in sync (the byte was consumed either way), and the
        // fault is named.
        ctc_log()->error("Ctc::load_state: the stream does not match this "
                         "build's declaration at '{}'",
                         d.failure() ? d.failure() : "?");
    }
    // The pending chained triggers travel in the Emulator's appended
    // interrupt-timing block (describe_timing), NOT here — so a load must not
    // leave the previous machine's delays behind while that block is still
    // ahead in the stream. Kept OUTSIDE the declaration deliberately: it is a
    // reset, not a field.
    for (auto& t : trg_delay_) t = 0;
}

// GH #27 S5 — the SECOND declaration (design §9.5(2)); see the header.
void Ctc::describe_timing(jnext::save::StateDesc& d)
{
    for (int i = 0; i < 4; ++i) d.u8(kTrgDelayKeys[i], trg_delay_[i]);
}

void Ctc::save_timing(StateWriter& w) const
{
    jnext::save::BinWriteDesc d(w);
    const_cast<Ctc*>(this)->describe_timing(d);
}

void Ctc::load_timing(StateReader& r)
{
    jnext::save::BinReadDesc d(r);
    describe_timing(d);
}
