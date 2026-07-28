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

void load_gain(QSettings& settings, const char* key, float& target)
{
    bool ok = false;
    const double db = settings.value(key, static_cast<double>(target)).toDouble(&ok);
    if (ok && std::isfinite(db) && db >= -24.0 && db <= 24.0)
        target = static_cast<float>(db);
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

void AppConfig::load() {
    loaded_from_existing_file_ = QFileInfo::exists(settings_.fileName());

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
    settings_.endGroup();

    // GH #25 — the emulated ESP-01. Read through EspHostPolicy::add so a
    // hand-edited file gets the same blank-rejection and de-duplication the
    // CLI does; a security control that means two different things depending
    // on where the value came from is not a control.
    settings_.beginGroup("esp");
    data_.esp_enabled = settings_.value("enabled", data_.esp_enabled).toBool();
    {
        EspHostPolicy policy;
        const QStringList saved = settings_.value("allowed_hosts").toStringList();
        for (const QString& host : saved) policy.add(host.toStdString());
        data_.esp_allowed_hosts = policy.allowed_hosts;
    }
    settings_.endGroup();
}

void AppConfig::save() const {
    // Ensure the parent directory (~/.jnext) exists — on a fresh machine it
    // may not yet, and QSettings will not persist to a missing directory.
    QDir().mkpath(QFileInfo(settings_.fileName()).absolutePath());

    settings_.setValue("config_version", AppConfigData::CONFIG_VERSION);

    settings_.beginGroup("startup");
    settings_.setValue("machine_type", machine_type_to_key(data_.machine_type));
    settings_.setValue("cpu_speed", static_cast<int>(data_.cpu_speed));
    settings_.setValue("emulator_speed_percent", data_.emulator_speed_percent);
    settings_.setValue("window_scale", data_.window_scale);
    settings_.setValue("crt_filter", data_.crt_filter);
    settings_.setValue("silent", data_.silent);
    settings_.setValue("tape_fast_load", data_.tape_fast_load);
    settings_.endGroup();

    settings_.beginGroup("audio");
    settings_.setValue("gain_db", data_.audio_gain_db);
    settings_.setValue("gain_beeper_db", data_.audio_gain_beeper_db);
    settings_.setValue("gain_ay0_db", data_.audio_gain_ay_db[0]);
    settings_.setValue("gain_ay1_db", data_.audio_gain_ay_db[1]);
    settings_.setValue("gain_ay2_db", data_.audio_gain_ay_db[2]);
    settings_.setValue("gain_dac_db", data_.audio_gain_dac_db);
    settings_.endGroup();

    settings_.beginGroup("paths");
    settings_.setValue("last_load_dir", data_.last_load_dir);
    settings_.setValue("sd_card_path", data_.sd_card_path);
    settings_.setValue("screenshot_dir", data_.screenshot_dir);
    settings_.endGroup();

    settings_.beginGroup("input");   // Task 79
    settings_.setValue("joy1_source", QString::fromLatin1(joy_source_str(data_.joy_source[0])));
    settings_.setValue("joy2_source", QString::fromLatin1(joy_source_str(data_.joy_source[1])));
    settings_.endGroup();

    settings_.beginGroup("esp");     // GH #25
    settings_.setValue("enabled", data_.esp_enabled);
    {
        QStringList hosts;
        for (const std::string& host : data_.esp_allowed_hosts)
            hosts << QString::fromStdString(host);
        settings_.setValue("allowed_hosts", hosts);
    }
    settings_.endGroup();

    settings_.sync();
}
