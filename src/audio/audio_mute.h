#pragma once

#include <cstdint>

/// Per-source audio mute mask — a **jnext debugger-only facility**.
///
/// There is NO hardware analogue: the ZX Next FPGA has no per-source mute.
/// NR 0x09's per-chip mono flags and turbosound.vhd's `psg_pan` gates are
/// real hardware and are modelled elsewhere; this mask is a host-side
/// listening aid driven from the debugger's Audio panel.
///
/// Because it has no hardware analogue, it MUST be invisible to the Z80:
///   - It gates only the *output* stages (TurboSound::compute_stereo_mix()
///     summing the 3 PSGs, and Mixer::mix() summing the 13-bit terms).
///   - The AY chips keep ticking while muted, so a mute never perturbs
///     oscillator/envelope/LFSR phase, and un-muting resumes mid-waveform
///     exactly where the hardware would be.
///   - The AY register file is untouched: reads via port 0xBFFD still return
///     what was written, muted or not.
///   - It is NOT machine state: it is deliberately excluded from
///     save_state()/load_state() (a rewind must not silently un-mute) and
///     from reset() (a machine reset is not a reason to move the user's
///     "volume knob").
///
/// Composition with `--silent` (Task 47): global silence wins trivially —
/// in silent mode the PSG tick and the whole mixer synthesis are skipped in
/// the hot loop, so no source is audible regardless of this mask.
namespace AudioMute {

enum : uint8_t {
    NONE   = 0x00,
    AY0    = 0x01,   // bit i == AY chip i — TurboSound relies on this mapping
    AY1    = 0x02,
    AY2    = 0x04,
    DAC    = 0x08,
    BEEPER = 0x10,   // EAR + MIC + tape EAR (all three ride the beeper path)

    AY_ALL = AY0 | AY1 | AY2,
    ALL    = AY_ALL | DAC | BEEPER,
};

}  // namespace AudioMute
