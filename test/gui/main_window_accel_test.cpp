// ===========================================================================
// Main-window accelerator-collision suite (GitHub issue #217).
//
// No VHDL oracle: this is host UI wiring, not emulated hardware. The oracle is
// Qt's accelerator model, and the measurement behind it is issue #124's — where
// two debugger menus shared Alt+W and Qt classified the sequence as AMBIGUOUS,
// dispatching it ROUND-ROBIN so each menu opened on alternate presses and
// neither could be opened deliberately.
//
// WHAT #217 REPORTED. The debugger got a guard after #124
// (test/debugger/accel_test.cpp, rows AC-01..05). The MAIN window never did,
// and was already carrying three duplicate mnemonics — all pre-existing, none
// of them catchable by anything the project ran:
//
//   File   Alt+N   "Load &NEX File..."       / "Save S&napshot..."
//   File   Alt+M   "&Mount SD Card Image..." / "Record &MPEG4 Video..."
//   View   Alt+F   "&Fullscreen"             / "CRT &Filter"
//
// A FOURTH was found by this suite on its first run, which is the point of
// writing it rather than hand-fixing the three:
//
//   Machine Alt+S  "&Soft Reset"             / "CPU &Speed"
//
// All four are POPUP-scope collisions. Note what that is not: it is not the
// window-wide Alt namespace that host_hotkey_test's H115-26/27 already pin
// (menubar titles vs QAction shortcuts, and the guest's Alt+E/G/C). A popup's
// items form a namespace of their own, live only while that popup is open, and
// nothing looked at it. That is the gap this file closes; the window-wide rows
// stay where they are and are deliberately not duplicated here.
//
// WHY THE REPLACEMENT LETTERS ARE WHAT THEY ARE. The main window's mnemonic
// space is genuinely tight — Alt+S is Save Screenshot, the GUEST owns Alt+E/G/C
// (keyboard.cpp:163,172-174), and the menubar already holds F/M/I/A/B/V/N/H —
// so each replacement was picked to collide with nothing in ANY namespace:
//
//   "&Load NEX File..."       L   verb's own first letter; free everywhere
//   "Record MPEG&4 Video..."  4   the ONLY character of that label free in
//                                 every namespace at once — its P is already
//                                 "Sto&p MPEG4 Recording" in the same popup,
//                                 and the V of Video is the View menu
//   "CP&U Speed"              U   the acronym's own letter
//   "CRT Fi&lter"             L   free everywhere (a different popup from
//                                 Load NEX, so reusing L is legitimate)
//
// Groups:
//   MA  walks the REAL MainWindow's accelerators and asserts uniqueness in each
//       Qt namespace. The guard the main window never had.
//
// Discriminative — each row was mutation-tested against the product, one
// mutation at a time; MA-02's four are the original collisions put back:
//   MA-01  a menubar title re-lettered onto another's letter        -> fails
//   MA-02  each of the four popup collisions restored, separately   -> fails,
//          naming the popup, the key and both claimants
//   MA-03  Soft Reset re-pointed from F4 to F11                     -> fails
//   MA-04  a status-bar QPushButton labelled "&Nudge"               -> fails
//   MA-05  one extra menu entry added                               -> fails
//
// The mutation MA-04 does NOT catch is recorded on the row itself, because it
// was tried first and is the obvious one to try again: putting a '&' in a
// TOOLBAR action's text changes nothing, and the reason was measured rather
// than reasoned about — see MA-04.
//
// The harvest is shared with the debugger suite (test/menu_accel.h) so the two
// guards cannot drift apart; the rows, scopes and pinned shape are this file's.
//
// MainWindow owns QWidgets, so a QApplication is required — but not a display:
// the offscreen QPA platform is forced in main(), the same idiom as
// host_hotkey_test and the debugger panel suites.
//
// Run: ./build/test/main_window_accel_test
// ===========================================================================

#include "gui/main_window.h"

#include "menu_accel.h"

#include <QApplication>
#include <QMenuBar>

#include <cstdarg>
#include <cstdio>
#include <set>
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

// The View > Debugger entry, and the SAME QAction added to the Debug menu by
// GH #215, exist only in a debugger build (main_window.cpp's #ifdef). They are
// two menu entries carrying one Alt+D shortcut, so MA-05's pinned shape has to
// account for the build configuration rather than pretend it does not: this
// suite is gated on ENABLE_QT_UI, which does not imply ENABLE_DEBUGGER (the
// project builds and verifies a Qt-only configuration too).
#ifdef ENABLE_DEBUGGER
constexpr size_t kDebuggerMnemonics = 2;
constexpr size_t kDebuggerShortcuts = 1;
#else
constexpr size_t kDebuggerMnemonics = 0;
constexpr size_t kDebuggerShortcuts = 0;
#endif

} // namespace

// ── MA: no two accelerators share a namespace ─────────────────────────

static void test_accelerators(MainWindow& w)
{
    using accel::Accel;

    QMenuBar* bar = w.menuBar();

    std::vector<Accel> menu_accels;
    std::set<QString>  popup_scopes;
    accel::harvest_menu_bar(bar, menu_accels, popup_scopes);

    const std::vector<Accel> top = accel::only_scope(menu_accels,
                                                     accel::menu_bar_scope());

    // MA-01 — the window-wide half of the Alt namespace, as far as the menu bar
    // owns it. Clean today and historically (issue #115 re-lettered &Settings,
    // &Tape and &Debug to N/A/B precisely to keep it that way); the row exists
    // so the next menu added here cannot quietly take a letter already spoken
    // for. host_hotkey_test H115-26 checks the OTHER half of this namespace —
    // menubar titles against QAction shortcuts — and is not repeated here.
    {
        const std::string dups = accel::collisions(top);
        check("MA-01", "the menu bar's own mnemonics are unique",
              dups.empty(),
              dups.empty() ? fmt("%zu top-level mnemonics, all distinct", top.size())
                           : dups);
    }

    // MA-02 — THE issue-#217 row. Four popups each had one letter claimed
    // twice, so pressing it with that popup open moved between the two entries
    // instead of choosing either. Restoring any one of the four fails this row
    // and names the popup, the key and both claimants.
    {
        const std::vector<Accel> items =
            accel::except_scope(menu_accels, accel::menu_bar_scope());
        const std::string dups = accel::collisions(items);
        check("MA-02", "no menu reuses a mnemonic among its own items",
              dups.empty(),
              dups.empty() ? fmt("%zu item mnemonics across %zu popups, all distinct",
                                 items.size(), popup_scopes.size())
                           : dups);
    }

    // The third namespace: F-keys and chords. Harvested via shortcuts()
    // (PLURAL) — see test/menu_accel.h for why the singular form is a blind
    // spot rather than a simplification.
    const std::vector<Accel> key_accels = accel::harvest_shortcuts(&w);

    // MA-03 — clean today; it is the check being absent, not a collision being
    // present, that this suite exists to fix. One member of this namespace is
    // platform-resolved rather than literal: Settings > Preferences... carries
    // QKeySequence::Preferences, which Qt renders "Settings" (the XF86Settings
    // key) on this platform and "Ctrl+," on macOS. It is counted either way; a
    // platform that resolved it to nothing at all would drop it, since the
    // harvest skips empty sequences — MA-05's number is what would say so.
    {
        const std::string dups = accel::collisions(key_accels);
        check("MA-03", "no two actions share a key-sequence shortcut",
              dups.empty(),
              dups.empty() ? fmt("%zu shortcuts, all distinct", key_accels.size())
                           : dups);
    }

    // MA-04 — the cross-mechanism row, and the reason it is separate from
    // MA-01: a QPushButton or a buddy QLabel labelled "&Nudge" takes Alt+N out
    // of the SAME window-wide namespace as the menu bar, from a completely
    // different call site. Nothing in the main window carries one today, which
    // is what makes the count below zero; this is the row that notices when
    // something does.
    //
    // WHAT IT DOES NOT REACH, measured: the TOOLBAR. `toolbar->addAction("&NMI")`
    // looks like it would claim Alt+N and does not — QToolButton takes its
    // label from QAction::iconText(), which returns the text with the mnemonic
    // stripped, so the live button reads "NMI" and registers no accelerator at
    // all. Checked against Qt directly, not inferred from this row staying
    // green: action text "&NMI" -> iconText "NMI" -> button text "NMI". So the
    // zero below is the truth about the window and not a hole in the walk, and
    // a toolbar '&' is a typo rather than a collision.
    {
        const std::vector<Accel> widgets = accel::harvest_widget_mnemonics(&w);
        const std::string bad = accel::widget_alt_conflicts(widgets, top);
        check("MA-04", "no widget mnemonic collides in the window-wide Alt namespace",
              bad.empty(),
              bad.empty() ? fmt("%zu widget mnemonics vs %zu menu-bar mnemonics",
                                widgets.size(), top.size())
                          : bad);
    }

    // MA-05 — the denominator, the same device as the debugger suite's AC-05
    // and for the same reason: every row above passes trivially against an
    // empty harvest, so the shape of the walk is pinned. Eight menus; thirteen
    // popups (File, Machine + Machine Type + CPU Speed + Emulator Speed, Input
    // + Joy 1 + Joy 2, Tape, Debug, View, Settings, Help); forty-four
    // mnemonics; eleven shortcuts — plus, in a debugger build, the two
    // View/Debug entries sharing one Alt+D.
    //
    // ADDING OR REMOVING A MENU ENTRY MEANS UPDATING THESE NUMBERS, and that
    // edit is the point: it is the claim about how much of the menu tree is
    // checked, and it is made deliberately. It counts the SAME vectors MA-02
    // and MA-03 check, not a second walk that could drift from them.
    //
    // Described as what it is — a comparison against pinned sizes — not as
    // "the walk covers the whole tree", which four size checks cannot
    // establish (add one entry and delete another and the counts still agree).
    {
        const bool as_expected =
               top.size()           == 8
            && popup_scopes.size()  == 13
            && menu_accels.size()   == 44 + kDebuggerMnemonics
            && key_accels.size()    == 11 + kDebuggerShortcuts;
        check("MA-05", "the harvest matches the pinned shape of the menu tree",
              as_expected,
              fmt("menus=%zu (want 8), popups=%zu (want 13), mnemonics=%zu (want %zu), "
                  "shortcuts=%zu (want %zu)",
                  top.size(), popup_scopes.size(),
                  menu_accels.size(), 44 + kDebuggerMnemonics,
                  key_accels.size(), 11 + kDebuggerShortcuts));
    }
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("Issue #217 - main window menu mnemonics are unique per Qt namespace\n");
    std::printf("==================================================================\n\n");

    // A bare MainWindow builds the whole menu tree in create_menus(); no
    // emulator is attached, deliberately, because nothing in the menu SHAPE
    // depends on one and set_emulator() would start a machine this suite has
    // no use for. The window is never shown and no action is ever triggered,
    // so none of the modal file dialogs behind them can open.
    MainWindow w;
    QApplication::processEvents();

    test_accelerators(w);
    std::printf("  Group: MA             - done\n");

    std::printf("\n==================================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
