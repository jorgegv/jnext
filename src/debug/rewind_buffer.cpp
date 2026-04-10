#include "debug/rewind_buffer.h"
#include "core/emulator.h"
#include "core/saveable.h"

RewindBuffer::RewindBuffer(size_t max_frames, size_t snapshot_bytes)
    : snapshot_bytes_(snapshot_bytes)
{
    slots_.resize(max_frames);
    for (auto& s : slots_) {
        s.data.resize(snapshot_bytes, 0);
    }
}

void RewindBuffer::take_snapshot(const Emulator& emu, uint64_t frame_cycle, uint32_t frame_num)
{
    // Determine write slot: always write to head_, then advance.
    // When buffer is full (count_ == slots_.size()), head_ wraps and overwrites oldest.
    size_t write_idx;
    if (count_ < slots_.size()) {
        // Buffer not yet full: write at position count_, head_ stays at 0 (oldest).
        write_idx = count_;
        ++count_;
    } else {
        // Buffer full: overwrite the oldest slot (at head_) and advance head_.
        write_idx = head_;
        head_ = (head_ + 1) % slots_.size();
    }

    Slot& s = slots_[write_idx];
    s.frame_cycle = frame_cycle;
    s.frame_num   = frame_num;

    StateWriter w(s.data.data(), snapshot_bytes_);
    emu.save_state(w);
}

uint64_t RewindBuffer::restore_nearest(uint64_t target_cycle, Emulator& emu) const
{
    if (count_ == 0) return UINT64_MAX;

    // Find the newest snapshot with frame_cycle <= target_cycle.
    // Slots are ordered oldest→newest from head_ for count_ entries.
    // Walk from newest backwards to find the first that fits.
    size_t best = SIZE_MAX;
    for (size_t i = 0; i < count_; ++i) {
        size_t idx = slot_index(i);
        if (slots_[idx].frame_cycle <= target_cycle) {
            best = idx;
        }
    }

    if (best == SIZE_MAX) {
        // All snapshots are newer than target_cycle — restore the oldest.
        best = slot_index(0);
    }

    const Slot& s = slots_[best];
    StateReader r(s.data.data(), snapshot_bytes_);
    emu.load_state(r);
    return s.frame_cycle;
}

uint64_t RewindBuffer::oldest_frame_cycle() const
{
    if (count_ == 0) return 0;
    return slots_[slot_index(0)].frame_cycle;
}

uint64_t RewindBuffer::newest_frame_cycle() const
{
    if (count_ == 0) return 0;
    return slots_[slot_index(count_ - 1)].frame_cycle;
}

uint32_t RewindBuffer::oldest_frame_num() const
{
    if (count_ == 0) return 0;
    return slots_[slot_index(0)].frame_num;
}

uint32_t RewindBuffer::newest_frame_num() const
{
    if (count_ == 0) return 0;
    return slots_[slot_index(count_ - 1)].frame_num;
}

uint64_t RewindBuffer::frame_cycle_for(uint32_t frame_num) const
{
    for (size_t i = 0; i < count_; ++i) {
        const Slot& s = slots_[slot_index(i)];
        if (s.frame_num == frame_num)
            return s.frame_cycle;
    }
    return UINT64_MAX;
}
