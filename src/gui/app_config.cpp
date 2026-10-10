#include "gui/app_config.h"
#include "gui/emulator_widget.h"
#include "peripheral/esp_host_policy.h"

#include <QDir>
#include <QFileInfo>

#include <cmath>

namespace {

// Short machine-type codes accepted by core/emulator_config.h's
// parse_machine_type() (48k/128k/+3/next) — distinct from machine_type_str()'s
// display strings ("ZX Next" etc.), which parse_machine_type() does not accept
// back. Keeping the round-trip local to this file avoids coupling the on-disk
// format to display text that may change.
QString machine_type_to_key(MachineType t) {
    switch (t) {
        case MachineType::ZX48K:      return QStringLiteral("48k");
        case MachineType::ZX128K:     return QStringLiteral("128k");
        case MachineType::ZX_PLUS3:   return QStringLiteral("+3");
        case MachineType::ZXN_ISSUE2: default: return QStringLiteral("next");
    }
}

// Issue #35 — on-disk spelling of the degradation policy. Deliberately the
// same two words the CLI accepts, so `--when-slow-prefer video` and
// `when_slow_prefer=video` are one vocabulary rather than two.
QString when_slow_prefer_to_key(audio_pacing::WhenSlowPrefer p) {
    return p == audio_pacing::WhenSlowPrefer::Video ? QStringLiteral("video")
                                                    : QStringLiteral("audio");
}

// GH #19 — on-disk spelling of the quick-screenshot format. The same two
// words a filename extension uses, so `.scr` and `quick_format=scr` are one
// vocabulary rather than two.
QString quick_screenshot_format_to_key(ScreenshotFormat f) {
    return f == ScreenshotFormat::Scr ? QStringLiteral("scr") : QStringLiteral("png");
}

void load_gain(QSettings& settings, const char* key, float& target)
{
    bool ok = false;
    const double db = settings.value(key, static_cast<double>(target)).toDouble(&ok);
    if (ok && std::isfinite(db) && db >= -24.0 && db <= 24.0)
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
        settings_.setValue(QStringLiteral("config/")
                           + (k == QLatin1String("config_version")
                                  ? QStringLiteral("version") : k), v);
        changed = true;
    }
    // A gain written as a float QVariant by a Qt5 build is "@Variant(...)":
    // rewrite it as text.
    static const char* const gain_keys[] = {
        "audio/gain_db", "audio/gain_beeper_db", "audio/gain_ay0_db",
        "audio/gain_ay1_db", "audio/gain_ay2_db", "audio/gain_dac_db"};
    for (const char* k : gain_keys) {
        const QVariant v = settings_.value(QLatin1String(k));
        if (v.userType() == QMetaType::Float || v.userType() == QMetaType::Double) {
            settings_.setValue(QLatin1String(k), gain_text(v.toFloat()));
            changed = true;
        }
    }
    const QString hosts_key = QStringLiteral("esp/allowed_hosts");
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

    settings_.beginGroup("startup");
    {
        bool ok = false;

        const QString mt_key = settings_.value(
            "machine_type", machine_type_to_key(data_.machine_type)).toString();
        MachineType parsed_mt;
        if (parse_machine_type(mt_key.toStdString(), parsed_mt))
            data_.machine_type = parsed_mt;

        const int speed_idx = settings_.value(
            "cpu_speed", static_cast<int>(data_.cpu_speed)).toInt(&ok);
        if (ok && speed_idx >= 0 && speed_idx <= 3)
            data_.cpu_speed = static_cast<CpuSpeed>(speed_idx);

        const int pct = settings_.value(
            "emulator_speed_percent", data_.emulator_speed_percent).toInt(&ok);
        if (ok && pct >= 10 && pct <= 1000)
            data_.emulator_speed_percent = pct;

        const int scale = settings_.value(
            "window_scale", data_.window_scale).toInt(&ok);
        if (ok && scale >= EmulatorWidget::MIN_SCALE && scale <= EmulatorWidget::MAX_SCALE)
            data_.window_scale = scale;

        data_.crt_filter     = settings_.value("crt_filter", data_.crt_filter).toBool();
        data_.silent         = settings_.value("silent", data_.silent).toBool();
        data_.tape_fast_load = settings_.value("tape_fast_load", data_.tape_fast_load).toBool();

        // Issue #35 — anything other than the two known words keeps the
        // default, the same way an unknown joy_source does: a typo in a
        // hand-edited file must not silently change how the emulator degrades.
        const QString prefer = settings_.value(
            "when_slow_prefer", when_slow_prefer_to_key(data_.when_slow_prefer))
                                   .toString().trimmed().toLower();
        if (prefer == QLatin1String("video"))
            data_.when_slow_prefer = audio_pacing::WhenSlowPrefer::Video;
        else if (prefer == QLatin1String("audio"))
            data_.when_slow_prefer = audio_pacing::WhenSlowPrefer::Audio;
    }
    settings_.endGroup();

    settings_.beginGroup("audio");
    {
        load_gain(settings_, "gain_db", data_.audio_gain_db);
        load_gain(settings_, "gain_beeper_db", data_.audio_gain_beeper_db);
        load_gain(settings_, "gain_ay0_db", data_.audio_gain_ay_db[0]);
        load_gain(settings_, "gain_ay1_db", data_.audio_gain_ay_db[1]);
        load_gain(settings_, "gain_ay2_db", data_.audio_gain_ay_db[2]);
        load_gain(settings_, "gain_dac_db", data_.audio_gain_dac_db);
    }
    settings_.endGroup();

    settings_.beginGroup("paths");
    data_.last_load_dir  = settings_.value("last_load_dir", data_.last_load_dir).toString();
    data_.sd_card_path   = settings_.value("sd_card_path", data_.sd_card_path).toString();
    data_.screenshot_dir = settings_.value("screenshot_dir", data_.screenshot_dir).toString();
    settings_.endGroup();

    // GH #19. A new group rather than more `[paths]` keys, because only one of
    // the two is a path. No CONFIG_VERSION bump: both keys are absent from a
    // v1 file and both fall back to their defaults, which is the behaviour
    // that file already had.
    settings_.beginGroup("screenshot");
    data_.quick_screenshot_dir = settings_.value(
        "quick_dir", data_.quick_screenshot_dir).toString();
    {
        // Anything other than the two known words keeps the default, the same
        // way when_slow_prefer and joy_source do: a typo in a hand-edited file
        // must not silently change what a capture produces.
        const QString fmt = settings_.value(
            "quick_format", quick_screenshot_format_to_key(data_.quick_screenshot_format))
                               .toString().trimmed().toLower();
        if (fmt == QLatin1String("scr"))
            data_.quick_screenshot_format = ScreenshotFormat::Scr;
        else if (fmt == QLatin1String("png"))
            data_.quick_screenshot_format = ScreenshotFormat::Png;
    }
    settings_.endGroup();

    // Task 79 — per-connector input source. Unknown/malformed values keep the
    // default (Sdl). The one-cursor rule is enforced by the UI and by the
    // Emulator coordinator, so a hand-edited both-"keys" file resolves cleanly
    // at wire-up time rather than being rejected here.
    settings_.beginGroup("input");
    const char* keys[2] = { "joy1_source", "joy2_source" };
    for (int i = 0; i < 2; ++i) {
        const QString v = settings_.value(keys[i], joy_source_str(data_.joy_source[i])).toString();
        JoySource parsed;
        if (parse_joy_source(v.toStdString().c_str(), parsed))
            data_.joy_source[i] = parsed;
    }
    // GH #311 — assigned controller. An id that does not parse is dropped
    // together with its name, so a hand-edit cannot leave a name with no id.
    const char* dev_keys[2]  = { "joy1_device", "joy2_device" };
    const char* name_keys[2] = { "joy1_device_name", "joy2_device_name" };
    for (int i = 0; i < 2; ++i) {
        std::string canon;
        const QString raw = settings_.value(dev_keys[i], data_.joy_device[i]).toString().trimmed();
        if (!raw.isEmpty() && normalize_joy_device_id(raw.toStdString(), canon)) {
            data_.joy_device[i]      = QString::fromStdString(canon);
            data_.joy_device_name[i] = settings_.value(name_keys[i], data_.joy_device_name[i]).toString();
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
        settings_.beginGroup("debugger_keys");
        // childKeys() preserves the file's order, which is what makes the
        // "first entry wins" tie-breaks in build_keymap() reproducible.
        for (const QString& k : settings_.childKeys())
            entries.emplace_back(k.toStdString(), settings_.value(k).toString().toStdString());
        settings_.endGroup();
        data_.debug_keys = jnext::dbgkeys::build_keymap(entries, debug_key_issues_);
    }

    settings_.beginGroup("esp");
    data_.esp_enabled = settings_.value("enabled", data_.esp_enabled).toBool();
    {
        EspHostPolicy policy;
        const QStringList saved = settings_.value("allowed_hosts").toStringList();
        for (const QString& host : saved) policy.add(host.toStdString());
        data_.esp_allowed_hosts = policy.allowed_hosts;
    }
    settings_.endGroup();

    settings_.beginGroup("nextpi");
    data_.nextpi_enabled     = settings_.value("enabled", data_.nextpi_enabled).toBool();
    data_.nextpi_dir         = settings_.value("dir", data_.nextpi_dir).toString().trimmed();
    data_.nextpi_release     = settings_.value("release", data_.nextpi_release).toString().trimmed();
    data_.nextpi_qemu_binary = settings_.value("qemu_binary", data_.nextpi_qemu_binary).toString().trimmed();
    data_.nextpi_audio       = settings_.value("audio", data_.nextpi_audio).toString().trimmed();
    settings_.endGroup();
}

void AppConfig::save() const {
    // Ensure the parent directory (~/.jnext) exists — on a fresh machine it
    // may not yet, and QSettings will not persist to a missing directory.
    QDir().mkpath(QFileInfo(settings_.fileName()).absolutePath());

    normalise_legacy_layout();
    settings_.setValue("config/version", AppConfigData::CONFIG_VERSION);

    settings_.beginGroup("startup");
    settings_.setValue("machine_type", machine_type_to_key(data_.machine_type));
    settings_.setValue("cpu_speed", static_cast<int>(data_.cpu_speed));
    settings_.setValue("emulator_speed_percent", data_.emulator_speed_percent);
    settings_.setValue("window_scale", data_.window_scale);
    settings_.setValue("crt_filter", data_.crt_filter);
    settings_.setValue("silent", data_.silent);
    settings_.setValue("tape_fast_load", data_.tape_fast_load);
    settings_.setValue("when_slow_prefer",
                       when_slow_prefer_to_key(data_.when_slow_prefer));
    settings_.endGroup();

    settings_.beginGroup("audio");
    settings_.setValue("gain_db", gain_text(data_.audio_gain_db));
    settings_.setValue("gain_beeper_db", gain_text(data_.audio_gain_beeper_db));
    settings_.setValue("gain_ay0_db", gain_text(data_.audio_gain_ay_db[0]));
    settings_.setValue("gain_ay1_db", gain_text(data_.audio_gain_ay_db[1]));
    settings_.setValue("gain_ay2_db", gain_text(data_.audio_gain_ay_db[2]));
    settings_.setValue("gain_dac_db", gain_text(data_.audio_gain_dac_db));
    settings_.endGroup();

    settings_.beginGroup("paths");
    settings_.setValue("last_load_dir", data_.last_load_dir);
    settings_.setValue("sd_card_path", data_.sd_card_path);
    settings_.setValue("screenshot_dir", data_.screenshot_dir);
    settings_.endGroup();

    settings_.beginGroup("screenshot");   // GH #19
    settings_.setValue("quick_dir", data_.quick_screenshot_dir);
    settings_.setValue("quick_format",
                       quick_screenshot_format_to_key(data_.quick_screenshot_format));
    settings_.endGroup();

    settings_.beginGroup("input");   // Task 79
    settings_.setValue("joy1_source", QString::fromLatin1(joy_source_str(data_.joy_source[0])));
    settings_.setValue("joy2_source", QString::fromLatin1(joy_source_str(data_.joy_source[1])));
    settings_.setValue("joy1_device", data_.joy_device[0]);        // GH #311
    settings_.setValue("joy1_device_name", data_.joy_device_name[0]);
    settings_.setValue("joy2_device", data_.joy_device[1]);
    settings_.setValue("joy2_device_name", data_.joy_device_name[1]);
    settings_.endGroup();

    // GH #1 — ONLY redefinitions are written. The group is removed first so
    // that resetting an action back to its default REMOVES its line rather
    // than leaving a stale one, and a file whose every action is at its
    // default has no [debugger_keys] section at all.
    settings_.remove("debugger_keys");
    {
        settings_.beginGroup("debugger_keys");
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

    settings_.beginGroup("esp");     // GH #25
    settings_.setValue("enabled", data_.esp_enabled);
    {
        QStringList hosts;
        for (const std::string& host : data_.esp_allowed_hosts)
            hosts << QString::fromStdString(host);
        // Empty -> a plain empty string: QSettings writes an empty
        // QStringList as "@Invalid()" (GH #312).
        if (hosts.isEmpty()) settings_.setValue("allowed_hosts", QString());
        else                 settings_.setValue("allowed_hosts", hosts);
    }
    settings_.endGroup();

    settings_.beginGroup("nextpi");
    settings_.setValue("enabled", data_.nextpi_enabled);
    settings_.setValue("dir", data_.nextpi_dir);
    settings_.setValue("release", data_.nextpi_release);
    settings_.setValue("qemu_binary", data_.nextpi_qemu_binary);
    settings_.setValue("audio", data_.nextpi_audio);
    settings_.endGroup();

    settings_.sync();
}
