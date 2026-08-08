// ===========================================================================
// Main-window scale and fullscreen geometry suite (GitHub issues #236, #242).
//
// No VHDL oracle: this is host window sizing, not emulated hardware. The
// oracle is the project's own rule for the windowed viewport — the emulator
// picture occupies EmulatorWidget::NATIVE_W x DISPLAY_H (640x512, the
// square-pixel 4:3 form of the 640x256 framebuffer, G104 Phase 7) multiplied
// by the integer scale, and the window is exactly that plus the chrome
// (menu bar, toolbar, status bar).
//
// WHAT #236 REPORTED. On a 2560x1440 Wayland desktop, starting jnext with a
// saved window scale of 2x gave a window whose picture was CLIPPED, while
// starting at 1x and then choosing 2x from the menu gave the right one. Two
// paths into the same MainWindow::set_scale(), two different windows.
//
// THE MECHANISM, measured rather than reasoned about. Reproduced with the
// real binary on an Xvfb 2560x1440 screen: startup-at-2x produced a
// 1280x960 window against the menu path's 1280x1109, and startup-at-3x
// produced 1706x960 — 2560*2/3 by 1440*2/3. Those two numbers name the
// culprit outright: QWidget::adjustSize(), which is documented to clamp a
// WINDOW to two thirds of the screen, and which apply_fixed_window_size()
// used to call before locking the result in with setFixedSize(size()).
// (1440*2/3 == 960 is also exactly the client height measured off the
// screenshot attached to the issue.)
//
// The clamp is normally invisible, and that is why it survived so long.
// adjustSize() also activates the layout, QLayout::SetDefaultConstraint makes
// the activation re-impose the layout's minimum on the window, and resize()
// then clamps the too-small size back UP to it. apply_fixed_window_size()
// clears that minimum first, so the rescue only happens when the
// activation actually does work — and it does not when the layout is already
// clean. Which is precisely the startup path: the scale is re-applied a
// second time with the value it already has, so EmulatorWidget::set_scale()
// re-sets a fixed size that is not changing, and a fixed-size child's
// updateGeometry() does NOT invalidate its parent layout
// (QWidgetPrivate::updateGeometry_helper skips that when minw==maxw &&
// minh==maxh). Nothing dirties the layout, activate() is a no-op, the
// minimum stays at the 0 we set, and the clamped size is locked in.
//
// The fix is to take the size from sizeHint() — the same layout size, with no
// screen clamp anywhere near it.
//
// WHAT THIS SUITE DRIVES. Only product code. The startup rows reproduce
// main.cpp's ordering exactly — construct, show(), apply_startup_config() —
// and then pump frames through the REAL EmulatorWidget, which is what arms
// its devicePixelRatio one-shot and makes it emit scale_changed, which is
// MainWindow's own second call into apply_fixed_window_size(). Nothing here
// imitates QtApp::init's 100 ms re-apply timer: that timer triggers the same
// bug, but so does the widget's one-shot, and a suite that re-implemented the
// timer would be testing its own copy of the wiring. Measured: with the
// timer left out entirely, the pre-fix code still fails these rows.
//
// THE SCREEN IS PART OF THE FIXTURE. The clamp is only reachable when the
// window wants more than two thirds of the screen, so the suite describes a
// 1280x1024 screen to the offscreen QPA plugin (the debugger_window_grow_test
// idiom). Two thirds of that is 853x682: a 1x window (640 wide) is under it
// and a 2x/3x window is over it, which is what makes GH236-02 and GH236-05
// honest controls rather than duplicates. GH236-01 asserts that arrangement,
// so a future Qt or plugin that handed the suite a roomy screen could not let
// the rows pass for the wrong reason.
//
// Discriminative — the mutation applied to the product and then reverted from
// a file copy: apply_fixed_window_size() back to `adjustSize();
// setFixedSize(size());` turns GH236-03, -04, -06, -07 and -09 red and leaves
// -01, -02, -05 and -08 green. The four that stay green are controls, each
// with its own stated job on the row; they are not padding.
//
// GH #242 — THE FULLSCREEN ROUND TRIP, a separate defect that was found here
// while measuring #236 and is causally independent of it: reverting #236's
// sizeHint() fix and removing #242's restore TOGETHER still collapses the
// window, so the old adjustSize() code carried the same bug. Entering
// fullscreen releases the viewport with setFixedSize(QWIDGETSIZE_MAX, ...),
// which Qt reads as no constraint at all, and EmulatorWidget implements no
// sizeHint() — so on the way back out the layout had nothing to size itself
// from. GH242-01 pins before == after across an F11 round trip.
//
// The collapsed figure depends on the surrounding code state, so both numbers
// this project's #236 notes recorded are real and neither is the other's
// correction: 239x100 offscreen (235x100 with the real binary) against the
// pre-#236 adjustSize() path, and 239x83 offscreen against the current
// sizeHint() path with the restore removed. The row asserts before == after,
// not any literal, so it is indifferent to which.
//
// Discriminative on its own: removing the set_scale() restore from
// toggle_fullscreen()'s exit branch turns GH242-01 red and leaves the nine
// GH236 rows green.
//
// WHAT IT DOES NOT PROVE. It runs on the offscreen QPA, so it says nothing
// about a real compositor's own opinion of the geometry it is handed — a
// Wayland compositor may refuse a window larger than the output, and this
// suite cannot see that. The real-binary Xvfb runs above are what stand
// behind the claim for X11; the reporter's Wayland desktop was not available.
// It also fixes devicePixelRatio at 1: the Hi-DPI arithmetic in
// EmulatorWidget::set_scale() was checked separately by running the real
// binary under QT_SCALE_FACTOR=2 (viewport 640/1280/1920 physical pixels wide
// at 1x/2x/3x, chrome a constant 170 physical), not here — the offscreen
// plugin's `dpr` field did not move devicePixelRatioF() off 1.0 (measured),
// so a DPR row in this file would assert nothing.
//
// Run: ./build/test/window_scale_test
// ===========================================================================

#include "gui/app_config.h"
#include "gui/emulator_widget.h"
#include "gui/main_window.h"

#include <QApplication>
#include <QFile>
#include <QScreen>
#include <QTemporaryDir>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s: %s", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
    }
    if (!detail.empty()) std::printf(" [%s]", detail.c_str());
    std::printf("\n");
}

std::string fmt(const char* fmt_str, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

// The emulator's own frame push, which is the only thing that arms
// EmulatorWidget's devicePixelRatio one-shot. Interleaved with event
// delivery so the posted LayoutRequest lands — a clean layout is half of the
// bug's precondition, and a pump that never delivered it would hide it.
void pump(MainWindow& w, const std::vector<uint32_t>& fb) {
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents();
        w.emulator_widget()->update_frame(fb.data(),
                                          EmulatorWidget::NATIVE_W,
                                          EmulatorWidget::NATIVE_H);
        QCoreApplication::processEvents();
    }
}

struct Geom {
    int win_w = 0, win_h = 0;   ///< window size, logical pixels
    int vp_w  = 0, vp_h  = 0;   ///< viewport size, PHYSICAL pixels
};

Geom read(MainWindow& w) {
    EmulatorWidget* ew = w.emulator_widget();
    const qreal dpr = ew->devicePixelRatioF();
    return Geom{ w.width(), w.height(),
                 qRound(ew->width() * dpr), qRound(ew->height() * dpr) };
}

// main.cpp's ordering: QtApp::init() constructs and shows the window, then
// main.cpp pushes the saved preferences into it, then the frames start.
Geom startup_path(int scale, const std::vector<uint32_t>& fb) {
    MainWindow w;
    w.show();
    AppConfigData cfg;
    cfg.window_scale = scale;
    w.apply_startup_config(cfg);
    pump(w, fb);
    return read(w);
}

// The View menu / F2 on a window that is already up and running at 1x.
Geom runtime_path(int scale, const std::vector<uint32_t>& fb) {
    MainWindow w;
    w.show();
    AppConfigData cfg;
    cfg.window_scale = EmulatorWidget::MIN_SCALE;
    w.apply_startup_config(cfg);
    pump(w, fb);
    w.set_scale(scale);
    pump(w, fb);
    return read(w);
}

} // namespace

int main(int argc, char** argv)
{
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("  FAIL: could not create a temporary directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }

    // A screen SMALL enough that the two-thirds clamp is reachable — see the
    // header. The offscreen plugin takes the geometry from a config file, and
    // fixes it at QApplication construction.
    const QString qpa_config = dir.path() + QStringLiteral("/offscreen-screen.json");
    {
        QFile f(qpa_config);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            std::printf("  FAIL: could not write the QPA screen config\n");
            std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
            return 1;
        }
        f.write("{\n"
                "  \"screens\": [\n"
                "    { \"name\": \"small\", \"x\": 0, \"y\": 0,\n"
                "      \"width\": 1280, \"height\": 1024,\n"
                "      \"logicalDpi\": 96, \"logicalBaseDpi\": 96, \"dpr\": 1 }\n"
                "  ]\n"
                "}\n");
    }
    qputenv("QT_QPA_PLATFORM",
            QStringLiteral("offscreen:configfile=%1").arg(qpa_config).toUtf8());
    // MainWindow's constructor reads the real ~/.jnext/jnext.conf otherwise,
    // and the saved window scale is exactly what this suite is about.
    qputenv("JNEXT_CONFIG_DIR", dir.path().toUtf8());

    QApplication app(argc, argv);

    std::printf("Issues #236 / #242 - window scale and fullscreen geometry\n");
    std::printf("=========================================================================\n\n");

    const std::vector<uint32_t> fb(
        static_cast<size_t>(EmulatorWidget::NATIVE_W) * EmulatorWidget::NATIVE_H,
        0xFF102030u);

    const QRect screen = QGuiApplication::primaryScreen()
                             ? QGuiApplication::primaryScreen()->geometry()
                             : QRect();
    const int clamp_w = screen.width()  * 2 / 3;
    const int clamp_h = screen.height() * 2 / 3;

    // ── GH236-01 — the fixture's own witness ─────────────────────────
    // Every row below is about a clamp that only exists when the window wants
    // more than two thirds of the screen. Assert the screen actually puts 2x
    // over that line and leaves 1x under it, so the controls stay controls.
    {
        const int vp1_w = EmulatorWidget::NATIVE_W;
        const int vp1_h = EmulatorWidget::DISPLAY_H;
        const bool reachable_at_2x = (2 * vp1_w > clamp_w) || (2 * vp1_h > clamp_h);
        const bool clear_at_1x     = (vp1_w <= clamp_w) && (vp1_h <= clamp_h);
        check("GH236-01",
              "the test screen makes Qt's 2/3-of-screen window clamp reachable at 2x, not at 1x",
              reachable_at_2x && clear_at_1x,
              fmt("screen %dx%d, 2/3 = %dx%d, 1x viewport %dx%d, 2x viewport %dx%d",
                  screen.width(), screen.height(), clamp_w, clamp_h,
                  vp1_w, vp1_h, 2 * vp1_w, 2 * vp1_h));
    }

    // ── GH236-02..04 — the startup path encloses its own viewport ────
    // The reported symptom was a clipped picture: a window smaller than the
    // fixed-size viewport it contains. 1x is the control (under the clamp,
    // and green before the fix too).
    Geom start[EmulatorWidget::MAX_SCALE + 1];
    Geom runtime[EmulatorWidget::MAX_SCALE + 1];
    for (int s = EmulatorWidget::MIN_SCALE; s <= EmulatorWidget::MAX_SCALE; ++s) {
        start[s]   = startup_path(s, fb);
        runtime[s] = runtime_path(s, fb);
    }

    static const char* enclose_id[] = { nullptr, "GH236-02", "GH236-03", "GH236-04" };
    for (int s = EmulatorWidget::MIN_SCALE; s <= EmulatorWidget::MAX_SCALE; ++s) {
        const int need_w = EmulatorWidget::NATIVE_W  * s;
        const int need_h = EmulatorWidget::DISPLAY_H * s;
        check(enclose_id[s],
              fmt("a window started at the saved scale %dx encloses its whole viewport", s).c_str(),
              start[s].win_w >= need_w && start[s].win_h >= need_h,
              fmt("window %dx%d, viewport needs %dx%d",
                  start[s].win_w, start[s].win_h, need_w, need_h));
    }

    // ── GH236-05..07 — the two paths agree, which IS the report ──────
    static const char* agree_id[] = { nullptr, "GH236-05", "GH236-06", "GH236-07" };
    for (int s = EmulatorWidget::MIN_SCALE; s <= EmulatorWidget::MAX_SCALE; ++s) {
        check(agree_id[s],
              fmt("starting at %dx and switching to %dx give the same window", s, s).c_str(),
              start[s].win_w == runtime[s].win_w && start[s].win_h == runtime[s].win_h,
              fmt("startup %dx%d vs menu %dx%d",
                  start[s].win_w, start[s].win_h,
                  runtime[s].win_w, runtime[s].win_h));
    }

    // ── GH236-08 — the viewport itself was never the wrong size ──────
    // Green before the fix as well, and deliberately kept: it is what pins
    // "the picture is drawn at an exact integer multiple of 640x512 physical
    // pixels", and it is what separates the window being wrong (what #236
    // was) from the picture being wrong (what it was not).
    {
        bool ok = true;
        std::string detail;
        for (int s = EmulatorWidget::MIN_SCALE; s <= EmulatorWidget::MAX_SCALE; ++s) {
            const int want_w = EmulatorWidget::NATIVE_W  * s;
            const int want_h = EmulatorWidget::DISPLAY_H * s;
            if (start[s].vp_w != want_w || start[s].vp_h != want_h) {
                ok = false;
                detail += fmt("%dx: %dx%d want %dx%d; ",
                              s, start[s].vp_w, start[s].vp_h, want_w, want_h);
            }
        }
        check("GH236-08",
              "the viewport is exactly 640x512 physical pixels per scale step",
              ok, ok ? "1x, 2x, 3x all exact" : detail);
    }

    // ── GH236-09 — the window is the viewport plus a constant chrome ──
    // Stronger than GH236-02..04, and the row that would catch a window
    // clamped on ONE axis or grown by a stray margin: the chrome does not
    // depend on the scale, so window - viewport must be the same at 1x, 2x
    // and 3x, and must add nothing at all horizontally.
    {
        const int chrome_h = start[1].win_h - EmulatorWidget::DISPLAY_H;
        bool ok = true;
        std::string detail;
        for (int s = EmulatorWidget::MIN_SCALE; s <= EmulatorWidget::MAX_SCALE; ++s) {
            const int this_chrome_h = start[s].win_h - EmulatorWidget::DISPLAY_H * s;
            const int this_chrome_w = start[s].win_w - EmulatorWidget::NATIVE_W * s;
            if (this_chrome_h != chrome_h || this_chrome_w != 0) {
                ok = false;
                detail += fmt("%dx: window %dx%d leaves chrome %d wide %d high; ",
                              s, start[s].win_w, start[s].win_h,
                              this_chrome_w, this_chrome_h);
            }
        }
        check("GH236-09",
              "window == viewport + a chrome that does not depend on the scale",
              ok, ok ? fmt("chrome 0 wide, %d high at every scale", chrome_h)
                     : detail);
    }

    // ── GH242-01 — the fullscreen round trip ─────────────────────────
    // Leaving fullscreen must give back the window that entered it, at every
    // scale. See the GH #242 note in the header: independent of #236's fix,
    // and red on its own when the restore is removed.
    {
        bool ok = true;
        std::string detail;
        for (int s = EmulatorWidget::MIN_SCALE; s <= EmulatorWidget::MAX_SCALE; ++s) {
            MainWindow w;
            w.show();
            AppConfigData cfg;
            cfg.window_scale = s;
            w.apply_startup_config(cfg);
            pump(w, fb);
            const Geom before = read(w);
            w.toggle_fullscreen();
            pump(w, fb);
            w.toggle_fullscreen();
            pump(w, fb);
            const Geom after = read(w);
            if (before.win_w != after.win_w || before.win_h != after.win_h) {
                ok = false;
                detail += fmt("%dx: %dx%d -> fullscreen -> %dx%d; ",
                              s, before.win_w, before.win_h,
                              after.win_w, after.win_h);
            }
        }
        check("GH242-01",
              "leaving fullscreen restores the exact windowed size at every scale",
              ok, ok ? "1x, 2x, 3x all restored" : detail);
    }

    std::printf("\n=========================================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
