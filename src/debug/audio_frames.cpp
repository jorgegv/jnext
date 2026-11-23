#include "debug/audio_frames.h"

#include <algorithm>

namespace jnext { namespace dbg {

namespace {
void fill_unknown(AudioFrame::Values& v)
{
    for (auto& chip : v) chip.fill(-1);
}
}

AudioFrame::AudioFrame()
{
    fill_unknown(final_value);
    fill_unknown(before);
}

int16_t AudioFrame::after(size_t chip, size_t reg) const
{
    return final_value[chip][reg] >= 0 ? final_value[chip][reg] : before[chip][reg];
}

bool AudioFrame::changed(size_t chip, size_t reg) const
{
    return after(chip, reg) != before[chip][reg];
}

AudioFrameAccumulator::AudioFrameAccumulator()
{
    fill_unknown(carry_);
}

void AudioFrameAccumulator::forget()
{
    last_completed_.reset();
    fill_unknown(carry_);
}

std::optional<AudioFrame> AudioFrameAccumulator::complete()
{
    if (!open_) return std::nullopt;
    AudioFrame f = *open_;
    open_.reset();
    for (size_t c = 0; c < AY_CHIP_COUNT; ++c)
        for (size_t r = 0; r < AUDIO_FRAME_REGS; ++r)
            carry_[c][r] = f.after(c, r);
    last_completed_ = f.frame;
    return f;
}

std::optional<AudioFrame> AudioFrameAccumulator::flush()
{
    return complete();
}

std::optional<AudioFrame> AudioFrameAccumulator::feed(const AudioLogEntry& e)
{
    if (e.marker != AudioMarker::None) {
        auto done = complete();
        forget();
        return done;
    }

    std::optional<AudioFrame> done;
    if (open_ && e.frame > open_->frame) {
        done = complete();
    } else if (open_ && e.frame < open_->frame) {          // cannot come from the log: a discontinuity
        done = complete();
        forget();
    } else if (!open_ && last_completed_ && e.frame <= *last_completed_) {
        forget();
    }

    const bool counted = e.target == AudioTarget::AyRegister && !(e.flags & AudioFlag::Dropped) &&
                         e.chip < AY_CHIP_COUNT && e.reg < 16;
    if (!counted) return done;

    if (!open_) {
        AudioFrame f;
        f.frame = e.frame;
        if (last_completed_) f.empty_before = e.frame - *last_completed_ - 1;
        f.before = carry_;
        open_ = f;
    }
    AudioFrame& f = *open_;
    const uint16_t bit = uint16_t(1u << e.reg);
    if (f.written_mask[e.chip] & bit) f.double_mask[e.chip] |= bit;
    else if (e.reg < AUDIO_FRAME_REGS) f.before[e.chip][e.reg] = e.prev;
    f.written_mask[e.chip] |= bit;
    if (e.reg < AUDIO_FRAME_REGS) f.final_value[e.chip][e.reg] = e.value;

    const AudioFrameStamp stamp{e.vc, e.hc, e.tstate_in_frame};
    if (f.writes == 0) f.first = stamp;
    f.last = stamp;
    ++f.writes;
    if (e.source == EventSource::Cpu) {
        if (e.pc < f.pc_min) f.pc_min = e.pc;
        if (e.pc > f.pc_max) f.pc_max = e.pc;
    }
    return done;
}

AudioTimingStats audio_timing_stats(const std::vector<const AudioFrame*>& frames, uint32_t window)
{
    AudioTimingStats st;
    if (frames.empty() || window == 0) return st;

    size_t begin = 0;                                       // the run after the last timeline break
    for (size_t i = 1; i < frames.size(); ++i)
        if (frames[i]->frame <= frames[i - 1]->frame) begin = i;

    const uint32_t newest = frames.back()->frame;
    const uint32_t nominal = newest >= window - 1 ? newest - (window - 1) : 0;
    while (begin < frames.size() && frames[begin]->frame < nominal) ++begin;
    if (begin == frames.size()) return st;                  // not reachable: the newest frame is >= nominal

    const AudioFrame& oldest = *frames[begin];
    const uint32_t known = oldest.frame - oldest.empty_before;
    st.window_first = std::max(nominal, known);
    st.window_last = newest;
    st.frames = uint32_t(frames.size() - begin);
    st.empty = (st.window_last - st.window_first + 1) - st.frames;

    auto fold = [&](AudioTimingRange& r, auto get) {
        double sum = 0;
        r.min = UINT32_MAX;
        r.max = 0;
        for (size_t i = begin; i < frames.size(); ++i) {
            const uint32_t v = get(*frames[i]);
            r.min = std::min(r.min, v);
            r.max = std::max(r.max, v);
            sum += v;
        }
        r.avg = sum / double(st.frames);
    };
    fold(st.first_line,   [](const AudioFrame& f) { return uint32_t(f.first.vc); });
    fold(st.first_tstate, [](const AudioFrame& f) { return f.first.tstate; });
    fold(st.last_line,    [](const AudioFrame& f) { return uint32_t(f.last.vc); });
    fold(st.last_tstate,  [](const AudioFrame& f) { return f.last.tstate; });
    fold(st.writes,       [](const AudioFrame& f) { return f.writes; });
    for (size_t i = begin; i < frames.size(); ++i) {
        const AudioFrame& f = *frames[i];
        if (f.pc_min > f.pc_max) continue;
        st.pc_min = std::min(st.pc_min, f.pc_min);
        st.pc_max = std::max(st.pc_max, f.pc_max);
    }
    return st;
}

} }
