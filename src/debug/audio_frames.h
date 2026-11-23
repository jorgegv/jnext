#pragma once

// ---------------------------------------------------------------------------
// GH #534 -- the audio write log grouped by frame: one AudioFrame per frame
// that holds counted AY register writes. A pure function of the entries fed to
// it (no Debugger, no machine, no Qt), published like audio_math.h for its three
// consumers: the Audio panel's Tracker, the player timing of the Audio panel
// (audio_timing_stats(), GH #535) and the .psg writer (A3.1).
//
// The frame number is the event stamper's (`AudioLogEntry::frame`, the number a
// rewind slot carries). The AY keeps the whole written byte in every register
// (`reg(n) <= I_DA`, ym2149.vhd:190-205), so a log entry's `prev` is the
// register's previous raw byte and gives the value at the start of a frame
// without any history.
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

#include "debug/inspect.h"   // AY_CHIP_COUNT; includes debug/events.h (AudioLogEntry)

namespace jnext { namespace dbg {

/// R0-R13, the registers that make sound. R14/R15 are the I/O ports: counted in
/// `writes` and the masks, with no value kept.
constexpr size_t AUDIO_FRAME_REGS = 14;

/// The raster position of a write: the entry's vc, hc and tstate_in_frame.
struct AudioFrameStamp {
    int16_t  vc = 0, hc = 0;
    uint32_t tstate = 0;
};

/// One frame of AY register writes.
struct AudioFrame {
    using Values = std::array<std::array<int16_t, AUDIO_FRAME_REGS>, AY_CHIP_COUNT>;

    uint32_t frame = 0;
    /// Frames with no counted AY write between the previous frame of the same
    /// timeline and this one. 0 for the first frame after construction, a marker
    /// or a backward frame number.
    uint32_t empty_before = 0;
    /// The LAST value written to each register in this frame; -1 = not written.
    Values final_value;
    /// The value at the START of this frame: the `prev` of the frame's first write
    /// to the register when it was written, else carried from the previous frame's
    /// `after()`; -1 = unknown (nothing seen since a marker).
    Values before;
    std::array<uint16_t, AY_CHIP_COUNT> written_mask{};   ///< bit r: register r (0-15) written in this frame
    std::array<uint16_t, AY_CHIP_COUNT> double_mask{};    ///< bit r: register r written twice or more
    uint32_t writes = 0;                                  ///< counted writes, R14/R15 included
    AudioFrameStamp first{}, last{};                      ///< the first and last counted write
    uint16_t pc_min = 0xFFFF, pc_max = 0;                 ///< CPU-source counted writes only; min > max = none

    AudioFrame();

    /// `final_value` if written in this frame, else `before`. R0-R13 only.
    int16_t after(size_t chip, size_t reg) const;
    /// `after(chip, reg) != before[chip][reg]`.
    bool changed(size_t chip, size_t reg) const;
};

/// Groups log entries, oldest first, into frames.
///
/// A COUNTED write is an `AyRegister` entry with no marker and no `Dropped` flag,
/// chip < 3 and reg < 16; every other entry (DAC, mirror, beeper, chip select,
/// dropped write) is never a write, but ANY non-marker entry whose frame is past the
/// open one proves that frame ended. A marker completes the open frame, then
/// forgets the timeline (no previous frame, every carried value unknown): the chips
/// may hold anything after a reset, a load or a landing. A frame number not above
/// the open or last completed one is treated as a marker followed by the entry.
class AudioFrameAccumulator {
public:
    AudioFrameAccumulator();

    /// Feed one log entry. Returns the frame it completed, if any (at most one).
    std::optional<AudioFrame> feed(const AudioLogEntry& e);
    /// Complete the open frame, if any. The timeline is kept.
    std::optional<AudioFrame> flush();
    /// The frame being accumulated, or null.
    const AudioFrame* open() const { return open_ ? &*open_ : nullptr; }

private:
    std::optional<AudioFrame> complete();
    void forget();

    std::optional<AudioFrame> open_;
    std::optional<uint32_t> last_completed_;
    AudioFrame::Values carry_;
};

/// Min, average and max of one per-frame quantity.
struct AudioTimingRange {
    uint32_t min = 0, max = 0;
    double   avg = 0;
};

/// Player timing over a window of frames: where in the frame the AY player writes.
struct AudioTimingStats {
    uint32_t window_first = 0, window_last = 0;   ///< frame numbers covered
    uint32_t frames = 0;                          ///< frames in the window with counted writes; 0 = nothing else is meaningful
    uint32_t empty  = 0;                          ///< frame numbers in [window_first, window_last] with no counted write
    AudioTimingRange first_line, first_tstate;    ///< of AudioFrame::first (vc, tstate)
    AudioTimingRange last_line,  last_tstate;     ///< of AudioFrame::last
    AudioTimingRange writes;
    uint16_t pc_min = 0xFFFF, pc_max = 0;         ///< union of the frames' CPU PC ranges; min > max = none
};

/// Statistics over the last `window` frame numbers ending at the newest of `frames` (completed
/// frames, oldest first). Total on any input: empty input or window 0 gives a default result; a frame
/// whose number is not above the previous one starts a new run and only the frames after the last such
/// break count. Frame numbers before the oldest frame used, minus its `empty_before`, are unknown (the
/// log's ring, a marker, a row cap) and are not counted as empty.
AudioTimingStats audio_timing_stats(const std::vector<const AudioFrame*>& frames, uint32_t window);

} }
