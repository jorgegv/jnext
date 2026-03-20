#pragma once

#include <cstdint>
#include <vector>

/// Beeper emulation: EAR + MIC square-wave output from port 0xFE.
///
/// The beeper accumulates audio samples at 44100 Hz by tracking the
/// current EAR/MIC state and computing the per-sample contribution.
///
/// Volume levels match the VHDL audio_mixer.vhd:
///   EAR high → 0x100 (256), MIC high → 0x020 (32).
class Beeper {
public:
    /// Sample rate for audio output.
    static constexpr int SAMPLE_RATE = 44100;

    Beeper();

    /// Reset beeper state (both lines low).
    void reset();

    /// Set the EAR output bit (port 0xFE bit 4).
    void set_ear(bool on) { ear_ = on; }

    /// Set the MIC output bit (port 0xFE bit 3).
    void set_mic(bool on) { mic_ = on; }

    /// Return the current beeper level as a 13-bit unsigned value,
    /// matching the VHDL mixer scaling (EAR=256, MIC=32).
    uint16_t current_level() const;

    /// Get current EAR state.
    bool ear() const { return ear_; }

    /// Get current MIC state.
    bool mic() const { return mic_; }

private:
    bool ear_ = false;
    bool mic_ = false;
};
