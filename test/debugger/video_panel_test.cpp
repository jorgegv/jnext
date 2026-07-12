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
//           observes is a Heisenbug generator.)
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
//
// Qt is required (the panel is a QWidget), but no display is: the test forces
// the offscreen QPA platform.  Run: ./build/test/debugger_video_panel_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debugger/debugger_manager.h"
#include "debugger/video_panel.h"
#include "debug/debug_state.h"
#include "memory/mmu.h"
#include "memory/ram.h"
#include "video/layer2.h"
#include "video/palette.h"
#include "video/renderer.h"
#include "video/tilemap.h"
#include "video/ula.h"

#include <QApplication>
#include <QImage>
#include <QMainWindow>

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
    for (auto layer : { VideoLayerView::Layer::ULA_PRIMARY,
                        VideoLayerView::Layer::ULA_SHADOW,
                        VideoLayerView::Layer::LAYER2_ACTIVE,
                        VideoLayerView::Layer::LAYER2_SHADOW,
                        VideoLayerView::Layer::SPRITES,
                        VideoLayerView::Layer::TILEMAP }) {
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

// ── main ──────────────────────────────────────────────────────────────

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
    test_run_to_targets();
    std::printf("  Group: DVP-RUNTO      — done\n");

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
