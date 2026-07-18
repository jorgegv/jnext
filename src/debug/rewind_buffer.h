#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "debug/call_stack.h"

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
    /// All memory is reserved here — no allocation during normal execution.
    /// The backing store is a single mmap(MAP_ANONYMOUS) region: pages are
    /// zero-filled by the kernel and faulted in lazily on first write, so
    /// construction does not eagerly memset the (potentially ~1 GB) buffer.
    RewindBuffer(size_t max_frames, size_t snapshot_bytes);
    ~RewindBuffer();

    RewindBuffer(const RewindBuffer&) = delete;
    RewindBuffer& operator=(const RewindBuffer&) = delete;

    /// Take a snapshot of the current emulator state.
    /// Called at the top of Emulator::run_frame(), before scheduling events.
    /// Task 60b (G67): if save_state does not write exactly snapshot_bytes
    /// (schema drift since construction), the slot is NOT published and an
    /// error is logged — a rewind can never restore a garbled snapshot.
    void take_snapshot(const Emulator& emu, uint64_t frame_cycle, uint32_t frame_num);

    /// Restore the nearest snapshot with frame_cycle <= target_cycle.
    /// Deserialises into emu.  Returns the frame_cycle of the restored snapshot,
    /// or UINT64_MAX if no snapshot is available OR the restore failed
    /// sentinel/bounds verification (Task 60b) — in the latter case the
    /// machine is partially restored and must not be reported as rewound.
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

    // ── Test-only hooks (rewind_test, Task 60b) ────────────────────────

    /// Direct access to stored slot bytes (i: 0=oldest .. depth()-1) so
    /// tests can corrupt a snapshot in place (sentinel-chain rows).
    uint8_t* slot_data_for_test(size_t i) { return slots_[slot_index(i)].data; }

    /// Pretend the expected snapshot size shrank after construction —
    /// simulates post-construction save_state schema drift (RB-FRAME-04
    /// eviction row). The real state-stream size is fixed-width by
    /// construction, so the mismatch cannot be produced any other way.
    ///
    /// That claim was FALSE until issue #42: three fields were written
    /// count-prefixed and variable-length, so ordinary guest behaviour —
    /// a single OUT (0xFF),A, or one received UART byte — widened the
    /// stream and every snapshot from then on was silently dropped.
    ///
    /// Full enumeration of runtime-variable-length state reachable from
    /// Emulator::save_state, as audited for issue #42. Anything added to
    /// this list MUST be serialised at constant width or rewind breaks
    /// the same silent way:
    ///
    ///   Ula::port_ff_log_          FIXED — writes MAX_CHANGES_PER_FRAME
    ///   Keyboard::auto_queue_      FIXED — writes MAX_AUTO_TYPE_KEYS
    ///   FifoBuffer<T,Capacity>     FIXED — writes Capacity (UART tx/rx,
    ///                                      2 channels x 2 FIFOs)
    ///   Ram::data_                 SAFE  — sized in the ctor, never
    ///                                      resized; constant for the
    ///                                      lifetime of a RewindBuffer
    ///   AttributeMux::log_         SAFE  — deliberately NOT serialised;
    ///                                      rebuilt each frame. Serialising
    ///                                      it previously caused a heap
    ///                                      corruption crash for exactly
    ///                                      this reason (see attribute_mux.h)
    ///
    /// Everything else writes std::array / fixed-extent buffers, whose
    /// .size() is a compile-time constant.
    ///
    /// RB-SIZE-01..09 in rewind_test pin both the width invariant and the
    /// content round-trip for each fixed field. Shrink only:
    /// the mmap slots keep their construction-time size, so a smaller
    /// claim makes writes overflow the CLAIM, never the allocation.
    void shrink_expected_snapshot_bytes_for_test(size_t n) {
        if (n < snapshot_bytes_) snapshot_bytes_ = n;
    }

private:
    struct Slot {
        uint64_t frame_cycle = 0;
        uint32_t frame_num   = 0;
        uint8_t* data        = nullptr;  ///< Points into block_ (mmap region)
        std::vector<CallFrame> call_frames;
    };

    std::vector<Slot> slots_;
    uint8_t* block_       = nullptr;  ///< mmap(MAP_ANONYMOUS) backing store
    size_t   block_bytes_ = 0;        ///< Total bytes mapped
    size_t head_          = 0;   ///< Next write index (oldest overwritten first)
    size_t count_         = 0;   ///< Number of valid snapshots stored
    size_t snapshot_bytes_;

    /// Index of the slot written at position i (0=oldest, count_-1=newest).
    size_t slot_index(size_t i) const {
        return (head_ + i) % slots_.size();
    }
};
