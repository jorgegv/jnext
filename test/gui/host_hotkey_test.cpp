// ===========================================================================
// Issue #115 part 2 — host hotkeys live on Alt, the guest keeps Ctrl.
//
// Ctrl IS the guest's Symbol Shift (keymaps.vhd:84; keyboard.cpp:124-125 binds
// LCTRL/RCTRL to matrix {7,1}), so the six Ctrl+<letter> QAction shortcuts the
// Qt main window used to carry each ate a Symbol Shift sequence NextBASIC
// needs constantly — SS+O ';', SS+D STEP, SS+R '<', SS+T '>', SS+Q '<=',
// SS+S '|'. Qt's shortcut map outranks keyPressEvent, so the guest never saw
// them at all. All six moved to Alt+<letter>.
//
// WHAT THIS SUITE PINS, and why each row exists:
//
//   1. The PROPERTY, not the menu label: a Ctrl+<letter> chord typed by the
//      user arrives in the emulated KEY MATRIX as Symbol Shift + <letter>.
//      Rows H115-01..12 drive real QKeyEvents into a real MainWindow, feed the
//      resulting SDL scancodes into a real Keyboard, and read the matrix back
//      through read_rows() — the same call port 0xFE makes.
//   2. The migration is complete and stays complete — H115-13.
//   3. Each new Alt+<letter> really activates its action — H115-20..25.
//   4. The Alt namespace has no ambiguous binding — H115-26/27. This one is
//      load-bearing: Alt+<letter> is ONE namespace shared by QAction shortcuts
//      and QMenuBar '&' mnemonics, and a letter claimed twice makes an
//      AMBIGUOUS Qt shortcut, which QAction::event() answers with a qWarning
//      and NOTHING ELSE — so both bindings break, silently. Three of the six
//      target letters (S, T, D) collided with &Settings / &Tape / &Debug and
//      the mnemonics were re-lettered to Alt+N / Alt+A / Alt+B to clear them.
//
// DELIVERY MECHANISM — the reason these rows are not decoration:
// QApplication::sendEvent() to a widget passes through QApplication::notify(),
// which consults the global QShortcutMap BEFORE dispatching the KeyPress. So
// a synthetic Ctrl+O here is swallowed by a live Ctrl+O QAction exactly as a
// real one would be. Verified by mutation: restoring any Ctrl+<letter>
// shortcut fails that letter's H115-01..12 rows AND its H115-2x row.
//
// MainWindow owns QWidgets, so a QApplication is required — but not a
// display: the offscreen QPA platform is forced in main() (same idiom as
// esc_break_test and the debugger panel suites).
// ===========================================================================
#include <QAction>
#include <QApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <cstdio>
#include <string>
#include <vector>

#include "gui/main_window.h"
#include "input/keyboard.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail) {
    ++g_total;
    if (cond) { ++g_pass; std::printf("  PASS %s: %s\n", id, desc); }
    else      { ++g_fail; std::printf("  FAIL %s: %s [%s]\n", id, desc, detail.c_str()); }
}

// --- The six migrated hotkeys ------------------------------------------------
//
// `row`/`col` are the guest matrix cell for the letter (keyboard.cpp:77-125);
// `action_text` is the QAction's untranslated text, which is how the action is
// located below (DebuggerManager finds its own action the same way,
// debugger_manager.cpp:45).
struct Hotkey {
    Qt::Key      qt_key;
    SDL_Scancode sdl;
    int          row, col;      // ZX matrix cell of the letter
    const char*  action_text;
    const char*  portable;      // expected QKeySequence, PortableText form
    const char*  ss_meaning;    // what Symbol Shift + letter types on a Next
};

const Hotkey HOTKEYS[] = {
    { Qt::Key_Q, SDL_SCANCODE_Q, 2, 0, "&Quit",              "Alt+Q", "<=" },
    { Qt::Key_O, SDL_SCANCODE_O, 5, 1, "Load &NEX File...",  "Alt+O", ";"  },
    { Qt::Key_S, SDL_SCANCODE_S, 1, 1, "Save &Screenshot...","Alt+S", "|"  },
    { Qt::Key_R, SDL_SCANCODE_R, 2, 3, "&Power Reset",       "Alt+R", "<"  },
    { Qt::Key_T, SDL_SCANCODE_T, 2, 4, "&Open Tape File...", "Alt+T", ">"  },
    { Qt::Key_D, SDL_SCANCODE_D, 1, 2, "&Debugger",          "Alt+D", "STEP" },
};
constexpr int N_HOTKEYS = int(sizeof(HOTKEYS) / sizeof(HOTKEYS[0]));

// Symbol Shift's own matrix cell (keyboard.cpp:124).
constexpr int SYM_ROW = 7, SYM_COL = 1;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void send(MainWindow& w, Qt::Key k, Qt::KeyboardModifiers mods, bool press) {
    QKeyEvent e(press ? QEvent::KeyPress : QEvent::KeyRelease, k, mods);
    QApplication::sendEvent(&w, &e);
}

// Port 0xFE semantics: bit N of addr_high == 0 selects row N; the returned
// 5 bits are ACTIVE-LOW, so a clear bit means "key down".
bool key_down(const Keyboard& kb, int row, int col) {
    const uint8_t addr_high = static_cast<uint8_t>(~(1u << row));
    return (kb.read_rows(addr_high) & (1u << col)) == 0;
}

std::string matrix_detail(const Keyboard& kb, int row, int col) {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "sym_row7=0x%02X letter_row%d=0x%02X",
                  kb.read_rows(static_cast<uint8_t>(~(1u << SYM_ROW))),
                  row, kb.read_rows(static_cast<uint8_t>(~(1u << row))));
    return buf;
}

QAction* find_action(MainWindow& w, const char* text) {
    for (QAction* a : w.findChildren<QAction*>())
        if (a->text() == QString::fromUtf8(text)) return a;
    return nullptr;
}

QString seq_of(const QAction* a) {
    return a->shortcut().toString(QKeySequence::PortableText);
}

// Cut every migrated action loose from its real handler, ONCE, before any row
// runs. Those handlers open modal file dialogs (Load NEX, Save Screenshot,
// Open Tape), quit the application (Quit) or reboot the machine (Power Reset),
// so a row that accidentally activates one would hang the suite forever rather
// than fail — which is exactly what happened the first time a mutation put the
// shortcuts back on Ctrl and the Ctrl+O row opened a QFileDialog. A test that
// hangs under mutation cannot be mutation-verified, so this is not tidiness:
// it is what makes the mutation evidence obtainable at all.
void disarm_actions(MainWindow& w) {
    for (int i = 0; i < N_HOTKEYS; ++i)
        if (QAction* a = find_action(w, HOTKEYS[i].action_text))
            QObject::disconnect(a, nullptr, nullptr, nullptr);
}

// Every top-level menubar mnemonic, as "Alt+X" portable strings.
QStringList menubar_mnemonics(MainWindow& w) {
    QStringList out;
    for (QAction* m : w.menuBar()->actions()) {
        const QKeySequence ks = QKeySequence::mnemonic(m->text());
        if (!ks.isEmpty()) out << ks.toString(QKeySequence::PortableText);
    }
    return out;
}

// Every non-empty QAction shortcut under the main window, portable form.
QStringList action_shortcuts(MainWindow& w) {
    QStringList out;
    for (QAction* a : w.findChildren<QAction*>()) {
        const QString s = seq_of(a);
        if (!s.isEmpty()) out << s;
    }
    return out;
}

// ---------------------------------------------------------------------------
// Group 1 — the six Symbol Shift sequences reach the emulated key matrix
// ---------------------------------------------------------------------------
//
// This is the property the issue is actually about. The chord is driven the
// way a user types it — Ctrl down, letter down, letter up, Ctrl up — and the
// assertion is read out of the guest's matrix, not out of a menu label.

void test_guest_reachability(MainWindow& w) {
    for (int i = 0; i < N_HOTKEYS; ++i) {
        const Hotkey& h = HOTKEYS[i];

        Keyboard kb;
        kb.reset();
        w.set_key_callback([&kb](SDL_Scancode sc, bool pressed) {
            kb.set_key(sc, pressed);
        });

        send(w, Qt::Key_Control, Qt::NoModifier,      true);
        send(w, h.qt_key,        Qt::ControlModifier, true);

        char desc[160];
        std::snprintf(desc, sizeof(desc),
                      "Ctrl+%c reaches the guest as SYM SHIFT + %c (types '%s')",
                      'A' + int(h.qt_key - Qt::Key_A), 'A' + int(h.qt_key - Qt::Key_A),
                      h.ss_meaning);
        char id[16];
        std::snprintf(id, sizeof(id), "H115-%02d", 1 + i);
        check(id, desc,
              key_down(kb, SYM_ROW, SYM_COL) && key_down(kb, h.row, h.col),
              matrix_detail(kb, h.row, h.col));

        send(w, h.qt_key,        Qt::ControlModifier, false);
        send(w, Qt::Key_Control, Qt::NoModifier,      false);

        std::snprintf(desc, sizeof(desc),
                      "releasing Ctrl+%c clears both matrix bits (no stuck key)",
                      'A' + int(h.qt_key - Qt::Key_A));
        std::snprintf(id, sizeof(id), "H115-%02d", 7 + i);
        check(id, desc,
              !key_down(kb, SYM_ROW, SYM_COL) && !key_down(kb, h.row, h.col),
              matrix_detail(kb, h.row, h.col));

        w.set_key_callback(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Group 2 — the migration is complete, and each action carries its new chord
// ---------------------------------------------------------------------------

void test_bindings(MainWindow& w) {
    // H115-13 — no QAction anywhere under the main window still binds a PLAIN
    // Ctrl+<letter>. This is the row that would have caught Ctrl+Q in the
    // first place, and it keeps catching the next one.
    //
    // Scope, stated precisely: "plain" means Ctrl plus a single letter and no
    // other modifier. Ctrl+Shift+S (Save Snapshot) and Ctrl+F5 / Ctrl+F6
    // (recording) are deliberately NOT matched — the owner's #115 scope was
    // the six plain chords, and F-keys have no ZX matrix meaning at all.
    // Ctrl+Shift+S does still swallow the guest's CS+SS+S; that residual is
    // recorded in doc/design/TASK-115-HOST-SHORTCUT-INVENTORY.md, not hidden
    // here.
    {
        static const QRegularExpression plain_ctrl_letter("^Ctrl\\+[A-Z]$");
        QStringList offenders;
        for (const QString& s : action_shortcuts(w))
            if (plain_ctrl_letter.match(s).hasMatch()) offenders << s;
        check("H115-13", "no QAction binds a plain Ctrl+<letter> (Ctrl is Symbol Shift)",
              offenders.isEmpty(),
              ("offenders=" + offenders.join(',')).toStdString());
    }

    // H115-14..19 — each migrated action carries exactly its Alt+<letter>.
    for (int i = 0; i < N_HOTKEYS; ++i) {
        const Hotkey& h = HOTKEYS[i];
        QAction* a = find_action(w, h.action_text);
        char id[16], desc[160];
        std::snprintf(id, sizeof(id), "H115-%02d", 14 + i);
        std::snprintf(desc, sizeof(desc), "action \"%s\" is bound to %s",
                      h.action_text, h.portable);
        check(id, desc,
              a != nullptr && seq_of(a) == QString::fromUtf8(h.portable),
              a ? ("got=" + seq_of(a)).toStdString() : "action not found");
    }
}

// ---------------------------------------------------------------------------
// Group 3 — each Alt+<letter> really fires its action through the shortcut map
// ---------------------------------------------------------------------------
//
// The actions are already disarmed (disarm_actions), so each one is rewired to
// a recorder for the duration of its row and cut loose again afterwards. That
// is deliberate and does not weaken the row: what is under test is the
// KEY -> QAction leg (the leg #115 changed), not the QAction -> handler leg
// (untouched, and covered where those handlers are tested).

void test_alt_activation(MainWindow& w) {
    for (int i = 0; i < N_HOTKEYS; ++i) {
        const Hotkey& h = HOTKEYS[i];
        QAction* a = find_action(w, h.action_text);

        bool fired = false;
        if (a)
            QObject::connect(a, &QAction::triggered, [&fired]() { fired = true; });

        // The guest must not see the letter PRESS: Alt is the host namespace.
        //
        // Deliberately scoped to the press. Qt consults the shortcut map only
        // for QEvent::KeyPress, so the matching KeyRelease is NOT swallowed and
        // does reach handle_key() — measured, not assumed. That stray release
        // is inert: Keyboard::set_key(sc, false) clears a matrix bit that was
        // never set (keyboard.cpp:285-300), and none of these six letters has
        // an s_alt_* variant to mis-latch. The same asymmetry existed for the
        // Ctrl chords before this change; it is Qt's, not jnext's. Asserting
        // it here would pin incidental Qt behaviour, so the row asserts only
        // what matters: no spurious keystroke is typed into the guest.
        bool letter_pressed_in_guest = false;
        w.set_key_callback([&letter_pressed_in_guest, &h](SDL_Scancode sc, bool pressed) {
            if (sc == h.sdl && pressed) letter_pressed_in_guest = true;
        });

        send(w, Qt::Key_Alt, Qt::NoModifier,  true);
        send(w, h.qt_key,    Qt::AltModifier, true);
        QApplication::processEvents();
        send(w, h.qt_key,    Qt::AltModifier, false);
        send(w, Qt::Key_Alt, Qt::NoModifier,  false);

        char id[16], desc[160];
        std::snprintf(id, sizeof(id), "H115-%02d", 20 + i);
        std::snprintf(desc, sizeof(desc),
                      "%s activates \"%s\" and types nothing into the guest",
                      h.portable, h.action_text);
        check(id, desc, a != nullptr && fired && !letter_pressed_in_guest,
              std::string("fired=") + (fired ? "1" : "0") +
              " letter_pressed_in_guest=" + (letter_pressed_in_guest ? "1" : "0"));

        // `fired` dies with this iteration — drop the connection with it.
        if (a) QObject::disconnect(a, nullptr, nullptr, nullptr);
        w.set_key_callback(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Group 4 — the Alt namespace has no ambiguous binding
// ---------------------------------------------------------------------------

void test_alt_namespace(MainWindow& w) {
    // H115-26 — menubar mnemonics and QAction shortcuts must be disjoint.
    //
    // Empirically confirmed while writing this suite: with "&Tape" (Alt+T) AND
    // the Alt+T Open-Tape shortcut both live, four consecutive Alt+T presses
    // fired the action ZERO times. Qt reports the sequence as ambiguous and
    // QAction::event() then only warns. Nothing crashes and nothing logs to
    // the user — the feature simply stops working, which is exactly the kind
    // of defect a menu-label review cannot see.
    {
        const QStringList mnem = menubar_mnemonics(w);
        QStringList clashes;
        for (const QString& s : action_shortcuts(w))
            if (mnem.contains(s)) clashes << s;
        check("H115-26",
              "no Alt chord is claimed by both a menubar mnemonic and a shortcut",
              clashes.isEmpty(),
              ("mnemonics=" + mnem.join(',') + " clashes=" + clashes.join(',')).toStdString());
    }

    // H115-27 — the three Alt chords the GUEST owns (EDIT / GRAPH / CAPS LOCK,
    // keyboard.cpp:163,172-174) must not be claimed by any host binding. Adding a
    // menu titled "&Edit", "&Graphics" or "&Config" would silently kill one;
    // this row is what stops that.
    {
        const QStringList guest_owned = {"Alt+E", "Alt+G", "Alt+C"};
        QStringList host = menubar_mnemonics(w);
        host += action_shortcuts(w);
        QStringList stolen;
        for (const QString& g : guest_owned)
            if (host.contains(g)) stolen << g;
        check("H115-27",
              "the guest's Alt+E / Alt+G / Alt+C are not claimed by any host binding",
              stolen.isEmpty(),
              ("host=" + host.join(',') + " stolen=" + stolen.join(',')).toStdString());
    }
}

} // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("Issue #115 part 2 - host hotkeys on Alt, Ctrl left to the guest\n");
    std::printf("==============================================================\n\n");

    // One window, shown and activated: Qt::WindowShortcut (the default, and
    // set nowhere in src/) only matches while the action's window is active.
    MainWindow w;
    w.show();
    w.activateWindow();
    QApplication::processEvents();
    disarm_actions(w);

    test_guest_reachability(w);
    test_bindings(w);
    test_alt_activation(w);
    test_alt_namespace(w);
    std::printf("  Group: H115           - done\n");

    std::printf("\n==============================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
