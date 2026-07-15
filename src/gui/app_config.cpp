#include "gui/app_config.h"
#include "gui/emulator_widget.h"

#include <QFileInfo>

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

} // namespace

AppConfig::AppConfig()
    : settings_("JNEXT", "jnext")
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

    settings_.beginGroup("paths");
    data_.last_load_dir  = settings_.value("last_load_dir", data_.last_load_dir).toString();
    data_.sd_card_path   = settings_.value("sd_card_path", data_.sd_card_path).toString();
    data_.screenshot_dir = settings_.value("screenshot_dir", data_.screenshot_dir).toString();
    settings_.endGroup();
}

void AppConfig::save() const {
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

    settings_.beginGroup("paths");
    settings_.setValue("last_load_dir", data_.last_load_dir);
    settings_.setValue("sd_card_path", data_.sd_card_path);
    settings_.setValue("screenshot_dir", data_.screenshot_dir);
    settings_.endGroup();

    settings_.sync();
}
