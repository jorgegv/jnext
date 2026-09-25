#pragma once
#include <cstddef>
#include <cstdint>
#include <array>
#include <functional>

namespace jnext { namespace save { class StateDesc; } }

/// CTC (Counter/Timer Controller) — 4-channel counter/timer peripheral.
///
/// The ZX Next CTC provides 4 independent counter/timer channels, each
/// accessible via its own I/O port:
///   Channel 0: port 0x183B
///   Channel 1: port 0x193B
///   Channel 2: port 0x1A3B
///   Channel 3: port 0x1B3B
///
/// Each channel operates in either timer mode (counts prescaled clock ticks)
/// or counter mode (counts external trigger events).  Channels daisy-chain:
/// ZC/TO output of channel N triggers channel N+1.
///
/// VHDL reference: device/ctc.vhd + device/ctc_chan.vhd
///
/// Control word format (bit 0 = 1):
///   bit 7: interrupt enable (1=enable, 0=disable)
///   bit 6: mode (0=timer, 1=counter)
///   bit 5: prescaler (0=16, 1=256) — timer mode only
///   bit 4: edge select (0=falling, 1=rising) — counter mode trigger edge
///   bit 3: trigger (0=auto start, 1=CLK/TRG starts counting) — timer mode
///   bit 2: time constant follows (1=next write is the time constant)
///   bit 1: software reset (1=reset channel)
///   bit 0: 1 (identifies as control word)
///
/// When bit 0 = 0: interrupt vector write (channel 0 only in real Z80 CTC).
///
/// State machine (from VHDL):
///   S_RESET    — hard reset state; needs control word with D2=1 to leave
///   S_RESET_TC — waiting for time constant after reset
///   S_TRIGGER  — timer loaded, waiting for trigger edge to start
///   S_RUN      — actively counting down
///   S_RUN_TC   — running, but expecting a new time constant next write

/// Single CTC channel — mirrors the ctc_chan.vhd state machine.
class CtcChannel {
public:
    CtcChannel() = default;

    void reset();

    /// Write a byte to this channel's port.
    void write(uint8_t val);

    /// Read this channel's port — returns the current down-counter value.
    uint8_t read() const;

    /// Advance the channel by one prescaler tick (called at 28 MHz rate).
    /// Returns true if ZC/TO fired this tick.
    bool tick();

    /// External trigger (counter mode, or timer trigger).
    /// Returns true if ZC/TO fired.
    bool trigger();

    /// Set interrupt enable externally (from NextREG int_en bits).
    void set_int_enable(bool en);

    /// Returns true if this channel has interrupts enabled.
    bool int_enabled() const { return control_int_en_; }

    /// Control word D4: the CLK/TRG edge this channel reacts to is the
    /// rising one (ctc_chan.vhd:127).
    bool rising_edge_trigger() const { return control_edge_; }

    /// True when tick() would do any work: timer mode in S_RUN/S_RUN_TC
    /// (exact mirror of the guard at the top of tick()). Counter-mode
    /// channels advance only via trigger(), never via tick().
    bool timer_running() const {
        return !control_counter_ && (state_ == State::RUN || state_ == State::RUN_TC);
    }

    /// True for the one edge a timer spends in S_TRIGGER after its time
    /// constant when it does not wait for a trigger (see State).
    bool leaving_trigger() const {
        return !control_counter_ && state_ == State::TRIGGER_AUTO;
    }

    /// Number of 28 MHz ticks until this channel's NEXT ZC/TO, assuming
    /// timer_running(). Derived from ctc_chan.vhd: p_count counts up each
    /// tick (:136-138) and prescaler_clk fires each time it wraps its
    /// period P (16 or 256 per control bit 5, :143-146); ZC/TO fires on
    /// the count step that takes the down-counter to 0 (:162-170;
    /// counter value 0 means 256 steps).
    uint32_t cycles_to_zc() const {
        const uint32_t period = control_prescale_ ? 256u : 16u;
        const uint32_t to_next_fire = period - (prescaler_ % period);  // in [1, period]
        const uint32_t steps = (counter_ == 0) ? 256u : counter_;
        return to_next_fire + (steps - 1u) * period;
    }

    /// Arithmetically advance the channel by n ticks, PRECONDITION
    /// n < cycles_to_zc() (no ZC/TO occurs inside the window). Observably
    /// identical to calling tick() n times: prescaler_ accumulates mod 256,
    /// counter_ decrements once per prescaler wrap.
    void advance(uint32_t n) {
        const uint32_t period = control_prescale_ ? 256u : 16u;
        const uint32_t phase = prescaler_ % period;  // ticks since last wrap
        const uint32_t fires = (phase + n) / period;
        prescaler_ = static_cast<uint8_t>(prescaler_ + n);
        counter_   = static_cast<uint8_t>(counter_ - fires);
    }

private:
    // TRIGGER_AUTO is S_TRIGGER entered by a time constant with nothing to
    // wait for: ctc_chan.vhd:220-226 leaves it for S_RUN on the next edge,
    // and while in it reset_soft holds p_count at 0 (:117, :134-139), so a
    // timer's first prescaler period starts one edge after the constant is
    // written. Appended last so snapshot state bytes keep their meaning.
    enum class State { RESET, RESET_TC, TRIGGER, RUN, RUN_TC, TRIGGER_AUTO };

    // Control word fields (stored as bits 7:3 of control word)
    bool control_int_en_   = false;  // bit 7
    bool control_counter_  = false;  // bit 6: 0=timer, 1=counter
    bool control_prescale_ = false;  // bit 5: 0=prescaler 16, 1=prescaler 256
    bool control_edge_     = false;  // bit 4: 0=falling, 1=rising
    bool control_trigger_  = false;  // bit 3: 0=auto, 1=wait for trigger

    uint8_t time_constant_ = 0;     // reload value (0 means 256)
    uint8_t counter_       = 0;     // current down-counter
    uint8_t prescaler_     = 0;     // prescaler counter (counts up)

    State state_ = State::RESET;

    bool clk_trg_prev_     = false;  // previous trigger input level (for edge detection)

    /// Apply one count-down step.  Returns true if underflow (ZC/TO).
    bool count_step();

public:
    /// GH #27 S5 — the ONE field list for a channel (design §9.2).
    ///
    /// It takes its KEY TABLE from the owning `Ctc`, because four channels
    /// sharing one declaration would name `counter` four times — and a
    /// duplicate key is the one fault the byte-identity gate structurally
    /// cannot see (the binary encoding ignores names; `JsonWriteDesc`'s
    /// `obj[name] = value` silently drops the earlier field). `keys` is a row
    /// of `kChanKeys` in ctc.cpp: ten string LITERALS, never built at run
    /// time, because `StateDesc::fail()` stores the pointer it is handed.
    ///
    /// There is no `save_state`/`load_state` pair here any more: their only
    /// callers were `Ctc`'s, which now walk this declaration directly.
    void describe_state(jnext::save::StateDesc& d, const char* const* keys);
};

/// CTC controller — 4 channels with daisy-chain.
class Ctc {
public:
    Ctc();

    void reset();

    /// Write to a CTC channel port.
    /// @param channel  channel index (0-3)
    /// @param val      byte value
    void write(int channel, uint8_t val);

    /// Read from a CTC channel port.
    /// @param channel  channel index (0-3)
    uint8_t read(int channel) const;

    /// Advance all timer-mode channels by the given number of 28 MHz ticks.
    /// For each tick, prescalers count and underflows propagate through
    /// the daisy-chain.
    ///
    /// Called for every instruction, so the common case — no timer running
    /// or leaving S_TRIGGER, no chained trigger in flight, where the span
    /// only moves time_ on — is decided here, inline; tick_events() is the
    /// same span with something to do.
    void tick(uint32_t master_cycles) {
        for (int ch = 0; ch < 4; ++ch) {
            if (channels_[ch].timer_running() || channels_[ch].leaving_trigger()
                    || trg_delay_[ch]) {
                tick_events(master_cycles);
                return;
            }
        }
        time_ += master_cycles;
    }

    /// GH #265 — the CLK_28 edge the channels are at. tick() advances it by
    /// one for every edge it processes, so an on_interrupt / on_zc_to
    /// callback reads the edge whose ZC/TO it reports: the edge at which
    /// ctc_chan.vhd:173-182 sets zc_to_d, i.e. the one after which o_zc_to
    /// (and with it i_int_req of the IM2 peripheral) is high. The caller sets
    /// it to the last edge already ticked before each tick() span; it is
    /// transient and not serialised.
    void set_time(uint64_t edge) { time_ = edge; }
    uint64_t time() const { return time_; }

    /// External trigger on a specific channel.
    void trigger(int channel);

    /// Set interrupt enable bits for all channels (from NextREG).
    void set_int_enable(uint8_t mask);

    /// Read back the 8-bit CTC interrupt-enable mask (NR 0xC5 read path).
    /// Bits 0..3 reflect channel 0..3 `int_enabled()`; bits 4..7 are zero
    /// because CTC channels 4..7 are hardwired to '0' in the Next VHDL
    /// (zxnext.vhd:4092 — ctc_int_en[7:4] := "0000").
    uint8_t get_int_enable() const;

    /// Callback fired when a channel generates ZC/TO and has interrupts enabled.
    /// Parameter is the channel index (0-3).
    std::function<void(int channel)> on_interrupt;

    /// Callback fired on EVERY ZC/TO pulse from any channel, regardless
    /// of the channel's interrupt-enable bit. Mirrors the VHDL `ctc_zc_to`
    /// signal vector used by non-interrupt consumers — currently the
    /// joy_iomode pin-7 toggle (zxnext.vhd:3521-3524) which gates on
    /// `ctc_zc_to(3)` directly, NOT on CTC ch3 IRQ enable. Distinct from
    /// `on_interrupt` because hardware can use the CTC purely as a clock
    /// source while leaving its IRQ disabled.
    /// Parameter is the channel index (0-3).
    std::function<void(int channel)> on_zc_to;

    // ── Accessors for debug / testing ─────────────────────────────

    const CtcChannel& channel(int ch) const { return channels_[ch]; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    /// GH #27 S5 — the ONE field list (design §9.2): the four channels'
    /// declarations back to back, in `channels_` order.
    void describe_state(jnext::save::StateDesc& d);

    /// GH #265 — the chained triggers in flight (see tick()), for the
    /// Emulator's appended interrupt-timing snapshot block.
    void save_timing(class StateWriter& w) const;
    void load_timing(class StateReader& r);

    /// GH #27 S5 — the SECOND declaration (design §9.5(2)). `save_timing` is
    /// called from a DIFFERENT block than `save_state`: the chained-trigger
    /// delays travel in `int_timing`, the last block of the Emulator stream,
    /// not in block 12. One `describe_state` cannot put its fields in two
    /// blocks and the byte-identity gate forbids moving them, so — exactly as
    /// for `Im2Controller` — there are two describe methods.
    void describe_timing(jnext::save::StateDesc& d);
    static constexpr std::size_t kTimingStateBytes = 4;   ///< one u8 per channel

private:
    std::array<CtcChannel, 4> channels_;
    uint64_t time_ = 0;   ///< see set_time()
    /// Edges until a ZC/TO of the previous channel reaches each channel's
    /// clk_trg_edge (0 = none in flight). See tick().
    uint8_t trg_delay_[4] = {0, 0, 0, 0};

    /// tick() for a span in which a channel has something to do.
    void tick_events(uint32_t master_cycles);

    /// Handle ZC/TO output from a channel: fire the callbacks and start the
    /// pulse on its way to the next channel of the ring.
    void handle_zc_to(int channel);
};
