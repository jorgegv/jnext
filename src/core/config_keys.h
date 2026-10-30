#pragma once

// GH #417 - the one table of jnext.conf keys.
//
// Every INI section and key that jnext persists is a row here, and ONLY here:
// AppConfig (src/gui/app_config.cpp) and DebuggerWindow
// (src/debugger/debugger_window.cpp) read their key names, bounds and word
// lists from this table instead of spelling them as literals. A key the table
// does not know cannot be persisted without app_config_test's AC-94 failing,
// and `--set SECTION.KEY=VALUE` (stage 2) is checked against the same rows.
//
// Qt-free on purpose: the SDL-only build and `--headless` never read
// jnext.conf, yet `--set` must parse the same key names and values there.
//
// parse_assignment() turns the text a user typed into CANONICAL text: the
// spelling AppConfig::save() writes. So `--set` and the file mean one thing,
// and app_config_test (AC-95/96) holds the two readers to it.
//
// Adding a Preferences field = adding a row here (AC-94 fails otherwise).

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace config_keys {

enum class Type : uint8_t {
    Bool,         // true | false
    Int,          // whole number in [lo, hi]
    Float,        // finite number in [lo, hi]
    Word,         // one of the words in `values` (or the type's own parser)
    Text,         // verbatim
    TrimmedText,  // surrounding whitespace removed, as load() does
    Path,         // verbatim
    HostList,     // comma-separated host names; empty = the empty list
    DeviceId,     // a controller id (input/joy_source.h); empty = Automatic
    Keep,         // 1..1000000 or `unlimited`
    Combo,        // a debugger key combination (debug/debug_keymap.h grammar)
    Machine,      // a machine type word
};

/// Where a key takes effect (a bit set).
enum Frontend : uint8_t { QT = 1, SDL = 2, HEADLESS = 4 };

/// Which module reads and writes the key in jnext.conf.
enum class Owner : uint8_t { AppConfig, DebuggerWindow };

/// One id per row, in table order. A `switch` over it with no `default` fails
/// to compile (-Werror=switch) when a row is added without an arm.
enum class KeyId : uint8_t {
    StartupMachineType,
    StartupCpuSpeed,
    StartupEmulatorSpeedPercent,
    StartupWindowScale,
    StartupCrtFilter,
    StartupCrtShader,
    StartupCrtShaderStyle,
    StartupSilent,
    StartupTapeFastLoad,
    StartupTapeAutoPlay,
    StartupWhenSlowPrefer,
    AudioGainDb,
    AudioGainBeeperDb,
    AudioGainAy0Db,
    AudioGainAy1Db,
    AudioGainAy2Db,
    AudioGainDacDb,
    PathsLastLoadDir,
    PathsSdCardPath,
    PathsScreenshotDir,
    RecentFiles,
    ScreenshotQuickDir,
    ScreenshotQuickFormat,
    SnapshotQuickKeep,
    InputJoy1Source,
    InputJoy2Source,
    InputJoy1Device,
    InputJoy2Device,
    InputJoy1DeviceName,
    InputJoy2DeviceName,
    InputMouseRelease,
    DebuggerKey,            // the family `debugger_keys.<action id>`
    EspEnabled,
    EspAllowedHosts,
    NextpiEnabled,
    NextpiDir,
    NextpiRelease,
    NextpiQemuBinary,
    NextpiAudio,
    NextpiQemuArgs,
    ConfigVersion,
    DebuggerAttached,
    DebuggerSize,
    DebuggerPosition,
    Count
};

struct Key {
    KeyId       id;
    const char* section;        // the INI section, exactly as written
    const char* name;           // the INI key; the family row's is "ACTION"
    Type        type;
    double      lo, hi;         // Int / Float / Keep bounds, inclusive
    const char* values;         // metavar for --help and the man page; for a Word key
                                // also the accepted words, '|'-separated
    const char* flag;           // a dedicated option with the same effect, or nullptr
    uint8_t     frontends;      // Frontend bits: where the key takes effect
    bool        needs_debugger; // only takes effect in a build with the debugger
    Owner       owner;
    const char* not_setting;    // nullptr = settable with --set; else WHY it is not
    const char* help;           // one line
};

// Man page order. Keep `flag` to spellings that are Doc::Documented in
// cli::OPTIONS (SETK-04).
inline constexpr Key KEYS[] = {
    { KeyId::StartupMachineType, "startup", "machine_type", Type::Machine, 0, 0,
      "48k|128k|plus3|pentagon|next", "--machine", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Machine type to start" },
    { KeyId::StartupCpuSpeed, "startup", "cpu_speed", Type::Int, 0, 3,
      "0..3", nullptr, QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "CPU speed at start: 0 = 3.5, 1 = 7, 2 = 14, 3 = 28 MHz" },
    { KeyId::StartupEmulatorSpeedPercent, "startup", "emulator_speed_percent", Type::Int, 10, 1000,
      "10..1000", "--speed", QT | SDL, false, Owner::AppConfig, nullptr,
      "Emulation speed as a percentage of real time" },
    { KeyId::StartupWindowScale, "startup", "window_scale", Type::Int, 1, 3,
      "1..3", nullptr, QT, false, Owner::AppConfig, nullptr,
      "Window scale factor" },
    { KeyId::StartupCrtFilter, "startup", "crt_filter", Type::Bool, 0, 0,
      "true|false", nullptr, QT, false, Owner::AppConfig, nullptr,
      "CRT scanlines effect" },
    { KeyId::StartupCrtShader, "startup", "crt_shader", Type::Bool, 0, 0,
      "true|false", nullptr, QT, false, Owner::AppConfig, nullptr,
      "OpenGL CRT shader effect" },
    { KeyId::StartupCrtShaderStyle, "startup", "crt_shader_style", Type::Int, 0, 2,
      "0..2", nullptr, QT, false, Owner::AppConfig, nullptr,
      "CRT shader style: 0 = tv, 1 = pc, 2 = soft" },
    { KeyId::StartupSilent, "startup", "silent", Type::Bool, 0, 0,
      "true|false", "--silent", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Start with the sound muted" },
    { KeyId::StartupTapeFastLoad, "startup", "tape_fast_load", Type::Bool, 0, 0,
      "true|false", "--tape-realtime", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Tape fast-load (false = real time)" },
    { KeyId::StartupTapeAutoPlay, "startup", "tape_auto_play", Type::Bool, 0, 0,
      "true|false", "--tape-auto-play", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Automatic tape play and stop" },
    { KeyId::StartupWhenSlowPrefer, "startup", "when_slow_prefer", Type::Word, 0, 0,
      "audio|video", "--when-slow-prefer", QT | SDL, false, Owner::AppConfig, nullptr,
      "What a host too slow for real time sacrifices" },
    { KeyId::AudioGainDb, "audio", "gain_db", Type::Float, -24, 24,
      "-24..24", "--audio-gain-db", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Master gain in dB" },
    { KeyId::AudioGainBeeperDb, "audio", "gain_beeper_db", Type::Float, -24, 24,
      "-24..24", "--audio-gain-beeper-db", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Beeper gain in dB" },
    { KeyId::AudioGainAy0Db, "audio", "gain_ay0_db", Type::Float, -24, 24,
      "-24..24", "--audio-gain-ay0-db", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "AY #0 gain in dB" },
    { KeyId::AudioGainAy1Db, "audio", "gain_ay1_db", Type::Float, -24, 24,
      "-24..24", "--audio-gain-ay1-db", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "AY #1 gain in dB" },
    { KeyId::AudioGainAy2Db, "audio", "gain_ay2_db", Type::Float, -24, 24,
      "-24..24", "--audio-gain-ay2-db", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "AY #2 gain in dB" },
    { KeyId::AudioGainDacDb, "audio", "gain_dac_db", Type::Float, -24, 24,
      "-24..24", "--audio-gain-dac-db", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "DAC gain in dB" },
    { KeyId::PathsLastLoadDir, "paths", "last_load_dir", Type::Path, 0, 0,
      "PATH", nullptr, QT, false, Owner::AppConfig, nullptr,
      "Directory the Load dialogs open in" },
    { KeyId::PathsSdCardPath, "paths", "sd_card_path", Type::Path, 0, 0,
      "FILE", "--sdcard", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Default SD card image" },
    { KeyId::PathsScreenshotDir, "paths", "screenshot_dir", Type::Path, 0, 0,
      "PATH", nullptr, QT, false, Owner::AppConfig, nullptr,
      "Directory the Save Screenshot dialog opens in" },
    { KeyId::RecentFiles, "recent", "files", Type::Text, 0, 0,
      "", nullptr, QT, false, Owner::AppConfig,
      "it is File > Recent Files, a history the program keeps", "Recent files" },
    { KeyId::ScreenshotQuickDir, "screenshot", "quick_dir", Type::Path, 0, 0,
      "PATH", nullptr, QT, false, Owner::AppConfig, nullptr,
      "Quick Screenshot directory (empty = ~/.jnext/screenshots)" },
    { KeyId::ScreenshotQuickFormat, "screenshot", "quick_format", Type::Word, 0, 0,
      "png|scr", nullptr, QT, false, Owner::AppConfig, nullptr,
      "Quick Screenshot format" },
    { KeyId::SnapshotQuickKeep, "snapshot", "quick_keep", Type::Keep, 1, 1000000,
      "1..1000000|unlimited", "--quick-snapshot-keep", QT | SDL, false, Owner::AppConfig, nullptr,
      "Quick snapshots to keep" },
    { KeyId::InputJoy1Source, "input", "joy1_source", Type::Word, 0, 0,
      "sdl|keys|none", "--joy1-source", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Joystick 1 input source" },
    { KeyId::InputJoy2Source, "input", "joy2_source", Type::Word, 0, 0,
      "sdl|keys|none", "--joy2-source", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Joystick 2 input source" },
    { KeyId::InputJoy1Device, "input", "joy1_device", Type::DeviceId, 0, 0,
      "ID", "--joy1-device", QT | SDL, false, Owner::AppConfig, nullptr,
      "Joystick 1 controller (empty = Automatic)" },
    { KeyId::InputJoy2Device, "input", "joy2_device", Type::DeviceId, 0, 0,
      "ID", "--joy2-device", QT | SDL, false, Owner::AppConfig, nullptr,
      "Joystick 2 controller (empty = Automatic)" },
    { KeyId::InputJoy1DeviceName, "input", "joy1_device_name", Type::Text, 0, 0,
      "", nullptr, QT, false, Owner::AppConfig,
      "it is derived from the chosen controller", "Joystick 1 controller name" },
    { KeyId::InputJoy2DeviceName, "input", "joy2_device_name", Type::Text, 0, 0,
      "", nullptr, QT, false, Owner::AppConfig,
      "it is derived from the chosen controller", "Joystick 2 controller name" },
    { KeyId::InputMouseRelease, "input", "mouse_release", Type::Word, 0, 0,
      "ctrl-alt|middle-click|esc", "--mouse-release", QT | SDL, false, Owner::AppConfig, nullptr,
      "How a captured mouse is released" },
    { KeyId::DebuggerKey, "debugger_keys", "ACTION", Type::Combo, 0, 0,
      "COMBO", nullptr, QT, true, Owner::AppConfig, nullptr,
      "Key combination of a debugger action (run, pause, step_into, step_over, step_out, "
      "step_back, frame_back, run_to_cursor, run_to_eof, run_to_eosl, trace_toggle, "
      "trace_export); none = unbound" },
    { KeyId::EspEnabled, "esp", "enabled", Type::Bool, 0, 0,
      "true|false", "--esp", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Emulated ESP-01 WiFi module" },
    { KeyId::EspAllowedHosts, "esp", "allowed_hosts", Type::HostList, 0, 0,
      "HOST,HOST,...", "--esp-allow", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Hosts the ESP may connect to (empty = any)" },
    { KeyId::NextpiEnabled, "nextpi", "enabled", Type::Bool, 0, 0,
      "true|false", "--nextpi", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "NextPi under QEMU on UART 1" },
    { KeyId::NextpiDir, "nextpi", "dir", Type::TrimmedText, 0, 0,
      "PATH", "--nextpi-dir", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "NextPi directory (empty = <config dir>/nextpi)" },
    { KeyId::NextpiRelease, "nextpi", "release", Type::TrimmedText, 0, 0,
      "TAG", "--nextpi-release", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "NextPi release (empty = the built-in one; latest)" },
    { KeyId::NextpiQemuBinary, "nextpi", "qemu_binary", Type::TrimmedText, 0, 0,
      "FILE", "--nextpi-qemu", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "QEMU binary (empty = qemu-system-arm on PATH)" },
    { KeyId::NextpiAudio, "nextpi", "audio", Type::TrimmedText, 0, 0,
      "MODE", "--nextpi-audio", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Pi audio: empty = the Next's mixer, host, a QEMU driver, none or wav:FILE" },
    { KeyId::NextpiQemuArgs, "nextpi", "qemu_args", Type::TrimmedText, 0, 0,
      "ARGS", "--nextpi-qemu-args", QT | SDL | HEADLESS, false, Owner::AppConfig, nullptr,
      "Extra QEMU arguments" },
    { KeyId::ConfigVersion, "config", "version", Type::Int, 0, 0,
      "", nullptr, QT, false, Owner::AppConfig,
      "it is the file format version", "File format version" },
    { KeyId::DebuggerAttached, "debugger", "attached", Type::Bool, 0, 0,
      "true|false", nullptr, QT, true, Owner::DebuggerWindow, nullptr,
      "Debugger window attached to the emulator window" },
    { KeyId::DebuggerSize, "debugger", "size", Type::Text, 0, 0,
      "", nullptr, QT, true, Owner::DebuggerWindow,
      "it is the remembered window geometry; no control sets it", "Debugger window size" },
    { KeyId::DebuggerPosition, "debugger", "position", Type::Text, 0, 0,
      "", nullptr, QT, true, Owner::DebuggerWindow,
      "it is the remembered window geometry; no control sets it", "Debugger window position" },
};

inline constexpr size_t KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);
static_assert(KEY_COUNT == static_cast<size_t>(KeyId::Count),
              "config_keys::KEYS must have exactly one row per KeyId");

constexpr bool table_in_id_order() {
    for (size_t i = 0; i < KEY_COUNT; ++i)
        if (static_cast<size_t>(KEYS[i].id) != i) return false;
    return true;
}
static_assert(table_in_id_order(), "config_keys::KEYS rows must be in KeyId order");

constexpr const Key& info(KeyId id) { return KEYS[static_cast<size_t>(id)]; }
constexpr const char* section(KeyId id) { return info(id).section; }
constexpr const char* name(KeyId id)    { return info(id).name; }

/// "section/name": the QSettings path of a key (not for the family row).
inline std::string path(KeyId id) {
    return std::string(info(id).section) + "/" + info(id).name;
}

/// The row for `section`.`name`, or nullptr. `debugger_keys.<id>` resolves to
/// the family row when <id> is one of the debugger action ids.
const Key* find(std::string_view section, std::string_view name);

// --- word lists ('|'-separated, as in Key::values) --------------------------

/// The index-th word of a '|'-separated list; empty when out of range.
constexpr std::string_view word_at(std::string_view list, int index) {
    size_t begin = 0;
    for (int i = 0; i < index; ++i) {
        const size_t bar = list.find('|', begin);
        if (bar == std::string_view::npos) return {};
        begin = bar + 1;
    }
    const size_t bar = list.find('|', begin);
    return list.substr(begin, bar == std::string_view::npos ? std::string_view::npos : bar - begin);
}

/// Index of `word` (ASCII case-insensitive) in the list, or -1.
int word_index(std::string_view list, std::string_view word);

// --- the setter -------------------------------------------------------------

/// A parsed `SECTION.KEY=VALUE`. `text` is the CANONICAL value: what
/// AppConfig::save() writes for it.
struct Assignment {
    KeyId       id = KeyId::Count;
    std::string family_name;   // the action id of a `debugger_keys.<id>` entry
    std::string text;
};

/// Parse one `SECTION.KEY=VALUE`, splitting at the FIRST '='. On refusal
/// returns false, sets `why` (it names what was typed) and leaves `out`
/// untouched.
bool parse_assignment(const char* arg, Assignment& out, std::string& why);

} // namespace config_keys
