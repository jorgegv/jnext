// docshot — regenerate the user guide's debugger screenshots from the running
// product, headlessly.
// ===========================================================================
//
// WHY THIS EXISTS
//
// src/doc/user-guide/img/debugger-*.png are pictures of the debugger. Until
// this tool they were hand-captured, and nothing compared them against the UI
// they claim to show: `make docs-check` proves the rendered HTML matches the
// markdown, and it cannot see inside a PNG. So a screenshot could contradict
// the prose directly beneath it with every gate green — which happened twice.
// GH #225 added an Enabled column and a master switch to the Breakpoints panel
// and left the picture above the description showing neither (caught in
// review); GH #22 replaced the Video panel's one-line HC/VC header with four
// labelled raster counters, a Region readout, a ULA-fetch readout and a frame
// diagram, and left debugger-video.png showing the old header (not caught at
// all — it was still stale two issues later).
//
// This tool is the answer to "re-capture it in the same change": one command,
// no display, no mouse, deterministic content.
//
// WHAT IT DOES
//
//   1. Boots a real ZX Next from the SD image — nextboot.rom -> TBBLUE.FW ->
//      NextZXOS — for exactly kBootFrames frames with the RTC pinned, which is
//      the same state the regression suite's `boot-nextzxos-welcome` row
//      captures. That is what puts real ROM code under the disassembly panel,
//      a real stack under the Stack panel, and a real screen in the Video
//      panel's composite view.
//   2. Brings up the REAL DebuggerWindow through the production path
//      (DebuggerManager::set_enabled(true), then on_pause() — what Alt+D
//      followed by F9 does), so every panel is filled by the same code the
//      user's panels are filled by. Nothing here reimplements a panel.
//   3. Renders the window once per shot and CROPS each panel out of that
//      render, using the panel's own geometry (mapTo + size). No panel is
//      reparented or resized, so no image can show a layout the window does
//      not actually have, and the panel crops line up with debugger-window.png
//      by construction.
//
// THE CONTENT FIXTURE, AND WHY IT IS HONEST
//
// NextZXOS at its welcome screen drives no sprites, no Copper program and no
// AY register. Those three panels would therefore be pictures of an empty
// table, which teaches a reader nothing about the columns the guide describes
// beside them — the old hand-captured sprites/copper images came from a demo
// program for exactly this reason. So after the boot-state shots are taken,
// this tool writes a small fixture THROUGH THE REAL HARDWARE WRITE PATHS
// (SpriteEngine::write_slot_select/write_attribute, NR 0x61/0x62/0x60 for the
// Copper, TurboSound::reg_addr/reg_write for the AY chips), runs two more
// frames so the Copper is genuinely executing and the sprites are genuinely
// composited, and only then captures those three panels. The panels decode
// real machine state produced by the real write paths; nothing is drawn by
// hand. The order matters and is fixed: every boot-state shot is taken BEFORE
// the fixture is applied.
//
// DETERMINISM
//
//   * The QPA platform is `offscreen`, with a screen described big enough
//     (3000x2200) that the debugger window opens at the size asked for rather
//     than clamped to it — the same configfile idiom as
//     test/debugger/window_grow_test.cpp.
//   * JNEXT_CONFIG_DIR is redirected to a throwaway directory, so a developer's
//     saved Debugger.conf geometry cannot change the shot (and this run cannot
//     change their geometry). It is set AFTER the SD image is resolved, since
//     the default SD location is derived from the same variable.
//   * The Qt style is forced to Fusion, so the images do not carry whichever
//     desktop theme the developer happens to run.
//   * The RTC is pinned, so the NextZXOS clock is the same every run.
//
// SD CARD
//
// A NextZXOS image is required; there is no useful screenshot of a machine
// with no ROMs. Resolution order is the project's existing one:
// --sdcard PATH, then $JNEXT_TEST_SD_IMAGE, then the location jnext itself
// falls back to (sdcard::default_sdcard_image_path()). jnext opens an image
// read-write and NextZXOS writes to it, so `make docs-screenshots` hands this
// tool a reflink clone rather than the master — see the Makefile target.
//
// WHAT IT DOES NOT COVER — and why, stated rather than silently skipped:
//
//   img/gui-main-window.png    the main emulator window. Needs jnext_gui and
//                              an SDL audio device (MainWindow owns the audio
//                              path); capturing it headlessly means bringing up
//                              a second frontend for one picture.
//   img/preferences-startup.png  the Preferences dialog. Its content IS the
//                              developer's own ~/.jnext/jnext.conf; a capture
//                              would publish whatever they had set.
//   every non-debugger image   boot-splash, machine-*, layers-*, nextzxos-*,
//                              nxtel-online, first-boot, 07-screenshot-diff —
//                              these are pictures of emulator OUTPUT, not of a
//                              Qt widget, and jnext already renders them itself
//                              via --delayed-screenshot.
//
// Run: ./build/docshot --out src/doc/user-guide/img [name ...]

#include "audio/turbosound.h"
#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/sdcard_provisioner.h"
#include "debug/breakpoints.h"
#include "debug/debug_state.h"
#include "debug/raster_state.h"
#include "debugger/cpu_panel.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/disasm_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/video_panel.h"
#include "debugger/watch_panel.h"
#include "port/nextreg.h"
#include "video/sprites.h"
#include "video/timing.h"

#include <QApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMainWindow>
#include <QScrollArea>
#include <QScrollBar>
#include <QStyleFactory>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

// ── Fixture constants ─────────────────────────────────────────────────

// Frames to the NextZXOS welcome screen. Same value as the
// `boot-nextzxos-welcome` row of test/00regression/regression_tests.conf, and
// for the same reason: it is where the boot has finished and nothing is
// animating, so the picture is stable.
constexpr int kBootFrames = 400;
// Same pinned clock as that row, so the NextZXOS date is identical every run.
constexpr const char* kRtc = "2026-07-10T08:55:00";
// Rewind depth. Non-zero so the rewind toolbar is populated and appears in the
// window shot (it is hidden when the buffer is empty), and so the guide's
// picture of the window shows the same control bar a user with rewind on sees.
constexpr int kRewindFrames = 300;
// Window WIDTH for every capture — the width the committed debugger-window.png
// has, so the three top-area columns keep the proportions the guide's pages
// were written around. The height is NOT pinned; see the sizing block in
// main() for why.
constexpr int kWindowW = 1400;

// ── Small helpers ─────────────────────────────────────────────────────

int g_saved  = 0;
int g_failed = 0;

#if defined(__GNUC__)
__attribute__((format(printf, 1, 2)))
#endif
void note(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(stdout, fmt, ap);
    va_end(ap);
    std::fflush(stdout);
}

/// Let Qt apply pending layout/resize/paint work. A widget rendered in the
/// same turn as the resize that sized it renders at the OLD size.
void settle(int ms = 60) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// Stop the machine on the LAST PAPER SCANLINE of a frame, mid-line.
///
/// Where the machine stops decides what half the debugger shows. Left at the
/// frame boundary the boot loop ends on, every video view is blank — the
/// panels draw rows the raster has not reached yet as the dark "unrendered"
/// placeholder (the convention the guide's Video page describes), and at raw
/// VC 0 that is all of them. The last paper line is the position that shows
/// the most: the whole 256x192 screen has been drawn, the GH #22 header reads
/// `Region: Paper` with a real `ULA fetch` rather than `Border`/`Idle`, and
/// the dark bottom-border strip still demonstrates the convention itself.
///
/// The target cycle is DebuggerManager::on_run_to_eof()'s arithmetic with the
/// row as a parameter (frame start + row*line + half a line, rolled into the
/// next frame if that instant has passed), armed through the same
/// DebugState::run_to_cycle() primitive both Run-to-EOF and Run-to-EOSL use.
/// The row itself is not hard-coded: it is found by asking raster_state_at()
/// — the panel's own classifier — which is the last line it calls Paper, so
/// this follows the machine type rather than a table copied beside one.
bool run_to_last_paper_line(Emulator& emu, DebuggerManager* mgr) {
    const VideoTiming& vt = emu.video_timing();
    const auto& t = emu.timing();
    const int hc_mid = static_cast<int>((t.master_cycles_per_line / 2) / 4);

    int raw_vc = -1;
    for (int v = vt.vc_max(); v >= 0; --v) {
        const RasterState rs = raster_state_at(vt, hc_mid, v,
                                               emu.ula().get_screen_mode_reg(),
                                               emu.ula().get_shadow_screen_en());
        if (rs.in_paper()) { raw_vc = v; break; }
    }
    if (raw_vc < 0) {
        std::fprintf(stderr, "docshot: no paper scanline found in the frame\n");
        return false;
    }

    const uint64_t frame_start = emu.current_frame_cycle();
    uint64_t target = frame_start
                    + static_cast<uint64_t>(raw_vc) * t.master_cycles_per_line
                    + t.master_cycles_per_line / 2;
    if (emu.clock().get() >= target)
        target += t.master_cycles_per_frame;

    // Clear the data-breakpoint latch before arming.
    //
    // NOT defensive housekeeping — it works around a real defect this tool
    // ran into. Mmu::read() raises DebugState::data_bp_hit for a READ
    // watchpoint, and the debugger panels read memory through that same
    // Mmu::read(): the Watches panel reads each watch's address, and the
    // Memory, Stack and Disassembly panels read the region they display. So a
    // READ or READ/WRITE watchpoint on an address any of those panels shows
    // is tripped BY THE PANEL REFRESH, the latch survives run_to_cycle()
    // (only DebugState::resume() clears it), and the next run stops one
    // instruction later at an address the watchpoint has nothing to do with.
    // Here that stopped the fixture frame dead and left the Copper panel
    // showing a PC of 0 on a program that had never run.
    emu.debug_state().set_data_bp_hit(false);
    emu.debug_state().run_to_cycle(target);
    for (int i = 0; i < 4 && !emu.debug_state().paused(); ++i)
        emu.run_frame();
    if (!emu.debug_state().paused()) {
        std::fprintf(stderr, "docshot: the run to raw VC %d never stopped\n", raw_vc);
        return false;
    }
    // on_pause() is what flips the four paused-only panels back on and runs
    // the follow-PC the disassembly needs. The refresh that follows is
    // DebuggerManager's, NOT DebuggerWindow's: only the manager's version
    // calls Emulator::snapshot_raster() first, and without that the Video
    // panel's whole GH #22 raster block reads a stale paused_hc/paused_vc
    // pair. In the running program the frame tick calls it every frame, so a
    // tool that refreshes any other way is not looking at what a user sees.
    mgr->on_pause();
    mgr->refresh_panels();

    const RasterState got = video_panel_raster_state(emu);
    if (!got.in_paper()) {
        std::fprintf(stderr,
            "docshot: stopped at raw hc %d vc %d, which is not paper — the video\n"
            "  views would be a picture of an unfinished frame.\n",
            got.raw_hc, got.raw_vc);
        return false;
    }
    note("docshot: stopped on the last paper line (raw hc %d vc %d)\n",
         got.raw_hc, got.raw_vc);
    return true;
}

/// Walk up from `w` to the QTabWidget that holds it. A QTabWidget's pages are
/// parented to its internal QStackedWidget, so this is two hops, not one — and
/// writing it as a walk means it keeps working if Qt adds a level.
QTabWidget* owning_tab_widget(QWidget* w) {
    for (QWidget* p = w ? w->parentWidget() : nullptr; p; p = p->parentWidget())
        if (auto* tabs = qobject_cast<QTabWidget*>(p)) return tabs;
    return nullptr;
}

/// Type `text` into a panel's address box and press Return, the way a user
/// does. Both the Memory and the Disassembly panel navigate from one, and
/// neither exposes a public "go to address" method — which is fine, because
/// driving the real widget proves the box still works rather than only that a
/// private method does.
bool type_into_address_box(QWidget* panel, const QString& text) {
    auto* box = panel ? panel->findChild<QLineEdit*>() : nullptr;
    if (!box) return false;
    box->setText(text);
    QKeyEvent press(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QApplication::sendEvent(box, &press);
    return true;
}

/// Render `window` and write the sub-rectangle occupied by `part` to `path`.
/// `part == window` writes the whole window.
///
/// The crop rectangle comes from the live layout (mapTo + size), never from a
/// hand-measured constant: a panel that moves or changes size produces a
/// correct image at the new geometry instead of a picture with somebody else's
/// pixels along its edge.
bool save_crop(QWidget* window, QWidget* part, const QString& path) {
    if (!window || !part) {
        note("  FAIL %s: widget not found\n", qPrintable(QFileInfo(path).fileName()));
        ++g_failed;
        return false;
    }
    QImage shot(window->size(), QImage::Format_RGB32);
    shot.fill(Qt::white);
    window->render(&shot);

    QRect rect(QPoint(0, 0), window->size());
    if (part != window) {
        rect = QRect(part->mapTo(window, QPoint(0, 0)), part->size());
        if (!rect.intersects(QRect(QPoint(0, 0), window->size()))) {
            note("  FAIL %s: panel is outside the rendered window (%d,%d %dx%d)\n",
                 qPrintable(QFileInfo(path).fileName()),
                 rect.x(), rect.y(), rect.width(), rect.height());
            ++g_failed;
            return false;
        }
        rect &= QRect(QPoint(0, 0), window->size());
    }

    const QImage out = shot.copy(rect);
    if (!out.save(path, "PNG")) {
        note("  FAIL %s: could not write the file\n",
             qPrintable(QFileInfo(path).fileName()));
        ++g_failed;
        return false;
    }
    note("  %-28s %4dx%-4d\n", qPrintable(QFileInfo(path).fileName()),
         out.width(), out.height());
    ++g_saved;
    return true;
}

// ── The demo fixture (sprites / Copper / AY) ──────────────────────────

/// Fill the sprite attribute table through the port-0x303B/0x57 upload path —
/// byte for byte what a program uploading sprites does.
///
/// The spread is deliberate and deterministic: the guide's Sprites page lists
/// nine columns (X, Y, pattern, palette offset, visible, mirror, rotate, X and
/// Y scale) and a table in which every one of them holds the same value shows
/// the reader nothing about any of them. Every column therefore varies down the
/// table, from a fixed integer recurrence so two runs produce the same table.
void apply_sprite_fixture(Emulator& emu) {
    SpriteEngine& spr = emu.sprites();
    uint32_t seed = 0x5EED1234u;
    auto next = [&seed]() { seed = seed * 1664525u + 1013904223u; return (seed >> 16) & 0xFFFF; };

    for (int i = 0; i < SpriteEngine::NUM_SPRITES; ++i) {
        // X is 9-bit (the MSB rides in byte 2), Y 9-bit (byte 4). Both are
        // kept inside the visible area so the table reads like a real scene.
        const int x = 32 + static_cast<int>(next() % 448);
        const int y = 16 + static_cast<int>(next() % 208);
        const uint8_t pal   = static_cast<uint8_t>(i % 16);
        const uint8_t pat   = static_cast<uint8_t>(i % 24);
        const bool xmir     = (i % 5) == 1;
        const bool ymir     = (i % 7) == 2;
        const bool rot      = (i % 11) == 3;
        const uint8_t xscale = static_cast<uint8_t>((i / 8) % 4);
        const uint8_t yscale = static_cast<uint8_t>((i / 4) % 4);

        // byte2: palette(7:4) xmirror(3) ymirror(2) rotate(1) x_msb(0)
        const uint8_t b2 = static_cast<uint8_t>((pal << 4) | (xmir ? 0x08 : 0) |
                                                (ymir ? 0x04 : 0) | (rot ? 0x02 : 0) |
                                                ((x >> 8) & 1));
        // byte3: visible(7) extended(6) pattern(5:0). Extended, so byte 4 is
        // read and the scale fields mean something.
        const uint8_t b3 = static_cast<uint8_t>(0x80 | 0x40 | (pat & 0x3F));
        // byte4: 4bit(7) N6(6) anchor-type(5) xscale(4:3) yscale(2:1) y_msb(0).
        // Bits 7:6 = 00 keeps every sprite an 8-bit ANCHOR sprite: 01 there
        // would make it relative to the previous one, which is a different
        // feature and not what this table is illustrating.
        const uint8_t b4 = static_cast<uint8_t>((xscale << 3) | (yscale << 1) |
                                                ((y >> 8) & 1));

        spr.write_slot_select(static_cast<uint8_t>(i));
        spr.write_attribute(static_cast<uint8_t>(x & 0xFF));
        spr.write_attribute(static_cast<uint8_t>(y & 0xFF));
        spr.write_attribute(b2);
        spr.write_attribute(b3);
        spr.write_attribute(b4);
    }
    // NR 0x15 bit 0 — global sprite visibility. peek(), not read(): read()
    // runs the register's read handler, and this is a read-modify-write of a
    // cached value, not an observation of the bus.
    emu.nextreg().write(0x15, static_cast<uint8_t>(emu.nextreg().peek(0x15) | 0x01));
}

/// Upload and start a Copper program, through NR 0x61/0x62/0x60 — the register
/// sequence a program uses.
///
/// The program is a per-band NR 0x4A (fallback colour) ramp: WAIT for a line,
/// MOVE a colour, repeat. That shape is chosen because it is what the guide's
/// Copper page describes row by row — a WAIT with a v/h position and a MOVE
/// written `NR 4A = yy` — so the picture and the prose show the same thing.
void apply_copper_fixture(Emulator& emu) {
    NextReg& nr = emu.nextreg();

    // Write address = 0, mode 00 (stopped) while the program is uploaded.
    nr.write(0x61, 0x00);
    nr.write(0x62, 0x00);

    // 32 WAIT/MOVE pairs = 64 words, which is exactly the window the Copper
    // panel shows around the PC.
    for (int band = 0; band < 32; ++band) {
        const uint8_t line   = static_cast<uint8_t>(band * 8);
        const uint8_t colour = static_cast<uint8_t>(band * 8);   // 00..F8, monotonic
        // WAIT: [15] = 1, [14:9] = hpos (6 bits, in units of 8 pixel
        // clocks), [8:0] = vpos (9 bits) — src/peripheral/copper.cpp:10-42.
        // hpos 0 means "as soon as this line starts". Written MSB first
        // through NR 0x60, the 8-bit auto-incrementing data port.
        const uint16_t wait_word = static_cast<uint16_t>(0x8000 | line);
        nr.write(0x60, static_cast<uint8_t>(wait_word >> 8));
        nr.write(0x60, static_cast<uint8_t>(wait_word & 0xFF));
        // MOVE: bit15 = 0, [14:8] = register, [7:0] = value.
        const uint16_t move_word = static_cast<uint16_t>((0x4A << 8) | colour);
        nr.write(0x60, static_cast<uint8_t>(move_word >> 8));
        nr.write(0x60, static_cast<uint8_t>(move_word & 0xFF));
    }

    // Mode 11 — start, and reset the PC at every frame.
    nr.write(0x62, 0xC0);
}

/// Load the three AY chips with a chord, through the register-select /
/// register-data pair (ports 0xFFFD / 0xBFFD).
///
/// A table of sixteen zeroes per chip is a legal AY state and a useless
/// picture: the guide names every register beside it, so every register should
/// hold something a reader can match to its name.
void apply_audio_fixture(Emulator& emu) {
    TurboSound& ts = emu.turbosound();

    struct ChipRegs { uint8_t v[16]; };
    // Three chips, three voices each, mixer letting tone through on all three
    // channels, a noise period, volumes, and an envelope so R11-R13 are not
    // blank either.
    static const ChipRegs kChips[3] = {
        {{0xFD, 0x00, 0x54, 0x01, 0xA9, 0x01, 0x0F, 0x38, 0x0D, 0x0B, 0x09, 0x00, 0x20, 0x0E, 0x00, 0x00}},
        {{0x7E, 0x00, 0xAA, 0x00, 0x54, 0x01, 0x07, 0x3C, 0x0C, 0x0A, 0x08, 0x80, 0x10, 0x08, 0x00, 0x00}},
        {{0xBF, 0x01, 0xFD, 0x00, 0x7E, 0x00, 0x1F, 0x38, 0x0A, 0x0F, 0x0B, 0x40, 0x08, 0x0A, 0x00, 0x00}},
    };

    // NR 0x08 bit 1 — TurboSound enable (src/core/emulator.cpp:5689). Without
    // it TurboSound::reg_addr ignores the chip-select bytes below, every write
    // lands on AY#0, and the second and third columns stay at their reset
    // values.
    emu.nextreg().write(0x08, static_cast<uint8_t>(emu.nextreg().peek(0x08) | 0x02));

    for (int chip = 0; chip < 3; ++chip) {
        // 0xFFFD value 0xFF..0xFD selects which of the three chips the
        // following register writes address (turbosound.vhd): 0xFF = AY#0,
        // 0xFE = AY#1, 0xFD = AY#2.
        ts.reg_addr(static_cast<uint8_t>(0xFF - chip));
        for (int r = 0; r < 16; ++r) {
            ts.reg_addr(static_cast<uint8_t>(r));
            ts.reg_write(kChips[chip].v[r]);
        }
    }
}

/// Watches, breakpoints and watchpoints — the state those two panels exist to
/// show. Both panels are pictures of a LIST, so an empty one is a picture of
/// nothing; and the Breakpoints page describes an On checkbox and a master
/// switch, so the table needs at least one row with the box cleared for the
/// difference to be visible at all (GH #225: the picture it replaced showed
/// neither control).
void apply_debug_fixture(Emulator& emu, DebuggerWindow* dbg) {
    BreakpointSet& bps = emu.debug_state().breakpoints();
    bps.add_pc(0x0C8F);                                  // the ROM HALT the boot sits on
    bps.add_pc(0x8000);
    bps.add_watchpoint(0x5C78, WatchType::WRITE);        // FRAMES, the 48K frame counter
    bps.add_watchpoint(0x5C3B, WatchType::READ_WRITE);   // FLAGS
    bps.set_pc_enabled(0x8000, false);                   // one row with On cleared

    if (auto* wp = dbg->watch_panel()) {
        wp->add_watch(0x5C78, "FRAMES", 1);   // word — the counter is 24-bit LE
        wp->add_watch(0x5C3B, "FLAGS",  0);   // byte
        wp->add_watch(0x4000, "SCREEN", 2);   // long
    }
}

// ── main ──────────────────────────────────────────────────────────────

struct Options {
    QString out_dir = QStringLiteral("src/doc/user-guide/img");
    QString sdcard;
    std::vector<std::string> only;   // empty = every image
};

bool wanted(const Options& o, const char* name) {
    if (o.only.empty()) return true;
    for (const auto& s : o.only)
        if (s == name) return true;
    return false;
}

void usage() {
    std::printf(
        "docshot — regenerate the user guide's debugger screenshots\n"
        "\n"
        "Usage: docshot [--out DIR] [--sdcard IMAGE] [NAME ...]\n"
        "\n"
        "  --out DIR       where to write the PNGs (default src/doc/user-guide/img)\n"
        "  --sdcard IMAGE  NextZXOS SD image to boot; defaults to $JNEXT_TEST_SD_IMAGE,\n"
        "                  then the location jnext itself falls back to\n"
        "  NAME ...        capture only these images (bare names, no .png)\n"
        "\n"
        "Images: debugger-window debugger-cpu-mmu debugger-disassembly\n"
        "        debugger-video debugger-nextreg debugger-memory debugger-stack\n"
        "        debugger-watches debugger-breakpoints\n"
        "        debugger-sprites debugger-copper debugger-audio\n");
}

} // namespace

int main(int argc, char** argv)
{
    Options opt;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--out" && i + 1 < argc)    opt.out_dir = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--sdcard" && i + 1 < argc) opt.sdcard  = QString::fromLocal8Bit(argv[++i]);
        else if (!a.empty() && a[0] == '-')       { usage(); return 2; }
        else                                      opt.only.push_back(a);
    }

    // ── SD image: resolve BEFORE JNEXT_CONFIG_DIR is redirected below, since
    // the default location is derived from that very variable.
    QString sd = opt.sdcard;
    if (sd.isEmpty()) {
        if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE"))
            sd = QString::fromLocal8Bit(env);
    }
    if (sd.isEmpty())
        sd = QString::fromStdString(sdcard::default_sdcard_image_path());
    if (!QFileInfo::exists(sd)) {
        std::fprintf(stderr,
            "docshot: no NextZXOS SD image at %s\n"
            "  The debugger screenshots are pictures of a booted machine; there is no\n"
            "  useful picture of one with no ROMs. Provision the default image with\n"
            "    ./build/jnext --headless --sdcard-download-confirm\n"
            "  or point --sdcard / $JNEXT_TEST_SD_IMAGE at an existing one.\n",
            qPrintable(sd));
        return 1;
    }

    // ── A screen with room, so the debugger window opens at the size asked
    // for rather than clamped to an 800x800 default offscreen screen.
    QTemporaryDir scratch;
    if (!scratch.isValid()) {
        std::fprintf(stderr, "docshot: could not create a temporary directory\n");
        return 1;
    }
    const QString qpa_config = scratch.path() + QStringLiteral("/offscreen-screen.json");
    {
        QFile f(qpa_config);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::fprintf(stderr, "docshot: could not write the QPA screen config\n");
            return 1;
        }
        f.write("{\n"
                "  \"screens\": [\n"
                "    { \"name\": \"docshot\", \"x\": 0, \"y\": 0,\n"
                "      \"width\": 3000, \"height\": 2200,\n"
                "      \"logicalDpi\": 96, \"logicalBaseDpi\": 96, \"dpr\": 1 }\n"
                "  ]\n"
                "}\n");
    }
    qputenv("QT_QPA_PLATFORM",
            QStringLiteral("offscreen:configfile=%1").arg(qpa_config).toUtf8());
    // Isolate the debugger's own config: a saved window geometry from the
    // developer's last real session would otherwise decide the size of every
    // image, and this run would overwrite it in passing.
    qputenv("JNEXT_CONFIG_DIR", scratch.path().toUtf8());

    QApplication app(argc, argv);
    // Pin the widget style. Without this the images carry whichever desktop
    // theme the machine that ran the target happens to use, which is the one
    // way two correct runs can still disagree byte for byte.
    if (QStyle* fusion = QStyleFactory::create(QStringLiteral("Fusion")))
        QApplication::setStyle(fusion);

    QDir().mkpath(opt.out_dir);

    // ── Boot ──────────────────────────────────────────────────────────
    note("docshot: booting NextZXOS (%d frames) from %s\n", kBootFrames, qPrintable(sd));
    Emulator emu;
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.sd_card_image        = sd.toStdString();
    cfg.rewind_buffer_frames = kRewindFrames;
    cfg.rtc_fixed            = parse_rtc_datetime(kRtc, cfg.rtc_fixed_tm);
    if (!emu.init(cfg)) {
        std::fprintf(stderr, "docshot: emulator init failed (bad SD image?)\n");
        return 1;
    }
    for (int i = 0; i < kBootFrames; ++i) emu.run_frame();

    // ── Debugger, through the production path ─────────────────────────
    QMainWindow host;
    auto* mgr = new DebuggerManager(&host, &emu, &host);
    mgr->set_enabled(true);                 // == Alt+D
    DebuggerWindow* dbg = mgr->debugger_window_ptr();
    if (!dbg) {
        std::fprintf(stderr, "docshot: the debugger window did not come up\n");
        return 1;
    }
    // Size the window. The WIDTH is pinned (it is what fixes the proportions
    // between the three top-area columns, and so the width of every panel
    // crop). The HEIGHT is not: DebuggerWindow::grow_default_size_to_natural()
    // measures how much panel area the scroll area is hiding and grows into
    // it, and the answer depends on the font metrics of the machine doing the
    // capture. Forcing a height would either fight that measurement or leave
    // the panel area SCROLLED — and a scrolled panel area silently invalidates
    // every crop below, because mapTo() reports a position the render does not
    // show. So: pin the width, let the window find its own height, then grow
    // it until nothing scrolls, and refuse to capture if anything still does.
    settle();                       // let the opening fit run first
    dbg->resize(kWindowW, dbg->height());
    settle();

    auto* scroll = qobject_cast<QScrollArea*>(dbg->centralWidget());
    for (int pass = 0; scroll && pass < 4; ++pass) {
        const int hx = scroll->horizontalScrollBar()->maximum();
        const int vx = scroll->verticalScrollBar()->maximum();
        if (hx == 0 && vx == 0) break;
        dbg->resize(dbg->width() + hx, dbg->height() + vx);
        settle();
    }
    if (scroll && (scroll->horizontalScrollBar()->maximum() != 0 ||
                   scroll->verticalScrollBar()->maximum() != 0)) {
        std::fprintf(stderr,
            "docshot: the panel area still scrolls at %dx%d (h max %d, v max %d);\n"
            "  panel crops would be taken at the wrong offsets.\n",
            dbg->width(), dbg->height(),
            scroll->horizontalScrollBar()->maximum(),
            scroll->verticalScrollBar()->maximum());
        return 1;
    }
    if (dbg->width() != kWindowW)
        note("docshot: window widened to %d so the panel area stops scrolling\n",
             dbg->width());
    note("docshot: debugger window %dx%d\n", dbg->width(), dbg->height());

    mgr->on_pause();                        // == F9
    if (!run_to_last_paper_line(emu, mgr)) return 1;
    settle();

    // Breakpoints and watches go in AFTER the positioning above, not before:
    // an armed breakpoint in the ROM loop the boot sits in would stop the
    // Run to EOF on its first instruction, and the frame would not be drawn.
    apply_debug_fixture(emu, dbg);
    mgr->refresh_panels();   // the MANAGER's — see run_to_last_paper_line()
    settle();

    const auto path = [&](const char* name) {
        return opt.out_dir + QStringLiteral("/") + QString::fromLatin1(name) + QStringLiteral(".png");
    };

    // ── Group 1: the booted machine ───────────────────────────────────
    note("capturing (booted machine):\n");

    if (wanted(opt, "debugger-window"))
        save_crop(dbg, dbg, path("debugger-window"));

    // CPU Registers + MMU: the vertical splitter holding both group boxes.
    // cpu_panel -> its QGroupBox -> the splitter.
    QWidget* cpu_col = nullptr;
    if (auto* cp = dbg->cpu_panel())
        if (QWidget* box = cp->parentWidget()) cpu_col = box->parentWidget();
    if (wanted(opt, "debugger-cpu-mmu"))
        save_crop(dbg, cpu_col, path("debugger-cpu-mmu"));

    // Disassembly: the panel's own QGroupBox, so the crop carries its title.
    QWidget* disasm_box = dbg->disasm_panel() ? dbg->disasm_panel()->parentWidget() : nullptr;
    if (wanted(opt, "debugger-disassembly"))
        save_crop(dbg, disasm_box, path("debugger-disassembly"));

    // The top-left tab widget (Video / Sprites / Copper / NextREG / Audio) and
    // the two bottom ones. Each image is that whole tab widget with a
    // different page current, so the tab bar is part of the picture — which is
    // how the guide's pages refer to them.
    // The Video panel has no accessor on DebuggerWindow, so its tab widget is
    // found from the panel itself — which also skips the Video panel's OWN
    // sub-tabs (All layers / ULA / Layer2 / ...), a QTabWidget one level down.
    QTabWidget* left_tabs   = owning_tab_widget(dbg->findChild<VideoPanel*>());
    QTabWidget* mem_tabs    = owning_tab_widget(dbg->stack_panel());
    QTabWidget* bottom_tabs = owning_tab_widget(dbg->watch_panel());

    auto shoot_tab = [&](QTabWidget* tabs, const char* label, const char* name) {
        if (!wanted(opt, name)) return;
        if (!tabs) { note("  FAIL %s: tab widget not found\n", name); ++g_failed; return; }
        int idx = -1;
        for (int i = 0; i < tabs->count(); ++i)
            if (tabs->tabText(i) == QString::fromLatin1(label)) { idx = i; break; }
        if (idx < 0) {
            note("  FAIL %s: no '%s' tab (the tab set changed)\n", name, label);
            ++g_failed;
            return;
        }
        tabs->setCurrentIndex(idx);
        mgr->refresh_panels();       // only the visible tab renders itself
        settle(40);
        save_crop(dbg, tabs, path(name));
    };

    // Point the Memory panel at $5800 — the boundary between pixel VRAM and
    // the attribute block, and the address its own guide page uses as the
    // example. In CPU View the panel colour-codes VRAM cyan and attributes
    // yellow, so this is the one address at which the picture shows the
    // colour coding the page describes. Where it opens ($0000, the ROM) shows
    // none of it.
    if (mem_tabs) {
        for (int i = 0; i < mem_tabs->count(); ++i) {
            if (mem_tabs->tabText(i) != QStringLiteral("Memory")) continue;
            // Make the page current and let the layout settle FIRST. The panel
            // centres the address over visible_rows(), which it measures from
            // its own height — navigate while the page is still unlaid and the
            // address lands at the bottom edge instead of the middle.
            mem_tabs->setCurrentIndex(i);
            settle(40);
            if (!type_into_address_box(mem_tabs->widget(i), QStringLiteral("5800")))
                note("  WARNING: the Memory panel has no address box any more;\n"
                     "           its image will show wherever the panel opened.\n");
            settle(20);
        }
    }

    shoot_tab(left_tabs,   "Video",       "debugger-video");
    shoot_tab(left_tabs,   "NextREG",     "debugger-nextreg");
    shoot_tab(mem_tabs,    "Memory",      "debugger-memory");
    shoot_tab(mem_tabs,    "Stack",       "debugger-stack");
    shoot_tab(bottom_tabs, "Watches",     "debugger-watches");
    shoot_tab(bottom_tabs, "Breakpoints", "debugger-breakpoints");

    // ── Group 2: with the sprite / Copper / AY fixture applied ────────
    //
    // Strictly after every shot above: this changes what the machine is doing.
    if (wanted(opt, "debugger-sprites") || wanted(opt, "debugger-copper") ||
        wanted(opt, "debugger-audio")) {
        note("capturing (with the sprite / Copper / AY fixture):\n");
        apply_sprite_fixture(emu);
        apply_copper_fixture(emu);
        apply_audio_fixture(emu);
        // One more full frame, so the Copper is genuinely EXECUTING — its PC
        // has advanced into the program and the panel's "Copper Running" state
        // is the machine's, not a claim — and the sprites have been composited
        // at least once.
        //
        // The master switch goes off across that frame and straight back on:
        // the breakpoints installed above sit in the ROM loop the machine is
        // in, so an armed set would stop this frame on its first instruction.
        // Turning them off wholesale (rather than deleting and re-adding them)
        // is exactly what the Breakpoints panel's own master switch is for,
        // and it leaves each row's individual On state untouched — which is
        // what the picture captured above has to keep showing.
        BreakpointSet& bps = emu.debug_state().breakpoints();
        const bool master_was = bps.master_enabled();
        bps.set_master_enabled(false);
        const bool ok = run_to_last_paper_line(emu, mgr);
        bps.set_master_enabled(master_was);
        if (!ok) return 1;
        settle();

        shoot_tab(left_tabs, "Sprites", "debugger-sprites");
        shoot_tab(left_tabs, "Copper",  "debugger-copper");
        shoot_tab(left_tabs, "Audio",   "debugger-audio");
    }

    // Leave the machine running, the way closing the debugger does, so nothing
    // downstream inherits a paused emulator.
    mgr->set_enabled(false, /*prompt_on_corrupt=*/false);

    note("\n%d image%s written to %s", g_saved, g_saved == 1 ? "" : "s",
         qPrintable(opt.out_dir));
    if (g_failed) note(", %d FAILED", g_failed);
    note("\n");
    return g_failed ? 1 : 0;
}
