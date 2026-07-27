// Debugger accelerator-collision suite (GitHub issue #124).
//
// No VHDL oracle: this is host UI wiring, not emulated hardware. The oracle is
// Qt's accelerator model plus what the running debugger actually did with it,
// measured rather than reasoned about.
//
// WHAT ISSUE #124 REPORTED, AND WHAT IS REALLY GOING ON
//
// Two menu-bar titles carried the same mnemonic: "&Watches" and "&Window",
// both Alt+W (debugger_window.cpp:564 and :573). The issue assumed one of them
// "silently never fires". Qt does something worse than that: QShortcutMap
// classifies two identical sequences as AMBIGUOUS and dispatches them
// ROUND-ROBIN, so Alt+W opened Watches on the 1st, 3rd, 5th... press and Window
// on the 2nd, 4th, 6th. Measured on the offscreen AND the xcb platform, four
// presses each, same result both times. So both menus fired, neither could be
// opened on purpose, and which one appeared depended on how many times Alt+W
// had been pressed since the window opened.
//
// Sweeping the rest of the debugger found the same defect four more times, all
// of them equally unguarded (the mnemonic that loses is listed second):
//
//   <menu bar>    Alt+W   "&Watches"                    / "&Window"
//   Debug         Alt+B   "Pause / &Break"              / "Step &Back"
//   Debug         Alt+F   "|< &Frame Back"              / "Run to End of &Frame"
//   Debug         Alt+T   "Step Ou&t"                   / "&Trace"
//   Breakpoints   Alt+W   "Add &Write Breakpoint..."    / "Add Read/&Write Breakpoint..."
//
// THE SCOPES QT ACTUALLY USES — which is what makes this checkable
//
//   * menu-bar titles and any QAbstractButton / buddy-QLabel mnemonic share ONE
//     window-wide Alt namespace (Qt::WindowShortcut context);
//   * the items inside one QMenu popup share a namespace of their own, live
//     only while that popup is open, and so may legitimately reuse a letter
//     another menu uses;
//   * QKeySequence shortcuts (F5, Shift+F7, ...) are a third, window-wide
//     namespace.
//
// Groups:
//   AC  walks the REAL DebuggerWindow's accelerators and asserts uniqueness in
//       each of those namespaces. This is the guard the defect class never had
//       — Alt+W was one instance of it, and nothing would have caught the next.
//   AK  presses the keys, through the real platform key path, and asserts the
//       menu that opens is the one asked for and the SAME one every time.
//
// Discriminative — each row was mutation-tested against the product, one
// mutation at a time:
//   AC-01  "&Window" put back on Alt+W  -> fails, naming both claimants
//   AC-02  each of the four in-menu collisions put back, separately -> fails
//   AC-03  a second action given F2                                 -> fails
//          ... and an action given a colliding ALTERNATE sequence via
//          setShortcuts({F3, F5}) -> fails too. That second mutation is the
//          reason the harvest reads shortcuts() rather than shortcut(): run
//          against the singular harvest it PASSES, silently, because F3 is
//          still the primary. Both directions were run.
//   AC-04  a bottom-bar QPushButton given "&Map"                    -> fails
//   AC-05  one extra menu item added                                -> fails
//   AK-02  "&Window" put back on Alt+W -> fails with the round-robin in the
//          detail: "four presses opened: Watches, Window, Watches, Window"
//   AK-03  same mutation -> Alt+N opens "<none>"
//   AK-01  Alt+W handed to Window and Alt+A to Watches (a collision-FREE
//          swap, so AC-01 stays green) -> fails
// Note AK-01 is not a #124 regression row: the broken code opened Watches on
// an odd press too. It is recorded as one rather than quietly counted, and
// earns its place by pinning WHICH of the two menus keeps W — the mutation
// above is a change AC-01 alone would wave through.
//
// Deliberately debugger-scoped: src/gui/main_window.cpp has its own menu bar in
// its own window, and host hotkeys are moving into its Alt namespace under
// issue #115. Nothing here reads or constrains that file.
//
// Run: ./build/test/debugger_accel_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debugger/debugger_manager.h"
#include "debugger/debugger_window.h"

#include <QAbstractButton>
#include <QApplication>
#include <QEventLoop>
#include <QKeySequence>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <set>
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
    char buf[1024];
    va_list ap;
    va_start(ap, fmt_str);
    std::vsnprintf(buf, sizeof(buf), fmt_str, ap);
    va_end(ap);
    return std::string(buf);
}

void settle(int ms = 60) {
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// ── Accelerator harvesting ────────────────────────────────────────────

/// One accelerator: the namespace it lives in, what carries it, and the key.
struct Accel {
    QString scope;   ///< "<menu bar>", "Debug", "Debug > Trace", "<window>"...
    QString label;   ///< the source text, ampersands and all
    QString key;     ///< "Alt+W", "Shift+F7", ...
};

const QString kMenuBar = QStringLiteral("<menu bar>");

/// The visible text: Qt eats the mnemonic '&' and renders "&&" as one '&'.
QString visible(const QString& text) {
    QString out;
    for (int i = 0; i < text.size(); ++i) {
        if (text[i] == u'&') {
            if (i + 1 < text.size() && text[i + 1] == u'&') { out += u'&'; ++i; }
            continue;
        }
        out += text[i];
    }
    return out;
}

/// Qt's OWN reading of a label's mnemonic, not a hand-rolled parser: this is
/// the same call Qt uses to turn "Step O&ut" into Alt+U, so the harvest cannot
/// drift from the rule the running menu bar applies.
QString mnemonic_of(const QString& text) {
    const QKeySequence seq = QKeySequence::mnemonic(text);
    return seq.isEmpty() ? QString() : seq.toString();
}

/// Recursively harvest one popup's items; each submenu opens a scope of its own.
void harvest_menu(QMenu* menu, const QString& scope,
                  std::vector<Accel>& out, std::set<QString>& scopes) {
    scopes.insert(scope);
    for (QAction* a : menu->actions()) {
        if (a->isSeparator() || a->text().isEmpty()) continue;
        const QString key = mnemonic_of(a->text());
        if (!key.isEmpty()) out.push_back(Accel{scope, a->text(), key});
    }
    for (QAction* a : menu->actions())
        if (a->menu())
            harvest_menu(a->menu(), scope + QStringLiteral(" > ") + visible(a->text()),
                         out, scopes);
}

/// Every (namespace, key) claimed by more than one label, rendered for a
/// failure detail. Empty string means no collision.
std::string collisions(const std::vector<Accel>& all) {
    std::map<QString, std::vector<QString>> by_key;
    for (const Accel& a : all)
        by_key[a.scope + QStringLiteral(" / ") + a.key].push_back(a.label);

    std::string out;
    for (const auto& kv : by_key) {
        if (kv.second.size() < 2) continue;
        if (!out.empty()) out += "; ";
        out += kv.first.toStdString() + " claimed by";
        for (const QString& label : kv.second)
            out += " \"" + label.toStdString() + "\"";
    }
    return out;
}

std::vector<Accel> only_scope(const std::vector<Accel>& all, const QString& scope) {
    std::vector<Accel> v;
    for (const Accel& a : all)
        if (a.scope == scope) v.push_back(a);
    return v;
}

std::vector<Accel> except_scope(const std::vector<Accel>& all, const QString& scope) {
    std::vector<Accel> v;
    for (const Accel& a : all)
        if (a.scope != scope) v.push_back(a);
    return v;
}

// ── Fixture: a headless Next emulator + the real debugger window ──────

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
        if (ok) {
            dbg->show();
            dbg->activateWindow();
        }
        settle(150);
    }
    ~Fixture() {
        if (mgr) mgr->set_enabled(false, /*prompt_on_corrupt=*/false);
    }
};

/// Press Alt+<key> on the debugger window through the real platform key path
/// (the one QShortcutMap listens on — a plain sendEvent() would bypass it and
/// prove nothing), report which menu-bar entry Qt made active, then dismiss
/// whatever opened so the next press starts from a closed menu bar.
QString alt_press(QWindow* wh, QMenuBar* bar, Qt::Key key) {
    QTest::keyClick(wh, key, Qt::AltModifier);
    settle(120);
    QAction* a = bar->activeAction();
    const QString opened = a ? visible(a->text()) : QStringLiteral("<none>");
    QTest::keyClick(wh, Qt::Key_Escape, Qt::NoModifier);
    settle(60);
    QTest::keyClick(wh, Qt::Key_Escape, Qt::NoModifier);
    settle(60);
    return opened;
}

} // namespace

// ── AC: no two accelerators share a namespace ─────────────────────────

static void test_accelerators(Fixture& fx)
{
    set_group("AC");

    if (!fx.ok) {
        check("AC-01", "the menu bar's own mnemonics are unique", false, "fixture failed");
        check("AC-02", "no menu reuses a mnemonic among its own items", false, "fixture failed");
        check("AC-03", "no two actions share a key-sequence shortcut", false, "fixture failed");
        check("AC-04", "no widget mnemonic collides in the window-wide Alt namespace", false, "fixture failed");
        check("AC-05", "the walk covers the whole menu tree", false, "fixture failed");
        return;
    }

    QMenuBar* bar = fx.dbg->menuBar();

    std::vector<Accel> menu_accels;
    std::set<QString>  popup_scopes;
    for (QAction* a : bar->actions()) {
        if (a->isSeparator() || a->text().isEmpty()) continue;
        const QString key = mnemonic_of(a->text());
        if (!key.isEmpty()) menu_accels.push_back(Accel{kMenuBar, a->text(), key});
    }
    for (QAction* a : bar->actions())
        if (a->menu())
            harvest_menu(a->menu(), visible(a->text()), menu_accels, popup_scopes);

    // AC-01 — THE issue-#124 row. Alt+W was on both "&Watches" and "&Window",
    // and Qt answered by alternating between them.
    {
        const std::vector<Accel> top = only_scope(menu_accels, kMenuBar);
        const std::string dups = collisions(top);
        check("AC-01", "the menu bar's own mnemonics are unique",
              dups.empty(),
              dups.empty() ? fmt("%zu top-level mnemonics, all distinct", top.size()) : dups);
    }

    // AC-02 — the same rule one level down. A popup's items share a namespace
    // while the popup is open, so a repeat there is the identical defect: the
    // sweep found it three more times in Debug and once in Breakpoints.
    {
        const std::vector<Accel> items = except_scope(menu_accels, kMenuBar);
        const std::string dups = collisions(items);
        check("AC-02", "no menu reuses a mnemonic among its own items",
              dups.empty(),
              dups.empty() ? fmt("%zu item mnemonics across %zu popups, all distinct",
                                 items.size(), popup_scopes.size())
                           : dups);
    }

    // The third namespace: F-keys and friends. Harvested via shortcuts()
    // (PLURAL), not shortcut(): the singular form returns only an action's
    // PRIMARY sequence, so an action given alternates through setShortcuts()
    // would carry a colliding sequence this suite never looked at. All nine
    // call sites in debugger_window.cpp use the singular setShortcut() today,
    // so the two harvests agree — but a guard with a blind spot is worse than
    // ordinary code with one, because everything else is relying on it to be
    // the thing that catches the NEXT Alt+W.
    std::vector<Accel> key_accels;
    for (QAction* a : fx.dbg->findChildren<QAction*>())
        for (const QKeySequence& seq : a->shortcuts()) {
            if (seq.isEmpty()) continue;
            key_accels.push_back(Accel{QStringLiteral("<window>"), a->text(),
                                       seq.toString()});
        }

    // AC-03 — clean today, and it is the check being absent, not the collision
    // being present, that this suite exists to fix.
    {
        const std::string dups = collisions(key_accels);
        check("AC-03", "no two actions share a key-sequence shortcut",
              dups.empty(),
              dups.empty() ? fmt("%zu shortcuts, all distinct", key_accels.size())
                           : dups);
    }

    // AC-04 — the cross-mechanism row, and the reason it is separate from
    // AC-01: a QPushButton labelled "&Watch" or a buddy QLabel takes Alt+W out
    // of the SAME window-wide namespace as the menu bar, from a completely
    // different call site. Nothing today carries one; this is the row that
    // notices when something does.
    {
        std::vector<Accel> widgets;
        for (QAbstractButton* b : fx.dbg->findChildren<QAbstractButton*>()) {
            const QString key = mnemonic_of(b->text());
            if (!key.isEmpty())
                widgets.push_back(Accel{QStringLiteral("<window Alt>"), b->text(), key});
        }
        for (QLabel* l : fx.dbg->findChildren<QLabel*>()) {
            if (!l->buddy()) continue;   // no buddy → Qt registers no mnemonic
            const QString key = mnemonic_of(l->text());
            if (!key.isEmpty())
                widgets.push_back(Accel{QStringLiteral("<window Alt>"), l->text(), key});
        }

        std::set<QString> taken;
        for (const Accel& a : only_scope(menu_accels, kMenuBar)) taken.insert(a.key);

        std::string bad;
        std::set<QString> seen;
        for (const Accel& w : widgets) {
            const char* why = taken.count(w.key)    ? "already a menu-bar mnemonic"
                            : seen.count(w.key)     ? "already taken by another widget"
                                                    : nullptr;
            if (why) {
                if (!bad.empty()) bad += "; ";
                bad += "\"" + w.label.toStdString() + "\" wants " + w.key.toStdString()
                     + " — " + why;
            }
            seen.insert(w.key);
        }
        check("AC-04", "no widget mnemonic collides in the window-wide Alt namespace",
              bad.empty(),
              bad.empty() ? fmt("%zu widget mnemonics vs %zu menu-bar mnemonics",
                                widgets.size(), taken.size())
                          : bad);
    }

    // AC-05 — the denominator. Every row above passes trivially against an
    // empty harvest, so the shape of the walk is pinned: five menus, eight
    // popups (Debug + its Trace and Rewind submenus, Map + Load MAP File,
    // Breakpoints, Watches, Window), thirty mnemonics, nine shortcuts. Adding
    // or removing a menu entry means updating these numbers, and that edit is
    // the point — it is the claim about how much of the menu tree is checked.
    // It counts the SAME vector AC-03 checks, not a second walk that could
    // drift from it: the denominator has to describe the harvest it vouches
    // for, or it vouches for nothing.
    {
        const size_t top = only_scope(menu_accels, kMenuBar).size();
        const size_t shortcuts = key_accels.size();

        const bool as_expected = top == 5
                              && popup_scopes.size() == 8
                              && menu_accels.size() == 30
                              && shortcuts == 9;
        // Described as what it is — a comparison against pinned sizes — not as
        // "the walk covers the whole tree", which would claim a completeness
        // four size checks cannot establish (add one menu and delete another
        // and the counts still agree).
        check("AC-05", "the harvest matches the pinned shape of the menu tree",
              as_expected,
              fmt("menus=%zu (want 5), popups=%zu (want 8), mnemonics=%zu (want 30), "
                  "shortcuts=%zu (want 9)",
                  top, popup_scopes.size(), menu_accels.size(), shortcuts));
    }
}

// ── AK: pressing the keys does what the labels promise ────────────────

static void test_keystrokes(Fixture& fx)
{
    set_group("AK");

    QWindow* wh = fx.ok ? fx.dbg->windowHandle() : nullptr;
    const bool active = fx.ok && wh != nullptr
                     && QApplication::activeWindow() == fx.dbg;
    if (!active) {
        // Loud, never a silent skip: without an active window the key path
        // under test is not reachable and these rows prove nothing.
        const std::string why = !fx.ok ? "fixture failed"
                              : !wh    ? "debugger window has no platform handle"
                                       : "debugger window is not the active window";
        check("AK-01", "the first Alt+W press opens the Watches menu", false, why);
        check("AK-02", "Alt+W opens Watches on every press", false, why);
        check("AK-03", "Alt+N opens the Window menu", false, why);
        return;
    }

    QMenuBar* bar = fx.dbg->menuBar();

    // Four presses of the same key, each from a closed menu bar.
    QStringList opened;
    for (int i = 0; i < 4; ++i)
        opened << alt_press(wh, bar, Qt::Key_W);

    // AK-01 — which of the two menus keeps W. NOT a regression row: the broken
    // code opened Watches on this press too. It pins the decision, so a later
    // change that quietly hands W to some other menu is a visible edit.
    check("AK-01", "the first Alt+W press opens the Watches menu",
          opened.value(0) == QStringLiteral("Watches"),
          fmt("first press opened \"%s\"", opened.value(0).toUtf8().constData()));

    // AK-02 — THE regression row. Under the duplicate, Qt's ambiguous-shortcut
    // dispatch alternated: Watches, Window, Watches, Window.
    //
    // Note what this asserts, because the obvious reading is wrong: it requires
    // Watches on ALL FOUR presses, which is the ASSIGNMENT on every press, not
    // mere self-consistency. A collision-free swap that handed W to Window
    // would report "Window, Window, Window, Window" — perfectly consistent —
    // and this row would still fail, correctly. The description says exactly
    // that, so a reader trusting the sentence is not misled.
    {
        bool all_watches = true;
        for (const QString& m : opened)
            if (m != QStringLiteral("Watches")) all_watches = false;
        check("AK-02", "Alt+W opens Watches on every press",
              all_watches,
              fmt("four presses opened: %s", opened.join(QStringLiteral(", "))
                                                   .toUtf8().constData()));
    }

    // AK-03 — the Window menu now has an accelerator that works. Under the
    // duplicate Alt+N was bound to nothing at all, and Window could only be
    // reached by an even-numbered Alt+W press.
    {
        const QString got = alt_press(wh, bar, Qt::Key_N);
        check("AK-03", "Alt+N opens the Window menu",
              got == QStringLiteral("Window"),
              fmt("opened \"%s\"", got.toUtf8().constData()));
    }
}

int main(int argc, char** argv)
{
    // The debugger window owns QWidgets, so a QApplication is required — but
    // not a display: force the offscreen QPA platform, the same idiom as the
    // other debugger suites. The offscreen plugin still runs the real
    // QShortcutMap, which is what the AK rows exercise.
    qputenv("QT_QPA_PLATFORM", "offscreen");

    // Isolate the config file: the window restores a saved geometry, and the
    // real ~/.jnext/Debugger.conf belongs to the user.
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());

    QApplication app(argc, argv);

    Fixture fx;
    test_accelerators(fx);
    std::printf("  Group: AC             — done\n");
    test_keystrokes(fx);
    std::printf("  Group: AK             — done\n");

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
