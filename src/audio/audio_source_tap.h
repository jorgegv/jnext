#pragma once

// The debugger's mixer source tap (GH #536, INS-24). Internal to the audio core and the debug
// backend: the Debugger owns one while a client asks for it, the Emulator holds a raw pointer to it
// and Mixer::integrate_span_tapped() feeds it. NOT machine state: never saved, never reset by the
// machine, never read by the Z80. Standard headers only.

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct AudioSourceTap {
    /// == jnext::dbg::AUDIO_SOURCE_COUNT (static_assert in debugger_audio_scope.cpp).
    static constexpr size_t SOURCES = 16;
    /// ~186 ms at 44.1 kHz.
    static constexpr size_t RING = 8192;

    std::array<int64_t, SOURCES> acc{};   ///< sum of level x master cycles since the last output sample
    uint64_t acc_cycles = 0;              ///< the master cycles those sums cover
    std::vector<int16_t> ring = std::vector<int16_t>(SOURCES * RING);   ///< source-major
    uint64_t written = 0;                 ///< samples pushed since this tap was created
};
