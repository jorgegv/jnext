#include "debugger/audio_panel.h"
#include "core/emulator.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QGroupBox>

// AY register names for row headers
static const char* const kRegNames[16] = {
    "R0  Tone A Lo",
    "R1  Tone A Hi",
    "R2  Tone B Lo",
    "R3  Tone B Hi",
    "R4  Tone C Lo",
    "R5  Tone C Hi",
    "R6  Noise",
    "R7  Mixer",
    "R8  Vol A",
    "R9  Vol B",
    "R10 Vol C",
    "R11 Env Lo",
    "R12 Env Hi",
    "R13 Env Shape",
    "R14 I/O A",
    "R15 I/O B",
};

AudioPanel::AudioPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    create_ui();
}

void AudioPanel::create_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // --- AY Register Table ---
    ay_table_ = new QTableWidget(16, 3, this);
    ay_table_->setHorizontalHeaderLabels({"AY #0", "AY #1", "AY #2"});

    // Row headers with register names
    QStringList row_labels;
    for (int i = 0; i < 16; ++i)
        row_labels << kRegNames[i];
    ay_table_->setVerticalHeaderLabels(row_labels);

    // Monospace font for values
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);
    ay_table_->setFont(mono);

    // Compact row height
    ay_table_->verticalHeader()->setDefaultSectionSize(20);
    ay_table_->verticalHeader()->setMinimumSectionSize(18);
    ay_table_->horizontalHeader()->setStretchLastSection(true);
    ay_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ay_table_->setAlternatingRowColors(true);
    ay_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ay_table_->setSelectionMode(QAbstractItemView::NoSelection);

    // Initialize cells
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 3; ++c) {
            auto* item = new QTableWidgetItem("00");
            item->setTextAlignment(Qt::AlignCenter);
            ay_table_->setItem(r, c, item);
        }
    }

    layout->addWidget(ay_table_, 1);

    // --- Source Mute Controls ---
    auto* sources_box = new QGroupBox(tr("Sources"), this);
    auto* sources_layout = new QHBoxLayout(sources_box);
    sources_layout->setContentsMargins(4, 4, 4, 4);

    mute_ay0_ = new QCheckBox(tr("AY #0"), sources_box);
    mute_ay1_ = new QCheckBox(tr("AY #1"), sources_box);
    mute_ay2_ = new QCheckBox(tr("AY #2"), sources_box);
    mute_dac_ = new QCheckBox(tr("DAC"), sources_box);
    mute_beeper_ = new QCheckBox(tr("Beeper"), sources_box);

    // All checked (unmuted) by default
    mute_ay0_->setChecked(true);
    mute_ay1_->setChecked(true);
    mute_ay2_->setChecked(true);
    mute_dac_->setChecked(true);
    mute_beeper_->setChecked(true);

    sources_layout->addWidget(mute_ay0_);
    sources_layout->addWidget(mute_ay1_);
    sources_layout->addWidget(mute_ay2_);
    sources_layout->addWidget(mute_dac_);
    sources_layout->addWidget(mute_beeper_);

    layout->addWidget(sources_box);

    // --- Info Labels ---
    auto* info_box = new QGroupBox(tr("Info"), this);
    auto* info_layout = new QVBoxLayout(info_box);
    info_layout->setContentsMargins(4, 4, 4, 4);
    info_layout->setSpacing(2);

    turbosound_label_ = new QLabel(tr("TurboSound: No"), info_box);
    ay_ym_label_ = new QLabel(tr("Mode: AY"), info_box);
    stereo_label_ = new QLabel(tr("Stereo: ABC"), info_box);

    QFont info_font("Monospace", 9);
    info_font.setStyleHint(QFont::Monospace);
    turbosound_label_->setFont(info_font);
    ay_ym_label_->setFont(info_font);
    stereo_label_->setFont(info_font);

    info_layout->addWidget(turbosound_label_);
    info_layout->addWidget(ay_ym_label_);
    info_layout->addWidget(stereo_label_);

    layout->addWidget(info_box);
}

void AudioPanel::refresh() {
    if (!emulator_)
        return;

    const auto& ts = emulator_->turbosound();

    // Update AY register table
    for (int chip = 0; chip < 3; ++chip) {
        const auto& ay = ts.ay(chip);
        for (int reg = 0; reg < 16; ++reg) {
            uint8_t val = ay.read_register(reg);
            auto* item = ay_table_->item(reg, chip);
            if (item) {
                item->setText(QString("%1").arg(val, 2, 16, QChar('0')).toUpper());
            }
        }
    }

    // Update info labels
    bool ts_enabled = ts.enabled();
    turbosound_label_->setText(
        tr("TurboSound: %1").arg(ts_enabled ? tr("Yes") : tr("No")));

    // Read NextREG 0x06 bit 4 for AY/YM mode (0=YM, 1=AY)
    // and NextREG 0x08 for stereo/turbosound config
    uint8_t reg06 = emulator_->nextreg().cached(0x06);
    uint8_t reg08 = emulator_->nextreg().cached(0x08);

    bool ay_mode = (reg06 & 0x10) != 0; // bit 4: 1=AY, 0=YM
    ay_ym_label_->setText(tr("Mode: %1").arg(ay_mode ? "AY" : "YM"));

    // Stereo mode from reg 0x08 bits 5:4
    // 00=ABC, 01=ACB, 10=BAC, 11=BCA (approximate — actual mapping may vary)
    static const char* const stereo_names[] = {"ABC", "ACB", "BAC", "BCA", "CAB", "CBA"};
    uint8_t stereo_bits = (reg08 >> 4) & 0x03;
    const char* stereo_name = (stereo_bits < 6) ? stereo_names[stereo_bits] : "???";
    stereo_label_->setText(tr("Stereo: %1").arg(stereo_name));
}
