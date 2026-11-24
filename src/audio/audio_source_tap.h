#pragma once

// The debugger's mixer source tap (GH #536, INS-24). Internal to the audio core and the debug
// backend: the Debugger owns one while a client asks for it, the Emulator holds a raw pointer to it
// and Mixer::integrate_span_tapped() feeds it. NOT machine state: never saved, never reset by the
// machine, never read by the Z80. Standard headers only.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

/// The per-source WAV stems (GH #539): fed by Mixer::integrate_span_tapped() while a stem recording runs.
/// Terms in native 13-bit mixer levels x master cycles, in the order ay0 L R, ay1 L R, ay2 L R, dac L R,
/// beeper, pi L R, mix L R.
struct AudioStemTap {
    static constexpr size_t TERMS = 13;
    /// Seven stereo pairs (ay0 ay1 ay2 dac beeper pi mix), L then R per pair; the beeper pair is mono.
    static constexpr size_t FRAME = 14;
    std::array<int64_t, TERMS> acc{};
    uint64_t acc_cycles = 0;
    uint64_t written = 0;                                     ///< stem frames pushed
    void (*out)(void* ctx, const int16_t* frame) = nullptr;   ///< receives FRAME values
    void* ctx = nullptr;
};

struct AudioSourceTap {
    /// == jnext::dbg::AUDIO_SOURCE_COUNT (static_assert in debugger_audio_scope.cpp).
    static constexpr size_t SOURCES = 16;
    /// ~186 ms at 44.1 kHz.
    static constexpr size_t RING = 8192;

    std::array<int64_t, SOURCES> acc{};   ///< sum of level x master cycles since the last output sample
    uint64_t acc_cycles = 0;              ///< the master cycles those sums cover
    std::vector<int16_t> ring = std::vector<int16_t>(SOURCES * RING);   ///< source-major
    uint64_t written = 0;                 ///< samples pushed since this tap was created
    AudioStemTap* stems = nullptr;        ///< non-null while a stem recording runs (GH #539)

    /// Drop the partial output sample being accumulated here and in the stems: the mixer discards its
    /// own at Emulator::init() and so must the tap, or the next sample is not "whole" for one of them.
    void clear_partial() {
        acc.fill(0);
        acc_cycles = 0;
        if (stems) {
            stems->acc.fill(0);
            stems->acc_cycles = 0;
        }
    }
};
