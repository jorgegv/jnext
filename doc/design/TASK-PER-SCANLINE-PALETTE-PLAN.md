# Per-scanline palette snapshot plan

**Date**: 2026-04-25
**Driver**: beast.nex sky-gradient (Copper writes NR 0x41 at rasters
48 / 75 / 95 / 118; jnext renders frame at end → all lines see the last
value → solid color instead of gradient).
**Pattern parallel**: existing per-scanline snapshots for `border`,
`fallback` (NR 0x4A), `ula_enabled` (NR 0x68 bit 7), `tilemap_scroll`.
This plan extends the same pattern to all PaletteManager state.

## Constraints

- **No dynamic memory**. Static buffers only.
- **Worst case**: assume Copper may fire palette writes on every
  scanline; budget for it.
- Render path is hot — replay must be O(1) amortised per write.

## Memory budget

PaletteManager state (`src/video/palette.h:169-178`):

| Bank | Size | Per ARGB+RGB333 entry |
|------|------|---|
| ULA first/second  | 16  | 6 bytes |
| L2  first/second  | 256 | 6 bytes |
| Spr first/second  | 256 | 6 bytes |
| TM  first/second  | 256 | 6 bytes |
| **Total per snapshot** | **~9.4 KB** | |

Full per-scanline snapshot: 312 × 9.4 KB ≈ **2.9 MB**. Too large.

Per-scanline write log instead (deltas):

```
struct PaletteChange {
    uint16_t line;       // 0-311
    uint8_t  palette_id; // 0..7 (4 palettes × 2 banks)
    uint8_t  index;      // 0-255 (or 0-15 for ULA)
    uint16_t rgb333;     // packed RRRGGGBB or 9-bit
};  // 6 bytes (8 with alignment)
```

Worst-case sizing: assume **8 writes per scanline × 312 scanlines = 2496
writes per frame**. At 8 bytes each ≈ **20 KB per frame**. Static array,
fixed cap at e.g. 4096 entries (≈ 32 KB) gives 1.6× headroom. If
exceeded, drop further writes for the frame and log a warning (rare;
most demos do <100/frame, beast does 5).

## Architecture

```
PaletteManager
 ├─ baseline_*_argb_[2][N]  // snapshot taken at frame start
 ├─ baseline_*_rgb333_[2][N]
 ├─ change_log_[MAX_CHANGES_PER_FRAME]
 ├─ change_count_
 ├─ current_line_     // set by Emulator per scanline
 ├─ render_cursor_    // log index; advances during render

  set_*_color(idx, rgb333):
      — write to live state (unchanged behaviour for non-render uses)
      — append to change_log_ if change_count_ < MAX
      — entry uses current_line_

  start_frame():
      — copy live → baseline
      — change_count_ = 0
      — render_cursor_ = 0

  set_current_line(line): current_line_ = line

  rewind_to_baseline():
      — copy baseline → live
      — render_cursor_ = 0

  apply_changes_for_line(line):
      — while render_cursor_ < change_count_
        and change_log_[render_cursor_].line == line:
            replay change → live
            render_cursor_++
```

Renderer flow becomes:

```
render_frame:
    palette_.rewind_to_baseline();
    for row in 0..H:
        palette_.apply_changes_for_line(row);
        render_scanline(row, ...)
```

Save/load state: change_log + baseline are transient (re-derived next
frame); only the live palette is serialised. No format change.

## Implementation phases

- **Phase 0** (this doc) — design.
- **Phase 1** — PaletteManager: add baseline snapshot arrays, change
  log, current_line_, render_cursor_, public API (`start_frame`,
  `set_current_line`, `rewind_to_baseline`, `apply_changes_for_line`).
  Hook all existing `set_*_color` write sites to append to the log.
- **Phase 2** — Emulator wiring:
  - frame start (next to `init_fallback_per_line` etc.) call
    `palette_.start_frame()`
  - `on_scanline(N)` call `palette_.set_current_line(N)`
- **Phase 3** — Renderer: rewind + per-scanline apply in
  `render_frame`. One-line addition inside the existing per-row loop.
- **Phase 4** — Tests:
  - bare PaletteManager: log records each write at correct line; rewind
    + replay reproduces final state; apply_for_line(N) reaches
    expected partial state; cap behaviour (writes after cap dropped).
  - integration test driving NR 0x41 at two scanlines via OUT
    0x253B/0x243B and verifying rendered scanlines see different
    palette entries.
- **Phase 5** — Verify beast renders gradient + full unit + regression.
- **Phase 6** — Independent code review.

## Open question

Q1 — Cap behaviour on overflow: **drop further writes silently** (with
optional once-per-frame warn log) vs. **assert/abort** in debug builds.
Decision: drop + warn. Pragmatic; matches the existing scheduler
overflow handling.

## Out of scope

The same delta-log pattern would generalise to other Copper-reachable
palette-related state, but no observed demo touches them mid-frame
today. Filed here so a future driver can extend the plan rather than
re-discover the boundary:

- **NR 0x14** global transparency colour
- **NR 0x4B** sprite transparency index
- **NR 0x4C** tilemap transparency index
- **NR 0x43 bits 1-3** active palette selector (`active_*_second_`)
- **NR 0x6B bit 4** active tilemap palette select
  (`active_tm_second_`)

Beast.nex does not modify any of these mid-frame; the per-frame value
is sufficient for the gradient driver.
