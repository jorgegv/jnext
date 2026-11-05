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
#include <QResizeEvent>
#include <QFile>
#include <QMainWindow>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QThread>
#include <QSettings>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "../row_id.h"

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
    report_row_id(id);
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

// GR-08: on the resize event of the fit's SECOND request, put the window back to its FIRST request's
// size, before the pass that request queued runs: the window manager's late answer to an earlier request.
struct EchoFilter : QObject {
    std::vector<QSize> seen;   // the sizes of the window's resize events, in order
    bool fired = false;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::Resize || !o->inherits("DebuggerWindow")) return false;
        const QSize z = static_cast<QResizeEvent*>(e)->size();
        if (!seen.empty() && seen.back() == z) return false;
        seen.push_back(z);
        if (seen.size() == 3 && !fired) {   // [0] the show, [1] the first request, [2] the second
            fired = true;
            QWidget* w = static_cast<QWidget*>(o);
            const QSize first = seen[1];
            QTimer::singleShot(0, w, [w, first]() { w->resize(first); });
        }
        return false;
    }
};

// GR-11: sleep inside the resize event of the fit's FIRST request (the window's 2nd Resize event), longer than
// the fit's grace window, so the grace timer is due before the pass that request queued.
struct StallFilter : QObject {
    int resizes = 0;
    bool stalled = false;
    bool eventFilter(QObject* o, QEvent* e) override {
        if (e->type() != QEvent::Resize || !o->inherits("DebuggerWindow")) return false;
        if (++resizes == 2 && !stalled) {
            stalled = true;
            QThread::msleep(1200);
        }
        return false;
    }
};

// GR-09..11: a DebuggerWindow no window manager ever sees (WA_DontShowOnScreen), so the fit's logic
// is tested alone: a manager's own late answers would spend the echo budget of the rows.
struct BareWindow {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    std::unique_ptr<DebuggerWindow> win;
    bool ok = false;
    BareWindow() {
        if (!build_next_emulator(emu)) return;
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win = std::make_unique<DebuggerWindow>(*backend);
        win->setAttribute(Qt::WA_DontShowOnScreen);
        ok = true;
    }
    // Wait until the size has not changed for 3 consecutive 20 ms polls (bounded at 2 s).
    void wait_passes_done() {
        QSize last = win->size();
        int stable = 0;
        for (int i = 0; i < 100 && stable < 3; ++i) {
            settle(20);
            stable = (win->size() == last) ? stable + 1 : 0;
            last = win->size();
        }
    }
};

struct Fixture {
    Emulator         emu;
    // GH #278 WP2 — the loop owner's backend (QtApp::debugger()), built
    // after init() and declared before the window, so it outlives the manager.
    std::unique_ptr<jnext::dbg::Debugger> backend;
    QMainWindow      win;
    DebuggerManager* mgr = nullptr;
    DebuggerWindow*  dbg = nullptr;
    bool             ok  = false;

    // `wait` = false returns right after the window is shown, for the rows that act while the fit runs.
    explicit Fixture(bool wait = true) {
        if (!build_next_emulator(emu)) return;
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        mgr = new DebuggerManager(&win, *backend, &win);   // parented → auto-freed
        mgr->set_enabled(true);                        // creates + shows the window
        dbg = mgr->debugger_window_ptr();
        ok  = (dbg != nullptr);
        if (!wait) return;
        settle();
        // The fit is a chain of queued passes; on a loaded host 300 ms can end between two of them
        // (GR-05 read Video 599<600 in `make unit-test` with the host at load 16). Keep waiting until
        // the size has not changed for two consecutive 100 ms polls (bounded at 5 s).
        if (ok) {
            QSize last = dbg->size();
            int stable = 0;
            for (int i = 0; i < 50 && stable < 2; ++i) {
                settle(100);
                stable = (dbg->size() == last) ? stable + 1 : 0;
                last = dbg->size();
            }
        }
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
    QFile::remove(dir + QStringLiteral("/jnext.conf"));   // where the window saves its size now (a restored size is not "default")
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
    // JNEXT_TEST_KEEP_QPA (set by the debugger-window-wm-func row): keep the platform the
    // environment names (xcb under a real window manager) instead of forcing offscreen.
    if (qgetenv("JNEXT_TEST_KEEP_QPA").isEmpty())
        qputenv("QT_QPA_PLATFORM",
                QStringLiteral("offscreen:configfile=%1").arg(qpa_config).toUtf8());
    // Isolate the config file: this suite writes saved geometries, and the real
    // ~/.jnext/jnext.conf belongs to the user.
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

    // GR-05 (GH #360) — the default window opens wide enough for its left column. With no saved
    // geometry the window asks for 1170 px (kDefaultWidth) and grows to its natural size on a roomy
    // (3000x2200) screen. At 1170 the left tab widget was 506 px and the Video panel (sizeHint 600)
    // lost its raster labels, palette strip and preview. Oracle: after the opening fit, every page of
    // the left column of tabs is at least as wide as its own sizeHint().width(), with no tolerance.
    {
        clear_saved_geometry(dir.path());
        Fixture fx;
        std::string bad;
        int pages = 0;
        if (fx.ok) {
            for (QTabWidget* tw : fx.dbg->findChildren<QTabWidget*>()) {
                bool left = false;
                for (int i = 0; i < tw->count(); ++i)
                    if (tw->tabText(i) == QLatin1String("Video")) left = true;
                if (!left) continue;
                for (int i = 0; i < tw->count(); ++i) {
                    QWidget* page = tw->widget(i);
                    ++pages;
                    if (page->width() < page->sizeHint().width())
                        bad += fmt("%s %d<%d ", tw->tabText(i).toStdString().c_str(), page->width(),
                                   page->sizeHint().width());
                    // ... and not wider by more than a few px: the widest page that lacked ends at its
                    // sizeHint, so a window that overshoots (33 px with the stretch factors swapped) fails.
                    if (tw->tabText(i) == QLatin1String("Video") && page->width() > page->sizeHint().width() + 4)
                        bad += fmt("Video overshoots %d>%d+4 ", page->width(), page->sizeHint().width());
                }
            }
        }
        check("GR-05",
              "at the default window size, grown to natural on a 3000x2200 screen, every page of the left "
              "column of tabs is at least as wide as its sizeHint (no tolerance) and the Video page at most 4 px wider",
              fx.ok && pages == 7 && bad.empty(),
              fx.ok ? bad + fmt("pages=%d window %dx%d", pages, fx.dbg->width(), fx.dbg->height())
                    : std::string("fixture failed"));
    }

    // GR-06 (GH #360 review) -- a window manager can answer a request of the opening fit LATE (the map
    // answers the pre-map request with the size from before it; openbox applies the first request after
    // later ones). For the fit's own lifetime that stale size is the manager's, not the user's, and must
    // not be the final size: the fit asks its current request again. The window is put back to the size
    // the fit started from (1170 x 900, a size the fit knows) while the fit is still in its grace window,
    // and must return to the size the fit settles on (a plain run's, B).
    QSize B;
    {
        clear_saved_geometry(dir.path());
        Fixture base;
        if (base.ok) B = base.dbg->size();
    }
    {
        clear_saved_geometry(dir.path());
        const QSize echo(1170, kDefaultHeight);
        Fixture fx(false);
        bool returned = false;
        if (fx.ok) {
            QSize last = fx.dbg->size();
            int stable = 0;
            for (int i = 0; i < 100 && stable < 3; ++i) {   // the fit's passes are done: 60 ms unchanged
                settle(20);
                stable = (fx.dbg->size() == last) ? stable + 1 : 0;
                last = fx.dbg->size();
            }
            if (fx.dbg->size() != echo) {
                fx.dbg->resize(echo);
                settle(400);
                returned = fx.dbg->size() == B;
            }
        }
        check("GR-06",
              "a stale answer of the window manager to an earlier request of the opening fit (the window put "
              "back to the size the fit started from, inside the grace window) is asked again: the window "
              "returns to the size the fit settles on",
              fx.ok && B.isValid() && returned,
              fmt("settled B %dx%d, echo %dx%d, now %dx%d", B.width(), B.height(), echo.width(), echo.height(),
                  fx.ok ? fx.dbg->width() : 0, fx.ok ? fx.dbg->height() : 0));
    }

    // GR-07 (GH #360 review) -- the manager-answer handling has an END. Once the fit is over, every size is
    // the user's: a resize to 1170 x 900 (the size the fit started from) and to the two widths just below
    // the settled one (sizes of the fit's last passes) stands, instead of being undone as a late answer.
    {
        clear_saved_geometry(dir.path());
        Fixture fx;
        bool stands = fx.ok && B.isValid();
        std::string seen;
        if (fx.ok) {
            settle(1500);   // the grace window (1 s) is over
            const QSize settled = fx.dbg->size();
            for (const QSize& want : {QSize(1170, kDefaultHeight), QSize(settled.width() - 3, settled.height()),
                                      QSize(settled.width() - 53, settled.height())}) {
                fx.dbg->resize(want);
                settle(300);
                const bool ok1 = fx.dbg->size() == want;
                stands = stands && ok1;
                seen += fmt("%dx%d->%dx%d ", want.width(), want.height(), fx.dbg->width(), fx.dbg->height());
            }
        }
        check("GR-07",
              "after the fit's grace window every size is the user's: a resize to 1170x900 and to the widths 3 and "
              "53 px below the settled one stands",
              stands, seen);
    }

    // GR-08 (GH #360 review) -- the manager's late answer lands while a pass is QUEUED: on the resize event
    // of the second request the window is put back to the first request's size before the pass that request
    // queued runs. That pass must treat it as the manager's (ask again, measure again), not as the user's
    // (end the fit and leave the stale size): the window ends at B.
    {
        clear_saved_geometry(dir.path());
        EchoFilter filter;
        qApp->installEventFilter(&filter);
        QSize now;
        {
            Fixture fx;
            if (fx.ok) now = fx.dbg->size();
        }
        qApp->removeEventFilter(&filter);
        check("GR-08",
              "an earlier request's size landing while a pass is queued is asked again by that pass: the window "
              "ends at the size a plain run settles on",
              filter.fired && B.isValid() && now == B,
              fmt("fired %d, B %dx%d, now %dx%d, events %zu", int(filter.fired), B.width(), B.height(),
                  now.width(), now.height(), filter.seen.size()));
    }

    // GR-09 (GH #451) -- a USER resize while the fit runs ends it and stands. After the first pass has run
    // (and queued the next) the window is resized to a size the fit never asked for: the pass that
    // follows must treat it as the user's, not grow from it. Oracle: grow_default_size_to_natural()
    // ("Not a size of the fit's own: the user's. The fit is over, their size stands") and the developer
    // guide ("any other size is the user's and ends the fit"). A window with no manager.
    {
        clear_saved_geometry(dir.path());
        BareWindow bw;
        QSize after_first, now;
        const QSize user(1234, 567);
        if (bw.ok) {
            bw.win->show();
            QApplication::processEvents();
            after_first = bw.win->size();
            if (after_first != QSize(1170, kDefaultHeight)) {   // the fit has started
                bw.win->resize(user);
                settle(1500);
            }
            now = bw.win->size();
        }
        check("GR-09",
              "a user resize made while the opening fit runs (a size the fit never asked for) ends the fit "
              "and stands",
              bw.ok && after_first != QSize(1170, kDefaultHeight) && now == user,
              fmt("after first pass %dx%d, user %dx%d -> %dx%d", after_first.width(), after_first.height(),
                  user.width(), user.height(), now.width(), now.height()));
    }

    // GR-10 (GH #451) -- each late answer of the manager counts ONCE against kMaxFitEchoes (6): after the
    // passes are done, the window is put back to 1170x900 (a size the fit knows) 7 times inside the grace
    // window; answers 1-6 are asked again (the window returns to the settled size P), the 7th stands.
    // Oracle: the developer guide's "asked again, at most kMaxFitEchoes = 6 times".
    {
        clear_saved_geometry(dir.path());
        BareWindow bw;
        std::string seen;
        bool as_documented = false;
        if (bw.ok) {
            bw.win->show();
            bw.wait_passes_done();
            const QSize P = bw.win->size();
            as_documented = P != QSize(1170, kDefaultHeight);
            for (int k = 1; k <= 7; ++k) {
                bw.win->resize(1170, kDefaultHeight);
                settle(100);
                const bool returned = bw.win->size() == P;
                seen += fmt("%d:%s ", k, returned ? "ret" : "stood");
                if (returned != (k <= 6)) as_documented = false;
            }
        }
        check("GR-10",
              "a late answer of the window manager inside the grace window is asked again at most 6 times "
              "(each answer counts once): answers 1-6 return to the settled size, the 7th stands",
              bw.ok && as_documented, seen);
    }

    // GR-11 (GH #451) -- a stalled event loop must not end the fit between a pass and the pass it queued:
    // the loop sleeps 1200 ms (> the 1 s grace window) in the resize event of the fit's first request, so
    // the grace timer is due before the queued pass. The window must still end at the settled size P.
    // Oracle: GH #114 ("a window opening at its DEFAULT size still opens at the size where nothing has to
    // scroll"); the grace window exists for late answers, not to cut a fit that has a pass pending.
    {
        clear_saved_geometry(dir.path());
        QSize P;
        {
            BareWindow plain;
            if (plain.ok) {
                plain.win->show();
                plain.wait_passes_done();
                settle(1200);   // past the grace window
                P = plain.win->size();
            }
        }
        clear_saved_geometry(dir.path());
        StallFilter stall;
        qApp->installEventFilter(&stall);
        QSize now;
        {
            BareWindow bw;
            if (bw.ok) {
                bw.win->show();
                settle(3000);
                now = bw.win->size();
            }
        }
        qApp->removeEventFilter(&stall);
        check("GR-11",
              "an event loop stalled for longer than the grace window between two passes does not end the "
              "fit with a pass still queued: the window ends at the size a plain run settles on",
              stall.stalled && P.isValid() && P != QSize(1170, kDefaultHeight) && now == P,
              fmt("stalled %d, P %dx%d, now %dx%d", int(stall.stalled), P.width(), P.height(), now.width(),
                  now.height()));
    }

    // GR-12 (GH #451 review) -- the bound holds on the PASS path too. The 6th re-ask queues its follow-up
    // pass; the 7th late answer lands BEFORE that pass runs (posted from the re-ask's resize event), when
    // resizeEvent can no longer queue. The pass is then the only bound: the 7th answer stands, it is
    // not asked again. Oracle: the developer guide's "asked again, at most kMaxFitEchoes = 6 times".
    {
        clear_saved_geometry(dir.path());
        BareWindow bw;
        QSize P, now;
        int fired = 0;
        if (bw.ok) {
            bw.win->show();
            bw.wait_passes_done();
            P = bw.win->size();
            for (int k = 1; k <= 5; ++k) {
                bw.win->resize(1170, kDefaultHeight);
                settle(100);
            }
            struct Late : QObject {
                QSize P; bool armed = true; int fired = 0;
                bool eventFilter(QObject* o, QEvent* e) override {
                    if (e->type() != QEvent::Resize || !o->inherits("DebuggerWindow") || !armed) return false;
                    if (static_cast<QResizeEvent*>(e)->size() == P) {
                        armed = false; ++fired;
                        QWidget* w = static_cast<QWidget*>(o);
                        QTimer::singleShot(0, w, [w]() { w->resize(1170, kDefaultHeight); });
                    }
                    return false;
                }
            } late;
            late.P = P;
            bw.win->resize(1170, kDefaultHeight);   // 6th answer: the pass re-asks P; the filter posts the 7th
            bw.win->installEventFilter(&late);
            settle(300);
            bw.win->removeEventFilter(&late);
            fired = late.fired;
            now = bw.win->size();
        }
        check("GR-12",
              "a 7th late answer landing before the pass queued by the 6th re-ask runs is not asked again: "
              "the bound holds on the pass's own path",
              bw.ok && fired == 1 && P != QSize(1170, kDefaultHeight) && now == QSize(1170, kDefaultHeight),
              fmt("fired %d, P %dx%d, now %dx%d", fired, P.width(), P.height(), now.width(), now.height()));
    }

    // GR-13 (GH #451 review) -- the grace does not end the fit while the pass queued by a RE-ASK is
    // pending. The loop stalls 1200 ms (> the grace window) inside the resize event of the re-ask, before
    // its follow-up pass is queued-and-run; a second late answer afterwards must still be asked again.
    {
        clear_saved_geometry(dir.path());
        BareWindow bw;
        QSize P, now;
        bool stalled = false;
        if (bw.ok) {
            bw.win->show();
            bw.wait_passes_done();
            P = bw.win->size();
            struct StallOnP : QObject {
                QSize P; bool armed = true;
                bool eventFilter(QObject* o, QEvent* e) override {
                    if (e->type() == QEvent::Resize && armed && o->inherits("DebuggerWindow") &&
                        static_cast<QResizeEvent*>(e)->size() == P) { armed = false; QThread::msleep(1200); }
                    return false;
                }
            } f;
            f.P = P;
            bw.win->installEventFilter(&f);
            bw.win->resize(1170, kDefaultHeight);
            settle(400);
            bw.win->removeEventFilter(&f);
            stalled = !f.armed;
            bw.win->resize(1170, kDefaultHeight);
            settle(300);
            now = bw.win->size();
        }
        check("GR-13",
              "a loop stalled past the grace window while the pass queued by a re-ask is pending does not end "
              "the fit: a later late answer is still asked again",
              bw.ok && stalled && P != QSize(1170, kDefaultHeight) && now == P,
              fmt("stalled %d, P %dx%d, now %dx%d", int(stalled), P.width(), P.height(), now.width(), now.height()));
    }

    // GR-14 (GH #451 review) -- the same for the pass queued by a late answer in resizeEvent. A late
    // answer is delivered in the timer phase, in the same batch as the overdue grace timer (a callback
    // registers a 5 ms timer for it, then stalls 1200 ms); the pass it queued runs after that batch.
    // The size is read only after a settle that was started AFTER the stall.
    {
        clear_saved_geometry(dir.path());
        BareWindow bw;
        QSize P, now;
        if (bw.ok) {
            bw.win->show();
            bw.wait_passes_done();
            P = bw.win->size();
            DebuggerWindow* w = bw.win.get();
            QTimer::singleShot(0, w, [w]() {
                QTimer::singleShot(5, w, [w]() { w->resize(1170, kDefaultHeight); });
                QThread::msleep(1200);
            });
            settle(400);
            settle(300);
            now = bw.win->size();
        }
        check("GR-14",
              "a late answer delivered together with a grace timer that is overdue is asked again: the grace "
              "does not end the fit while the pass that answer queued is pending",
              bw.ok && P != QSize(1170, kDefaultHeight) && now == P,
              fmt("P %dx%d, now %dx%d", P.width(), P.height(), now.width(), now.height()));
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
