#include "peripheral/ctc.h"
#include "core/log.h"

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
        // This write is a time constant
        time_constant_ = val;
        counter_ = val;
        prescaler_ = 0;

        ctc_log()->debug("time constant = {:#04x} ({})", val, val == 0 ? 256 : val);

        if (state_ == State::RESET_TC) {
            // After loading TC from reset: check if we need to wait for trigger
            if (!control_counter_ && control_trigger_) {
                state_ = State::TRIGGER;
                ctc_log()->trace("state -> TRIGGER (waiting for CLK/TRG edge)");
            } else {
                state_ = State::RUN;
                ctc_log()->trace("state -> RUN (auto-start)");
            }
        } else {
            // S_RUN_TC -> S_RUN: reload while running
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
            // Soft reset: stop counting
            if (tc_follows) {
                state_ = State::RESET_TC;
                ctc_log()->trace("state -> RESET_TC (soft reset + TC follows)");
            } else {
                state_ = State::RESET;
                ctc_log()->trace("state -> RESET (soft reset, no TC)");
            }
            prescaler_ = 0;
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
            } else if (state_ == State::TRIGGER) {
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
    ctc_log()->info("CTC reset");
}

void Ctc::write(int channel, uint8_t val) {
    if (channel < 0 || channel > 3) return;
    ctc_log()->debug("write ch{} = {:#04x}", channel, val);
    channels_[channel].write(val);
}

uint8_t Ctc::read(int channel) const {
    if (channel < 0 || channel > 3) return 0xFF;
    uint8_t val = channels_[channel].read();
    ctc_log()->trace("read ch{} = {:#04x}", channel, val);
    return val;
}

void Ctc::tick(uint32_t master_cycles) {
    for (uint32_t i = 0; i < master_cycles; ++i) {
        for (int ch = 0; ch < 4; ++ch) {
            if (channels_[ch].tick()) {
                handle_zc_to(ch);
            }
        }
    }
}

void Ctc::trigger(int channel) {
    if (channel < 0 || channel > 3) return;
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

void Ctc::handle_zc_to(int channel) {
    // Fire interrupt if enabled
    if (channels_[channel].int_enabled() && on_interrupt) {
        ctc_log()->debug("ch{} ZC/TO -> interrupt", channel);
        on_interrupt(channel);
    }

    // Daisy-chain: trigger the next channel
    if (channel < 3) {
        ctc_log()->trace("ch{} ZC/TO -> trigger ch{}", channel, channel + 1);
        if (channels_[channel + 1].trigger()) {
            handle_zc_to(channel + 1);
        }
    }
}
