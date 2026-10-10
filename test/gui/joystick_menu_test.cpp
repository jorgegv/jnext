// ===========================================================================
// Choose which physical controller drives each joystick port (GitHub #311).
//
// No VHDL oracle: host UI and host input wiring (the FPGA only sees
// i_JOY_LEFT/RIGHT, zxnext.vhd:3441-3442). The oracle is the issue's
// acceptance text (every detected controller per connector plus Cursor Keys +
// Space and None, the choice applies live and persists by device identity, a
// missing device falls back, hot-plug updates the lists) and the GH #311 plan's
// policy (input/joy_assign.h). Device ids are read back from SDL.
//
// The host is wired exactly as QtApp::wire_gamepad_and_sources wires it (both
// Emulator callbacks, on_devices_changed -> the menu, the device provider),
// because the feature is the wiring: a menu that is perfect and not connected
// to the host passes every unit row. QtApp itself cannot be constructed here,
// so JMN-12 pins the wiring lines in its source.
//
// Controllers are SDL virtual joysticks and GamepadHost::set_virtual_only()
// hides physical ones, so a pad plugged into the machine running the suite
// cannot change what a row sees.
//
// Run: ./build/test/joystick_menu_test
// ===========================================================================

#include "gui/main_window.h"
#include "gui/preferences_dialog.h"

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "debug/debugger.h"
#include "input/gamepad_host.h"
#include "input/joy_assign.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>

#include <SDL3/SDL.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
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

SDL_JoystickID attach(const char* name, Uint16 vendor, Uint16 product, bool raw) {
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.vendor_id  = vendor;
    desc.product_id = product;
    desc.name       = name;
    if (raw) {
        desc.type     = SDL_JOYSTICK_TYPE_UNKNOWN;
        desc.naxes    = 2;
        desc.nbuttons = 2;
    } else {
        desc.type     = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes    = 6;
        desc.nbuttons = 11;
        desc.nhats    = 1;
        desc.button_mask = (1u << SDL_GAMEPAD_BUTTON_SOUTH) | (1u << SDL_GAMEPAD_BUTTON_EAST) |
                           (1u << SDL_GAMEPAD_BUTTON_WEST)  | (1u << SDL_GAMEPAD_BUTTON_NORTH) |
                           (1u << SDL_GAMEPAD_BUTTON_DPAD_UP)   | (1u << SDL_GAMEPAD_BUTTON_DPAD_DOWN) |
                           (1u << SDL_GAMEPAD_BUTTON_DPAD_LEFT) | (1u << SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
        desc.axis_mask = (1u << SDL_GAMEPAD_AXIS_LEFTX) | (1u << SDL_GAMEPAD_AXIS_LEFTY);
    }
    return SDL_AttachVirtualJoystick(&desc);
}

std::string guid_of(SDL_JoystickID iid) {
    char buf[33] = {};
    SDL_GUIDToString(SDL_GetJoystickGUIDForID(iid), buf, sizeof(buf));
    return buf;
}

void feed(GamepadHost& host, SDL_EventType t, SDL_JoystickID iid) {
    SDL_Event e{};
    e.type = t;
    e.jdevice.which = iid;
    host.handle_event(e);
}

// A machine, a shown window and a host, wired as QtApp::wire_gamepad_and_sources
// does. Three controllers: two identical pads and a raw stick whose name has an
// ampersand.
struct Fixture {
    Emulator emu;
    std::unique_ptr<jnext::dbg::Debugger> backend;
    std::unique_ptr<MainWindow> w;
    std::unique_ptr<GamepadHost> host;
    SDL_JoystickID a1 = 0, a2 = 0, c = 0;
    std::string G, G2, H;

    Fixture() {
        EmulatorConfig cfg;
        cfg.type = MachineType::ZX48K;
        emu.init(cfg);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        w = std::make_unique<MainWindow>();
        w->set_debugger(backend.get());
        w->set_emulator(&emu);

        a1 = attach("jnext pad A", 0x1234, 0x0001, false);
        a2 = attach("jnext pad A", 0x1234, 0x0001, false);
        c  = attach("A&B stick", 0x5678, 0x0002, true);
        G  = guid_of(a1);
        G2 = G + "#2";
        H  = guid_of(c);

        host = std::make_unique<GamepadHost>(emu.joystick());
        emu.keyboard().set_joystick_dispatcher(&host->dispatcher());
        emu.on_joystick_source_changed = [this](int slot, JoySource src) { host->set_source(slot, src); };
        emu.on_joystick_device_changed = [this](int slot, const JoyDeviceRef& r) { host->set_device(slot, r); };
        host->on_devices_changed = [this] { w->sync_joy_source_menu(); };
        w->set_joy_device_provider([this] { return host->devices(); });
        emu.refresh_joystick_sources();
        w->sync_joy_source_menu();
        host->enumerate_existing_devices();
        w->show();
        QApplication::processEvents();
    }
    ~Fixture() {
        w.reset();
        host.reset();
        for (SDL_JoystickID i : { a1, a2, c }) SDL_DetachVirtualJoystick(i);
        backend.reset();
    }

    QMenu* submenu(int conn) {
        const QString title = conn == 0 ? "Joy &1 Source (port 0x1F)" : "Joy &2 Source (port 0x37)";
        for (QAction* top : w->menuBar()->actions()) {
            if (!top->menu()) continue;
            for (QAction* sub : top->menu()->actions())
                if (sub->menu() && sub->text() == title) return sub->menu();
        }
        return nullptr;
    }
    std::vector<QString> texts(int conn) {
        std::vector<QString> v;
        if (QMenu* m = submenu(conn))
            for (QAction* a : m->actions()) v.push_back(a->isSeparator() ? QString("-") : a->text());
        return v;
    }
    QAction* find(int conn, const QString& text) {
        if (QMenu* m = submenu(conn))
            for (QAction* a : m->actions()) if (a->text() == text) return a;
        return nullptr;
    }
    QString checked(int conn) {
        if (QMenu* m = submenu(conn))
            for (QAction* a : m->actions()) if (a->isChecked()) return a->text();
        return {};
    }
    int slot(SDL_JoystickID i) { return host->dispatcher().slot_for_instance(i); }
};

QSettings saved_conf(const QString& dir) {
    return QSettings(dir + "/jnext.conf", QSettings::IniFormat);
}

std::string strip_comments(const std::string& src) {
    std::string code;
    std::istringstream ls(src);
    for (std::string line; std::getline(ls, line);) {
        const size_t c = line.find("//");
        code += (c == std::string::npos ? line : line.substr(0, c)) + "\n";
    }
    return code;
}
std::string slurp(const char* path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
size_t count_of(const std::string& hay, const std::string& needle) {
    size_t n = 0;
    for (size_t p = hay.find(needle); p != std::string::npos; p = hay.find(needle, p + 1)) ++n;
    return n;
}

// The two Joy combos of a Preferences dialog: the ones with an "auto" entry,
// in creation order (Joy 1 then Joy 2).
std::vector<QComboBox*> joy_combos(PreferencesDialog& d) {
    std::vector<QComboBox*> v;
    for (QComboBox* c : d.findChildren<QComboBox*>())
        if (c->findData(QStringLiteral("auto")) >= 0) v.push_back(c);
    return v;
}

AppConfigData apply_dialog(PreferencesDialog& d) {
    AppConfigData got;
    QObject::connect(&d, &PreferencesDialog::apply_requested,
                     [&got](const AppConfigData& cfg) { got = cfg; });
    d.findChild<QDialogButtonBox*>()->button(QDialogButtonBox::Apply)->click();
    return got;
}

void run(const QString& confdir) {
    const QString auto_txt = "&Automatic (first free controller)";
    const QString keys_txt = "Cursor &Keys + Space";

    // JMN-01 / JMN-02 — what the menu lists, and what is ticked at the start.
    {
        Fixture f;
        const std::vector<QString> want = { auto_txt, "jnext pad A", "jnext pad A #2",
                                            "A&&B stick", "-", keys_txt, "None" };
        check("JMN-01", "Joy 1 submenu lists Automatic, every controller (identical ones numbered, & escaped), Cursor Keys, None",
              f.texts(0) == want && f.texts(1) == want,
              f.texts(0).empty() ? "" : f.texts(0)[1].toStdString());
        check("JMN-02", "Automatic is the ticked entry on both connectors at start",
              f.checked(0) == auto_txt && f.checked(1) == auto_txt);
    }

    // JMN-03 / JMN-04 — picking a controller applies live, persists, and is exclusive.
    {
        Fixture f;
        QAction* pick = f.find(0, "A&&B stick");
        if (pick) pick->trigger();
        QApplication::processEvents();
        QSettings s = saved_conf(confdir);
        s.beginGroup("input");
        check("JMN-03", "picking a controller in Joy 1 binds it live and saves joy1_device / joy1_device_name",
              pick && f.emu.joystick_device(0).id == f.H &&
              f.emu.joystick_source(0) == JoySource::Sdl && f.slot(f.c) == 0 &&
              s.value("joy1_device").toString().toStdString() == f.H &&
              s.value("joy1_device_name").toString() == "A&B stick" &&
              f.checked(0) == "A&&B stick",
              "slot=" + std::to_string(f.slot(f.c)));
        s.endGroup();

        QAction* pick2 = f.find(1, "A&&B stick");
        if (pick2) pick2->trigger();
        QApplication::processEvents();
        QSettings s2 = saved_conf(confdir);
        s2.beginGroup("input");
        check("JMN-04", "the same controller picked for Joy 2 takes it from Joy 1, which reverts to Automatic (emulator, menu, file)",
              pick2 && f.emu.joystick_device(1).id == f.H && f.emu.joystick_device(0).id.empty() &&
              f.checked(0) == auto_txt && f.checked(1) == "A&&B stick" &&
              s2.value("joy1_device").toString().isEmpty() &&
              s2.value("joy2_device").toString().toStdString() == f.H && f.slot(f.c) == 1,
              "slot=" + std::to_string(f.slot(f.c)));
    }

    // JMN-05 — hot-plug rebuilds the list.
    {
        Fixture f;
        const SDL_JoystickID d = attach("jnext pad D", 0x4321, 0x0009, false);
        feed(*f.host, SDL_EVENT_JOYSTICK_ADDED, d);
        const bool added = f.find(0, "jnext pad D") != nullptr && f.find(1, "jnext pad D") != nullptr;
        SDL_DetachVirtualJoystick(d);
        feed(*f.host, SDL_EVENT_JOYSTICK_REMOVED, d);
        check("JMN-05", "a controller plugged in appears in both submenus and disappears when unplugged",
              added && f.find(0, "jnext pad D") == nullptr && f.find(1, "jnext pad D") == nullptr);
    }

    // JMN-06 — an assigned controller that goes away stays assigned and is shown as such.
    {
        Fixture f;
        QAction* pick = f.find(0, "jnext pad A #2");
        if (pick) pick->trigger();
        QApplication::processEvents();
        SDL_DetachVirtualJoystick(f.a2);
        feed(*f.host, SDL_EVENT_JOYSTICK_REMOVED, f.a2);
        QSettings s = saved_conf(confdir);
        s.beginGroup("input");
        check("JMN-06", "an unplugged assigned controller is shown ticked as '(not connected)' and stays assigned in the emulator and the file",
              pick && f.checked(0) == "jnext pad A (not connected)" &&
              f.emu.joystick_device(0).id == f.G2 &&
              s.value("joy1_device").toString().toStdString() == f.G2);
        f.a2 = 0;   // already detached
    }

    // JMN-07 — None.
    {
        Fixture f;
        f.find(0, "A&&B stick")->trigger();
        QApplication::processEvents();
        QAction* none = f.find(0, "None");
        if (none) none->trigger();
        QApplication::processEvents();
        QSettings s = saved_conf(confdir);
        s.beginGroup("input");
        check("JMN-07", "None sets the source to none, clears the controller, unbinds the pad, and is saved",
              none && f.emu.joystick_source(0) == JoySource::None &&
              f.emu.joystick_device(0).id.empty() && f.slot(f.c) == -1 &&
              s.value("joy1_source").toString() == "none" &&
              s.value("joy1_device").toString().isEmpty() && f.checked(0) == "None");
    }

    // JMN-08 .. JMN-10 — the Preferences dialog offers the same choices and does not lose them.
    {
        Fixture f;
        AppConfigData initial;
        initial.joy_device[0] = QString(32, QLatin1Char('d'));
        initial.joy_device_name[0] = "Gone Pad";
        PreferencesDialog dlg(initial, nullptr, {}, {}, f.host->devices());
        auto combos = joy_combos(dlg);
        const bool shape = combos.size() == 2 && combos[0]->currentText() == "Gone Pad (not connected)";
        const AppConfigData got = apply_dialog(dlg);
        check("JMN-08", "an absent assigned controller is shown '(not connected)' and survives Apply unchanged",
              shape && got.joy_device[0] == initial.joy_device[0] &&
              got.joy_device_name[0] == "Gone Pad" && got.joy_source[0] == JoySource::Sdl,
              got.joy_device[0].toStdString());
    }
    {
        Fixture f;
        AppConfigData initial;
        PreferencesDialog dlg(initial, nullptr, {}, {}, f.host->devices());
        auto combos = joy_combos(dlg);
        bool ok = combos.size() == 2;
        if (ok) {
            combos[1]->setCurrentIndex(combos[1]->findText("jnext pad A #2"));
            combos[0]->setCurrentIndex(combos[0]->findText("jnext pad A #2"));
            ok = combos[1]->currentData().toString() == "auto";
        }
        const AppConfigData got = apply_dialog(dlg);
        check("JMN-09", "picking one controller for Joy 1 resets Joy 2 to Automatic; Apply carries Joy 1 = that controller",
              ok && got.joy_source[0] == JoySource::Sdl && got.joy_device[0].toStdString() == f.G2 &&
              got.joy_device[1].isEmpty() && got.joy_source[1] == JoySource::Sdl);
    }
    {
        Fixture f;
        AppConfigData initial;
        PreferencesDialog dlg(initial, nullptr, {}, {}, f.host->devices());
        auto combos = joy_combos(dlg);
        const int none = combos.size() == 2 ? combos[1]->findData(QStringLiteral("none")) : -1;
        if (none >= 0) combos[1]->setCurrentIndex(none);
        const AppConfigData got = apply_dialog(dlg);
        check("JMN-10", "Preferences offers None and Apply carries JoySource::None",
              none >= 0 && got.joy_source[1] == JoySource::None && got.joy_device[1].isEmpty());
    }

    // JMN-11 — applying Preferences assigns the controller on the machine and in the menu.
    {
        Fixture f;
        AppConfigData cfg;
        cfg.machine_type = MachineType::ZX48K;
        cfg.joy_device[1] = QString::fromStdString(f.G2);
        cfg.joy_device_name[1] = "jnext pad A";
        f.w->apply_preferences(cfg);
        QApplication::processEvents();
        check("JMN-11", "apply_preferences assigns the controller to the emulator, binds it live and ticks it in the menu",
              f.emu.joystick_device(1).id == f.G2 && f.slot(f.a2) == 1 &&
              f.checked(1) == "jnext pad A #2",
              "slot=" + std::to_string(f.slot(f.a2)));
    }

    // JMN-12 — the QtApp / SdlApp wiring, as a source check (neither can be constructed here).
    {
        const std::string qt  = strip_comments(slurp(JNEXT_QT_APP_CPP));
        const std::string sdl = strip_comments(slurp(JNEXT_SDL_APP_CPP));
        const size_t b = qt.find("void QtApp::wire_gamepad_and_sources(");
        const size_t e = qt.find("bool QtApp::init(");
        const std::string wire = (b != std::string::npos && e > b) ? qt.substr(b, e - b) : "";
        const size_t last_set = wire.rfind("set_joystick_device(");
        const size_t enumer   = wire.find("enumerate_existing_devices()");
        const bool qt_ok = !wire.empty() &&
            wire.find("on_joystick_device_changed") != std::string::npos &&
            wire.find("on_devices_changed") != std::string::npos &&
            last_set != std::string::npos && enumer != std::string::npos && last_set < enumer &&
            qt.find("set_joy_device_provider(") != std::string::npos;
        check("JMN-12", "QtApp and SdlApp wire the device callbacks, assign before enumerating, and call the test hook",
              qt_ok && count_of(sdl, "on_joystick_device_changed") == 2 &&
              count_of(sdl, "set_joystick_device(") == 4 &&
              qt.find("attach_test_devices_from_env()") != std::string::npos &&
              sdl.find("attach_test_devices_from_env()") != std::string::npos);
    }
}

}  // namespace

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QTemporaryDir cfg;
    if (!cfg.isValid()) {
        std::printf("  FAIL: could not create a temporary config directory\n");
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    qputenv("JNEXT_CONFIG_DIR", cfg.path().toUtf8());
    QApplication app(argc, argv);

    std::printf("GH #311 - choose the controller for each joystick port\n");
    std::printf("======================================================\n\n");
    if (!SDL_Init(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD)) {
        std::printf("  FAIL: SDL joystick subsystem unavailable: %s\n", SDL_GetError());
        std::printf("Total:    0  Passed:    0  Failed:    1  Skipped:    0\n");
        return 1;
    }
    GamepadHost::set_virtual_only(true);
    run(cfg.path());
    GamepadHost::set_virtual_only(false);
    SDL_Quit();
    std::printf("  Group: JMN            - done\n");
    std::printf("\n======================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n", g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
