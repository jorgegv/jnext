// Captured-mouse release gesture, through the real MainWindow (GitHub #365).
//
// No VHDL oracle: host UI. The oracle is the design table of the issue plan:
//   * three exclusive methods; the default (ctrl-alt) is the historical
//     behaviour: Ctrl+Alt releases and is NOT consumed (Alt must still reach
//     Keyboard::alt_held_), the middle button stays a Kempston button, and Esc
//     is the ZX BREAK key;
//   * esc and middle-click are consumed whole: the press, and for Esc its
//     release, never reach the ZX keyboard / Kempston mouse;
//   * uncaptured, Esc is BREAK and any click on the viewport captures;
//   * every hint names the active gesture; Preferences > Input has the combo and
//     Apply changes the method live.
//
// Events go through QApplication::sendEvent on the real window; capture goes
// through the real "Capture &Mouse" action. Offscreen QPA, JNEXT_CONFIG_DIR in a
// temp dir (apply_preferences persists).
//
// Declared equivalent (not killable): forwarding the middle press to the
// dispatcher BEFORE the release in the same handler. set_mouse_captured(false)
// resets the dispatcher before any frame can read port 0xFADF.
//
// Run: ./build/test/mouse_release_test

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "gui/app_config.h"
#include "gui/main_window.h"
#include "gui/preferences_dialog.h"
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

using RM = pointer_capture::ReleaseMethod;

struct KeyEv { SDL_Scancode sc; bool pressed; };

struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    MainWindow win;
    std::vector<KeyEv> keys;
    QAction* capture = nullptr;

    explicit Fixture(RM method = RM::CtrlAlt)
    {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win.set_debugger(backend.get());
        win.set_emulator(&emu);
        win.set_key_callback([this](SDL_Scancode sc, bool pressed) { keys.push_back({sc, pressed}); });
        if (method != RM::CtrlAlt) {
            AppConfigData c;
            c.mouse_release = method;
            win.apply_startup_config(c);
        }
        for (QAction* a : win.findChildren<QAction*>())
            if (a->text() == QStringLiteral("Capture &Mouse")) capture = a;
    }

    bool captured() const { return capture && capture->isChecked(); }
    void capture_now() { if (capture && !capture->isChecked()) capture->trigger(); }

    void key(Qt::Key k, bool down, Qt::KeyboardModifiers m = Qt::NoModifier)
    {
        QKeyEvent ev(down ? QEvent::KeyPress : QEvent::KeyRelease, k, m);
        QApplication::sendEvent(&win, &ev);
    }
    void button(Qt::MouseButton b, bool down)
    {
        QMouseEvent ev(down ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
                       QPointF(10, 10), QPointF(10, 10), b, down ? b : Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(&win, &ev);
    }
    int count(SDL_Scancode sc, bool pressed) const
    {
        int n = 0;
        for (const auto& e : keys) if (e.sc == sc && e.pressed == pressed) ++n;
        return n;
    }
};

const Qt::KeyboardModifiers kCtrlAlt = Qt::ControlModifier | Qt::AltModifier;

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfgdir;
    qputenv("JNEXT_CONFIG_DIR", cfgdir.path().toUtf8());
    QApplication app(argc, argv);

    {   // MRQ-01
        Fixture f;
        f.capture_now();
        const bool was = f.captured();
        f.key(Qt::Key_Alt, true, kCtrlAlt);
        check("MRQ-01", "default: captured Ctrl+Alt releases, and the Alt press still reaches the ZX keyboard",
              was && !f.captured() && f.count(SDL_SCANCODE_LALT, true) == 1);
    }
    {   // MRQ-02
        Fixture f(RM::Esc);
        f.capture_now();
        const bool was = f.captured();
        f.key(Qt::Key_Escape, true);
        f.key(Qt::Key_Escape, false);
        check("MRQ-02", "esc: a captured Esc press+release releases and neither reaches the ZX keyboard",
              was && !f.captured() && f.count(SDL_SCANCODE_ESCAPE, true) == 0 &&
                  f.count(SDL_SCANCODE_ESCAPE, false) == 0,
              std::to_string(f.keys.size()));
    }
    {   // MRQ-03
        Fixture f(RM::Esc);
        f.capture_now();
        f.key(Qt::Key_Alt, true, kCtrlAlt);
        check("MRQ-03", "esc: captured Ctrl+Alt does not release", f.captured());
    }
    {   // MRQ-04
        Fixture f(RM::Esc);
        f.key(Qt::Key_Escape, true);
        f.key(Qt::Key_Escape, false);
        check("MRQ-04", "esc: an uncaptured Esc press and release both reach the ZX keyboard (BREAK intact)",
              f.count(SDL_SCANCODE_ESCAPE, true) == 1 && f.count(SDL_SCANCODE_ESCAPE, false) == 1);
    }
    {   // MRQ-05
        Fixture f(RM::Esc);
        f.capture_now();
        f.key(Qt::Key_Escape, true);                 // releases; its key-up goes elsewhere
        QFocusEvent out(QEvent::FocusOut);
        QApplication::sendEvent(&f.win, &out);
        f.key(Qt::Key_Escape, true);
        check("MRQ-05", "esc: after a focus loss the swallow is forgotten and an uncaptured Esc is BREAK",
              !f.captured() && f.count(SDL_SCANCODE_ESCAPE, true) == 1);
    }
    {   // MRQ-06
        Fixture f(RM::MiddleClick);
        f.capture_now();
        f.button(Qt::MiddleButton, true);
        const int after_press = f.emu.mouse().buttons();
        f.button(Qt::MiddleButton, false);
        check("MRQ-06", "middle-click: a captured middle press releases; Kempston buttons stay 0 (no stuck bit)",
              !f.captured() && after_press == 0 && f.emu.mouse().buttons() == 0,
              std::to_string(after_press));
    }
    {   // MRQ-07
        Fixture f(RM::MiddleClick);
        f.capture_now();
        f.button(Qt::LeftButton, true);
        check("MRQ-07", "middle-click: a captured left press is Kempston bit 1 (0x02) and keeps the capture",
              f.captured() && f.emu.mouse().buttons() == 0x02, std::to_string(f.emu.mouse().buttons()));
    }
    {   // MRQ-08
        Fixture f;
        f.capture_now();
        f.button(Qt::MiddleButton, true);
        check("MRQ-08", "default: a captured middle press is Kempston bit 2 (0x04) and keeps the capture",
              f.captured() && (f.emu.mouse().buttons() & 0x04), std::to_string(f.emu.mouse().buttons()));
    }
    {   // MRQ-09
        Fixture f(RM::MiddleClick);
        f.capture_now();
        f.key(Qt::Key_Alt, true, kCtrlAlt);
        check("MRQ-09", "middle-click: captured Ctrl+Alt does not release", f.captured());
    }
    {   // MRQ-10
        Fixture f(RM::Esc);
        const QString base = f.win.windowTitle();
        f.capture_now();
        const QString title = f.win.windowTitle();
        check("MRQ-10a", "esc: the captured title starts with the base title and says Esc to release mouse",
              title.startsWith(base) && title.contains(QStringLiteral("Esc to release mouse")),
              title.toStdString());
        check("MRQ-10b", "esc: the status bar reads exactly 'Mouse captured - Esc to release'",
              f.win.statusBar()->currentMessage() == QStringLiteral("Mouse captured - Esc to release"),
              f.win.statusBar()->currentMessage().toStdString());
        Fixture g(RM::MiddleClick);
        const QString tip = g.capture ? g.capture->statusTip() : QString();
        check("MRQ-10c", "middle-click: the Capture Mouse status tip names 'middle click to release'",
              tip.contains(QStringLiteral("middle click to release")) && !tip.contains(QStringLiteral("%1")),
              tip.toStdString());
    }
    {   // MRQ-11
        Fixture f;
        AppConfigData c;
        c.machine_type = MachineType::ZX48K;
        c.mouse_release = RM::Esc;
        f.win.apply_preferences(c);
        f.capture_now();
        f.key(Qt::Key_Escape, true);
        check("MRQ-11", "apply_preferences switches the method live: a captured Esc then releases",
              !f.captured() && f.count(SDL_SCANCODE_ESCAPE, true) == 0);
    }
    {   // MRQ-12
        AppConfigData initial;
        initial.mouse_release = RM::Esc;
        PreferencesDialog dlg(initial);
        auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("mouseReleaseCombo"));
        const bool have = combo != nullptr;
#ifdef Q_OS_MACOS
        const char* first = "Alt+Cmd";
#else
        const char* first = "Ctrl+Alt";
#endif
        check("MRQ-12a", "Preferences > Input has mouseReleaseCombo: ctrl-alt, middle-click, esc in order",
              have && combo->count() == 3 &&
                  combo->itemData(0).toInt() == static_cast<int>(RM::CtrlAlt) &&
                  combo->itemData(1).toInt() == static_cast<int>(RM::MiddleClick) &&
                  combo->itemData(2).toInt() == static_cast<int>(RM::Esc) &&
                  combo->itemText(0) == QString::fromLatin1(first) &&
                  combo->itemText(1) == QStringLiteral("middle click") &&
                  combo->itemText(2) == QStringLiteral("Esc"));
        check("MRQ-12b", "the combo is seeded from the saved method (Esc)",
              have && combo->currentData().toInt() == static_cast<int>(RM::Esc));

        AppConfigData collected;
        bool emitted = false;
        QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                         [&](const AppConfigData& c) { collected = c; emitted = true; });
        auto* buttons = dlg.findChild<QDialogButtonBox*>();
        buttons->button(QDialogButtonBox::Apply)->click();
        check("MRQ-12d", "with no change, collect() keeps Esc",
              emitted && collected.mouse_release == RM::Esc);
        emitted = false;
        if (have) combo->setCurrentIndex(1);
        buttons->button(QDialogButtonBox::Apply)->click();
        check("MRQ-12c", "selecting middle click makes collect() return MiddleClick",
              emitted && collected.mouse_release == RM::MiddleClick);
    }

    std::printf("\n==============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
