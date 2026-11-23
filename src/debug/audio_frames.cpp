#include "debug/audio_frames.h"

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

} }
