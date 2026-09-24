// AppConfig unit test (Task 66 — Configurability).
//
// Covers src/gui/app_config.{h,cpp}: defaults when no on-disk file exists,
// round-trip save()->load() for every persisted field, resilience to a
// missing or malformed on-disk file, and the CLI-vs-saved-preference
// precedence helper (merge_cli_precedence<T>) main.cpp uses to decide,
// field by field, whether a saved GUI preference or the command line wins.
//
// AppConfig never touches the real user config here: every row uses the
// explicit-INI-path constructor (QSettings::IniFormat) against a fresh temp
// file, never QSettings("JNEXT", "jnext") (the production ctor, only used by
// MainWindow — see src/gui/main_window.cpp).
//
// Standalone runner, no GoogleTest — same idiom as the other subsystem
// suites (fuse_z80_test, copper_test, ...). No QApplication/QCoreApplication
// is constructed: AppConfig only uses QSettings, which needs neither for the
// explicit-file-path IniFormat constructor used throughout.

#include "gui/app_config.h"

#include "debug/debug_keymap.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>

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

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
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

// One fresh, non-existent .ini path per caller. All rows share a single
// QTemporaryDir (cleaned up when it goes out of scope in main()), so each
// group must ask for a DIFFERENT filename inside it — otherwise a later
// group would silently reopen an earlier group's already-written file.
QString fresh_ini_path(QTemporaryDir& dir, const char* name) {
    return dir.filePath(QString(name) + ".conf");
}

} // namespace

// ── AC-DEFAULT: no on-disk file -> AppConfigData{} defaults ────────────

static void test_defaults_no_file(QTemporaryDir& dir) {
    set_group("AC-DEFAULT");

    const QString path = fresh_ini_path(dir, "default");
    AppConfig cfg(path);
    check("AC-01", "fresh temp path has no file before load()",
          !QFile::exists(path));

    cfg.load();
    const AppConfigData def;
    const AppConfigData& d = cfg.data();

    check("AC-02", "machine_type defaults to ZXN_ISSUE2", d.machine_type == def.machine_type);
    check("AC-03", "cpu_speed defaults to MHZ_3_5", d.cpu_speed == def.cpu_speed);
    check("AC-04", "emulator_speed_percent defaults to 100",
          d.emulator_speed_percent == def.emulator_speed_percent);
    check("AC-05", "window_scale defaults match AppConfigData{}", d.window_scale == def.window_scale);
    check("AC-06", "crt_filter defaults to false", d.crt_filter == def.crt_filter);
    check("AC-07", "silent defaults to false", d.silent == def.silent);
    check("AC-08", "tape_fast_load defaults to true", d.tape_fast_load == def.tape_fast_load);
    // Issue #35 — the default must be the behaviour that existed before the
    // option, or an upgrade changes how every machine degrades.
    check("AC-58", "when_slow_prefer defaults to Audio",
          d.when_slow_prefer == audio_pacing::WhenSlowPrefer::Audio &&
              def.when_slow_prefer == audio_pacing::WhenSlowPrefer::Audio);
    check("AC-46", "audio_gain_db defaults to 0 dB", d.audio_gain_db == 0.0f);
    check("AC-43", "joy_source defaults to Sdl/Sdl",
          d.joy_source[0] == JoySource::Sdl && d.joy_source[1] == JoySource::Sdl);
    check("AC-09", "last_load_dir defaults to empty", d.last_load_dir.isEmpty());
    check("AC-10", "sd_card_path defaults to empty", d.sd_card_path.isEmpty());
    check("AC-11", "screenshot_dir defaults to empty", d.screenshot_dir.isEmpty());
    // GH #19 — an empty quick_screenshot_dir MEANS ~/.jnext/screenshots, and
    // the struct must not bake an absolute home path in: a config written on
    // one machine must not pin another machine's home directory.
    check("AC-65", "quick_screenshot_dir defaults to empty (= ~/.jnext/screenshots)",
          d.quick_screenshot_dir.isEmpty() && def.quick_screenshot_dir.isEmpty());
    check("AC-66", "quick_screenshot_format defaults to PNG",
          d.quick_screenshot_format == ScreenshotFormat::Png
              && def.quick_screenshot_format == ScreenshotFormat::Png);
    // GH #25 — the ESP is a network capability, so "no config file" MUST mean
    // "not on the network". This row is the one that would catch a default
    // flipped by accident.
    check("AC-53", "esp_enabled defaults to false (the guest is not on the network)",
          d.esp_enabled == false && def.esp_enabled == false);
    check("AC-54", "esp_allowed_hosts defaults to empty", d.esp_allowed_hosts.empty());
    check("AC-12", "loaded_from_existing_file() is false when no file existed",
          !cfg.loaded_from_existing_file());
}

// ── AC-ROUNDTRIP: save() then a fresh AppConfig's load() sees every field ──

static void test_roundtrip(QTemporaryDir& dir) {
    set_group("AC-ROUNDTRIP");

    const QString path = fresh_ini_path(dir, "roundtrip");
    AppConfigData written;
    {
        AppConfig writer(path);
        writer.data().machine_type           = MachineType::ZX_PLUS3;
        writer.data().cpu_speed              = CpuSpeed::MHZ_14;
        writer.data().emulator_speed_percent = 400;
        writer.data().window_scale           = 3;
        writer.data().crt_filter             = true;
        writer.data().silent                 = true;
        writer.data().tape_fast_load         = false;
        writer.data().when_slow_prefer       = audio_pacing::WhenSlowPrefer::Video;
        writer.data().audio_gain_db          = -12.5f;
        writer.data().joy_source[0]          = JoySource::CursorKeys;
        writer.data().joy_source[1]          = JoySource::Sdl;
        writer.data().last_load_dir          = "/home/user/games";
        writer.data().sd_card_path           = "/home/user/sd/next.img";
        writer.data().screenshot_dir         = "/home/user/shots";
        writer.data().quick_screenshot_dir    = "/home/user/quick-shots";
        writer.data().quick_screenshot_format = ScreenshotFormat::Scr;
        writer.data().esp_enabled            = true;
        writer.data().esp_allowed_hosts      = {"nx.nxtel.org", "sync.lan"};
        writer.save();
        written = writer.data();
    }

    AppConfig reader(path);
    check("AC-13", "the saved file exists before the fresh reader loads it",
          QFile::exists(path));
    reader.load();
    const AppConfigData& d = reader.data();

    check("AC-14", "machine_type round-trips", d.machine_type == written.machine_type);
    check("AC-15", "cpu_speed round-trips", d.cpu_speed == written.cpu_speed);
    check("AC-16", "emulator_speed_percent round-trips",
          d.emulator_speed_percent == written.emulator_speed_percent);
    check("AC-17", "window_scale round-trips", d.window_scale == written.window_scale);
    check("AC-18", "crt_filter round-trips", d.crt_filter == written.crt_filter);
    check("AC-19", "silent round-trips", d.silent == written.silent);
    check("AC-20", "tape_fast_load round-trips", d.tape_fast_load == written.tape_fast_load);
    check("AC-59", "when_slow_prefer round-trips",
          d.when_slow_prefer == written.when_slow_prefer);
    check("AC-47", "audio_gain_db round-trips",
          std::abs(d.audio_gain_db - written.audio_gain_db) < 0.001f);
    check("AC-21", "last_load_dir round-trips", d.last_load_dir == written.last_load_dir);
    check("AC-22", "sd_card_path round-trips", d.sd_card_path == written.sd_card_path);
    check("AC-23", "screenshot_dir round-trips", d.screenshot_dir == written.screenshot_dir);
    check("AC-67", "quick_screenshot_dir round-trips",
          d.quick_screenshot_dir == written.quick_screenshot_dir);
    check("AC-68", "quick_screenshot_format round-trips as \"scr\"",
          d.quick_screenshot_format == written.quick_screenshot_format);
    check("AC-24", "loaded_from_existing_file() is true once a file was written",
          reader.loaded_from_existing_file());
    check("AC-44", "joy_source round-trips (Joy 1 keys, Joy 2 sdl)",
          d.joy_source[0] == written.joy_source[0] &&
          d.joy_source[1] == written.joy_source[1]);
    check("AC-55", "esp_enabled round-trips", d.esp_enabled == written.esp_enabled);
    check("AC-56", "esp_allowed_hosts round-trips in order",
          d.esp_allowed_hosts == written.esp_allowed_hosts);
}

// ── AC-PARTIAL: a file with only SOME keys present ─────────────────────
// (missing-key resilience, distinct from AC-DEFAULT's missing-FILE case)

static void test_partial_file(QTemporaryDir& dir) {
    set_group("AC-PARTIAL");

    const QString path = fresh_ini_path(dir, "partial");
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.beginGroup("startup");
        raw.setValue("crt_filter", true);
        // Every other startup/* key, and the whole paths/ group, is left
        // absent on purpose.
        raw.endGroup();
        raw.sync();
    }

    AppConfig cfg(path);
    cfg.load();
    const AppConfigData def;
    const AppConfigData& d = cfg.data();

    check("AC-25", "the one present key (crt_filter) is honoured", d.crt_filter == true);
    check("AC-26", "an absent startup key (cpu_speed) falls back to default",
          d.cpu_speed == def.cpu_speed);
    check("AC-27", "an absent startup key (emulator_speed_percent) falls back to default",
          d.emulator_speed_percent == def.emulator_speed_percent);
    check("AC-28", "an absent startup key (tape_fast_load) falls back to default",
          d.tape_fast_load == def.tape_fast_load);
    check("AC-29", "an entirely absent group (paths/*) falls back to default",
          d.last_load_dir.isEmpty() && d.sd_card_path.isEmpty() && d.screenshot_dir.isEmpty());
    check("AC-48", "an absent audio/gain_db falls back to 0 dB",
          d.audio_gain_db == def.audio_gain_db);
    // GH #19 — the whole [screenshot] group is absent from every config file
    // written before it existed, which is why it needed no CONFIG_VERSION
    // bump: a v1 file loads with both fields at their defaults.
    check("AC-69", "an entirely absent group (screenshot/*) falls back to defaults",
          d.quick_screenshot_dir.isEmpty()
              && d.quick_screenshot_format == def.quick_screenshot_format);
}

// ── AC-MALFORMED: garbage / out-of-range values must not be accepted ───

static void test_malformed_values(QTemporaryDir& dir) {
    set_group("AC-MALFORMED");

    const QString path = fresh_ini_path(dir, "malformed");
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.beginGroup("startup");
        raw.setValue("machine_type", "not-a-machine");   // parse_machine_type() rejects
        raw.setValue("cpu_speed", "banana");              // non-numeric
        raw.setValue("emulator_speed_percent", 999999);   // out of [10,1000]
        raw.setValue("window_scale", 0);                  // out of [MIN_SCALE,MAX_SCALE]
        raw.setValue("when_slow_prefer", "pictures");     // issue #35: not a policy
        raw.endGroup();
        raw.beginGroup("input");
        raw.setValue("joy1_source", "not-a-source");      // parse_joy_source() rejects
        raw.endGroup();
        raw.beginGroup("audio");
        raw.setValue("gain_db", 25.0);                     // out of [-24,+24]
        raw.endGroup();
        raw.beginGroup("screenshot");
        raw.setValue("quick_format", "jpeg");              // GH #19: not a format
        raw.endGroup();
        // GH #25 — a hand-edited allowlist goes through EspHostPolicy::add, so
        // blanks and case-duplicates are handled exactly as on the CLI.
        raw.beginGroup("esp");
        raw.setValue("allowed_hosts",
                     QStringList{"Example.Test", "  example.test  ", "", "   "});
        raw.endGroup();
        raw.sync();
    }

    AppConfig cfg(path);
    cfg.load();  // must not crash
    const AppConfigData def;
    const AppConfigData& d = cfg.data();

    check("AC-30", "unparseable machine_type string falls back to default",
          d.machine_type == def.machine_type);
    check("AC-31", "non-numeric cpu_speed string falls back to default",
          d.cpu_speed == def.cpu_speed);
    check("AC-32", "out-of-range emulator_speed_percent falls back to default",
          d.emulator_speed_percent == def.emulator_speed_percent);
    check("AC-33", "out-of-range window_scale falls back to default",
          d.window_scale == def.window_scale);
    check("AC-45", "unparseable joy1_source string falls back to default",
          d.joy_source[0] == def.joy_source[0]);
    // A typo must not silently change how the emulator degrades.
    check("AC-60", "unknown when_slow_prefer string falls back to default",
          d.when_slow_prefer == def.when_slow_prefer);
    check("AC-49", "out-of-range audio gain falls back to default",
          d.audio_gain_db == def.audio_gain_db);
    check("AC-57", "a hand-edited allowlist drops blanks and case-duplicates",
          d.esp_allowed_hosts.size() == 1 && d.esp_allowed_hosts[0] == "Example.Test");
    // GH #19 — a typo must not silently change what a quick capture produces.
    check("AC-70", "unknown quick_format string falls back to default (png)",
          d.quick_screenshot_format == def.quick_screenshot_format);

    // The slider narrowed the range to +/-24 dB (PR #41): a formerly-valid
    // deep attenuation must now fall back as well.
    const QString below = fresh_ini_path(dir, "below-range-gain");
    {
        QSettings raw(below, QSettings::IniFormat);
        raw.beginGroup("audio");
        raw.setValue("gain_db", -24.5);                    // out of [-24,+24]
        raw.endGroup();
        raw.sync();
    }
    AppConfig below_cfg(below);
    below_cfg.load();
    check("AC-52", "below-range audio gain falls back to default",
          below_cfg.data().audio_gain_db == def.audio_gain_db);
}

// ── AC-PRECEDENCE: merge_cli_precedence<T> — CLI always wins when given ─

static void test_merge_precedence() {
    set_group("AC-PRECEDENCE");

    check("AC-34", "cli_provided=true returns the CLI value (int)",
          merge_cli_precedence(true, 400, 100) == 400);
    check("AC-35", "cli_provided=false returns the saved value (int)",
          merge_cli_precedence(false, 400, 100) == 100);
    check("AC-36", "cli_provided=true returns the CLI value (MachineType)",
          merge_cli_precedence(true, MachineType::ZX48K, MachineType::ZXN_ISSUE2)
              == MachineType::ZX48K);
    check("AC-37", "cli_provided=false returns the saved value (MachineType)",
          merge_cli_precedence(false, MachineType::ZX48K, MachineType::ZXN_ISSUE2)
              == MachineType::ZXN_ISSUE2);
    // The --silent idiom used in main.cpp: the flag is both the "provided"
    // signal and its own (always-true) CLI value.
    check("AC-38", "--silent semantics: not passed -> saved value wins",
          merge_cli_precedence(/*silent=*/false, true, /*saved=*/false) == false);
    check("AC-39", "--silent semantics: passed -> true wins regardless of saved",
          merge_cli_precedence(/*silent=*/true, true, /*saved=*/false) == true);
    check("AC-50", "explicit CLI audio gain wins over saved gain",
          merge_cli_precedence(true, 6.0f, -12.0f) == 6.0f);
    check("AC-51", "saved audio gain wins when CLI option is absent",
          merge_cli_precedence(false, 6.0f, -12.0f) == -12.0f);
}

// ── AC-PATH: the production config lives under ~/.jnext, not ~/.config ──

static void test_default_path() {
    set_group("AC-PATH");

    const QString p = AppConfig::default_config_path();
    check("AC-40", "default config path is ~/.jnext/jnext.conf",
          p.endsWith(QStringLiteral("/.jnext/jnext.conf")), p.toStdString());
    check("AC-41", "default config path is not under ~/.config",
          !p.contains(QStringLiteral("/.config/")), p.toStdString());

    // JNEXT_CONFIG_DIR override (used by regression.sh for test isolation).
    qputenv("JNEXT_CONFIG_DIR", "/tmp/jnext-cfg-test");
    const QString over = AppConfig::default_config_path();
    qunsetenv("JNEXT_CONFIG_DIR");
    check("AC-42", "JNEXT_CONFIG_DIR overrides the config directory",
          over == QStringLiteral("/tmp/jnext-cfg-test/jnext.conf"), over.toStdString());
}


// ── DK: GH #1 — the debugger key bindings ──────────────────────────────────
//
// The model (grammar, validation, conflict resolution) is Qt-free and lives in
// src/debug/debug_keymap.cpp; its persistence is AppConfig's. Both are covered
// here because the persistence RULE — only redefinitions are written — is only
// meaningful against the defaults the model declares.
//
// No VHDL oracle: host UI configuration, not emulated hardware.

static void test_debug_keys_defaults(QTemporaryDir& dir) {
    set_group("DK");
    using namespace jnext::dbgkeys;

    AppConfig cfg(fresh_ini_path(dir, "dk_defaults"));
    cfg.load();
    const Keymap& km = cfg.data().debug_keys;

    // DK-01 is the guard on the ONE promise GH #1 makes about behaviour: it
    // adds redefinability and changes no default. Every value below was read
    // out of DebuggerWindow::create_menus() in the running product.
    struct Expect { Action a; const char* text; };
    const Expect expected[] = {
        { Action::Run,         "F5"       },
        { Action::Pause,       "F9"       },
        { Action::StepInto,    "F6"       },
        { Action::StepOver,    "F7"       },
        { Action::StepOut,     "F8"       },
        { Action::StepBack,    "Shift+F7" },
        { Action::FrameBack,   "Shift+F6" },
        { Action::RunToCursor, "none"     },
        { Action::RunToEof,    "none"     },
        { Action::RunToEosl,   "none"     },
        { Action::TraceToggle, "F2"       },
        { Action::TraceExport, "F3"       },
    };
    bool all_ok = true;
    std::string detail;
    for (const Expect& e : expected) {
        const std::string got = render_combo(km.combo(e.a));
        if (got != e.text) {
            all_ok = false;
            detail += std::string(info(e.a).id) + "=" + got + " (want " + e.text + ") ";
        }
    }
    check("DK-01", "with no config file every action carries its shipped default",
          all_ok, detail);

    check("DK-02", "no entry is refused when there is no [debugger_keys] section",
          cfg.debug_key_issues().empty(),
          std::to_string(cfg.debug_key_issues().size()) + " issues");

    // The ids are the config-file surface; a rename orphans every existing
    // user binding, so the list is pinned rather than merely used.
    std::string ids;
    for (int i = 0; i < ACTION_COUNT; ++i) ids += std::string(action_table()[i].id) + " ";
    check("DK-03", "the action ids are exactly the twelve published names",
          ids == "run pause step_into step_over step_out step_back frame_back "
                 "run_to_cursor run_to_eof run_to_eosl trace_toggle trace_export ",
          ids);
}

static void test_debug_keys_grammar() {
    set_group("DK");
    using namespace jnext::dbgkeys;

    Combo c;
    std::string why;

    check("DK-10", "a bare function key parses",
          parse_combo("F7", c, why) && c.mods == MOD_NONE && c.key == Key::F7, why);

    check("DK-11", "modifiers parse in any case and with stray spaces",
          parse_combo("  cTRl + shift + f10 ", c, why)
              && c.mods == (MOD_CTRL | MOD_SHIFT) && c.key == Key::F10, why);

    check("DK-12", "rendering is canonical regardless of how it was typed",
          parse_combo("shift+ctrl+F10", c, why) && render_combo(c) == "Ctrl+Shift+F10",
          render_combo(c));

    check("DK-13", "'none' and the empty string both mean unbound",
          parse_combo("none", c, why) && !c.bound()
              && parse_combo("", c, why) && !c.bound(), why);

    check("DK-14", "an unbound combination renders as 'none'",
          render_combo(Combo{}) == "none", render_combo(Combo{}));

    check("DK-15", "a key outside the vocabulary is refused BY NAME",
          !parse_combo("F13", c, why) && why.find("F13") != std::string::npos, why);

    check("DK-16", "an unknown modifier is refused by name",
          !parse_combo("Hyper+F5", c, why) && why.find("Hyper") != std::string::npos, why);

    check("DK-17", "a trailing '+' with no key is refused",
          !parse_combo("Ctrl+", c, why), why);

    // Every accepted text renders to something that parses back to the same
    // combination — the property the config file's round-trip depends on.
    bool stable = true;
    std::string bad;
    int probed = 0;
    for (int k = 1; k <= static_cast<int>(Key::Right) && stable; ++k) {
        for (uint8_t m = 0; m < 16 && stable; ++m) {
            const Combo probe{m, static_cast<Key>(k)};
            Combo back;
            ++probed;
            if (!parse_combo(render_combo(probe), back, why) || !(back == probe)) {
                stable = false;
                bad = render_combo(probe);
            }
        }
    }
    check("DK-18", "render -> parse is the identity for every combination",
          stable && probed == static_cast<int>(Key::Right) * 16,
          bad.empty() ? ("probed " + std::to_string(probed)) : bad);
}

static void test_debug_keys_validation() {
    set_group("DK");
    using namespace jnext::dbgkeys;

    Combo c;
    std::string why;

    // A window-wide bare letter is consumed by Qt's shortcut map before the
    // focused panel sees it — and the memory panel types hex with bare 0-9/A-F.
    check("DK-20", "a bare letter is refused as a binding",
          parse_combo("K", c, why) && !validate_combo(c, why), why);
    check("DK-21", "a bare arrow / Home / Return is refused as a binding",
          parse_combo("Home", c, why) && !validate_combo(c, why), why);
    check("DK-22", "Shift alone does not make a letter bindable",
          parse_combo("Shift+K", c, why) && !validate_combo(c, why), why);
    check("DK-23", "Ctrl+letter IS allowed (the debugger never feeds the guest)",
          parse_combo("Ctrl+K", c, why) && validate_combo(c, why), why);
    check("DK-24", "a bare function key is allowed",
          parse_combo("F7", c, why) && validate_combo(c, why), why);
    check("DK-25", "Shift + a function key is allowed",
          parse_combo("Shift+F7", c, why) && validate_combo(c, why), why);
    check("DK-26", "F11 is allowed — a separate window has its own shortcut map",
          parse_combo("F11", c, why) && validate_combo(c, why), why);
    check("DK-27", "Alt+letter is refused — the debugger's menu bar owns it",
          parse_combo("Alt+D", c, why) && !validate_combo(c, why), why);
    check("DK-28", "Alt + a function key is allowed",
          parse_combo("Alt+F5", c, why) && validate_combo(c, why), why);
    check("DK-29", "Ctrl+C is refused — the disassembly panel's Copy",
          parse_combo("Ctrl+C", c, why) && !validate_combo(c, why), why);
    check("DK-30", "Ctrl+A is refused — the disassembly panel's Select All",
          parse_combo("Ctrl+A", c, why) && !validate_combo(c, why), why);
    check("DK-31", "unbound is always a legal state",
          validate_combo(Combo{}, why), why);
}

static void test_debug_keys_roundtrip(QTemporaryDir& dir) {
    set_group("DK");
    using namespace jnext::dbgkeys;

    const QString path = fresh_ini_path(dir, "dk_roundtrip");
    {
        AppConfig cfg(path);
        cfg.load();
        Combo c; std::string why;
        parse_combo("F10", c, why);   cfg.data().debug_keys.set(Action::StepOver, c);
        parse_combo("F11", c, why);   cfg.data().debug_keys.set(Action::StepInto, c);
        parse_combo("Ctrl+F10", c, why);
        cfg.data().debug_keys.set(Action::RunToCursor, c);
        cfg.save();
    }

    AppConfig reloaded(path);
    reloaded.load();
    const Keymap& km = reloaded.data().debug_keys;
    check("DK-40", "a redefinition survives save -> load",
          render_combo(km.combo(Action::StepOver)) == "F10"
              && render_combo(km.combo(Action::StepInto)) == "F11",
          render_combo(km.combo(Action::StepOver)) + "/"
              + render_combo(km.combo(Action::StepInto)));

    check("DK-41", "binding a default-unbound action survives too",
          render_combo(km.combo(Action::RunToCursor)) == "Ctrl+F10",
          render_combo(km.combo(Action::RunToCursor)));

    check("DK-42", "the untouched actions are still at their defaults",
          km.is_default(Action::Run) && km.is_default(Action::Pause)
              && km.is_default(Action::TraceToggle),
          render_combo(km.combo(Action::Run)));

    check("DK-43", "a clean reload reports no problems",
          reloaded.debug_key_issues().empty(),
          std::to_string(reloaded.debug_key_issues().size()) + " issues");
}

static void test_debug_keys_only_redefinitions(QTemporaryDir& dir) {
    set_group("DK");
    using namespace jnext::dbgkeys;

    // A file written with every action at its default must carry NO
    // [debugger_keys] entry at all. That is what lets this project change a
    // default later and have it reach a user who never overrode one.
    const QString all_default = fresh_ini_path(dir, "dk_alldefault");
    {
        AppConfig cfg(all_default);
        cfg.load();
        cfg.save();
    }
    {
        QSettings raw(all_default, QSettings::IniFormat);
        raw.beginGroup("debugger_keys");
        const QStringList keys = raw.childKeys();
        raw.endGroup();
        check("DK-50", "an all-default keymap writes no [debugger_keys] entries",
              keys.isEmpty(), keys.join(QStringLiteral(",")).toStdString());
    }

    const QString one = fresh_ini_path(dir, "dk_one");
    {
        AppConfig cfg(one);
        cfg.load();
        Combo c; std::string why;
        parse_combo("F10", c, why);
        cfg.data().debug_keys.set(Action::StepOver, c);
        cfg.save();
    }
    {
        QSettings raw(one, QSettings::IniFormat);
        raw.beginGroup("debugger_keys");
        const QStringList keys = raw.childKeys();
        const QString val = raw.value("step_over").toString();
        raw.endGroup();
        check("DK-51", "exactly the one redefined action is written",
              keys.size() == 1 && keys.first() == QStringLiteral("step_over")
                  && val == QStringLiteral("F10"),
              keys.join(QStringLiteral(",")).toStdString() + " -> " + val.toStdString());
    }

    // Resetting it back must REMOVE the line, not leave a stale one. The group
    // is cleared before each write for exactly this.
    {
        AppConfig cfg(one);
        cfg.load();
        cfg.data().debug_keys.reset(Action::StepOver);
        cfg.save();
    }
    {
        QSettings raw(one, QSettings::IniFormat);
        raw.beginGroup("debugger_keys");
        const QStringList keys = raw.childKeys();
        raw.endGroup();
        check("DK-52", "resetting an action removes its line from the file",
              keys.isEmpty(), keys.join(QStringLiteral(",")).toStdString());
    }

    // reset_all() is the "Reset All to Defaults" button's whole implementation.
    {
        Keymap km;
        Combo c; std::string why;
        parse_combo("Ctrl+F12", c, why);
        km.set(Action::Run, c);
        km.set(Action::TraceExport, c);   // conflicting on purpose; reset clears both
        km.reset_all();
        bool all_def = true;
        for (int i = 0; i < ACTION_COUNT; ++i)
            if (!km.is_default(static_cast<Action>(i))) all_def = false;
        check("DK-53", "reset_all() restores every action", all_def, "");
    }
}

static void test_debug_keys_bad_entries(QTemporaryDir& dir) {
    set_group("DK");
    using namespace jnext::dbgkeys;

    const QString path = fresh_ini_path(dir, "dk_bad");
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.beginGroup("debugger_keys");
        raw.setValue("step_over",  "F13");        // unparseable
        raw.setValue("step_out",   "K");          // parses, illegal as a binding
        raw.setValue("step_ovr",   "F10");        // unknown action id (a typo)
        raw.setValue("trace_toggle", "Ctrl+C");   // reserved
        raw.endGroup();
        raw.sync();
    }

    AppConfig cfg(path);
    cfg.load();
    const Keymap& km = cfg.data().debug_keys;
    const auto& issues = cfg.debug_key_issues();

    check("DK-60", "an unparseable value keeps the default",
          render_combo(km.combo(Action::StepOver)) == "F7",
          render_combo(km.combo(Action::StepOver)));
    check("DK-61", "an illegal binding keeps the default",
          render_combo(km.combo(Action::StepOut)) == "F8",
          render_combo(km.combo(Action::StepOut)));
    check("DK-62", "a reserved chord keeps the default",
          render_combo(km.combo(Action::TraceToggle)) == "F2",
          render_combo(km.combo(Action::TraceToggle)));

    check("DK-63", "every bad entry is REPORTED, none swallowed",
          issues.size() == 4, std::to_string(issues.size()) + " issues");

    bool named_all = true;
    std::string missing;
    for (const char* id : {"step_over", "step_out", "step_ovr", "trace_toggle"}) {
        bool found = false;
        for (const auto& i : issues) if (i.action_id == id) found = true;
        if (!found) { named_all = false; missing += std::string(id) + " "; }
    }
    check("DK-64", "each report names the entry it refused", named_all, missing);

    // The unknown id is kept, not deleted: it may be a NEWER jnext's binding,
    // and an older jnext must not be destructive. It is reported every load,
    // which is what stops a real typo from hiding.
    check("DK-65", "an unknown action id is preserved verbatim",
          km.unknown_entries().size() == 1
              && km.unknown_entries()[0].first == "step_ovr"
              && km.unknown_entries()[0].second == "F10",
          std::to_string(km.unknown_entries().size()));

    cfg.save();
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.beginGroup("debugger_keys");
        const QString kept = raw.value("step_ovr").toString();
        const QStringList keys = raw.childKeys();
        raw.endGroup();
        check("DK-66", "a preserved unknown id is written back on save",
              kept == QStringLiteral("F10"), kept.toStdString());
        check("DK-67", "the refused entries are NOT written back as overrides",
              !keys.contains(QStringLiteral("step_over"))
                  && !keys.contains(QStringLiteral("step_out"))
                  && !keys.contains(QStringLiteral("trace_toggle")),
              keys.join(QStringLiteral(",")).toStdString());
    }
}

static void test_debug_keys_conflicts() {
    set_group("DK");
    using namespace jnext::dbgkeys;

    // Qt makes two identical sequences AMBIGUOUS and fires them round-robin, so
    // a clash breaks BOTH bindings (GH #124). The UI refuses one outright; a
    // hand-edited file is resolved here, deterministically and loudly.
    {
        std::vector<LoadIssue> issues;
        // step_into asks for F5, which is run's DEFAULT: the explicit entry wins.
        Keymap km = build_keymap({{"step_into", "F5"}}, issues);
        check("DK-70", "an explicit override beats a colliding default",
              render_combo(km.combo(Action::StepInto)) == "F5"
                  && !km.combo(Action::Run).bound(),
              render_combo(km.combo(Action::StepInto)) + "/"
                  + render_combo(km.combo(Action::Run)));
        check("DK-71", "the displaced action is left UNBOUND, not re-defaulted",
              !km.combo(Action::Run).bound(), render_combo(km.combo(Action::Run)));
        check("DK-72", "the collision is reported, naming both actions",
              issues.size() == 1
                  && issues[0].action_id == "run"
                  && issues[0].reason.find("step_into") != std::string::npos,
              issues.empty() ? "no issue" : issues[0].reason);
    }
    {
        // Two explicit overrides on one chord: the earlier action wins.
        std::vector<LoadIssue> issues;
        Keymap km = build_keymap({{"step_over", "Ctrl+F1"}, {"step_out", "Ctrl+F1"}},
                                 issues);
        check("DK-73", "between two explicit overrides the earlier action wins",
              render_combo(km.combo(Action::StepOver)) == "Ctrl+F1"
                  && !km.combo(Action::StepOut).bound(),
              render_combo(km.combo(Action::StepOver)) + "/"
                  + render_combo(km.combo(Action::StepOut)));
        check("DK-74", "that collision is reported too", issues.size() == 1,
              std::to_string(issues.size()));
    }
    {
        // A SWAP is not a conflict and must survive intact — the single most
        // likely thing a user does with this feature.
        std::vector<LoadIssue> issues;
        Keymap km = build_keymap({{"step_into", "F7"}, {"step_over", "F6"}}, issues);
        check("DK-75", "swapping two actions' keys is accepted with no complaint",
              issues.empty()
                  && render_combo(km.combo(Action::StepInto)) == "F7"
                  && render_combo(km.combo(Action::StepOver)) == "F6",
              std::to_string(issues.size()) + " issues");
    }
    {
        // action_for() is what the Preferences tab asks before accepting a
        // capture; unbound must never "match" the three unbound actions.
        Keymap km;
        check("DK-76", "action_for() finds the owner of a bound chord",
              km.action_for(Combo{MOD_NONE, Key::F5}) != nullptr
                  && km.action_for(Combo{MOD_NONE, Key::F5})->action == Action::Run,
              "");
        check("DK-77", "action_for() never matches an unbound combination",
              km.action_for(Combo{}) == nullptr, "");
        check("DK-78", "a default keymap has no conflicts at all",
              find_conflicts(km).empty(),
              std::to_string(find_conflicts(km).size()));
    }
}

int main() {
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("FATAL: could not create a temp directory for the test\n");
        return 1;
    }

    test_defaults_no_file(dir);
    test_roundtrip(dir);
    test_partial_file(dir);
    test_malformed_values(dir);
    test_merge_precedence();
    test_default_path();
    test_debug_keys_defaults(dir);
    test_debug_keys_grammar();
    test_debug_keys_validation();
    test_debug_keys_roundtrip(dir);
    test_debug_keys_only_redefinitions(dir);
    test_debug_keys_bad_entries(dir);
    test_debug_keys_conflicts();

    std::printf("\n");
    for (const auto& r : g_results) {
        std::printf("%-6s %-16s %-6s %s\n", r.passed ? "PASS" : "FAIL",
                     r.group.c_str(), r.id.c_str(), r.desc.c_str());
    }
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
