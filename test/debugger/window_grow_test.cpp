// Debugger window opening-size test (GitHub issue #114), roomy-screen half.
//
// Companion to debugger/window_size_test.cpp, and a SEPARATE BINARY on purpose:
// the offscreen QPA screen geometry is fixed when QApplication is constructed,
// so one process can only ever test one screen size. window_size_test runs on
// the default offscreen screen (800x800) — smaller than the debugger needs,
// which is what makes its clamp rows discriminative. This one runs on a screen
// with ROOM (3000x2200, via the offscreen plugin's configfile), which is the
// only way to reach the other branch.
//
// That branch is the issue's first acceptance criterion:
//
//   "keep the current default dimensions on open (no change for users with
//    room)"
//
// and it is implemented by DebuggerWindow::grow_default_size_to_natural(),
// which measures how much panel area the scroll area is hiding and resizes the
// window by exactly that much (bounded, clamped to the work area). On a screen
// too small to grow into — window_size_test's screen — the first pass clamps to
// the size the window already has and returns, so the resize is never reached
// there. Without this suite the success path of the single most load-bearing
// requirement in the issue would have no automated coverage at all: it was
// proven once, by hand, in a GUI session that will never run again.
//
// Discriminative, measured rather than asserted:
//   * delete the resize() in grow_default_size_to_natural()  -> GR-02 + GR-03
//     fail (window opens 1170x900 with 279px of panel area hidden), and
//     window_size_test stays 21/21 green — that is the coverage hole this file
//     closes;
//   * delete its showEvent trigger                           -> same two fail;
//   * delete the size_is_default_ gate                       -> GR-04 fails
//     (a saved 900x700 window grows to 985x1179).
//
// Against the FULL pre-#114 code (reconstructed: no scroll area, hard 1170
// minimum, old saved-size guard, no clamp) GR-02 and GR-04 fail. GR-03 passes
// there, and does so honestly: the pre-fix window opened 1179 tall because Qt
// enforced the panels' minimum as the window minimum, not because anything
// grew. GR-03's job is to catch the grow being removed from the CURRENT design,
// which is what the mutations above measure.
//
// GR-01 is the fixture's own witness: describing an 800x800 screen in the
// config below instead of 3000x2200 makes it fail (measured), so a screen that
// silently came up small could never be mistaken for a passing run.
//
// Run: ./build/test/debugger_window_grow_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"

#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMainWindow>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

// ── Test infrastructure (mirrors the other debugger suites) ───────────

namespace {

int g_pass  = 0;
int g_fail  = 0;
int g_total = 0;

struct Result {
    std::string id;
    bool        passed;
};

std::vector<Result> g_results;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    ++g_total;
    g_results.push_back(Result{id, cond});
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

// The opening fit runs off the first show and re-measures a bounded number of
// times, each pass through the event loop — so a row that reads the size
// straight after show() reads an intermediate value.
void settle(int ms = 300) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// The default size DebuggerWindow asks for before it grows (kDefaultWidth /
// kDefaultHeight in src/debugger/debugger_window.cpp). The whole point of the
// grow is that the opening window ends up TALLER than this.
constexpr int kDefaultHeight = 900;

bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

struct Fixture {
    Emulator         emu;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;
    DebuggerWindow*  dbg = nullptr;
    bool             ok  = false;

    Fixture() {
        if (!build_next_emulator(emu)) return;
        mgr = new DebuggerManager(&win, &emu, &win);   // parented → auto-freed
        mgr->set_enabled(true);                        // creates + shows the window
        dbg = mgr->debugger_window_ptr();
        ok  = (dbg != nullptr);
        settle();
    }
    ~Fixture() {
        if (mgr) mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
    }

    QScrollArea* scroll() const {
        return dbg ? qobject_cast<QScrollArea*>(dbg->centralWidget()) : nullptr;
    }
};

void write_saved_size(const QString& dir, int w, int h) {
    QDir().mkpath(dir);
    QSettings s(dir + QStringLiteral("/Debugger.conf"), QSettings::IniFormat);
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);
    ds << w << h;
    s.setValue("debugger/size", data);
    s.sync();
}

void clear_saved_geometry(const QString& dir) {
    QFile::remove(dir + QStringLiteral("/Debugger.conf"));
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

    // A screen with room, which the offscreen plugin will hand us if we describe
    // one. 3000x2200 is comfortably larger than any size this window wants.
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
                "    { \"name\": \"roomy\", \"x\": 0, \"y\": 0,\n"
                "      \"width\": 3000, \"height\": 2200,\n"
                "      \"logicalDpi\": 96, \"logicalBaseDpi\": 96, \"dpr\": 1 }\n"
                "  ]\n"
                "}\n");
    }
    qputenv("QT_QPA_PLATFORM",
            QStringLiteral("offscreen:configfile=%1").arg(qpa_config).toUtf8());
    // Isolate the config file: this suite writes saved geometries, and the real
    // ~/.jnext/Debugger.conf belongs to the user.
    qputenv("JNEXT_CONFIG_DIR", dir.path().toUtf8());

    QApplication app(argc, argv);

    const QRect avail = QGuiApplication::primaryScreen()
                            ? QGuiApplication::primaryScreen()->availableGeometry()
                            : QRect();

    // GR-01 — the denominator. Every row below is meaningless if the screen is
    // not actually roomy, and a silently-800x800 screen would make GR-02 pass
    // for the wrong reason on some future Qt. Assert the fixture itself.
    check("GR-01", "the test screen really has room for the window to grow into",
          avail.width() >= 2000 && avail.height() >= 1600,
          fmt("work area %dx%d", avail.width(), avail.height()));

    // Rows on a window opened with NO saved geometry.
    {
        clear_saved_geometry(dir.path());
        Fixture fx;
        if (!fx.ok) {
            check("GR-02", "fixture (emulator + debugger window)", false);
            check("GR-03", "fixture (emulator + debugger window)", false);
        } else {
            QScrollArea* sa = fx.scroll();
            const int hmax = sa ? sa->horizontalScrollBar()->maximum() : -1;
            const int vmax = sa ? sa->verticalScrollBar()->maximum() : -1;

            // GR-02 — THE issue's first acceptance criterion. With room, the
            // window opens showing everything: no panel area is out of view, so
            // a user with a big enough monitor sees exactly what they saw
            // before the window learned to scroll.
            check("GR-02", "with room, the window opens with nothing scrolled out of view",
                  sa != nullptr && hmax == 0 && vmax == 0,
                  sa ? fmt("opened %dx%d, hidden h=%d v=%d",
                           fx.dbg->width(), fx.dbg->height(), hmax, vmax)
                     : std::string("central widget is not a QScrollArea"));

            // GR-03 — and it got there by GROWING. The window asks for 900 of
            // height and the panels need more than that, so an opening height
            // still at 900 means the grow never ran — which is exactly what
            // GR-02 alone cannot distinguish from a panel set that happens to
            // fit. This is the row that fails if resize() is removed from
            // grow_default_size_to_natural().
            check("GR-03", "the opening window grew past the size it initially asked for",
                  fx.dbg->height() > kDefaultHeight,
                  fmt("opened %dx%d, default height %d",
                      fx.dbg->width(), fx.dbg->height(), kDefaultHeight));
        }
    }

    // GR-04 — the gate on the other side: a size the user saved is NOT grown,
    // even on a screen with all the room in the world. A debugger deliberately
    // kept small must stay small, which is the reason the grow is restricted to
    // a default-sized window in the first place.
    {
        write_saved_size(dir.path(), 900, 700);
        Fixture fx;
        check("GR-04", "a saved size is honoured exactly and never grown",
              fx.ok && fx.dbg->width() == 900 && fx.dbg->height() == 700,
              fx.ok ? fmt("saved 900x700, opened %dx%d",
                          fx.dbg->width(), fx.dbg->height())
                    : std::string("fixture failed"));
        clear_saved_geometry(dir.path());
    }

    std::printf("  Group: GR             — done\n");
    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    std::printf("\nPer-group breakdown:\n");
    std::printf("  %-22s %d/%d\n", "GR", g_pass, g_total);

    return g_fail > 0 ? 1 : 0;
}
