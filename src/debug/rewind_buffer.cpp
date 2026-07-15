#include "debug/rewind_buffer.h"
#include "core/emulator.h"
#include "core/log.h"
#include "core/saveable.h"

#include <new>
#include <sys/mman.h>

RewindBuffer::RewindBuffer(size_t max_frames, size_t snapshot_bytes)
    : snapshot_bytes_(snapshot_bytes)
{
    // One anonymous mapping for all slots: the kernel zero-fills pages and
    // faults them in lazily on first write, so a large buffer (e.g. 500
    // frames × 2.29 MB ≈ 1.09 GB) costs no startup memset and no RSS until
    // snapshots are actually taken (same strategy as Profiler::init()).
    block_bytes_ = max_frames * snapshot_bytes;
    void* p = ::mmap(nullptr, block_bytes_,
                     PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS,
                     -1, 0);
    if (p == MAP_FAILED) {
        block_bytes_ = 0;
        throw std::bad_alloc();   // matches the old vector::resize failure mode
    }
    block_ = static_cast<uint8_t*>(p);

    slots_.resize(max_frames);
    for (size_t i = 0; i < max_frames; ++i) {
        slots_[i].data = block_ + i * snapshot_bytes;
    }
}

RewindBuffer::~RewindBuffer()
{
    if (block_) {
        ::munmap(block_, block_bytes_);
    }
}

void RewindBuffer::take_snapshot(const Emulator& emu, uint64_t frame_cycle, uint32_t frame_num)
{
    // Determine write slot. Not full: the first free slot past the newest
    // (slot_index(count_)). Full: overwrite the oldest slot (at head_).
    const bool   full      = (count_ == slots_.size());
    const size_t write_idx = full ? head_ : slot_index(count_);

    Slot& s = slots_[write_idx];
    StateWriter w(s.data, snapshot_bytes_);
    emu.save_state(w);

    // Task 60b (G67) — bound guard: the slot size was measured from
    // save_state at construction; if the state stream has widened (or
    // shrunk) since, the slot content is not a valid snapshot. The
    // StateWriter's bounds check already prevented any out-of-slot write;
    // here we refuse to publish the slot and fail loudly instead of
    // letting a later rewind feed a garbled snapshot into load_state.
    if (w.overflow() || w.position() != snapshot_bytes_) {
        Log::emulator()->error(
            "rewind: snapshot size mismatch (wrote {} bytes, slot is {}) — "
            "snapshot dropped{}",
            w.position(), snapshot_bytes_,
            full ? "; oldest snapshot destroyed by the aborted write" : "");
        if (full) {
            // The aborted write scribbled over the oldest PUBLISHED slot —
            // unpublish it so a rewind can never restore the garbled data.
            head_ = (head_ + 1) % slots_.size();
            --count_;
        }
        return;
    }

    // Publish the slot only after a size-exact write.
    s.frame_cycle = frame_cycle;
    s.frame_num   = frame_num;
    if (full) {
        head_ = (head_ + 1) % slots_.size();
    } else {
        ++count_;
    }
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
    StateReader r(s.data, snapshot_bytes_);
    if (!emu.load_state(r)) {
        // Task 60b: the snapshot failed sentinel/bounds verification.
        // Emulator::load_state already logged the failing subsystem; the
        // machine is partially restored and NOT trustworthy (documented
        // limitation — no rollback). Report failure instead of a cycle
        // value that claims success.
        Log::emulator()->error(
            "rewind: restore of snapshot @cycle {} failed verification — "
            "machine state is not trustworthy", s.frame_cycle);
        return UINT64_MAX;
    }
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
