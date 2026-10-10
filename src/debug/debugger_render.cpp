// ---------------------------------------------------------------------------
// jnext::dbg::Debugger — INS-14 `render_layer()`: one layer view of the paused
// frame, drawn with no Qt.
//
// GH #278 package Q, work package WP4d. This is the render body of the Qt
// debugger's `VideoLayerView::render_to_image` (`src/debugger/video_panel.cpp`)
// and its three `replay_*` helpers, MOVED here verbatim so every frontend draws
// the same eight views from the same code; `rom_in_sram` came with it. Until
// this file existed the verb refused `Unsupported` from the pending file
// `debugger_pending.cpp` (package B left it to Q,
// doc/design/DEBUG-SUBSYSTEM-ARCHITECTURE.md §11); that file, then empty, was
// deleted (owner decision 2026-09-29).
//
// WHAT CHANGED IN THE MOVE, AND WHAT DID NOT (design-qt §3.7 is the contract):
//
//   * The destination is the caller's buffer, `RENDER_WIDTH` (640) pixels per
//     row at `stride_pixels`. Every layer engine emits 640 cells since G104, so
//     the widget's per-layer width switch — every arm of which resolved to 640 —
//     is gone rather than moved.
//   * Rows 0..vc are filled with 0x00000000 and then rendered, so a cell the
//     layer does not paint, or that the ULA clip zeroes, is alpha 0 =
//     TRANSPARENT. The widget used to pre-fill the checkerboard it shows for
//     transparency instead; drawing it is the widget's job now (it paints the
//     checkerboard under every alpha-0 cell), because a checkerboard is
//     presentation and a DZRP or script client wants the transparency itself.
//   * Rows > vc are NOT touched. The widget fills them with its "not yet
//     rendered" colour, as it always did.
//   * Everything else — the per-scanline replay, the forced ULA bank, the ULA
//     clip, the per-row NR 0x14 / NR 0x4A / palette-select reads, the Layer 2
//     SRAM bank shift — is the widget's code, unchanged.
//
// STATE PRESERVATION IS THIS VERB'S GUARANTEE, not the caller's. The replay
// walks each change log to its end and leaves every live register where it
// started (the long comment above `replay_rewind()`), and the port 0x303B
// sprite status bits, which drawing sprites latches, are put back explicitly
// (see `render_layer()`). Row INS-14-08 checks the whole serialised machine
// state around each of the eight views.
//
// NOT ON THE HOT PATH. Nothing in the emulation loop calls this: its callers are
// the Qt Video panel, which renders only while the machine is paused and only
// the visible tab, and any client that asks.
// ---------------------------------------------------------------------------

#include "debug/debugger_impl.h"

#include <algorithm>
#include <cstdint>

#include "memory/mmu.h"
#include "memory/ram.h"
#include "video/layer2.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/sprites.h"
#include "video/tilemap.h"
#include "video/ula.h"

namespace jnext {
namespace dbg {
namespace {

/// Rows in every `render_layer()` view: the framebuffer's height.
constexpr int RENDER_ROWS = Renderer::FB_HEIGHT;
static_assert(RENDER_ROWS == 256, "the eight views are 256 rows (design-qt §3.7)");

// ---------------------------------------------------------------------------
// Per-scanline state replay (mirrors Renderer::render_frame)
// ---------------------------------------------------------------------------
//
// Every video subsystem keeps a per-frame change log of the register writes
// the Z80 / Copper made mid-frame, tagged with the framebuffer row they landed
// on.  `Renderer::render_frame` rewinds each log to the frame baseline and
// replays it line by line, so row N is composited with the register state that
// was live when the raster crossed row N.  That is what produces raster splits
// — beast.nex's Layer 2 parallax bands, its per-line palette gradient,
// parallax.nex's DMA-multiplexed sprites, tilemap scroll splits.
//
// The panel used to render every row with the END-OF-PAUSE live register
// state, so all of those effects collapsed to a single flat value and the
// panel showed something the compositor never draws.  The compositor is the
// oracle for "what should this layer look like", so the panel replays the
// frame exactly the same way.
//
// This is state-preserving.  Each subsystem's live register state is, by
// construction, equal to the last entry in its change log (every write both
// mutates the live register and appends a log entry).  So rewind → apply rows
// 0..FB_HEIGHT-1 → flush-remaining walks the cursor to the end of the log and
// leaves every live register exactly where it started; the render cursors are
// reset by the next render_frame's rewind anyway.  It is the same round trip
// render_frame performs once per frame — no more, no less.  The Qt panel only
// ever does it while the emulator is PAUSED (VideoLayerView::refresh() draws a
// placeholder instead while running), so there is no concurrent emulation to
// perturb.

void replay_rewind(Emulator& emu)
{
    emu.palette().rewind_to_baseline();
    emu.layer2().rewind_to_baseline();
    emu.sprites().rewind_to_baseline();
    emu.ula().rewind_to_baseline();            // port 0xFF Timex screen-mode
    emu.ula().rewind_scroll_to_baseline();     // ULA scroll
    emu.ula().palsel_rewind_to_baseline();     // ULA active-palette selector
    emu.tilemap().rewind_nr6b_to_baseline();   // NR 0x6B
    emu.mmu().attr_mux_rewind_to_baseline();   // G12 Nirvana-class attribute mux
    emu.renderer().rewind_to_baseline_nr15();  // G02 NR 0x15 priority/sprite-en
}

void replay_line(Emulator& emu, int row)
{
    emu.palette().apply_changes_for_line(row);
    emu.layer2().apply_changes_for_line(row);
    emu.sprites().apply_changes_for_line(row);
    emu.ula().apply_changes_for_line(row);
    emu.ula().apply_scroll_changes_for_line(row);
    emu.ula().palsel_apply_changes_for_line(row);
    emu.tilemap().apply_nr6b_changes_for_line(row);
    emu.mmu().attr_mux_apply_line(row);        // G12 Nirvana-class attribute mux
    emu.renderer().apply_changes_for_line_nr15(row);  // G02 NR 0x15
}

void replay_restore(Emulator& emu)
{
    emu.palette().flush_remaining_changes();
    emu.layer2().flush_remaining_changes();
    emu.sprites().flush_remaining_changes();
    emu.ula().flush_remaining_changes();
    emu.ula().flush_remaining_scroll_changes();
    emu.ula().palsel_flush_remaining_changes();
    emu.tilemap().flush_remaining_nr6b_changes();
    emu.mmu().attr_mux_flush_remaining();  // G12 Nirvana-class attribute mux
    emu.renderer().flush_remaining_changes_nr15();  // G02 NR 0x15
}

}  // namespace

// ===========================================================================
// INS-14 — render_layer
// ===========================================================================

Result Debugger::render_layer(Layer layer, int vc, uint32_t* dst,
                              size_t stride_pixels) const
{
    // Refused, never clamped: a clamped vc would draw a DIFFERENT picture from
    // the one asked for, and a short stride would write past the caller's row.
    // A running machine is not a backend call at all — the widget draws its
    // placeholder instead (design-qt §3.7), so vc < 0 is out of range here too.
    if (static_cast<size_t>(layer) >= LAYER_COUNT) return Result::Unsupported;
    if (vc < 0 || vc >= RENDER_ROWS)               return Result::RefusedUnavailable;
    if (!dst)                                      return Result::RefusedUnavailable;
    if (stride_pixels < RENDER_WIDTH)              return Result::RefusedUnavailable;

    Emulator& emu = impl_->emu;

    // G104: every layer engine emits the canonical 640 cells — the ULA
    // (phase 2), Layer 2 (phase 3), the tilemap in both column modes (phase 4)
    // and the sprites (phase 5) — and so does the compositor.
    constexpr int layer_w = static_cast<int>(RENDER_WIDTH);

    // Layer 2 fetches its pixels straight out of physical SRAM, so it needs
    // the same bank transform the compositor applies (renderer.cpp: the
    // mmu.rom_in_sram() argument to Layer2::render_scanline).  On a Next the
    // ROM lives in SRAM and every ZX RAM bank is shifted by +16 16K-banks
    // (VHDL layer2.vhd:172); without this the panel read Layer 2 pixels from
    // banks 0..N instead of 16..N+16 — i.e. from unrelated (usually zeroed)
    // SRAM, which is why the Layer 2 view rendered solid black on every Next
    // program.
    const bool rom_in_sram = emu.mmu().rom_in_sram();

    // The port 0x303B status bits. Drawing sprites latches collision and
    // max-sprites-per-line — rightly, for the live compositor, which runs the
    // same engine — so the Sprites and Composite views would otherwise leave a
    // guest-visible latch behind every paused refresh: the guest's next read
    // of port 0x303B would see a collision the debugger drew, not the machine.
    // The replay below does not cover them (they are not register writes), so
    // they are put back explicitly (GH #278 WP4d; row INS-14-08/09).
    const uint8_t sprite_status = emu.sprites().peek_status();

    // Replay the frame line by line, exactly as Renderer::render_frame does
    // (see the replay_* helpers above).  Rows past the paused raster position
    // have not been drawn yet this frame, but we still have to walk the
    // change-log cursors across them so replay_restore() puts every live
    // register back where it was.
    replay_rewind(emu);

    for (int row = 0; row < RENDER_ROWS; ++row) {
        replay_line(emu, row);

        // Rows past the raster are the caller's: not drawn this frame yet.
        if (row > vc) continue;

        uint32_t* line = dst + static_cast<size_t>(row) * stride_pixels;

        // Transparent (alpha 0) wherever the layer paints nothing.
        std::fill_n(line, layer_w, 0x00000000u);

        switch (layer) {
            case Layer::Composite:
                // The real compositor, not a second copy of it: the very row
                // body Renderer::render_frame runs (Task 36).  It writes every
                // one of the 640 cells — a composited pixel is never
                // transparent, because wherever all four layers are, the
                // NR 0x4A fallback colour is emitted instead.
                //
                // That fallback colour is exactly why this view has to exist:
                // it belongs to NO layer, so no per-layer view can show it, and
                // the per-layer views therefore do not visibly add up to the
                // picture on screen (sonic.nex: ULA disabled via NR 0x68 b7,
                // Layer 2 empty, whole sky = NR 0x4A = 0x13 = #0092FF).
                emu.renderer().render_row(line, row, emu.mmu(), emu.ram(),
                                          emu.palette(), emu.layer2(),
                                          &emu.sprites(), &emu.tilemap());
                break;

            case Layer::UlaPrimary:
            case Layer::UlaShadow:
                // Force the bank: the live render_scanline() follows the
                // port-0x7FFD b3 shadow selector, so with the shadow screen
                // active the "Primary (bank 5)" view used to show bank 7.
                // GH #95: thread the row's NR $4A fallback into the direct
                // bank render — render_scanline_bank bypasses render_row's
                // per-row set_select_bgnd_argb push, so a ULAnext
                // `ula_select_bgnd` pixel (zxula.vhd:490/499-501/525 →
                // zxnext.vhd:6986-6991) would otherwise show a stale value.
                // Same per-line snapshot the BACKGROUND view reads below.
                emu.ula().render_scanline_bank(
                    line, row, emu.mmu(),
                    /*use_bank7=*/layer == Layer::UlaShadow,
                    Renderer::fallback_to_argb(
                        emu.renderer().fallback_for_line(row)));
                // The ULA is the one layer whose clip window (NR 0x1A) is
                // applied by the COMPOSITOR rather than inside its own
                // render_scanline (VHDL zxnext.vhd:7104 — ula_clipped feeds
                // ula_transparent).  Layer 2 / Tilemap / Sprites all clip
                // themselves, so without this the ULA view was the only layer
                // view showing content the compositor suppresses.
                // apply_ula_clip zeroes the clipped-away cells, which the
                // contract above already means: transparent.
                emu.renderer().apply_ula_clip(line, row);
                break;

            case Layer::Layer2Active:
                // G104 Phase 3: render_scanline_debug always emits 640.
                // active_bank() is re-read per row — it is itself replayed
                // per scanline (Layer2 bank change-log). transparent_rgb is
                // read PER ROW from the renderer's own NR 0x14 snapshot —
                // not from the live PaletteManager::global_transparency() —
                // exactly like the BACKGROUND view's fallback_for_line(row)
                // read below (Task 46).
                // GH #163: the NR 0x43 b2 palette-bank select is replayed
                // per row by replay_line() above (palsel_apply_changes_for_
                // line), exactly like the Layer2 bank change-log, so read it
                // per row too instead of leaving the colours on the live
                // end-of-frame bank.
                emu.layer2().render_scanline_debug(
                    line, row, emu.ram(), emu.palette(),
                    emu.layer2().active_bank(),
                    emu.renderer().transparent_rgb_for_line(row),
                    rom_in_sram,
                    emu.ula().get_active_layer2_palette());
                break;

            case Layer::Layer2Shadow:
                emu.layer2().render_scanline_debug(
                    line, row, emu.ram(), emu.palette(),
                    emu.layer2().shadow_bank(),
                    emu.renderer().transparent_rgb_for_line(row),
                    rom_in_sram,
                    emu.ula().get_active_layer2_palette());
                break;

            case Layer::Sprites:
                // GH #163: NR 0x43 b3, replayed per row — see Layer2Active.
                emu.sprites().render_scanline_debug(
                    line, row, emu.palette(),
                    emu.ula().get_active_sprite_palette());
                break;

            case Layer::Tilemap: {
                bool ula_over[layer_w];
                std::fill_n(ula_over, layer_w, false);
                // G104 phase 4: tilemap render_scanline_debug always
                // emits 640 (no width parameter).
                // GH #168: NR 0x6B b4, replayed per row — see Layer2Active.
                emu.tilemap().render_scanline_debug(
                    line, ula_over, row, emu.ram(), emu.palette(),
                    /*textmode_flags=*/nullptr,
                    emu.ula().get_active_tilemap_palette());
                break;
            }

            case Layer::Background: {
                // The NR 0x4A fallback colour — what the compositor emits where
                // EVERY layer is transparent (VHDL zxnext.vhd:7218-7352).  It
                // belongs to no layer, so it appears in none of the views above;
                // this one makes it inspectable directly, which is the whole
                // point (sonic.nex's sky is nothing but this).
                //
                // Read PER ROW from the renderer's own snapshot — the exact byte
                // render_row feeds fallback_to_argb for this row — not from the
                // live NR 0x4A.  A Copper MOVE to NR 0x4A mid-frame paints a
                // gradient down the raster; a flat swatch of the live register
                // would show only the last value of the frame.
                const uint32_t argb = Renderer::fallback_to_argb(
                    emu.renderer().fallback_for_line(row));
                std::fill_n(line, layer_w, argb);
                break;
            }

            case Layer::Count:   // refused above; here for -Werror=switch
                break;
        }
    }

    replay_restore(emu);
    emu.sprites().restore_status(sprite_status);
    return Result::Ok;
}

}  // namespace dbg
}  // namespace jnext
