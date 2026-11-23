#pragma once

#include <cstdint>
#include <vector>

namespace jnext { namespace save { class StateDesc; } }

/// Beeper emulation: EAR + MIC square-wave output from port 0xFE.
///
/// The beeper accumulates audio samples at 44100 Hz by tracking the
/// current EAR/MIC state and computing the per-sample contribution.
///
/// Volume levels match the VHDL audio_mixer.vhd (audio_mixer.vhd:63-64):
///   EAR high -> 0x200 (512), MIC high -> 0x080 (128), both 13-bit terms.
/// The mixer has two beeper-side inputs, `ear_i` and `mic_i`, and no tape
/// input: `mic_i` is `beep_mic_final` (zxnext.vhd:6503, 6516), which is
/// `mic_final()` here.
class Beeper {
public:
    /// Sample rate for audio output.
    static constexpr int SAMPLE_RATE = 44100;

    Beeper();

    /// Reset beeper state (both lines low).
    void reset();

    /// Set the EAR output bit (port 0xFE bit 4).
    void set_ear(bool on) { ear_ = on; }

    /// Set the MIC output bit (port 0xFE bit 3). With no tape playing and
    /// no Pi audio routed to EAR, `beep_mic_final = port_fe_mic`
    /// (zxnext.vhd:6503), so this also sets `mic_final`; the emulator
    /// refreshes it with the full formula where a tape or the Pi is involved.
    void set_mic(bool on) { mic_ = on; mic_final_ = on; }

    /// Set the raw tape EAR input bit (from real-time tape playback). It is
    /// NOT a mixer input: the tape reaches the mix only through
    /// `mic_final` (`beep_mic_final`, zxnext.vhd:6503, 6516).
    void set_tape_ear(bool on) { tape_ear_ = on; }

    /// Set `beep_mic_final`, the mixer's `mic_i` (zxnext.vhd:6503, 6516):
    /// `i_AUDIO_EAR xor (port_fe_mic and nr_08_keyboard_issue2) xor
    /// port_fe_mic`. DERIVED state, not serialised: the emulator computes it
    /// (`Emulator::tape_out_level`) at the events that change it.
    void set_mic_final(bool on) { mic_final_ = on; }

    /// Get current EAR state.
    bool ear() const { return ear_; }

    /// Get current MIC state.
    bool mic() const { return mic_; }

    /// Get current tape EAR input state.
    bool tape_ear() const { return tape_ear_; }

    /// `beep_mic_final`: the signal on the mixer's MIC input.
    bool mic_final() const { return mic_final_; }

    void save_state(class StateWriter& w) const;
    void load_state(class StateReader& r);

    /// GH #27 S5 — the ONE field list (design §9.2). `save_state` /
    /// `load_state` are both a walk of this declaration, so the rewind
    /// stream and a `.jns` cannot disagree about which fields exist.
    void describe_state(jnext::save::StateDesc& d);

private:
    bool ear_ = false;
    bool mic_ = false;
    bool tape_ear_ = false;
    bool mic_final_ = false;   // derived (not in describe_state): see set_mic_final()
};
