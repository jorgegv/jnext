#include "gui/preferences_dialog.h"
#include "gui/emulator_widget.h"

#include <QComboBox>
#include <QSpinBox>
#include <QSlider>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QDialogButtonBox>
#include <QFileDialog>

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
    tabs->addTab(build_paths_tab(), tr("Paths"));

    // --- Fill from `current` ---
    machine_combo_->setCurrentIndex(machine_combo_->findData(static_cast<int>(current.machine_type)));
    cpu_speed_combo_->setCurrentIndex(cpu_speed_combo_->findData(static_cast<int>(current.cpu_speed)));
    emu_speed_spin_->setValue(current.emulator_speed_percent);
    scale_combo_->setCurrentIndex(scale_combo_->findData(current.window_scale));
    crt_check_->setChecked(current.crt_filter);
    silent_check_->setChecked(current.silent);
    tape_fast_check_->setChecked(current.tape_fast_load);
    audio_gain_slider_->setValue(static_cast<int>(std::lround(current.audio_gain_db)));
    joy1_source_combo_->setCurrentIndex(
        joy1_source_combo_->findData(static_cast<int>(current.joy_source[0])));
    joy2_source_combo_->setCurrentIndex(
        joy2_source_combo_->findData(static_cast<int>(current.joy_source[1])));
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

    // Slider rather than a numeric field (PR #41 review): the range is
    // symmetric so the 0 dB default sits at the centre; the label beside it
    // carries the exact value.
    auto* row = new QWidget(tab);
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    audio_gain_slider_ = new QSlider(Qt::Horizontal, row);
    audio_gain_slider_->setObjectName(QStringLiteral("audioGainDbSlider"));
    audio_gain_slider_->setRange(-24, 24);
    audio_gain_slider_->setSingleStep(1);
    audio_gain_slider_->setPageStep(6);
    audio_gain_slider_->setTickPosition(QSlider::TicksBelow);
    audio_gain_slider_->setTickInterval(6);
    audio_gain_slider_->setToolTip(
        tr("Host output gain after the emulated hardware mix; applies live to playback and recordings."));
    auto* value = new QLabel(row);
    value->setObjectName(QStringLiteral("audioGainDbValue"));
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    value->setMinimumWidth(value->fontMetrics().horizontalAdvance(QStringLiteral("-24 dB")));
    auto update_value = [value](int db) {
        value->setText(QStringLiteral("%1%2 dB")
                           .arg(db > 0 ? QStringLiteral("+") : QString())
                           .arg(db));
    };
    connect(audio_gain_slider_, &QSlider::valueChanged, value, update_value);
    update_value(audio_gain_slider_->value());
    h->addWidget(audio_gain_slider_, 1);
    h->addWidget(value);
    form->addRow(tr("Output gain:"), row);

    auto* note = new QLabel(
        tr("0 dB preserves the emulated mix. Positive values boost quiet software; "
           "large boosts may clip."), tab);
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
    cfg.window_scale = scale_combo_->currentData().toInt();
    cfg.crt_filter   = crt_check_->isChecked();
    cfg.silent       = silent_check_->isChecked();
    cfg.tape_fast_load = tape_fast_check_->isChecked();
    cfg.audio_gain_db = static_cast<float>(audio_gain_slider_->value());
    cfg.joy_source[0] = static_cast<JoySource>(joy1_source_combo_->currentData().toInt());
    cfg.joy_source[1] = static_cast<JoySource>(joy2_source_combo_->currentData().toInt());
    cfg.last_load_dir  = last_load_dir_edit_->text();
    cfg.sd_card_path   = sd_card_path_edit_->text();
    cfg.screenshot_dir = screenshot_dir_edit_->text();
    return cfg;
}
