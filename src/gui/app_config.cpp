#include "gui/app_config.h"
#include "gui/qt_ctrl_is_command.h"   // GH #403
#include "gui/emulator_widget.h"
#include "platform/config_keys.h"
#include "gui/recent_files.h"
#include "peripheral/esp_host_policy.h"

#include <QDir>
#include <QFileInfo>

#include <cmath>

namespace {

using config_keys::KeyId;

// Section and key names come from the one table (GH #417).
const char* S(KeyId id) { return config_keys::section(id); }
const char* N(KeyId id) { return config_keys::name(id); }

// The table's bounds are the file reader's bounds; the ones it cannot include
// are tied to their owners here.
static_assert(config_keys::info(KeyId::StartupWindowScale).lo == EmulatorWidget::MIN_SCALE &&
              config_keys::info(KeyId::StartupWindowScale).hi == EmulatorWidget::MAX_SCALE,
              "config_keys window_scale bounds must match EmulatorWidget");
static_assert(config_keys::info(KeyId::StartupCrtShaderStyle).hi == crt_shader::STYLE_COUNT - 1,
              "config_keys crt_shader_style bound must match crt_shader::STYLE_COUNT");
static_assert(config_keys::word_at(config_keys::info(KeyId::StartupWhenSlowPrefer).values,
                  static_cast<int>(audio_pacing::WhenSlowPrefer::Audio)) == "audio" &&
              config_keys::word_at(config_keys::info(KeyId::StartupWhenSlowPrefer).values,
                  static_cast<int>(audio_pacing::WhenSlowPrefer::Video)) == "video",
              "config_keys when_slow_prefer words must follow the enum order");
static_assert(config_keys::word_at(config_keys::info(KeyId::ScreenshotQuickFormat).values,
                  static_cast<int>(ScreenshotFormat::Png)) == "png" &&
              config_keys::word_at(config_keys::info(KeyId::ScreenshotQuickFormat).values,
                  static_cast<int>(ScreenshotFormat::Scr)) == "scr",
              "config_keys quick_format words must follow the enum order");

// A free-text value read from the INI. QSettings IniFormat reads an UNQUOTED value
// containing a comma as a QStringList, and toString() of a multi-element list is "":
// a hand-edited `qemu_args=-serial telnet::2357,server,nowait` was silently dropped
// (GH #407). Join it back. A value jnext saves is quoted and arrives as a string.
QString ini_text(const QVariant& v)
{
    if (v.userType() == QMetaType::QStringList) return v.toStringList().join(QLatin1Char(','));
    return v.toString();
}

// On-disk machine type: core/emulator_config.h's machine_type_key(), the
// spelling parse_machine_type() reads back (older files wrote "+3"; both parse).
QString machine_type_to_key(MachineType t) {
    return QString::fromLatin1(machine_type_key(t));
}

// Issue #35 — on-disk spelling of the degradation policy. Deliberately the
// same two words the CLI accepts, so `--when-slow-prefer video` and
// `when_slow_prefer=video` are one vocabulary rather than two.
QString when_slow_prefer_to_key(audio_pacing::WhenSlowPrefer p) {
    const std::string_view w = config_keys::word_at(
        config_keys::info(KeyId::StartupWhenSlowPrefer).values, static_cast<int>(p));
    return QString::fromLatin1(w.data(), static_cast<qsizetype>(w.size()));
}

// GH #19 — on-disk spelling of the quick-screenshot format. The same two
// words a filename extension uses, so `.scr` and `quick_format=scr` are one
// vocabulary rather than two.
QString quick_screenshot_format_to_key(ScreenshotFormat f) {
    const std::string_view w = config_keys::word_at(
        config_keys::info(KeyId::ScreenshotQuickFormat).values, static_cast<int>(f));
    return QString::fromLatin1(w.data(), static_cast<qsizetype>(w.size()));
}

void load_gain(QSettings& settings, KeyId id, float& target)
{
    bool ok = false;
    const config_keys::Key& k = config_keys::info(id);
    const double db = settings.value(k.name, static_cast<double>(target)).toDouble(&ok);
    if (ok && std::isfinite(db) && db >= k.lo && db <= k.hi)
        target = static_cast<float>(db);
}

// GH #312 — gains are written as TEXT. QSettings' IniFormat has no plain-text
// form for a float QVariant in Qt 5.15 (it writes "@Variant(...)" there; Qt 6
// writes text), so passing the float straight through would put a binary blob
// in the file on the Qt5 builds. Six significant digits are far finer than the
// +-24 dB range needs, and load_gain() reads either form back.
QString gain_text(float db) {
    return QString::number(static_cast<double>(db), 'g', 6);
}

} // namespace

void merge_cli_assignments(AppConfigData& d, const std::vector<config_keys::Assignment>& list)
{
    for (const config_keys::Assignment& a : list) {
        const QString text = QString::fromStdString(a.text);
        switch (a.id) {
        case KeyId::StartupWindowScale:     d.window_scale = text.toInt(); break;
        case KeyId::StartupCrtFilter:       d.crt_filter = (a.text == "true"); break;
        case KeyId::StartupCrtShader:       d.crt_shader = (a.text == "true"); break;
        case KeyId::StartupCrtShaderStyle:  d.crt_shader_style = crt_shader::style_from_int(text.toInt()); break;
        case KeyId::PathsLastLoadDir:       d.last_load_dir = text; break;
        case KeyId::PathsScreenshotDir:     d.screenshot_dir = text; break;
        case KeyId::ScreenshotQuickDir:     d.quick_screenshot_dir = text; break;
        case KeyId::ScreenshotQuickFormat: {
            const int i = config_keys::word_index(config_keys::info(KeyId::ScreenshotQuickFormat).values, a.text);
            if (i >= 0) d.quick_screenshot_format = static_cast<ScreenshotFormat>(i);
            break;
        }
        default: break;   // not a Qt-window-only key: main.cpp applied it elsewhere
        }
    }
}

QString AppConfig::default_config_path() {
    // ~/.jnext by default (the same home dir jnext uses for the SD image).
    // JNEXT_CONFIG_DIR overrides the directory so automated tests
    // (test/00regression/regression.sh) can isolate the config from a
    // developer's real ~/.jnext/jnext.conf and stay deterministic.
    QString dir = qEnvironmentVariable("JNEXT_CONFIG_DIR");
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.jnext");
    return dir + QStringLiteral("/jnext.conf");
}

AppConfig::AppConfig()
    : settings_(default_config_path(), QSettings::IniFormat)
{
}

AppConfig::AppConfig(const QString& ini_path)
    : settings_(ini_path, QSettings::IniFormat)
{
}

// GH #312 — older files carry constructs QSettings writes in a form the owner
// does not want: root keys (filed under "[General]") and an empty host list
// ("@Invalid()"). Rewrite ONLY those, once; every other value is untouched
// (a hand-edited value load() rejects must survive), and an already-new file
// is not rewritten at all.
void AppConfig::normalise_legacy_layout() const {
    bool changed = false;
    const QStringList root_keys = settings_.childKeys();
    for (const QString& k : root_keys) {
        const QVariant v = settings_.value(k);
        settings_.remove(k);
        // config_version -> [config] version; any other root key keeps its name.
        // The pre-#312 root key `config_version` is the one name the table does
        // not carry: it is a legacy spelling, not a key jnext writes.
        settings_.setValue(k == QLatin1String("config_version")
                               ? QString::fromStdString(config_keys::path(KeyId::ConfigVersion))
                               : QString::fromLatin1(S(KeyId::ConfigVersion)) + QLatin1Char('/') + k,
                           v);
        changed = true;
    }
    // A gain written as a float QVariant by a Qt5 build is "@Variant(...)":
    // rewrite it as text.
    static const KeyId gain_keys[] = {
        KeyId::AudioGainDb, KeyId::AudioGainBeeperDb, KeyId::AudioGainAy0Db,
        KeyId::AudioGainAy1Db, KeyId::AudioGainAy2Db, KeyId::AudioGainDacDb};
    for (const KeyId id : gain_keys) {
        const QString k = QString::fromStdString(config_keys::path(id));
        const QVariant v = settings_.value(k);
        if (v.userType() == QMetaType::Float || v.userType() == QMetaType::Double) {
            settings_.setValue(k, gain_text(v.toFloat()));
            changed = true;
        }
    }
    const QString hosts_key = QString::fromStdString(config_keys::path(KeyId::EspAllowedHosts));
    if (settings_.contains(hosts_key)) {
        const QVariant v = settings_.value(hosts_key);
        if (!v.isValid() || (v.userType() == QMetaType::QStringList && v.toStringList().isEmpty())) {
            settings_.setValue(hosts_key, QString());
            changed = true;
        }
    }
    if (changed) settings_.sync();
}

void AppConfig::load() {
    loaded_from_existing_file_ = QFileInfo::exists(settings_.fileName());
    normalise_legacy_layout();

    // Reset to defaults first: a partially-corrupt or truncated file must
    // not leave stale values in fields it didn't touch.
    data_ = AppConfigData{};

    settings_.beginGroup(S(KeyId::StartupMachineType));
    {
        bool ok = false;

        const QString mt_key = settings_.value(
            N(KeyId::StartupMachineType), machine_type_to_key(data_.machine_type)).toString();
        MachineType parsed_mt;
        if (parse_machine_type(mt_key.toStdString(), parsed_mt))
            data_.machine_type = parsed_mt;

        const int speed_idx = settings_.value(
            N(KeyId::StartupCpuSpeed), static_cast<int>(data_.cpu_speed)).toInt(&ok);
        if (ok && speed_idx >= config_keys::info(KeyId::StartupCpuSpeed).lo
               && speed_idx <= config_keys::info(KeyId::StartupCpuSpeed).hi)
            data_.cpu_speed = static_cast<CpuSpeed>(speed_idx);

        const int pct = settings_.value(
            N(KeyId::StartupEmulatorSpeedPercent), data_.emulator_speed_percent).toInt(&ok);
        if (ok && pct >= config_keys::info(KeyId::StartupEmulatorSpeedPercent).lo
               && pct <= config_keys::info(KeyId::StartupEmulatorSpeedPercent).hi)
            data_.emulator_speed_percent = pct;

        const int scale = settings_.value(
            N(KeyId::StartupWindowScale), data_.window_scale).toInt(&ok);
        if (ok && scale >= config_keys::info(KeyId::StartupWindowScale).lo
               && scale <= config_keys::info(KeyId::StartupWindowScale).hi)
            data_.window_scale = scale;

        data_.crt_filter     = settings_.value(N(KeyId::StartupCrtFilter), data_.crt_filter).toBool();
        data_.crt_shader     = settings_.value(N(KeyId::StartupCrtShader), data_.crt_shader).toBool();
        {
            // An unreadable or out-of-range style keeps the default.
            bool style_ok = false;
            const int style = settings_.value(N(KeyId::StartupCrtShaderStyle),
                static_cast<int>(data_.crt_shader_style)).toInt(&style_ok);
            data_.crt_shader_style = style_ok ? crt_shader::style_from_int(style)
                                              : crt_shader::STYLE_DEFAULT;
        }
        data_.silent         = settings_.value(N(KeyId::StartupSilent), data_.silent).toBool();
        data_.tape_fast_load = settings_.value(N(KeyId::StartupTapeFastLoad), data_.tape_fast_load).toBool();
        data_.tape_auto_play = settings_.value(N(KeyId::StartupTapeAutoPlay), data_.tape_auto_play).toBool();

        // Issue #35 — anything other than the two known words keeps the
        // default, the same way an unknown joy_source does: a typo in a
        // hand-edited file must not silently change how the emulator degrades.
        const QString prefer = settings_.value(
            N(KeyId::StartupWhenSlowPrefer), when_slow_prefer_to_key(data_.when_slow_prefer))
                                   .toString().trimmed().toLower();
        const int prefer_idx = config_keys::word_index(
            config_keys::info(KeyId::StartupWhenSlowPrefer).values, prefer.toStdString());
        if (prefer_idx >= 0)
            data_.when_slow_prefer = static_cast<audio_pacing::WhenSlowPrefer>(prefer_idx);
    }
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::AudioGainDb));
    {
        load_gain(settings_, KeyId::AudioGainDb, data_.audio_gain_db);
        load_gain(settings_, KeyId::AudioGainBeeperDb, data_.audio_gain_beeper_db);
        load_gain(settings_, KeyId::AudioGainAy0Db, data_.audio_gain_ay_db[0]);
        load_gain(settings_, KeyId::AudioGainAy1Db, data_.audio_gain_ay_db[1]);
        load_gain(settings_, KeyId::AudioGainAy2Db, data_.audio_gain_ay_db[2]);
        load_gain(settings_, KeyId::AudioGainDacDb, data_.audio_gain_dac_db);
    }
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::PathsLastLoadDir));
    data_.last_load_dir  = ini_text(settings_.value(N(KeyId::PathsLastLoadDir), data_.last_load_dir));
    data_.sd_card_path   = ini_text(settings_.value(N(KeyId::PathsSdCardPath), data_.sd_card_path));
    data_.screenshot_dir = ini_text(settings_.value(N(KeyId::PathsScreenshotDir), data_.screenshot_dir));
    settings_.endGroup();

    // GH #406. Its own group; cleaned of blanks and cut to the cap, so a
    // hand-edited file cannot grow the menu.
    settings_.beginGroup(S(KeyId::RecentFiles));
    data_.recent_files = settings_.value(N(KeyId::RecentFiles)).toStringList();
    data_.recent_files.removeAll(QString());
    while (data_.recent_files.size() > static_cast<qsizetype>(recent_files::kMax))
        data_.recent_files.removeLast();
    settings_.endGroup();

    // GH #19. A new group rather than more `[paths]` keys, because only one of
    // the two is a path. No CONFIG_VERSION bump: both keys are absent from a
    // v1 file and both fall back to their defaults, which is the behaviour
    // that file already had.
    settings_.beginGroup(S(KeyId::ScreenshotQuickDir));
    data_.quick_screenshot_dir = ini_text(settings_.value(
        N(KeyId::ScreenshotQuickDir), data_.quick_screenshot_dir));
    {
        // Anything other than the two known words keeps the default, the same
        // way when_slow_prefer and joy_source do: a typo in a hand-edited file
        // must not silently change what a capture produces.
        const QString fmt = settings_.value(
            N(KeyId::ScreenshotQuickFormat), quick_screenshot_format_to_key(data_.quick_screenshot_format))
                               .toString().trimmed().toLower();
        const int fmt_idx = config_keys::word_index(
            config_keys::info(KeyId::ScreenshotQuickFormat).values, fmt.toStdString());
        if (fmt_idx >= 0)
            data_.quick_screenshot_format = static_cast<ScreenshotFormat>(fmt_idx);
    }
    settings_.endGroup();

    // GH #321 — Quick Save retention. A number >= 1 or the word `unlimited`;
    // anything else keeps the default, so a typo cannot silently turn pruning
    // off (or delete more than the user asked).
    settings_.beginGroup(S(KeyId::SnapshotQuickKeep));
    {
        const QString v = settings_.value(N(KeyId::SnapshotQuickKeep)).toString().trimmed().toLower();
        bool ok = false;
        const int n = v.toInt(&ok);
        if (v == QLatin1String("unlimited"))     data_.quick_snapshot_keep = 0;
        else if (ok && n >= config_keys::info(KeyId::SnapshotQuickKeep).lo
                 && n <= config_keys::info(KeyId::SnapshotQuickKeep).hi) data_.quick_snapshot_keep = n;
    }
    settings_.endGroup();

    // Task 79 — per-connector input source. Unknown/malformed values keep the
    // default (Sdl). The one-cursor rule is enforced by the UI and by the
    // Emulator coordinator, so a hand-edited both-"keys" file resolves cleanly
    // at wire-up time rather than being rejected here.
    settings_.beginGroup(S(KeyId::InputJoy1Source));
    const char* keys[2] = { N(KeyId::InputJoy1Source), N(KeyId::InputJoy2Source) };
    for (int i = 0; i < 2; ++i) {
        const QString v = settings_.value(keys[i], joy_source_str(data_.joy_source[i])).toString();
        JoySource parsed;
        if (parse_joy_source(v.toStdString().c_str(), parsed))
            data_.joy_source[i] = parsed;
    }
    // GH #365 — an unknown word keeps the default, as joy_source does.
    {
        pointer_capture::ReleaseMethod m;
        const QString v = settings_.value(N(KeyId::InputMouseRelease)).toString().trimmed();
        if (pointer_capture::parse_release_method(v.toStdString(), m))
            data_.mouse_release = m;
    }
    // GH #311 — assigned controller. An id that does not parse is dropped
    // together with its name, so a hand-edit cannot leave a name with no id.
    const char* dev_keys[2]  = { N(KeyId::InputJoy1Device), N(KeyId::InputJoy2Device) };
    const char* name_keys[2] = { N(KeyId::InputJoy1DeviceName), N(KeyId::InputJoy2DeviceName) };
    for (int i = 0; i < 2; ++i) {
        std::string canon;
        const QString raw = settings_.value(dev_keys[i], data_.joy_device[i]).toString().trimmed();
        if (!raw.isEmpty() && normalize_joy_device_id(raw.toStdString(), canon)) {
            data_.joy_device[i]      = QString::fromStdString(canon);
            data_.joy_device_name[i] = ini_text(settings_.value(name_keys[i], data_.joy_device_name[i]));
        } else {
            data_.joy_device[i].clear();
            data_.joy_device_name[i].clear();
        }
    }
    settings_.endGroup();

    // GH #25 — the emulated ESP-01. Read through EspHostPolicy::add so a
    // hand-edited file gets the same blank-rejection and de-duplication the
    // CLI does; a security control that means two different things depending
    // on where the value came from is not a control.
    // GH #1 — the debugger key bindings. Only REDEFINITIONS live in the file,
    // so an absent (or absent-key) group simply leaves every action at its
    // compiled-in default, and a default this project changes later reaches a
    // user who never overrode it.
    debug_key_issues_.clear();
    {
        std::vector<std::pair<std::string, std::string>> entries;
        settings_.beginGroup(S(KeyId::DebuggerKey));
        // childKeys() preserves the file's order, which is what makes the
        // "first entry wins" tie-breaks in build_keymap() reproducible.
        for (const QString& k : settings_.childKeys())
            entries.emplace_back(k.toStdString(), settings_.value(k).toString().toStdString());
        settings_.endGroup();
        data_.debug_keys = jnext::dbgkeys::build_keymap(entries, debug_key_issues_, kQtCtrlIsCommand);
    }

    settings_.beginGroup(S(KeyId::EspEnabled));
    data_.esp_enabled = settings_.value(N(KeyId::EspEnabled), data_.esp_enabled).toBool();
    {
        EspHostPolicy policy;
        const QStringList saved = settings_.value(N(KeyId::EspAllowedHosts)).toStringList();
        for (const QString& host : saved) policy.add(host.toStdString());
        data_.esp_allowed_hosts = policy.allowed_hosts;
    }
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::NextpiEnabled));
    data_.nextpi_enabled     = settings_.value(N(KeyId::NextpiEnabled), data_.nextpi_enabled).toBool();
    data_.nextpi_dir         = ini_text(settings_.value(N(KeyId::NextpiDir), data_.nextpi_dir)).trimmed();
    data_.nextpi_release     = ini_text(settings_.value(N(KeyId::NextpiRelease), data_.nextpi_release)).trimmed();
    data_.nextpi_qemu_binary = ini_text(settings_.value(N(KeyId::NextpiQemuBinary), data_.nextpi_qemu_binary)).trimmed();
    data_.nextpi_audio       = ini_text(settings_.value(N(KeyId::NextpiAudio), data_.nextpi_audio)).trimmed();
    data_.nextpi_qemu_args   = ini_text(settings_.value(N(KeyId::NextpiQemuArgs), data_.nextpi_qemu_args)).trimmed();
    settings_.endGroup();
}

void AppConfig::save() const {
    // Ensure the parent directory (~/.jnext) exists — on a fresh machine it
    // may not yet, and QSettings will not persist to a missing directory.
    QDir().mkpath(QFileInfo(settings_.fileName()).absolutePath());

    normalise_legacy_layout();
    settings_.setValue(QString::fromStdString(config_keys::path(KeyId::ConfigVersion)),
                       AppConfigData::CONFIG_VERSION);

    settings_.beginGroup(S(KeyId::StartupMachineType));
    settings_.setValue(N(KeyId::StartupMachineType), machine_type_to_key(data_.machine_type));
    settings_.setValue(N(KeyId::StartupCpuSpeed), static_cast<int>(data_.cpu_speed));
    settings_.setValue(N(KeyId::StartupEmulatorSpeedPercent), data_.emulator_speed_percent);
    settings_.setValue(N(KeyId::StartupWindowScale), data_.window_scale);
    settings_.setValue(N(KeyId::StartupCrtFilter), data_.crt_filter);
    settings_.setValue(N(KeyId::StartupCrtShader), data_.crt_shader);
    settings_.setValue(N(KeyId::StartupCrtShaderStyle), static_cast<int>(data_.crt_shader_style));
    settings_.setValue(N(KeyId::StartupSilent), data_.silent);
    settings_.setValue(N(KeyId::StartupTapeFastLoad), data_.tape_fast_load);
    settings_.setValue(N(KeyId::StartupTapeAutoPlay), data_.tape_auto_play);
    settings_.setValue(N(KeyId::StartupWhenSlowPrefer),
                       when_slow_prefer_to_key(data_.when_slow_prefer));
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::AudioGainDb));
    settings_.setValue(N(KeyId::AudioGainDb), gain_text(data_.audio_gain_db));
    settings_.setValue(N(KeyId::AudioGainBeeperDb), gain_text(data_.audio_gain_beeper_db));
    settings_.setValue(N(KeyId::AudioGainAy0Db), gain_text(data_.audio_gain_ay_db[0]));
    settings_.setValue(N(KeyId::AudioGainAy1Db), gain_text(data_.audio_gain_ay_db[1]));
    settings_.setValue(N(KeyId::AudioGainAy2Db), gain_text(data_.audio_gain_ay_db[2]));
    settings_.setValue(N(KeyId::AudioGainDacDb), gain_text(data_.audio_gain_dac_db));
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::PathsLastLoadDir));
    settings_.setValue(N(KeyId::PathsLastLoadDir), data_.last_load_dir);
    settings_.setValue(N(KeyId::PathsSdCardPath), data_.sd_card_path);
    settings_.setValue(N(KeyId::PathsScreenshotDir), data_.screenshot_dir);
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::RecentFiles));
    // An empty list is not written: QSettings stores it as "@Invalid()".
    if (data_.recent_files.isEmpty()) settings_.remove(N(KeyId::RecentFiles));
    else settings_.setValue(N(KeyId::RecentFiles), data_.recent_files);
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::ScreenshotQuickDir));   // GH #19
    settings_.setValue(N(KeyId::ScreenshotQuickDir), data_.quick_screenshot_dir);
    settings_.setValue(N(KeyId::ScreenshotQuickFormat),
                       quick_screenshot_format_to_key(data_.quick_screenshot_format));
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::SnapshotQuickKeep));   // GH #321
    if (data_.quick_snapshot_keep <= 0) settings_.setValue(N(KeyId::SnapshotQuickKeep), "unlimited");
    else settings_.setValue(N(KeyId::SnapshotQuickKeep), data_.quick_snapshot_keep);
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::InputJoy1Source));   // Task 79
    settings_.setValue(N(KeyId::InputJoy1Source), QString::fromLatin1(joy_source_str(data_.joy_source[0])));
    settings_.setValue(N(KeyId::InputJoy2Source), QString::fromLatin1(joy_source_str(data_.joy_source[1])));
    settings_.setValue(N(KeyId::InputJoy1Device), data_.joy_device[0]);        // GH #311
    settings_.setValue(N(KeyId::InputJoy1DeviceName), data_.joy_device_name[0]);
    settings_.setValue(N(KeyId::InputJoy2Device), data_.joy_device[1]);
    settings_.setValue(N(KeyId::InputJoy2DeviceName), data_.joy_device_name[1]);
    settings_.setValue(N(KeyId::InputMouseRelease),                          // GH #365
                       QString::fromLatin1(pointer_capture::release_method_name(data_.mouse_release)));
    settings_.endGroup();

    // GH #1 — ONLY redefinitions are written. The group is removed first so
    // that resetting an action back to its default REMOVES its line rather
    // than leaving a stale one, and a file whose every action is at its
    // default has no [debugger_keys] section at all.
    settings_.remove(S(KeyId::DebuggerKey));
    {
        settings_.beginGroup(S(KeyId::DebuggerKey));
        for (int i = 0; i < jnext::dbgkeys::ACTION_COUNT; ++i) {
            const auto a = static_cast<jnext::dbgkeys::Action>(i);
            if (data_.debug_keys.is_default(a)) continue;
            settings_.setValue(QString::fromLatin1(jnext::dbgkeys::info(a).id),
                               QString::fromStdString(
                                   jnext::dbgkeys::render_combo(data_.debug_keys.combo(a))));
        }
        // Entries this build did not recognise are written back verbatim, so
        // running an older jnext does not delete a newer one's binding.
        for (const auto& u : data_.debug_keys.unknown_entries())
            settings_.setValue(QString::fromStdString(u.first),
                               QString::fromStdString(u.second));
        settings_.endGroup();
    }

    settings_.beginGroup(S(KeyId::EspEnabled));     // GH #25
    settings_.setValue(N(KeyId::EspEnabled), data_.esp_enabled);
    {
        QStringList hosts;
        for (const std::string& host : data_.esp_allowed_hosts)
            hosts << QString::fromStdString(host);
        // Empty -> a plain empty string: QSettings writes an empty
        // QStringList as "@Invalid()" (GH #312).
        if (hosts.isEmpty()) settings_.setValue(N(KeyId::EspAllowedHosts), QString());
        else                 settings_.setValue(N(KeyId::EspAllowedHosts), hosts);
    }
    settings_.endGroup();

    settings_.beginGroup(S(KeyId::NextpiEnabled));
    settings_.setValue(N(KeyId::NextpiEnabled), data_.nextpi_enabled);
    settings_.setValue(N(KeyId::NextpiDir), data_.nextpi_dir);
    settings_.setValue(N(KeyId::NextpiRelease), data_.nextpi_release);
    settings_.setValue(N(KeyId::NextpiQemuBinary), data_.nextpi_qemu_binary);
    settings_.setValue(N(KeyId::NextpiAudio), data_.nextpi_audio);
    settings_.setValue(N(KeyId::NextpiQemuArgs), data_.nextpi_qemu_args);
    settings_.endGroup();

    settings_.sync();
}
