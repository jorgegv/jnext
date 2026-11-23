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
///     "volume knob"). TurboSound therefore keeps two sums: the audible one
///     (muted chips left out) and the hardware one, which is what it
///     serialises, so a snapshot taken while muted is byte-identical to one
///     taken unmuted.
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

/// Per-channel audio mute mask -- the second **jnext debugger-only facility**,
/// beside AudioMute (same rules: no hardware analogue, output stage only, NOT
/// machine state, untouched by reset, load and snapshots, invisible to the Z80).
///
/// 13 bits: AY chip c channel k (A=0, B=1, C=2) is bit 3c+k (bits 0-8), the
/// Soundrive DAC channels A-D are bits 9-12. A set bit makes the mix behave as
/// if that channel's input were silent: 0 for an AY output (zeroed BEFORE the
/// per-chip A/B/C mux, so ACB, mono and pan stay exact), 0x80 (the DAC idle
/// value, soundrive.vhd:74-77) for a DAC channel (Dac keeps an audible copy of its
/// registers; the Mixer reads that, so its mix() is the same code muted or not). The hardware sum that is
/// serialised never sees it. It composes with AudioMute: a channel is heard
/// only if neither its chip/DAC/beeper bit nor its channel bit is set.
namespace AudioChannelMute {

enum : uint16_t {
    NONE    = 0x0000,
    AY_ALL  = 0x01FF,
    DAC_A   = 0x0200,
    DAC_B   = 0x0400,
    DAC_C   = 0x0800,
    DAC_D   = 0x1000,
    DAC_ALL = 0x1E00,
    ALL     = 0x1FFF,
};

/// Bit of AY chip `chip` (0-2), channel `channel` (A=0, B=1, C=2).
constexpr uint16_t ay_bit(int chip, int channel) { return static_cast<uint16_t>(1u << (3 * chip + channel)); }

}  // namespace AudioChannelMute
