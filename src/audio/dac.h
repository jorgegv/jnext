#pragma once

#include <cstdint>

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
    void write_mono(uint8_t val)  { ch_[0] = val; ch_[3] = val; }
    void write_left(uint8_t val)  { ch_[1] = val; }
    void write_right(uint8_t val) { ch_[2] = val; }

    /// Get 9-bit stereo output (A+B for left, C+D for right).
    uint16_t pcm_left() const  { return static_cast<uint16_t>(ch_[0]) + ch_[1]; }
    uint16_t pcm_right() const { return static_cast<uint16_t>(ch_[2]) + ch_[3]; }

private:
    uint8_t ch_[4];  // A, B, C, D
};
