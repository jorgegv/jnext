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

#include "platform/config_keys.h"
#include "debug/debug_keymap.h"
#include "core/cli_options.h"

#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include "../row_id.h"

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
    report_row_id(id);
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
    check("AC-87", "tape_auto_play (GH #395) defaults to true",
          d.tape_auto_play && def.tape_auto_play);
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
    // NextPi downloads ~6 GB on first use, so "no config file" must mean
    // "off"; its other fields default to empty, i.e. the built-in defaults.
    check("AC-71", "nextpi_enabled defaults to false (no NextPi download, no QEMU)",
          d.nextpi_enabled == false && def.nextpi_enabled == false);
    check("AC-72", "the nextpi dir/release/qemu_binary/audio fields default to empty (= built-in defaults)",
          d.nextpi_dir.isEmpty() && d.nextpi_release.isEmpty() &&
              d.nextpi_qemu_binary.isEmpty() && d.nextpi_audio.isEmpty());
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
        writer.data().tape_auto_play         = false;
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
        writer.data().nextpi_enabled         = true;
        writer.data().nextpi_dir             = "/home/user/nextpi";
        writer.data().nextpi_release         = "latest";
        writer.data().nextpi_qemu_binary     = "/opt/qemu/bin/qemu-system-arm";
        writer.data().nextpi_audio           = "wav:/tmp/pi.wav";
        writer.data().recent_files           = {"/srv/u/b,c.nex", "/srv/u/a.tap"};   // GH #406
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
    check("AC-88", "tape_auto_play round-trips (false survives)",
          !written.tape_auto_play && d.tape_auto_play == written.tape_auto_play);
    check("AC-89", "recent_files round-trips in order (a comma in a name included)",
          d.recent_files == written.recent_files && d.recent_files.size() == 2,
          d.recent_files.join('|').toStdString());
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
    check("AC-73", "nextpi_enabled round-trips", d.nextpi_enabled == written.nextpi_enabled);
    check("AC-74", "nextpi dir/release/qemu_binary/audio round-trip through [nextpi]",
          d.nextpi_dir == written.nextpi_dir && d.nextpi_release == written.nextpi_release &&
              d.nextpi_qemu_binary == written.nextpi_qemu_binary &&
              d.nextpi_audio == written.nextpi_audio &&
              QSettings(path, QSettings::IniFormat).value("nextpi/release").toString() ==
                  QStringLiteral("latest"));
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

// ── PENT-20 (GH #324): the Pentagon machine type persists as "pentagon" ──

static void test_pentagon_roundtrip(QTemporaryDir& dir) {
    set_group("AC-PENTAGON");

    const QString path = fresh_ini_path(dir, "pentagon");
    {
        AppConfig writer(path);
        writer.data().machine_type = MachineType::PENTAGON;
        writer.save();
    }
    QString on_disk;
    {
        QSettings raw(path, QSettings::IniFormat);
        raw.beginGroup("startup");
        on_disk = raw.value("machine_type").toString();
        raw.endGroup();
    }
    AppConfig reader(path);
    reader.load();
    check("PENT-20", "machine_type PENTAGON is written as \"pentagon\" and read back",
          on_disk == "pentagon" && reader.data().machine_type == MachineType::PENTAGON,
          on_disk.toStdString());

    // D10: older jnext wrote the +3 as "+3"; both spellings still load.
    const QString old_path = fresh_ini_path(dir, "old-plus3");
    {
        QSettings raw(old_path, QSettings::IniFormat);
        raw.beginGroup("startup");
        raw.setValue("machine_type", "+3");
        raw.endGroup();
    }
    AppConfig old_reader(old_path);
    old_reader.load();
    check("PENT-29", "an older file's machine_type=+3 still loads as the +3 (the key is now plus3)",
          old_reader.data().machine_type == MachineType::ZX_PLUS3);
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

// ── AC-90: GH #406 — [recent] files (File > Recent Files) ──────────────

static void test_recent_files(QTemporaryDir& dir) {
    set_group("AC-RECENT");
    const QString none = fresh_ini_path(dir, "recent-none");
    AppConfig absent(none);
    absent.load();
    const QString p = fresh_ini_path(dir, "recent-many");
    {
        QSettings raw(p, QSettings::IniFormat);
        QStringList many;
        for (int i = 0; i < 14; ++i) many << QString("/p/f%1.nex").arg(i);
        many.insert(3, QString());   // a blank entry in a hand-edited file
        raw.beginGroup("recent");
        raw.setValue("files", many);
        raw.endGroup();
        raw.sync();
    }
    AppConfig r(p);
    r.load();
    check("AC-90", "recent_files defaults to empty; a hand-edited list loses blanks and is cut to ten, order kept",
          absent.data().recent_files.isEmpty() && r.data().recent_files.size() == 10 &&
              r.data().recent_files[0] == "/p/f0.nex" && r.data().recent_files[3] == "/p/f3.nex" &&
              !r.data().recent_files.contains(QString()),
          r.data().recent_files.join('|').toStdString());
}

// ── AC-91..93: GH #407: [nextpi] qemu_args, and a free-text value with an unquoted comma ──

static void test_nextpi_qemu_args(QTemporaryDir& dir) {
    set_group("AC-QEMUARGS");
    const QString none = fresh_ini_path(dir, "qemuargs-none");
    AppConfig absent(none);
    absent.load();
    check("AC-91", "nextpi_qemu_args defaults to empty (no extra QEMU arguments), in a fresh struct and after load()",
          AppConfigData{}.nextpi_qemu_args.isEmpty() && absent.data().nextpi_qemu_args.isEmpty());

    const QString p = fresh_ini_path(dir, "qemuargs-rt");
    const QString text = QStringLiteral("-serial telnet::2357,server,nowait -name \"my pi\" 'x' -drive file=C:\\pi\\x.img;y");
    { AppConfig w(p); w.data().nextpi_qemu_args = text; w.save(); }
    AppConfig r(p);
    r.load();
    const QString in_file = QSettings(p, QSettings::IniFormat).value("nextpi/qemu_args").toString();
    check("AC-92", "nextpi_qemu_args round-trips byte for byte (commas, spaces, both quotes, a backslash path and a semicolon) under [nextpi]",
          r.data().nextpi_qemu_args == text && in_file == text,
          r.data().nextpi_qemu_args.toStdString() + " | " + in_file.toStdString());

    // A hand-written file, with no QSettings involved: unquoted values holding a comma. QSettings reads each
    // as a list and toString() of it is "", so every free-text key used to come back empty.
    // Only the comma is repaired here; a backslash, a bare quote or a semicolon in a hand-edited value follow Qt's own
    // INI rules, which the man page (FILES, [nextpi]) spells out. What jnext itself writes round-trips exactly (AC-92).
    const QString h = fresh_ini_path(dir, "qemuargs-hand");
    {
        QFile f(h);
        const bool opened = f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        f.write("[paths]\n"
                "last_load_dir=/srv/a,b\n"
                "sd_card_path=/srv/c,d.img\n"
                "screenshot_dir=/srv/e,f\n"
                "[screenshot]\n"
                "quick_dir=/srv/g,h\n"
                "[input]\n"
                "joy1_device=ff0047df341200000100000000007601\n"
                "joy1_device_name=Pad, Pro\n"
                "joy2_device=ff000211785600000200000000007600\n"
                "joy2_device_name=Pad, Two\n"
                "[nextpi]\n"
                "dir=/srv/i,j\n"
                "release=1_93D,x\n"
                "qemu_binary=/opt/q,r/qemu\n"
                "audio=wav:/tmp/s,t.wav\n"
                "qemu_args=-serial telnet::2357,server,nowait\n");
        f.close();
        (void)opened;
    }
    AppConfig hr(h);
    hr.load();
    const AppConfigData& d = hr.data();
    struct Want { const char* key; QString got; QString want; };
    const Want wants[] = {
        {"last_load_dir", d.last_load_dir, "/srv/a,b"},
        {"sd_card_path", d.sd_card_path, "/srv/c,d.img"},
        {"screenshot_dir", d.screenshot_dir, "/srv/e,f"},
        {"quick_dir", d.quick_screenshot_dir, "/srv/g,h"},
        {"joy1_device_name", d.joy_device_name[0], "Pad,Pro"},   // Qt's parser drops the space beside the comma
        {"joy2_device_name", d.joy_device_name[1], "Pad,Two"},
        {"nextpi dir", d.nextpi_dir, "/srv/i,j"},
        {"release", d.nextpi_release, "1_93D,x"},
        {"qemu_binary", d.nextpi_qemu_binary, "/opt/q,r/qemu"},
        {"audio", d.nextpi_audio, "wav:/tmp/s,t.wav"},
        {"qemu_args", d.nextpi_qemu_args, "-serial telnet::2357,server,nowait"},
    };
    std::string bad;
    for (const Want& w : wants)
        if (w.got != w.want) bad += std::string(w.key) + "='" + w.got.toStdString() + "' ";
    check("AC-93", "every free-text key (paths, quick_dir, joystick names, the nextpi fields) reads an UNQUOTED hand-edited "
          "value holding a comma whole, instead of silently dropping it",
          bad.empty(), bad);
}

// ── AC-80..83: GH #321 — [snapshot] quick_keep (Quick Save retention) ────

static void test_quick_keep(QTemporaryDir& dir) {
    set_group("AC-QUICKKEEP");

    const QString none = fresh_ini_path(dir, "quickkeep-none");
    AppConfig absent(none);
    absent.load();
    check("AC-80", "quick_snapshot_keep defaults to 20 with no file",
          absent.data().quick_snapshot_keep == 20,
          std::to_string(absent.data().quick_snapshot_keep));

    int back_n = -1, back_unl = -1;
    {
        const QString p = fresh_ini_path(dir, "quickkeep-n");
        { AppConfig w(p); w.data().quick_snapshot_keep = 7; w.save(); }
        AppConfig r(p); r.load();
        back_n = r.data().quick_snapshot_keep;
        const QString q = fresh_ini_path(dir, "quickkeep-unl");
        { AppConfig w(q); w.data().quick_snapshot_keep = 0; w.save(); }
        AppConfig r2(q); r2.load();
        back_unl = r2.data().quick_snapshot_keep;
        // The file spells it: a number, or the word `unlimited`.
        QSettings raw(q, QSettings::IniFormat);
        raw.beginGroup("snapshot");
        check("AC-81", "quick_keep round-trips as a number and as the word unlimited (0)",
              back_n == 7 && back_unl == 0 &&
                  raw.value("quick_keep").toString() == QStringLiteral("unlimited"),
              std::to_string(back_n) + "/" + std::to_string(back_unl));
    }
    {
        // A hand-edited word hand-written in [snapshot] is honoured; junk keeps
        // the default, so a typo cannot silently turn pruning off.
        const QString p = fresh_ini_path(dir, "quickkeep-hand");
        { QSettings raw(p, QSettings::IniFormat); raw.beginGroup("snapshot");
          raw.setValue("quick_keep", " Unlimited "); raw.endGroup(); raw.sync(); }
        AppConfig a(p); a.load();
        bool junk_ok = true;
        for (const char* bad : {"0", "-3", "many", "5x", "1000001", ""}) {
            const QString q = fresh_ini_path(dir, (std::string("quickkeep-bad-") + bad).c_str());
            { QSettings raw(q, QSettings::IniFormat); raw.beginGroup("snapshot");
              raw.setValue("quick_keep", bad); raw.endGroup(); raw.sync(); }
            AppConfig b(q); b.load();
            if (b.data().quick_snapshot_keep != 20) junk_ok = false;
        }
        check("AC-82", "the word unlimited (any case, padded) means 0; 0, negatives, junk and "
                       "out-of-range keep the default of 20",
              a.data().quick_snapshot_keep == 0 && junk_ok);
    }
    check("AC-83", "an explicit --quick-snapshot-keep beats the saved setting, and the saved "
                   "setting applies when the flag is absent",
          merge_cli_precedence(true, 3, 20) == 3 && merge_cli_precedence(false, 3, 20) == 20 &&
              merge_cli_precedence(true, 0, 20) == 0);
}

// ── AC-84..86: GH #365 — [input] mouse_release (captured-mouse release gesture) ──

static void test_mouse_release(QTemporaryDir& dir) {
    set_group("AC-MOUSEREL");
    using RM = pointer_capture::ReleaseMethod;

    {
        const QString none = fresh_ini_path(dir, "mouserel-none");
        AppConfig absent(none);
        absent.load();
        check("AC-84", "mouse_release defaults to ctrl-alt, in the struct and from an empty file",
              AppConfigData{}.mouse_release == RM::CtrlAlt && absent.data().mouse_release == RM::CtrlAlt);
    }
    const QString pe = fresh_ini_path(dir, "mouserel-esc");
    const QString pm = fresh_ini_path(dir, "mouserel-mid");
    RM back_e = RM::CtrlAlt, back_m = RM::CtrlAlt;
    { AppConfig w(pe); w.data().mouse_release = RM::Esc; w.save(); }
    { AppConfig w(pm); w.data().mouse_release = RM::MiddleClick; w.save(); }
    { AppConfig r(pe); r.load(); back_e = r.data().mouse_release; }
    { AppConfig r(pm); r.load(); back_m = r.data().mouse_release; }
    check("AC-85a", "esc round-trips through save/load", back_e == RM::Esc);
    check("AC-85b", "middle-click round-trips through save/load", back_m == RM::MiddleClick);
    {
        QSettings raw(pe, QSettings::IniFormat);
        raw.beginGroup("input");
        check("AC-85c", "the file spells it mouse_release=esc under [input]",
              raw.value("mouse_release").toString() == QStringLiteral("esc"));
    }
    {
        bool bad_ok = true;
        for (const char* junk : {"middle", "escape", "ctrl+alt", ""}) {
            const QString p = fresh_ini_path(dir, "mouserel-junk");
            { QSettings raw(p, QSettings::IniFormat); raw.beginGroup("input");
              raw.setValue("mouse_release", QString::fromLatin1(junk)); raw.endGroup(); raw.sync(); }
            AppConfig a(p); a.load();
            if (a.data().mouse_release != RM::CtrlAlt) bad_ok = false;
        }
        check("AC-86a", "unknown saved words load as the default ctrl-alt", bad_ok);
    }
    {
        const QString p = fresh_ini_path(dir, "mouserel-case");
        { QSettings raw(p, QSettings::IniFormat); raw.beginGroup("input");
          raw.setValue("mouse_release", " ESC "); raw.endGroup(); raw.sync(); }
        AppConfig a(p); a.load();
        check("AC-86b", "a padded, upper-case ESC loads as Esc", a.data().mouse_release == RM::Esc);
    }
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

    // Parsed on its own statement, never inside the check() call: check()'s
    // arguments are unsequenced, so a detail string computed from a combo the
    // CONDITION fills can be printed stale. The verdicts were never affected;
    // the diagnostics were, and a misleading failure message is a bug in a
    // test.
    struct P { bool ok; Combo c; std::string why; };
    auto P_ = [](const char* text) {
        P r; r.ok = parse_combo(text, r.c, r.why); return r;
    };

    {
        const P r = P_("F7");
        check("DK-10", "a bare function key parses",
              r.ok && r.c.mods == MOD_NONE && r.c.key == Key::F7, r.why);
    }
    {
        const P r = P_("  cTRl + shift + f10 ");
        check("DK-11", "modifiers parse in any case and with stray spaces",
              r.ok && r.c.mods == (MOD_CTRL | MOD_SHIFT) && r.c.key == Key::F10, r.why);
    }
    // Both halves matter. DK-12 pins that the order is NORMALISED at all;
    // DK-19 pins WHICH order, with all four modifiers present — without it,
    // swapping two of them inside render_combo() is invisible, because no
    // other row uses a combination carrying more than two.
    {
        const P r = P_("shift+ctrl+F10");
        check("DK-12", "rendering is canonical regardless of how it was typed",
              r.ok && render_combo(r.c) == "Ctrl+Shift+F10", render_combo(r.c));
    }
    {
        const P a = P_("none");
        const P b = P_("");
        check("DK-13", "'none' and the empty string both mean unbound",
              a.ok && !a.c.bound() && b.ok && !b.c.bound(), a.why + b.why);
    }
    check("DK-14", "an unbound combination renders as 'none'",
          render_combo(Combo{}) == "none", render_combo(Combo{}));
    {
        const P r = P_("F13");
        check("DK-15", "a key outside the vocabulary is refused BY NAME",
              !r.ok && r.why.find("F13") != std::string::npos, r.why);
    }
    {
        const P r = P_("Hyper+F5");
        check("DK-16", "an unknown modifier is refused by name",
              !r.ok && r.why.find("Hyper") != std::string::npos, r.why);
    }
    {
        const P r = P_("Ctrl+");
        check("DK-17", "a trailing '+' with no key is refused", !r.ok, r.why);
    }

    // Every accepted text renders to something that parses back to the same
    // combination — the property the config file's round-trip depends on.
    {
        bool stable = true;
        std::string bad;
        int probed = 0;
        for (int k = 1; k <= static_cast<int>(Key::Right) && stable; ++k) {
            for (uint8_t m = 0; m < 16 && stable; ++m) {
                const Combo probe{m, static_cast<Key>(k)};
                Combo back;
                std::string why;
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
    {
        const P r = P_("meta+shift+alt+ctrl+F1");
        check("DK-19", "the modifier order is Ctrl, Alt, Shift, Meta",
              r.ok && render_combo(r.c) == "Ctrl+Alt+Shift+Meta+F1", render_combo(r.c));
    }
}

static void test_debug_keys_validation() {
    set_group("DK");
    using namespace jnext::dbgkeys;

    // Same shape as above: parse, then validate, then check — so the reason
    // printed on a failure is the reason for THIS row.
    struct V { bool parsed; bool legal; std::string why; };
    auto V_ = [](const char* text) {
        V r;
        Combo c;
        r.parsed = parse_combo(text, c, r.why);
        r.legal  = r.parsed && validate_combo(c, r.why);
        return r;
    };

    struct Row { const char* id; const char* text; bool want_legal; const char* desc; };
    const Row rows[] = {
        { "DK-20", "K",        false, "a bare letter is refused as a binding" },
        { "DK-21", "Home",     false, "a bare arrow / Home / Return is refused as a binding" },
        { "DK-22", "Shift+K",  false, "Shift alone does not make a letter bindable" },
        { "DK-23", "Ctrl+K",   true,  "Ctrl+letter IS allowed (the debugger never feeds the guest)" },
        { "DK-24", "F7",       true,  "a bare function key is allowed" },
        { "DK-25", "Shift+F7", true,  "Shift + a function key is allowed" },
        { "DK-26", "F11",      true,  "F11 is allowed — a separate window has its own shortcut map" },
        { "DK-27", "Alt+D",    false, "Alt+letter is refused — the debugger's menu bar owns it" },
        { "DK-28", "Alt+F5",   true,  "Alt + a function key is allowed" },
        { "DK-29", "Ctrl+C",   false, "Ctrl+C is refused — the disassembly panel's Copy" },
        { "DK-30", "Ctrl+A",   false, "Ctrl+A is refused — the disassembly panel's Select All" },
        // GH #26 WP5 — Alt+1..Alt+8 are the script host keys in both windows.
        { "DK-32", "Alt+1",    false, "Alt+1 is refused — a script host key" },
        { "DK-33", "Alt+8",    false, "Alt+8 is refused — a script host key" },
        { "DK-34", "Alt+9",    true,  "Alt+9 stays bindable — the reservation is 1..8" },
        { "DK-35", "Alt+Shift+1", true, "Alt+Shift+1 stays bindable — a different chord" },
        // GH #321 — Alt+Shift+F5 / Alt+Shift+F9 are Quick Save / Quick Load.
        { "DK-36", "Alt+Shift+F5", false, "Alt+Shift+F5 is refused — Quick Save Snapshot" },
        { "DK-37", "Alt+Shift+F9", false, "Alt+Shift+F9 is refused — Quick Load Snapshot" },
        { "DK-38", "Alt+Shift+F6", true,  "Alt+Shift+F6 stays bindable — only F5 and F9 are reserved" },
    };
    for (const Row& r : rows) {
        const V v = V_(r.text);
        // `parsed` is part of every condition on purpose: a row whose text
        // stopped parsing would otherwise "pass" its refusal for the wrong
        // reason.
        check(r.id, r.desc, v.parsed && v.legal == r.want_legal,
              std::string(r.text) + ": " + (v.legal ? "accepted" : "refused: " + v.why));
    }

    std::string why;
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

    // GH #26 WP5 — the file-level twin of debugger_keymap_test DKSK-02 (DK-68): a
    // config that binds an action to a script host key is reported and the
    // action keeps its default.
    {
        const QString sk = fresh_ini_path(dir, "dk_scriptkey");
        {
            QSettings raw(sk, QSettings::IniFormat);
            raw.beginGroup("debugger_keys");
            raw.setValue("step_into", "Alt+1");
            raw.endGroup();
            raw.sync();
        }
        AppConfig c2(sk);
        c2.load();
        const auto& iss = c2.debug_key_issues();
        const bool named = iss.size() == 1 && iss[0].action_id == "step_into" &&
                           iss[0].reason.find("script host keys") != std::string::npos;
        check("DK-68", "a saved binding on Alt+1 (a script host key) is reported by name and the action "
                       "keeps its default",
              named && c2.data().debug_keys.is_default(Action::StepInto),
              std::to_string(iss.size()) + " issues" + (iss.empty() ? "" : ": " + iss[0].reason));
    }

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
        // GH #321 — a hand-edited file naming a quick-snapshot key is refused AT
        // LOAD, with a reason that says what the chord is, and the default stays.
        std::vector<LoadIssue> issues;
        Keymap km = build_keymap({{"step_into", "Alt+Shift+F9"}}, issues);
        check("DK-79", "a config naming Alt+Shift+F9 for a debugger action is refused at load, "
                       "naming the quick-snapshot keys, and the default stays",
              issues.size() == 1
                  && issues[0].reason.find("quick-snapshot") != std::string::npos
                  && render_combo(km.combo(Action::StepInto)) != "Alt+Shift+F9",
              issues.empty() ? "no issue" : issues[0].reason);
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

// GH #311 — the assigned controller is persisted by stable id (ACJ-*).
// Oracle: the issue ("persists in jnext.conf by a stable device identity") and
// the config-file rule that no value is written with a leading '@'. The key
// names are the plan's: joyN_device, joyN_device_name under [input].
static void test_gh311_joy_device(QTemporaryDir& dir) {
    set_group("ACJ");
    const QString G  = "ff0047df341200000100000000007601";
    const QString H  = "ff000211785600000200000000007600";
    {
        AppConfig cfg(fresh_ini_path(dir, "acj-defaults"));
        cfg.load();
        const AppConfigData& d = cfg.data();
        check("ACJ-01", "joy_device and joy_device_name default to empty on both connectors",
              d.joy_device[0].isEmpty() && d.joy_device[1].isEmpty() &&
              d.joy_device_name[0].isEmpty() && d.joy_device_name[1].isEmpty());
    }
    const QString path = fresh_ini_path(dir, "acj-roundtrip");
    {
        AppConfig w(path);
        w.data().joy_device[0]      = G + "#2";
        w.data().joy_device_name[0] = "Logitech, Inc. F310 & co";
        w.data().joy_device[1]      = H;
        w.data().joy_source[1]      = JoySource::None;
        w.data().joy_device_name[1] = "Stick";
        w.save();
        AppConfig r(path);
        r.load();
        const AppConfigData& d = r.data();
        check("ACJ-02", "device ids, names (comma and ampersand included) and source none round-trip",
              d.joy_device[0] == G + "#2" && d.joy_device_name[0] == "Logitech, Inc. F310 & co" &&
              d.joy_device[1] == H && d.joy_device_name[1] == "Stick" &&
              d.joy_source[1] == JoySource::None);
    }
    {
        QFile f(path);
        const bool opened = f.open(QIODevice::ReadOnly);
        const QByteArray text = f.readAll();
        // Only [input] is inspected: the [esp] group's own '@Invalid()' value
        // is not this feature's and is dealt with by the config fix-ups.
        const int at  = text.indexOf("\n[input]\n");
        const int end = at < 0 ? -1 : text.indexOf("\n[", at + 1);
        const QByteArray input = at < 0 ? QByteArray() : text.mid(at, end < 0 ? -1 : end - at);
        check("ACJ-03", "[input] carries a bare 'joy1_device=<guid>#2' line and no '=@' value",
              opened && !input.isEmpty() &&
              input.contains(("\njoy1_device=" + G + "#2\n").toLatin1()) &&
              !input.contains("=@"));
    }
    {
        const QString p2 = fresh_ini_path(dir, "acj-badid");
        {
            QSettings raw(p2, QSettings::IniFormat);
            raw.beginGroup("input");
            raw.setValue("joy1_device", "not-a-guid");
            raw.setValue("joy1_device_name", "Orphan");
            raw.setValue("joy2_device", G.toUpper() + "#1");
            raw.setValue("joy2_device_name", "Upper");
            raw.setValue("joy1_source", "none");
            raw.endGroup();
            raw.sync();
        }
        AppConfig r(p2);
        r.load();
        const AppConfigData& d = r.data();
        check("ACJ-04", "an invalid hand-edited id loads as empty id AND empty name",
              d.joy_device[0].isEmpty() && d.joy_device_name[0].isEmpty());
        check("ACJ-05", "an uppercase id with #1 loads as the canonical lowercase bare id",
              d.joy_device[1] == G && d.joy_device_name[1] == "Upper");
        check("ACJ-06", "joy1_source=none loads as JoySource::None",
              d.joy_source[0] == JoySource::None);
    }
}

// ── AC-FILE: GH #312 — on-disk layout of jnext.conf ────────────────────
// Oracle: the issue text (lowercase sections, no "@..." values, old files read
// correctly and rewritten once) and Qt IniFormat semantics. Legacy fixtures
// are LITERAL text written with QFile, not produced by the code under test.

static QByteArray slurp(const QString& path) {
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

static void plant(const QString& path, const char* text) {
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) f.write(text);
}

static bool has_line(const QByteArray& file, const char* line) {
    return file.split('\n').contains(QByteArray(line));
}

static const char kLegacyFile[] =
    "[General]\nconfig_version=1\nstray=x\n\n[esp]\nallowed_hosts=@Invalid()\nenabled=true\n\n"
    "[startup]\ncpu_speed=2\n";

static void test_file_layout(QTemporaryDir& dir) {
    set_group("AC-FILE");

    // Default data (empty host list) saved.
    const QString p1 = fresh_ini_path(dir, "layout_default");
    { AppConfig c(p1); c.save(); }
    const QByteArray f1 = slurp(p1);
    int bad_headers = 0, at_values = 0;
    for (const QByteArray& l : f1.split('\n')) {
        if (l.startsWith('[') && !QRegularExpression("^\\[[a-z0-9_]+\\]$")
                .match(QString::fromUtf8(l)).hasMatch()) ++bad_headers;
        if (QRegularExpression("^[^=]*=@").match(QString::fromUtf8(l)).hasMatch()) ++at_values;
    }
    check("CF-01", "every section header of a saved file is lowercase [a-z0-9_]",
          !f1.isEmpty() && bad_headers == 0, std::to_string(bad_headers));
    check("CF-02", "a saved default file has no \"=@\" value", at_values == 0,
          std::to_string(at_values));

    const QString p2 = fresh_ini_path(dir, "layout_hosts");
    { AppConfig c(p2); c.data().esp_allowed_hosts = {"nx.nxtel.org", "sync.lan"}; c.save(); }
    check("CF-03", "empty hosts are written \"allowed_hosts=\", two hosts \"a, b\"",
          has_line(f1, "allowed_hosts=")
              && has_line(slurp(p2), "allowed_hosts=nx.nxtel.org, sync.lan"));

    check("CF-04", "save() writes [config] version=1 and no [General] section",
          has_line(f1, "[config]") && has_line(f1, "version=1")
              && !f1.contains("[General]") && !f1.contains("config_version"));

    // Literal legacy file.
    const QString p5 = fresh_ini_path(dir, "layout_legacy");
    plant(p5, kLegacyFile);
    AppConfig c5(p5);
    c5.load();
    check("CF-05", "a legacy file ([General], @Invalid()) loads with the right values",
          c5.data().esp_enabled && c5.data().cpu_speed == static_cast<CpuSpeed>(2)
              && c5.data().esp_allowed_hosts.empty());
    const QByteArray f6 = slurp(p5);
    check("CF-06", "load() rewrites that file once: [config], no [General], no =@, values kept",
          !f6.contains("[General]") && !f6.contains("=@") && has_line(f6, "[config]")
              && has_line(f6, "version=1") && has_line(f6, "stray=x")
              && has_line(f6, "allowed_hosts=") && has_line(f6, "cpu_speed=2")
              && has_line(f6, "enabled=true"));

    // Idempotence: a new-format file is neither rewritten nor touched.
    const QString p7 = fresh_ini_path(dir, "layout_idem");
    { AppConfig c(p7); c.save(); }
    const QDateTime past = QDateTime::fromSecsSinceEpoch(1000000000);
    bool touched = false;
    { QFile f(p7); touched = f.open(QIODevice::ReadWrite)
                              && f.setFileTime(past, QFileDevice::FileModificationTime); }
    const QByteArray before = slurp(p7);
    { AppConfig c(p7); c.load(); }
    check("CF-07", "load() of a new-format file leaves its bytes and mtime unchanged",
          touched && slurp(p7) == before && QFileInfo(p7).lastModified() == past,
          touched ? std::string() : std::string("touched=0 (test setup could not set the mtime)"));

    // A hand-edited rejected value survives the legacy rewrite.
    const QString p8 = fresh_ini_path(dir, "layout_banana");
    plant(p8, "[General]\nconfig_version=1\n\n[startup]\ncpu_speed=banana\n");
    { AppConfig c(p8); c.load(); }
    check("CF-08", "a rejected hand-edited value (cpu_speed=banana) is still in the file",
          has_line(slurp(p8), "cpu_speed=banana"));

    const QString p9 = fresh_ini_path(dir, "layout_nonempty");
    plant(p9, "[esp]\nallowed_hosts=a.example, b.example\n");
    AppConfig c9(p9);
    c9.load();
    check("CF-09", "a non-empty legacy host list loads and its line is unchanged",
          c9.data().esp_allowed_hosts.size() == 2
              && has_line(slurp(p9), "allowed_hosts=a.example, b.example"));

    // The debugger writes [debugger] into the same file; save() must keep it.
    const QString p10 = fresh_ini_path(dir, "layout_shared");
    AppConfig c10(p10);
    c10.load();
    {
        QSettings raw(p10, QSettings::IniFormat);
        raw.setValue("debugger/size", QStringList{"640", "456"});
        raw.sync();
    }
    c10.save();
    const QByteArray f10 = slurp(p10);
    check("CF-10", "save() after a debugger write keeps [debugger] size=640, 456",
          has_line(f10, "[debugger]") && has_line(f10, "size=640, 456"));

    // Gains must be written as TEXT on every Qt: Qt 5.15 turns a float
    // QVariant into "@Variant(...)". 1.2345679f is written by Qt 6 as the
    // shortest round-trip "1.2345679" if a float QVariant reaches QSettings,
    // and as "1.23457" through gain_text(), so this row also fails on Qt 6.
    const QString p11 = fresh_ini_path(dir, "layout_gain");
    // All six gain keys get a value with more than 6 significant digits, so
    // reverting ANY one of the six writes to a raw float is caught.
    {
        AppConfig c(p11);
        c.data().audio_gain_db = 1.2345679f;
        c.data().audio_gain_beeper_db = 2.3456789f;
        c.data().audio_gain_ay_db[0] = 3.4567891f;
        c.data().audio_gain_ay_db[1] = 4.5678912f;
        c.data().audio_gain_ay_db[2] = -5.6789123f;
        c.data().audio_gain_dac_db = -6.7891234f;
        c.save();
    }
    const QByteArray f11 = slurp(p11);
    check("CF-11", "all six gains are written as plain text with 6 significant digits, never a float QVariant",
          has_line(f11, "gain_db=1.23457") && has_line(f11, "gain_beeper_db=2.34568")
              && has_line(f11, "gain_ay0_db=3.45679") && has_line(f11, "gain_ay1_db=4.56789")
              && has_line(f11, "gain_ay2_db=-5.67891") && has_line(f11, "gain_dac_db=-6.78912")
              && !f11.contains("@"),
          f11.toStdString());

    // A Qt5-written gain ("@Variant(float 1.5)") is read and rewritten as text.
    const QString p12 = fresh_ini_path(dir, "layout_gain_legacy");
    plant(p12, "[audio]\ngain_db=@Variant(\\0\\0\\0\\x87?\\xc0\\0\\0)\n");
    AppConfig c12(p12);
    c12.load();
    check("CF-12", "a legacy @Variant float gain loads as 1.5 and is rewritten as \"gain_db=1.5\"",
          c12.data().audio_gain_db == 1.5f && has_line(slurp(p12), "gain_db=1.5")
              && !slurp(p12).contains("@"),
          slurp(p12).toStdString());

    // save() on a legacy file with NO prior load() must not leave [General].
    const QString p13 = fresh_ini_path(dir, "layout_save_only");
    plant(p13, "[General]\nconfig_version=1\nstray=x\n");
    { AppConfig c(p13); c.save(); }
    const QByteArray f13 = slurp(p13);
    check("CF-13", "save() of a legacy file without load() leaves no [General] / config_version",
          !f13.contains("[General]") && !f13.contains("config_version")
              && has_line(f13, "stray=x") && has_line(f13, "version=1"),
          f13.toStdString());

    // The @Invalid() rewrite on its own: no root key to piggy-back on.
    const QString p14 = fresh_ini_path(dir, "layout_invalid_only");
    plant(p14, "[esp]\nallowed_hosts=@Invalid()\n");
    AppConfig c14(p14);   // alive while the file is read: no destructor sync
    c14.load();
    check("CF-14", "a lone allowed_hosts=@Invalid() is rewritten as \"allowed_hosts=\" by load() itself",
          has_line(slurp(p14), "allowed_hosts=") && !slurp(p14).contains("@"),
          slurp(p14).toStdString());
}

// ── AC-94..96: GH #417 - jnext.conf against the table of keys ──────────────
//
// Oracle: platform/config_keys.h and AppConfig::load()/save() themselves. No VHDL:
// a host configuration file.
//   AC-94  the enforcement row: every key save() writes is a table row, and
//          every AppConfig-owned row is written. A new persisted field under a
//          key with no row fails here.
//   AC-95  a value `--set` accepts means the same in the file: the canonical
//          text survives load() then save().
//   AC-96  a value `--set` refuses is one load() keeps at the default.

namespace {

// The raw text of `path` in the file (a list joined with ',').
std::string raw_text(const QString& file, const std::string& path, bool* present = nullptr) {
    QSettings s(file, QSettings::IniFormat);
    const QString k = QString::fromStdString(path);
    if (present) *present = s.contains(k);
    const QVariant v = s.value(k);
    if (v.userType() == QMetaType::QStringList) return v.toStringList().join(QLatin1Char(',')).toStdString();
    return v.toString().toStdString();
}

// Per-key sample values `--set` accepts, in the user's spelling.
std::vector<std::string> accepted_samples(const config_keys::Key& k) {
    using config_keys::Type;
    switch (k.type) {
        case Type::Bool:  return {"true", "false"};
        case Type::Int: {
            const long lo = static_cast<long>(k.lo), hi = static_cast<long>(k.hi);
            return {std::to_string(lo), std::to_string((lo + hi) / 2), std::to_string(hi)};
        }
        case Type::Float: return {"-24", "-1.5", "24"};
        case Type::Word:
        case Type::Machine: {
            std::vector<std::string> out;
            for (int i = 0;; ++i) {
                const std::string w(config_keys::word_at(k.values, i));
                if (w.empty()) break;
                out.push_back(w);
            }
            return out;
        }
        case Type::Text:
        case Type::Path:  return {"a b", "x, y", ""};
        case Type::TrimmedText: return {"a b", "x, y", "  padded ", ""};
        case Type::HostList: return {"a.example,B.example", ""};
        case Type::DeviceId: return {"0123456789ABCDEF0123456789abcdef#2", ""};
        case Type::Keep:  return {"unlimited", "1", "20", "1000000"};
        case Type::Combo: return {"Ctrl+Shift+F10", "none"};
    }
    return {};
}

// Per-key sample values `--set` refuses.
std::vector<std::string> refused_samples(const config_keys::Key& k) {
    using config_keys::Type;
    switch (k.type) {
        case Type::Int: {
            return {std::to_string(static_cast<long>(k.lo) - 1), std::to_string(static_cast<long>(k.hi) + 1), "x"};
        }
        case Type::Float: return {"-24.1", "25", "x"};
        case Type::Word:
        case Type::Machine: return {"nonsense"};
        case Type::Keep:  return {"0", "-1", "1000001", "x"};
        case Type::DeviceId: return {"xyz"};
        case Type::Combo: return {"Ctrl+"};
        // The file reader is more permissive than `--set` for these (a QVariant
        // bool reads 1/yes; blank hosts are dropped), and the text types take
        // anything: nothing to compare.
        case Type::Bool:
        case Type::HostList:
        case Type::Text:
        case Type::TrimmedText:
        case Type::Path: return {};
    }
    return {};
}

// "section/name" of `k`, or of its family member `family`.
std::string key_path(const config_keys::Key& k, const std::string& family = {}) {
    return std::string(k.section) + "/" + (family.empty() ? k.name : family.c_str());
}

} // namespace

static void test_config_keys(QTemporaryDir& dir) {
    set_group("AC-KEYS");
    using namespace config_keys;

    // AC-98 - normalise_legacy_layout() names the six gains from the table: EACH of them is
    // rewritten from a Qt5 float @Variant by load() alone (CF-12 pins only gain_db). One file
    // per key, because a sync rewrites every float in the file as text: with all six planted
    // together, dropping one from the list would still pass.
    {
        std::string bad;
        int n = 0;
        for (const char* k : {"gain_db", "gain_beeper_db", "gain_ay0_db", "gain_ay1_db", "gain_ay2_db", "gain_dac_db"}) {
            const QString p = fresh_ini_path(dir, (std::string("keys-98-") + k).c_str());
            plant(p, (std::string("[audio]\n") + k + "=@Variant(\\0\\0\\0\\x87?\\xc0\\0\\0)\n").c_str());
            { AppConfig c(p); c.load(); }   // no save(): load() alone rewrites the file
            const QByteArray f = slurp(p);
            ++n;
            if (!has_line(f, (std::string(k) + "=1.5").c_str()) || f.contains("@")) bad += std::string(k) + " ";
        }
        check("AC-98", "load() alone rewrites each of the six Qt5 float @Variant gains as text (one file per key)",
              bad.empty() && n == 6, bad);
    }

    // AC-99 - a root key other than config_version keeps its name, under [config].
    {
        const QString p = fresh_ini_path(dir, "keys-99");
        plant(p, "stray_key=x\n[startup]\ncpu_speed=2\n");
        { AppConfig c(p); c.load(); }
        QSettings s(p, QSettings::IniFormat);
        check("AC-99", "a legacy root key other than config_version lands at config/<key> on load()",
              s.value("config/stray_key").toString() == "x" && !s.contains("stray_key") &&
                  !s.contains("startup/stray_key"),
              slurp(p).toStdString());
    }

    // AC-94
    {
        const QString p = fresh_ini_path(dir, "keys-94");
        {
            AppConfig w(p);
            w.load();
            w.data().recent_files = QStringList{QStringLiteral("x")};
            w.data().joy_device[0] = QStringLiteral("0123456789abcdef0123456789abcdef");
            w.data().joy_device_name[0] = QStringLiteral("Pad");
            for (int i = 0; i < jnext::dbgkeys::ACTION_COUNT; ++i)
                w.data().debug_keys.set(static_cast<jnext::dbgkeys::Action>(i),
                    jnext::dbgkeys::Combo{static_cast<uint8_t>(jnext::dbgkeys::MOD_CTRL | jnext::dbgkeys::MOD_SHIFT),
                        static_cast<jnext::dbgkeys::Key>(static_cast<int>(jnext::dbgkeys::Key::F1) + i)});
            w.save();
        }
        QSet<QString> in_file;
        {
            QSettings s(p, QSettings::IniFormat);
            for (const QString& k : s.allKeys()) in_file.insert(k);
        }
        std::string bad;
        QSet<QString> expect;
        for (const Key& k : KEYS) {
            if (k.owner != Owner::AppConfig) continue;
            if (k.id == KeyId::DebuggerKey) {
                for (int i = 0; i < jnext::dbgkeys::ACTION_COUNT; ++i)
                    expect.insert(QString::fromStdString(
                        key_path(k, jnext::dbgkeys::action_table()[i].id)));
            } else {
                expect.insert(QString::fromStdString(key_path(k)));
            }
        }
        for (const QString& k : in_file)
            if (!expect.contains(k)) bad += "written but not in the table: " + k.toStdString() + "; ";
        for (const QString& k : expect)
            if (!in_file.contains(k)) bad += "in the table but not written: " + k.toStdString() + "; ";
        check("AC-94", "every key save() writes is a config_keys row owned by AppConfig, and every such row is written (the debugger_keys family with all 12 actions)",
              bad.empty() && in_file.size() == expect.size(), bad);
    }

    // AC-95 / AC-96
    std::string bad95, bad96;
    int n95 = 0;
    int idx = 0;
    // The default file's raw text, per key, for AC-96.
    const QString defp = fresh_ini_path(dir, "keys-default");
    { AppConfig d(defp); d.load(); d.save(); }

    for (const Key& k : KEYS) {
        if (k.owner != Owner::AppConfig || k.not_setting) continue;
        const bool fam = k.id == KeyId::DebuggerKey;
        const std::string fam_id = fam ? "run" : std::string();
        const std::string sec_key = std::string(k.section) + "." + (fam ? fam_id.c_str() : k.name);
        const std::string path = key_path(k, fam_id);

        for (const std::string& v : accepted_samples(k)) {
            Assignment a;
            std::string why;
            if (!parse_assignment((sec_key + "=" + v).c_str(), a, why)) {
                bad95 += sec_key + "=" + v + " refused (" + why + "); ";
                continue;
            }
            const QString p1 = fresh_ini_path(dir, ("keys-a1-" + std::to_string(++idx)).c_str());
            {
                QSettings raw(p1, QSettings::IniFormat);
                raw.setValue(QString::fromStdString(path), QString::fromStdString(v));
                raw.sync();
            }
            { AppConfig c(p1); c.load(); c.save(); }
            bool present = false;
            const std::string got = raw_text(p1, path, &present);
            ++n95;
            if (!present || got != a.text)
                bad95 += sec_key + "=" + v + " -> '" + got + "' wanted '" + a.text + "'; ";
        }

        for (const std::string& v : refused_samples(k)) {
            Assignment a;
            std::string why;
            if (parse_assignment((sec_key + "=" + v).c_str(), a, why)) {
                bad96 += sec_key + "=" + v + " accepted by --set; ";
                continue;
            }
            const QString p1 = fresh_ini_path(dir, ("keys-r1-" + std::to_string(++idx)).c_str());
            {
                QSettings raw(p1, QSettings::IniFormat);
                raw.setValue(QString::fromStdString(path), QString::fromStdString(v));
                raw.sync();
            }
            { AppConfig c(p1); c.load(); c.save(); }
            // save() rewrote p1; compare the key with the default file's.
            bool present_got = false, present_def = false;
            const std::string got = raw_text(p1, path, &present_got);
            const std::string def = raw_text(defp, path, &present_def);
            if (present_got != present_def || got != def)
                bad96 += sec_key + "=" + v + " kept '" + got + "' not the default '" + def + "'; ";
        }
    }
    check("AC-95", "every value --set accepts for an AppConfig key survives the file: load() then save() writes the canonical text",
          bad95.empty() && n95 > 100, bad95 + " samples=" + std::to_string(n95));
    check("AC-96", "every sample value --set refuses for an AppConfig key leaves load() at the default (Bool, HostList and the text types excluded: the file reader is more permissive there)",
          bad96.empty(), bad96);
}

// ── AC-97: GH #417 - merge_cli_assignments() ────────────────────────────────
// Oracle: the contract in the man page's "Settings for --set" (eight Qt-window
// keys, canonical text, later of a key wins) and Preferences' CRT pop-up, whose
// combinations --crt reproduces (preferences_dialog.cpp collect()). No VHDL.
static void test_merge_cli_assignments(QTemporaryDir& dir) {
    set_group("AC-MERGE");
    using config_keys::KeyId;
    const QString saved_path = fresh_ini_path(dir, "merge-saved");
    const QString merged_path = fresh_ini_path(dir, "merge-merged");

    AppConfigData saved;
    saved.window_scale = 3;
    saved.crt_filter = true;
    saved.crt_shader = true;
    saved.crt_shader_style = crt_shader::Style::Pc;
    saved.last_load_dir = "/saved/load";
    saved.screenshot_dir = "/saved/shots";
    saved.quick_screenshot_dir = "/saved/quick";
    saved.quick_screenshot_format = ScreenshotFormat::Png;
    saved.cpu_speed = CpuSpeed::MHZ_14;
    saved.silent = true;
    saved.esp_enabled = true;
    saved.esp_allowed_hosts = {"keep.example"};
    saved.nextpi_dir = "/saved/pi";
    saved.mouse_release = pointer_capture::ReleaseMethod::Esc;
    saved.joy_source[0] = JoySource::CursorKeys;
    saved.audio_gain_db = 3.0f;

    AppConfigData merged = saved;
    merge_cli_assignments(merged, {
        { KeyId::StartupWindowScale, {}, "2" },
        { KeyId::StartupCrtFilter, {}, "false" },
        { KeyId::StartupCrtShader, {}, "false" },
        { KeyId::StartupCrtShaderStyle, {}, "0" },
        { KeyId::PathsLastLoadDir, {}, "/cli/load" },
        { KeyId::PathsScreenshotDir, {}, "/cli/shots" },
        { KeyId::ScreenshotQuickDir, {}, "/cli/quick" },
        { KeyId::ScreenshotQuickFormat, {}, "scr" },
        // A key this function does not own, and a later value for a key already given.
        { KeyId::StartupSilent, {}, "false" },
        { KeyId::StartupWindowScale, {}, "1" },
    });
    { AppConfig a(saved_path); a.data() = saved; a.save(); }
    { AppConfig b(merged_path); b.data() = merged; b.save(); }
    QSettings sa(saved_path, QSettings::IniFormat), sb(merged_path, QSettings::IniFormat);
    QStringList differ;
    for (const QString& k : sa.allKeys())
        if (sa.value(k) != sb.value(k)) differ << k;
    differ.sort();
    const QStringList want = {"paths/last_load_dir", "paths/screenshot_dir", "screenshot/quick_dir",
                              "screenshot/quick_format", "startup/crt_filter", "startup/crt_shader",
                              "startup/crt_shader_style", "startup/window_scale"};
    const bool merge_ok =
          differ == want && merged.window_scale == 1 && merged.silent && merged.esp_enabled
              && merged.last_load_dir == QLatin1String("/cli/load")
              && merged.quick_screenshot_format == ScreenshotFormat::Scr
              && merged.crt_shader_style == crt_shader::Style::Tv;

    // --crt: the five effects give the Preferences pop-up's combinations.
    // Each effect starts from a style different from the one it must leave (a --crt that
    // forgot to set the style would otherwise pass by coincidence).
    struct Want { const char* effect; bool filter, shader; crt_shader::Style start, style; };
    const Want wants[] = {
        { "off",       false, false, crt_shader::Style::Tv, crt_shader::Style::Tv },   // style untouched
        { "scanlines", true,  false, crt_shader::Style::Lite, crt_shader::Style::Lite },
        { "soft",      false, true,  crt_shader::Style::Pc, crt_shader::Style::Lite },
        { "tv",        false, true,  crt_shader::Style::Pc, crt_shader::Style::Tv },
        { "pc",        false, true,  crt_shader::Style::Tv, crt_shader::Style::Pc },
    };
    std::string bad;
    for (const Want& w : wants) {
        cli::CrtEffect e;
        std::vector<config_keys::Assignment> list;
        AppConfigData d = saved;
        d.crt_shader_style = w.start;
        if (!cli::parse_crt_effect(w.effect, e)) { bad += std::string(w.effect) + ":unparsed "; continue; }
        config_keys::append_crt_effect(list, e.filter, e.shader, e.style);
        merge_cli_assignments(d, list);
        if (d.crt_filter != w.filter || d.crt_shader != w.shader || d.crt_shader_style != w.style)
            bad += std::string(w.effect) + " ";
    }
    cli::CrtEffect none;
    if (cli::parse_crt_effect("crt", none) || cli::parse_crt_effect("", none) || cli::parse_crt_effect("TV", none))
        bad += "accepted-bad ";
    check("AC-97", "merge_cli_assignments() sets its eight keys (the later of one key wins) and leaves every "
                   "other field at the saved value; the five --crt effects merge to the Preferences pop-up's "
                   "filter/shader/style combinations",
          merge_ok && bad.empty(), differ.join(',').toStdString() + " " + bad);
}

int main() {
    QTemporaryDir dir;
    if (!dir.isValid()) {
        std::printf("FATAL: could not create a temp directory for the test\n");
        return 1;
    }

    test_defaults_no_file(dir);
    test_roundtrip(dir);
    test_pentagon_roundtrip(dir);
    test_partial_file(dir);
    test_malformed_values(dir);
    test_merge_precedence();
    test_quick_keep(dir);
    test_recent_files(dir);
    test_nextpi_qemu_args(dir);
    test_mouse_release(dir);
    test_default_path();
    test_debug_keys_defaults(dir);
    test_debug_keys_grammar();
    test_debug_keys_validation();
    test_debug_keys_roundtrip(dir);
    test_debug_keys_only_redefinitions(dir);
    test_debug_keys_bad_entries(dir);
    test_debug_keys_conflicts();
    test_file_layout(dir);
    test_gh311_joy_device(dir);
    test_config_keys(dir);
    test_merge_cli_assignments(dir);

    std::printf("\n");
    for (const auto& r : g_results) {
        std::printf("%-6s %-16s %-6s %s\n", r.passed ? "PASS" : "FAIL",
                     r.group.c_str(), r.id.c_str(), r.desc.c_str());
    }
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);

    return g_fail > 0 ? 1 : 0;
}
