#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

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
    void start_frame(const uint8_t* baseline);

    /// Record that attribute-plane offset `offset` (0..767) was written
    /// `value` while scanline `line` was the current beam position (per
    /// Mmu::attr_mux_set_current_line, itself mirroring
    /// Emulator::on_scanline). Returns false and drops the write (once-
    /// per-frame warn via caller) if the log is already full — a
    /// conservative fallback that degrades to a stale (but never wrong-
    /// address) value for the overflow tail.
    bool record_write(uint16_t line, uint16_t offset, uint8_t value);

    /// Reset the render-time `current` plane to the frame baseline and
    /// rewind the replay cursor. Call once per frame before the first
    /// apply_changes_for_line().
    void rewind_to_baseline();

    /// Apply every logged write tagged with exactly `line`, advancing
    /// the cursor. Log is in scanline order (writes append with a
    /// monotonically non-decreasing line tag), so total cost across a
    /// full render is O(log size).
    void apply_changes_for_line(int line);

    /// Apply every remaining log entry regardless of its line tag —
    /// call once after the per-line render loop so writes tagged at an
    /// off-screen line (e.g. vblank) still land in `current` before the
    /// next start_frame() re-snapshots it as next frame's baseline.
    void flush_remaining_changes();

    /// Render-time read: the effective attribute byte for plane offset
    /// `offset` as reconstructed by the apply_changes_for_line() calls
    /// made so far this render pass.
    uint8_t current(int offset) const {
        return (offset >= 0 && offset < kNumBytes)
            ? current_[static_cast<size_t>(offset)] : 0xFF;
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

private:
    struct Entry {
        uint16_t line;
        uint16_t offset;
        uint8_t  value;
    };

    std::array<uint8_t, kNumBytes> baseline_{};
    std::array<uint8_t, kNumBytes> current_{};
    std::array<Entry, kMaxLogEntries> log_{};
    size_t   log_size_        = 0;
    size_t   render_cursor_   = 0;
    bool     overflow_warned_ = false;
};
