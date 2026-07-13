#include "debugger/audio_panel.h"
#include "core/emulator.h"
#include "audio/audio_mute.h"

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
    QFont header_font = ay_table_->verticalHeader()->font();
    header_font.setPointSize(8);
    ay_table_->verticalHeader()->setFont(header_font);

    // Compact row height
    ay_table_->verticalHeader()->setDefaultSectionSize(20);
    ay_table_->verticalHeader()->setMinimumSectionSize(18);
    ay_table_->verticalHeader()->setFixedWidth(140);
    ay_table_->horizontalHeader()->setStretchLastSection(false);
    ay_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ay_table_->setAlternatingRowColors(true);
    ay_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ay_table_->setSelectionMode(QAbstractItemView::NoSelection);

    // Disable scrollbars — the panel is tall enough to show all 16 rows.
    ay_table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ay_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ay_table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

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

    // Seed the boxes FROM the emulator (checked = audible), rather than
    // hardcoding "all checked". The panel is re-created whenever the debugger
    // window is rebuilt, and the mute mask deliberately outlives that (and a
    // machine reset); seeding from the live mask is what keeps the tick-marks
    // from lying about what is actually audible. On a fresh machine the mask is
    // AudioMute::NONE, so this still comes up all-checked.
    // Done BEFORE the connects below, so seeding does not itself emit toggled().
    const uint8_t mask = emulator_ ? emulator_->audio_mute_mask() : AudioMute::NONE;
    mute_ay0_->setChecked(!(mask & AudioMute::AY0));
    mute_ay1_->setChecked(!(mask & AudioMute::AY1));
    mute_ay2_->setChecked(!(mask & AudioMute::AY2));
    mute_dac_->setChecked(!(mask & AudioMute::DAC));
    mute_beeper_->setChecked(!(mask & AudioMute::BEEPER));

    // Task 48: these five boxes were pure decoration — there was no connect()
    // on any of them and nobody ever read isChecked(), so clicking them did
    // exactly nothing. Each toggle now recomputes the whole mask and pushes it
    // to the emulator, which gates the corresponding output stage.
    for (QCheckBox* cb : {mute_ay0_, mute_ay1_, mute_ay2_, mute_dac_, mute_beeper_})
        connect(cb, &QCheckBox::toggled, this, &AudioPanel::apply_source_mutes);

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

void AudioPanel::apply_source_mutes() {
    if (!emulator_)
        return;

    // Checked = audible, unchecked = muted, so the mask bit is the NEGATION of
    // the box. The emulator fans the mask out to TurboSound (AY chips) and the
    // Mixer (DAC / beeper) — see src/audio/audio_mute.h for why this cannot be
    // allowed to disturb anything the Z80 can observe.
    uint8_t mask = AudioMute::NONE;
    if (!mute_ay0_->isChecked())    mask |= AudioMute::AY0;
    if (!mute_ay1_->isChecked())    mask |= AudioMute::AY1;
    if (!mute_ay2_->isChecked())    mask |= AudioMute::AY2;
    if (!mute_dac_->isChecked())    mask |= AudioMute::DAC;
    if (!mute_beeper_->isChecked()) mask |= AudioMute::BEEPER;

    emulator_->set_audio_mute_mask(mask);
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

    // AY/YM mode. VHDL zxnext.vhd:6389 wires `aymode_i <= nr_06_psg_mode(0)`
    // (audio/ym2149.vhd:86 "0 = YM, 1 = AY") into the chip core; NR 0x06
    // bits 1:0 are a 2-bit field (nextreg.txt: 00=YM, 01=AY, 10=ZXN-8950,
    // 11=hold AY in reset) but only bit 0 drives chip mode. Read
    // TurboSound::ay_mode() (src/audio/turbosound.h:57-58), which mirrors
    // that live signal, instead of re-decoding a raw NextREG byte: the
    // previous code read NR 0x06 bit 4, which is "Enable divmmc nmi by
    // DRIVE button" (nextreg.txt) — an unrelated field.
    ay_ym_label_->setText(tr("Mode: %1").arg(ts.ay_mode() ? "AY" : "YM"));

    // Stereo mode. VHDL zxnext.vhd:5177 — `nr_08_psg_stereo_mode` is a
    // SINGLE bit (NR 0x08 bit 5), not a 2-bit field; audio/turbosound.vhd:45
    // — "0 = ABC, 1 = ACB for all psg". Read TurboSound::stereo_mode()
    // (src/audio/turbosound.h:46-48), which mirrors that live signal. The
    // previous code read bits 5:4 as a bogus 2-bit field (bit 4 is
    // nr_08_internal_speaker_en, VHDL zxnext.vhd:5178, unrelated to stereo)
    // against a 6-entry table that invented four stereo modes ZX Next
    // hardware does not have.
    stereo_label_->setText(tr("Stereo: %1").arg(ts.stereo_mode() ? "ACB" : "ABC"));
}
