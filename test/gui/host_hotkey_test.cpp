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
//   3b. Issue #130 — Ctrl+Shift+S (Save Snapshot) was the ONE chord #115 left
//      on Ctrl, because its scope was the six PLAIN Ctrl+<letter> chords. Ctrl
//      is Symbol Shift and Shift is Caps Shift, so it ate the guest's CS+SS+S
//      exactly as the other six ate their sequences. Rows H115-30..33 pin its
//      move to Alt+Shift+S: the Ctrl+Shift+<letter> class is empty, the action
//      carries the new chord, CS+SS+S reaches the matrix, and the new chord
//      fires Save Snapshot without disturbing Alt+S.
//   3c. Issue #259 — Settings > Preferences carried QKeySequence::Preferences,
//      which Qt 6.11.2 resolves to nothing on Linux. Rows H115-34/35 pin its
//      literal Alt+P and that the chord fires it.
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
//
// BUILD CONFIGURATION (GH #273). This suite is gated on ENABLE_QT_UI, which
// does NOT imply ENABLE_DEBUGGER — the project builds and verifies a Qt-only
// configuration, and three rows compared documentation against the product
// while assuming the debugger was always there: H115-19 and H115-25 (View >
// Debugger, inside main_window.cpp's `#ifdef`) and H115-29 (FEATURES.md names
// the disassembly panel's Ctrl+A / Ctrl+C and rewind's Shift+F6 / Shift+F7).
// Every one of the 45 rows runs in BOTH configurations — the row count is
// pinned in test/unit-tests.conf and a vanishing row is a harness failure —
// and the three above assert the property their configuration actually has.
// The default build is byte-for-byte unaffected.
// ===========================================================================
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QStyle>
#include <QEventLoop>
#include <QTest>
#include <QWindow>

#include <cstdio>
#include <string>
#include <vector>

#include "core/emulator.h"
#include "gui/main_window.h"
#include "input/keyboard.h"
#include "platform/host_key_latch.h"
#include "platform/host_key_wiring.h"
#ifdef ENABLE_DEBUGGER
#include "debugger/debugger_window.h"
#endif

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
    bool         debugger_only; // the action exists only under ENABLE_DEBUGGER
};

const Hotkey HOTKEYS[] = {
    { Qt::Key_Q, SDL_SCANCODE_Q, 2, 0, "&Quit",              "Alt+Q", "<=", false },
    // GH #217 moved this label's mnemonic from N to L ("Load &NEX File..." ->
    // "&Load NEX File..."): N was claimed by "Save S&napshot..." as well. The
    // ACTION is located by its exact text, so the string tracks the product;
    // the binding under test here — Alt+O — is untouched.
    { Qt::Key_O, SDL_SCANCODE_O, 5, 1, "&Load NEX File...",  "Alt+O", ";",  false },
    { Qt::Key_S, SDL_SCANCODE_S, 1, 1, "Save &Screenshot...","Alt+S", "|",  false },
    { Qt::Key_R, SDL_SCANCODE_R, 2, 3, "&Power Reset",       "Alt+R", "<",  false },
    { Qt::Key_T, SDL_SCANCODE_T, 2, 4, "&Open Tape File...", "Alt+T", ">",  false },
    // GH #273 — View > Debugger is inside main_window.cpp's `#ifdef
    // ENABLE_DEBUGGER`, and ENABLE_QT_UI (which gates this suite) does NOT
    // imply ENABLE_DEBUGGER: the project builds and verifies a Qt-only
    // configuration too. Its rows below therefore assert the mirror-image
    // property in that build instead of the one this one names.
    { Qt::Key_D, SDL_SCANCODE_D, 1, 2, "&Debugger",          "Alt+D", "STEP", true },
};
constexpr int N_HOTKEYS = int(sizeof(HOTKEYS) / sizeof(HOTKEYS[0]));

// True when this build compiles the debugger in, i.e. when a `debugger_only`
// action exists at all. Rows that name one branch on this rather than
// disappearing: `test/unit-tests.conf` pins this suite's row count EXACTLY, in
// both directions, so a row that vanished in one configuration would be a hard
// harness failure there — and a row asserting the OTHER configuration's real
// property is worth more than one that skips anyway.
#ifdef ENABLE_DEBUGGER
constexpr bool kDebuggerBuilt = true;
#else
constexpr bool kDebuggerBuilt = false;
#endif

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
    // Issue #130's Save Snapshot needs the same treatment: on_save_snapshot()
    // opens a modal QFileDialog.
    if (QAction* a = find_action(w, "Save S&napshot..."))
        QObject::disconnect(a, nullptr, nullptr, nullptr);
    // Issue #259's Preferences too: on_open_preferences() opens a modal dialog.
    if (QAction* a = find_action(w, "&Preferences..."))
        QObject::disconnect(a, nullptr, nullptr, nullptr);
    // GH #19's Quick Screenshot: it is inert on this emulator-less window
    // (on_quick_screenshot() returns at once), but disarmed with the rest so
    // the row below rewires a recorder exactly as every other activation row
    // does, rather than depending on that guard staying in place.
    if (QAction* a = find_action(w, "Quic&k Screenshot"))
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

// Every non-empty QAction shortcut under `root`, portable form. Takes a bare
// QObject so it serves both the main window and (group 5) the debugger window.
QStringList action_shortcuts_of(const QObject& root) {
    QStringList out;
    for (QAction* a : root.findChildren<QAction*>()) {
        const QString s = seq_of(a);
        if (!s.isEmpty()) out << s;
    }
    return out;
}

QStringList action_shortcuts(MainWindow& w) { return action_shortcuts_of(w); }

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
    // other modifier. Ctrl+F5 / Ctrl+F6 (recording) are deliberately NOT
    // matched — F-keys have no ZX matrix meaning at all, so they steal nothing.
    // Ctrl+Shift+S is not matched either, and no longer needs to be: issue #130
    // moved it to Alt+Shift+S, and H115-30 below is the row that keeps the
    // Ctrl+Shift+<letter> class clear.
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
    //
    // A `debugger_only` action in a build with no debugger asserts the
    // mirror-image claim instead (GH #273): the action is absent, and its
    // chord is therefore claimed by NOBODY — not by a leftover QAction that
    // escaped the `#ifdef`, and not by a menubar mnemonic that drifted onto
    // the letter now that nothing else holds it. That is a real property of
    // that configuration, and it fails loudly if either happens.
    for (int i = 0; i < N_HOTKEYS; ++i) {
        const Hotkey& h = HOTKEYS[i];
        QAction* a = find_action(w, h.action_text);
        char id[16], desc[192];
        std::snprintf(id, sizeof(id), "H115-%02d", 14 + i);

        if (h.debugger_only && !kDebuggerBuilt) {
            const QString chord = QString::fromUtf8(h.portable);
            QStringList claimants;
            for (const QString& s : action_shortcuts(w))
                if (s == chord) claimants << "shortcut";
            for (const QString& s : menubar_mnemonics(w))
                if (s == chord) claimants << "mnemonic";
            std::snprintf(desc, sizeof(desc),
                          "no debugger build: \"%s\" is absent and nothing binds %s",
                          h.action_text, h.portable);
            check(id, desc,
                  a == nullptr && claimants.isEmpty(),
                  std::string(a ? "action present; " : "action absent; ") +
                      ("claimed by=" + claimants.join(',')).toStdString());
            continue;
        }

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
        // does reach handle_key() — measured, not assumed.
        //
        // That stray release is inert BY CONSTRUCTION, not just in practice:
        // on an unpaired release Keyboard::set_key takes
        // `use_alt = alt_variant_[sc]` (keyboard.cpp:288-292), which was never
        // set because the press never arrived — and none of Q/O/S/R/T/D has an
        // s_alt_compound or s_alt_extkey entry at all, so use_alt resolves
        // false on the press edge too. The release takes the plain s_map path
        // and clears a bit that is already clear. The same asymmetry existed
        // for the Ctrl chords before this change; it is Qt's, not jnext's.
        // Asserting it here would pin incidental Qt behaviour, so the row
        // asserts only what matters: no spurious keystroke reaches the guest.
        bool letter_pressed_in_guest = false;
        w.set_key_callback([&letter_pressed_in_guest, &h](SDL_Scancode sc, bool pressed) {
            if (sc == h.sdl && pressed) letter_pressed_in_guest = true;
        });

        send(w, Qt::Key_Alt, Qt::NoModifier,  true);
        send(w, h.qt_key,    Qt::AltModifier, true);
        QApplication::processEvents();
        send(w, h.qt_key,    Qt::AltModifier, false);
        send(w, Qt::Key_Alt, Qt::NoModifier,  false);

        char id[16], desc[192];
        std::snprintf(id, sizeof(id), "H115-%02d", 20 + i);
        const std::string detail =
            std::string("fired=") + (fired ? "1" : "0") +
            " letter_pressed_in_guest=" + (letter_pressed_in_guest ? "1" : "0");

        if (h.debugger_only && !kDebuggerBuilt) {
            // Mirror image of the row above, measured the way this group
            // measures everything — through the LIVE shortcut map rather than
            // by enumerating QKeySequences. With the action compiled out the
            // chord belongs to no host binding, so the press is not swallowed
            // and reaches the guest. Anything that quietly claimed Alt+D in a
            // debugger-less build would swallow it and fail this row.
            std::snprintf(desc, sizeof(desc),
                          "no debugger build: %s is not a host hotkey, so it reaches the guest",
                          h.portable);
            check(id, desc, a == nullptr && !fired && letter_pressed_in_guest, detail);
        } else {
            std::snprintf(desc, sizeof(desc),
                          "%s activates \"%s\" and types nothing into the guest",
                          h.portable, h.action_text);
            check(id, desc, a != nullptr && fired && !letter_pressed_in_guest, detail);
        }

        // `fired` dies with this iteration — drop the connection with it.
        if (a) QObject::disconnect(a, nullptr, nullptr, nullptr);
        w.set_key_callback(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Group 3b — issue #130: Ctrl+Shift+S was the last chord left on Ctrl
// ---------------------------------------------------------------------------
//
// #115 moved the six PLAIN Ctrl+<letter> chords and stopped there, leaving
// Ctrl+Shift+S (Save Snapshot) behind because it is a three-key chord. But Ctrl
// is Symbol Shift and Shift is Caps Shift, so CS+SS+S is as real a guest
// keystroke as SS+O — the shortcut map swallowed it just the same. #130 moved
// it to Alt+Shift+S.
//
// Save Snapshot's handler opens a modal QFileDialog, so it is disarmed like the
// other six (see disarm_actions' comment: a row that activates it would HANG
// the suite rather than fail, which would make mutation evidence unobtainable).

const char* const SNAPSHOT_TEXT = "Save S&napshot...";

void test_snapshot_chord(MainWindow& w) {
    // H115-30 — the Ctrl+Shift+<letter> class is now empty, the same claim
    // H115-13 makes for plain Ctrl+<letter>. Restore Ctrl+Shift+S and this
    // fails.
    {
        static const QRegularExpression ctrl_shift_letter("^Ctrl\\+Shift\\+[A-Z]$");
        QStringList offenders;
        for (const QString& s : action_shortcuts(w))
            if (ctrl_shift_letter.match(s).hasMatch()) offenders << s;
        check("H115-30",
              "no QAction binds Ctrl+Shift+<letter> (that is the guest's CS+SS+letter)",
              offenders.isEmpty(),
              ("offenders=" + offenders.join(',')).toStdString());
    }

    // H115-31 — Save Snapshot carries exactly Alt+Shift+S.
    {
        QAction* a = find_action(w, SNAPSHOT_TEXT);
        check("H115-31", "action \"Save S&napshot...\" is bound to Alt+Shift+S",
              a != nullptr && seq_of(a) == QStringLiteral("Alt+Shift+S"),
              a ? ("got=" + seq_of(a)).toStdString() : "action not found");
    }

    // H115-32 — the property the issue is about, read out of the guest's own
    // matrix: Ctrl+Shift+S typed by the user arrives as Caps Shift (0,0) +
    // Symbol Shift (7,1) + S (1,1). Driven in the order a user produces it.
    {
        Keyboard kb;
        kb.reset();
        w.set_key_callback([&kb](SDL_Scancode sc, bool pressed) {
            kb.set_key(sc, pressed);
        });

        send(w, Qt::Key_Shift,   Qt::NoModifier,                          true);
        send(w, Qt::Key_Control, Qt::ShiftModifier,                       true);
        send(w, Qt::Key_S,       Qt::ShiftModifier | Qt::ControlModifier, true);

        char detail[160];
        std::snprintf(detail, sizeof(detail),
                      "caps_row0=0x%02X sym_row7=0x%02X s_row1=0x%02X",
                      kb.read_rows(0xFEu), kb.read_rows(0x7Fu), kb.read_rows(0xFDu));
        check("H115-32",
              "Ctrl+Shift+S reaches the guest as CAPS SHIFT + SYM SHIFT + S",
              key_down(kb, 0, 0) && key_down(kb, SYM_ROW, SYM_COL) && key_down(kb, 1, 1),
              detail);

        send(w, Qt::Key_S,       Qt::ShiftModifier | Qt::ControlModifier, false);
        send(w, Qt::Key_Control, Qt::ShiftModifier,                       false);
        send(w, Qt::Key_Shift,   Qt::NoModifier,                          false);
        w.set_key_callback(nullptr);
    }

    // H115-33 — the new chord really fires through the live shortcut map, and
    // the S keeps out of the guest. Same shape as H115-20..25, and the same
    // scoping: Qt consults the shortcut map only for KeyPress, so the check is
    // on the S PRESS. (The Shift key's own press does reach the guest as Caps
    // Shift, which is correct and is what H115-32 asserts.)
    //
    // This is also the row that proves Alt+Shift+S did not collide with Alt+S:
    // an AMBIGUOUS Qt shortcut fires NEITHER action, silently — the failure
    // mode measured during #115 with the &Tape mnemonic — so a collision here
    // would show up as fired=0.
    {
        QAction* snap = find_action(w, SNAPSHOT_TEXT);
        bool fired = false;
        if (snap) QObject::connect(snap, &QAction::triggered, [&fired]() { fired = true; });

        bool s_pressed_in_guest = false;
        w.set_key_callback([&s_pressed_in_guest](SDL_Scancode sc, bool pressed) {
            if (sc == SDL_SCANCODE_S && pressed) s_pressed_in_guest = true;
        });

        send(w, Qt::Key_Alt,   Qt::NoModifier,                      true);
        send(w, Qt::Key_Shift, Qt::AltModifier,                     true);
        send(w, Qt::Key_S,     Qt::AltModifier | Qt::ShiftModifier, true);
        QApplication::processEvents();
        send(w, Qt::Key_S,     Qt::AltModifier | Qt::ShiftModifier, false);
        send(w, Qt::Key_Shift, Qt::AltModifier,                     false);
        send(w, Qt::Key_Alt,   Qt::NoModifier,                      false);

        check("H115-33",
              "Alt+Shift+S activates Save Snapshot and types no S into the guest",
              snap != nullptr && fired && !s_pressed_in_guest,
              std::string("fired=") + (fired ? "1" : "0") +
              " s_pressed_in_guest=" + (s_pressed_in_guest ? "1" : "0"));

        if (snap) QObject::disconnect(snap, nullptr, nullptr, nullptr);
        w.set_key_callback(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Group 3c — issue #259: Settings > Preferences lost its shortcut on Qt 6.11.2
// ---------------------------------------------------------------------------
//
// The action carried QKeySequence::Preferences, whose meaning is Qt's platform
// table, not jnext's. On Linux that table said the Key_Settings multimedia key
// up to Qt 6.11.1 and NOTHING from Qt 6.11.2 (measured: keyBindings() is empty
// under the default, kde and gtk3 themes), so the action silently had no
// shortcut at all. It now carries a literal Alt+P.
//
// Its handler opens the modal Preferences dialog, so it is disarmed with the
// others (disarm_actions).

const char* const PREFERENCES_TEXT = "&Preferences...";

void test_preferences_chord(MainWindow& w) {
    // H115-34 — Preferences carries exactly Alt+P, on every platform and Qt
    // version, because nothing about it is resolved at run time any more. Put
    // QKeySequence::Preferences back and this fails on Qt 6.11.2 with got=''.
    {
        QAction* a = find_action(w, PREFERENCES_TEXT);
        check("H115-34", "action \"&Preferences...\" is bound to Alt+P",
              a != nullptr && seq_of(a) == QStringLiteral("Alt+P"),
              a ? ("got='" + seq_of(a) + "'").toStdString() : "action not found");
    }

    // H115-35 — Alt+P really fires Preferences through the live shortcut map
    // and types no P into the guest. Same shape and scoping as H115-33: the
    // shortcut map is consulted for KeyPress only, so the check is on the P
    // PRESS. An ambiguous Alt+P (a menu titled "&P...", say) fires neither
    // binding, so it would show up here as fired=0.
    {
        QAction* prefs = find_action(w, PREFERENCES_TEXT);
        bool fired = false;
        if (prefs) QObject::connect(prefs, &QAction::triggered, [&fired]() { fired = true; });

        bool p_pressed_in_guest = false;
        w.set_key_callback([&p_pressed_in_guest](SDL_Scancode sc, bool pressed) {
            if (sc == SDL_SCANCODE_P && pressed) p_pressed_in_guest = true;
        });

        send(w, Qt::Key_Alt, Qt::NoModifier,  true);
        send(w, Qt::Key_P,   Qt::AltModifier, true);
        QApplication::processEvents();
        send(w, Qt::Key_P,   Qt::AltModifier, false);
        send(w, Qt::Key_Alt, Qt::NoModifier,  false);

        check("H115-35",
              "Alt+P activates Preferences and types no P into the guest",
              prefs != nullptr && fired && !p_pressed_in_guest,
              std::string("fired=") + (fired ? "1" : "0") +
              " p_pressed_in_guest=" + (p_pressed_in_guest ? "1" : "0"));

        if (prefs) QObject::disconnect(prefs, nullptr, nullptr, nullptr);
        w.set_key_callback(nullptr);
    }
}

// ---------------------------------------------------------------------------
// Group 3d — GH #19: File > Quick Screenshot is a host chord too
// ---------------------------------------------------------------------------
//
// A no-dialog capture is only worth having as ONE keypress, so it needs a
// chord of its own — and every chord in this window has to come out of the Alt
// namespace, because Ctrl is Symbol Shift. Alt+K is what was free: Alt+S is
// Save Screenshot and Alt+Shift+S is Save Snapshot, Alt+Q/O/R/T/D/P are taken,
// the menu bar holds Alt+F/M/I/A/B/V/N/H, and the guest owns Alt+E/G/C
// (keyboard.cpp:163,172-174). H115-26 keeps the namespace itself unambiguous;
// these two rows pin that THIS action carries the chord and that the chord
// reaches it rather than the guest.

const char* const QUICK_SHOT_TEXT = "Quic&k Screenshot";

void test_quick_screenshot_chord(MainWindow& w) {
    // H115-36 — the action exists and carries exactly Alt+K.
    {
        QAction* a = find_action(w, QUICK_SHOT_TEXT);
        check("H115-36", "action \"Quic&k Screenshot\" is bound to Alt+K",
              a != nullptr && seq_of(a) == QStringLiteral("Alt+K"),
              a ? ("got='" + seq_of(a) + "'").toStdString() : "action not found");
    }

    // H115-37 — Alt+K fires it through the live shortcut map and types no K
    // into the guest. Same shape and scoping as H115-33/35: Qt consults the
    // shortcut map on KeyPress only, so the guest check is on the K PRESS.
    {
        QAction* quick = find_action(w, QUICK_SHOT_TEXT);
        bool fired = false;
        if (quick) QObject::connect(quick, &QAction::triggered, [&fired]() { fired = true; });

        bool k_pressed_in_guest = false;
        w.set_key_callback([&k_pressed_in_guest](SDL_Scancode sc, bool pressed) {
            if (sc == SDL_SCANCODE_K && pressed) k_pressed_in_guest = true;
        });

        send(w, Qt::Key_Alt, Qt::NoModifier,  true);
        send(w, Qt::Key_K,   Qt::AltModifier, true);
        QApplication::processEvents();
        send(w, Qt::Key_K,   Qt::AltModifier, false);
        send(w, Qt::Key_Alt, Qt::NoModifier,  false);

        check("H115-37",
              "Alt+K activates Quick Screenshot and types no K into the guest",
              quick != nullptr && fired && !k_pressed_in_guest,
              std::string("fired=") + (fired ? "1" : "0") +
              " k_pressed_in_guest=" + (k_pressed_in_guest ? "1" : "0"));

        if (quick) QObject::disconnect(quick, nullptr, nullptr, nullptr);
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

// ---------------------------------------------------------------------------
// Group 5 — no UI string or feature list names a chord the product does not bind
// ---------------------------------------------------------------------------
//
// WHY THIS GROUP EXISTS. The first cut of the #115 migration re-pointed every
// QAction and every mnemonic correctly, and still shipped two defects, because
// the enumeration stopped at QKeySequence objects and never swept for literal
// "Ctrl+<letter>" TEXT:
//
//   * debugger_manager.cpp:246 — the toolbar bug-button tooltip still read
//     "Toggle Debugger (Ctrl+D)" while that action's binding had moved to
//     Alt+D. The product was instructing the user to press the exact chord the
//     fix hands back to the guest (Ctrl+D types SS+D = STEP).
//   * FEATURES.md:64,66 — still advertised Ctrl+R and Ctrl+S.
//
// Neither was catchable: nothing tests tooltip text, and `make docs-check`
// cannot see FEATURES.md at all, so both passed every gate green.
//
// A chord named in prose is a PROMISE about a binding, and a promise is
// checkable whenever the binding is a live object — which, in a suite that
// already drives a real MainWindow, it is.

// Any modifier chord, e.g. "Alt+D", "Ctrl+Shift+S", "Shift+F6".
// Deliberately requires a modifier: a bare "F9" in a tooltip is a key handled
// in keyPressEvent, not a QAction shortcut, and is not this group's business.
// "Ctrl+Alt" (the mouse-capture release) does not match either — there is no
// key after the second modifier — which is why this group needs no exception
// table today. If one is ever needed it belongs HERE, declared with its
// reason, never as a pattern that quietly skips the offending string.
const char* const CHORD_RE =
    "\\b(?:Ctrl|Alt|Shift|Meta)(?:\\+(?:Ctrl|Alt|Shift|Meta))*\\+(?:F\\d{1,2}|[A-Z])\\b";

QStringList chords_in(const QString& text) {
    static const QRegularExpression re(CHORD_RE);
    QStringList out;
    auto it = re.globalMatch(text);
    while (it.hasNext()) out << it.next().captured(0);
    return out;
}

// H115-28 — every modifier chord appearing in a QAction's user-visible text,
// tooltip or status tip must name a REAL binding. Two tiers:
//   * the action has its own shortcut  -> the chord must BE that shortcut;
//   * the action has none (a toolbar twin of a menu action, like the bug
//     button) -> the chord must at least be bound by some live action.
// The second tier is what catches the tooltip defect: after the migration
// "Ctrl+D" was bound by nothing at all.
void test_ui_strings(MainWindow& w2, const QStringList& live) {
    QStringList bad;
    for (QAction* a : w2.findChildren<QAction*>()) {
        const QString own = seq_of(a);
        for (const QString& src : {a->text(), a->toolTip(), a->statusTip()}) {
            for (const QString& c : chords_in(src)) {
                const bool ok = own.isEmpty() ? live.contains(c) : (c == own);
                if (!ok)
                    bad << (a->text() + " says " + c +
                            (own.isEmpty() ? " but nothing binds it"
                                           : " but it is bound to " + own));
            }
        }
    }
    check("H115-28",
          "no QAction text/tooltip/statustip names a chord the product does not bind",
          bad.isEmpty(), ("offenders: " + bad.join(" | ")).toStdString());
}

// H115-29 — the same promise, made in FEATURES.md.
//
// SCOPE, stated precisely so nobody reads more into a green row than it earns.
// This covers FEATURES.md ONLY. It deliberately does NOT scan doc/man/jnext.1.md
// or src/doc/user-guide/**, even though those are exactly the "prose no gate can
// see" files, because both now carry a paragraph that names Ctrl+O/D/R/T/Q/S ON
// PURPOSE — to tell the reader those chords reach the guest. Scanning them would
// need the checker to skip that passage, and a checker exclusion is precisely
// how a real defect hides. Those files stay a human-review responsibility.
// README.md is not scanned either: it contains no chord at all today, so the
// branch would be vacuous.
//
// BUILD CONFIGURATION (GH #273). FEATURES.md describes the SHIPPED build, which
// has the debugger in it; this suite is gated on ENABLE_QT_UI, which does not
// imply ENABLE_DEBUGGER. A Qt-only build binds neither the disassembly panel's
// Ctrl+A / Ctrl+C nor rewind's Shift+F6 / Shift+F7, so the unmodified row
// failed there on four chords the product is right not to bind and the document
// is right to advertise. The fix is NOT a list of chords to forgive — that is
// the checker exclusion this comment already warns about, and it would rot the
// moment a debugger feature gained a chord. It is to scope the CLAIM to what
// this build can witness, by dropping FEATURES.md's own "## Debugger" section:
// the document's structure is the product's own statement of which features
// belong to the debugger, so the scoping tracks new debugger chords by itself.
// The default build is untouched and still reads the whole file.
QString features_md_in_scope(const QString& text, QString& why_not) {
    if (kDebuggerBuilt) return text;
    // Match "## Debugger (Qt 6)" and any re-titling of it, up to the next
    // top-level section. A heading that no longer matches is reported, never
    // silently ignored: this row's meaning in this configuration rests on that
    // section existing.
    static const QRegularExpression head("^##\\s+Debugger\\b.*$",
                                         QRegularExpression::MultilineOption);
    static const QRegularExpression next("^##\\s",
                                         QRegularExpression::MultilineOption);
    const auto m = head.match(text);
    if (!m.hasMatch()) {
        why_not = "FEATURES.md has no '## Debugger' section to scope out";
        return text;
    }
    const int start = m.capturedStart();
    const int after = next.match(text, m.capturedEnd()).capturedStart();
    return text.left(start) + (after < 0 ? QString() : text.mid(after));
}

void test_features_md(const QStringList& live) {
    QFile f(QString::fromUtf8(JNEXT_FEATURES_MD));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        check("H115-29", "every chord in FEATURES.md is one the product binds",
              false, std::string("cannot open ") + JNEXT_FEATURES_MD);
        return;
    }
    QString why_not;
    const QString text =
        features_md_in_scope(QString::fromUtf8(f.readAll()), why_not);
    const char* desc = kDebuggerBuilt
        ? "every chord in FEATURES.md is one the product binds"
        : "every chord in FEATURES.md outside its Debugger section is one the product binds";
    const QStringList found = chords_in(text);
    QStringList bad;
    for (const QString& c : found)
        if (!live.contains(c)) bad << c;
    // `!found.isEmpty()` is part of the condition on purpose: an unreadable
    // file or a broken pattern must FAIL, not pass vacuously. So must a
    // Debugger section this build could not find — `why_not` is empty on the
    // path that did find one, and on every path of a debugger build.
    check("H115-29", desc,
          why_not.isEmpty() && !found.isEmpty() && bad.isEmpty(),
          (why_not.isEmpty()
               ? "found=" + found.join(',') + " unbound=" + bad.join(',')
               : why_not).toStdString());
}

// ---------------------------------------------------------------------------
// Group 6 (GH #268) — the menu bar must never take the keyboard from the guest
//
// The third symptom of #268, reported as: "press A I got A, press G nothing is
// shown, but somehow the focus went to the UI menu of the application".
// Reproduced on the product and traced end to end; two distinct mechanisms,
// both of which are about Qt taking the keyboard away MID-KEYSTROKE.
//
//   1. FOCUS THEFT. QMenuBar arms itself on the Alt SHORTCUT-OVERRIDE and, on
//      the matching Alt key-up with nothing in between, calls
//      setKeyboardMode(true) -> setFocus() (qmenubar.cpp:252,1463). The next
//      letter that is a top-level mnemonic (F/M/I/A/B/V/N/H — A is "T&ape") is
//      then eaten by the menu bar and opens that popup. Nothing on screen says
//      focus has left the emulator.
//
//   2. THE KEY-UP THAT NEVER ARRIVES. Whatever took the keyboard is where the
//      key-ups go. Measured: Alt+F consumes both the `F` key-up AND the `Alt`
//      key-up, so Keyboard::alt_held_ stays true for the rest of the session —
//      after which E/G/C resolve to their ALT variants, EDIT / GRAPH /
//      CAPS LOCK (keyboard.cpp:248-250), and those three letters silently stop
//      appearing while every other letter still types. That is the report,
//      word for word. With an ordinary letter stranded instead, the 48K ROM's
//      own auto-repeat fills the BASIC line with it.
//
// DELIVERY MECHANISM — why these rows do not use send() like the rest of the
// file. QApplication::sendEvent() delivers a KeyPress and nothing else; the
// ShortcutOverride that arms QMenuBar is synthesised by Qt from a PLATFORM key
// event, so a sendEvent-driven row cannot see mechanism 1 at all and would
// pass with the fix reverted. H268-01/02/03/08 go through
// QTest::keyPress/keyRelease on the real QWindow, which is the same path a
// keystroke takes in the product. The modifier state is the platform's, not
// ours: an Alt key-DOWN really does arrive carrying Qt::AltModifier (measured
// on the running product), and passing NoModifier there would silently disarm
// the very filter under test.
// ---------------------------------------------------------------------------

// G's own matrix cell (keyboard.cpp:156) and Caps Shift's (keyboard.cpp:144).
// GRAPH is CS+9, so "the guest saw a plain G" and "the guest saw GRAPH" differ
// in both: G's cell down and Caps Shift UP is the letter; Caps Shift down is
// the extended key.
constexpr int G_ROW = 1, G_COL = 4;
constexpr int CS_ROW = 0, CS_COL = 0;

void plat_press(MainWindow& w, Qt::Key k, Qt::KeyboardModifiers mods) {
    if (QWindow* wh = w.windowHandle()) QTest::keyPress(wh, k, mods);
    QApplication::processEvents();
}

void plat_release(MainWindow& w, Qt::Key k, Qt::KeyboardModifiers mods) {
    if (QWindow* wh = w.windowHandle()) QTest::keyRelease(wh, k, mods);
    QApplication::processEvents();
}

/// A bare Alt tap, exactly as the platform delivers one: the key-DOWN carries
/// Qt::AltModifier, the key-UP does not.
void tap_alt(MainWindow& w) {
    plat_press(w, Qt::Key_Alt, Qt::AltModifier);
    plat_release(w, Qt::Key_Alt, Qt::NoModifier);
}

void test_menu_focus(MainWindow& w) {
    // The whole group is about where keyboard focus is, so a window that never
    // got it would make every row vacuous. Say so instead.
    w.setFocus();
    QApplication::processEvents();
    const bool focused_at_start = (QApplication::focusWidget() == &w);
    check("H268-00", "fixture: the emulator window starts with keyboard focus",
          focused_at_start && w.windowHandle() != nullptr,
          std::string("focus=") +
              (QApplication::focusWidget()
                   ? QApplication::focusWidget()->metaObject()->className()
                   : "(null)") +
              " windowHandle=" + (w.windowHandle() ? "yes" : "no"));

    // --- 1. the style hint, which is what makes the rest of it impossible ---
    check("H268-04", "the menu bar's style reports Alt-key navigation OFF",
          w.menuBar()->style()->styleHint(QStyle::SH_MenuBar_AltKeyNavigation,
                                          nullptr, w.menuBar()) == 0,
          "menubar hint=" +
              std::to_string(w.menuBar()->style()->styleHint(
                  QStyle::SH_MenuBar_AltKeyNavigation, nullptr, w.menuBar())));

    // --- 2. a bare Alt tap changes nothing ---
    tap_alt(w);
    QWidget* after = QApplication::focusWidget();
    check("H268-01", "a bare Alt tap leaves keyboard focus on the emulator window",
          after == &w,
          std::string("focus=") +
              (after ? after->metaObject()->className() : "(null)"));

    // ...and the letter that follows it still reaches the guest. A is the
    // sharpest choice available: it is the "T&ape" mnemonic, so it is exactly
    // the letter the menu bar would swallow.
    bool a_pressed = false;
    w.set_key_callback([&a_pressed](SDL_Scancode sc, bool pressed) {
        if (sc == SDL_SCANCODE_A && pressed) a_pressed = true;
    });
    plat_press(w, Qt::Key_A, Qt::NoModifier);
    plat_release(w, Qt::Key_A, Qt::NoModifier);
    check("H268-02", "and the letter after it still reaches the guest, mnemonic or not",
          a_pressed, std::string("a_pressed=") + (a_pressed ? "1" : "0"));
    w.set_key_callback(nullptr);

    // --- 3. the scope guard: Alt+<mnemonic> must still open its menu ---
    //
    // SH_MenuBar_AltKeyNavigation does NOT gate the mnemonic itself
    // (qmenubar.cpp:1687 opens the popup either way and consults the hint only
    // for whether to ALSO enter keyboard mode), and this row is what keeps that
    // reading honest rather than asserted.
    QMenu* file_menu = nullptr;
    for (QAction* m : w.menuBar()->actions())
        if (m->text() == QStringLiteral("&File")) file_menu = m->menu();

    plat_press(w, Qt::Key_Alt, Qt::AltModifier);
    plat_press(w, Qt::Key_F, Qt::AltModifier);
    const bool opened = file_menu && file_menu->isVisible()
                        && QApplication::activePopupWidget() != nullptr;
    check("H268-03", "Alt+F still opens the File menu",
          opened,
          std::string("menu=") + (file_menu ? "found" : "missing") +
              " visible=" + (file_menu && file_menu->isVisible() ? "1" : "0") +
              " popup=" + (QApplication::activePopupWidget() ? "1" : "0"));

    // --- 4. while that popup is up, the guest gets nothing ---
    //
    // QMenu::keyPressEvent forwards an unmatched key-DOWN back to the menu bar,
    // which propagates it up to this window, and swallows the matching key-UP.
    // Measured on the product: one tap of `g` with a menu open delivered a
    // key-down and no key-up at all, and the guest held G until the ROM had
    // filled the line with it.
    bool guest_saw_anything = false;
    w.set_key_callback([&guest_saw_anything](SDL_Scancode, bool) {
        guest_saw_anything = true;
    });
    send(w, Qt::Key_G, Qt::NoModifier, true);
    send(w, Qt::Key_G, Qt::NoModifier, false);
    check("H268-05", "a key arriving while a menu is open reaches the guest not at all",
          opened && !guest_saw_anything,
          std::string("popup_was_open=") + (opened ? "1" : "0") +
              " guest_saw=" + (guest_saw_anything ? "1" : "0"));
    w.set_key_callback(nullptr);

    // Put the window back the way the group found it. Escape would do it via
    // the popup, but closing the menu directly is what the next rows need and
    // does not depend on the popup's key handling.
    if (file_menu) file_menu->close();
    plat_release(w, Qt::Key_F, Qt::AltModifier);
    plat_release(w, Qt::Key_Alt, Qt::NoModifier);
    w.setFocus();
    QApplication::processEvents();

    // --- 5. losing the keyboard tells the frontend so ---
    int lost = 0;
    w.set_keyboard_lost_callback([&lost]() { ++lost; });
    w.clearFocus();
    QApplication::processEvents();
    check("H268-06", "losing keyboard focus reports it, so held keys can be dropped",
          lost == 1, "callbacks=" + std::to_string(lost));
    w.set_keyboard_lost_callback(nullptr);
    w.setFocus();
    QApplication::processEvents();
}

// ---------------------------------------------------------------------------
// The reported symptom itself, through the production key path.
//
// MainWindow -> host_key_latch::Router -> Keyboard, wired exactly as
// qt_app.cpp:278-289 wires it, then the matrix is read back through
// read_rows() — the same call port 0xFE makes. The row asserts the USER-VISIBLE
// fact: after the keyboard has been taken away mid-Alt and given back, `G`
// types a G. With host Alt stranded it types GRAPH instead, which prints
// nothing at all, and every other letter keeps working — "press A I got A,
// press G nothing is shown".
// ---------------------------------------------------------------------------
void test_stranded_alt() {
    // ITS OWN WINDOW, and its own drained event queue. Neither is what was
    // actually broken here — see kb.reset() below for that — but the groups
    // above drive this suite's shared MainWindow through QTest's PLATFORM key
    // path, which POSTS events rather than delivering them, and through a real
    // menu popup. A row that ends by reading a key matrix should not be
    // downstream of either. Isolation is cheap; diagnosing a leak into it is
    // not.
    MainWindow w;
    w.show();
    w.activateWindow();
    w.setFocus();
    for (int i = 0; i < 20; ++i) {
        QApplication::sendPostedEvents();
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    }

    Keyboard kb;
    // THE BUG THIS ROW SHIPPED WITH, recorded because it nearly went unnoticed.
    // Keyboard::reset() is what builds the static scancode maps and clears the
    // matrix (keyboard.cpp:294 -> init_map); every other Keyboard in this file
    // calls it and this row did not. Without it the row read G and CAPS SHIFT
    // as already down and asserted against uninitialised state — it PASSED, and
    // it passed under mutation too, for reasons that had nothing to do with
    // what it claims to test. The matrix_clean precondition below is what
    // exposed it, and it stays for the same reason.
    kb.reset();
    host_key_latch::Router<Keyboard, SDL_Scancode> router;
    router.attach(kb);
    // The PRODUCTION wiring, called rather than copied: qt_app.cpp connects the
    // window to the router through this same function. A copy here would leave
    // both callbacks free to be deleted from the product in silence — measured,
    // before wire_host_keys() existed, by deleting them and watching every row
    // stay green.
    wire_host_keys(w, router);

    // Two preconditions, ASSERTED rather than assumed, because clearFocus() is
    // silent on a widget that does not have focus and an already-dirty matrix
    // would make the outcome meaningless. Both appear in the detail string, so
    // a failure says which half broke.
    const bool focused_before = (QApplication::focusWidget() == &w);
    const bool matrix_clean   = !key_down(kb, G_ROW, G_COL)
                                && !key_down(kb, CS_ROW, CS_COL);

    // Host Alt goes down and its key-up is delivered somewhere else — the exact
    // shape of what Alt+F does (both the F and the Alt key-up go to the menu).
    send(w, Qt::Key_Alt, Qt::NoModifier, true);
    router.on_tick_end(1);                 // a frame samples the matrix

    // NOTHING is installed over wire_host_keys() here, deliberately. An earlier
    // draft wrapped the keyboard-lost callback to count it, and that silently
    // COST the row its reach: with the production callback replaced, deleting
    // it from wire_host_keys() failed nothing at all. "release_all() did not
    // fix this" and "nothing ever told it to" are still separable — H268-06 is
    // the second half, and it is the row to read when this one fails.
    w.clearFocus();                        // the keyboard goes elsewhere
    QApplication::processEvents();
    w.setFocus();                          // ...and comes back
    QApplication::processEvents();

    // Now type G, as the reporter did.
    send(w, Qt::Key_G, Qt::NoModifier, true);
    router.on_tick_end(1);

    const bool g_down  = key_down(kb, G_ROW, G_COL);
    const bool cs_down = key_down(kb, CS_ROW, CS_COL);
    check("H268-07", "after the keyboard is taken away mid-Alt, G still types G",
          focused_before && matrix_clean && g_down && !cs_down,
          std::string("focused_before=") + (focused_before ? "1" : "0") +
              " matrix_clean=" + (matrix_clean ? "1" : "0") +
              " G_cell=" + (g_down ? "down" : "up") +
              " CapsShift=" + (cs_down ? "down (GRAPH)" : "up"));
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
    test_snapshot_chord(w);
    test_preferences_chord(w);
    test_quick_screenshot_chord(w);
    test_alt_namespace(w);
    test_menu_focus(w);
    test_stranded_alt();

    // Group 5 needs a SECOND window, with an emulator attached: the bug-button
    // whose tooltip regressed lives on the debug toolbar, and MainWindow only
    // builds that (via DebuggerManager) once it has an emulator. Kept separate
    // from `w` so groups 1-4 keep running against the bare window they were
    // written and mutation-verified against.
    {
        Emulator emu;
        EmulatorConfig cfg;
        emu.init(cfg);

        MainWindow w2;
        w2.set_emulator(&emu);
        QApplication::processEvents();

        // The "live bindings" set spans BOTH windows. The debugger window is a
        // separate top level (parent nullptr, debugger_manager.cpp:171), so its
        // Shift+F6 / Shift+F7 are invisible to w2.findChildren — and FEATURES.md
        // advertises both. One is built here purely to harvest its shortcuts.
        //
        // set_debugger_manager() is what builds its menus (debugger_window.cpp:
        // 200-204) — the constructor alone yields an empty window, which is how
        // the first cut of this row false-failed on Shift+F6/F7. It is handed
        // the manager w2 already owns, never shown, and destroyed at once; the
        // manager's own window (ensure_window) is never created, so no debugger
        // state is toggled and the machine is not paused.
        QStringList live = action_shortcuts(w2);
#ifdef ENABLE_DEBUGGER
        {
            DebuggerWindow dbg(&emu, nullptr);
            dbg.set_debugger_manager(w2.debugger_manager());
            live += action_shortcuts_of(dbg);
        }
#endif
        live.removeDuplicates();

        test_ui_strings(w2, live);
        test_features_md(live);
    }
    std::printf("  Group: H115           - done\n");

    std::printf("\n==============================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
