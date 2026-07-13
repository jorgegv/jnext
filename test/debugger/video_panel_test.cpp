// Debugger Video Panel Tests (Task 22a — "debugger video panels are broken")
// ===========================================================================
//
// The debugger's per-layer views (src/debugger/video_panel.cpp,
// VideoLayerView::render_to_image) must reproduce, for the paused frame, what
// the live compositor (src/video/renderer.cpp, Renderer::render_frame) draws
// for that layer.  The compositor is the oracle: everything it feeds the layer
// engines, the panel must feed them too.  Several pieces of that state had
// drifted, and this suite pins each one.
//
// Rows and the defect each catches (all FAIL against the pre-Task-22a panel):
//
//   DVP-01  Layer 2 active view honours mmu.rom_in_sram().  The panel called
//           Layer2::render_scanline_debug() without the rom_in_sram argument
//           (it defaults to false), so on a Next — where ROM lives in SRAM and
//           every ZX RAM bank is shifted by +16 16K-banks, VHDL layer2.vhd:172
//           — it fetched Layer 2 pixels from banks 0..N instead of 16..N+16.
//           The view rendered solid black on every Next program.
//   DVP-02  Layer 2 shadow view honours rom_in_sram (same defect).
//   DVP-03  ULA "Primary (bank 5)" view shows bank 5 even while port 0x7FFD
//           bit 3 selects the shadow screen.  The panel called the LIVE
//           Ula::render_scanline(), which follows vram_use_bank7_, so with the
//           shadow screen active BOTH radio buttons showed bank 7.
//   DVP-04  ULA "Shadow (bank 7)" view shows bank 7 even while bank 5 is the
//           selected screen (regression guard for the other direction).
//   DVP-05  Per-scanline palette replay.  render_frame rewinds the palette to
//           the frame baseline and replays the change log line by line, so a
//           mid-frame NR 0x41/0x44 write only affects rows at/below the line
//           it landed on.  The panel rendered every row with the end-of-pause
//           live palette, collapsing every raster split (beast.nex's sky
//           gradient, its Layer 2 parallax bands) to a single flat value.
//   DVP-06  The replay is state-preserving: a panel refresh must not perturb
//           any live register.  (A debug view that mutates the state it
//           observes is a Heisenbug generator.)  NOTE: this group has a known
//           blind spot — it cannot catch a MISSING replay_restore()/flush; only
//           DVP-16c can.  See the comment above test_no_state_mutation().
//   DVP-07  Tilemap per-line scroll snapshots are READ, not overwritten.
//           Tilemap::render_scanline_debug used to write the LIVE scroll into
//           scroll_{x,y}_per_line_[y], which both flattened mid-frame scroll
//           splits AND corrupted emulation state for the rest of the frame.
//   DVP-08  Raw-VC → framebuffer-row conversion (G164v2 / Task 13):
//           fb_row = raw_vc - VideoTiming::vblank_top().  The panel fed the
//           raw VC straight in as a row index, so the "already rendered"
//           cut-off and the red raster marker sat vblank_top (32 on Next)
//           rows below the true raster position.
//   DVP-09  Rows past the raster are marked unrendered, rows at/before it are
//           drawn (pins the cut-off semantics DVP-08 depends on).
//   DVP-10  "Run to EOF" targeted raw VC 255 = framebuffer row 223 on a Next,
//           so it stopped 32 rows short of the end of the frame.
//   DVP-11  "Run to EOSL" classified raw VC 256..287 (the bottom border,
//           framebuffer rows 224..255) as blanking and skipped to the next
//           frame instead of stepping one scanline.
//   DVP-12  The ULA views apply the NR 0x1A clip window.  Layer 2 / Tilemap /
//           Sprites clip inside their own render_scanline; the ULA's clip is a
//           compositor-stage signal (zxnext.vhd:7104), which the panel skipped
//           — so the ULA view was the only one showing suppressed content.
//
// Task 36 — the "All layers" (composite) view.  The per-layer views above can
// never add up to the picture on screen, because the NR 0x4A FALLBACK COLOUR
// belongs to no layer: the compositor emits it wherever every layer is
// transparent (VHDL zxnext.vhd:7218-7352, the `else` of each priority mux).
// sonic.nex is the case that surfaced this — it disables the ULA (NR 0x68 b7),
// leaves Layer 2 empty, and paints its whole sky with NR 0x4A = 0x13 = #0092FF
// — so ULA and Layer 2 panels are blank and NOTHING shows the sky.  The
// composite view fixes that by rendering through Renderer::render_row, i.e.
// the very row body render_frame runs.  These rows pin that:
//
//   DVP-13  The composite view is pixel-for-pixel the emulator's own
//           framebuffer for the same frame (all four layers in the scene).
//   DVP-14  The NR 0x4A fallback colour shows where every layer is
//           transparent — the sonic.nex case: ULA disabled, Layer 2 off, only
//           tiles + sprites — and the per-layer views cannot show it.
//   DVP-15  The composite honours the raster cut-off convention of the other
//           views (rows past the paused row are the unrendered placeholder).
//   DVP-16  Compositing for the panel leaves the live machine state untouched
//           (the round trip render_frame does — no more, no less).
//   DVP-17  "All layers" is the leftmost tab and the selected one by default.
//   DVP-18  The Background tab shows the NR 0x4A fallback colour, read PER
//           SCANLINE from the renderer's own snapshot (DVP-18d) rather than
//           from the live register.
//   DVP-18b The reason that matters, proved end to end: a REAL Copper program
//           (assembled bytecode, uploaded via NR 0x60/0x61/0x62, executed by
//           run_frame()) MOVEs NR 0x4A mid-frame, and the band split shows up
//           in the panel — and in the emulator's own framebuffer (DVP-18c).
//   DVP-19  It honours the raster cut-off, and rendering it preserves state.
//   DVP-20  "Background" is the RIGHTMOST tab.
//   DVP-21  Task 46: the Layer 2 ACTIVE view reads NR 0x14 (global
//           transparency) PER SCANLINE from Renderer::transparent_rgb_for_line
//           rather than the live PaletteManager::global_transparency() —
//           the same per-line-snapshot bug class as DVP-05/DVP-18, against a
//           third array. Layer2::render_scanline_debug used to read the live
//           register directly, so a mid-frame NR 0x14 write flattened every
//           row in the view to the frame's END-OF-PAUSE value.
//
// Qt is required (the panel is a QWidget), but no display is: the test forces
// the offscreen QPA platform.  Run: ./build/test/debugger_video_panel_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debugger/debugger_manager.h"
#include "debugger/video_panel.h"
#include "debugger/nextreg_panel.h"
#include "debug/debug_state.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "video/layer2.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/tilemap.h"
#include "peripheral/copper.h"
#include "audio/i2s.h"
#include "video/ula.h"

#include "video/sprites.h"
#include "cpu/z80_cpu.h"
#include "port/port_dispatch.h"

#include <QApplication>
#include <QImage>
#include <QMainWindow>
#include <QTabWidget>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ── Test infrastructure (mirrors the other subsystem suites) ──────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string group;
    std::string id;
    std::string desc;
    bool        passed;
    std::string detail;
};

std::vector<Result> g_results;
std::string         g_group;

struct SkipNote {
    std::string id;
    std::string reason;
};
std::vector<SkipNote> g_skipped;

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    g_results.push_back(Result{g_group, id, desc, cond, detail});
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string fmt(const char* fmt_str, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

// ── Emulator helpers ──────────────────────────────────────────────────

bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;   // Next → mmu.rom_in_sram() == true
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

// Snapshot the current subsystem state as the frame baseline and reset every
// per-scanline change log — exactly what Emulator::run_frame does at the top
// of each frame (src/core/emulator.cpp:6065-6112).  The panel's replay rewinds
// to these baselines, so a test that sets up state must establish it as the
// baseline first, or the rewind would (correctly) throw the setup away.
void begin_frame(Emulator& emu) {
    emu.renderer().init_fallback_per_line();
    emu.renderer().init_ula_enabled_per_line();
    emu.renderer().init_transparent_rgb_per_line();
    emu.renderer().init_ula_clip_per_line();
    emu.ula().init_border_per_line();
    emu.tilemap().init_scroll_per_line();
    emu.tilemap().start_frame_nr6b();
    emu.palette().start_frame();
    emu.layer2().start_frame();
    emu.sprites().start_frame();
    emu.ula().start_frame();
    emu.ula().start_frame_scroll();
    emu.ula().palsel_start_frame();
}

// Write a Layer 2 palette entry.  NR 0x43 control 0x10 selects the Layer 2
// first palette; NR 0x44 takes two bytes in 9-bit mode (RRRGGGBB, then
// b0 = blue LSB / b7 = priority).  Same idiom as test/layer2/layer2_test.cpp.
void set_l2_palette(PaletteManager& pal, uint8_t idx, uint8_t rgb8) {
    pal.write_control(0x10);
    pal.set_index(idx);
    pal.write_9bit(rgb8);
    pal.write_9bit(0x00);
}

// Physical SRAM byte address of a Layer 2 pixel, per VHDL layer2.vhd:172.
// `rom_in_sram` adds the fixed +0x20-8K-page (= +16 16K-bank) SRAM offset.
uint32_t l2_phys_addr(uint8_t bank, uint32_t l2_addr, bool rom_in_sram) {
    int final_bank = bank + static_cast<int>(l2_addr >> 14)
                   + (rom_in_sram ? 16 : 0);
    return static_cast<uint32_t>(final_bank) * 16384u + (l2_addr & 0x3FFFu);
}

// Layer 2 resolution 0 (256x192, 8bpp, row-major): l2_addr = y*256 + x.
uint32_t l2_addr_256x192(int x, int y) {
    return static_cast<uint32_t>(y) * 256u + static_cast<uint32_t>(x);
}

// The framebuffer cell the panel emits for source pixel x of the 256x192 mode.
// G104 phase 3: pixel-doubled into [DISP_X .. DISP_X+512), DISP_X = 64.
int l2_cell_256(int x) { return 64 + 2 * x; }

// Framebuffer row of display line `screen_row` (top border = 32 rows).
int fb_row_of(int screen_row) { return 32 + screen_row; }

// Render one layer view at framebuffer row `fb_row` and hand back the image.
// Renders through the real panel widget, so every step the panel takes (bank
// selection, rom_in_sram, per-line replay, unrendered fill) is under test.
QImage render_view(Emulator& emu, VideoLayerView::Layer layer, int fb_row) {
    VideoLayerView view(layer, "test", &emu);
    view.refresh(fb_row);
    return view.image();
}

uint32_t px(const QImage& img, int x, int y) {
    return img.pixel(x, y) | 0xFF000000u;   // normalise alpha for comparison
}

// ── Task 36 (composite view) fixture helpers ──────────────────────────

// The panel's "not yet rendered" fill (video_panel.cpp: UNRENDERED_ARGB).
constexpr uint32_t UNRENDERED = 0xFF111111;

// Paint a palette entry in one of the four 8-bit palettes.  `ctl` is the
// NR 0x43 control byte: 0x00 ULA-first, 0x10 Layer2-first, 0x20 Sprite-first,
// 0x30 Tilemap-first (PaletteId, palette.h:15-18).
void paint_palette(PaletteManager& pal, uint8_t ctl, uint8_t idx, uint8_t rgb8) {
    pal.write_control(ctl);
    pal.set_index(idx);
    pal.write_8bit(rgb8);
}

// Tilemap map / definition memory live in bank 5, which on a full Emulator is
// the dedicated 16K VRAM BRAM (Tilemap::vram_read → bank5_vram_), NOT Ram.
// NR 0x6E (map) and NR 0x6F (definitions) each hold a 256-byte-unit offset
// into that bank.  The reset defaults (0x2C / 0x0C) put the tile definitions
// INSIDE the ULA screen at 0x0C00, and the 40×32×2-byte map overlaps them, so
// the fixtures below re-base both: map → 0x2000, definitions → 0x3000.  Both
// are clear of the ULA screen (0x0000-0x1AFF) and of each other.
constexpr uint8_t  TM_MAP_BASE = 0x20;                       // NR 0x6E
constexpr uint8_t  TM_DEF_BASE = 0x30;                       // NR 0x6F
constexpr uint32_t TM_MAP_OFF  = TM_MAP_BASE * 256u;         // 0x2000
constexpr uint32_t TM_DEF_OFF  = TM_DEF_BASE * 256u;         // 0x3000
constexpr int      TM_TILES_PER_ROW = 40;   // 40-column mode (NR 0x6B b6 = 0)

// 4bpp tile: every pixel of tile `idx` = `pixel_val` (low nibble).
void fill_tile(uint8_t* b5, uint16_t idx, uint8_t pixel_val) {
    const uint32_t addr = TM_DEF_OFF + static_cast<uint32_t>(idx) * 32u;
    const uint8_t  b    = static_cast<uint8_t>(((pixel_val & 0x0F) << 4)
                                             | (pixel_val & 0x0F));
    for (int i = 0; i < 32; ++i) b5[addr + i] = b;
}

// 2-byte map entry (tile index + attribute).
void write_tile_map(uint8_t* b5, int col, int row, uint8_t idx, uint8_t attr) {
    const uint32_t addr = TM_MAP_OFF
        + static_cast<uint32_t>(row * TM_TILES_PER_ROW + col) * 2u;
    b5[addr]     = idx;
    b5[addr + 1] = attr;
}

// Upload a solid 16×16 8bpp sprite pattern and make sprite 0 show it.
void put_solid_sprite(SpriteEngine& spr, uint8_t pat_idx, uint8_t colour_idx,
                      uint8_t x, uint8_t y) {
    spr.write_slot_select(pat_idx & 0x3F);          // bit 7 = 0 → pattern port
    for (int i = 0; i < 256; ++i) spr.write_pattern(colour_idx);

    spr.write_slot_select(0);                        // sprite 0 attributes
    spr.write_attribute(x);                          // byte 0: X lsb
    spr.write_attribute(y);                          // byte 1: Y lsb
    spr.write_attribute(0x00);                       // byte 2: no pal offset/mirror
    spr.write_attribute(0x80 | (pat_idx & 0x3F));    // byte 3: visible, pattern

    spr.set_clip_x1(0);   spr.set_clip_x2(0xFF);
    spr.set_clip_y1(0);   spr.set_clip_y2(0xFF);
    spr.set_over_border(true);
    spr.set_sprites_visible(true);
}

// Does `argb` appear anywhere in the 640×256 buffer?
bool contains(const uint32_t* fb, uint32_t argb) {
    for (int i = 0; i < Renderer::FB_WIDTH * Renderer::FB_HEIGHT; ++i)
        if (fb[i] == argb) return true;
    return false;
}

// …and in a rendered layer view?
bool contains(const QImage& img, uint32_t argb) {
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            if (px(img, x, y) == argb) return true;
    return false;
}


} // namespace

// ── DVP-01/02: Layer 2 views must apply the rom_in_sram bank shift ────

static void test_layer2_rom_in_sram(Emulator& emu) {
    set_group("DVP-L2-SRAM");

    constexpr uint8_t ACTIVE_BANK = 9;
    constexpr uint8_t SHADOW_BANK = 20;
    constexpr uint8_t IDX_TRUE    = 0x40;   // what the VHDL-correct fetch sees
    constexpr uint8_t IDX_DECOY   = 0x41;   // what the un-shifted fetch would see
    constexpr int     SCREEN_ROW  = 10;
    constexpr int     SRC_X       = 7;

    const bool rom_in_sram = emu.mmu().rom_in_sram();
    check("DVP-00",
          "ZXN_ISSUE2 machine really has ROM in SRAM (test premise)",
          rom_in_sram,
          fmt("rom_in_sram=%d", int(rom_in_sram)));

    Layer2& l2 = emu.layer2();
    l2.set_enabled(true);
    l2.set_control(0x00);              // resolution 0 = 256x192 8bpp
    l2.set_active_bank(ACTIVE_BANK);
    l2.set_shadow_bank(SHADOW_BANK);
    l2.set_clip_x1(0); l2.set_clip_x2(255);
    l2.set_clip_y1(0); l2.set_clip_y2(191);

    // Distinct opaque colours so a wrong fetch cannot accidentally match.
    PaletteManager& pal = emu.palette();
    pal.set_global_transparency(0xE3);              // not any colour we use
    set_l2_palette(pal, IDX_TRUE,  0xFF);          // white-ish
    set_l2_palette(pal, IDX_DECOY, 0x3F);          // cyan-ish
    const uint32_t argb_true  = pal.layer2_colour(IDX_TRUE);
    const uint32_t argb_decoy = pal.layer2_colour(IDX_DECOY);

    const uint32_t addr = l2_addr_256x192(SRC_X, SCREEN_ROW);

    // Plant the real pixel where the VHDL bank transform says it lives, and a
    // decoy where the un-shifted (buggy) fetch would land.  Both banks are
    // written for the active and the shadow bank.
    Ram& ram = emu.ram();
    for (uint8_t bank : {ACTIVE_BANK, SHADOW_BANK}) {
        ram.write(l2_phys_addr(bank, addr, /*rom_in_sram=*/true),  IDX_TRUE);
        ram.write(l2_phys_addr(bank, addr, /*rom_in_sram=*/false), IDX_DECOY);
    }

    begin_frame(emu);

    const int   row  = fb_row_of(SCREEN_ROW);
    const int   cell = l2_cell_256(SRC_X);

    {
        QImage img = render_view(emu, VideoLayerView::Layer::LAYER2_ACTIVE, row);
        const uint32_t got = px(img, cell, row);
        check("DVP-01",
              "Layer2 ACTIVE view fetches through the rom_in_sram +16 bank shift",
              got == argb_true,
              fmt("got=0x%08X want=0x%08X (unshifted decoy=0x%08X)",
                  got, argb_true, argb_decoy));
    }
    {
        QImage img = render_view(emu, VideoLayerView::Layer::LAYER2_SHADOW, row);
        const uint32_t got = px(img, cell, row);
        check("DVP-02",
              "Layer2 SHADOW view fetches through the rom_in_sram +16 bank shift",
              got == argb_true,
              fmt("got=0x%08X want=0x%08X (unshifted decoy=0x%08X)",
                  got, argb_true, argb_decoy));
    }
}

// ── DVP-21: Layer 2 ACTIVE view honours the per-line NR 0x14 snapshot ──
//
// Task 46: Layer2::render_scanline_debug used to read the LIVE
// PaletteManager::global_transparency() instead of the per-line NR 0x14
// snapshot Renderer::transparent_rgb_for_line() exposes, so a mid-frame
// Copper MOVE to NR 0x14 flattened every row of the LAYER2_ACTIVE/SHADOW
// debugger views to the frame's END-OF-PAUSE value — the same per-line-
// snapshot bug class as DVP-05 (palette) and DVP-18 (fallback colour),
// against a third array that happens to live in Renderer rather than in
// the subsystem being viewed. Fixed by threading transparent_rgb through
// render_scanline_debug and having video_panel.cpp pass
// emu.renderer().transparent_rgb_for_line(row) at the call site, exactly
// mirroring the BACKGROUND view's fallback_for_line(row) read.
//
// SCOPE: mirrors DVP-18a/18d/19a — pokes Renderer::snapshot_transparent_
// rgb_for_line() directly to simulate the per-scanline capture
// Emulator::on_scanline performs (emulator.cpp:7648), proving the VIEW
// reads the per-line array rather than the live register. It does not
// drive a real Copper program (that end-to-end proof is DVP-18b/18c's
// role for the fallback colour; not duplicated here).

static void test_layer2_transparent_rgb_replay(Emulator& emu) {
    set_group("DVP-L2-TRANSP");

    // Matches video_panel.cpp's own checker constants exactly (duplicated
    // here rather than exposed, following this file's existing convention
    // for UNRENDERED_ARGB).
    constexpr uint32_t CHECKER_DARK_ARGB = 0xFFAAAAAA;
    constexpr uint32_t CHECKER_LITE_ARGB = 0xFFCCCCCC;
    constexpr int       CHECK_SZ         = 8;
    auto checker_at = [](int row, int col) -> uint32_t {
        const bool dark = (((row / CHECK_SZ) ^ (col / CHECK_SZ)) & 1) != 0;
        return dark ? CHECKER_DARK_ARGB : CHECKER_LITE_ARGB;
    };

    constexpr uint8_t BASELINE_NR14 = 0xE3;   // VHDL reset default (zxnext.vhd:4946)
    constexpr uint8_t SPLIT_NR14    = 0xAA;   // == the pixel's palette RGB8
    constexpr uint8_t BANK          = 9;
    constexpr uint8_t IDX           = 0x60;
    constexpr int      SPLIT_ROW    = 100;
    constexpr int      SRC_X        = 5;

    Layer2& l2 = emu.layer2();
    l2.set_enabled(true);
    l2.set_control(0x00);              // resolution 0 = 256x192 8bpp
    l2.set_active_bank(BANK);
    l2.set_clip_x1(0); l2.set_clip_x2(255);
    l2.set_clip_y1(0); l2.set_clip_y2(191);

    PaletteManager& pal = emu.palette();
    set_l2_palette(pal, IDX, SPLIT_NR14);   // palette entry's RRRGGGBB == SPLIT_NR14
    const uint32_t argb_opaque = pal.layer2_colour(IDX);

    Ram& ram = emu.ram();
    for (int y = 0; y < 192; ++y)
        ram.write(l2_phys_addr(BANK, l2_addr_256x192(SRC_X, y), true), IDX);

    Renderer& r = emu.renderer();
    r.set_transparent_rgb(BASELINE_NR14);

    begin_frame(emu);   // init_transparent_rgb_per_line() -> every row = BASELINE_NR14

    // Simulate the raster walking the frame with NR 0x14 changing at
    // SPLIT_ROW — the snapshot half of what Emulator::on_scanline does
    // (emulator.cpp:7648 snapshot_transparent_rgb_for_line(prev_fb_row)).
    for (int row = 0; row < SPLIT_ROW; ++row) r.snapshot_transparent_rgb_for_line(row);
    r.set_transparent_rgb(SPLIT_NR14);
    for (int row = SPLIT_ROW; row < Renderer::FB_HEIGHT; ++row)
        r.snapshot_transparent_rgb_for_line(row);

    QImage img = render_view(emu, VideoLayerView::Layer::LAYER2_ACTIVE,
                             Renderer::FB_HEIGHT - 1);
    const int cell = l2_cell_256(SRC_X);

    check("DVP-21a",
          "rows above the NR 0x14 split still show the opaque L2 pixel "
          "(pre-write snapshot BASELINE_NR14 != pixel RGB, VHDL 7121)",
          px(img, cell, SPLIT_ROW - 1) == argb_opaque,
          fmt("row%d=0x%08X want=0x%08X", SPLIT_ROW - 1,
              px(img, cell, SPLIT_ROW - 1), argb_opaque));

    check("DVP-21",
          "the row the NR 0x14 write landed on, and every row below it, "
          "already shows the pixel as transparent (checkerboard) — the "
          "view reads the PER-LINE snapshot, not the live NR 0x14 register",
          px(img, cell, SPLIT_ROW) == checker_at(SPLIT_ROW, cell)
              && px(img, cell, Renderer::FB_HEIGHT - 1)
                     == checker_at(Renderer::FB_HEIGHT - 1, cell),
          fmt("row%d=0x%08X (want checker 0x%08X) row%d=0x%08X (want "
              "checker 0x%08X)",
              SPLIT_ROW, px(img, cell, SPLIT_ROW),
              checker_at(SPLIT_ROW, cell), Renderer::FB_HEIGHT - 1,
              px(img, cell, Renderer::FB_HEIGHT - 1),
              checker_at(Renderer::FB_HEIGHT - 1, cell)));

    // State preservation, mirroring DVP-19a.
    const uint8_t live_before = r.transparent_rgb();
    for (int i = 0; i < 3; ++i)
        (void)render_view(emu, VideoLayerView::Layer::LAYER2_ACTIVE,
                          Renderer::FB_HEIGHT - 1);
    check("DVP-21b",
          "...and rendering it leaves the live NR 0x14 and its per-line "
          "snapshots alone",
          r.transparent_rgb() == live_before
              && r.transparent_rgb_for_line(SPLIT_ROW - 1) == BASELINE_NR14
              && r.transparent_rgb_for_line(SPLIT_ROW) == SPLIT_NR14,
          fmt("live 0x%02X->0x%02X | line%d=0x%02X line%d=0x%02X",
              live_before, r.transparent_rgb(),
              SPLIT_ROW - 1, r.transparent_rgb_for_line(SPLIT_ROW - 1),
              SPLIT_ROW, r.transparent_rgb_for_line(SPLIT_ROW)));
}

// ── DVP-03/04: ULA views must pin their bank, not follow port 0x7FFD ──

static void test_ula_bank_views(Emulator& emu) {
    set_group("DVP-ULA-BANK");

    constexpr int SCREEN_ROW = 0;

    Layer2& l2 = emu.layer2();
    l2.set_enabled(false);              // keep Layer 2 out of the way

    Ula& ula = emu.ula();
    ula.set_ula_enabled(true);
    ula.set_screen_mode(0x00);          // Timex STANDARD
    ula.set_clip_x1(0); ula.set_clip_x2(255);
    ula.set_clip_y1(0); ula.set_clip_y2(191);

    // Bank 5: pixel byte 0xFF (all ink) with attr ink=2 (red), paper=0.
    // Bank 7: pixel byte 0x00 (all paper) with attr ink=0, paper=6 (yellow).
    // The two banks therefore paint visibly different colours for the same
    // cell, and either bank showing up in the wrong view is unmissable.
    const uint16_t poff = 0;                          // screen_row 0, col 0
    const uint16_t attr = static_cast<uint16_t>(0x5800);

    uint8_t* b5 = emu.mmu().bank5_vram();
    uint8_t* b7 = emu.mmu().bank7_bram();
    for (int i = 0; i < 32; ++i) {
        b5[(0x4000 + poff + i) & 0x3FFF] = 0xFF;      // ink everywhere
        b5[(attr + i) & 0x3FFF]          = 0x02;      // ink 2 (red), paper 0
        b7[(0x4000 + poff + i) & 0x1FFF] = 0x00;      // paper everywhere
        b7[(attr + i) & 0x1FFF]          = 0x30;      // ink 0, paper 6 (yellow)
    }

    PaletteManager& pal = emu.palette();
    // std-ULA encoder: ink n → palette index n; paper n → index 0x10 + n
    // (VHDL zxula.vhd:543-553).  Read the ARGB the renderer will emit.
    const bool     pbank      = ula.get_active_ula_palette();
    const uint32_t argb_bank5 = pal.ula_colour(pbank, 0x02);        // ink 2
    const uint32_t argb_bank7 = pal.ula_colour(pbank, 0x10 + 0x06); // paper 6

    check("DVP-03a",
          "test premise: bank-5 and bank-7 fixtures produce different colours",
          argb_bank5 != argb_bank7,
          fmt("b5=0x%08X b7=0x%08X", argb_bank5, argb_bank7));

    const int row  = fb_row_of(SCREEN_ROW);
    const int cell = 64;   // first display cell (DISP_X)

    // --- Shadow screen SELECTED (port 0x7FFD bit 3 = 1) ---
    ula.set_shadow_screen_en(true);
    begin_frame(emu);
    {
        QImage img = render_view(emu, VideoLayerView::Layer::ULA_PRIMARY, row);
        const uint32_t got = px(img, cell, row);
        check("DVP-03",
              "ULA PRIMARY view shows bank 5 even while the shadow screen is selected",
              got == argb_bank5,
              fmt("got=0x%08X want(bank5)=0x%08X bank7=0x%08X",
                  got, argb_bank5, argb_bank7));
    }
    {
        QImage img = render_view(emu, VideoLayerView::Layer::ULA_SHADOW, row);
        const uint32_t got = px(img, cell, row);
        check("DVP-04a",
              "ULA SHADOW view shows bank 7 while the shadow screen is selected",
              got == argb_bank7,
              fmt("got=0x%08X want(bank7)=0x%08X", got, argb_bank7));
    }
    check("DVP-04c",
          "rendering the debug views restored the live bank-7 selector",
          ula.vram_bank7(),
          fmt("vram_bank7=%d (want 1)", int(ula.vram_bank7())));

    // --- Primary screen SELECTED (port 0x7FFD bit 3 = 0) ---
    ula.set_shadow_screen_en(false);
    begin_frame(emu);
    {
        QImage img = render_view(emu, VideoLayerView::Layer::ULA_SHADOW, row);
        const uint32_t got = px(img, cell, row);
        check("DVP-04",
              "ULA SHADOW view shows bank 7 even while the primary screen is selected",
              got == argb_bank7,
              fmt("got=0x%08X want(bank7)=0x%08X bank5=0x%08X",
                  got, argb_bank7, argb_bank5));
    }
    {
        QImage img = render_view(emu, VideoLayerView::Layer::ULA_PRIMARY, row);
        const uint32_t got = px(img, cell, row);
        check("DVP-03b",
              "ULA PRIMARY view shows bank 5 while the primary screen is selected",
              got == argb_bank5,
              fmt("got=0x%08X want(bank5)=0x%08X", got, argb_bank5));
    }
    check("DVP-04d",
          "rendering the debug views restored the live bank-5 selector",
          !ula.vram_bank7(),
          fmt("vram_bank7=%d (want 0)", int(ula.vram_bank7())));
}

// ── DVP-05: per-scanline palette replay ───────────────────────────────

static void test_per_line_palette_replay(Emulator& emu) {
    set_group("DVP-REPLAY");

    constexpr uint8_t BANK       = 9;
    constexpr uint8_t IDX        = 0x55;
    constexpr int     SPLIT_ROW  = 100;    // framebuffer row of the palette write
    constexpr int     SRC_X      = 3;

    Layer2& l2 = emu.layer2();
    l2.set_enabled(true);
    l2.set_control(0x00);
    l2.set_active_bank(BANK);
    l2.set_clip_x1(0); l2.set_clip_x2(255);
    l2.set_clip_y1(0); l2.set_clip_y2(191);

    PaletteManager& pal = emu.palette();
    pal.set_global_transparency(0xE3);

    // Every display row carries the same pixel index — only the PALETTE
    // changes mid-frame, so any per-row colour difference is replay, nothing
    // else.
    Ram& ram = emu.ram();
    for (int y = 0; y < 192; ++y) {
        ram.write(l2_phys_addr(BANK, l2_addr_256x192(SRC_X, y), true), IDX);
    }

    // Frame-start palette: colour A.  This becomes the replay baseline.
    set_l2_palette(pal, IDX, 0xFF);
    const uint32_t argb_a = pal.layer2_colour(IDX);
    begin_frame(emu);

    // Mid-frame write at SPLIT_ROW: colour B.  This is what Emulator's
    // on_scanline → palette.set_current_line(row) + the NR write handler do.
    pal.set_current_line(SPLIT_ROW);
    set_l2_palette(pal, IDX, 0x3F);
    const uint32_t argb_b = pal.layer2_colour(IDX);

    check("DVP-05a",
          "test premise: the two palette colours differ",
          argb_a != argb_b,
          fmt("A=0x%08X B=0x%08X", argb_a, argb_b));

    // Pause at the very bottom of the frame so every row is drawn.
    QImage img = render_view(emu, VideoLayerView::Layer::LAYER2_ACTIVE,
                             Renderer::FB_HEIGHT - 1);
    const int cell = l2_cell_256(SRC_X);

    const uint32_t above = px(img, cell, SPLIT_ROW - 1);
    const uint32_t at    = px(img, cell, SPLIT_ROW);
    const uint32_t below = px(img, cell, SPLIT_ROW + 20);

    check("DVP-05",
          "rows above a mid-frame palette write keep the frame-baseline colour",
          above == argb_a,
          fmt("row %d: got=0x%08X want=0x%08X", SPLIT_ROW - 1, above, argb_a));
    check("DVP-05b",
          "the row the palette write landed on already shows the new colour",
          at == argb_b,
          fmt("row %d: got=0x%08X want=0x%08X", SPLIT_ROW, at, argb_b));
    check("DVP-05c",
          "rows below the palette write keep the new colour",
          below == argb_b,
          fmt("row %d: got=0x%08X want=0x%08X", SPLIT_ROW + 20, below, argb_b));
}

// ── DVP-06: the panel must not perturb live emulator state ────────────
//
// KNOWN BLIND SPOT — read before trusting this group in isolation.  Every write
// this group makes is tagged at a VISIBLE scanline (0..FB_HEIGHT-1), and the
// panel's replay applies rows 0..FB_HEIGHT-1, so those entries are walked back
// to exactly where they started whether or not the panel calls
// replay_restore().  These rows therefore CANNOT fail if the panel drops the
// final flush_remaining_changes(): deleting replay_restore() from
// VideoLayerView::render_to_image leaves this whole group green (verified by
// mutation, Task 36).  Only entries tagged in VBLANK (line >= FB_HEIGHT) expose
// that bug — the tilemap_demo class of defect the renderer's own flush comment
// describes.  DVP-16c plants one and is what actually guards the invariant; it
// covers every Layer view, because replay_restore() has a single call site
// shared by all of them.  Keep DVP-16c alive.

static void test_no_state_mutation(Emulator& emu) {
    set_group("DVP-NOMUT");

    Layer2& l2 = emu.layer2();
    l2.set_enabled(true);
    l2.set_control(0x00);
    l2.set_active_bank(9);

    Tilemap& tm = emu.tilemap();
    tm.set_enabled(true);

    PaletteManager& pal = emu.palette();
    set_l2_palette(pal, 0x55, 0xFF);

    // Frame baseline, then a couple of mid-frame writes so the change logs are
    // non-empty (that is the interesting case: the replay walks them).
    tm.set_scroll_x_msb(0);
    tm.set_scroll_x_lsb(0);
    tm.set_scroll_y(0);
    begin_frame(emu);

    // The raster has walked rows 0..119 and the tilemap scrolled at row 60.
    for (int row = 0; row < 60; ++row) tm.snapshot_scroll_for_line(row);
    tm.set_scroll_x_lsb(48);
    tm.set_scroll_y(12);
    for (int row = 60; row < 120; ++row) tm.snapshot_scroll_for_line(row);

    l2.set_current_line(40);
    l2.set_scroll_x_lsb(17);
    pal.set_current_line(70);
    set_l2_palette(pal, 0x55, 0x3F);

    // Snapshot every piece of live state the replay touches.
    const uint16_t l2_scroll_x   = l2.scroll_x();
    const uint8_t  l2_bank       = l2.active_bank();
    const bool     l2_enabled    = l2.enabled();
    const uint32_t pal_l2_colour = pal.layer2_colour(0x55);
    const uint16_t tm_sx_30      = tm.scroll_x_for_line(30);   // before the split
    const uint16_t tm_sx_90      = tm.scroll_x_for_line(90);   // after the split
    const uint8_t  tm_sy_30      = tm.scroll_y_for_line(30);
    const bool     tm_enabled    = tm.enabled();

    // Refresh every view — each one runs a full rewind → replay → restore.
    for (auto layer : { VideoLayerView::Layer::COMPOSITE,
                        VideoLayerView::Layer::ULA_PRIMARY,
                        VideoLayerView::Layer::ULA_SHADOW,
                        VideoLayerView::Layer::LAYER2_ACTIVE,
                        VideoLayerView::Layer::LAYER2_SHADOW,
                        VideoLayerView::Layer::SPRITES,
                        VideoLayerView::Layer::TILEMAP,
                        VideoLayerView::Layer::BACKGROUND }) {
        (void)render_view(emu, layer, Renderer::FB_HEIGHT - 1);
    }

    check("DVP-06a", "panel refresh preserves the live Layer 2 scroll",
          l2.scroll_x() == l2_scroll_x,
          fmt("was=%u now=%u", l2_scroll_x, l2.scroll_x()));
    check("DVP-06b", "panel refresh preserves the live Layer 2 active bank",
          l2.active_bank() == l2_bank,
          fmt("was=%u now=%u", l2_bank, l2.active_bank()));
    check("DVP-06c", "panel refresh preserves the live Layer 2 enable",
          l2.enabled() == l2_enabled);
    check("DVP-06d", "panel refresh preserves the live palette",
          pal.layer2_colour(0x55) == pal_l2_colour,
          fmt("was=0x%08X now=0x%08X", pal_l2_colour, pal.layer2_colour(0x55)));
    check("DVP-06e", "panel refresh preserves the live tilemap enable",
          tm.enabled() == tm_enabled);
    // The one that used to be corrupted: Tilemap::render_scanline_debug wrote
    // the LIVE scroll into scroll_{x,y}_per_line_[y] for every row it drew, so
    // pausing mid-frame destroyed the already-captured snapshots and the frame
    // the compositor rendered on resume used the wrong scroll for its top half.
    check("DVP-06",
          "panel refresh does not clobber the tilemap per-line scroll snapshots",
          tm.scroll_x_for_line(30) == tm_sx_30
              && tm.scroll_y_for_line(30) == tm_sy_30
              && tm.scroll_x_for_line(90) == tm_sx_90,
          fmt("line30 x: was=%u now=%u | line30 y: was=%u now=%u | "
              "line90 x: was=%u now=%u",
              tm_sx_30, tm.scroll_x_for_line(30),
              tm_sy_30, tm.scroll_y_for_line(30),
              tm_sx_90, tm.scroll_x_for_line(90)));
}

// ── DVP-07: tilemap debug view honours the per-line scroll snapshots ──

static void test_tilemap_per_line_scroll(Emulator& emu) {
    set_group("DVP-TM-SCROLL");

    Tilemap& tm = emu.tilemap();
    tm.set_enabled(true);

    tm.set_scroll_x_msb(0);
    tm.set_scroll_x_lsb(0);
    tm.set_scroll_y(0);
    begin_frame(emu);

    // Raster walks the frame; scroll jumps at row 128 (a classic raster split).
    for (int row = 0; row < 128; ++row) tm.snapshot_scroll_for_line(row);
    tm.set_scroll_x_lsb(96);
    for (int row = 128; row < Renderer::FB_HEIGHT; ++row)
        tm.snapshot_scroll_for_line(row);

    // Pausing and refreshing must leave those snapshots alone AND use them.
    (void)render_view(emu, VideoLayerView::Layer::TILEMAP,
                      Renderer::FB_HEIGHT - 1);

    check("DVP-07",
          "tilemap debug render reads the per-line scroll split (0 above, 96 below)",
          tm.scroll_x_for_line(64) == 0 && tm.scroll_x_for_line(200) == 96,
          fmt("line64=%u (want 0) line200=%u (want 96)",
              tm.scroll_x_for_line(64), tm.scroll_x_for_line(200)));
}

// ── DVP-08/09: raw VC → framebuffer row ───────────────────────────────

static void test_fb_row_conversion(Emulator& emu) {
    set_group("DVP-FBROW");

    // G164v2 / Task 13: fb_row = raw_vc - vblank_top().
    const int vt_next = emu.video_timing().vblank_top();
    check("DVP-08a",
          "NEXT-family vblank_top is 32 (test premise)",
          vt_next == 32,
          fmt("vblank_top=%d", vt_next));

    check("DVP-08",
          "fb_row_for_vc subtracts vblank_top (raw VC 100 → fb row 68 on Next)",
          VideoPanel::fb_row_for_vc(100, 32) == 68,
          fmt("got=%d want=68", VideoPanel::fb_row_for_vc(100, 32)));
    check("DVP-08b",
          "raw VC inside the top vblank yields a negative fb row (nothing drawn)",
          VideoPanel::fb_row_for_vc(10, 32) < 0,
          fmt("got=%d", VideoPanel::fb_row_for_vc(10, 32)));
    check("DVP-08c",
          "the last visible raw VC (FB_HEIGHT-1 + vblank_top) maps to fb row 255",
          VideoPanel::fb_row_for_vc(Renderer::FB_HEIGHT - 1 + 32, 32)
              == Renderer::FB_HEIGHT - 1,
          fmt("got=%d",
              VideoPanel::fb_row_for_vc(Renderer::FB_HEIGHT - 1 + 32, 32)));
    check("DVP-08d",
          "raw VC past the last visible line clamps to fb row 255",
          VideoPanel::fb_row_for_vc(305, 32) == Renderer::FB_HEIGHT - 1,
          fmt("got=%d", VideoPanel::fb_row_for_vc(305, 32)));
    check("DVP-08e",
          "Pentagon vblank_top=48 and the 60Hz vblank_top=8 are honoured too",
          VideoPanel::fb_row_for_vc(100, 48) == 52
              && VideoPanel::fb_row_for_vc(100, 8) == 92,
          fmt("pentagon=%d sixty=%d",
              VideoPanel::fb_row_for_vc(100, 48),
              VideoPanel::fb_row_for_vc(100, 8)));

    // Rows past the raster must be flagged unrendered, rows at/before it drawn.
    constexpr uint32_t UNRENDERED_ARGB = 0xFF111111;
    Layer2& l2 = emu.layer2();
    l2.set_enabled(true);
    l2.set_control(0x00);
    l2.set_active_bank(9);
    begin_frame(emu);

    const int cut = 137;
    QImage img = render_view(emu, VideoLayerView::Layer::LAYER2_ACTIVE, cut);
    check("DVP-09",
          "row == the paused framebuffer row is drawn; row+1 is marked unrendered",
          px(img, 0, cut) != UNRENDERED_ARGB
              && px(img, 0, cut + 1) == UNRENDERED_ARGB,
          fmt("row%d=0x%08X row%d=0x%08X",
              cut, px(img, 0, cut), cut + 1, px(img, 0, cut + 1)));
}

// ── DVP-12: the ULA views apply the NR 0x1A clip window ───────────────
//
// Layer 2 / Tilemap / Sprites each apply their clip window inside their own
// render_scanline, so the debug views inherit it for free.  The ULA does not:
// in VHDL the clip is a compositor-stage signal (zxnext.vhd:7104 — ula_clipped
// is OR'd into ula_transparent), so Ula::render_scanline emits an UNCLIPPED
// line and Renderer::apply_ula_clip masks it.  The panel skipped that pass, so
// the ULA view was the only layer view showing content the compositor
// suppresses.

static void test_ula_clip_window(Emulator& emu) {
    set_group("DVP-ULA-CLIP");

    emu.layer2().set_enabled(false);

    Ula& ula = emu.ula();
    ula.set_ula_enabled(true);
    ula.set_screen_mode(0x00);
    ula.set_shadow_screen_en(false);

    // Paint the whole bank-5 screen with opaque ink.
    uint8_t* b5 = emu.mmu().bank5_vram();
    for (int i = 0; i < 0x1800; ++i)      b5[i] = 0xFF;   // pixels: all ink
    for (int i = 0x1800; i < 0x1B00; ++i) b5[i] = 0x02;   // attrs: ink 2 (red)

    // Clip to display columns 32..63 (source pixels) on every row.  Under G104
    // each source column covers two framebuffer cells, so the surviving band is
    // framebuffer cells DISP_X+64 .. DISP_X+127.
    ula.set_clip_x1(32);
    ula.set_clip_x2(63);
    ula.set_clip_y1(0);
    ula.set_clip_y2(191);

    begin_frame(emu);

    const int row = fb_row_of(20);
    QImage img = render_view(emu, VideoLayerView::Layer::ULA_PRIMARY, row);

    const uint32_t inside  = px(img, 64 + 2 * 40, row);   // src col 40 → kept
    const uint32_t outside = px(img, 64 + 2 * 10, row);   // src col 10 → clipped
    const uint32_t border  = px(img, 4, row);             // left border → clipped

    const bool     pbank    = ula.get_active_ula_palette();
    const uint32_t argb_ink = emu.palette().ula_colour(pbank, 0x02);

    check("DVP-12a",
          "ULA view keeps display cells INSIDE the NR 0x1A clip window",
          inside == argb_ink,
          fmt("got=0x%08X want=0x%08X", inside, argb_ink));
    check("DVP-12",
          "ULA view clips display cells OUTSIDE the NR 0x1A clip window",
          outside != argb_ink,
          fmt("got=0x%08X (must not be the ink colour 0x%08X)",
              outside, argb_ink));
    check("DVP-12b",
          "…and clips the border strips when clip_x1 > 0 / clip_x2 < 255",
          border != argb_ink,
          fmt("got=0x%08X", border));
}

// ── DVP-10/11: "Run to EOF" / "Run to EOSL" raster targets ────────────
//
// Same G164v2 defect, one level up in DebuggerManager: both slots treated the
// raw VC as a framebuffer row.
//   * on_run_to_eof targeted raw VC = FB_HEIGHT-1 = 255, which is framebuffer
//     row 223 on the Next — so "Run to EOF" stopped 32 rows short and the
//     video panel never showed a complete frame.
//   * on_run_to_eosl classified raw VC >= FB_HEIGHT as blanking, so for the
//     bottom border (raw VC 256..287 = framebuffer rows 224..255) it skipped
//     to the next frame instead of stepping one scanline.

static void test_run_to_targets() {
    set_group("DVP-RUNTO");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        check("DVP-10", "Emulator construction for run-to targets", false);
        return;
    }

    QMainWindow host;
    DebuggerManager mgr(&host, &emu);
    mgr.set_enabled(true);

    const auto& t   = emu.timing();
    const int   vbt = emu.video_timing().vblank_top();

    // --- Run to EOF ---
    emu.debug_state().pause();
    mgr.on_run_to_eof();

    const uint64_t frame_start = emu.current_frame_cycle();
    const uint64_t want_eof =
        frame_start
        + static_cast<uint64_t>(Renderer::FB_HEIGHT - 1 + vbt)
              * t.master_cycles_per_line
        + t.master_cycles_per_line / 2;
    const uint64_t got_eof = emu.debug_state().target_cycle();

    check("DVP-10",
          "Run to EOF targets the last VISIBLE scanline (raw VC = 255 + vblank_top)",
          got_eof == want_eof,
          fmt("got=%llu want=%llu (raw VC %d, not %d)",
              static_cast<unsigned long long>(got_eof),
              static_cast<unsigned long long>(want_eof),
              Renderer::FB_HEIGHT - 1 + vbt, Renderer::FB_HEIGHT - 1));

    // The stop cycle must map to framebuffer row FB_HEIGHT-1 — i.e. the panel,
    // fed through fb_row_for_vc, must consider EVERY row drawn.
    const int stop_vc =
        static_cast<int>((got_eof - frame_start) / t.master_cycles_per_line);
    check("DVP-10b",
          "…and that raw VC maps to framebuffer row FB_HEIGHT-1 (whole frame drawn)",
          VideoPanel::fb_row_for_vc(stop_vc, vbt) == Renderer::FB_HEIGHT - 1,
          fmt("stop raw VC=%d → fb_row=%d (want %d)",
              stop_vc, VideoPanel::fb_row_for_vc(stop_vc, vbt),
              Renderer::FB_HEIGHT - 1));

    // --- Run to EOSL from inside the bottom border ---
    // Park the clock in the bottom border (raw VC 260 = framebuffer row 228),
    // which the old code misclassified as blanking.
    constexpr int PARK_VC = 260;
    const uint64_t park_at = frame_start
        + static_cast<uint64_t>(PARK_VC) * t.master_cycles_per_line + 10;
    emu.clock().tick(static_cast<int>(park_at - emu.clock().get()));
    emu.debug_state().pause();
    mgr.on_run_to_eosl();

    const uint64_t want_eosl = frame_start
        + static_cast<uint64_t>(PARK_VC + 1) * t.master_cycles_per_line;
    const uint64_t got_eosl  = emu.debug_state().target_cycle();

    check("DVP-11",
          "Run to EOSL inside the bottom border steps ONE scanline, not to the next frame",
          got_eosl == want_eosl,
          fmt("got=%llu want=%llu (next-frame target would be %llu)",
              static_cast<unsigned long long>(got_eosl),
              static_cast<unsigned long long>(want_eosl),
              static_cast<unsigned long long>(frame_start
                  + t.master_cycles_per_frame)));

    check("DVP-11b",
          "…and raw VC 260 really is a VISIBLE framebuffer row (228), not blanking",
          VideoPanel::fb_row_for_vc(PARK_VC, vbt) >= 0
              && VideoPanel::fb_row_for_vc(PARK_VC, vbt) < Renderer::FB_HEIGHT,
          fmt("fb_row=%d", VideoPanel::fb_row_for_vc(PARK_VC, vbt)));
}

// ── DVP-13: the composite view IS the emulator's framebuffer ──────────
//
// The strongest statement of "do not hand-roll a second compositor": build a
// scene with all four layers live, let Renderer::render_frame draw it into the
// emulator's own framebuffer (that is literally what the emulator window
// shows), then render the panel's "All layers" view for the same frame and
// demand every one of the 640 × 256 pixels agree.

static void test_composite_matches_framebuffer(Emulator& emu) {
    set_group("DVP-COMPOSITE");

    constexpr uint8_t L2_BANK   = 9;
    constexpr uint8_t L2_IDX    = 0x40;
    constexpr uint8_t SPR_PAT   = 3;
    constexpr uint8_t SPR_IDX   = 0x77;
    constexpr uint8_t TM_PIXEL  = 0x03;
    constexpr int     TM_COL    = 2;
    constexpr int     TM_ROW    = 4;

    PaletteManager& pal = emu.palette();
    pal.set_global_transparency(0xE3);           // NR 0x14

    // --- ULA: whole bank-5 screen in ink 2 (red) ---
    Ula& ula = emu.ula();
    ula.set_ula_enabled(true);
    ula.set_screen_mode(0x00);
    ula.set_shadow_screen_en(false);
    ula.set_clip_x1(0); ula.set_clip_x2(255);
    ula.set_clip_y1(0); ula.set_clip_y2(191);

    uint8_t* b5 = emu.mmu().bank5_vram();
    for (int i = 0; i < 0x1800; ++i)      b5[i] = 0xFF;   // all ink
    for (int i = 0x1800; i < 0x1B00; ++i) b5[i] = 0x02;   // ink 2, paper 0, no flash

    // --- Layer 2: a block of pixels in the top-left of the display ---
    Layer2& l2 = emu.layer2();
    l2.set_enabled(true);
    l2.set_control(0x00);                        // 256×192, 8bpp
    l2.set_active_bank(L2_BANK);
    l2.set_clip_x1(0); l2.set_clip_x2(255);
    l2.set_clip_y1(0); l2.set_clip_y2(191);
    set_l2_palette(pal, 0x00, 0xE3);             // index 0 == NR 0x14 → transparent
    set_l2_palette(pal, L2_IDX, 0xFF);           // white
    Ram& ram = emu.ram();
    for (int y = 20; y < 40; ++y)
        for (int x = 20; x < 60; ++x)
            ram.write(l2_phys_addr(L2_BANK, l2_addr_256x192(x, y), true), L2_IDX);

    // --- Tilemap: one opaque tile; every other cell transparent ---
    Tilemap& tm = emu.tilemap();
    tm.set_map_base(TM_MAP_BASE);
    tm.set_def_base(TM_DEF_BASE);
    tm.set_control(0x80);                        // enable, 40-col, 2-byte map entries
    fill_tile(b5, 0, 0x0F);                      // tile 0 == NR 0x4C transparency index
    fill_tile(b5, 1, TM_PIXEL);
    write_tile_map(b5, TM_COL, TM_ROW, 1, 0x00);
    paint_palette(pal, 0x30, TM_PIXEL, 0x1C);    // tilemap palette: green

    // --- Sprites ---
    SpriteEngine& spr = emu.sprites();
    paint_palette(pal, 0x20, SPR_IDX, 0xA5);     // sprite palette: purple
    put_solid_sprite(spr, SPR_PAT, SPR_IDX, /*x=*/100, /*y=*/100);

    Renderer& r = emu.renderer();
    r.set_sprite_en(true);                       // NR 0x15 b0
    r.set_tm_enabled(true);                      // NR 0x6B b7 (compositor mirror)
    r.set_transparent_rgb(0xE3);                 // NR 0x14
    r.set_fallback_colour(0x13);                 // NR 0x4A — the sonic.nex blue
    r.set_layer_priority(0);                     // SLU

    begin_frame(emu);

    // Draw the frame exactly as the emulator does — this buffer IS the picture
    // in the emulator window.
    r.render_frame(emu.get_framebuffer(), emu.mmu(), ram, pal, l2, &spr, &tm);
    std::vector<uint32_t> want(
        emu.get_framebuffer(),
        emu.get_framebuffer() + Renderer::FB_WIDTH * Renderer::FB_HEIGHT);

    // Premise: all four layers really did reach the framebuffer.  Without this
    // the pixel-for-pixel comparison below could pass on an empty scene.
    const uint32_t argb_ula = pal.ula_colour(ula.get_active_ula_palette(), 0x02);
    const uint32_t argb_l2  = pal.layer2_colour(L2_IDX);
    const uint32_t argb_tm  = pal.tilemap_colour(TM_PIXEL);
    const uint32_t argb_spr = pal.sprite_colour(SPR_IDX);
    check("DVP-13a",
          "test premise: ULA, Layer 2, tilemap and sprite pixels all reach the framebuffer",
          contains(want.data(), argb_ula) && contains(want.data(), argb_l2)
              && contains(want.data(), argb_tm) && contains(want.data(), argb_spr),
          fmt("ula=%d l2=%d tm=%d spr=%d (0x%08X/0x%08X/0x%08X/0x%08X)",
              int(contains(want.data(), argb_ula)), int(contains(want.data(), argb_l2)),
              int(contains(want.data(), argb_tm)), int(contains(want.data(), argb_spr)),
              argb_ula, argb_l2, argb_tm, argb_spr));

    // The panel, paused at the last framebuffer row → every row drawn.
    QImage img = render_view(emu, VideoLayerView::Layer::COMPOSITE,
                             Renderer::FB_HEIGHT - 1);

    check("DVP-13b",
          "the composite view is 640 cells wide (Renderer::FB_WIDTH)",
          img.width() == Renderer::FB_WIDTH && img.height() == Renderer::FB_HEIGHT,
          fmt("%dx%d", img.width(), img.height()));

    int  diffs = 0;
    int  fx = -1, fy = -1;
    uint32_t fgot = 0, fwant = 0;
    for (int y = 0; y < Renderer::FB_HEIGHT; ++y) {
        for (int x = 0; x < Renderer::FB_WIDTH; ++x) {
            const uint32_t got = px(img, x, y);
            const uint32_t exp = want[y * Renderer::FB_WIDTH + x];
            if (got != exp) {
                if (diffs == 0) { fx = x; fy = y; fgot = got; fwant = exp; }
                ++diffs;
            }
        }
    }
    check("DVP-13",
          "\"All layers\" view is pixel-for-pixel the emulator's own framebuffer",
          diffs == 0,
          fmt("%d differing pixels; first at (%d,%d) got=0x%08X want=0x%08X",
              diffs, fx, fy, fgot, fwant));
}

// ── DVP-14: the NR 0x4A fallback colour — the sonic.nex case ──────────
//
// sonic.nex writes NR 0x68 = 0x80 (ULA off), leaves Layer 2 empty, and writes
// NR 0x4A = 0x13.  Its whole sky is that fallback colour — emitted by the
// compositor wherever EVERY layer is transparent (VHDL zxnext.vhd:7218-7352:
// `result` starts at the fallback and is only overwritten by an opaque layer).
// It therefore belongs to no layer and NO per-layer view can ever show it,
// which is exactly why the panels did not visibly add up to the picture on
// screen.  Only the composite view can.

static void test_fallback_colour_sonic_case(Emulator& emu) {
    set_group("DVP-FALLBACK");

    // NR 0x4A = 0x13 → RRRGGGBB r3=0 g3=4 b2=3 → #0092FF (Renderer::rrrgggbb_to_argb).
    constexpr uint8_t  NR4A     = 0x13;
    const     uint32_t FALLBACK = Renderer::rrrgggbb_to_argb(NR4A);

    constexpr uint8_t SPR_PAT  = 5;
    constexpr uint8_t SPR_IDX  = 0x77;
    constexpr uint8_t TM_PIXEL = 0x03;
    constexpr int     TM_COL   = 2;
    constexpr int     TM_ROW   = 4;

    check("DVP-14a",
          "test premise: NR 0x4A = 0x13 really is #0092FF (sonic.nex's sky)",
          FALLBACK == 0xFF0092FFu,
          fmt("got=0x%08X", FALLBACK));

    PaletteManager& pal = emu.palette();
    pal.set_global_transparency(0xE3);

    // ULA OFF (NR 0x68 b7 = 1) — as sonic.nex leaves it.
    emu.ula().set_ula_enabled(false);
    // Layer 2 empty.
    emu.layer2().set_enabled(false);
    set_l2_palette(pal, 0x00, 0xE3);

    // Tilemap: one opaque tile, everything else the transparency index.
    uint8_t* b5 = emu.mmu().bank5_vram();
    Tilemap& tm = emu.tilemap();
    tm.set_map_base(TM_MAP_BASE);
    tm.set_def_base(TM_DEF_BASE);
    tm.set_control(0x80);
    fill_tile(b5, 0, 0x0F);
    fill_tile(b5, 1, TM_PIXEL);
    write_tile_map(b5, TM_COL, TM_ROW, 1, 0x00);
    paint_palette(pal, 0x30, TM_PIXEL, 0x1C);

    // One sprite, well clear of the tile.
    SpriteEngine& spr = emu.sprites();
    paint_palette(pal, 0x20, SPR_IDX, 0xA5);
    put_solid_sprite(spr, SPR_PAT, SPR_IDX, /*x=*/200, /*y=*/200);

    Renderer& r = emu.renderer();
    r.set_sprite_en(true);
    r.set_tm_enabled(true);
    r.set_transparent_rgb(0xE3);
    r.set_fallback_colour(NR4A);
    r.set_layer_priority(0);

    begin_frame(emu);

    QImage comp = render_view(emu, VideoLayerView::Layer::COMPOSITE,
                              Renderer::FB_HEIGHT - 1);

    // 40-col tilemap: tile column C covers source x 8C..8C+7, pixel-doubled to
    // framebuffer cells 16C..16C+15; tile row R covers framebuffer rows 8R..8R+7.
    const int tm_x = 16 * TM_COL + 4;
    const int tm_y = 8 * TM_ROW + 4;
    // A cell far from both the tile and the sprite: nothing but fallback there.
    const int sky_x = 500, sky_y = 20;

    const uint32_t argb_tm  = pal.tilemap_colour(TM_PIXEL);
    const uint32_t argb_spr = pal.sprite_colour(SPR_IDX);

    check("DVP-14",
          "composite shows the NR 0x4A fallback where EVERY layer is transparent",
          px(comp, sky_x, sky_y) == FALLBACK,
          fmt("(%d,%d) got=0x%08X want=0x%08X", sky_x, sky_y,
              px(comp, sky_x, sky_y), FALLBACK));

    check("DVP-14b",
          "…and an opaque tilemap pixel still composites over it",
          px(comp, tm_x, tm_y) == argb_tm,
          fmt("(%d,%d) got=0x%08X want=0x%08X", tm_x, tm_y,
              px(comp, tm_x, tm_y), argb_tm));

    check("DVP-14c",
          "…and the sprite composites over it too",
          contains(comp, argb_spr),
          fmt("sprite colour 0x%08X not found in the composite", argb_spr));

    // The point of the whole task: the fallback belongs to NO layer, so no
    // per-layer view can show it.  If any of these views did contain it, the
    // composite would be redundant — and the user's confusion would be a bug
    // rather than a missing view.
    bool in_any_layer = false;
    for (auto layer : { VideoLayerView::Layer::ULA_PRIMARY,
                        VideoLayerView::Layer::ULA_SHADOW,
                        VideoLayerView::Layer::LAYER2_ACTIVE,
                        VideoLayerView::Layer::LAYER2_SHADOW,
                        VideoLayerView::Layer::SPRITES,
                        VideoLayerView::Layer::TILEMAP }) {
        QImage v = render_view(emu, layer, Renderer::FB_HEIGHT - 1);
        if (contains(v, FALLBACK)) in_any_layer = true;
    }
    check("DVP-14d",
          "the fallback colour appears in NO per-layer view — only the composite can show it",
          !in_any_layer && contains(comp, FALLBACK),
          fmt("in_a_layer_view=%d in_composite=%d",
              int(in_any_layer), int(contains(comp, FALLBACK))));
}

// ── DVP-15: the composite honours the raster cut-off ──────────────────

static void test_composite_raster_cutoff(Emulator& emu) {
    set_group("DVP-COMP-RASTER");

    const uint32_t FALLBACK = Renderer::rrrgggbb_to_argb(0x13);

    emu.ula().set_ula_enabled(false);
    emu.layer2().set_enabled(false);

    Renderer& r = emu.renderer();
    r.set_fallback_colour(0x13);
    r.set_layer_priority(0);

    begin_frame(emu);

    constexpr int CUT = 137;
    QImage img = render_view(emu, VideoLayerView::Layer::COMPOSITE, CUT);

    check("DVP-15",
          "composite draws the paused row and marks the row below it unrendered",
          px(img, 0, CUT) == FALLBACK && px(img, 0, CUT + 1) == UNRENDERED,
          fmt("row%d=0x%08X (want 0x%08X) row%d=0x%08X (want 0x%08X)",
              CUT, px(img, 0, CUT), FALLBACK,
              CUT + 1, px(img, 0, CUT + 1), UNRENDERED));
    check("DVP-15a",
          "…and every row below the raster is the unrendered placeholder",
          px(img, 320, Renderer::FB_HEIGHT - 1) == UNRENDERED
              && px(img, 0, 0) == FALLBACK,
          fmt("last=0x%08X first=0x%08X",
              px(img, 320, Renderer::FB_HEIGHT - 1), px(img, 0, 0)));
}

// ── DVP-16: the composite render must not perturb the machine ─────────

static void test_composite_no_state_mutation(Emulator& emu) {
    set_group("DVP-COMP-NOMUT");

    constexpr uint8_t L2_BANK = 9;

    PaletteManager& pal = emu.palette();
    Layer2&         l2  = emu.layer2();
    Tilemap&        tm  = emu.tilemap();
    Ula&            ula = emu.ula();
    Renderer&       r   = emu.renderer();

    l2.set_enabled(true);
    l2.set_control(0x00);
    l2.set_active_bank(L2_BANK);
    tm.set_map_base(TM_MAP_BASE);
    tm.set_def_base(TM_DEF_BASE);
    tm.set_control(0x80);
    ula.set_ula_enabled(true);
    r.set_sprite_en(true);
    r.set_tm_enabled(true);
    r.set_fallback_colour(0x13);
    set_l2_palette(pal, 0x55, 0xFF);
    // Frame-baseline values for the two registers the VBLANK writes below move.
    set_l2_palette(pal, 0x66, 0x11);
    l2.set_scroll_y(0);

    tm.set_scroll_x_lsb(0);
    tm.set_scroll_y(0);
    begin_frame(emu);

    // Mid-frame writes, so every change log the replay walks is non-empty —
    // that is the case where a sloppy replay would leave the machine off.
    for (int row = 0; row < 60; ++row) tm.snapshot_scroll_for_line(row);
    tm.set_scroll_x_lsb(48);
    tm.set_scroll_y(12);
    for (int row = 60; row < 120; ++row) tm.snapshot_scroll_for_line(row);

    l2.set_current_line(40);
    l2.set_scroll_x_lsb(17);
    pal.set_current_line(70);
    set_l2_palette(pal, 0x55, 0x3F);
    ula.set_current_scroll_line(90);
    ula.set_ula_scroll_x_coarse(7);

    // VBLANK writes (line >= FB_HEIGHT).  These are the ones that pin the
    // RESTORE half of the round trip: rewind_to_baseline() undoes the direct
    // live mutation, and apply_changes_for_line(0..255) never reaches an entry
    // tagged at line 300 — only flush_remaining_changes() replays it.  A panel
    // that rewinds and replays but forgets to flush therefore silently reverts
    // every vblank write to its frame baseline, and the NEXT frame's
    // start_frame() snapshots that corrupted state as the new baseline.  (This
    // is the tilemap_demo class of bug the renderer's own flush comment
    // describes — at NR 0x07 >= 0x02 the whole setup lands in vblank.)  Without
    // a vblank-tagged entry a state-preservation row cannot see the defect at
    // all: rows 0..255 happen to replay everything else back to where it was.
    constexpr int VBLANK_LINE = Renderer::FB_HEIGHT + 44;   // 300
    pal.set_current_line(VBLANK_LINE);
    set_l2_palette(pal, 0x66, 0x77);
    l2.set_current_line(VBLANK_LINE);
    l2.set_scroll_y(123);

    // Snapshot everything the composite path touches.
    const uint16_t l2_scroll_x   = l2.scroll_x();
    const uint8_t  l2_scroll_y   = l2.scroll_y();
    const uint8_t  l2_bank       = l2.active_bank();
    const uint32_t pal_colour    = pal.layer2_colour(0x55);
    const uint32_t pal_vblank    = pal.layer2_colour(0x66);
    const uint16_t tm_sx_30      = tm.scroll_x_for_line(30);
    const uint16_t tm_sx_90      = tm.scroll_x_for_line(90);
    const uint8_t  tm_sy_30      = tm.scroll_y_for_line(30);
    const uint8_t  ula_scroll_x  = ula.get_ula_scroll_x_coarse();
    const bool     ula_enabled   = ula.ula_enabled();
    const uint8_t  fallback      = r.fallback_colour();
    const uint8_t  priority      = r.layer_priority();
    const bool     sprite_en     = r.sprite_en();
    const bool     tm_en         = tm.enabled();

    check("DVP-16b",
          "test premise: the vblank writes really did move the live registers",
          pal_vblank == pal.layer2_colour(0x66) && pal_vblank != 0
              && l2_scroll_y == 123,
          fmt("pal(0x66)=0x%08X l2 scroll_y=%u", pal_vblank, l2_scroll_y));

    // Render the composite view repeatedly — each pass is a full rewind →
    // replay → restore round trip; N passes must be as harmless as one.
    for (int i = 0; i < 3; ++i)
        (void)render_view(emu, VideoLayerView::Layer::COMPOSITE,
                          Renderer::FB_HEIGHT - 1);

    check("DVP-16",
          "compositing for the panel leaves every live video register untouched",
          l2.scroll_x() == l2_scroll_x && l2.active_bank() == l2_bank
              && pal.layer2_colour(0x55) == pal_colour
              && ula.get_ula_scroll_x_coarse() == ula_scroll_x
              && ula.ula_enabled() == ula_enabled
              && r.fallback_colour() == fallback
              && r.layer_priority() == priority
              && r.sprite_en() == sprite_en
              && tm.enabled() == tm_en,
          fmt("l2sx %u→%u l2bank %u→%u pal 0x%08X→0x%08X ulasx %u→%u",
              l2_scroll_x, l2.scroll_x(), l2_bank, l2.active_bank(),
              pal_colour, pal.layer2_colour(0x55),
              ula_scroll_x, ula.get_ula_scroll_x_coarse()));

    check("DVP-16c",
          "…including the VBLANK-tagged writes, which only the final flush replays",
          pal.layer2_colour(0x66) == pal_vblank
              && l2.scroll_y() == l2_scroll_y,
          fmt("pal(0x66) 0x%08X→0x%08X  l2 scroll_y %u→%u",
              pal_vblank, pal.layer2_colour(0x66),
              l2_scroll_y, l2.scroll_y()));

    check("DVP-16a",
          "…and does not clobber the tilemap per-line scroll snapshots",
          tm.scroll_x_for_line(30) == tm_sx_30
              && tm.scroll_y_for_line(30) == tm_sy_30
              && tm.scroll_x_for_line(90) == tm_sx_90,
          fmt("l30x %u→%u l30y %u→%u l90x %u→%u",
              tm_sx_30, tm.scroll_x_for_line(30),
              tm_sy_30, tm.scroll_y_for_line(30),
              tm_sx_90, tm.scroll_x_for_line(90)));
}

// ── DVP-18/19: the Background (NR 0x4A fallback) view ─────────────────
//
// The sequel to DVP-14.  The fallback colour is on screen but in no layer, so
// the Background tab makes it directly inspectable.
//
// SCOPE OF THIS GROUP: it drives `Renderer::snapshot_fallback_for_line()`
// DIRECTLY — i.e. it simulates the per-line snapshot mechanism that
// Emulator::on_scanline performs, without running the emulator.  It therefore
// proves that the VIEW reads the per-line array rather than the live register,
// and nothing about how that array gets filled.  The real Copper → NextReg →
// snapshot integration is proved by DVP-18b below, which assembles actual
// Copper bytecode and runs a frame (the UDIS-02 idiom from
// test/compositor/compositor_integration_test.cpp).  Do not let this group's
// convenience pokes be mistaken for that.

static void test_background_view(Emulator& emu) {
    set_group("DVP-BACKGROUND");

    constexpr uint8_t SKY   = 0x13;   // #0092FF — sonic.nex's sky
    constexpr uint8_t GROUND = 0xE0;  // a clearly different colour
    constexpr int     SPLIT = 100;    // the row the value changes on

    const uint32_t argb_sky    = Renderer::rrrgggbb_to_argb(SKY);
    const uint32_t argb_ground = Renderer::rrrgggbb_to_argb(GROUND);

    check("DVP-18a",
          "test premise: the two fallback colours differ",
          argb_sky != argb_ground,
          fmt("sky=0x%08X ground=0x%08X", argb_sky, argb_ground));

    // Nothing opaque anywhere: the fallback is the entire picture.
    emu.ula().set_ula_enabled(false);
    emu.layer2().set_enabled(false);

    Renderer& r = emu.renderer();
    r.set_fallback_colour(SKY);
    r.set_layer_priority(0);

    begin_frame(emu);   // init_fallback_per_line() → every row = SKY

    // Simulate the raster walking the frame with NR 0x4A changing at SPLIT —
    // the snapshot half of what Emulator::on_scanline does.  (The Z80/Copper
    // half is DVP-18b's job.)
    for (int row = 0; row < SPLIT; ++row) r.snapshot_fallback_for_line(row);
    r.set_fallback_colour(GROUND);
    for (int row = SPLIT; row < Renderer::FB_HEIGHT; ++row)
        r.snapshot_fallback_for_line(row);

    QImage bg = render_view(emu, VideoLayerView::Layer::BACKGROUND,
                            Renderer::FB_HEIGHT - 1);

    check("DVP-18",
          "Background view shows the NR 0x4A fallback colour",
          px(bg, 300, 20) == argb_sky,
          fmt("(300,20) got=0x%08X want=0x%08X", px(bg, 300, 20), argb_sky));

    // The view reads fallback_per_line_[row], not the live register: a flat
    // swatch of Renderer::fallback_colour() would paint every row GROUND (the
    // frame's LAST value), so the rows above the split would be wrong.
    check("DVP-18d",
          "…the view reads the PER-LINE snapshot, not the live NR 0x4A register",
          px(bg, 0, SPLIT - 1) == argb_sky
              && px(bg, 0, SPLIT) == argb_ground
              && px(bg, 639, Renderer::FB_HEIGHT - 1) == argb_ground,
          fmt("row%d=0x%08X (want sky 0x%08X) row%d=0x%08X (want ground 0x%08X)",
              SPLIT - 1, px(bg, 0, SPLIT - 1), argb_sky,
              SPLIT, px(bg, 0, SPLIT), argb_ground));

    check("DVP-19",
          "Background view honours the raster cut-off like every other view",
          px(render_view(emu, VideoLayerView::Layer::BACKGROUND, 137), 0, 137)
                  == argb_ground
              && px(render_view(emu, VideoLayerView::Layer::BACKGROUND, 137),
                    0, 138) == UNRENDERED,
          fmt("row137=0x%08X row138=0x%08X",
              px(render_view(emu, VideoLayerView::Layer::BACKGROUND, 137), 0, 137),
              px(render_view(emu, VideoLayerView::Layer::BACKGROUND, 137), 0, 138)));

    // State preservation: the Background view runs the same replay round trip.
    const uint8_t live_nr4a = r.fallback_colour();
    for (int i = 0; i < 3; ++i)
        (void)render_view(emu, VideoLayerView::Layer::BACKGROUND,
                          Renderer::FB_HEIGHT - 1);
    check("DVP-19a",
          "…and rendering it leaves the live NR 0x4A and its per-line snapshots alone",
          r.fallback_colour() == live_nr4a
              && r.fallback_for_line(SPLIT - 1) == SKY
              && r.fallback_for_line(SPLIT) == GROUND,
          fmt("live 0x%02X→0x%02X | line%d=0x%02X line%d=0x%02X",
              live_nr4a, r.fallback_colour(),
              SPLIT - 1, r.fallback_for_line(SPLIT - 1),
              SPLIT, r.fallback_for_line(SPLIT)));
}

// ── DVP-18b/18c: a REAL Copper program moves NR 0x4A mid-frame ────────
//
// The reason the Background view is per-scanline at all is that the Copper can
// MOVE NR 0x4A partway down the raster — that is what fallback_per_line_[]
// exists for.  A test that pokes snapshot_fallback_for_line() by hand proves
// nothing about that path, so this row drives the genuine one end to end:
//
//   Z80 parked on HALT → Copper bytecode uploaded via NR 0x60/0x61/0x62 →
//   Emulator::run_frame() ticks Copper::execute at every raster edge →
//   the MOVE writes NR 0x4A through the real NextReg dispatch →
//   Emulator::on_scanline snapshots it into fallback_per_line_[row] →
//   the panel's Background view shows the band split.
//
// Copper program (copper.vhd:20-43; src/peripheral/copper.cpp:10-43):
//   [0] WAIT vpos=100, hpos=0   = 0x8000 | 100
//   [1] MOVE NR 0x4A, GROUND    = (0x4A << 8) | 0xE0     (MSB clear)
//   [2] WAIT vpos=511           = 0x8000 | 511            → park (HALT)
// Upload idiom + NR 0x62 = 0xC0 (mode 11: reset PC each vsync, run) are copied
// verbatim from UDIS-02 in test/compositor/compositor_integration_test.cpp,
// which is the established proof of the Copper → NextReg dispatch.
//
// VHDL: zxnext.vhd:6809 (Copper scheduling from cvc/hcount), :7214 (the
// compositor's per-line fallback_rgb_2), copper.vhd:85-106 (WAIT/MOVE).
//
// Row choice mirrors UDIS-02: the exact framebuffer row the MOVE lands on may
// be DISP_Y+100 or DISP_Y+101 depending on where inside the line the raster
// edge falls, so we sample one row before and one row after with a cushion —
// the same "acceptable simplification" UDIS-02 documents.

static void test_background_copper() {
    set_group("DVP-BG-COPPER");

    constexpr uint8_t SKY    = 0x13;   // #0092FF — sonic.nex's sky
    constexpr uint8_t GROUND = 0xE0;   // bright red
    constexpr int     WAIT_LINE = 100;

    const uint32_t argb_sky    = Renderer::rrrgggbb_to_argb(SKY);
    const uint32_t argb_ground = Renderer::rrrgggbb_to_argb(GROUND);

    Emulator emu;
    if (!build_next_emulator(emu)) {
        check("DVP-18b", "Emulator construction for the Copper fixture", false);
        return;
    }

    // Park the Z80 on a HALT so the boot ROM cannot issue NR writes of its own
    // — every NR 0x4A change this frame must come from the Copper.
    emu.mmu().write(0x8000, 0x76);              // HALT
    auto regs = emu.cpu().get_registers();
    regs.PC = 0x8000; regs.SP = 0xFFFD; regs.IFF1 = 0; regs.IFF2 = 0;
    emu.cpu().set_registers(regs);

    // Every NR below goes through the REAL port dispatch (OUT 0x243B/0x253B).
    auto nr = [&emu](uint8_t reg, uint8_t val) {
        emu.port().out(0x243B, reg);
        emu.port().out(0x253B, val);
    };

    nr(0x4A, SKY);      // frame-start fallback
    nr(0x68, 0x80);     // ULA off  → nothing opaque anywhere…
    nr(0x69, 0x00);     // Layer 2 off
    nr(0x6B, 0x00);     // Tilemap off
    nr(0x15, 0x00);     // Sprites off, priority SLU
    //                     …so the whole frame IS the fallback colour.

    const uint16_t WAIT_V   = static_cast<uint16_t>(0x8000u | WAIT_LINE);
    const uint16_t MOVE_4A  = static_cast<uint16_t>((0x4Au << 8) | GROUND);
    const uint16_t WAIT_END = static_cast<uint16_t>(0x8000u | 511u);

    nr(0x61, 0x00);     // copper write addr low  = 0
    nr(0x62, 0x00);     // copper write addr high = 0, mode 00 (stopped)
    for (uint16_t insn : {WAIT_V, MOVE_4A, WAIT_END}) {
        nr(0x60, static_cast<uint8_t>((insn >> 8) & 0xFF));
        nr(0x60, static_cast<uint8_t>( insn       & 0xFF));
    }
    nr(0x62, 0xC0);     // mode 11: reset PC each vsync + run

    emu.run_frame();    // the Copper actually executes here

    // Ground truth first: did the Copper really write NR 0x4A mid-frame?
    Renderer& r = emu.renderer();
    const int row_before = Renderer::DISP_Y + WAIT_LINE - 1;   // 131
    const int row_after  = Renderer::DISP_Y + WAIT_LINE + 1;   // 133
    const int row_deep   = Renderer::DISP_Y + 150;             // 182

    check("DVP-18b1",
          "premise: the Copper MOVE really reached NR 0x4A (live register = GROUND)",
          r.fallback_colour() == GROUND
              && r.fallback_for_line(row_before) == SKY
              && r.fallback_for_line(row_after)  == GROUND,
          fmt("live=0x%02X row%d=0x%02X (want 0x%02X) row%d=0x%02X (want 0x%02X)",
              r.fallback_colour(),
              row_before, r.fallback_for_line(row_before), SKY,
              row_after,  r.fallback_for_line(row_after),  GROUND));

    // Now the panel: paused at the end of the frame, every row drawn.
    QImage bg = render_view(emu, VideoLayerView::Layer::BACKGROUND,
                            Renderer::FB_HEIGHT - 1);

    check("DVP-18b",
          "a mid-frame Copper MOVE to NR 0x4A shows as a band split in the Background view",
          px(bg, 0, row_before) == argb_sky
              && px(bg, 0, row_after) == argb_ground
              && px(bg, 400, row_deep) == argb_ground,
          fmt("row%d=0x%08X (want sky 0x%08X) row%d=0x%08X row%d=0x%08X "
              "(want ground 0x%08X)",
              row_before, px(bg, 0, row_before), argb_sky,
              row_after,  px(bg, 0, row_after),
              row_deep,   px(bg, 400, row_deep), argb_ground));

    // The Background view is a window onto the compositor, not a second opinion:
    // with every layer transparent the composite IS the fallback, row for row —
    // and both must equal the framebuffer run_frame() actually produced.
    QImage comp = render_view(emu, VideoLayerView::Layer::COMPOSITE,
                              Renderer::FB_HEIGHT - 1);
    const uint32_t* fb = emu.get_framebuffer();
    auto fb_px = [&](int x, int y) {
        return fb[y * Renderer::FB_WIDTH + x] | 0xFF000000u;
    };

    check("DVP-18c",
          "…and the composite AND the emulator's own framebuffer agree with it, row for row",
          px(comp, 0, row_before) == px(bg, 0, row_before)
              && px(comp, 0, row_after) == px(bg, 0, row_after)
              && px(comp, 400, row_deep) == px(bg, 400, row_deep)
              && fb_px(0, row_before) == argb_sky
              && fb_px(0, row_after)  == argb_ground
              && fb_px(400, row_deep) == argb_ground,
          fmt("comp row%d=0x%08X bg=0x%08X fb=0x%08X | "
              "comp row%d=0x%08X bg=0x%08X fb=0x%08X",
              row_before, px(comp, 0, row_before), px(bg, 0, row_before),
              fb_px(0, row_before),
              row_after, px(comp, 0, row_after), px(bg, 0, row_after),
              fb_px(0, row_after)));
}

// ── DVP-17: "All layers" is the leftmost tab, selected by default ─────

static void test_composite_is_default_tab() {
    set_group("DVP-COMP-TAB");

    Emulator emu;
    if (!build_next_emulator(emu)) {
        check("DVP-17", "Emulator construction for the tab-order check", false);
        return;
    }

    VideoPanel panel(&emu);
    auto* tabs = panel.findChild<QTabWidget*>();
    if (!tabs) {
        check("DVP-17", "VideoPanel has a QTabWidget", false);
        return;
    }

    check("DVP-17",
          "\"All layers\" is the leftmost tab and the selected one by default",
          tabs->count() == 6 && tabs->tabText(0) == QStringLiteral("All layers")
              && tabs->currentIndex() == 0,
          fmt("count=%d tab0='%s' current=%d", tabs->count(),
              tabs->tabText(0).toUtf8().constData(), tabs->currentIndex()));

    check("DVP-17a",
          "…and the per-layer tabs still follow it in order",
          tabs->count() == 6
              && tabs->tabText(1) == QStringLiteral("ULA")
              && tabs->tabText(2) == QStringLiteral("Layer2")
              && tabs->tabText(3) == QStringLiteral("Sprites")
              && tabs->tabText(4) == QStringLiteral("TileMap"),
          fmt("tabs: %s|%s|%s|%s|%s|%s",
              tabs->tabText(0).toUtf8().constData(),
              tabs->tabText(1).toUtf8().constData(),
              tabs->tabText(2).toUtf8().constData(),
              tabs->tabText(3).toUtf8().constData(),
              tabs->tabText(4).toUtf8().constData(),
              tabs->tabText(5).toUtf8().constData()));

    check("DVP-20",
          "\"Background\" is the RIGHTMOST tab (and not the selected one)",
          tabs->count() == 6
              && tabs->tabText(tabs->count() - 1) == QStringLiteral("Background")
              && tabs->currentIndex() != tabs->count() - 1,
          fmt("count=%d last='%s' current=%d", tabs->count(),
              tabs->tabText(tabs->count() - 1).toUtf8().constData(),
              tabs->currentIndex()));
}

// ── main ──────────────────────────────────────────────────────────────

// ── DVP-LAYERSTATE: the "active layers" line must read the LIVE register ─────
//
// Task 40, from beast.nex: the panel reported "ULA, Tilemap, Sprites" active while the
// Layer 2 view beside it showed the demo's graphics. It read NextReg::cached(), i.e.
// the last byte written to the NextREG number — but Layer 2's enable is a single FF
// that port 0x123B bit 1 latches too (VHDL zxnext.vhd:3916, 3924-3925), and that is
// how programs actually switch Layer 2 on. Raw NR 0x69 stayed 0x00; the true value
// was 0xC0.
//
// Which rows here are DISCRIMINATIVE (fail against the old cached() code) and which are
// only mapping coverage is stated per row, because it is not uniform and pretending
// otherwise would be the same species of lie this task is about:
//
//   NR 0x69 (Layer 2)  discriminative — port 0x123B never touches regs_[0x69]
//   NR 0x6B (Tilemap)  discriminative — the read handler returns the live tilemap control
//   NR 0x15 (sprites,  discriminative — the read handler recomposes both from the
//            priority)                  Renderer, so we drive the Renderer directly
//   NR 0x68 (ULA)      NOT discriminative, and cannot be: the read handler takes bit 7
//                      straight from cached(0x68) (emulator.cpp:2729-2733), so read()
//                      and cached() agree on it BY CONSTRUCTION. These rows pin the
//                      bit-7-is-a-DISABLE-bit inversion, nothing more.
static void test_layer_state_reads_live_registers(Emulator& emu) {
    set_group("DVP-LAYERSTATE");

    bool active[4];
    int  priority = -1;

    // Layer 2 ON via port 0x123B bit 1 — the beast.nex path. regs_[0x69] is untouched.
    emu.port().write(0x123B, 0x02);
    video_panel_layer_state(emu, active, priority);
    check("DVP-LS-01",
          "Layer 2 enabled via port 0x123B is reported ACTIVE (cached NR 0x69 is stale)",
          active[1],
          fmt("nr69 raw=%02X read=%02X layer2.enabled=%d",
              emu.nextreg().cached(0x69), emu.nextreg().read(0x69),
              int(emu.layer2().enabled())));

    // …and the raw cache really is stale, so the assertion above is discriminative
    // rather than accidentally true.
    check("DVP-LS-02",
          "the raw NR 0x69 cache does NOT see the port 0x123B write (test premise)",
          (emu.nextreg().cached(0x69) & 0x80) == 0,
          fmt("nr69 raw=%02X", emu.nextreg().cached(0x69)));

    // Layer 2 OFF again through the same port: the flag must follow it back down, so
    // the fix cannot be "always report Layer 2 active".
    emu.port().write(0x123B, 0x00);
    video_panel_layer_state(emu, active, priority);
    check("DVP-LS-03",
          "Layer 2 disabled via port 0x123B is reported INACTIVE",
          !active[1],
          fmt("nr69 read=%02X", emu.nextreg().read(0x69)));

    // Sprites + priority live in NR 0x15, whose read handler recomposes BOTH from the
    // Renderer (emulator.cpp:1628-1636). Drive the Renderer directly — regs_[0x15] is
    // never written, so these rows fail against cached() too.
    emu.nextreg().write(0x15, 0x00);          // cache says: no sprites, priority 0 (SLU)
    emu.renderer().set_sprite_en(true);       // …the machine says otherwise
    emu.renderer().set_layer_priority(3);     // priority 3 = LUS
    video_panel_layer_state(emu, active, priority);
    check("DVP-LS-04",
          "sprites enabled on the Renderer is reported ACTIVE (cached NR 0x15 says off)",
          active[3],
          fmt("nr15 raw=%02X read=%02X", emu.nextreg().cached(0x15), emu.nextreg().read(0x15)));
    check("DVP-LS-05",
          "layer priority comes from the Renderer, not the NR 0x15 cache",
          priority == 3,
          fmt("priority=%d want 3 (nr15 raw=%02X)", priority, emu.nextreg().cached(0x15)));

    // ULA: NR 0x68 bit 7 is a DISABLE bit, so the flag is inverted. NOT discriminative
    // (see the header) — the read handler sources bit 7 from the cache itself.
    emu.nextreg().write(0x68, 0x80);
    video_panel_layer_state(emu, active, priority);
    check("DVP-LS-06", "ULA disabled via NR 0x68 b7 is reported INACTIVE", !active[0]);
    emu.nextreg().write(0x68, 0x00);
    video_panel_layer_state(emu, active, priority);
    check("DVP-LS-07", "ULA enabled (NR 0x68 b7 clear) is reported ACTIVE", active[0]);

    // Tilemap: NR 0x6B's read handler returns the live tilemap control byte, so set it
    // on the Tilemap itself — regs_[0x6B] never sees this.
    emu.tilemap().set_control(0x80);
    video_panel_layer_state(emu, active, priority);
    check("DVP-LS-08",
          "tilemap enabled via the live control byte is reported ACTIVE",
          active[2],
          fmt("nr6b raw=%02X read=%02X",
              emu.nextreg().cached(0x6B), emu.nextreg().read(0x6B)));
}

// ── DVP-PEEK: the debugger must not MUTATE the machine it observes ───────────
//
// The NextREG panel displays all 256 registers and DebuggerManager refreshes it several
// times a second WHILE THE GUEST RUNS. NR 0x2C / 0x2E are the only two read handlers in
// the machine whose read has a side effect: per VHDL zxnext.vhd:6006-6015 they latch the
// Pi-I2S low bits into the NR 0x2D shadow. A panel that reads them the way the Z80 does
// would latch L, then latch R over it, and hand the guest a sample the DEBUGGER invented
// — a far worse bug than the stale display it was fixing (caught in review, Task 40).
//
// So the panel peeks. peek() returns the same byte with no side effect, and refuses to
// call a destructive handler at all.
static void test_peek_does_not_mutate(Emulator& emu) {
    set_group("DVP-PEEK");

    emu.nextreg().write(0xA2, 0xC0);            // enable Pi-I2S so the samples are live
    emu.i2s().set_sample(0x3FF, 0x000);         // L low bits = 0b11, R low bits = 0b00

    // The guest reads NR 0x2C: per the VHDL this LATCHES L's low 2 bits into NR 0x2D.
    const uint8_t l_hi = emu.nextreg().read(0x2C);
    const uint8_t d_after_guest_read = emu.nextreg().read(0x2D);
    check("DVP-PEEK-01",
          "guest read of NR 0x2C latches L's low bits into NR 0x2D (VHDL 6006-6015)",
          d_after_guest_read == 0xC0,
          fmt("nr2C=%02X nr2D=%02X want C0", l_hi, d_after_guest_read));

    // Now refresh the REAL NextREG panel — not a stand-in loop, the widget itself, so
    // this row still fails if someone points it back at read(). The latch must be
    // untouched. Through read() the sweep leaves 0x00 behind: R's low bits, clobbering
    // the guest's L sample.
    NextRegPanel panel(&emu);
    panel.refresh();

    check("DVP-PEEK-02",
          "refreshing the real NextREG panel does NOT disturb the NR 0x2D latch",
          emu.nextreg().read(0x2D) == 0xC0,
          fmt("nr2D=%02X want C0 (0x00 = the debugger clobbered it with R)",
              emu.nextreg().read(0x2D)));

    // …and peek still returns the right VALUE for the destructive registers: the twin
    // must not be a defensive zero. L = 0x3FF → high 8 bits = 0xFF.
    check("DVP-PEEK-03",
          "peek(0x2C) returns the same byte read(0x2C) does",
          emu.nextreg().peek(0x2C) == 0xFF,
          fmt("peek=%02X want FF", emu.nextreg().peek(0x2C)));
    // Assert what the label says, over all 256 — not a spot-check of two negatives. If a
    // register ever acquires a read side effect, it must be DECLARED, and this row is
    // what notices when it is not.
    int wrongly_destructive = 0;
    for (int i = 0; i < 256; ++i) {
        const uint8_t r = static_cast<uint8_t>(i);
        const bool expect = (r == 0x2C || r == 0x2E);
        if (emu.nextreg().read_is_destructive(r) != expect) ++wrongly_destructive;
    }
    check("DVP-PEEK-04",
          "NR 0x2C / 0x2E are declared destructive, and — across all 256 — nothing else is",
          wrongly_destructive == 0,
          fmt("%d register(s) disagree", wrongly_destructive));

    // peek() must equal read() everywhere it is safe to compare — i.e. everywhere except
    // the two destructive registers and NR 0x2D, whose value read() itself changes.
    int mismatches = 0;
    for (int i = 0; i < 256; ++i) {
        const uint8_t r = static_cast<uint8_t>(i);
        if (r == 0x2C || r == 0x2D || r == 0x2E) continue;
        if (emu.nextreg().peek(r) != emu.nextreg().read(r)) ++mismatches;
    }
    check("DVP-PEEK-05",
          "peek() agrees with read() for every non-destructive register",
          mismatches == 0, fmt("%d mismatch(es)", mismatches));
}

// ── DVP-RESUME: a frame the debugger paused must be RESUMED, not RESTARTED ───
//
// Task 40, second half. The debugger pauses by returning from inside run_frame()'s loop,
// leaving the frame half-executed; the frontend then calls run_frame() again to continue
// it. run_frame() re-ran its entire frame-start block on that call — mid-frame:
//
//   copper_.on_vsync()        rewound the Copper's PC, so a Copper program restarted at
//                             mid-frame re-executed its WAITs against scanlines already
//                             gone by, never matched again, and wrote NOTHING for the
//                             rest of the frame;
//   palette_.start_frame()    CLEARED the per-scanline palette change log and rebaselined
//     (and the same for      it to the mid-frame state — and that log is exactly what the
//      layer2/sprites/ula/    compositor and the debugger's video panels replay to
//      tilemap)               reproduce raster splits.
//
// Reported from beast.nex: Break, then "Run to EOF", and the Copper's per-scanline sky
// gradient vanished — from the ULA panel on one press, and from the EMULATOR WINDOW on
// the next, alternating, as the wiped log was consumed by one replay or the other.
//
// Stepping through a frame must OBSERVE the emulation, never alter it.
static void test_paused_frame_resumes_not_restarts(Emulator& emu) {
    set_group("DVP-RESUME");

    emu.debug_state().set_active(true);
    emu.run_frame();                      // settle: one clean frame

    // Pause partway through the next frame, exactly as "Break" / "Run to EOF" do.
    const auto& t = emu.timing();
    const uint64_t mid = emu.current_frame_cycle() + 100 * t.master_cycles_per_line;
    emu.debug_state().run_to_cycle(mid);
    emu.run_frame();
    check("DVP-RES-01", "the debugger paused mid-frame (test premise)",
          emu.debug_state().paused() && emu.clock().get() < emu.current_frame_cycle()
              + t.master_cycles_per_frame,
          fmt("paused=%d", int(emu.debug_state().paused())));

    // A mid-frame palette write — the kind a Copper MOVE makes on every scanline, and
    // the kind whose change-log entry the compositor replays to draw a gradient.
    emu.nextreg().write(0x40, 0x10);      // palette index
    emu.nextreg().write(0x41, 0xE3);      // 8-bit colour write
    const size_t log_before = emu.palette().change_log_size();
    check("DVP-RES-02", "the mid-frame palette write is in the change log (test premise)",
          log_before >= 1, fmt("entries=%zu", log_before));

    // Resume. Before the fix this call re-ran the frame-start block: start_frame() wiped
    // the log (entries -> 0) and on_vsync() rewound the Copper.
    emu.debug_state().resume();
    emu.run_frame();

    check("DVP-RES-03",
          "resuming does NOT clear the frame's palette change log",
          emu.palette().change_log_size() >= log_before,
          fmt("entries=%zu, were %zu (0 = the resume wiped the frame's raster splits)",
              emu.palette().change_log_size(), log_before));

    // …and the NEXT frame must still get its frame-start actions: the guard resumes a
    // paused frame, it does not suppress frame starts altogether.
    emu.run_frame();
    check("DVP-RES-05",
          "a genuinely new frame still gets its frame-start baseline (log reset)",
          emu.palette().change_log_size() == 0,
          fmt("entries=%zu", emu.palette().change_log_size()));
}

// The Copper half of the same bug, on its own.
//
// copper_.on_vsync() and the change-log start_frame()s are two INDEPENDENT casualties of
// re-running the frame-start block mid-frame, and today one boolean guards both. The row
// above would still pass if a future change decided that "only the change logs need the
// guard; on_vsync() is harmless" — and beast.nex's sky would silently go flat again, which
// is precisely the reported symptom. So the Copper gets its own row, with a real Copper
// program: WAIT scanline N, MOVE a palette colour — the shape that paints a gradient.
//
// A Copper rewound to PC 0 mid-frame re-executes its WAITs against scanlines that have
// already gone by, so it never matches again and writes nothing for the rest of the frame.
static void test_resume_does_not_rewind_the_copper(Emulator& emu) {
    set_group("DVP-COPPER");

    auto nr = [&emu](uint8_t reg, uint8_t val) { emu.nextreg().write(reg, val); };
    auto enc_move = [](uint8_t reg, uint8_t val) -> uint16_t {
        return static_cast<uint16_t>(((reg & 0x7F) << 8) | val);
    };
    auto enc_wait = [](uint8_t hpos, uint16_t vpos) -> uint16_t {
        return static_cast<uint16_t>(0x8000 | ((hpos & 0x3F) << 9) | (vpos & 0x1FF));
    };
    auto program_word = [&](uint16_t word_addr, uint16_t instr) {
        const uint8_t mode_hi = static_cast<uint8_t>(emu.nextreg().read(0x62) & 0xC0);
        nr(0x61, static_cast<uint8_t>(((word_addr & 0x3FF) << 1) & 0xFF));
        nr(0x62, static_cast<uint8_t>(mode_hi | ((((word_addr & 0x3FF) << 1) >> 8) & 0x07)));
        nr(0x63, static_cast<uint8_t>(instr >> 8));
        nr(0x63, static_cast<uint8_t>(instr & 0xFF));
    };

    // A gradient program: on every 8th scanline, write a palette colour.
    constexpr int kSteps = 24;
    for (int i = 0; i < kSteps; ++i) {
        program_word(static_cast<uint16_t>(i * 3 + 0), enc_wait(0, static_cast<uint16_t>(32 + i * 8)));
        program_word(static_cast<uint16_t>(i * 3 + 1), enc_move(0x40, 0x10));            // palette index
        program_word(static_cast<uint16_t>(i * 3 + 2), enc_move(0x41, static_cast<uint8_t>(i * 4)));
    }
    nr(0x62, 0xC0);                       // mode 11 = run + RESET PC AT VSYNC (what a
                                          // per-frame gradient uses, and what makes
                                          // on_vsync() mid-frame destructive)
    emu.run_frame();                      // one clean frame with the Copper running

    // Pause mid-frame, PAST several WAITs, so the Copper PC is deep in its program.
    const auto& t = emu.timing();
    emu.debug_state().set_active(true);
    emu.debug_state().run_to_cycle(emu.current_frame_cycle() + 120 * t.master_cycles_per_line);
    emu.run_frame();
    const uint16_t pc_before = emu.copper().pc();
    check("DVP-COP-01",
          "the Copper is mid-program at the pause (test premise — not PC 0)",
          emu.debug_state().paused() && pc_before > 0,
          fmt("paused=%d copper pc=%u", int(emu.debug_state().paused()), pc_before));

    // Resume, and stop again a few scanlines later — still inside the SAME frame. Sample
    // the PC immediately, before the frame can run on and mask a rewind by advancing past
    // it again. Pre-fix, the resume called on_vsync() and this reads 0.
    emu.debug_state().run_to_cycle(emu.current_frame_cycle() + 150 * t.master_cycles_per_line);
    emu.run_frame();
    const uint16_t pc_after = emu.copper().pc();
    check("DVP-COP-02",
          "resuming a paused frame does NOT rewind the Copper to its program start",
          pc_after >= pc_before,
          fmt("copper pc %u -> %u (a rewind to 0 = the gradient stops here)",
              pc_before, pc_after));

    // The observable the user actually reported: the Copper must keep painting for the
    // REST of the frame after the resume. A rewound Copper waits forever for scanlines
    // that have already passed, and writes nothing more.
    const size_t log_at_resume = emu.palette().change_log_size();
    emu.debug_state().resume();
    emu.run_frame();                      // finish the frame
    check("DVP-COP-03",
          "the Copper keeps writing its per-scanline palette after the resume",
          emu.palette().change_log_size() > log_at_resume,
          fmt("entries %zu -> %zu (no growth = the Copper stopped painting)",
              log_at_resume, emu.palette().change_log_size()));
}

int main(int argc, char** argv) {
    // A QWidget needs a QApplication, but not a display.
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("Debugger Video Panel Tests (Task 22a)\n");
    std::printf("=====================================\n\n");

    // Each group gets a fresh Emulator: the groups deliberately leave the
    // machine in different video configurations.
    {
        Emulator emu;
        if (!build_next_emulator(emu)) {
            std::printf("FATAL: could not construct Emulator\n");
            return 1;
        }
        test_layer2_rom_in_sram(emu);
        std::printf("  Group: DVP-L2-SRAM    — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_layer2_transparent_rgb_replay(emu);
        std::printf("  Group: DVP-L2-TRANSP  — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_ula_bank_views(emu);
        std::printf("  Group: DVP-ULA-BANK   — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_per_line_palette_replay(emu);
        std::printf("  Group: DVP-REPLAY     — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_no_state_mutation(emu);
        std::printf("  Group: DVP-NOMUT      — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_tilemap_per_line_scroll(emu);
        std::printf("  Group: DVP-TM-SCROLL  — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_fb_row_conversion(emu);
        std::printf("  Group: DVP-FBROW      — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_ula_clip_window(emu);
        std::printf("  Group: DVP-ULA-CLIP   — done\n");
    }
    test_run_to_targets();
    std::printf("  Group: DVP-RUNTO      — done\n");

    // ── Task 40: the "active layers" line must read the live registers ───
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_layer_state_reads_live_registers(emu);
        std::printf("  Group: DVP-LAYERSTATE — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_peek_does_not_mutate(emu);
        std::printf("  Group: DVP-PEEK       — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_paused_frame_resumes_not_restarts(emu);
        std::printf("  Group: DVP-RESUME     — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_resume_does_not_rewind_the_copper(emu);
        std::printf("  Group: DVP-COPPER     — done\n");
    }

    // ── Task 36: the "All layers" composite view ─────────────────────
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_composite_matches_framebuffer(emu);
        std::printf("  Group: DVP-COMPOSITE  — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_fallback_colour_sonic_case(emu);
        std::printf("  Group: DVP-FALLBACK   — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_composite_raster_cutoff(emu);
        std::printf("  Group: DVP-COMP-RASTER— done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_composite_no_state_mutation(emu);
        std::printf("  Group: DVP-COMP-NOMUT — done\n");
    }
    {
        Emulator emu;
        if (!build_next_emulator(emu)) return 1;
        test_background_view(emu);
        std::printf("  Group: DVP-BACKGROUND — done\n");
    }
    test_background_copper();
    std::printf("  Group: DVP-BG-COPPER  — done\n");
    test_composite_is_default_tab();
    std::printf("  Group: DVP-COMP-TAB   — done\n");

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4zu\n",
                g_total + static_cast<int>(g_skipped.size()),
                g_pass, g_fail, g_skipped.size());

    std::printf("\nPer-group breakdown (live rows only):\n");
    std::string last;
    int gp = 0, gf = 0;
    for (const auto& r : g_results) {
        if (r.group != last) {
            if (!last.empty())
                std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);
            last = r.group;
            gp = gf = 0;
        }
        if (r.passed) ++gp; else ++gf;
    }
    if (!last.empty())
        std::printf("  %-22s %d/%d\n", last.c_str(), gp, gp + gf);

    return g_fail > 0 ? 1 : 0;
}
