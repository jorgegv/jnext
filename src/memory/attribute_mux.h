#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

// Beam-time replay of every byte the ULA fetches from VRAM (GH #305, builds
// on G12 Nirvana-class attribute multiplex).
//
// VHDL authority: cores/zxnext/src/video/zxula.vhd.
//
// The ULA does not read the screen once per frame. On EVERY scanline it
// fetches, for every one of the 32 columns, a pixel byte and an attribute
// byte (or, in the Timex hi-colour / hi-res modes, a second pixel-format
// byte) at a fixed horizontal instant (zxula.vhd:226-303). A CPU write is
// therefore seen by the first fetch that follows it in real time, whichever
// plane it hit. This class keeps, for the ULA-fetched bytes of bank 5 and
// bank 7, a per-frame log of the writes so the renderer can answer "what did
// the ULA read from offset X on scanline L at beam position H".
//
// Which bytes the ULA can fetch (zxula.vhd:191, 223-224, 233-252):
//   * vram_a = screen_mode(0) & addr_p(12:0)       -- pixel slot
//     vram_a = screen_mode(0) & "110" & py(7:3) & col -- attribute slot
//       or, when screen_mode(1) (hi-colour, hi-res),
//     vram_a = '1' & addr_p(12:0)                   -- second-plane slot
//   with addr_p < 0x1800. So bank 5 offsets 0x0000-0x1AFF (page 0x0A) and
//   0x2000-0x3AFF (page 0x0B), and, with the shadow screen, bank 7 offsets
//   0x0000-0x1AFF (page 0x0E; Timex modes are forced to "000" when shadow
//   is on, zxula.vhd:191, because bank 7 is an 8K BRAM). Those are the
//   ranges Mmu::write() records.
//
// Fetch instants, re-derived from zxula.vhd. Let hc be i_hc (hc_ula) and j
// the DISPLAY slot (hc(7:3) at the start of its block). The "record bytes
// read" process (:270-303) latches, on the falling edge of i_CLK_7 with
// hc(3:0) = X, one byte into a register named for the column parity:
//
//   hc(3:0)  1       3       5       7       9       B       D       F
//   register pbyte11 abyte11 pbyte01 abyte01 pbyte00 abyte00 pbyte10 abyte10
//
// Registers *00 / *10 hold the PRIMARY column of an even / odd slot (the
// address uses `px`, latched at hc(3:0)=3 / B, :262-267 and :198-200, so
// column = (j + scroll_x(7:3)) mod 32); registers *01 / *11 hold the
// SECONDARY column (`px_1` = px + 1, :216, :245-248), used only for the bits
// that fine X scroll shifts in from the next byte (shift_pbyte =
// pbyte00 & pbyte01 or pbyte10 & pbyte11, :383-384; shift_abyte likewise).
// The shift register loads at sload_0 (hc(3:0)=C) and sload_1 (hc(3:0)=4),
// :360-382. Written as hc_ula for slot j (even j: block start 8j; odd j: the
// odd half of the block that starts at 8(j-1)):
//
//   primary attribute  A(j) = 8j + 11 (j even, abyte00 @B)
//                           = 8j +  7 (j odd,  abyte10 @F)
//   primary pixel      P(j) = A(j) - 2        (pbyte00 @9 / pbyte10 @D)
//   secondary pixel         = 8j + 5 (j even, pbyte01 @5)
//                           = 8j + 9 (j odd,  pbyte11 @1 of the next block)
//   secondary attribute     = 8j + 7 (j even, abyte01 @7)
//                           = 8j +11 (j odd,  abyte11 @3 of the next block)
//
// hc_ula = 0 at raw hc = c_min_hactive - 11 (registered reset,
// zxula_timing.vhd:423-436), so raw = hc_ula + hc_origin + 1 with
// hc_origin = VideoTiming::ula_prefetch_origin_hc() = c_min_hactive - 12.
// That reproduces the pre-GH#305 attribute instant exactly
// (origin + 8j + 12 even / + 8 odd, verified against FUSE on bifrost.tap,
// Task 54) and extends it to the other three kinds of fetch (fetch_hc()).
// A write is visible to a fetch when its (line, hc) tag is at or before the
// fetch instant, the same convention the attribute plane always used.
//
// Because the instant is by DISPLAY slot, a coarse X scroll moves WHICH byte
// is read at an instant, not the instant itself, and the fine-scroll bits
// come from the secondary fetch (VMUX-09, VMUX-10).
//
// The log is growable: nothing can be dropped. A CPU write needs at least 3
// CPU clocks and a DMA transfer 2, so one frame (70908 T-states = 567264
// 28 MHz clocks at the longest machine timing) holds well under 300 000
// writes, about 3.6 MB of 12-byte entries; typical frames hold a few
// hundred. Per-frame work is proportional to the number of writes: only the
// offsets a write touched are reset (touched_), and the baseline is one
// memcpy of the bank.
//
// Replay: per offset the entries form a chain in append (= chronological)
// order. `read()` advances a per-offset cursor lazily and monotonically; a
// query earlier than the previous one for that offset restarts from the
// baseline, so the instants of different fetch kinds may interleave freely.
class AttributeMux {
public:
    // 32 columns x 24 character rows = one full standard attribute plane
    // (offsets 0x1800-0x1AFF of a bank).
    static constexpr int      kNumBytes  = 768;
    static constexpr uint32_t kAttrBase  = 0x1800;
    // Bank sizes (bank 5: pages 0x0A+0x0B; bank 7: page 0x0E only).
    static constexpr uint32_t kBank5Bytes = 0x4000;
    static constexpr uint32_t kBank7Bytes = 0x2000;

    /// The four kinds of fetch a display slot makes (see the table above).
    enum class Fetch {
        Pixel,      ///< primary pixel (or hi-res screen-0) byte
        Attr,       ///< primary attribute (or hi-colour colour / hi-res screen-1) byte
        PixelNext,  ///< secondary pixel byte, the next column's, shifted in by fine scroll
        AttrNext,   ///< secondary attribute byte
    };

    /// Snapshot `baseline[0..plane_bytes)` as this frame's starting state and
    /// reset the per-frame log. `baseline` may be null (treated as zero).
    /// `hc_origin` is VideoTiming::ula_prefetch_origin_hc(); the default 0
    /// lets bare-Mmu fixtures that never tag an hc resolve on line alone.
    void start_frame(const uint8_t* baseline, int hc_origin = 0,
                     uint32_t plane_bytes = kBank5Bytes);

    /// True once start_frame() has been called. A plain lifecycle invariant:
    /// reads only mean something once a baseline exists, so callers that
    /// render a bare Ula+Mmu without the per-frame lifecycle fall through to
    /// the live-memory read.
    bool started() const { return started_; }

    /// Record that bank offset `offset` was written `value` while scanline
    /// `line` (framebuffer-row space) was the beam position and `hc` the
    /// horizontal position within it. Never drops a write; returns false
    /// only for an offset outside the bank.
    bool record_write(uint16_t line, uint16_t hc, uint32_t offset, uint8_t value);

    /// Reset every replay cursor to the frame baseline (call before the
    /// first apply_changes_for_line of a render pass; the debugger's second
    /// pass re-walks the same log).
    void rewind_to_baseline();

    /// Set the scanline subsequent reads resolve against. O(1).
    void apply_changes_for_line(int line) { target_line_ = static_cast<uint16_t>(line); }

    /// Consume every remaining entry (ignoring the line/hc gate), so writes
    /// tagged past the visible area still land before the next frame.
    void flush_remaining_changes();

    /// Horizontal instant (raw hc) at which display slot `slot` (0..31)
    /// makes a fetch of kind `f`. See the table above.
    uint16_t fetch_hc(int slot, Fetch f) const {
        const int origin = (hc_origin_ < 0) ? 0 : hc_origin_;
        const bool odd = (slot & 1) != 0;
        int d = 0;
        switch (f) {
            case Fetch::Attr:      d = odd ? 8  : 12; break;
            case Fetch::Pixel:     d = odd ? 6  : 10; break;
            case Fetch::PixelNext: d = odd ? 10 : 6;  break;
            case Fetch::AttrNext:  d = odd ? 12 : 8;  break;
        }
        return static_cast<uint16_t>(origin + slot * 8 + d);
    }

    /// Value of bank offset `offset` as the ULA fetch at horizontal instant
    /// `hc` of the scanline set by apply_changes_for_line() reads it: the
    /// last write at or before (line, hc), else the frame baseline.
    uint8_t read(uint32_t offset, uint16_t hc) const {
        if (offset >= nbytes_) return 0;
        if (linked_ != log_.size()) link_pending_();
        if (!hot_[offset]) return base_[offset];   // never written this frame
        OffState& s = st_[offset];
        const uint32_t key = (static_cast<uint32_t>(target_line_) << 16) | hc;
        if (key < s.lastkey) {           // out-of-order query: replay from the start
            s.cursor = s.first;
            s.curval = base_[offset];
        }
        s.lastkey = key;
        while (s.cursor != kNone) {
            const Entry& e = log_[s.cursor];
            if (e.key > key) break;
            s.curval = e.value;
            s.cursor = e.next;
        }
        return s.curval;
    }

    /// Standard attribute plane read (offset 0..767 of 0x1800-0x1AFF) at the
    /// primary attribute instant of its column.
    uint8_t current(int offset) const {
        if (offset < 0 || offset >= kNumBytes) return 0xFF;
        return read(kAttrBase + static_cast<uint32_t>(offset),
                    fetch_hc(offset % 32, Fetch::Attr));
    }

    size_t log_size() const { return log_.size(); }
    void clear();

    // No save_state/load_state: every structure here is rebuilt each frame
    // from live RAM by start_frame() (Emulator::begin_new_frame), so a
    // rewind restores RAM and the next start_frame() re-derives the rest.
    // Mmu::save_state persists only the coarse scanline tag.

private:
    static constexpr uint32_t kNone = 0xFFFFFFFFu;

    struct Entry {
        uint32_t key;      // (line << 16) | hc
        uint32_t next;     // next entry for the same offset, or kNone
        uint16_t offset;
        uint8_t  value;
    };

    struct OffState {
        uint32_t first   = kNone;   // chain head (kNone = offset never written)
        uint32_t last    = kNone;   // chain tail
        uint32_t cursor  = kNone;   // next unconsumed entry
        uint32_t lastkey = 0;       // key of the previous read
        uint8_t  curval  = 0;       // value after the consumed entries
    };

    void reset_touched_();
    void link_pending_() const;

    uint32_t              nbytes_    = 0;
    std::vector<uint8_t>  base_;
    // The write path only appends to log_ (it runs inside the CPU's hot loop);
    // the per-offset chains are built from the new entries the first time they
    // are needed (link_pending_), once per frame in practice.
    mutable std::vector<OffState> st_;
    mutable std::vector<uint8_t>  hot_;   // 1 = offset written this frame (the untouched read stays on two small arrays)
    mutable std::vector<Entry>    log_;
    mutable std::vector<uint32_t> touched_;
    mutable size_t                linked_ = 0;   // log_ entries already chained
    bool                  started_   = false;
    int                   hc_origin_ = 0;
    uint16_t              target_line_ = 0;
};

// Defined here, not in the .cpp: read() is called from the ULA render path, and
// a target that links the video library without the memory one (screenshot_test)
// must still resolve it.
inline void AttributeMux::link_pending_() const
{
    for (; linked_ < log_.size(); ++linked_) {
        const uint32_t n = static_cast<uint32_t>(linked_);
        const uint32_t offset = log_[n].offset;
        OffState& s = st_[offset];
        if (!hot_[offset]) {
            s.first   = n;
            s.last    = n;
            s.cursor  = n;
            s.lastkey = 0;
            s.curval  = base_[offset];
            hot_[offset] = 1;
            touched_.push_back(offset);
        } else {
            log_[s.last].next = n;
            s.last = n;
            if (s.cursor == kNone) s.cursor = n;
        }
    }
}
