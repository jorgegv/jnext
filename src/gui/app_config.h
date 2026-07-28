#pragma once

#include <QSettings>
#include <QString>

#include <string>
#include <vector>

#include "core/emulator_config.h"

/// Persisted GUI preferences (Task 66 — Configurability).
///
/// Every field defaults to today's hardcoded emulator/GUI behaviour, so a
/// machine with no config file behaves exactly as before this task.
struct AppConfigData {
    static constexpr int CONFIG_VERSION = 1;

    // --- Startup defaults (applied when the CLI did not override them) ---
    MachineType machine_type           = MachineType::ZXN_ISSUE2;
    CpuSpeed    cpu_speed              = CpuSpeed::MHZ_3_5;
    int         emulator_speed_percent = 100;   // 10..1000, matches --speed's clamp
    int         window_scale           = 1;     // matches MainWindow's default (G104)
    bool        crt_filter             = false;
    bool        silent                 = false; // matches --silent's default
    bool        tape_fast_load         = true;  // matches the Tape menu's default

    // Host-side gain applied after the emulated hardware mix. Unlike
    // `silent`, this can be changed live from Preferences. The CLI option
    // wins for the current run when explicitly supplied.
    float       audio_gain_db          = 0.0f;  // -24..+24 dB
    float       audio_gain_beeper_db   = 0.0f;
    float       audio_gain_ay_db[3]    = {0.0f, 0.0f, 0.0f};
    float       audio_gain_dac_db      = 0.0f;

    // Task 79 — per-connector host input source (index 0 = Joy 1, 1 = Joy 2).
    // Default Sdl/Sdl reproduces the historical behaviour. Persisted as the
    // "sdl"/"keys" strings via joy_source_str()/parse_joy_source().
    JoySource   joy_source[2]          = { JoySource::Sdl, JoySource::Sdl };

    // GH #25 — the emulated ESP-01 WiFi module. Defaults match the CLI's
    // (off, no host restriction), so a machine with no config file is not on
    // the network. A user who runs NXtel every day should not have to type
    // --esp and seven --esp-allow entries every time, which is what makes this
    // worth persisting at all.
    //
    // NOTHING SETS THESE FROM THE UI TODAY: there is no Preferences page for
    // the ESP, so they are hand-edited (documented in the man page's FILES
    // section) or left alone. That is deliberate rather than unfinished — but
    // it is only acceptable because the enabled state is VISIBLE: the status
    // bar grows an ESP cell whenever the module is on, so a config file that
    // silently enables it cannot go unnoticed.
    //
    // `--no-esp` is the escape hatch: the CLI can always force it off for one
    // run, in both directions, which is why that flag exists.
    bool        esp_enabled            = false;
    std::vector<std::string> esp_allowed_hosts;   // empty = any host

    // --- Paths (remembered across sessions) ---
    QString last_load_dir;    // seeds "Load Program..." / "Open Tape File..."
    QString sd_card_path;     // default --sdcard when the CLI gives none
    QString screenshot_dir;   // seeds "Save Screenshot..."
};

/// Task 66 — CLI-vs-saved-preference precedence, one definition shared by
/// main.cpp's real merges and its unit test (test/gui/app_config_test.cpp).
/// "CLI always wins": when the command line explicitly provided a value,
/// use it; otherwise fall back to the saved preference.
template <typename T>
inline T merge_cli_precedence(bool cli_provided, const T& cli_value, const T& saved_value) {
    return cli_provided ? cli_value : saved_value;
}

/// Loads/saves AppConfigData via QSettings.
///
/// The production constructor stores an INI file at ~/.jnext/jnext.conf —
/// the same ~/.jnext home directory jnext already uses for the SD-card image
/// (~/.jnext/sdcard/...). The debugger window's geometry lives alongside it
/// at ~/.jnext/Debugger.conf (src/debugger/debugger_window.cpp), a separate
/// file so the two stay independent.
///
/// A second constructor points at an explicit INI file so unit tests never
/// touch the real user config.
class AppConfig {
public:
    AppConfig();
    explicit AppConfig(const QString& ini_path);

    /// Absolute path of the production config file (~/.jnext/jnext.conf).
    /// Exposed so it can be asserted without touching the real user config.
    static QString default_config_path();

    /// (Re)load from the backing QSettings. Missing or malformed keys fall
    /// back to the AppConfigData default for that field; a value out of its
    /// valid range is rejected the same way. Always resets to full defaults
    /// first, so a partially-corrupt file cannot leave stale data behind.
    void load();

    /// Persist the current data() to the backing QSettings.
    void save() const;

    AppConfigData&       data()       { return data_; }
    const AppConfigData& data() const { return data_; }

    /// True once load() has run and found an on-disk settings file (i.e. this
    /// is not the very first run / a fresh temp file). Informational only —
    /// nothing in AppConfig gates behaviour on it.
    bool loaded_from_existing_file() const { return loaded_from_existing_file_; }

private:
    // mutable: QSettings::setValue()/sync() are non-const, but writing the
    // backing store is not part of AppConfig's logical (AppConfigData) state,
    // so save() stays const like the rest of this read-mostly value type.
    mutable QSettings settings_;
    AppConfigData      data_;
    bool               loaded_from_existing_file_ = false;
};
