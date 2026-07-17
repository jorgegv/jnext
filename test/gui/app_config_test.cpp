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
    check("AC-46", "audio_gain_db defaults to 0 dB", d.audio_gain_db == 0.0f);
    check("AC-43", "joy_source defaults to Sdl/Sdl",
          d.joy_source[0] == JoySource::Sdl && d.joy_source[1] == JoySource::Sdl);
    check("AC-09", "last_load_dir defaults to empty", d.last_load_dir.isEmpty());
    check("AC-10", "sd_card_path defaults to empty", d.sd_card_path.isEmpty());
    check("AC-11", "screenshot_dir defaults to empty", d.screenshot_dir.isEmpty());
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
        writer.data().audio_gain_db          = -12.5f;
        writer.data().joy_source[0]          = JoySource::CursorKeys;
        writer.data().joy_source[1]          = JoySource::Sdl;
        writer.data().last_load_dir          = "/home/user/games";
        writer.data().sd_card_path           = "/home/user/sd/next.img";
        writer.data().screenshot_dir         = "/home/user/shots";
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
    check("AC-47", "audio_gain_db round-trips",
          std::abs(d.audio_gain_db - written.audio_gain_db) < 0.001f);
    check("AC-21", "last_load_dir round-trips", d.last_load_dir == written.last_load_dir);
    check("AC-22", "sd_card_path round-trips", d.sd_card_path == written.sd_card_path);
    check("AC-23", "screenshot_dir round-trips", d.screenshot_dir == written.screenshot_dir);
    check("AC-24", "loaded_from_existing_file() is true once a file was written",
          reader.loaded_from_existing_file());
    check("AC-44", "joy_source round-trips (Joy 1 keys, Joy 2 sdl)",
          d.joy_source[0] == written.joy_source[0] &&
          d.joy_source[1] == written.joy_source[1]);
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
        raw.endGroup();
        raw.beginGroup("input");
        raw.setValue("joy1_source", "not-a-source");      // parse_joy_source() rejects
        raw.endGroup();
        raw.beginGroup("audio");
        raw.setValue("gain_db", 25.0);                     // out of [-24,+24]
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
    check("AC-49", "out-of-range audio gain falls back to default",
          d.audio_gain_db == def.audio_gain_db);

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

    std::printf("\n");
    for (const auto& r : g_results) {
        std::printf("%-6s %-16s %-6s %s\n", r.passed ? "PASS" : "FAIL",
                     r.group.c_str(), r.id.c_str(), r.desc.c_str());
    }
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
