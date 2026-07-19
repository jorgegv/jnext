// ===========================================================================
// MainWindow::apply_preferences() — "Apply" must not restart the machine.
// GitHub issue #40.
//
// WHAT IS UNDER TEST, and why it is here rather than in a pure policy suite.
//
// The headline acceptance criterion of issue #40 is a statement about what
// happens to a RUNNING MACHINE when the user presses Apply:
//
//   * changing only a joypad source must apply that source and NOT reboot;
//   * a machine-type change is the one thing that needs a restart, and it must
//     be explicit — the user is asked;
//   * declining that restart must leave the machine running AND must still
//     apply every other setting the user changed. Silently discarding the
//     joypad change on the way out is a variant of the very bug this issue
//     exists to fix;
//   * accepting it must route the restart through the frontend's reboot
//     callback (the one canonical cold-boot path), never a bare init().
//
// None of that can be proven by testing the decision helper in
// preferences_apply_policy.h. That helper answers "what should happen"; this
// suite answers "what DOES happen", which is the part that regressed. An
// earlier cut of this work asserted only the helper — via two predicates that
// ignored their arguments and were called from no production code — and a
// reviewer mutated apply_preferences() so DeferMachineType early-returned
// before the live-apply block, reintroducing the bug, and the ENTIRE triplet
// stayed green. Rows PA-03/PA-04 exist to kill exactly that mutation.
//
// A NOTE ON REACHABILITY. It was previously claimed that MainWindow is
// unreachable by unit tests, citing Task 91. That was wrong, and the
// misattribution mattered: Task 91 is about QtApp::on_frame_tick /
// on_status_tick, which need a live emulator, a real SDL audio device and
// wall-clock cadence. apply_preferences() needs none of those. MainWindow
// builds fine under the offscreen QPA platform — test/gui/esc_break_test.cpp
// has been constructing one and injecting real QKeyEvents all along, and
// present_count_test.cpp records a previous instance of this same excuse being
// made and being wrong. One failed approach is not a proof of impossibility.
//
// The only genuinely untestable part was the modal QMessageBox (it would block
// forever with no user). That is now an injectable seam
// (MainWindow::set_confirm_restart_callback), so the confirm ANSWER is
// supplied by the test while the surrounding control flow is the production
// one. The dialog's own wording/buttons remain unverified by automation.
//
// Qt is required (MainWindow is a real QWidget), but no display is: main()
// forces the offscreen QPA platform.
// Run: ./build/test/preferences_apply_test
// ===========================================================================

#include <QApplication>

#include <cstdio>
#include <string>
#include <vector>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "gui/app_config.h"
#include "gui/main_window.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
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

const char* type_name(MachineType t)
{
    switch (t) {
    case MachineType::ZXN_ISSUE2: return "Next";
    case MachineType::ZX48K:      return "48K";
    case MachineType::ZX128K:     return "128K";
    case MachineType::ZX_PLUS3:   return "+3";
    }
    return "?";
}

/// A running machine plus the window bound to it, with both frontend seams
/// (reboot + confirm) replaced by recorders. 48K is the cheapest machine to
/// init and needs no SD image.
struct Fixture {
    Emulator   emu;
    MainWindow win;

    std::vector<MachineType> reboots;      // every reboot the window requested
    std::vector<MachineType> confirms;     // every machine type it asked about
    bool                     answer = false;

    explicit Fixture(MachineType type = MachineType::ZX48K)
    {
        EmulatorConfig cfg;
        cfg.type = type;
        emu.init(cfg);
        win.set_emulator(&emu);
        win.set_reboot_callback([this](MachineType t) { reboots.push_back(t); });
        win.set_confirm_restart_callback([this](MachineType t) {
            confirms.push_back(t);
            return answer;
        });
    }
};

/// A preferences payload that differs from the fixture's defaults in every
/// live-applicable field, so "the live settings were applied" is observable.
AppConfigData live_prefs(MachineType type)
{
    AppConfigData c;
    c.machine_type    = type;
    c.cpu_speed       = CpuSpeed::MHZ_14;
    c.tape_fast_load  = false;
    c.joy_source[0]   = JoySource::CursorKeys;
    c.joy_source[1]   = JoySource::Sdl;
    return c;
}

/// Did the live-applicable settings of `live_prefs` actually reach the
/// machine? Every field is checked, so a partial apply cannot pass.
/// The PROGRAMMED CPU speed, NR 0x07 bits 1:0. Reading NR 0x07 also returns
/// the CURRENT ACTUAL speed in bits 5:4 (nextreg.txt:131-143), so the raw byte
/// is 0x22 after selecting 14 MHz, not 0x02 — masking is required, and this is
/// the spec's field, not a fudge to make a wrong expectation pass.
uint8_t programmed_cpu_speed(Fixture& f)
{
    return f.emu.nextreg().read(0x07) & 0x03;
}

bool live_applied(Fixture& f)
{
    return programmed_cpu_speed(f) == static_cast<uint8_t>(CpuSpeed::MHZ_14)
        && f.emu.tape().fast_load() == false
        && f.emu.tzx_tape().fast_load() == false
        && f.emu.joystick_source(0) == JoySource::CursorKeys
        && f.emu.joystick_source(1) == JoySource::Sdl;
}

std::string live_detail(Fixture& f)
{
    return "cpuspeed=" + std::to_string(programmed_cpu_speed(f))
         + " tap_fast=" + std::to_string(f.emu.tape().fast_load())
         + " tzx_fast=" + std::to_string(f.emu.tzx_tape().fast_load())
         + " joy0=" + std::to_string(static_cast<int>(f.emu.joystick_source(0)))
         + " joy1=" + std::to_string(static_cast<int>(f.emu.joystick_source(1)));
}

// ---------------------------------------------------------------------------

/// PA-01/02 — ApplyLiveOnly: the issue's own reproduction steps.
/// The user changes a joypad source and presses Apply, leaving the machine
/// type alone. The pad must start working and NOTHING else may happen: no
/// confirmation prompt, and above all no reboot.
void test_apply_live_only()
{
    Fixture f(MachineType::ZX48K);
    f.answer = true;   // would accept a restart — but must never be asked

    f.win.apply_preferences(live_prefs(MachineType::ZX48K));

    check("PA-01", "Apply with an unchanged machine type does NOT reboot",
          f.reboots.empty(),
          "reboots=" + std::to_string(f.reboots.size()));
    check("PA-02a", "...and does not even ask the user about a restart",
          f.confirms.empty(),
          "confirms=" + std::to_string(f.confirms.size()));
    check("PA-02b", "...and the live settings ARE applied",
          live_applied(f), live_detail(f));
}

/// PA-03/04 — DeferMachineType: the user changes the machine type, is asked,
/// and says No.
///
/// PA-04 is the row that kills the reviewer's mutation (early-return in the
/// DeferMachineType case, before the live-apply block): declining the restart
/// must not silently throw away the joypad change the user came here to make.
void test_defer_machine_type()
{
    Fixture f(MachineType::ZX48K);
    f.answer = false;                       // user declines the restart

    f.win.apply_preferences(live_prefs(MachineType::ZX128K));

    check("PA-03a", "a machine-type change ASKS before restarting",
          f.confirms.size() == 1,
          "confirms=" + std::to_string(f.confirms.size()));
    check("PA-03b", "...about the machine type actually requested",
          f.confirms.size() == 1 && f.confirms[0] == MachineType::ZX128K,
          f.confirms.empty() ? "none" : type_name(f.confirms[0]));
    check("PA-03c", "declining the restart does NOT reboot",
          f.reboots.empty(),
          "reboots=" + std::to_string(f.reboots.size()));
    check("PA-03d", "...and leaves the machine on its original type",
          f.emu.config().type == MachineType::ZX48K,
          type_name(f.emu.config().type));
    check("PA-04", "declining the restart STILL applies every live setting",
          live_applied(f), live_detail(f));
}

/// PA-05 — RebootThenApplyLive: the user changes the machine type and says Yes.
/// The restart must go through the frontend's reboot callback (the single
/// canonical cold-boot path), carrying the requested type — and the live
/// settings must still be applied afterwards.
void test_reboot_then_apply_live()
{
    Fixture f(MachineType::ZX48K);
    f.answer = true;                        // user accepts the restart

    f.win.apply_preferences(live_prefs(MachineType::ZX128K));

    check("PA-05a", "accepting the restart requests exactly one reboot",
          f.reboots.size() == 1,
          "reboots=" + std::to_string(f.reboots.size()));
    check("PA-05b", "...through the frontend callback, with the requested type",
          f.reboots.size() == 1 && f.reboots[0] == MachineType::ZX128K,
          f.reboots.empty() ? "none" : type_name(f.reboots[0]));
    check("PA-05c", "...and the live settings are applied too",
          live_applied(f), live_detail(f));
}

/// PA-06 — every outcome applies the live settings. Asserted as its own row
/// across all three outcomes, because this is the invariant a deleted
/// tautology used to "cover": an Apply always applies what it can.
void test_live_settings_in_every_outcome()
{
    struct Case { const char* what; MachineType want; bool answer; };
    const Case cases[] = {
        { "ApplyLiveOnly",       MachineType::ZX48K,  true  },
        { "DeferMachineType",    MachineType::ZX128K, false },
        { "RebootThenApplyLive", MachineType::ZX128K, true  },
    };
    for (const auto& c : cases) {
        Fixture f(MachineType::ZX48K);
        f.answer = c.answer;
        f.win.apply_preferences(live_prefs(c.want));
        check("PA-06", "live settings are applied in this outcome",
              live_applied(f),
              std::string(c.what) + ": " + live_detail(f));
    }
}

/// PA-07 — only the accepted-restart outcome may reboot. The complement of
/// PA-06, asserted per outcome so a control flow that rebooted on Defer (or
/// on no change at all) cannot hide.
void test_only_accepted_restart_reboots()
{
    struct Case { const char* what; MachineType want; bool answer; size_t reboots; };
    const Case cases[] = {
        { "ApplyLiveOnly",       MachineType::ZX48K,  true,  0 },
        { "DeferMachineType",    MachineType::ZX128K, false, 0 },
        { "RebootThenApplyLive", MachineType::ZX128K, true,  1 },
    };
    for (const auto& c : cases) {
        Fixture f(MachineType::ZX48K);
        f.answer = c.answer;
        f.win.apply_preferences(live_prefs(c.want));
        check("PA-07", "this outcome reboots exactly as often as it should",
              f.reboots.size() == c.reboots,
              std::string(c.what) + ": got " + std::to_string(f.reboots.size())
                  + ", want " + std::to_string(c.reboots));
    }
}

/// PA-08/09 — the Machine MENU, driven through its real QAction.
///
/// This is the documented UX change beyond the issue's literal ask: selecting
/// the machine you are already running is now a no-op (it used to reboot).
/// Machine > Reset / F1 remains the way to restart. Pinned in both directions
/// so neither half can regress unnoticed.
void test_machine_menu()
{
    auto trigger = [](MainWindow& w, MachineType t) {
        for (QAction* a : w.findChildren<QAction*>()) {
            if (a->data().isValid() && a->data().toInt() == static_cast<int>(t)
                && a->isCheckable()) {
                a->trigger();
                return true;
            }
        }
        return false;
    };

    // Same machine as the running one: no reboot, and no prompt either (the
    // menu never prompts — picking a machine from the Machine menu IS the
    // explicit request; only Preferences asks).
    {
        Fixture f(MachineType::ZX48K);
        f.answer = true;
        const bool found = trigger(f.win, MachineType::ZX48K);
        check("PA-08a", "the 48K Machine-menu action exists and is triggerable", found);
        check("PA-08b", "selecting the ALREADY-RUNNING machine type is a no-op",
              f.reboots.empty(),
              "reboots=" + std::to_string(f.reboots.size()));
    }
    // A different machine: reboots, through the frontend callback, unprompted.
    {
        Fixture f(MachineType::ZX48K);
        const bool found = trigger(f.win, MachineType::ZX128K);
        check("PA-09a", "the 128K Machine-menu action exists and is triggerable", found);
        check("PA-09b", "selecting a DIFFERENT machine type reboots via the callback",
              f.reboots.size() == 1 && f.reboots[0] == MachineType::ZX128K,
              f.reboots.empty() ? "none" : type_name(f.reboots[0]));
        check("PA-09c", "...and the menu does not prompt (only Preferences does)",
              f.confirms.empty(),
              "confirms=" + std::to_string(f.confirms.size()));
    }
}

/// PA-10 — the confirmation is asked exactly once per Apply, not per setting.
void test_confirm_asked_once()
{
    Fixture f(MachineType::ZX48K);
    f.answer = false;
    f.win.apply_preferences(live_prefs(MachineType::ZX_PLUS3));
    check("PA-10", "the restart confirmation is asked exactly once",
          f.confirms.size() == 1,
          "confirms=" + std::to_string(f.confirms.size()));
}

}  // namespace

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    std::printf("preferences_apply_test (Apply must not restart the machine, issue #40)\n");

    test_apply_live_only();
    test_defer_machine_type();
    test_reboot_then_apply_live();
    test_live_settings_in_every_outcome();
    test_only_accepted_restart_reboots();
    test_machine_menu();
    test_confirm_asked_once();

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
