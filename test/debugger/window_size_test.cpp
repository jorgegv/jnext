// Debugger window sizing test (GitHub issue #114).
//
// No VHDL oracle: this is host window ergonomics, not emulated hardware. The
// oracle is the contract stated in issue #114 (reported from a 1366x768
// Windows 8.1 laptop, where the debugger simply did not fit):
//
//   * the window opens at the same size it always did when the screen has room
//     for it — a user with a big monitor must see no change at all;
//   * but it can be resized SMALLER than the panels' combined minimum size,
//     which it flatly refused to do before (setMinimumWidth(1170) plus the
//     panel minimums made 1170x1069 an absolute floor);
//   * when it is smaller, vertical and horizontal scrollbars pan the panel
//     area, so every panel remains reachable;
//   * the bottom button bar stays visible at all times — never scrolled out of
//     view, never clipped;
//   * and a geometry SAVED on a large monitor and restored on a small one is
//     clamped to the work area, because that is the back door through which the
//     unreachable window would otherwise return.
//
// Two groups:
//   WF  the pure size/position clamp arithmetic (src/debugger/window_attach.h),
//       driven directly with screen rectangles no test machine has to own.
//   DW  the REAL DebuggerWindow, brought up through the production path
//       (DebuggerManager::set_enabled(true)) and resized. Qt is required
//       (the window is a QWidget) but no display: main() forces the offscreen
//       QPA platform, the same idiom as the other debugger suites.
//
// Discriminative: NINE of the ten DW rows fail against the pre-#114 code, which
// was reconstructed and run rather than reasoned about (central widget back to
// the splitter, setMinimumWidth(1170) back, the old `w >= 1170` saved-size
// guard back, the opening clamp removed) — 21 rows, 12 passed, 9 failed:
// DW-01/10 because the window opened, and reopened, larger than the screen;
// DW-02/07 because the central widget WAS the splitter; DW-03 because resize()
// was refused outright (1170x1179); DW-04/05/08 because there was no scroll
// area to produce or withhold scrollbars; DW-09 because a saved size below 1170
// wide was discarded.
//
// DW-06 is the exception, and is recorded as one rather than quietly counted:
// "the button bar is fully inside the window" was vacuously true before the fix
// too, since the window could not shrink at all. It earns its place as the row
// that would catch a FUTURE change clipping the bar, not as evidence about the
// old code.
//
// The one behaviour this suite cannot reach is the window GROWING to its
// natural size on a screen with room: the offscreen screen here is 800x800, so
// the opening fit always clamps to the size it already has. That branch is
// covered by debugger/window_grow_test.cpp, which needs its own process because
// the offscreen screen geometry is fixed when QApplication is constructed.
//
// Run: ./build/test/debugger_window_size_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"
#include "debugger/window_attach.h"

#include <QApplication>
#include <QDataStream>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QFile>
#include <QEventLoop>
#include <QMainWindow>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

using jnext::AttachRect;
using jnext::AttachPlacement;
using jnext::WindowSize;
using jnext::clamp_window_size_to_screen;
using jnext::clamp_window_to_work_area;

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

void set_group(const char* name) { g_group = name; }

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
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

// Let the layout settle: a resize is applied through the event loop, so a row
// that asserts scrollbar ranges immediately after resize() reads stale values.
void settle(int ms = 60) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// ── WF fixtures ───────────────────────────────────────────────────────

// A work area with room to spare, and one the size of a small laptop panel
// (1366x768 minus a 40px task bar — the reporter's machine).
const AttachRect kBigScreen{0, 0, 3840, 2160};
const AttachRect kSmallScreen{0, 0, 1366, 728};
// What the debugger opens at, and the size it needs before anything has to
// scroll (measured: 1170x1069 with this panel set).
constexpr int kDefaultW = 1170;
constexpr int kNaturalH = 1069;
constexpr int kTitleBar = 37;

// ── DW fixture: a headless Next Emulator + the real debugger window ───

bool build_next_emulator(Emulator& emu) {
    EmulatorConfig cfg;
    cfg.type = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    return emu.init(cfg);
}

struct Fixture {
    Emulator         emu;
    // GH #278 WP2 — the loop owner's backend (QtApp::debugger()), built
    // after init() and declared before the window, so it outlives the manager.
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;
    DebuggerWindow*  dbg = nullptr;
    bool             ok  = false;

    Fixture() {
        if (!build_next_emulator(emu)) return;
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);   // parented → auto-freed
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
    /// The bottom button bar.
    QToolBar* button_bar() const {
        if (!dbg) return nullptr;
        for (QToolBar* tb : dbg->findChildren<QToolBar*>())
            if (tb->isVisible()) return tb;
        return nullptr;
    }
};

/// Write a saved size into the isolated Debugger.conf, in the exact format
/// DebuggerWindow::save_geometry() uses.
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

QRect work_area() {
    QScreen* s = QGuiApplication::primaryScreen();
    return s ? s->availableGeometry() : QRect(0, 0, 1024, 768);
}

} // namespace

// ── WF: the pure clamp arithmetic ─────────────────────────────────────

static void test_clamp()
{
    set_group("WF");

    // WF-01 — the promise to users with room: a size that fits is returned
    // untouched. The clamp must only ever shrink.
    {
        WindowSize s = clamp_window_size_to_screen(kDefaultW, kNaturalH, kBigScreen, kTitleBar);
        check("WF-01", "a size that fits the screen is returned unchanged",
              s.w == kDefaultW && s.h == kNaturalH, fmt("%dx%d", s.w, s.h));
    }

    // WF-02 — the report: 1069 of window plus a title bar does not fit a
    // 728px work area, so the height is cut to what does.
    {
        WindowSize s = clamp_window_size_to_screen(kDefaultW, kNaturalH, kSmallScreen, kTitleBar);
        check("WF-02", "a window taller than the work area is shrunk to fit it",
              s.h == kSmallScreen.h - kTitleBar, fmt("%dx%d", s.w, s.h));
    }

    // WF-03 — same for width.
    {
        WindowSize s = clamp_window_size_to_screen(2000, 400, kSmallScreen, kTitleBar);
        check("WF-03", "a window wider than the work area is shrunk to its width",
              s.w == kSmallScreen.w && s.h == 400, fmt("%dx%d", s.w, s.h));
    }

    // WF-04 — the title bar is part of what has to fit: resize() sizes the
    // client area, the window manager adds the bar on top. Without this the
    // window's title bar (and with it any way to move the window) lands under
    // the task bar.
    {
        WindowSize s = clamp_window_size_to_screen(kDefaultW, kNaturalH, kSmallScreen, kTitleBar);
        check("WF-04", "the clamped FRAME — client height plus title bar — fits the work area",
              s.h + kTitleBar <= kSmallScreen.h, fmt("h=%d + deco=%d vs %d", s.h, kTitleBar, kSmallScreen.h));
    }

    // WF-05 — never grows. A window the user deliberately made small must not
    // be inflated to the screen just because there is space.
    {
        WindowSize s = clamp_window_size_to_screen(640, 480, kBigScreen, kTitleBar);
        check("WF-05", "a size smaller than the screen is never grown",
              s.w == 640 && s.h == 480, fmt("%dx%d", s.w, s.h));
    }

    // WF-06 — degenerate input: nothing is known about the screen, so honour
    // what was asked for rather than guessing a size from a zero-sized rect.
    {
        WindowSize s = clamp_window_size_to_screen(kDefaultW, kNaturalH, AttachRect{0, 0, 0, 0}, kTitleBar);
        check("WF-06", "a zero-sized work area leaves the requested size alone",
              s.w == kDefaultW && s.h == kNaturalH, fmt("%dx%d", s.w, s.h));
    }

    // WF-07 — position: a window hanging off the right edge is pulled left
    // until its right edge is inside.
    {
        AttachPlacement p = clamp_window_to_work_area(1200, 100, 400, 300, kSmallScreen, kTitleBar);
        check("WF-07", "a window hanging off the right edge is pulled fully inside",
              p.reposition && p.x + 400 <= kSmallScreen.w && p.x == kSmallScreen.w - 400,
              fmt("x=%d", p.x));
    }

    // WF-08 — same off the bottom, title bar included in the budget.
    {
        AttachPlacement p = clamp_window_to_work_area(100, 700, 400, 300, kSmallScreen, kTitleBar);
        check("WF-08", "a window hanging off the bottom is pulled up, title bar included",
              p.reposition && p.y + 300 + kTitleBar <= kSmallScreen.h, fmt("y=%d", p.y));
    }

    // WF-09 — a work area that does not start at (0,0): a second monitor to the
    // left, or a top panel. The clamp must never park the window above or left
    // of the work area origin.
    {
        const AttachRect offset{1920, 40, 1366, 728};
        AttachPlacement p = clamp_window_to_work_area(1900, 20, 400, 300, offset, kTitleBar);
        check("WF-09", "clamping respects a non-zero work-area origin",
              p.reposition && p.x >= offset.x && p.y >= offset.y, fmt("x=%d y=%d", p.x, p.y));
    }

    // WF-10 — a window still larger than the work area lands AT the origin,
    // never at a negative coordinate: its top-left, and therefore its title
    // bar and its close button, stay reachable.
    {
        AttachPlacement p = clamp_window_to_work_area(200, 200, 4000, 3000, kSmallScreen, kTitleBar);
        check("WF-10", "a window larger than the work area is pinned to its origin",
              p.reposition && p.x == kSmallScreen.x && p.y == kSmallScreen.y, fmt("x=%d y=%d", p.x, p.y));
    }

    // WF-11 — THE issue's back door, end to end: a geometry saved on a 4K
    // monitor, restored on the reporter's laptop. Size then position, the order
    // the window uses, must leave the whole frame inside the work area.
    {
        WindowSize s = clamp_window_size_to_screen(2400, 1600, kSmallScreen, kTitleBar);
        AttachPlacement p = clamp_window_to_work_area(2000, 1200, s.w, s.h, kSmallScreen, kTitleBar);
        const bool inside = p.reposition
                         && p.x >= kSmallScreen.x && p.y >= kSmallScreen.y
                         && p.x + s.w <= kSmallScreen.x + kSmallScreen.w
                         && p.y + s.h + kTitleBar <= kSmallScreen.y + kSmallScreen.h;
        check("WF-11", "a geometry saved on a big monitor lands fully on a small one",
              inside, fmt("%dx%d at %d,%d", s.w, s.h, p.x, p.y));
    }
}

// ── DW: the real debugger window ──────────────────────────────────────

static void test_window(const QString& cfg_dir)
{
    set_group("DW");

    const QRect avail = work_area();

    // Rows on a window opened with NO saved geometry.
    {
        clear_saved_geometry(cfg_dir);
        Fixture fx;
        if (!fx.ok) {
            check("DW-01", "fixture (emulator + debugger window)", false);
            return;
        }

        // DW-01 — the window fits the screen it opened on. Before #114 it
        // opened at its layout minimum (1170x1069) whatever the screen was,
        // which is the whole report.
        check("DW-01", "the window opens no larger than the screen work area",
              fx.dbg->width() <= avail.width() && fx.dbg->height() <= avail.height(),
              fmt("opened %dx%d, work area %dx%d",
                  fx.dbg->width(), fx.dbg->height(), avail.width(), avail.height()));

        // DW-02 — the panel area is inside a scroll area. This is the mechanism
        // the rest of the group depends on.
        QScrollArea* sa = fx.scroll();
        check("DW-02", "the panels live in a resizable scroll area",
              sa != nullptr && sa->widgetResizable() && sa->widget() != nullptr,
              sa ? fmt("resizable=%d widget=%d", sa->widgetResizable(), sa->widget() != nullptr)
                 : std::string("central widget is not a QScrollArea"));

        // DW-03 — THE regression row. 520x380 is far below the old 1170x1069
        // floor; before the fix resize() was silently refused and the window
        // stayed exactly where it was.
        fx.dbg->resize(520, 380);
        settle();
        check("DW-03", "the window can be resized far below the panels' combined minimum",
              fx.dbg->width() == 520 && fx.dbg->height() == 380,
              fmt("%dx%d", fx.dbg->width(), fx.dbg->height()));

        // DW-04 — and being smaller produces BOTH scrollbars, with a real range
        // to pan over: every panel stays reachable.
        if (sa) {
            const int hmax = sa->horizontalScrollBar()->maximum();
            const int vmax = sa->verticalScrollBar()->maximum();
            check("DW-04", "shrinking the window gives both scrollbars a non-zero pan range",
                  hmax > 0 && vmax > 0, fmt("h max=%d v max=%d", hmax, vmax));

            // DW-05 — panning actually moves the view. A scrollbar that cannot
            // be moved would satisfy DW-04 and still leave panels unreachable.
            sa->horizontalScrollBar()->setValue(hmax);
            sa->verticalScrollBar()->setValue(vmax);
            settle();
            check("DW-05", "the view really pans to the far corner of the panel area",
                  sa->horizontalScrollBar()->value() == hmax
                      && sa->verticalScrollBar()->value() == vmax && hmax > 0 && vmax > 0,
                  fmt("h=%d/%d v=%d/%d", sa->horizontalScrollBar()->value(), hmax,
                      sa->verticalScrollBar()->value(), vmax));
        } else {
            check("DW-04", "shrinking the window gives both scrollbars a non-zero pan range", false);
            check("DW-05", "the view really pans to the far corner of the panel area", false);
        }

        // DW-06 — the button bar is fully inside the shrunk window: visible,
        // not clipped, not pushed off the edge.
        QToolBar* bar = fx.button_bar();
        check("DW-06", "the button bar is fully inside the shrunk window",
              bar != nullptr && bar->isVisible() && fx.dbg->rect().contains(bar->geometry()),
              bar ? fmt("bar (%d,%d %dx%d) in window %dx%d",
                        bar->x(), bar->y(), bar->width(), bar->height(),
                        fx.dbg->width(), fx.dbg->height())
                  : std::string("no visible toolbar"));

        // DW-07 — and it is OUTSIDE the scrolled region, so no amount of
        // panning can ever take it out of view. Structural, not incidental:
        // the row above would still pass with the bar scrolled to the top of a
        // scrolled area, this one would not.
        bool bar_outside_scroll = (bar != nullptr && sa != nullptr);
        for (const QWidget* p = bar; p && sa; p = p->parentWidget())
            if (p == sa->widget() || p == sa->viewport()) bar_outside_scroll = false;
        check("DW-07", "the button bar is not inside the scrolled area, so it can never scroll away",
              bar_outside_scroll);

        // DW-08 — control: given room, nothing scrolls. Proves the scroll area
        // did not simply staple permanent scrollbars onto the window.
        if (sa && sa->widget()) {
            const QSize need = sa->widget()->minimumSizeHint();
            fx.dbg->resize(need.width() + 120, need.height() + 320);
            settle();
            check("DW-08", "with room for the panels, neither scrollbar has any range",
                  sa->horizontalScrollBar()->maximum() == 0
                      && sa->verticalScrollBar()->maximum() == 0,
                  fmt("window %dx%d, h max=%d v max=%d",
                      fx.dbg->width(), fx.dbg->height(),
                      sa->horizontalScrollBar()->maximum(),
                      sa->verticalScrollBar()->maximum()));
        } else {
            check("DW-08", "with room for the panels, neither scrollbar has any range", false);
        }
    }

    // DW-09 — a small saved size comes back exactly. The old restore path
    // required `w >= 1170` and threw anything smaller away, so a window the
    // user had deliberately shrunk reopened at full size on the next run.
    {
        const int saved_w = std::min(700, avail.width()  - 40);
        const int saved_h = std::min(520, avail.height() - 40);
        write_saved_size(cfg_dir, saved_w, saved_h);
        Fixture fx;
        check("DW-09", "a saved size below the old 1170 minimum is restored as saved",
              fx.ok && fx.dbg->width() == saved_w && fx.dbg->height() == saved_h,
              fx.ok ? fmt("saved %dx%d, restored %dx%d", saved_w, saved_h,
                          fx.dbg->width(), fx.dbg->height())
                    : std::string("fixture failed"));
    }

    // DW-10 — and a size saved on a monitor bigger than this one is clamped to
    // the work area instead of reopening as a window whose far edge, button bar
    // included, cannot be reached.
    {
        write_saved_size(cfg_dir, 6000, 4000);
        Fixture fx;
        check("DW-10", "a saved size larger than this screen is clamped to the work area",
              fx.ok && fx.dbg->width() <= avail.width() && fx.dbg->height() <= avail.height(),
              fx.ok ? fmt("restored %dx%d, work area %dx%d",
                          fx.dbg->width(), fx.dbg->height(), avail.width(), avail.height())
                    : std::string("fixture failed"));
        clear_saved_geometry(cfg_dir);
    }
}

// ── DCF: GH #312 — one config file ────────────────────────────────────
// Oracle: the issue text (the debugger layout lives in jnext.conf, lowercase
// sections, no "@" values, an older Debugger.conf is read and migrated once)
// and the bytes the real v1.1.15 binary wrote for a 640x456 window (below).
// Legacy fixtures are LITERAL text, not produced by the code under test.

namespace {
const char kLegacyHead[] = "[debugger]\nattached=true\n";
const char kLegacySize[] = R"(size=@ByteArray(\0\0\x2\x80\0\0\x1\xc8))" "\n";   // 640x456
const char kLegacyPos[]  = R"(position=@ByteArray(\0\0\0\x64\0\0\0x))" "\n";    // 100,120

void wipe_dir(const QString& dir) {
    QDir d(dir);
    for (const QString& f : d.entryList(QDir::Files | QDir::Hidden)) d.remove(f);
}
void put(const QString& path, const QByteArray& text) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(text);
}
QByteArray get(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}
bool line_in(const QByteArray& file, const char* line) {
    return file.split('\n').contains(QByteArray(line));
}
} // namespace

static void test_one_file(const QString& dir)
{
    set_group("DCF");
    const QString legacy = dir + "/Debugger.conf";
    const QString conf   = dir + "/jnext.conf";
    const QRect avail = work_area();

    // DCF-01 — the window path, with no direct migration call: a v1.1.15
    // Debugger.conf is honoured when the debugger opens.
    {
        wipe_dir(dir);
        put(legacy, QByteArray(kLegacyHead) + kLegacySize);
        Fixture fx;
        const int ew = std::min(640, avail.width()), eh = std::min(456, avail.height());
        check("DCF-01", "a v1.1.15 Debugger.conf (@ByteArray 640x456) is read by the window",
              fx.ok && fx.dbg->width() == ew && fx.dbg->height() == eh,
              fx.ok ? fmt("%dx%d", fx.dbg->width(), fx.dbg->height()) : std::string("fixture failed"));
    }

    // DCF-02 — direct migration of all three keys.
    {
        wipe_dir(dir);
        put(legacy, QByteArray("[debugger]\nattached=false\n") + kLegacySize + kLegacyPos);
        const bool r = DebuggerWindow::migrate_legacy_config();
        const QByteArray c = get(conf);
        check("DCF-02", "migration: attached, size \"640, 456\", position \"100, 120\"; Debugger.conf gone; no =@",
              r && !QFileInfo::exists(legacy) && line_in(c, "[debugger]")
                  && line_in(c, "attached=false") && line_in(c, "size=640, 456")
                  && line_in(c, "position=100, 120") && !c.contains("=@"),
              c.toStdString());
    }

    // DCF-03 — nothing else in jnext.conf is lost; Debugger.conf wins.
    {
        wipe_dir(dir);
        put(conf, "[debugger]\nsize=800, 600\n\n[debugger_keys]\nstep_over=F10\n\n[startup]\ncpu_speed=2\n");
        put(legacy, QByteArray(kLegacyHead) + kLegacySize);
        DebuggerWindow::migrate_legacy_config();
        const QByteArray c = get(conf);
        check("DCF-03", "migration keeps [startup] and [debugger_keys]; Debugger.conf's size wins",
              line_in(c, "cpu_speed=2") && line_in(c, "step_over=F10")
                  && line_in(c, "size=640, 456") && !line_in(c, "size=800, 600"),
              c.toStdString());
    }

    // DCF-04 — idempotent: nothing to migrate leaves jnext.conf untouched.
    {
        const QByteArray before = get(conf);
        const bool r = DebuggerWindow::migrate_legacy_config();
        check("DCF-04", "a second migration (no Debugger.conf) returns false and changes nothing",
              !r && get(conf) == before && !before.isEmpty());
    }

    // DCF-05 — a fresh run leaves exactly one file, readable.
    {
        wipe_dir(dir);
        {
            Fixture fx;
            if (fx.ok) { fx.dbg->resize(std::min(700, avail.width() - 40),
                                        std::min(520, avail.height() - 40)); settle(); }
        }
        const QStringList files = QDir(dir).entryList(QDir::Files | QDir::Hidden);
        const QByteArray c = get(conf);
        const int ew = std::min(700, avail.width() - 40), eh = std::min(520, avail.height() - 40);
        check("DCF-05", "a fresh debugger run writes only jnext.conf: lowercase [debugger], \"W, H\", no =@",
              files == QStringList{"jnext.conf"} && line_in(c, "[debugger]")
                  && line_in(c, QByteArray("size=" + QByteArray::number(ew) + ", " + QByteArray::number(eh)).constData())
                  && !c.contains("=@"),
              files.join(",").toStdString() + " / " + c.toStdString());
    }

    // DCF-06 — a corrupt (2-byte) legacy blob is dropped, the default size is used.
    {
        wipe_dir(dir);
        put(legacy, QByteArray(kLegacyHead) + R"(size=@ByteArray(\x1\x2))" + "\n");
        Fixture fx;
        // Read before the window's own teardown save adds a size.
        QSettings st(conf, QSettings::IniFormat);
        check("DCF-06", "a corrupt legacy size blob is not migrated; the window opens at a real size",
              fx.ok && !st.contains("debugger/size") && !QFileInfo::exists(legacy)
                  && fx.dbg->width() > 0 && fx.dbg->height() > 0,
              get(conf).toStdString());
    }

    // DCF-07 — jnext.conf cannot be written (a directory sits where the file
    // should be, which fails for root too): Debugger.conf must survive. The
    // directory itself stays writable, so the removal of Debugger.conf WOULD
    // succeed if migrate_legacy_config() wrongly went ahead.
    {
        wipe_dir(dir);
        QDir().mkpath(conf);
        put(legacy, QByteArray(kLegacyHead) + kLegacySize);
        const bool r = DebuggerWindow::migrate_legacy_config();
        check("DCF-07", "when jnext.conf cannot be written, Debugger.conf is kept",
              !r && QFileInfo::exists(legacy));
        QDir().rmdir(conf);
        wipe_dir(dir);
    }

    // A window opened on jnext.conf text `text`; geometry as the window shows it.
    struct Geo { bool ok; int x, y, w, h; };
    auto open_with = [&](const char* text) {
        wipe_dir(dir);
        // QSettings reuses a parsed file when its size and mtime are unchanged,
        // and two of these texts have the same length: pad each one differently.
        static int n = 0;
        put(conf, QByteArray(text) + "; " + QByteArray(++n, 'p') + "\n");
        Fixture fx;
        if (!fx.ok) return Geo{false, 0, 0, 0, 0};
        return Geo{true, fx.dbg->x(), fx.dbg->y(), fx.dbg->width(), fx.dbg->height()};
    };

    // DCF-08 — "Debugger.conf wins" holds for `attached` too, not only `size`.
    {
        wipe_dir(dir);
        put(conf, "[debugger]\nattached=true\n");
        put(legacy, "[debugger]\nattached=false\n");
        DebuggerWindow::migrate_legacy_config();
        const QByteArray c = get(conf);
        check("DCF-08", "migration: Debugger.conf's attached=false wins over jnext.conf's attached=true",
              line_in(c, "attached=false") && !line_in(c, "attached=true"), c.toStdString());
    }

    // DCF-09 — a detached window's position is written as plain "X, Y".
    {
        wipe_dir(dir);
        put(conf, "[debugger]\nattached=false\nsize=400, 300\n");
        {
            Fixture fx;
            if (fx.ok) { fx.dbg->move(130, 90); settle(); }
        }
        const QByteArray c = get(conf);
        check("DCF-09", "a detached window saves position=X, Y (two integers, no =@)",
              QRegularExpression("(^|\\n)position=-?[0-9]+, -?[0-9]+(\\n|$)")
                  .match(QString::fromUtf8(c)).hasMatch() && !c.contains("=@"),
              c.toStdString());
    }

    // DCF-10..13 — the position/size READ path. The baseline is the same
    // detached window with no position saved; each input must either move the
    // window to (120,100) (the valid one) or leave it where the baseline is.
    {
        const Geo base = open_with("[debugger]\nattached=false\nsize=400, 300\n");
        const Geo good = open_with("[debugger]\nattached=false\nsize=400, 300\nposition=120, 100\n");
        check("DCF-10", "a detached window opens at the saved position=120, 100",
              base.ok && good.ok && good.x == 120 && good.y == 100
                  && !(base.x == 120 && base.y == 100),
              fmt("base %d,%d good %d,%d", base.x, base.y, good.x, good.y));
        const Geo bad = open_with("[debugger]\nattached=false\nsize=400, 300\nposition=120, abc\n");
        check("DCF-11", "a non-numeric position (\"120, abc\") is ignored: default placement",
              bad.ok && bad.x == base.x && bad.y == base.y,
              fmt("base %d,%d bad %d,%d", base.x, base.y, bad.x, bad.y));
        const Geo three = open_with("[debugger]\nattached=false\nsize=400, 300\nposition=120, 100, 7\n");
        check("DCF-12", "a three-element position is ignored: default placement",
              three.ok && three.x == base.x && three.y == base.y,
              fmt("base %d,%d three %d,%d", base.x, base.y, three.x, three.y));
        const Geo size3 = open_with("[debugger]\nattached=false\nsize=400, 300, 9\n");
        check("DCF-13", "a three-element size is ignored: the window does not open at 400x300",
              size3.ok && base.ok && !(size3.w == 400 && size3.h == 300),
              fmt("%dx%d", size3.w, size3.h));
    }

    // DCF-14 — a legacy blob LONGER than two ints is read for its first two
    // (the v1.1.15 reader did exactly that), not dropped.
    {
        wipe_dir(dir);
        put(legacy, QByteArray(kLegacyHead)
                        + R"(size=@ByteArray(\0\0\x2\x80\0\0\x1\xc8\0\0\0\0))" + "\n");
        DebuggerWindow::migrate_legacy_config();
        const QByteArray c = get(conf);
        check("DCF-14", "a 12-byte legacy size blob migrates its first two ints (640, 456)",
              line_in(c, "size=640, 456"), c.toStdString());
    }

    // DCF-15 — an UNREADABLE Debugger.conf is kept, not deleted unmigrated. A
    // symlink to a directory is unreadable for root too (a chmod 000 is not),
    // and QFile::remove() would delete the link, so the row discriminates.
    {
        wipe_dir(dir);
        const QString target = dir + "/legacy-target-dir";
        QDir().mkpath(target);
        const bool linked = QFile::link(target, legacy);
        const bool r = DebuggerWindow::migrate_legacy_config();
        const bool kept = QFileInfo(legacy).isSymLink();
        QFile::remove(legacy);   // the link itself
        wipe_dir(dir);
        QDir().rmdir(target);
        check("DCF-15", "an unreadable Debugger.conf is left in place and not reported migrated",
              linked && !r && kept, fmt("linked=%d r=%d kept=%d", linked, r, kept));
    }
}

int main(int argc, char** argv)
{
    // The debugger window owns QWidgets, so a QApplication is required — but
    // not a display: force the offscreen QPA platform.
    qputenv("QT_QPA_PLATFORM", "offscreen");

    // Isolate the config file: these rows write saved geometries, and the real
    // ~/.jnext/Debugger.conf belongs to the user.
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());

    QApplication app(argc, argv);

    test_clamp();
    std::printf("  Group: WF             — done\n");
    test_window(cfg.path());
    std::printf("  Group: DW             — done\n");
    test_one_file(cfg.path());
    std::printf("  Group: DCF            — done\n");

    std::printf("\n=====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    std::printf("\nPer-group breakdown:\n");
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
