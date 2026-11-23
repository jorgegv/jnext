#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace jnext { namespace save { class StateDesc; } }

/// Soundrive 4-channel 8-bit DAC emulation.
///
/// Channels A+B → Left, C+D → Right.
/// Matches VHDL soundrive.vhd: registers default to 0x80 (silence),
/// output is 9-bit per stereo channel (sum of two 8-bit channels).
///
/// Port mapping (all write-only, active when DAC enabled via NextREG 0x08 bit 3):
///   Channel A (left):  0x1F, 0xF1
///   Channel B (left):  0x0F, 0xF3
///   Channel C (right): 0x4F, 0xF9
///   Channel D (right): 0x5F, 0xFB
///   Plus various legacy aliases (Covox, Specdrum, etc.)
class Dac {
public:
    Dac();

    void reset();

    /// Write to a DAC channel (0=A, 1=B, 2=C, 3=D).
    void write_channel(int ch, uint8_t val);

    /// NextREG mirror writes (from VHDL: nr_mono writes A+D, nr_left writes B, nr_right writes C).
    void write_mono(uint8_t val);
    void write_left(uint8_t val);
    void write_right(uint8_t val);

    using WriteCallback = std::function<void(int, uint8_t)>;
    void set_write_callback(WriteCallback callback) { write_callback_ = std::move(callback); }

    /// One channel's byte (0=A, 1=B, 2=C, 3=D), for the debugger.
    uint8_t channel(int ch) const { return ch_[ch & 3]; }

    /// Get 9-bit stereo output (A+B for left, C+D for right): the AUDIBLE sum, i.e.
    /// the channel registers with the debugger's channel mute applied (a muted
    /// channel reads its idle 0x80, soundrive.vhd:74-77). Only the mixer reads it.
    uint16_t pcm_left() const  { return static_cast<uint16_t>(aud_[0]) + aud_[1]; }
    uint16_t pcm_right() const { return static_cast<uint16_t>(aud_[2]) + aud_[3]; }

    /// Debugger-only per-channel mute (bit k = channel k, A=0; AudioChannelMute::DAC_A..D >> 9).
    /// It changes only the audible copy `aud_`, kept up to date on each write and on a
    /// mute change, so the mixer's read costs the same muted or not. The registers
    /// (`ch_`: what channel() returns and what describe_state() serialises) never see
    /// it; not machine state: reset() and load_state() leave it alone (audio/audio_mute.h).
    void set_channel_mute(uint8_t m) { channel_mute_ = m & 0x0F; refresh_audible_(); }
    uint8_t channel_mute() const { return channel_mute_; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    /// GH #27 S5 — the ONE field list (design §9.2). `save_state` /
    /// `load_state` are both a walk of this declaration, so the rewind
    /// stream and a `.jns` cannot disagree about which fields exist.
    void describe_state(jnext::save::StateDesc& d);

private:
    uint8_t ch_[4];  // A, B, C, D
    uint8_t aud_[4]; // the audible copy of ch_ (muted channels read 0x80); not serialised
    uint8_t channel_mute_ = 0;  // debugger-only; NOT machine state (not reset, not serialised)
    void refresh_audible_() {
        for (int k = 0; k < 4; ++k) aud_[k] = ((channel_mute_ >> k) & 1) ? 0x80 : ch_[k];
    }
    WriteCallback write_callback_;
};
