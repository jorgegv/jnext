#pragma once
#include <cstdint>
#include <array>
#include <functional>

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

private:
    enum class State { RESET, RESET_TC, TRIGGER, RUN, RUN_TC };

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
    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);
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
    void tick(uint32_t master_cycles);

    /// External trigger on a specific channel.
    void trigger(int channel);

    /// Set interrupt enable bits for all channels (from NextREG).
    void set_int_enable(uint8_t mask);

    /// Callback fired when a channel generates ZC/TO and has interrupts enabled.
    /// Parameter is the channel index (0-3).
    std::function<void(int channel)> on_interrupt;

    // ── Accessors for debug / testing ─────────────────────────────

    const CtcChannel& channel(int ch) const { return channels_[ch]; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

private:
    std::array<CtcChannel, 4> channels_;

    /// Handle ZC/TO output from a channel: fire interrupt callback and
    /// trigger the next channel in the daisy-chain.  depth guards against
    /// infinite recursion in pathological all-counter TC=1 ring configs.
    void handle_zc_to(int channel, int depth = 0);
};
