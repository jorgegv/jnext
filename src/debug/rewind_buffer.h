#pragma once

#include <cstdint>
#include <vector>

class Emulator;
class StateWriter;
class StateReader;

/// Circular ring buffer of complete emulator state snapshots.
///
/// Stores up to max_frames full snapshots, each of exactly snapshot_bytes.
/// Snapshots are taken at frame boundaries (start of run_frame, before any
/// events are scheduled) so the scheduler queue is always empty at snapshot
/// time — simplifying serialisation significantly.
///
/// When the buffer is full, the oldest snapshot is overwritten (ring wrap).
class RewindBuffer {
public:
    /// Allocate ring buffer with max_frames slots of snapshot_bytes each.
    /// All memory is pre-allocated here — no allocation during normal execution.
    RewindBuffer(size_t max_frames, size_t snapshot_bytes);

    /// Take a snapshot of the current emulator state.
    /// Called at the top of Emulator::run_frame(), before scheduling events.
    void take_snapshot(const Emulator& emu, uint64_t frame_cycle, uint32_t frame_num);

    /// Restore the nearest snapshot with frame_cycle <= target_cycle.
    /// Deserialises into emu.  Returns the frame_cycle of the restored snapshot,
    /// or UINT64_MAX if no snapshot is available.
    uint64_t restore_nearest(uint64_t target_cycle, Emulator& emu) const;

    /// Number of snapshots currently stored (0..max_frames).
    size_t depth() const { return count_; }
    bool   empty() const { return count_ == 0; }

    /// Frame cycle of the oldest stored snapshot.
    uint64_t oldest_frame_cycle() const;

    /// Frame cycle of the newest stored snapshot.
    uint64_t newest_frame_cycle() const;

    /// Frame number of the oldest stored snapshot.
    uint32_t oldest_frame_num() const;

    /// Frame number of the newest stored snapshot.
    uint32_t newest_frame_num() const;

    /// Return the frame_cycle for the snapshot with the given frame_num.
    /// Returns UINT64_MAX if no snapshot with that frame_num is stored.
    uint64_t frame_cycle_for(uint32_t frame_num) const;

    /// Byte size of each snapshot slot (computed once at construction).
    size_t snapshot_bytes() const { return snapshot_bytes_; }

private:
    struct Slot {
        uint64_t             frame_cycle = 0;
        uint32_t             frame_num   = 0;
        std::vector<uint8_t> data;          ///< Pre-allocated snapshot bytes
    };

    std::vector<Slot> slots_;
    size_t head_          = 0;   ///< Next write index (oldest overwritten first)
    size_t count_         = 0;   ///< Number of valid snapshots stored
    size_t snapshot_bytes_;

    /// Index of the slot written at position i (0=oldest, count_-1=newest).
    size_t slot_index(size_t i) const {
        return (head_ + i) % slots_.size();
    }
};
