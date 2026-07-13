#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

// G12 — Nirvana-class mid-scanline attribute multiplex.
//
// VHDL authority: cores/zxnext/src/video/zxula.vhd.
//   * zxula.vhd:223-224 — `addr_a_spc_12_5 <= "110" & py(7 downto 3);` The
//     attribute-byte VRAM address depends ONLY on `py(7:3)` — the
//     CHARACTER row — not on `py(2:0)`, the pixel row within the 8-line
//     cell.
//   * zxula.vhd:192 — `py_s <= i_vc + scroll_y`, and `py` is re-latched
//     from `py_s` every scanline (zxula.vhd:194-214, falling edge of
//     `i_CLK_7`, gated on `i_hc(3:0) = 0x3 | 0xB` — i.e. twice per
//     scanline, once per pixel-fetch half).
//   * zxula.vhd:226-263 — the VRAM read cycles (`vram_a`/`vram_rd`) that
//     use `addr_a_spc_12_5` fire on cycles `hc(3:0) = 0xA/0xB/0xE/0xF`,
//     which recur every scanline (`i_hc` wraps every line).
//
// Net effect: because `i_vc` (and therefore `py`) increments every
// scanline, the ULA re-issues a FRESH read of the SAME attribute-byte
// address on EACH of the 8 scanlines making up one character cell. This
// is the hardware mechanism Nirvana-class demos exploit — rewrite the
// attribute byte between each scanline's fetch so every scanline in a
// cell shows a different colour ("multicolour").
//
// Per-column granularity: `addr_a_spc_12_5` is combined with the pixel
// column (`px_1(7:3)` / `px(7:3)`, zxula.vhd:235-252), so strictly each
// of the 32 columns in a row gets its own fetch, spread across the
// scanline's horizontal period. True Nirvana demos always write the
// FULL 32-byte row during the horizontal border/blanking period before
// that row's first fetch (there is no time to win a race against 32
// separate column fetches within one active scanline at 3.5-28 MHz CPU
// speeds), so per-SCANLINE replay granularity reproduces the visible
// effect faithfully. Sub-scanline (mid-row column split) racing is a
// separate, much rarer effect and is explicitly out of scope — see
// doc/design/PER-SCANLINE-DISPLAY-STATE-AUDIT.md "Out of scope for this
// audit".
//
// AttributeMux therefore replays, for a target scanline, the most
// recent write to each attribute byte at or before that scanline's tag
// — NOT simply "whatever RAM holds at frame end" (today's behaviour,
// which collapses every cell to its LAST write of the frame).
//
// Shape mirrors the established per-scanline change-log pattern
// (PaletteManager, Layer2, Sprites, Ula scroll/palsel — all in
// src/video/) so a maintainer who already knows one of those reads this
// immediately: start_frame() snapshots a baseline, per-write record()
// appends to a capped log tagged with the current scanline,
// rewind_to_baseline()+apply_changes_for_line() reconstruct "value as
// of scanline N" for the render pass, flush_remaining_changes() drains
// any tail that landed past the visible area.
//
// ---------------------------------------------------------------------
// Column-accurate resolution (Task 8 Nirvana round 4, 2026-07-13)
// ---------------------------------------------------------------------
//
// Round 3 resolved writes at SCANLINE granularity only: whichever value
// was current for an offset as of the scanline tag won, for ALL 32
// columns of that scanline. That is wrong — the paragraph above already
// says the ULA fetches the SAME attribute address again "on EACH of the
// 8 scanlines", but within one scanline it fetches each of the 32
// columns' bytes at a DIFFERENT, strictly increasing horizontal
// position, not once for the whole line (zxula.vhd:226-263 — the
// attribute-address SET cycles alternate with pixel-address SET cycles
// every 4 ticks, `hc(3:0) = 0x1/0x5/0x9/0xD`, and the fetched byte for
// column C becomes visible 2 ticks later per the "record bytes read"
// process at :270-303, `hc(3:0) = 0x3/0x7/0xB/0xF`). A write that lands
// AFTER column C's own fetch instant this scanline cannot retroactively
// recolour column C — it only takes effect from the cell's NEXT fetch
// (the following scanline of the same 8-line cell, or next frame if
// this was the cell's last scanline). Per-scanline granularity applied
// such a write one scanline too early, which is invisible for
// nirvana.tap (writes well ahead of the beam) but corrupts bifrost.tap
// (the whole point of that technique is per-column colour).
//
// Exact fetch instant used here: `hc_fetch(col) = hc_origin + col * 8`.
//   * `hc_origin` = `VideoTiming::ula_prefetch_origin_hc()` (timing.h),
//     itself VHDL zxula_timing.vhd:423 (`c_min_hactive - 12`) — column
//     0's own fetch has completed by the time hc reaches the display
//     window, so it must start 12 ticks earlier; this constant was
//     already implemented and unit-tested (VT-07/VT-08) but never wired
//     into production rendering before this round.
//   * `8` = one character cell's width in the 7 MHz pixel-tick domain
//     (8 pixels/cell, 1 tick/pixel) — matches the cadence established
//     by zxula.vhd's own case block: address-set cycles for a given
//     attribute role (px vs px_1) recur every 16 ticks for a 2-column
//     pair, i.e. 8 ticks per column. The two anchors agree at column 0
//     by construction (that's what `hc_origin` already means).
//   Deliberately NOT modelling the two-column double-buffered
//   pixel-shift-register pipeline visible in zxula.vhd:305-360
//   (`abyte00/01/10/11`, `sload_0`/`sload_1`) — that stagger exists to
//   feed a 16-bit shift register at 14 MHz and has no CPU-write-race
//   observable at the granularity this emulator renders (whole bytes,
//   once per scanline pass); it does not change WHICH column a given
//   write's hc falls before or after, only sub-tick pipeline delay
//   inside the ULA that a same-frame CPU poke cannot exploit anyway.
//
// Because offset -> column is a fixed, 1:1 mapping (`col = offset %
// 32`), and because entries for a single offset are appended in strict
// chronological (line, hc) order, resolving a given offset only needs
// to walk its OWN entries in order — no cross-offset interleaving
// matters. `per_offset_log_[offset]` (indices into the flat `log_`) +
// `per_offset_cursor_[offset]` implement exactly that: a per-offset
// monotonic cursor, advanced lazily by `current(offset)` the first time
// each scanline needs it, consuming every entry that is either from a
// strictly earlier scanline (settled) or from the target scanline at or
// before that offset's own column fetch instant. A later, same-line
// entry that arrives too late for THIS scanline is left unconsumed —
// the very next call (next scanline of the cell, where "strictly
// earlier" now applies unconditionally) picks it up, which is exactly
// the VHDL-faithful "carries forward to the next fetch" behaviour.
class AttributeMux {
public:
    // 32 columns x 24 character rows = one full attribute plane
    // (0x5800-0x5AFF or 0x7800-0x7AFF).
    static constexpr int kNumBytes = 768;

    // Worst case per G12 gap analysis (doc/issues/KNOWN-FUNCTIONALITY-
    // GAPS-AND-PLAN.md "G12"): every one of the 768 bytes rewritten once
    // per scanline of its own 8-line cell = 768 * 8 = 6144. Round up
    // with headroom for a second full 768-byte re-write inside overflow
    // margin.
    static constexpr size_t kMaxLogEntries = 8192;

    /// Snapshot `baseline[0..kNumBytes)` as this frame's starting state
    /// and reset the per-frame log. `baseline` may be null (e.g. Mmu not
    /// yet wired to a physical buffer) — treated as all-zero.
    ///
    /// `hc_origin` is `VideoTiming::ula_prefetch_origin_hc()` for the
    /// active machine — the 7 MHz-domain tick at which column 0's
    /// attribute fetch completes; `hc_fetch(col) = hc_origin + col*8`
    /// (see the column-accurate-resolution block comment above the
    /// class). Defaults to 0 so bare-Mmu unit-test fixtures that never
    /// call `attr_mux_set_current_hc()` (and therefore always record
    /// hc=0) keep resolving on LINE alone — hc=0 trivially satisfies
    /// `hc <= hc_fetch(col)` for every non-negative origin/column, which
    /// is exactly the pre-round-4 scanline-only behaviour.
    void start_frame(const uint8_t* baseline, int hc_origin = 0);

    /// True once start_frame() has been called at least once. NOT a
    /// racing/arm heuristic — a plain lifecycle invariant: `current()`
    /// only reflects real content once a baseline has actually been
    /// snapshotted. Real production rendering always calls
    /// Mmu::attr_mux_start_frame() once per frame before any CPU
    /// execution (see mmu.h), so this is true for the entire life of a
    /// running emulator. It exists so callers that construct a bare
    /// Ula+Mmu fixture and render directly (most subsystem unit/
    /// integration tests, which write attribute bytes straight into Ram
    /// and never touch the per-frame mux lifecycle at all) fall through
    /// to the plain vram_read() path instead of reading an all-zero
    /// `current_` — exactly the pre-G12 behaviour for any caller that
    /// never opts into the per-frame lifecycle, regardless of whether
    /// any write happened to race.
    bool started() const { return started_; }

    /// Record that attribute-plane offset `offset` (0..767) was written
    /// `value` while scanline `line` was the current beam position (per
    /// Mmu::attr_mux_set_current_line, itself mirroring
    /// Emulator::on_scanline) and `hc` was the current horizontal
    /// pixel-tick position within that scanline (per
    /// Mmu::attr_mux_set_current_hc, set from the true per-write T-state
    /// position in src/cpu/z80_cpu.cpp — see that file's
    /// fuse_z80_writebyte()). Returns false and drops the write (once-
    /// per-frame warn via caller) if the log is already full — a
    /// conservative fallback that degrades to a stale (but never wrong-
    /// address) value for the overflow tail.
    bool record_write(uint16_t line, uint16_t hc, uint16_t offset, uint8_t value);

    /// Reset the render-time `current` plane to the frame baseline and
    /// rewind every per-offset replay cursor. Call once per frame (or
    /// once per independent replay pass — see src/debugger/video_panel.cpp,
    /// which re-walks the same already-recorded log a second time for its
    /// own rendering) before the first apply_changes_for_line().
    void rewind_to_baseline();

    /// Set the scanline that subsequent current(offset) calls should
    /// resolve against. O(1) — the actual per-offset resolution work is
    /// deferred to current() itself (lazy, only for offsets the render
    /// pass actually reads this scanline).
    void apply_changes_for_line(int line) { target_line_ = static_cast<uint16_t>(line); }

    /// Unconditionally drain every offset's remaining log entries
    /// (ignoring the line/hc gate) — call once after the per-line render
    /// loop so writes tagged at an off-screen line (e.g. vblank, or past
    /// FB_HEIGHT — see Emulator::on_scanline's post-display comment)
    /// still land in `current` before the next start_frame() re-snapshots
    /// it as next frame's baseline.
    void flush_remaining_changes();

    /// Render-time read: the effective attribute byte for plane offset
    /// `offset`, resolved for the scanline set by the most recent
    /// apply_changes_for_line() call, at COLUMN GRANULARITY: the value
    /// is the last write to this offset that happened either on a
    /// strictly earlier scanline, or on the target scanline at or before
    /// `col`'s own attribute-fetch instant (`hc_fetch(col)`, see the
    /// column-accurate-resolution block comment above the class) — NOT
    /// simply the last write of the scanline regardless of column.
    /// `col = offset % 32` is derived internally (offset->column is a
    /// fixed 1:1 mapping), so callers pass the same `offset` they always
    /// have; no signature change was needed at any call site.
    /// Logically const (each offset's own resolution is idempotent and
    /// monotonic — repeat calls with the same target line are cheap
    /// no-ops); the lazy per-offset cursor advance is implemented via
    /// `mutable` members.
    uint8_t current(int offset) const {
        if (offset < 0 || offset >= kNumBytes) return 0xFF;
        resolve(static_cast<size_t>(offset));
        return current_[static_cast<size_t>(offset)];
    }

    size_t log_size() const { return log_size_; }
    void clear();

    // No save_state/load_state: nothing here needs to survive a rewind
    // snapshot. `log_`/`baseline_`/`current_` are all rebuilt fresh
    // every frame from live RAM by start_frame() (called at the top of
    // every Emulator::run_frame, BEFORE any CPU execution) — a rewind
    // restores RAM content, and the very next start_frame() re-derives
    // everything from that restored content. Serialising `log_` was
    // tried first and was the actual bug behind a "free(): invalid
    // size" heap-corruption crash: RewindBuffer slots are fixed-size,
    // but log_size_ (and therefore the serialised byte count) varies
    // frame to frame — see Mmu::save_state, which persists only the
    // scanline-tag cursor (`attr_mux_current_line_`) that genuinely
    // needs to survive a rewind. (The mux itself is always-on — no
    // "armed" state exists to persist as of Task 8 Nirvana round 3.)
    // `attr_mux_current_hc_` (round 4) does NOT need persisting either:
    // unlike the scanline tag (set once per on_scanline event, so it can
    // go stale between a mid-scanline rewind-load and the next natural
    // scanline boundary), the hc tag is set immediately before EVERY
    // write in the same call sequence that consults it (z80_cpu.cpp),
    // so it is never stale at the point it matters.

private:
    struct Entry {
        uint16_t line;
        uint16_t hc;
        uint16_t offset;
        uint8_t  value;
    };

    /// Column-accurate resolution helper: advance the per-offset cursor
    /// for `offset`, consuming every not-yet-applied entry that is
    /// either from a strictly earlier scanline than `target_line_`
    /// (settled — the render pass has moved past it) or from
    /// `target_line_` itself at or before this offset's own column
    /// fetch instant. Entries within one offset's own list are appended
    /// in strict chronological (line, hc) order, so "stop at the first
    /// entry that doesn't yet qualify" is correct: no later entry for
    /// THIS offset can have an earlier hc. A held-back entry is left in
    /// place for a future call (this offset's next scanline, where
    /// "strictly earlier" makes it unconditional).
    void resolve(size_t offset) const {
        const auto& idx = per_offset_log_[offset];
        size_t& cur = per_offset_cursor_[offset];
        const uint16_t target_hc = hc_fetch(static_cast<int>(offset % 32));
        // TEMPORARY diagnostic (G12): JNEXT_G12_LINEBIAS shifts every
        // entry's line tag, to test whether a pure raster phase offset
        // explains the BIFROST mismatch. Default 0 = no change.
        static const int line_bias = [] {
            const char* e = std::getenv("JNEXT_G12_LINEBIAS");
            return e ? std::atoi(e) : 0;
        }();
        while (cur < idx.size()) {
            const Entry& e = log_[idx[cur]];
            const int eline = static_cast<int>(e.line) + line_bias;
            if (eline < static_cast<int>(target_line_)
                || (eline == static_cast<int>(target_line_) && e.hc <= target_hc)) {
                current_[offset] = e.value;
                ++cur;
            } else {
                break;
            }
        }
    }

    /// `hc_fetch(col) = hc_origin_ + col * 8` — see the column-accurate-
    /// resolution block comment above the class for the VHDL citations
    /// behind both the origin and the 8-tick-per-column stride.
    uint16_t hc_fetch(int col) const {
        const int origin = (hc_origin_ < 0) ? 0 : hc_origin_;
        return static_cast<uint16_t>(origin + col * 8);
    }

    std::array<uint8_t, kNumBytes> baseline_{};
    mutable std::array<uint8_t, kNumBytes> current_{};
    std::array<Entry, kMaxLogEntries> log_{};
    size_t   log_size_        = 0;
    bool     overflow_warned_ = false;
    bool     started_         = false;
    int      hc_origin_       = 0;
    uint16_t target_line_     = 0;

    // Per-offset index into `log_`, in append (= chronological) order.
    // Cleared (not deallocated -- keeps capacity warm across frames) by
    // both start_frame() and rewind_to_baseline(), since the latter is
    // also used to re-walk an already-recorded log a second time (the
    // debugger's independent replay pass).
    mutable std::array<std::vector<uint16_t>, kNumBytes> per_offset_log_{};
    mutable std::array<size_t, kNumBytes> per_offset_cursor_{};
};
