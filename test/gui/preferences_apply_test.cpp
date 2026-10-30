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

#include "debug/debug_keymap.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QDialog>
#include <QTimer>
#include <QMenu>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStatusBar>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <cstring>
#include <filesystem>
#include <cmath>
#include <cstdlib>
#include <sstream>
#include <string>
#include <vector>

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/log.h"
#include "gui/app_config.h"
#include "gui/main_window.h"
#include <memory>
#include "debug/debugger.h"
#include "gui/preferences_dialog.h"
#include <spdlog/sinks/ostream_sink.h>
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

/// Bytes this process has passed through read() syscalls (Linux
/// /proc/self/io `rchar`); 0 when unreadable. PA-25d's instrument: a Quick
/// Load that reads the whole 1 GB card moves it by >= 1 GiB.
uint64_t rchar_bytes()
{
    std::ifstream f("/proc/self/io");
    std::string key;
    uint64_t v = 0;
    while (f >> key >> v)
        if (key == "rchar:") return v;
    return 0;
}

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

const char* type_name(MachineType t)
{
    switch (t) {
    case MachineType::ZXN_ISSUE2: return "Next";
    case MachineType::ZX48K:      return "48K";
    case MachineType::ZX128K:     return "128K";
    case MachineType::ZX_PLUS3:   return "+3";
    case MachineType::PENTAGON:   return "Pentagon";
    }
    return "?";
}

/// A running machine plus the window bound to it, with both frontend seams
/// (reboot + confirm) replaced by recorders. 48K is the cheapest machine to
/// init and needs no SD image.
struct Fixture {
    Emulator   emu;
    // GH #278 WP2 — the loop owner's debugger backend, which set_emulator()
    // requires in a debugger build; declared before the window, so it outlives it.
    std::unique_ptr<jnext::dbg::Debugger> backend;
    MainWindow win;

    std::vector<MachineType> reboots;      // every reboot the window requested
    std::vector<MachineType> confirms;     // every machine type it asked about
    bool                     answer = false;
    /// The frontend seams in the order they were driven. Only PA-14 cares, but
    /// it has to be recorded here: the ordering it pins is between two DIFFERENT
    /// callbacks, so neither one alone can observe it.
    std::vector<std::string> order;

    /// `keep_cli` >= 0 stands for `--quick-snapshot-keep` given on the command
    /// line with that value (0 = unlimited); -1 = flag absent (GH #321).
    explicit Fixture(MachineType type = MachineType::ZX48K, int keep_cli = -1,
                     const std::string& sd_image = {})
    {
        EmulatorConfig cfg;
        cfg.type = type;
        if (!sd_image.empty()) {   // GH #386: PA-25c counts SD digests
            cfg.sd_card_image    = sd_image;
            cfg.sd_card_readonly = true;
        }
        if (keep_cli >= 0) {
            cfg.quick_snapshot_keep     = keep_cli;
            cfg.quick_snapshot_keep_cli = true;
        }
        emu.init(cfg);
        backend = std::make_unique<jnext::dbg::Debugger>(emu);
        win.set_debugger(backend.get());
        win.set_emulator(&emu);
        win.set_reboot_callback([this](MachineType t) {
            reboots.push_back(t);
            order.push_back("reboot");
        });
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
    c.audio_gain_db   = 6.0f;
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
        && f.emu.mixer().output_gain_db() == 6.0f
        && f.emu.joystick_source(0) == JoySource::CursorKeys
        && f.emu.joystick_source(1) == JoySource::Sdl;
}

std::string live_detail(Fixture& f)
{
    return "cpuspeed=" + std::to_string(programmed_cpu_speed(f))
         + " tap_fast=" + std::to_string(f.emu.tape().fast_load())
         + " tzx_fast=" + std::to_string(f.emu.tzx_tape().fast_load())
         + " gain_db=" + std::to_string(f.emu.mixer().output_gain_db())
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
    struct Case { const char* what; MachineType want; bool answer; const char* id; };
    const Case cases[] = {
        { "ApplyLiveOnly",       MachineType::ZX48K,  true,  "PA-06"  },
        { "DeferMachineType",    MachineType::ZX128K, false, "PA-06b" },
        { "RebootThenApplyLive", MachineType::ZX128K, true,  "PA-06c" },
    };
    for (const auto& c : cases) {
        Fixture f(MachineType::ZX48K);
        f.answer = c.answer;
        f.win.apply_preferences(live_prefs(c.want));
        check(c.id, "live settings are applied in this outcome",
              live_applied(f),
              std::string(c.what) + ": " + live_detail(f));
    }
}

/// PA-07 — only the accepted-restart outcome may reboot. The complement of
/// PA-06, asserted per outcome so a control flow that rebooted on Defer (or
/// on no change at all) cannot hide.
void test_only_accepted_restart_reboots()
{
    struct Case { const char* what; MachineType want; bool answer; size_t reboots; const char* id; };
    const Case cases[] = {
        { "ApplyLiveOnly",       MachineType::ZX48K,  true,  0, "PA-07"  },
        { "DeferMachineType",    MachineType::ZX128K, false, 0, "PA-07b" },
        { "RebootThenApplyLive", MachineType::ZX128K, true,  1, "PA-07c" },
    };
    for (const auto& c : cases) {
        Fixture f(MachineType::ZX48K);
        f.answer = c.answer;
        f.win.apply_preferences(live_prefs(c.want));
        check(c.id, "this outcome reboots exactly as often as it should",
              f.reboots.size() == c.reboots,
              std::string(c.what) + ": got " + std::to_string(f.reboots.size())
                  + ", want " + std::to_string(c.reboots));
    }
}

/// PA-08/09 — the Machine MENU, driven through its real QAction.
///
/// This is the documented UX change beyond the issue's literal ask: selecting
/// the machine you are already running is now a no-op (it used to reboot).
/// Machine > Power Reset / F1 remains the way to restart. Pinned in both
/// directions so neither half can regress unnoticed.
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

/// PA-11 — the regular Preferences infrastructure exposes and returns host
/// gain, rather than leaving it as a CLI-only setting. The control is a
/// slider whose range is symmetric so the 0 dB default sits centred
/// (PR #41 review).
void test_audio_gain_preference_control()
{
    AppConfigData initial;
    initial.audio_gain_db = -7.0f;
    PreferencesDialog dlg(initial);

    auto* slider = dlg.findChild<QSlider*>(QStringLiteral("audioGainDbSlider"));
    check("PA-11a", "Preferences exposes the host output gain slider",
          slider && slider->minimum() == -24 && slider->maximum() == 24);
    check("PA-11b", "the slider range is symmetric so 0 dB is centred",
          slider && slider->minimum() == -slider->maximum());
    check("PA-11c", "the gain control starts from the persisted value",
          slider && slider->value() == -7);

    bool emitted = false;
    AppConfigData collected;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&emitted, &collected](const AppConfigData& cfg) {
                         emitted = true;
                         collected = cfg;
                     });
    auto* buttons = dlg.findChild<QDialogButtonBox*>();
    if (slider) slider->setValue(6);
    if (buttons && buttons->button(QDialogButtonBox::Apply))
        buttons->button(QDialogButtonBox::Apply)->click();
    check("PA-11d", "Apply returns the edited host gain",
          emitted && std::abs(collected.audio_gain_db - 6.0f) < 0.001f);
}

/// PA-12 — GH #25: the ESP-01 settings have a page, and it round-trips them.
///
/// WHY THE ROUND-TRIP ROWS MATTER MORE THAN THE "IS THERE A WIDGET" ONES.
/// PreferencesDialog::collect() builds a FRESH AppConfigData and fills in only
/// the fields it has controls for, and MainWindow assigns that whole object
/// over the saved config and writes it to disk. So a persisted field with no
/// control is not merely uneditable — it is DESTROYED, silently, the moment
/// the user opens Preferences and presses OK. Both ESP fields were in exactly
/// that state: `--esp`/`--esp-allow` could be made permanent by hand-editing
/// ~/.jnext/jnext.conf, and any later visit to Preferences threw the edit away
/// with nothing said. PA-12e is the row that fails if that regresses; it does
/// not touch a single control, which is the whole point.
void test_esp_preference_controls()
{
    AppConfigData initial;
    initial.esp_enabled       = true;
    initial.esp_allowed_hosts = {"nx.nxtel.org", "192.168.1.10"};

    PreferencesDialog dlg(initial);
    auto* check_box = dlg.findChild<QCheckBox*>(QStringLiteral("espEnabledCheck"));
    auto* hosts     = dlg.findChild<QPlainTextEdit*>(QStringLiteral("espAllowedHostsEdit"));

    check("PA-12a", "Preferences exposes the ESP enable and allowlist controls",
          check_box != nullptr && hosts != nullptr);
    if (!check_box || !hosts) return;

    check("PA-12b", "both ESP controls start from the persisted values",
          check_box->isChecked() &&
              hosts->toPlainText() == QStringLiteral("nx.nxtel.org\n192.168.1.10"),
          hosts->toPlainText().toStdString());

    // The allowlist only means anything while the module is on.
    check_box->setChecked(false);
    check("PA-12c1", "the allowlist is disabled while the ESP is off",
          !hosts->isEnabled());
    check_box->setChecked(true);
    check("PA-12c2", "...and enabled again when the ESP is turned on",
          hosts->isEnabled());

    AppConfigData collected;
    bool emitted = false;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { emitted = true; collected = cfg; });
    auto* buttons = dlg.findChild<QDialogButtonBox*>();
    auto apply = [&]() {
        emitted = false;
        collected = AppConfigData{};
        if (buttons && buttons->button(QDialogButtonBox::Apply))
            buttons->button(QDialogButtonBox::Apply)->click();
    };

    // Edited, with the two shapes a text box adds that a repeated CLI flag
    // cannot: blank lines and a duplicate. Both must be dropped, exactly as
    // EspHostPolicy::add drops them for --esp-allow and for the config loader.
    hosts->setPlainText(QStringLiteral("  NX.NxTel.ORG  \n\nnx.nxtel.org\nexample.test\n"));
    apply();
    check("PA-12d", "Apply returns the edited allowlist, blanks and duplicates dropped",
          emitted && collected.esp_enabled &&
              collected.esp_allowed_hosts ==
                  std::vector<std::string>{"NX.NxTel.ORG", "example.test"},
          std::to_string(collected.esp_allowed_hosts.size()) + " host(s)");

    // A COMMA SEPARATES TOO. ~/.jnext/jnext.conf writes allowed_hosts as a
    // comma-separated list, so a line pasted from there (or from the man
    // page's FILES section) reaches this box. Stored literally it would be one
    // host name no guest can ever match, refusing every connection while
    // showing the user what looks like a correct list.
    hosts->setPlainText(QStringLiteral("nx.nxtel.org, example.test"));
    apply();
    check("PA-12f", "a comma-separated line is split, not stored as one unmatchable name",
          emitted && collected.esp_allowed_hosts ==
                         std::vector<std::string>{"nx.nxtel.org", "example.test"},
          std::to_string(collected.esp_allowed_hosts.size()) + " host(s)");

    // THE REGRESSION ROW: a dialog constructed on a config that has ESP
    // settings, applied without the user touching the Network tab at all,
    // must give those settings back untouched.
    PreferencesDialog untouched(initial);
    AppConfigData passthrough;
    bool passthrough_emitted = false;
    QObject::connect(&untouched, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) {
                         passthrough_emitted = true;
                         passthrough = cfg;
                     });
    auto* untouched_buttons = untouched.findChild<QDialogButtonBox*>();
    if (untouched_buttons && untouched_buttons->button(QDialogButtonBox::Apply))
        untouched_buttons->button(QDialogButtonBox::Apply)->click();
    check("PA-12e", "an untouched dialog does NOT wipe the persisted ESP settings",
          passthrough_emitted && passthrough.esp_enabled &&
              passthrough.esp_allowed_hosts == initial.esp_allowed_hosts,
          "enabled=" + std::to_string(passthrough.esp_enabled) + " hosts=" +
              std::to_string(passthrough.esp_allowed_hosts.size()));
}

/// PENT-28 — GH #324: the machine combo lists the Pentagon, and an untouched
/// dialog on a Pentagon config hands the Pentagon back. Without the item,
/// findData() is -1, currentData() is an invalid variant (0 = the Next) and
/// pressing OK would silently switch a Pentagon user to a Next.
void test_pentagon_preference()
{
    AppConfigData initial;
    initial.machine_type = MachineType::PENTAGON;
    PreferencesDialog dlg(initial);
    bool listed = false;
    for (auto* combo : dlg.findChildren<QComboBox*>())
        for (int i = 0; i < combo->count(); ++i)
            if (combo->itemText(i) == QStringLiteral("Pentagon") &&
                combo->itemData(i).toInt() == static_cast<int>(MachineType::PENTAGON))
                listed = true;
    AppConfigData got;
    bool emitted = false;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { emitted = true; got = cfg; });
    auto* buttons = dlg.findChild<QDialogButtonBox*>();
    if (buttons && buttons->button(QDialogButtonBox::Apply))
        buttons->button(QDialogButtonBox::Apply)->click();
    check("PENT-28", "the Preferences machine combo lists Pentagon, and an untouched dialog "
                     "on a Pentagon config gives the Pentagon back (not the Next)",
          listed && emitted && got.machine_type == MachineType::PENTAGON,
          "listed=" + std::to_string(listed) + " emitted=" + std::to_string(emitted) +
              " type=" + std::to_string(static_cast<int>(got.machine_type)));
}

/// PA-20 — the NextPi tab: one control per [nextpi] field, starting from the
/// saved values, the rest disabled while the enable tick is off, and — the
/// regression row, as PA-12e is for the ESP — an untouched dialog hands every
/// field back unchanged rather than resetting it.
void test_nextpi_preference_controls()
{
    AppConfigData initial;
    initial.nextpi_enabled     = true;
    initial.nextpi_dir         = QStringLiteral("/data/nextpi");
    initial.nextpi_release     = QStringLiteral("latest");
    initial.nextpi_qemu_binary = QStringLiteral("/opt/qemu/bin/qemu-system-arm");
    initial.nextpi_audio       = QStringLiteral("none");

    PreferencesDialog dlg(initial);
    auto* enable  = dlg.findChild<QCheckBox*>(QStringLiteral("nextpiEnabledCheck"));
    auto* dir     = dlg.findChild<QLineEdit*>(QStringLiteral("nextpiDirEdit"));
    auto* release = dlg.findChild<QComboBox*>(QStringLiteral("nextpiReleaseCombo"));
    auto* qemu    = dlg.findChild<QLineEdit*>(QStringLiteral("nextpiQemuEdit"));
    auto* audio   = dlg.findChild<QComboBox*>(QStringLiteral("nextpiAudioCombo"));
    check("PA-20a", "Preferences exposes a NextPi tab with enable, directory, release, QEMU and audio controls",
          enable && dir && release && qemu && audio);
    if (!enable || !dir || !release || !qemu || !audio) return;

    check("PA-20b", "the NextPi controls start from the persisted values",
          enable->isChecked() && dir->text() == initial.nextpi_dir &&
              release->currentText() == initial.nextpi_release &&
              qemu->text() == initial.nextpi_qemu_binary && audio->currentText() == initial.nextpi_audio);

    enable->setChecked(false);
    const bool off = !release->isEnabled() && !audio->isEnabled() && !dir->parentWidget()->isEnabled();
    enable->setChecked(true);
    const bool on = release->isEnabled() && audio->isEnabled() && dir->parentWidget()->isEnabled();
    check("PA-20c", "the other NextPi controls are disabled while the tick is off, enabled when on",
          off && on);

    AppConfigData collected;
    bool emitted = false;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { emitted = true; collected = cfg; });
    release->setEditText(QStringLiteral(" 1_93D "));
    qemu->setText(QString());
    if (auto* b = dlg.findChild<QDialogButtonBox*>(); b && b->button(QDialogButtonBox::Apply))
        b->button(QDialogButtonBox::Apply)->click();
    check("PA-20d", "Apply returns the edited NextPi fields, trimmed; an emptied field means its default",
          emitted && collected.nextpi_enabled && collected.nextpi_release == QStringLiteral("1_93D") &&
              collected.nextpi_qemu_binary.isEmpty() && collected.nextpi_dir == initial.nextpi_dir,
          collected.nextpi_release.toStdString());

    PreferencesDialog untouched(initial);
    AppConfigData passthrough;
    bool passthrough_emitted = false;
    QObject::connect(&untouched, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { passthrough_emitted = true; passthrough = cfg; });
    if (auto* b = untouched.findChild<QDialogButtonBox*>(); b && b->button(QDialogButtonBox::Apply))
        b->button(QDialogButtonBox::Apply)->click();
    check("PA-20e", "an untouched dialog does NOT reset the persisted NextPi settings",
          passthrough_emitted && passthrough.nextpi_enabled == initial.nextpi_enabled &&
              passthrough.nextpi_dir == initial.nextpi_dir &&
              passthrough.nextpi_release == initial.nextpi_release &&
              passthrough.nextpi_qemu_binary == initial.nextpi_qemu_binary &&
              passthrough.nextpi_audio == initial.nextpi_audio);
}

/// PA-28a/b, GH #407: the NextPi tab's "Extra QEMU arguments" box.
void test_nextpi_qemu_args_control()
{
    AppConfigData initial;
    initial.nextpi_enabled   = true;
    initial.nextpi_qemu_args = QStringLiteral("-serial telnet::2357,server,nowait");

    PreferencesDialog dlg(initial);
    auto* enable = dlg.findChild<QCheckBox*>(QStringLiteral("nextpiEnabledCheck"));
    auto* args   = dlg.findChild<QLineEdit*>(QStringLiteral("nextpiQemuArgsEdit"));
    bool follows = false;
    if (enable && args) {
        enable->setChecked(false);
        const bool off = !args->isEnabled();
        enable->setChecked(true);
        follows = off && args->isEnabled();
    }
    check("PA-28a", "the NextPi tab has an Extra QEMU arguments box that starts from the saved text and "
                    "follows the enable tick",
          args && follows && args->text() == initial.nextpi_qemu_args);
    if (!args) return;

    AppConfigData collected;
    bool emitted = false;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { emitted = true; collected = cfg; });
    args->setText(QStringLiteral("  -name 'my pi' -serial telnet::2357,server,nowait  "));
    if (auto* b = dlg.findChild<QDialogButtonBox*>(); b && b->button(QDialogButtonBox::Apply))
        b->button(QDialogButtonBox::Apply)->click();

    PreferencesDialog untouched(initial);
    AppConfigData passthrough;
    bool passthrough_emitted = false;
    QObject::connect(&untouched, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { passthrough_emitted = true; passthrough = cfg; });
    if (auto* b = untouched.findChild<QDialogButtonBox*>(); b && b->button(QDialogButtonBox::Apply))
        b->button(QDialogButtonBox::Apply)->click();
    check("PA-28b", "Apply returns the extra QEMU arguments trimmed, with quotes and commas kept and NOT split; "
                    "an untouched dialog hands the saved text back unchanged",
          emitted &&
              collected.nextpi_qemu_args == QStringLiteral("-name 'my pi' -serial telnet::2357,server,nowait") &&
              passthrough_emitted && passthrough.nextpi_qemu_args == initial.nextpi_qemu_args,
          collected.nextpi_qemu_args.toStdString());
}

/// PA-13 — GH #25: the ESP cannot be toggled on a running machine, so Apply
/// must hand the new values to the frontend (which owns the EmulatorConfig
/// every later cold boot is built from) rather than drop them on the floor.
/// Without this the setting would be honoured only on the NEXT LAUNCH, and a
/// user who enabled the ESP and pressed F1 would get a machine with no module
/// and no explanation.
void test_esp_settings_reach_the_frontend()
{
    Fixture f(MachineType::ZX48K);

    bool                     seen_enabled = false;
    std::vector<std::string> seen_hosts;
    int                      calls = 0;
    f.win.set_esp_config_callback(
        [&](bool enabled, const std::vector<std::string>& hosts) {
            ++calls;
            seen_enabled = enabled;
            seen_hosts   = hosts;
        });

    AppConfigData cfg = live_prefs(MachineType::ZX48K);
    cfg.esp_enabled       = true;
    cfg.esp_allowed_hosts = {"nx.nxtel.org"};
    f.win.apply_preferences(cfg);

    check("PA-13a", "Apply forwards the ESP settings to the frontend exactly once",
          calls == 1, "calls=" + std::to_string(calls));
    check("PA-13b", "...with both the enable flag and the allowlist",
          seen_enabled && seen_hosts == std::vector<std::string>{"nx.nxtel.org"});
    check("PA-13c", "...and does NOT reboot the running machine to do it",
          f.reboots.empty(), "reboots=" + std::to_string(f.reboots.size()));
    check("PA-13d", "...while every other live setting still applies",
          live_applied(f), live_detail(f));
}

/// PA-14 — GH #25, found in review: the ESP forward must reach the frontend
/// BEFORE the machine-type reboot, not after it.
///
/// Both seams write to the same object — the frontend's EmulatorConfig. The
/// ESP callback sets `esp_enabled`/`esp_allowed_hosts` in it; the reboot
/// callback sets `type` and then COLD-BOOTS from it. Run the reboot first and
/// the machine is reconstructed from the old ESP values, so a user who changed
/// the machine type and enabled the ESP in one visit gets a restart that does
/// not have the ESP in it — while the Network tab's own note and the man page
/// both promise the change takes effect on the next hard reset. The restart
/// they just accepted IS that reset.
///
/// Asserting on the ORDER rather than on a post-boot Emulator is deliberate:
/// the fixture's reboot seam is a recorder, so there is no second machine to
/// inspect, and the order is the whole of the contract anyway.
void test_esp_forward_precedes_the_reboot()
{
    Fixture f(MachineType::ZX48K);
    f.answer = true;                       // accept the restart
    f.win.set_esp_config_callback(
        [&f](bool, const std::vector<std::string>&) { f.order.push_back("esp"); });

    AppConfigData cfg = live_prefs(MachineType::ZX128K);   // a REAL type change
    cfg.esp_enabled       = true;
    cfg.esp_allowed_hosts = {"nx.nxtel.org"};
    f.win.apply_preferences(cfg);

    const auto joined = [&f]() {
        std::string s;
        for (const std::string& step : f.order) s += (s.empty() ? "" : ",") + step;
        return s;
    };
    check("PA-14a", "both frontend seams are driven when type and ESP change together",
          f.order.size() == 2, joined());
    check("PA-14b", "the ESP config reaches the frontend BEFORE the reboot uses it",
          f.order.size() == 2 && f.order[0] == "esp" && f.order[1] == "reboot",
          joined());
}

/// PA-15 — issue #35: the too-slow degradation policy has a control, that
/// control round-trips, and Apply hands the value to the frontend that owns
/// the frame sequencer.
///
/// The pass-through row (PA-15d) is the one that matters most, for the reason
/// spelled out above PA-12: collect() builds a FRESH AppConfigData, so a
/// persisted field without a control is not merely uneditable, it is wiped the
/// moment the user presses OK. A hand-edited `when_slow_prefer = video` must
/// survive a visit to Preferences that never touches the Startup tab.
void test_when_slow_prefer_control()
{
    AppConfigData initial;
    initial.when_slow_prefer = audio_pacing::WhenSlowPrefer::Video;

    PreferencesDialog dlg(initial);
    auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("whenSlowPreferCombo"));
    check("PA-15a", "Preferences exposes the too-slow policy control", combo != nullptr);
    if (!combo) return;

    check("PA-15b", "the control starts from the persisted value",
          combo->currentData().toInt() ==
              static_cast<int>(audio_pacing::WhenSlowPrefer::Video));

    AppConfigData collected;
    bool emitted = false;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { emitted = true; collected = cfg; });
    auto* buttons = dlg.findChild<QDialogButtonBox*>();
    combo->setCurrentIndex(
        combo->findData(static_cast<int>(audio_pacing::WhenSlowPrefer::Audio)));
    if (buttons && buttons->button(QDialogButtonBox::Apply))
        buttons->button(QDialogButtonBox::Apply)->click();
    check("PA-15c", "Apply returns the edited policy",
          emitted &&
              collected.when_slow_prefer == audio_pacing::WhenSlowPrefer::Audio);

    PreferencesDialog untouched(initial);
    AppConfigData passthrough;
    bool passthrough_emitted = false;
    QObject::connect(&untouched, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) {
                         passthrough_emitted = true;
                         passthrough = cfg;
                     });
    auto* untouched_buttons = untouched.findChild<QDialogButtonBox*>();
    if (untouched_buttons && untouched_buttons->button(QDialogButtonBox::Apply))
        untouched_buttons->button(QDialogButtonBox::Apply)->click();
    check("PA-15d", "an untouched dialog does NOT wipe the persisted policy",
          passthrough_emitted &&
              passthrough.when_slow_prefer == audio_pacing::WhenSlowPrefer::Video);
}

/// PA-17 — GH #19: the quick-screenshot directory and format have controls,
/// both round-trip, and an untouched dialog does not wipe either.
///
/// Same reasoning as PA-12e and PA-15d, and the same failure mode: collect()
/// builds a FRESH AppConfigData, so a `[screenshot]` section hand-edited into
/// ~/.jnext/jnext.conf would be thrown away by any later visit to Preferences
/// if these two fields had no control here. PA-17e is the row that fails if
/// that regresses, and it touches no control at all.
void test_quick_screenshot_controls()
{
    AppConfigData initial;
    initial.quick_screenshot_dir    = QStringLiteral("/tmp/jnext-shots");
    initial.quick_screenshot_format = ScreenshotFormat::Scr;

    PreferencesDialog dlg(initial);
    auto* dir_edit = dlg.findChild<QLineEdit*>(QStringLiteral("quickScreenshotDirEdit"));
    auto* fmt_combo = dlg.findChild<QComboBox*>(QStringLiteral("quickScreenshotFormatCombo"));
    check("PA-17a", "Preferences exposes the quick-screenshot directory and format controls",
          dir_edit != nullptr && fmt_combo != nullptr);
    if (!dir_edit || !fmt_combo) return;

    check("PA-17b", "both controls start from the persisted values",
          dir_edit->text() == QStringLiteral("/tmp/jnext-shots")
              && fmt_combo->currentData().toInt() == static_cast<int>(ScreenshotFormat::Scr),
          (dir_edit->text() + "/" + QString::number(fmt_combo->currentData().toInt()))
              .toStdString());

    AppConfigData collected;
    bool emitted = false;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { emitted = true; collected = cfg; });
    dir_edit->setText(QStringLiteral("/tmp/other-shots"));
    fmt_combo->setCurrentIndex(fmt_combo->findData(static_cast<int>(ScreenshotFormat::Png)));
    auto* buttons = dlg.findChild<QDialogButtonBox*>();
    if (buttons && buttons->button(QDialogButtonBox::Apply))
        buttons->button(QDialogButtonBox::Apply)->click();
    check("PA-17c", "Apply returns the edited directory",
          emitted && collected.quick_screenshot_dir == QStringLiteral("/tmp/other-shots"),
          collected.quick_screenshot_dir.toStdString());
    check("PA-17d", "Apply returns the edited format",
          emitted && collected.quick_screenshot_format == ScreenshotFormat::Png,
          std::to_string(static_cast<int>(collected.quick_screenshot_format)));

    PreferencesDialog untouched(initial);
    AppConfigData passthrough;
    bool passthrough_emitted = false;
    QObject::connect(&untouched, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) {
                         passthrough_emitted = true;
                         passthrough = cfg;
                     });
    auto* untouched_buttons = untouched.findChild<QDialogButtonBox*>();
    if (untouched_buttons && untouched_buttons->button(QDialogButtonBox::Apply))
        untouched_buttons->button(QDialogButtonBox::Apply)->click();
    check("PA-17e", "an untouched dialog does NOT wipe the persisted quick-screenshot settings",
          passthrough_emitted
              && passthrough.quick_screenshot_dir == QStringLiteral("/tmp/jnext-shots")
              && passthrough.quick_screenshot_format == ScreenshotFormat::Scr,
          (passthrough.quick_screenshot_dir + "/"
           + QString::number(static_cast<int>(passthrough.quick_screenshot_format)))
              .toStdString());
}

/// PA-18 — GH #19: File > Quick Screenshot really writes a file, with the
/// configured directory and the configured format.
///
/// The pieces are covered elsewhere (auto_screenshot_path and both writers in
/// `screenshot_test`, the settings in `app_config_test`, the chord in
/// `host_hotkey_test`). What NOTHING else covers is the WIRING in
/// MainWindow::on_quick_screenshot — resolve the directory, build the name,
/// write it — and a capture that silently goes nowhere is exactly the failure
/// the issue is written against. So this drives the real QAction on a real
/// MainWindow with a real machine attached, and looks on disk.
///
/// JNEXT_CONFIG_DIR is set FIRST, before the Fixture exists: MainWindow builds
/// its AppConfig in its constructor, so the redirection has to be in place by
/// then — and it must be, or this suite would write captures into the
/// developer's own ~/.jnext/screenshots.
void test_quick_screenshot_writes_a_file()
{
    // (a) No configuration at all: the default directory is
    // <JNEXT_CONFIG_DIR>/screenshots, created on demand, and the format is PNG.
    {
        QTemporaryDir home;
        qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());

        Fixture f;
        QAction* quick = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == QStringLiteral("Quic&k Screenshot")) quick = a;
        if (quick) {
            quick->trigger();
            QApplication::processEvents();
        } else {
            check("PA-18a", "File > Quick Screenshot writes a PNG into the default directory",
                  false, "action not found");
        }

        const QDir shots(home.path() + QStringLiteral("/screenshots"));
        const QStringList made = shots.entryList(QDir::Files);
        // jnext-YYYYMMDD-HHMMSS.png — the name shape, not just "a file".
        static const QRegularExpression shape(
            QStringLiteral("^jnext-\\d{8}-\\d{6}(-\\d{2})?\\.png$"));
        const bool named = made.size() == 1 && shape.match(made.first()).hasMatch();
        const qint64 size = named
            ? QFileInfo(shots.filePath(made.first())).size() : 0;
        if (quick) {
            check("PA-18a",
                  "File > Quick Screenshot writes one timestamped PNG into the default directory",
                  named && size > 0,
                  (QStringLiteral("files=") + made.join(QLatin1Char(','))
                   + QStringLiteral(" size=") + QString::number(size)).toStdString());
        }
    }

    // (b) A configuration file that asks for .SCR in a directory of its own:
    // both must be honoured, and the file must be a 6912-byte screen dump —
    // not a PNG with a .scr name.
    {
        QTemporaryDir home;
        const QString shots = home.path() + QStringLiteral("/my shots");
        {
            QSettings raw(home.path() + QStringLiteral("/jnext.conf"), QSettings::IniFormat);
            raw.beginGroup("screenshot");
            raw.setValue("quick_dir", shots);
            raw.setValue("quick_format", "scr");
            raw.endGroup();
            raw.sync();
        }
        qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());

        Fixture f;
        QAction* quick = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == QStringLiteral("Quic&k Screenshot")) quick = a;
        if (quick) { quick->trigger(); QApplication::processEvents(); }

        const QStringList made = QDir(shots).entryList(QDir::Files);
        const bool one_scr = made.size() == 1 && made.first().endsWith(QStringLiteral(".scr"));
        const qint64 size = one_scr ? QFileInfo(QDir(shots).filePath(made.first())).size() : 0;
        check("PA-18b",
              "the configured directory and .SCR format are both honoured, 6912 bytes",
              one_scr && size == 6912,
              (QStringLiteral("files=") + made.join(QLatin1Char(','))
               + QStringLiteral(" size=") + QString::number(size)).toStdString());
    }

    // UNCONDITIONAL, and that is the point: this function is the last call in
    // main() today, so leaking JNEXT_CONFIG_DIR is harmless RIGHT NOW and would
    // silently redirect the config of whatever test is added after it. The
    // early `return` that used to skip this line is gone for the same reason —
    // a missing action now records its row and falls through.
    qunsetenv("JNEXT_CONFIG_DIR");
}

/// PA-22 — GH #321: File > Quick Save / Quick Load Snapshot are wired to the
/// real machine and the real folder (<JNEXT_CONFIG_DIR>/snapshots).
///
/// Oracles: the issue text and the owner decisions (no dialog, timestamped name
/// that never overwrites, Quick Load = the NEWEST .jns through the File > Load
/// route, nothing touched when there is nothing to load or the file is bad).
/// The pieces are pinned in emulator_boot_test (QS-*); what only this covers is
/// MainWindow::on_quick_save / on_quick_load.
void test_quick_snapshot_actions()
{
    QTemporaryDir home;
    qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
    const QDir snaps(home.path() + QStringLiteral("/snapshots"));
    static const QRegularExpression shape(
        QStringLiteral("^jnext-\\d{8}-\\d{6}(-\\d{2})?\\.jns$"));

    Fixture f;
    f.win.set_unattended(true);   // a refusal logs instead of blocking on a dialog
    auto find = [&f](const char* text) {
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == QString::fromLatin1(text)) return a;
        return static_cast<QAction*>(nullptr);
    };
    QAction* save = find("Quick Save Snapshot");
    QAction* load = find("Quick Load Snapshot");
    std::vector<std::string> loaded;
    f.win.set_load_file_callback([&loaded](const std::string& p, bool) { loaded.push_back(p); });

    // (d) first: nothing saved yet, the folder does not exist.
    if (load) { load->trigger(); QApplication::processEvents(); }
    check("PA-22d", "Quick Load with no snapshot loads nothing and says so in the status bar",
          load != nullptr && loaded.empty() &&
              f.win.statusBar()->currentMessage().contains(QStringLiteral("no snapshot")),
          f.win.statusBar()->currentMessage().toStdString());

    if (save) { save->trigger(); QApplication::processEvents(); }
    const QStringList one = snaps.entryList(QDir::Files);
    const bool named = one.size() == 1 && shape.match(one.first()).hasMatch();
    check("PA-22a", "Quick Save writes one timestamped non-empty .jns into <config>/snapshots",
          save != nullptr && named && QFileInfo(snaps.filePath(one.first())).size() > 0,
          one.join(QLatin1Char(',')).toStdString());

    if (save) { save->trigger(); QApplication::processEvents(); }
    const QStringList two = snaps.entryList(QDir::Files, QDir::Name);
    check("PA-22b", "a second Quick Save writes a second file and overwrites nothing",
          two.size() == 2 && two[0] != two[1],
          two.join(QLatin1Char(',')).toStdString());

    // (c) make the lexically FIRST file the newer one, so name order and mtime
    // order disagree.
    std::filesystem::last_write_time(
        snaps.filePath(two[0]).toStdString(),
        std::filesystem::file_time_type::clock::now() + std::chrono::hours(1));
    if (load) { load->trigger(); QApplication::processEvents(); }
    check("PA-22c", "Quick Load hands the newest file to the File > Load route, exactly once",
          loaded.size() == 1 && loaded[0] == snaps.filePath(two[0]).toStdString(),
          loaded.empty() ? std::string("no load") : loaded[0]);

    // (e) a garbage newest file is refused before any load is requested.
    {
        QFile junk(snaps.filePath(QStringLiteral("junk.jns")));
        if (junk.open(QIODevice::WriteOnly)) junk.write("not a snapshot");
        junk.close();
        std::filesystem::last_write_time(
            junk.fileName().toStdString(),
            std::filesystem::file_time_type::clock::now() + std::chrono::hours(2));
    }
    loaded.clear();
    if (load) { load->trigger(); QApplication::processEvents(); }
    // The wording is Quick Load's own ("Quick Load: refused"), not File > Load's
    // dialog text ("Cannot load"): Quick Load asks for no dialog.
    check("PA-22e", "Quick Load of an unreadable newest .jns requests no load and says so in the "
                    "status bar, no dialog (the session is kept)",
          load != nullptr && loaded.empty() &&
              f.win.statusBar()->currentMessage().startsWith(QStringLiteral("Quick Load: refused")),
          f.win.statusBar()->currentMessage().toStdString());

    // (f) the same refusal on File > Load's own route: handle_load_path() is
    // what the menu picker calls, and the cold boot it requests would throw the
    // session away before load_jns_file() refused the file.
    loaded.clear();
    f.win.handle_load_path(snaps.filePath(QStringLiteral("junk.jns")));
    check("PA-22f", "File > Load of an unreadable .jns requests no cold boot (the session is kept)",
          loaded.empty(), loaded.empty() ? std::string("none") : loaded[0]);

    qunsetenv("JNEXT_CONFIG_DIR");
}

/// PA-25 - GH #386: Quick Save / Quick Load take their turn in the key queue.
/// With a host-action callback set, the File menu / chord only RAISES the
/// request; the frontend performs it later (quick_save_now / quick_load_now),
/// once the keys typed before it have been shown to a frame. Unset, they act
/// at once, which PA-22 pins. (PA-25c, the one-digest-per-Quick-Load count, was
/// retired by GH #396 with the digest itself.)
void test_quick_snapshot_host_actions()
{
    QTemporaryDir home;
    qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
    const QDir snaps(home.path() + QStringLiteral("/snapshots"));

    const char* env = std::getenv("JNEXT_TEST_SD_IMAGE");
    const std::string sd = env ? std::string(env)
                               : std::string(std::getenv("HOME") ? std::getenv("HOME") : ".") +
                                     "/.jnext/sdcard/cspect-next-1gb-fixed.img";
    Fixture f(MachineType::ZX48K, -1, sd);
    f.win.set_unattended(true);
    auto find = [&f](const char* text) {
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == QString::fromLatin1(text)) return a;
        return static_cast<QAction*>(nullptr);
    };
    QAction* save = find("Quick Save Snapshot");
    QAction* load = find("Quick Load Snapshot");
    std::vector<std::string> loaded;
    f.win.set_load_file_callback([&loaded](const std::string& p, bool) { loaded.push_back(p); });
    std::vector<jnext::QuickAction> raised;
    f.win.set_host_action_callback([&raised](jnext::QuickAction a) { raised.push_back(a); });

    // (a) Quick Save: the slot only raises the request.
    if (save) { save->trigger(); QApplication::processEvents(); }
    const bool nothing_yet = !snaps.exists() || snaps.entryList(QDir::Files).isEmpty();
    const bool raised_save = raised.size() == 1 && raised[0] == jnext::QuickAction::Save;
    f.win.quick_save_now();
    const QStringList one = snaps.entryList(QDir::Files);
    check("PA-25a", "with a host-action callback, Quick Save writes NO file until the frontend "
                    "services the request, then exactly one",
          save != nullptr && nothing_yet && raised_save && one.size() == 1,
          "nothing_yet=" + std::to_string(nothing_yet) + " raised=" + std::to_string(raised.size()) +
              " files=" + one.join(QLatin1Char(',')).toStdString());

    // (c) one digest per Quick Load probe. Capture the emulator log.
    std::ostringstream cap;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(cap);
    sink->set_pattern("%v");
    Log::emulator()->sinks().push_back(sink);
    auto digests = [&cap]() {
        size_t n = 0;
        const std::string t = cap.str();
        const std::string key = "snapshot: digested SD image";
        for (size_t p = t.find(key); p != std::string::npos; p = t.find(key, p + 1)) ++n;
        return n;
    };

    // (b) Quick Load: the slot only raises the request; nothing reaches the load route.
    raised.clear();
    if (load) { load->trigger(); QApplication::processEvents(); }
    const bool not_loaded_yet = loaded.empty();
    const bool raised_load = raised.size() == 1 && raised[0] == jnext::QuickAction::Load;
    const size_t digests_after_raise = digests();
    const uint64_t rchar0 = rchar_bytes();
    f.win.quick_load_now();
    const uint64_t quick_load_read = rchar_bytes() - rchar0;
    check("PA-25b", "with a host-action callback, Quick Load hands nothing to the load route "
                    "until the frontend services the request, then exactly once",
          load != nullptr && not_loaded_yet && raised_load && digests_after_raise == 0 &&
              loaded.size() == 1,
          "loaded_before=" + std::to_string(!not_loaded_yet) + " raised=" +
              std::to_string(raised.size()) + " loaded=" + std::to_string(loaded.size()));
    // PA-25d (GH #396): the frontend-level guard that replaces PA-25c's
    // digest-count oracle. A Qt Quick Load (probe included) must not read the
    // card: under the old code it read >= 1 GiB; the bound is 16 MiB. A
    // positive control proves the counter moves, so an unreadable
    // /proc/self/io fails the row instead of passing it.
    {
        constexpr uint64_t kControl = 32ull * 1024 * 1024;
        std::ifstream cf(sd, std::ios::binary);
        std::vector<char> buf(1024 * 1024);
        const uint64_t c0 = rchar_bytes();
        for (uint64_t got = 0; got < kControl && cf; got += buf.size())
            cf.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const uint64_t control = rchar_bytes() - c0;
        const bool instrument_ok = c0 != 0 && control >= kControl;
        check("PA-25d", "a Qt Quick Load serviced through quick_load_now() reads under 16 MiB "
                        "(no whole-card read), with a control read proving the counter works",
              instrument_ok && loaded.size() == 1 && quick_load_read < 16ull * 1024 * 1024,
              "control=" + std::to_string(control) +
                  " quick_load_read=" + std::to_string(quick_load_read));
    }
    Log::emulator()->sinks().pop_back();

    qunsetenv("JNEXT_CONFIG_DIR");
}

/// PA-24 — GH #323: File > Load of a .sna/.szx/.z80 runs it on the machine it was
/// saved on, so a snapshot jnext cannot run is refused BEFORE the cold boot (the
/// session is kept), exactly as PA-22f does for a .jns. Oracle: the file's own
/// header (libspectrum z80.c:536-560: hardware mode 128 is a Timex TS2068; sna.c:
/// a .sna is at least 49179 bytes); the positive control is a valid 128K .sna.
void test_snapshot_load_refusal()
{
    QTemporaryDir home;
    qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
    Fixture f;
    f.win.set_unattended(true);
    std::vector<std::string> loaded;
    f.win.set_load_file_callback([&loaded](const std::string& p, bool) { loaded.push_back(p); });
    auto write = [&](const char* name, const std::vector<uint8_t>& b) {
        const QString p = home.path() + QLatin1Char('/') + QLatin1String(name);
        QFile out(p);
        if (out.open(QIODevice::WriteOnly))
            out.write(reinterpret_cast<const char*>(b.data()), static_cast<qint64>(b.size()));
        return p;
    };

    std::vector<uint8_t> ts2068(32 + 54, 0);          // a v3 .z80, hardware mode 128 (TS2068)
    ts2068[30] = 54; ts2068[34] = 128;
    f.win.handle_load_path(write("ts2068.z80", ts2068));
    check("PA-24a", "File > Load of a TS2068 .z80 requests no cold boot (the session is kept)",
          loaded.empty(), loaded.empty() ? std::string("none") : loaded[0]);

    loaded.clear();
    f.win.handle_load_path(write("short.sna", std::vector<uint8_t>(100, 0)));
    check("PA-24b", "File > Load of a truncated .sna requests no cold boot",
          loaded.empty(), loaded.empty() ? std::string("none") : loaded[0]);

    loaded.clear();
    const QString good = write("good128.sna", std::vector<uint8_t>(131103, 0));
    f.win.handle_load_path(good);
    check("PA-24c", "control: File > Load of a valid 128K .sna IS requested",
          loaded.size() == 1 && loaded[0] == good.toStdString(),
          loaded.empty() ? std::string("none") : loaded[0]);


    // PA-24d: a file the TABLE accepts (.szx ID 1) that its own loader's PARSE rejects (a 3-byte AY
    // chunk; libspectrum szx.c:471-497 wants exactly 18): refused before the cold boot too.
    std::vector<uint8_t> bad = {'Z', 'X', 'S', 'T', 1, 4, 1, 0};
    std::vector<uint8_t> z80r(37, 0);
    auto chunk = [&bad](const char* id, const std::vector<uint8_t>& d) {
        bad.insert(bad.end(), id, id + 4);
        for (int k = 0; k < 4; ++k) bad.push_back(static_cast<uint8_t>(d.size() >> (8 * k)));
        bad.insert(bad.end(), d.begin(), d.end());
    };
    chunk("Z80R", z80r);
    chunk("AY\0\0", {0, 0, 0});
    loaded.clear();
    f.win.handle_load_path(write("badscld.szx", bad));
    check("PA-24d", "File > Load of a .szx the table accepts but the loader cannot parse requests no cold boot",
          loaded.empty(), loaded.empty() ? std::string("none") : loaded[0]);

    // PA-24e/f: GH #381 (RF-1). An extension nothing owns routes to the NEX loader, which wants the
    // 8-byte prologue "Next" + version (NEX file format, doc/REFERENCES.md; nex_loader.cpp
    // probe_version), so a file without it cannot load: refused BEFORE the cold boot, naming the formats.
    // The control is a real NEX header under an odd extension: it still loads.
    loaded.clear();
    const std::string notes = "just some notes, no NEX header\n";
    f.win.handle_load_path(write("notes.txt", std::vector<uint8_t>(notes.begin(), notes.end())));
    const QString refusal = f.win.statusBar()->currentMessage();
    check("PA-24e", "File > Load of a file with no NEX header and no known extension requests no cold boot "
                    "and the refusal names the formats and POK Trainers",
          loaded.empty() && refusal.startsWith(QStringLiteral("Cannot load ")) &&
              refusal.contains(QStringLiteral(".rzx")) && refusal.contains(QStringLiteral("POK Trainers")),
          (loaded.empty() ? std::string("none") : loaded[0]) + " | " + refusal.toStdString());

    loaded.clear();
    std::vector<uint8_t> odd(512, 0);
    std::memcpy(odd.data(), "NextV1.2", 8);
    const QString oddpath = write("game.bin", odd);
    f.win.handle_load_path(oddpath);
    check("PA-24f", "control: File > Load of a NEX V1.2 header under an unknown extension IS requested",
          loaded.size() == 1 && loaded[0] == oddpath.toStdString(),
          loaded.empty() ? std::string("none") : loaded[0]);

    // PA-24g: GH #381 review F3. The same refusal for a file that DOES carry the NEX extension but is not a
    // NEX (a saved web page; a name that does not exist): the reason says it is not a NEX file and does not
    // call the extension unknown or point at POK Trainers, which would contradict the name. The reason is
    // the text after the path, so a path that happens to contain such a word cannot matter.
    loaded.clear();
    const std::string html = "<html><body>404 Not Found</body></html>\n";
    const std::vector<uint8_t> page(html.begin(), html.end());
    std::string detail;
    bool nex_ok = true;
    for (const QString& p : {write("game.nex", page), write("PAGE.NEX", page),
                             home.path() + QStringLiteral("/missing.nex")}) {
        f.win.handle_load_path(p);
        const QString shown = f.win.statusBar()->currentMessage();
        const QString reason = shown.mid(shown.indexOf(p) + p.size());
        const bool says = shown.startsWith(QStringLiteral("Cannot load ")) && shown.indexOf(p) >= 0 &&
                          reason.contains(QStringLiteral("not a NEX file")) &&
                          reason.contains(QStringLiteral("\"Next\" header")) &&
                          !reason.contains(QStringLiteral("extension")) &&
                          !reason.contains(QStringLiteral("POK")) && !reason.contains(QStringLiteral(".pok"));
        if (!says) {
            nex_ok = false;
            detail += "[" + shown.toStdString() + "]";
        }
    }
    check("PA-24g", "File > Load of a .nex (any case, present or missing) that has no NEX header requests no cold "
                    "boot and says it is not a NEX file, without calling its extension unknown or mentioning "
                    ".pok (NEX file format: a NEX starts with \"Next\"; doc/REFERENCES.md)",
          loaded.empty() && nex_ok, (loaded.empty() ? std::string("none") : loaded[0]) + " " + detail);

    qunsetenv("JNEXT_CONFIG_DIR");
}

/// PA-22g..j — GH #321: Quick Save prunes the oldest quick snapshots beyond the
/// retention count, from the setting or, when given, the CLI flag.
///
/// Oracle: the owner decision (default 20; the setting in jnext.conf
/// [snapshot] quick_keep; `--quick-snapshot-keep` overrides it; only jnext's own
/// quick-snapshot names are ever deleted). The files that exist before the save
/// are made old by an explicit mtime, so "oldest" is not the new one.
void test_quick_save_prunes()
{
    static const QRegularExpression pattern(
        QStringLiteral("^jnext-\\d{8}-\\d{6}(-\\d{2})?\\.jns$"));
    auto run = [](const char* conf_value, int keep_cli, int precreated, bool foreign,
                  QStringList& left, bool& new_kept, bool& foreign_kept) {
        QTemporaryDir home;
        if (conf_value) {
            QSettings raw(home.path() + QStringLiteral("/jnext.conf"), QSettings::IniFormat);
            raw.beginGroup("snapshot");
            raw.setValue("quick_keep", conf_value);
            raw.endGroup();
            raw.sync();
        }
        qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
        const QDir snaps(home.path() + QStringLiteral("/snapshots"));
        QDir().mkpath(snaps.path());
        const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(48);
        for (int i = 0; i < precreated; ++i) {
            const QString name = QStringLiteral("jnext-20200101-%1.jns")
                                     .arg(i, 6, 10, QLatin1Char('0'));
            QFile f(snaps.filePath(name));
            if (f.open(QIODevice::WriteOnly)) f.write("x");
            f.close();
            std::filesystem::last_write_time(f.fileName().toStdString(),
                                             old + std::chrono::seconds(i));
        }
        if (foreign) {
            QFile f(snaps.filePath(QStringLiteral("mine.jns")));
            if (f.open(QIODevice::WriteOnly)) f.write("x");
            f.close();
            std::filesystem::last_write_time(f.fileName().toStdString(), old - std::chrono::hours(9));
        }
        {
            Fixture f(MachineType::ZX48K, keep_cli);
            f.win.set_unattended(true);
            QAction* save = nullptr;
            for (QAction* a : f.win.findChildren<QAction*>())
                if (a->text() == QStringLiteral("Quick Save Snapshot")) save = a;
            if (save) { save->trigger(); QApplication::processEvents(); }
        }
        left.clear();
        new_kept = false;
        for (const QString& n : snaps.entryList(QDir::Files, QDir::Name)) {
            left << n;
            if (!n.startsWith(QStringLiteral("jnext-2020")) && pattern.match(n).hasMatch())
                new_kept = true;
        }
        foreign_kept = left.contains(QStringLiteral("mine.jns"));
        qunsetenv("JNEXT_CONFIG_DIR");
    };

    QStringList left; bool nk = false, fk = false;
    auto quick = [&]() { int c = 0; for (const QString& n : left) if (pattern.match(n).hasMatch()) ++c; return c; };

    run("2", -1, 5, true, left, nk, fk);
    check("PA-22g", "with quick_keep=2 in jnext.conf a Quick Save leaves the new snapshot and one "
                    "older, and does not touch a file that is not jnext's",
          quick() == 2 && nk && fk, left.join(QLatin1Char(',')).toStdString());

    run("5", 1, 3, false, left, nk, fk);
    check("PA-22h", "--quick-snapshot-keep 1 overrides quick_keep=5 from jnext.conf",
          quick() == 1 && nk, left.join(QLatin1Char(',')).toStdString());

    run("unlimited", -1, 25, false, left, nk, fk);
    check("PA-22i", "quick_keep=unlimited deletes nothing (25 old + 1 new)",
          quick() == 26 && nk, std::to_string(quick()));

    run(nullptr, -1, 25, false, left, nk, fk);
    check("PA-22j", "with no setting and no flag the default of 20 applies (25 old + 1 new -> 20)",
          quick() == 20 && nk, std::to_string(quick()));
}

/// PA-23 — GH #321: Preferences has a control for the retention count, carries
/// it through Apply, and an untouched dialog does not reset it (collect()
/// rebuilds the whole struct, so a field without a control is silently reset).
void test_quick_keep_control()
{
    AppConfigData initial;
    initial.quick_snapshot_keep = 7;
    PreferencesDialog dlg(initial);
    auto* spin = dlg.findChild<QSpinBox*>(QStringLiteral("quickKeepSpin"));
    check("PA-23a", "Preferences exposes the quick-snapshot retention control, starting from the "
                    "persisted value, with 0 shown as Unlimited",
          spin != nullptr && spin->value() == 7 &&
              spin->specialValueText() == QStringLiteral("Unlimited") && spin->minimum() == 0,
          spin ? std::to_string(spin->value()) : "no control");
    if (!spin) return;

    AppConfigData collected;
    bool emitted = false;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { emitted = true; collected = cfg; });
    auto* buttons = dlg.findChild<QDialogButtonBox*>();
    if (buttons && buttons->button(QDialogButtonBox::Apply))
        buttons->button(QDialogButtonBox::Apply)->click();
    const bool untouched = emitted && collected.quick_snapshot_keep == 7;
    emitted = false;
    spin->setValue(0);
    if (buttons && buttons->button(QDialogButtonBox::Apply))
        buttons->button(QDialogButtonBox::Apply)->click();
    check("PA-23b", "Apply returns the edited retention count (0 = unlimited)",
          emitted && collected.quick_snapshot_keep == 0, std::to_string(collected.quick_snapshot_keep));
    check("PA-23c", "an untouched dialog does NOT reset the persisted retention count",
          untouched, std::to_string(collected.quick_snapshot_keep));
}

/// PA-27 - GH #406: File > Recent Files has no control in Preferences, and
/// collect() rebuilds the whole struct, so the list must be carried through or
/// pressing OK/Apply would wipe it.
void test_recent_files_carried()
{
    AppConfigData initial;
    initial.recent_files = {QStringLiteral("/srv/u/a.nex"), QStringLiteral("/srv/u/b.tap")};
    PreferencesDialog dlg(initial);
    AppConfigData collected;
    bool emitted = false;
    QObject::connect(&dlg, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { emitted = true; collected = cfg; });
    auto* buttons = dlg.findChild<QDialogButtonBox*>();
    if (buttons && buttons->button(QDialogButtonBox::Apply))
        buttons->button(QDialogButtonBox::Apply)->click();
    check("PA-27", "an untouched Preferences dialog does NOT wipe the recent-files list",
          emitted && collected.recent_files == initial.recent_files,
          collected.recent_files.join('|').toStdString());
}

/// PA-16 — issue #35: the policy lives in the frontend's frame sequencer, not
/// in the emulator, so Apply must forward it. It applies live in every
/// outcome, including the one where the user declined a machine-type restart.
void test_when_slow_prefer_reaches_the_frontend()
{
    Fixture f(MachineType::ZX48K);
    std::vector<audio_pacing::WhenSlowPrefer> seen;
    f.win.set_when_slow_prefer_callback(
        [&](audio_pacing::WhenSlowPrefer p) { seen.push_back(p); });

    AppConfigData cfg = live_prefs(MachineType::ZX48K);
    cfg.when_slow_prefer = audio_pacing::WhenSlowPrefer::Video;
    f.win.apply_preferences(cfg);
    check("PA-16a", "Apply forwards the policy to the frontend exactly once",
          seen.size() == 1 && seen[0] == audio_pacing::WhenSlowPrefer::Video,
          "calls=" + std::to_string(seen.size()));

    // Declining a machine-type restart must not discard it either.
    Fixture g(MachineType::ZX48K);
    g.answer = false;
    std::vector<audio_pacing::WhenSlowPrefer> seen_declined;
    g.win.set_when_slow_prefer_callback(
        [&](audio_pacing::WhenSlowPrefer p) { seen_declined.push_back(p); });
    AppConfigData other = live_prefs(MachineType::ZX128K);   // a real type change
    other.when_slow_prefer = audio_pacing::WhenSlowPrefer::Video;
    g.win.apply_preferences(other);
    check("PA-16b", "a declined machine-type restart still applies the policy",
          seen_declined.size() == 1 &&
              seen_declined[0] == audio_pacing::WhenSlowPrefer::Video &&
              g.reboots.empty(),
          "calls=" + std::to_string(seen_declined.size()));
}

}  // namespace


// ── PA-19: the debugger key bindings survive a dialog nobody touched ──────
//
// GH #1's wipe hazard, and the ONE row that covers it in a build with
// ENABLE_DEBUGGER=OFF.
//
// PreferencesDialog::collect() builds a fresh AppConfigData, so any field it
// does not read back is silently reset the moment the user presses OK. That is
// not hypothetical: it is exactly what happened to the two ESP fields before
// they had a page (GH #25), and `debug_keys` is the newest field of that shape.
//
// It is here rather than in debugger_keymap_test on purpose. That suite is
// gated on ENABLE_DEBUGGER, so it cannot see the configuration where the
// hazard is WORST: a Qt build with the debugger compiled out has no Debugger
// Keys tab at all, so nothing in the dialog reads or writes the field and the
// only thing keeping it is the pair of unconditional lines in the constructor
// and in collect(). This suite is gated on ENABLE_QT_UI alone, so this row
// runs in both configurations and pins the same promise in each.
void test_debug_keymap_passthrough()
{
    AppConfigData initial;
    jnext::dbgkeys::Combo custom;
    std::string why;
    parse_combo("Ctrl+F12", custom, why);
    initial.debug_keys.set(jnext::dbgkeys::Action::StepOver, custom);
    initial.debug_keys.add_unknown_entry("future_action", "Ctrl+F11");

    PreferencesDialog untouched(initial);
    AppConfigData passthrough;
    bool emitted = false;
    QObject::connect(&untouched, &PreferencesDialog::apply_requested,
                     [&](const AppConfigData& cfg) { emitted = true; passthrough = cfg; });
    auto* buttons = untouched.findChild<QDialogButtonBox*>();
    if (buttons && buttons->button(QDialogButtonBox::Apply))
        buttons->button(QDialogButtonBox::Apply)->click();

    check("PA-19a", "an untouched dialog does NOT wipe a redefined debugger key",
          emitted
              && render_combo(passthrough.debug_keys.combo(jnext::dbgkeys::Action::StepOver))
                     == "Ctrl+F12",
          emitted ? render_combo(
                        passthrough.debug_keys.combo(jnext::dbgkeys::Action::StepOver))
                  : "nothing emitted");

    // The preserved-unknown entries ride along too: they are the mechanism that
    // stops an OLDER jnext deleting a NEWER one's binding, so a dialog that
    // dropped them would reintroduce exactly that.
    check("PA-19b", "and it does not drop the preserved unknown entries",
          emitted && passthrough.debug_keys.unknown_entries().size() == 1
              && passthrough.debug_keys.unknown_entries()[0].first == "future_action"
              && passthrough.debug_keys.unknown_entries()[0].second == "Ctrl+F11",
          std::to_string(passthrough.debug_keys.unknown_entries().size()) + " kept");

    // The untouched dialog must leave EVERY action alone, not just the one the
    // row above happens to read.
    AppConfigData expected = initial;
    check("PA-19c", "every other action is untouched as well",
          emitted && passthrough.debug_keys == expected.debug_keys,
          render_combo(passthrough.debug_keys.combo(jnext::dbgkeys::Action::Run)));
}

/// PA-26 - GH #395: the Preferences "automatic play/stop" setting reaches the
/// machine (and Apply with it on leaves it on).
void test_tape_auto_play_preference()
{
    Fixture f(MachineType::ZX48K);
    const bool on_before = f.emu.tape_auto_play();
    AppConfigData c = live_prefs(MachineType::ZX48K);
    c.tape_auto_play = false;
    f.win.apply_preferences(c);
    check("PA-26", "Preferences automatic tape play/stop OFF reaches the machine",
          on_before && !f.emu.tape_auto_play(),
          "before=" + std::to_string(on_before) + " after=" + std::to_string(f.emu.tape_auto_play()));
}

// ── GH #417: a command-line value governs the session and never reaches jnext.conf ──
//
// MainWindow reads four settings LIVE from what it loaded (the Load / Save
// Screenshot dialog directories, the Quick Screenshot directory and format), and
// saves that same copy on every unrelated change (the recent-files list, a
// joystick toggle). A `--set paths.last_load_dir=...` merged into it would be
// written back by the first such save. The session copy keeps the two apart.
// Oracle: no VHDL (a host GUI). The contract is the man page: "never written to
// jnext.conf; a GUI change takes over; Preferences shows the saved values".
namespace {

QString ini_value(const QString& dir, const char* key)
{
    QSettings raw(dir + QStringLiteral("/jnext.conf"), QSettings::IniFormat);
    return raw.value(QString::fromLatin1(key)).toString();
}

void seed_saved(const QString& dir)
{
    QSettings raw(dir + QStringLiteral("/jnext.conf"), QSettings::IniFormat);
    raw.setValue("paths/last_load_dir", "/saved/load");
    raw.setValue("paths/screenshot_dir", "/saved/shots");
    raw.setValue("screenshot/quick_dir", dir + QStringLiteral("/saved-quick"));
    raw.setValue("screenshot/quick_format", "png");
    raw.sync();
}

/// The saved file with `--set` values over it, exactly as main.cpp builds the copy
/// it hands to apply_startup_config().
AppConfigData cli_over_saved(const QString& dir, const QString& quick_dir)
{
    using config_keys::KeyId;
    AppConfig cfg(dir + QStringLiteral("/jnext.conf"));
    cfg.load();
    merge_cli_assignments(cfg.data(), {
        { KeyId::PathsLastLoadDir, {}, "/cli/load" },
        { KeyId::PathsScreenshotDir, {}, "/cli/shots" },
        { KeyId::ScreenshotQuickDir, {}, quick_dir.toStdString() },
        { KeyId::ScreenshotQuickFormat, {}, "scr" },
    });
    return cfg.data();
}

}  // namespace

void test_session_paths_not_persisted()
{
    // PA-29: the dialogs seed from the session values; an unrelated save leaves the file's.
    {
        QTemporaryDir home;
        seed_saved(home.path());
        qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
        Fixture f;
        f.win.set_unattended(true);
        f.win.apply_startup_config(cli_over_saved(home.path(), home.path() + QStringLiteral("/cli-quick")));
        const QString load_seed = f.win.load_dialog_seed();
        const QString shot_seed = f.win.screenshot_dialog_seed();
        f.win.set_startup_load("/x/a.nex", "/x/a.nex");
        f.win.startup_rzx_started("/x/a.nex");   // records a recent file: an unrelated save
        const QString saved_load = ini_value(home.path(), "paths/last_load_dir");
        const QString saved_shot = ini_value(home.path(), "paths/screenshot_dir");
        const QString recent = ini_value(home.path(), "recent/files");
        check("PA-29", "the Load and Save Screenshot dialogs open in the session's directories, and an "
                       "unrelated save leaves jnext.conf holding the saved ones",
              load_seed == QStringLiteral("/cli/load") && shot_seed == QStringLiteral("/cli/shots")
                  && saved_load == QStringLiteral("/saved/load")
                  && saved_shot == QStringLiteral("/saved/shots") && recent.contains("a.nex"),
              (load_seed + "|" + shot_seed + "|" + saved_load + "|" + saved_shot + "|" + recent).toStdString());
    }

    // PA-30: a load from directory D moves the seed AND the saved value to D.
    {
        QTemporaryDir home;
        seed_saved(home.path());
        qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
        const QString d = home.path() + QStringLiteral("/picked");
        QDir().mkpath(d);
        QFile prog(d + QStringLiteral("/prog.tap"));
        if (prog.open(QIODevice::WriteOnly)) prog.close();
        Fixture f;
        f.win.set_unattended(true);
        f.win.apply_startup_config(cli_over_saved(home.path(), home.path() + QStringLiteral("/cli-quick")));
        f.win.on_recent_file(prog.fileName());
        const QString seed = f.win.load_dialog_seed();
        const QString saved = ini_value(home.path(), "paths/last_load_dir");
        check("PA-30", "after a load from a directory the Load dialog's seed and the saved value are both "
                       "that directory: the GUI change takes over from the command line",
              seed == d && saved == d, (seed + "|" + saved).toStdString());
    }

    // PA-31: Quick Screenshot writes into the session's directory in the session's format.
    {
        QTemporaryDir home;
        seed_saved(home.path());
        qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
        const QString cli_quick = home.path() + QStringLiteral("/cli-quick");
        Fixture f;
        f.win.set_unattended(true);
        f.win.apply_startup_config(cli_over_saved(home.path(), cli_quick));
        QAction* quick = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == QStringLiteral("Quic&k Screenshot")) quick = a;
        if (quick) { quick->trigger(); QApplication::processEvents(); }
        f.win.set_startup_load("/x/b.nex", "/x/b.nex");
        f.win.startup_rzx_started("/x/b.nex");   // an unrelated save
        const QStringList made = QDir(cli_quick).entryList(QDir::Files);
        const bool one_scr = made.size() == 1 && made.first().endsWith(QStringLiteral(".scr"));
        const bool none_saved = QDir(home.path() + QStringLiteral("/saved-quick")).entryList(QDir::Files).isEmpty();
        const QString fmt = ini_value(home.path(), "screenshot/quick_format");
        const QString dir = ini_value(home.path(), "screenshot/quick_dir");
        check("PA-31", "Quick Screenshot writes a .scr into the session's directory; the file keeps the "
                       "saved directory and format",
              quick && one_scr && none_saved && fmt == QStringLiteral("png")
                  && dir == home.path() + QStringLiteral("/saved-quick"),
              (made.join(',') + "|" + fmt + "|" + dir).toStdString());
    }

    // PA-32: Preferences OK/Apply replaces the session values with the dialog's.
    {
        QTemporaryDir home;
        seed_saved(home.path());
        qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
        Fixture f;
        f.win.set_unattended(true);
        f.win.apply_startup_config(cli_over_saved(home.path(), home.path() + QStringLiteral("/cli-quick")));
        const QString dlg_quick = home.path() + QStringLiteral("/dlg-quick");
        AppConfigData c = live_prefs(MachineType::ZX48K);
        c.last_load_dir = QStringLiteral("/dlg/load");
        c.screenshot_dir = QStringLiteral("/dlg/shots");
        c.quick_screenshot_dir = dlg_quick;
        c.quick_screenshot_format = ScreenshotFormat::Png;
        f.win.apply_preferences(c);
        QAction* quick = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text() == QStringLiteral("Quic&k Screenshot")) quick = a;
        if (quick) { quick->trigger(); QApplication::processEvents(); }
        const QStringList made = QDir(dlg_quick).entryList(QDir::Files);
        check("PA-32", "Preferences Apply replaces the session's directories and quick-screenshot format "
                       "with the dialog's",
              quick && f.win.load_dialog_seed() == QStringLiteral("/dlg/load")
                  && f.win.screenshot_dialog_seed() == QStringLiteral("/dlg/shots")
                  && made.size() == 1 && made.first().endsWith(QStringLiteral(".png")),
              (f.win.load_dialog_seed() + "|" + f.win.screenshot_dialog_seed() + "|" + made.join(',')).toStdString());
    }
    qunsetenv("JNEXT_CONFIG_DIR");
}

// ── PA-34/PA-35: GH #417 - Preferences shows, and OK saves, the SAVED values ──
// PA-34 reads main_window.cpp and checks one spelling only: every `app_config_.data().last_load_dir`
// / `.screenshot_dir` there must be the target of an assignment (the remember_* helpers), i.e. no
// reader in that file takes the saved value directly. It does not see other spellings, the header or
// other files; the accessors' behaviour is held by PA-29..PA-32. PA-35 drives the real Preferences dialog: after a command-line session (paths, format,
// debugger keys), Apply writes the saved values back, not the command line's.
void test_preferences_saves_saved_values()
{
    {
        std::ifstream in(JNEXT_MAIN_WINDOW_SRC);
        std::string line, bad;
        int reads = 0;
        while (std::getline(in, line)) {
            for (const char* key : {"app_config_.data().last_load_dir", "app_config_.data().screenshot_dir"}) {
                if (line.find(key) == std::string::npos) continue;
                ++reads;
                // Must be an assignment: after the key, optional blanks, then '=' but not '=='.
                size_t at = line.find(key) + std::strlen(key);
                while (at < line.size() && line[at] == ' ') ++at;
                const bool assigns = at < line.size() && line[at] == '='
                                     && (at + 1 >= line.size() || line[at + 1] != '=');
                if (!assigns) bad += line + " | ";
            }
        }
        check("PA-34", "in main_window.cpp the saved last_load_dir / screenshot_dir are named via "
                       "app_config_.data() only as assignment targets (no direct reader in that file)",
              in.eof() && reads == 2 && bad.empty(), bad + std::to_string(reads) + " references");
    }
    QTemporaryDir home;
    seed_saved(home.path());
    qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
    bool opened = false;
    QString load_dir, shot_dir, quick_dir, quick_fmt;
    QStringList key_overrides;
    {
        Fixture f;
        f.win.set_unattended(true);
        f.win.apply_startup_config(cli_over_saved(home.path(), home.path() + QStringLiteral("/cli-quick")));
        jnext::dbgkeys::Keymap km;
        jnext::dbgkeys::Combo c;
        std::string why;
        jnext::dbgkeys::parse_combo("Shift+F9", c, why);
        km.set(jnext::dbgkeys::Action::Run, c);
        f.win.set_session_debug_keys(km);
        QAction* prefs = nullptr;
        for (QAction* a : f.win.findChildren<QAction*>())
            if (a->text().remove('&').startsWith(QStringLiteral("Preferences"))) prefs = a;
        if (prefs) {
            QTimer::singleShot(300, [&]() {
                QWidget* w = QApplication::activeModalWidget();
                auto* dlg = qobject_cast<QDialog*>(w);
                if (!dlg) return;
                opened = true;
                auto* buttons = dlg->findChild<QDialogButtonBox*>();
                if (buttons && buttons->button(QDialogButtonBox::Apply))
                    buttons->button(QDialogButtonBox::Apply)->click();
                dlg->reject();
            });
            prefs->trigger();
        }
        load_dir  = ini_value(home.path(), "paths/last_load_dir");
        shot_dir  = ini_value(home.path(), "paths/screenshot_dir");
        quick_dir = ini_value(home.path(), "screenshot/quick_dir");
        quick_fmt = ini_value(home.path(), "screenshot/quick_format");
        QSettings raw(home.path() + QStringLiteral("/jnext.conf"), QSettings::IniFormat);
        raw.beginGroup("debugger_keys");
        key_overrides = raw.childKeys();
    }
    check("PA-35", "after a command-line session, Preferences Apply saves the SAVED directories, quick "
                   "screenshot directory and format and debugger keys, not the command line's",
          opened && load_dir == QStringLiteral("/saved/load") && shot_dir == QStringLiteral("/saved/shots")
              && quick_dir == home.path() + QStringLiteral("/saved-quick") && quick_fmt == QStringLiteral("png")
              && key_overrides.isEmpty(),
          (QString::number(opened) + "|" + load_dir + "|" + shot_dir + "|" + quick_dir + "|" + quick_fmt + "|" +
           key_overrides.join(',')).toStdString());
    qunsetenv("JNEXT_CONFIG_DIR");
}

// ── PA-33: GH #417 - an Input-menu pick persists what it changed, not a command-line value ──
//
// persist_joy_config() used to copy BOTH connectors' live state into the file, so
// picking Joy 1's source wrote a `--joy2-source keys` of the same run into
// jnext.conf. Now the connector picked is saved, and the other only when the
// emulator's one-cursor rule moved it. Oracle: the contract (man page: "never
// written to jnext.conf"); the real menu action and the real file.
void test_joy_menu_persists_only_the_pick()
{
    QTemporaryDir home;
    qputenv("JNEXT_CONFIG_DIR", home.path().toUtf8());
    const QString conf = home.path() + QStringLiteral("/jnext.conf");
    const auto pick = [](Fixture& f, const char* menu_prefix, const char* item) {
        for (QMenu* m : f.win.findChildren<QMenu*>()) {
            if (!m->title().remove('&').startsWith(QString::fromLatin1(menu_prefix))) continue;
            for (QAction* a : m->actions())
                if (a->text().remove('&').startsWith(QString::fromLatin1(item))) { a->trigger(); return true; }
        }
        return false;
    };
    // (a) Joy 2 runs a command-line value; picking Joy 1 None must not save it.
    bool picked_a = false;
    QString j1_a, j2_a;
    {
        Fixture f;
        f.emu.set_joystick_source(1, JoySource::CursorKeys);   // `--joy2-source keys`
        picked_a = pick(f, "Joy 1 Source", "None");
        j1_a = ini_value(home.path(), "input/joy1_source");
        j2_a = ini_value(home.path(), "input/joy2_source");
    }
    // (b) Joy 2 holds Cursor Keys (saved AND live); picking Cursor Keys on Joy 1 takes them away
    // from Joy 2 (the emulator's rule). That change is the pick's doing and is saved: the file
    // must stop saying `keys` for Joy 2, which only a save of the OTHER connector can do.
    {
        QSettings raw(conf, QSettings::IniFormat);
        raw.setValue("input/joy2_source", "keys");
        raw.sync();
    }
    bool picked_b = false;
    QString j1_b, j2_b;
    JoySource live_j2 = JoySource::CursorKeys;
    {
        Fixture f;
        f.emu.set_joystick_source(1, JoySource::CursorKeys);
        picked_b = pick(f, "Joy 1 Source", "Cursor");
        live_j2 = f.emu.joystick_source(1);
        j1_b = ini_value(home.path(), "input/joy1_source");
        j2_b = ini_value(home.path(), "input/joy2_source");
    }
    // (c) The device half: Joy 2 holds controller X (saved and live); Joy 1 picks X. The emulator
    // takes X off Joy 2, and the file must lose Joy 2's X with it.
    const std::string x = "0123456789abcdef0123456789abcdef";
    {
        QSettings raw(conf, QSettings::IniFormat);
        raw.setValue("input/joy2_device", QString::fromStdString(x));
        raw.setValue("input/joy2_source", "sdl");
        raw.sync();
    }
    bool picked_c = false;
    QString j1_c, j2_c;
    {
        Fixture f;
        f.win.set_joy_device_provider([&]() { return std::vector<JoyDeviceInfo>{ { x, "Pad X", -1 } }; });
        f.emu.set_joystick_device(1, JoyDeviceRef{ x, "Pad X" });
        f.win.sync_joy_source_menu();
        picked_c = pick(f, "Joy 1 Source", "Pad X");
        j1_c = ini_value(home.path(), "input/joy1_device");
        j2_c = ini_value(home.path(), "input/joy2_device");
    }
    check("PA-33", "an Input-menu pick saves its own connector and not the other connector's command-line "
                   "value; a source or controller change the pick forced on the other one is saved with it",
          picked_a && picked_b && picked_c && j1_a == QLatin1String("none") && j2_a != QLatin1String("keys")
              && j1_b == QLatin1String("keys") && live_j2 != JoySource::CursorKeys
              && j2_b == QString::fromLatin1(joy_source_str(live_j2)) && j2_b != QLatin1String("keys")
              && j1_c == QString::fromStdString(x) && j2_c.isEmpty(),
          (j1_a + "|" + j2_a + "|" + j1_b + "|" + j2_b + "|" + j1_c + "|" + j2_c).toStdString());
    qunsetenv("JNEXT_CONFIG_DIR");
}

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
    test_audio_gain_preference_control();
    test_esp_preference_controls();
    test_pentagon_preference();
    test_nextpi_preference_controls();
    test_nextpi_qemu_args_control();
    test_esp_settings_reach_the_frontend();
    test_esp_forward_precedes_the_reboot();
    test_when_slow_prefer_control();
    test_when_slow_prefer_reaches_the_frontend();
    test_quick_screenshot_controls();
    test_quick_screenshot_writes_a_file();
    test_quick_snapshot_actions();
    test_quick_snapshot_host_actions();
    test_snapshot_load_refusal();
    test_quick_save_prunes();
    test_quick_keep_control();
    test_recent_files_carried();
    test_debug_keymap_passthrough();
    test_tape_auto_play_preference();
    test_session_paths_not_persisted();
    test_joy_menu_persists_only_the_pick();
    test_preferences_saves_saved_values();

    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
