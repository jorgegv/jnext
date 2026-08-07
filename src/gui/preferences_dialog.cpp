#include "gui/preferences_dialog.h"
#include "gui/emulator_widget.h"

#include "peripheral/esp_host_policy.h"

#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QRegularExpression>

#include <cmath>

PreferencesDialog::PreferencesDialog(const AppConfigData& current, QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));

    // Populated by build_startup_tab()/build_paths_tab() below, then filled
    // from `current` once every widget exists (avoids partially-initialised
    // reads if a future field is added to one tab but not the other).
    auto* tabs = new QTabWidget(this);
    tabs->addTab(build_startup_tab(), tr("Startup"));
    tabs->addTab(build_input_tab(), tr("Input"));
    tabs->addTab(build_audio_tab(), tr("Audio"));
    tabs->addTab(build_network_tab(), tr("Network"));
    tabs->addTab(build_paths_tab(), tr("Paths"));

    // --- Fill from `current` ---
    machine_combo_->setCurrentIndex(machine_combo_->findData(static_cast<int>(current.machine_type)));
    cpu_speed_combo_->setCurrentIndex(cpu_speed_combo_->findData(static_cast<int>(current.cpu_speed)));
    emu_speed_spin_->setValue(current.emulator_speed_percent);
    when_slow_combo_->setCurrentIndex(
        when_slow_combo_->findData(static_cast<int>(current.when_slow_prefer)));
    scale_combo_->setCurrentIndex(scale_combo_->findData(current.window_scale));
    crt_check_->setChecked(current.crt_filter);
    silent_check_->setChecked(current.silent);
    tape_fast_check_->setChecked(current.tape_fast_load);
    audio_gain_slider_->setValue(static_cast<int>(std::lround(current.audio_gain_db)));
    audio_beeper_gain_slider_->setValue(
        static_cast<int>(std::lround(current.audio_gain_beeper_db)));
    for (int chip = 0; chip < 3; ++chip)
        audio_ay_gain_slider_[chip]->setValue(
            static_cast<int>(std::lround(current.audio_gain_ay_db[chip])));
    audio_dac_gain_slider_->setValue(
        static_cast<int>(std::lround(current.audio_gain_dac_db)));
    joy1_source_combo_->setCurrentIndex(
        joy1_source_combo_->findData(static_cast<int>(current.joy_source[0])));
    joy2_source_combo_->setCurrentIndex(
        joy2_source_combo_->findData(static_cast<int>(current.joy_source[1])));
    esp_enabled_check_->setChecked(current.esp_enabled);
    {
        QStringList hosts;
        for (const std::string& host : current.esp_allowed_hosts)
            hosts << QString::fromStdString(host);
        esp_hosts_edit_->setPlainText(hosts.join(QLatin1Char('\n')));
    }
    esp_hosts_edit_->setEnabled(current.esp_enabled);
    last_load_dir_edit_->setText(current.last_load_dir);
    sd_card_path_edit_->setText(current.sd_card_path);
    screenshot_dir_edit_->setText(current.screenshot_dir);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this);
    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        emit apply_requested(collect());
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this, [this]() {
        emit apply_requested(collect());
    });

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

QWidget* PreferencesDialog::build_startup_tab() {
    auto* tab = new QWidget(this);
    auto* form = new QFormLayout(tab);

    machine_combo_ = new QComboBox(tab);
    machine_combo_->addItem(tr("48K"),  static_cast<int>(MachineType::ZX48K));
    machine_combo_->addItem(tr("128K"), static_cast<int>(MachineType::ZX128K));
    machine_combo_->addItem(tr("+3"),   static_cast<int>(MachineType::ZX_PLUS3));
    machine_combo_->addItem(tr("Next"), static_cast<int>(MachineType::ZXN_ISSUE2));
    form->addRow(tr("Machine type:"), machine_combo_);

    cpu_speed_combo_ = new QComboBox(tab);
    cpu_speed_combo_->addItem(tr(cpu_speed_str(CpuSpeed::MHZ_3_5)), static_cast<int>(CpuSpeed::MHZ_3_5));
    cpu_speed_combo_->addItem(tr(cpu_speed_str(CpuSpeed::MHZ_7)),   static_cast<int>(CpuSpeed::MHZ_7));
    cpu_speed_combo_->addItem(tr(cpu_speed_str(CpuSpeed::MHZ_14)),  static_cast<int>(CpuSpeed::MHZ_14));
    cpu_speed_combo_->addItem(tr(cpu_speed_str(CpuSpeed::MHZ_28)),  static_cast<int>(CpuSpeed::MHZ_28));
    form->addRow(tr("CPU speed:"), cpu_speed_combo_);

    emu_speed_spin_ = new QSpinBox(tab);
    emu_speed_spin_->setRange(10, 1000);
    emu_speed_spin_->setSingleStep(10);
    emu_speed_spin_->setSuffix(tr("%"));
    form->addRow(tr("Emulator speed:"), emu_speed_spin_);

    // Issue #35 — the degradation policy. Sits next to Emulator speed because
    // it is the other control over how emulated time meets real time, and it
    // only ever does anything when the two cannot be reconciled.
    when_slow_combo_ = new QComboBox(tab);
    when_slow_combo_->setObjectName(QStringLiteral("whenSlowPreferCombo"));
    when_slow_combo_->addItem(
        tr("Smooth sound (drop frames)"),
        static_cast<int>(audio_pacing::WhenSlowPrefer::Audio));
    when_slow_combo_->addItem(
        tr("Smooth picture (run slower, sound stutters)"),
        static_cast<int>(audio_pacing::WhenSlowPrefer::Video));
    when_slow_combo_->setToolTip(
        tr("Only takes effect on a host that cannot emulate in real time. "
           "Smooth sound keeps the sound card fed by emulating two frames in "
           "one slot, so the first of the pair is never shown. Smooth picture "
           "shows every frame and lets the machine run slower than real time."));
    form->addRow(tr("When the host is too slow:"), when_slow_combo_);

    scale_combo_ = new QComboBox(tab);
    for (int s = EmulatorWidget::MIN_SCALE; s <= EmulatorWidget::MAX_SCALE; ++s)
        scale_combo_->addItem(tr("%1x").arg(s), s);
    form->addRow(tr("Window scale:"), scale_combo_);

    crt_check_ = new QCheckBox(tr("CRT scanline filter"), tab);
    form->addRow(QString(), crt_check_);

    silent_check_ = new QCheckBox(tr("Start muted (no audio output)"), tab);
    silent_check_->setToolTip(tr("Applies on next launch — there is no live audio-device toggle."));
    form->addRow(QString(), silent_check_);

    tape_fast_check_ = new QCheckBox(tr("Tape fast-load by default"), tab);
    form->addRow(QString(), tape_fast_check_);

    return tab;
}

QWidget* PreferencesDialog::build_input_tab() {
    auto* tab = new QWidget(this);
    auto* form = new QFormLayout(tab);

    auto make_source_combo = [&]() {
        auto* c = new QComboBox(tab);
        c->addItem(tr("SDL Gamepad"),        static_cast<int>(JoySource::Sdl));
        c->addItem(tr("Cursor Keys + Space"), static_cast<int>(JoySource::CursorKeys));
        return c;
    };
    joy1_source_combo_ = make_source_combo();
    joy2_source_combo_ = make_source_combo();
    form->addRow(tr("Joy 1 source (port 0x1F):"), joy1_source_combo_);
    form->addRow(tr("Joy 2 source (port 0x37):"), joy2_source_combo_);

    // The host cursor keys can drive only one connector. When one combo is set
    // to cursor keys, force the other back to SDL so the pair stays valid.
    auto enforce_one_cursor = [this](QComboBox* changed, QComboBox* other) {
        if (static_cast<JoySource>(changed->currentData().toInt()) == JoySource::CursorKeys &&
            static_cast<JoySource>(other->currentData().toInt()) == JoySource::CursorKeys) {
            other->setCurrentIndex(other->findData(static_cast<int>(JoySource::Sdl)));
        }
    };
    connect(joy1_source_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [=]() { enforce_one_cursor(joy1_source_combo_, joy2_source_combo_); });
    connect(joy2_source_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [=]() { enforce_one_cursor(joy2_source_combo_, joy1_source_combo_); });

    auto* note = new QLabel(
        tr("Cursor keys drive one connector's directions with Space as fire;\n"
           "while active, the arrows and Space stop acting as ZX keys."), tab);
    note->setWordWrap(true);
    form->addRow(QString(), note);

    return tab;
}

QWidget* PreferencesDialog::build_audio_tab() {
    auto* tab = new QWidget(this);
    auto* form = new QFormLayout(tab);

    auto add_gain = [this, tab, form](const QString& label, const QString& object_name,
                                      const QString& tooltip, QSlider*& slider) {
        auto* row = new QWidget(tab);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        slider = new QSlider(Qt::Horizontal, row);
        slider->setObjectName(object_name);
        slider->setRange(-24, 24);
        slider->setSingleStep(1);
        slider->setPageStep(6);
        slider->setTickPosition(QSlider::TicksBelow);
        slider->setTickInterval(6);
        slider->setToolTip(tooltip);
        auto* value = new QLabel(row);
        value->setObjectName(object_name + QStringLiteral("Value"));
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value->setMinimumWidth(
            value->fontMetrics().horizontalAdvance(QStringLiteral("-24 dB")));
        auto update_value = [value](int db) {
            value->setText(QStringLiteral("%1%2 dB")
                               .arg(db > 0 ? QStringLiteral("+") : QString())
                               .arg(db));
        };
        connect(slider, &QSlider::valueChanged, value, update_value);
        update_value(slider->value());
        h->addWidget(slider, 1);
        h->addWidget(value);
        form->addRow(label, row);
    };

    add_gain(tr("Master:"), QStringLiteral("audioGainDbSlider"),
             tr("Host output gain after the complete mix."),
             audio_gain_slider_);
    add_gain(tr("Beeper:"), QStringLiteral("audioGainBeeperDbSlider"),
             tr("Host gain for EAR, MIC and tape-EAR sound."),
             audio_beeper_gain_slider_);
    add_gain(tr("AY #0:"), QStringLiteral("audioGainAy0DbSlider"),
             tr("Host gain for TurboSound AY chip 0."), audio_ay_gain_slider_[0]);
    add_gain(tr("AY #1:"), QStringLiteral("audioGainAy1DbSlider"),
             tr("Host gain for TurboSound AY chip 1."), audio_ay_gain_slider_[1]);
    add_gain(tr("AY #2:"), QStringLiteral("audioGainAy2DbSlider"),
             tr("Host gain for TurboSound AY chip 2."), audio_ay_gain_slider_[2]);
    add_gain(tr("DAC:"), QStringLiteral("audioGainDacDbSlider"),
             tr("Host gain for Specdrum, Soundrive and Covox DAC audio."),
             audio_dac_gain_slider_);

    auto* note = new QLabel(
        tr("0 dB preserves each source. Subsystem gains are applied before the "
           "master gain; large boosts may clip."), tab);
    note->setWordWrap(true);
    form->addRow(QString(), note);

    return tab;
}

QWidget* PreferencesDialog::build_network_tab() {
    // GH #25 — the persistent form of --esp and --esp-allow. The two fields
    // were already loaded and saved by AppConfig; what was missing was any way
    // to see or set them, which also meant collect() silently reset them (see
    // the header comment).
    auto* tab = new QWidget(this);
    auto* form = new QFormLayout(tab);

    esp_enabled_check_ = new QCheckBox(tr("Enable the emulated ESP-01 WiFi module"), tab);
    esp_enabled_check_->setObjectName(QStringLiteral("espEnabledCheck"));
    esp_enabled_check_->setToolTip(
        tr("Lets guest software (NXtel, nextsync) open real TCP connections "
           "from your machine. Off by default."));
    form->addRow(QString(), esp_enabled_check_);

    esp_hosts_edit_ = new QPlainTextEdit(tab);
    esp_hosts_edit_->setObjectName(QStringLiteral("espAllowedHostsEdit"));
    esp_hosts_edit_->setPlaceholderText(tr("nx.nxtel.org"));
    esp_hosts_edit_->setToolTip(
        tr("One host per line (a comma separates too). Matching is exact and "
           "case-insensitive: no wildcards, and an IP address must be listed "
           "as itself."));
    // Four lines is enough for the handful of literal hosts this is for, and
    // keeps the tab the same height as the others.
    esp_hosts_edit_->setFixedHeight(4 * esp_hosts_edit_->fontMetrics().lineSpacing() + 12);
    form->addRow(tr("Allowed hosts:"), esp_hosts_edit_);

    // The allowlist is meaningless while the module is off, and showing it as
    // editable would imply otherwise. Kept in sync from here on; the initial
    // state is set with the rest of the fields in the constructor.
    connect(esp_enabled_check_, &QCheckBox::toggled,
            esp_hosts_edit_, &QPlainTextEdit::setEnabled);

    auto* note = new QLabel(
        tr("An EMPTY list places no restriction on the host name — the guest "
           "may name any host. Listing hosts narrows that.\n\n"
           "Connections to loopback, link-local and cloud-metadata addresses "
           "are always refused; your own LAN is reachable.\n\n"
           "This setting is not applied to the machine that is already "
           "running: it takes effect on the next hard reset (F1, or "
           "Machine ▸ Power Reset) and on the next launch. --no-esp overrides "
           "it for a single run."), tab);
    note->setWordWrap(true);
    form->addRow(QString(), note);

    return tab;
}

QWidget* PreferencesDialog::build_paths_tab() {
    auto* tab = new QWidget(this);
    auto* form = new QFormLayout(tab);

    auto add_path_row = [&](const QString& label, QLineEdit*& edit,
                             void (PreferencesDialog::*browse)(QLineEdit*)) {
        auto* row = new QWidget(tab);
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        edit = new QLineEdit(row);
        auto* browse_btn = new QPushButton(tr("Browse..."), row);
        h->addWidget(edit);
        h->addWidget(browse_btn);
        connect(browse_btn, &QPushButton::clicked, this, [this, edit, browse]() {
            (this->*browse)(edit);
        });
        form->addRow(label, row);
    };

    add_path_row(tr("Last load directory:"), last_load_dir_edit_,
                 &PreferencesDialog::browse_directory);
    add_path_row(tr("Default SD card image:"), sd_card_path_edit_,
                 &PreferencesDialog::browse_sd_image);
    add_path_row(tr("Screenshot directory:"), screenshot_dir_edit_,
                 &PreferencesDialog::browse_directory);

    return tab;
}

void PreferencesDialog::browse_directory(QLineEdit* target) {
    QString dir = QFileDialog::getExistingDirectory(this, tr("Select Directory"), target->text());
    if (!dir.isEmpty()) target->setText(dir);
}

void PreferencesDialog::browse_sd_image(QLineEdit* target) {
    QString path = QFileDialog::getOpenFileName(
        this, tr("Select SD Card Image"), target->text(),
        tr("Disk Images (*.img *.bin);;All Files (*)"));
    if (!path.isEmpty()) target->setText(path);
}

AppConfigData PreferencesDialog::collect() const {
    AppConfigData cfg;
    cfg.machine_type = static_cast<MachineType>(machine_combo_->currentData().toInt());
    cfg.cpu_speed    = static_cast<CpuSpeed>(cpu_speed_combo_->currentData().toInt());
    cfg.emulator_speed_percent = emu_speed_spin_->value();
    cfg.when_slow_prefer = static_cast<audio_pacing::WhenSlowPrefer>(
        when_slow_combo_->currentData().toInt());
    cfg.window_scale = scale_combo_->currentData().toInt();
    cfg.crt_filter   = crt_check_->isChecked();
    cfg.silent       = silent_check_->isChecked();
    cfg.tape_fast_load = tape_fast_check_->isChecked();
    cfg.audio_gain_db = static_cast<float>(audio_gain_slider_->value());
    cfg.audio_gain_beeper_db = static_cast<float>(audio_beeper_gain_slider_->value());
    for (int chip = 0; chip < 3; ++chip)
        cfg.audio_gain_ay_db[chip] =
            static_cast<float>(audio_ay_gain_slider_[chip]->value());
    cfg.audio_gain_dac_db = static_cast<float>(audio_dac_gain_slider_->value());
    cfg.joy_source[0] = static_cast<JoySource>(joy1_source_combo_->currentData().toInt());
    cfg.joy_source[1] = static_cast<JoySource>(joy2_source_combo_->currentData().toInt());
    cfg.esp_enabled = esp_enabled_check_->isChecked();
    {
        // Through EspHostPolicy::add, exactly as AppConfig::load() and the CLI
        // do: blanks (every stray newline in a text box is one) dropped,
        // duplicates collapsed. A security control must not mean three
        // different things depending on where the value was typed.
        //
        // A COMMA SEPARATES TOO, not just a newline. `allowed_hosts` in
        // ~/.jnext/jnext.conf is a comma-separated list (QSettings writes
        // string lists that way), so a user who has read that file — or the man
        // page's FILES section — will paste a comma-separated line in here.
        // Taking it literally would store one unmatchable host name and refuse
        // every connection while displaying what looks like a correct list. No
        // legal host name contains a comma, so splitting is unambiguous. The
        // CLI, where the option is one-host-per-occurrence, rejects a comma
        // loudly instead — see main.cpp's EspAllow case.
        EspHostPolicy policy;
        const QStringList entries = esp_hosts_edit_->toPlainText().split(
            QRegularExpression(QStringLiteral("[\n,]")));
        for (const QString& entry : entries) policy.add(entry.trimmed().toStdString());
        cfg.esp_allowed_hosts = policy.allowed_hosts;
    }
    cfg.last_load_dir  = last_load_dir_edit_->text();
    cfg.sd_card_path   = sd_card_path_edit_->text();
    cfg.screenshot_dir = screenshot_dir_edit_->text();
    return cfg;
}
