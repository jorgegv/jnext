#pragma once

// GH #537 (DEBUGGER-MUSIC-SOUND-PLAN.md A2.7) -- the DAC sample stream, computed from the audio write log.
//
// A pure function over `AudioLogEntry` vectors: no Debugger, no Emulator, no Qt, so the Qt view, a test and
// any other client compute the same numbers. One writer's stream is its DAC (and NR 0x2C-0x2E mirror) writes;
// the rules (each one a DSS row in audio_debug_test):
//   counted entry   marker None, target Dac or DacMirror, no Dropped flag, cycle in the window, and after the
//                   newest marker of the vector (a reset restarts the player, a rewind truncated the log).
//   sample          consecutive counted entries with equal (cycle, source, pc, port) are ONE sample: the log
//                   has one entry per DAC channel, and 0xDF, 0xB3, mono 0xFB and NR 0x2D write two channels.
//   writer          (source, pc) for Cpu; (source) alone for Dma, Copper and Debugger, whose pc is the slot's.
//   intervals       between consecutive samples of a writer; the median is the LOWER median; a gap is an
//                   interval strictly above DAC_STREAM_GAP_FACTOR x the median.

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "debug/events.h"
#include "debug/inspect.h"

namespace jnext { namespace dbg {

/// The gap threshold, relative to the stream's own median interval: one skipped sample is exactly 2 periods,
/// one period is 1, so 1.5 sits midway and does not depend on the interrupt-acceptance jitter.
constexpr double DAC_STREAM_GAP_FACTOR = 1.5;

/// The window the debugger view measures over: one second of master cycles.
uint64_t dac_stream_window_cycles();

/// One writer's DAC stream in the window.
struct DacWriterStats {
    EventSource source = EventSource::Cpu;
    uint16_t pc = 0;                 ///< Cpu: the writing instruction; 0 for every other source
    uint8_t  channels = 0;           ///< bit n = DAC channel n (A = 0 .. D = 3) written
    uint32_t samples = 0;            ///< port writes; a fan-out to two channels is ONE
    uint64_t first_cycle = 0, last_cycle = 0;
    uint32_t rate_millihz = 0;       ///< (samples - 1) x 28 MHz / (last - first), rounded; 0 when samples < 2
    uint32_t interval_min = 0, interval_max = 0, interval_median = 0;   ///< master cycles; 0 when samples < 2
    double   interval_mean = 0;      ///< master cycles
    double   jitter = 0;             ///< population standard deviation of the intervals, master cycles
    uint32_t gaps = 0;               ///< intervals strictly above DAC_STREAM_GAP_FACTOR x interval_median
    double   interval_min_us = 0, interval_mean_us = 0, interval_max_us = 0, jitter_us = 0;   ///< for display
};

struct DacStreamStats {
    uint64_t window_start = 0, window_end = 0;   ///< (start, end] in master cycles
    std::vector<DacWriterStats> writers;         ///< samples descending, then source, then pc
};

/// The DAC streams in (end_cycle - window_cycles, end_cycle] (window_cycles 0: everything up to end_cycle),
/// counting only entries after the newest marker.
DacStreamStats dac_stream_stats(const std::vector<AudioLogEntry>& entries, uint64_t end_cycle,
                                uint64_t window_cycles);

/// The engine whose computed rate is nearest a writer's measured one.
struct PacingMatch {
    int      engine = 0;             ///< 0-3 a CTC channel, 4 the DMA
    uint32_t rate_millihz = 0;       ///< that engine's computed rate
    double   rel_diff = 0;           ///< (measured - computed) / computed
};

/// A CTC channel for a Cpu writer (running channels with a rate), the DMA for a Dma writer (non-zero
/// prescaler); nothing for Copper or Debugger writers, or when no candidate has a rate. The nearest wins,
/// the lower channel on a tie.
std::optional<PacingMatch> nearest_pacing(const DacWriterStats& w,
                                          const std::array<CtcChannelState, CTC_CHANNEL_COUNT>& ctc,
                                          const DmaState& dma);

}}  // namespace jnext::dbg
