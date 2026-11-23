// GH #537 -- see debug/dac_stream.h.

#include "debug/dac_stream.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <tuple>

#include "core/emulator_config.h"

namespace jnext { namespace dbg {

uint64_t dac_stream_window_cycles() { return MASTER_CLOCK_HZ; }

namespace {

using Key = std::pair<int, uint16_t>;   // (source, pc); pc is 0 for every source but Cpu

struct Acc {
    uint8_t channels = 0;
    std::vector<uint64_t> cycles;       // one per sample, in log order
};

double to_us(double cycles) { return cycles * 1e6 / double(MASTER_CLOCK_HZ); }

}  // namespace

DacStreamStats dac_stream_stats(const std::vector<AudioLogEntry>& entries, uint64_t end_cycle,
                                uint64_t window_cycles) {
    DacStreamStats out;
    out.window_end = end_cycle;
    // (start, end]: only a clock strictly younger than the window has no start (a write at cycle 0 counts);
    // at end == window the window is (0, end] and cycle 0 is outside it.
    const bool bounded = window_cycles != 0 && end_cycle >= window_cycles;
    out.window_start = bounded ? end_cycle - window_cycles : 0;

    size_t from = 0;
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].marker != AudioMarker::None) from = i + 1;

    std::map<Key, Acc> writers;
    const AudioLogEntry* prev = nullptr;   // the previous COUNTED entry
    for (size_t i = from; i < entries.size(); ++i) {
        const AudioLogEntry& e = entries[i];
        if (e.target != AudioTarget::Dac && e.target != AudioTarget::DacMirror) continue;
        if (e.flags & AudioFlag::Dropped) continue;
        if (e.cycle > end_cycle) continue;
        if (bounded && e.cycle <= out.window_start) continue;
        const Key key{int(e.source), e.source == EventSource::Cpu ? e.pc : uint16_t(0)};
        const bool same_sample = prev && prev->cycle == e.cycle && prev->source == e.source &&
                                 prev->pc == e.pc && prev->port == e.port;
        // A writer exists only once it has a sample: a repeat of the previous entry (same source and pc,
        // so the same key) adds its channel to a writer that already holds that sample.
        Acc& a = writers[key];
        if (!same_sample) a.cycles.push_back(e.cycle);
        if (e.chip < 4) a.channels |= uint8_t(1u << e.chip);
        prev = &e;
    }

    for (const auto& kv : writers) {
        const Acc& a = kv.second;
        if (a.cycles.empty()) continue;   // cannot happen (a writer is made by its first sample); never read front() of nothing
        DacWriterStats w;
        w.source = EventSource(kv.first.first);
        w.pc = kv.first.second;
        w.channels = a.channels;
        w.samples = uint32_t(a.cycles.size());
        w.first_cycle = a.cycles.front();
        w.last_cycle = a.cycles.back();
        if (w.samples >= 2) {
            std::vector<uint32_t> iv;
            iv.reserve(a.cycles.size() - 1);
            for (size_t i = 1; i < a.cycles.size(); ++i)
                iv.push_back(uint32_t(a.cycles[i] >= a.cycles[i - 1] ? a.cycles[i] - a.cycles[i - 1] : 0));
            std::vector<uint32_t> sorted = iv;
            std::sort(sorted.begin(), sorted.end());
            w.interval_min = sorted.front();
            w.interval_max = sorted.back();
            w.interval_median = sorted[(sorted.size() - 1) / 2];
            double sum = 0;
            for (uint32_t v : iv) sum += v;
            w.interval_mean = sum / double(iv.size());
            double var = 0;
            for (uint32_t v : iv) var += (double(v) - w.interval_mean) * (double(v) - w.interval_mean);
            w.jitter = std::sqrt(var / double(iv.size()));
            const double limit = DAC_STREAM_GAP_FACTOR * double(w.interval_median);
            for (uint32_t v : iv) if (double(v) > limit) ++w.gaps;
            const uint64_t span = w.last_cycle - w.first_cycle;
            if (span > 0)
                w.rate_millihz = uint32_t(((uint64_t(w.samples) - 1) * MASTER_CLOCK_HZ * 1000ULL + span / 2) / span);
            w.interval_min_us = to_us(w.interval_min);
            w.interval_mean_us = to_us(w.interval_mean);
            w.interval_max_us = to_us(w.interval_max);
            w.jitter_us = to_us(w.jitter);
        }
        out.writers.push_back(w);
    }
    std::stable_sort(out.writers.begin(), out.writers.end(), [](const DacWriterStats& x, const DacWriterStats& y) {
        return std::make_tuple(~x.samples, int(x.source), x.pc) < std::make_tuple(~y.samples, int(y.source), y.pc);
    });
    return out;
}

std::optional<PacingMatch> nearest_pacing(const DacWriterStats& w,
                                          const std::array<CtcChannelState, CTC_CHANNEL_COUNT>& ctc,
                                          const DmaState& dma) {
    if (w.rate_millihz == 0) return std::nullopt;
    std::optional<PacingMatch> best;
    auto offer = [&](int engine, uint32_t rate) {
        if (rate == 0) return;
        const double d = std::fabs(double(w.rate_millihz) - double(rate));
        if (!best || d < std::fabs(double(w.rate_millihz) - double(best->rate_millihz))) {
            PacingMatch m;
            m.engine = engine;
            m.rate_millihz = rate;
            m.rel_diff = (double(w.rate_millihz) - double(rate)) / double(rate);
            best = m;
        }
    };
    if (w.source == EventSource::Cpu) {
        for (size_t c = 0; c < CTC_CHANNEL_COUNT; ++c)
            if (ctc[c].running) offer(int(c), ctc[c].zc_rate_millihz);
    } else if (w.source == EventSource::Dma) {
        if (dma.prescaler != 0) offer(4, dma.rate_millihz);
    }
    return best;
}

}}  // namespace jnext::dbg
