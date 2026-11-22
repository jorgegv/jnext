#include "debugger/audio_panel.h"
#include "debug/audio_math.h"
#include "debug/debugger.h"   // also AudioMute::* (via inspect.h)

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QGroupBox>
#include <QGridLayout>
#include <QPalette>
#include <QColor>

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

AudioPanel::AudioPanel(jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent)
    , dbg_(dbg)
{
    create_ui();
}

namespace {

QString note_text(const PsgNote& n) {
    return n.valid ? QString("%1%2").arg(n.name).arg(static_cast<int>(n.octave)) : QStringLiteral("-");
}

QLabel* mono_label(const QString& object_name, const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(object_name);
    QFont f("Monospace", 9);
    f.setStyleHint(QFont::Monospace);
    l->setFont(f);
    return l;
}

// A T / N gate indicator: lit when its R7 bit enables it. The state is the
// dynamic property `lit` and the label's colour.
void set_lit(QLabel* l, bool lit) {
    l->setProperty("lit", lit);
    QPalette pal = l->palette();
    pal.setColor(QPalette::WindowText, lit ? QColor(0x20, 0xC0, 0x20) : QColor(0x80, 0x80, 0x80));
    l->setPalette(pal);
    QFont f = l->font();
    f.setBold(lit);
    l->setFont(f);
}

}  // namespace

void AudioPanel::create_ui() {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    // --- Source Mute Controls (visible from every tab) ---
    auto* sources_box = new QGroupBox(tr("Sources"), this);
    auto* sources_layout = new QHBoxLayout(sources_box);
    sources_layout->setContentsMargins(4, 4, 4, 4);

    mute_ay0_ = new QCheckBox(tr("AY #0"), sources_box);
    mute_ay1_ = new QCheckBox(tr("AY #1"), sources_box);
    mute_ay2_ = new QCheckBox(tr("AY #2"), sources_box);
    mute_dac_ = new QCheckBox(tr("DAC"), sources_box);
    mute_beeper_ = new QCheckBox(tr("Beeper"), sources_box);

    // Seed the boxes FROM the machine (checked = audible), rather than
    // hardcoding "all checked". The panel is re-created whenever the debugger
    // window is rebuilt, and the mute mask deliberately outlives that (and a
    // machine reset); seeding from the live mask is what keeps the tick-marks
    // from lying about what is actually audible. On a fresh machine the mask is
    // AudioMute::NONE, so this still comes up all-checked.
    // Done BEFORE the connects below, so seeding does not itself emit toggled().
    const uint8_t mask = dbg_ ? dbg_->audio_mute_mask() : static_cast<uint8_t>(AudioMute::NONE);   // INS-10
    mute_ay0_->setChecked(!(mask & AudioMute::AY0));
    mute_ay1_->setChecked(!(mask & AudioMute::AY1));
    mute_ay2_->setChecked(!(mask & AudioMute::AY2));
    mute_dac_->setChecked(!(mask & AudioMute::DAC));
    mute_beeper_->setChecked(!(mask & AudioMute::BEEPER));

    // Task 48: these five boxes were pure decoration — there was no connect()
    // on any of them and nobody ever read isChecked(), so clicking them did
    // exactly nothing. Each toggle now recomputes the whole mask and pushes it
    // to the machine, which gates the corresponding output stage.
    for (QCheckBox* cb : {mute_ay0_, mute_ay1_, mute_ay2_, mute_dac_, mute_beeper_})
        connect(cb, &QCheckBox::toggled, this, &AudioPanel::apply_source_mutes);

    sources_layout->addWidget(mute_ay0_);
    sources_layout->addWidget(mute_ay1_);
    sources_layout->addWidget(mute_ay2_);
    sources_layout->addWidget(mute_dac_);
    sources_layout->addWidget(mute_beeper_);

    layout->addWidget(sources_box);

    // --- Sub-tabs ---
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName("audio_tabs");
    chips_scroll_ = new QScrollArea(tabs_);
    chips_scroll_->setWidgetResizable(true);
    chips_scroll_->setFrameShape(QFrame::NoFrame);
    chips_scroll_->setWidget(create_chips_tab());
    tabs_->addTab(chips_scroll_, tr("Chips"));
    tabs_->addTab(placeholder_tab(), tr("DAC && Beeper"));
    tabs_->addTab(placeholder_tab(), tr("Log"));
    tabs_->addTab(placeholder_tab(), tr("Tracker"));
    tabs_->addTab(placeholder_tab(), tr("Scopes"));
    layout->addWidget(tabs_, 1);
}

QWidget* AudioPanel::placeholder_tab() {
    auto* w = new QWidget(tabs_);
    auto* l = new QVBoxLayout(w);
    auto* label = new QLabel(tr("Not available yet."), w);
    label->setAlignment(Qt::AlignCenter);
    l->addWidget(label);
    l->addStretch(1);
    return w;
}

bool AudioPanel::chips_on_show() const {
    return tabs_ && tabs_->currentWidget() == chips_scroll_;
}

QWidget* AudioPanel::create_chips_tab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(4);

    // --- Global header: the three live signals, the PSG mode, the AY ports ---
    auto* info_box = new QGroupBox(tr("Info"), page);
    auto* info_layout = new QVBoxLayout(info_box);
    info_layout->setContentsMargins(4, 4, 4, 4);
    info_layout->setSpacing(2);

    turbosound_label_ = new QLabel(tr("TurboSound: No"), info_box);
    ay_ym_label_ = new QLabel(tr("Mode: AY"), info_box);
    stereo_label_ = new QLabel(tr("Stereo: ABC"), info_box);
    psg_mode_label_ = new QLabel(tr("PSG mode: YM"), info_box);
    ay_ports_label_ = new QLabel(tr("AY ports: on"), info_box);

    QFont info_font("Monospace", 9);
    info_font.setStyleHint(QFont::Monospace);
    for (QLabel* l : {turbosound_label_, ay_ym_label_, stereo_label_, psg_mode_label_, ay_ports_label_}) {
        l->setFont(info_font);
        info_layout->addWidget(l);
    }
    hex_box_ = new QCheckBox(tr("Hex"), info_box);
    hex_box_->setObjectName("audio_hex");
    info_layout->addWidget(hex_box_);
    layout->addWidget(info_box);

    // --- The decoded view: one group per chip ---
    decoded_ = new QWidget(page);
    auto* dl = new QVBoxLayout(decoded_);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->setSpacing(4);
    static const char* const kChan[3] = {"a", "b", "c"};
    for (int c = 0; c < 3; ++c) {
        ChipWidgets& w = chips_[c];
        const QString pre = QString("chip%1_").arg(c);
        auto* box = new QGroupBox(tr("AY #%1").arg(c), decoded_);
        auto* grid = new QGridLayout(box);
        grid->setContentsMargins(4, 4, 4, 4);
        grid->setHorizontalSpacing(12);
        grid->setVerticalSpacing(2);
        w.active = mono_label(pre + "active", "", box);
        w.addr = mono_label(pre + "addr", "0x00", box);
        w.pan = mono_label(pre + "pan", "L R", box);
        w.mono = mono_label(pre + "mono", "stereo", box);
        w.mix = mono_label(pre + "mix", "in mix", box);
        int col = 0;
        for (QLabel* l : {w.active, w.addr, w.pan, w.mono, w.mix}) grid->addWidget(l, 0, col++);
        for (int k = 0; k < 3; ++k) {
            ChannelWidgets& ch = w.ch[k];
            const QString cp = pre + kChan[k] + "_";
            const int row = 1 + k;
            ch.period = mono_label(cp + "period", "0", box);
            ch.note = mono_label(cp + "note", "-", box);
            ch.cents = mono_label(cp + "cents", "-", box);
            ch.tone = mono_label(cp + "tone", "T", box);
            ch.noise = mono_label(cp + "noise", "N", box);
            ch.vol = mono_label(cp + "vol", "0", box);
            ch.level = new QProgressBar(box);
            ch.level->setObjectName(cp + "level");
            ch.level->setRange(0, 255);
            ch.level->setTextVisible(false);
            ch.level->setMaximumHeight(10);
            set_lit(ch.tone, false);
            set_lit(ch.noise, false);
            grid->addWidget(mono_label(cp + "name", QString(QChar('A' + k)), box), row, 0);
            grid->addWidget(ch.period, row, 1);
            grid->addWidget(ch.note, row, 2);
            grid->addWidget(ch.cents, row, 3);
            grid->addWidget(ch.tone, row, 4);
            grid->addWidget(ch.noise, row, 5);
            grid->addWidget(ch.vol, row, 6);
            grid->addWidget(ch.level, row, 7);
        }
        w.noise_period = mono_label(pre + "noise_period", "Noise 0", box);
        w.env_glyph = mono_label(pre + "env_glyph", "", box);
        w.env_period = mono_label(pre + "env_period", "0", box);
        w.env_note = mono_label(pre + "env_note", "-", box);
        w.env_step = mono_label(pre + "env_step", "0 falling", box);
        grid->addWidget(w.noise_period, 4, 0, 1, 2);
        grid->addWidget(w.env_glyph, 4, 2);
        grid->addWidget(w.env_period, 4, 3);
        grid->addWidget(w.env_note, 4, 4);
        grid->addWidget(w.env_step, 4, 5, 1, 3);
        dl->addWidget(box);
    }
    layout->addWidget(decoded_);

    // --- The hex view: today's 16 x 3 register table ---
    ay_table_ = new QTableWidget(16, 3, page);
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

    // Disable scrollbars: the table is tall enough (the tab scrolls) to show all 16 rows.
    ay_table_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ay_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    ay_table_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    ay_table_->setFixedHeight(16 * 20 + ay_table_->horizontalHeader()->height() + 4);

    // Initialize cells
    for (int r = 0; r < 16; ++r) {
        for (int c = 0; c < 3; ++c) {
            auto* item = new QTableWidgetItem("00");
            item->setTextAlignment(Qt::AlignCenter);
            ay_table_->setItem(r, c, item);
        }
    }
    layout->addWidget(ay_table_);
    layout->addStretch(1);

    // Hex off (the default): the decoded view; on: the table. Both are always
    // refreshed, shown or not, so toggling never shows a stale view.
    ay_table_->setVisible(false);
    connect(hex_box_, &QCheckBox::toggled, this, [this](bool on) {
        ay_table_->setVisible(on);
        decoded_->setVisible(!on);
    });
    return page;
}

void AudioPanel::apply_source_mutes() {
    if (!dbg_)
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

    dbg_->set_audio_mute_mask(client_, mask);   // INS-10, attributed
}

void AudioPanel::refresh() {
    if (!dbg_)
        return;

    // One consistent read of the audio chain (INS-22).
    const jnext::dbg::AudioState st = dbg_->audio_state();

    // Hex table (INS-10 registers, the same bytes): filled whether it is shown or not.
    for (int chip = 0; chip < 3; ++chip) {
        for (int reg = 0; reg < 16; ++reg) {
            auto* item = ay_table_->item(reg, chip);
            if (item)
                item->setText(QString("%1").arg(st.chips[chip].regs[reg], 2, 16, QChar('0')).toUpper());
        }
    }

    // Decoded view. Notes at the machine's own PSG clock (never a literal: the
    // 128K's 1.7734 MHz plays the same period about 23 cents higher).
    for (int c = 0; c < 3; ++c) {
        const jnext::dbg::AyChipState& cs = st.chips[c];
        ChipWidgets& w = chips_[c];
        w.active->setText(st.active_chip == c ? tr("ACTIVE") : QString());
        w.addr->setText(QString::asprintf("0x%02X", cs.address));
        w.pan->setText(QString("%1 %2").arg((cs.pan & 2) ? "L" : "-").arg((cs.pan & 1) ? "R" : "-"));
        w.mono->setText(cs.mono ? tr("mono") : tr("stereo"));
        w.mix->setText(cs.reaches_mix ? tr("in mix") : tr("not in mix"));
        for (int k = 0; k < 3; ++k) {
            const jnext::dbg::AyChannelState& ch = cs.channels[k];
            ChannelWidgets& cw = w.ch[k];
            cw.period->setText(QString::number(ch.tone_period));
            const PsgNote n = psg_note(psg_tone_hz(ch.tone_period, st.psg_clock_hz));
            cw.note->setText(note_text(n));
            cw.cents->setText(n.valid ? QString::asprintf("%+.1f", n.cents) : QStringLiteral("-"));
            set_lit(cw.tone, ch.tone_enabled);
            set_lit(cw.noise, ch.noise_enabled);
            cw.vol->setText(ch.uses_envelope ? QStringLiteral("Env") : QString::number(ch.volume));
            cw.level->setValue(ch.amplitude);
        }
        w.noise_period->setText(QString("Noise %1").arg(cs.noise_period));
        const jnext::dbg::AyEnvelopeState& e = cs.envelope;
        w.env_glyph->setText(QString::fromLatin1(psg_envelope_glyph(e.shape)));
        w.env_period->setText(QString::number(e.period));
        const double ehz = psg_envelope_hz(e.period, e.shape, st.psg_clock_hz);
        w.env_note->setText(ehz > 0.0 ? note_text(psg_note(ehz)) : QStringLiteral("-"));
        const char* motion = e.motion == jnext::dbg::EnvelopeMotion::Holding ? "holding"
                           : e.motion == jnext::dbg::EnvelopeMotion::Rising  ? "rising" : "falling";
        w.env_step->setText(QString("%1 %2").arg(e.step).arg(motion));
    }

    // Update info labels
    turbosound_label_->setText(
        tr("TurboSound: %1").arg(st.turbosound ? tr("Yes") : tr("No")));

    // AY/YM mode. VHDL zxnext.vhd:6389 wires `aymode_i <= nr_06_psg_mode(0)`
    // (audio/ym2149.vhd:86 "0 = YM, 1 = AY") into the chip core; NR 0x06
    // bits 1:0 are a 2-bit field (nextreg.txt: 00=YM, 01=AY, 10=ZXN-8950,
    // 11=hold AY in reset) but only bit 0 drives chip mode. Read the
    // backend's ay_mode() (INS-10: TurboSound::ay_mode(),
    // src/audio/turbosound.h:57-58), which mirrors that live signal, instead
    // of re-decoding a raw NextREG byte: the previous code read NR 0x06 bit 4,
    // which is "Enable divmmc nmi by DRIVE button" (nextreg.txt) — an
    // unrelated field.
    ay_ym_label_->setText(tr("Mode: %1").arg(
        dbg_->ay_mode() == jnext::dbg::AyChipMode::Ay ? "AY" : "YM"));

    // Stereo mode. VHDL zxnext.vhd:5177 — `nr_08_psg_stereo_mode` is a
    // SINGLE bit (NR 0x08 bit 5), not a 2-bit field; audio/turbosound.vhd:45
    // -- "0 = ABC, 1 = ACB for all psg". The backend's value mirrors that live
    // signal. The previous code read bits 5:4 as a bogus 2-bit field (bit 4 is
    // nr_08_internal_speaker_en, VHDL zxnext.vhd:5178, unrelated to stereo)
    // against a 6-entry table that invented four stereo modes ZX Next
    // hardware does not have.
    stereo_label_->setText(tr("Stereo: %1").arg(
        st.stereo == jnext::dbg::StereoMode::Acb ? "ACB" : "ABC"));

    // The whole NR 0x06 psg-mode field (the "Mode:" line above is bit 0 only).
    static const char* const kPsgMode[4] = {"YM", "AY", "YM (ZXN-8950)", "held in reset"};
    psg_mode_label_->setText(tr("PSG mode: %1").arg(kPsgMode[st.psg_mode & 3]));
    ay_ports_label_->setText(tr("AY ports: %1").arg(st.ay_ports_enabled ? tr("on") : tr("off")));
}
