#include "debugger/audio_panel.h"
#include "debugger/audio_log_view.h"
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
#include <QStringList>

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
    dac_scroll_ = new QScrollArea(tabs_);
    dac_scroll_->setWidgetResizable(true);
    dac_scroll_->setFrameShape(QFrame::NoFrame);
    dac_scroll_->setWidget(create_dac_tab());
    tabs_->addTab(dac_scroll_, tr("DAC && Beeper"));
    log_view_ = new AudioLogView(dbg_, tabs_);
    tabs_->addTab(log_view_, tr("Log"));
    connect(log_view_, &AudioLogView::show_in_disassembly_requested,
            this, &AudioPanel::show_in_disassembly_requested);
    connect(tabs_, &QTabWidget::currentChanged, this, [this](int) { emit sub_tab_changed(); });
    tabs_->addTab(placeholder_tab(), tr("Tracker"));
    tabs_->addTab(placeholder_tab(), tr("Scopes"));
    layout->addWidget(tabs_, 1);

    // The M and S buttons show the live channel mask (a panel rebuilt against a
    // muted machine must not claim the channels are audible).
    settle_channel_buttons(dbg_ ? dbg_->audio_channel_mute_mask() : static_cast<uint16_t>(AudioChannelMute::NONE));
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

QGroupBox* AudioPanel::create_dac_channel_group() {
    auto* box = new QGroupBox(tr("DAC channels"));
    auto* grid = new QGridLayout(box);
    grid->setContentsMargins(4, 4, 4, 4);
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(2);
    static const char* const kDac[4] = {"a", "b", "c", "d"};
    for (int d = 0; d < 4; ++d) {
        grid->addWidget(new QLabel(QString(QChar('A' + d)), box), d, 0);
        add_channel_buttons(grid, d, 1, 9 + d, QString("dac_") + kDac[d]);
    }
    grid->setColumnStretch(3, 1);
    return box;
}

void AudioPanel::add_channel_buttons(QGridLayout* grid, int row, int col, int index, const QString& stem) {
    QWidget* parent = grid->parentWidget();
    auto make = [&](const char* text, const QString& name, const QString& tip) {
        auto* b = new QToolButton(parent);
        b->setObjectName(name);
        b->setText(QString::fromLatin1(text));
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setAutoRaise(false);
        return b;
    };
    QToolButton* m = make("M", stem + "_mute", tr("Mute this channel"));
    QToolButton* s = make("S", stem + "_solo", tr("Solo this channel (mute the other twelve)"));
    mute_btn_[index] = m;
    solo_btn_[index] = s;
    // `clicked`, not `toggled`: setChecked() from a refresh must not push.
    connect(m, &QToolButton::clicked, this, [this, index] { channel_mute_clicked(index); });
    connect(s, &QToolButton::clicked, this, [this, index] { channel_solo_clicked(index); });
    grid->addWidget(m, row, col);
    grid->addWidget(s, row, col + 1);
}

void AudioPanel::settle_channel_buttons(uint16_t mask) {
    // A solo is the mask "everything but that channel". Any other mask means
    // something else changed the mutes (a Mute click, a script, another
    // client): the solo is over, and restoring the older mask later would undo
    // that change.
    if (solo_ >= 0 && mask != (AudioChannelMute::ALL & ~(1u << solo_)))
        solo_ = -1;
    for (int i = 0; i < 13; ++i) {
        if (mute_btn_[i]) mute_btn_[i]->setChecked(((mask >> i) & 1) != 0);
        if (solo_btn_[i]) solo_btn_[i]->setChecked(solo_ == i);
    }
}

void AudioPanel::channel_mute_clicked(int index) {
    if (!dbg_)
        return;
    const uint16_t live = dbg_->audio_channel_mute_mask();
    settle_channel_buttons(live);   // a stale solo ends before this change
    dbg_->set_audio_channel_mute_mask(client_, static_cast<uint16_t>(live ^ (1u << index)));
    settle_channel_buttons(dbg_->audio_channel_mute_mask());
}

void AudioPanel::channel_solo_clicked(int index) {
    if (!dbg_)
        return;
    const uint16_t live = dbg_->audio_channel_mute_mask();
    settle_channel_buttons(live);
    if (solo_ == index) {
        // Un-solo: back to the mask the first solo found.
        solo_ = -1;
        dbg_->set_audio_channel_mute_mask(client_, pre_solo_mask_);
    } else {
        // Switching from one solo to another keeps the first saved mask.
        if (solo_ < 0)
            pre_solo_mask_ = live;
        solo_ = index;
        dbg_->set_audio_channel_mute_mask(client_, static_cast<uint16_t>(AudioChannelMute::ALL & ~(1u << index)));
    }
    settle_channel_buttons(dbg_->audio_channel_mute_mask());
}

bool AudioPanel::chips_on_show() const {
    return tabs_ && tabs_->currentWidget() == chips_scroll_;
}

bool AudioPanel::log_on_show() const {
    return tabs_ && log_view_ && tabs_->currentWidget() == log_view_;
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
    // The AY dropped-write total, with a link to the table (DAC & Beeper tab).
    ay_drops_label_ = new QLabel(tr("AY writes dropped: 0 (<a href=\"drops\">show</a>)"), info_box);
    ay_drops_label_->setObjectName("ay_drops");
    ay_drops_label_->setTextFormat(Qt::RichText);
    ay_drops_label_->setTextInteractionFlags(Qt::LinksAccessibleByMouse);
    connect(ay_drops_label_, &QLabel::linkActivated, this, [this](const QString&) {
        tabs_->setCurrentWidget(dac_scroll_);
    });

    QFont info_font("Monospace", 9);
    info_font.setStyleHint(QFont::Monospace);
    for (QLabel* l : {turbosound_label_, ay_ym_label_, stereo_label_, psg_mode_label_, ay_ports_label_, ay_drops_label_}) {
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
            add_channel_buttons(grid, row, 8, 3 * c + k, QString("chip%1_%2").arg(c).arg(kChan[k]));
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

    // The mutes can change under the panel (a script's `set AUDIO_MUTE` or
    // `set AUDIO_CHANNEL_MUTE`, another client): the Sources boxes and the M / S
    // buttons follow the live masks. The boxes are re-seeded with their signals
    // blocked, so this pushes nothing and logs no MUTATE line.
    const struct { QCheckBox* box; uint8_t bit; } sources[5] = {
        {mute_ay0_, AudioMute::AY0}, {mute_ay1_, AudioMute::AY1}, {mute_ay2_, AudioMute::AY2},
        {mute_dac_, AudioMute::DAC}, {mute_beeper_, AudioMute::BEEPER},
    };
    for (const auto& s : sources) {
        const QSignalBlocker blocker(s.box);
        s.box->setChecked(!(st.host.mute_mask & s.bit));
    }
    settle_channel_buttons(st.host.channel_mute_mask);

    uint64_t ay_dropped = 0;
    for (size_t r = size_t(jnext::dbg::AudioDropReason::AyPortDisabled);
         r <= size_t(jnext::dbg::AudioDropReason::AyAddressHigh); ++r)
        ay_dropped += st.drops[r].count;
    ay_drops_label_->setText(
        tr("AY writes dropped: %1 (<a href=\"drops\">show</a>)").arg(ay_dropped));

    refresh_dac_(st);

    // The Log tab reads its own verb, and only while it is the tab on show.
    if (log_on_show()) log_view_->refresh();
}

namespace {

QLabel* plain_label(const QString& object_name, const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setObjectName(object_name);
    return l;
}

}  // namespace

QWidget* AudioPanel::create_dac_tab() {
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(4);

    // --- DAC: the four channels (the raw byte, so silence 0x80 is mid-bar) ---
    auto* dac_box = new QGroupBox(tr("DAC"), page);
    auto* dac_grid = new QGridLayout(dac_box);
    dac_grid->setContentsMargins(4, 4, 4, 4);
    dac_enabled_ = mono_label("dac_enabled", tr("DAC: off"), dac_box);
    dac_grid->addWidget(dac_enabled_, 0, 0, 1, 3);
    static const char* const kDacName[4] = {"a", "b", "c", "d"};
    for (int i = 0; i < 4; ++i) {
        const QString pre = QString("dac_%1_").arg(kDacName[i]);
        dac_grid->addWidget(mono_label(pre + "name", QString(QChar('A' + i)), dac_box), 1 + i, 0);
        dac_value_[i] = mono_label(QString("dac_%1_value").arg(kDacName[i]), "0x80", dac_box);
        dac_bar_[i] = new QProgressBar(dac_box);
        dac_bar_[i]->setObjectName(QString("dac_%1_bar").arg(kDacName[i]));
        dac_bar_[i]->setRange(0, 255);
        dac_bar_[i]->setTextVisible(false);
        dac_bar_[i]->setMaximumHeight(10);
        dac_grid->addWidget(dac_value_[i], 1 + i, 1);
        dac_grid->addWidget(dac_bar_[i], 1 + i, 2);
    }
    layout->addWidget(dac_box);
    layout->addWidget(create_dac_channel_group());   // slot: dac_channels (#533 Mute/Solo)

    // --- Port families (NR 0x84) ---
    auto* fam_box = new QGroupBox(tr("Port families (NR 0x84)"), page);
    auto* fam_layout = new QVBoxLayout(fam_box);
    fam_layout->setContentsMargins(4, 4, 4, 4);
    fam_layout->setSpacing(2);
    static const char* const kFamily[7] = {
        "Soundrive 1: 1F 0F 4F 5F", "Soundrive 2: F1 F3 F9 FB", "Profi Covox: 3F 5F",
        "Covox: 0F 4F", "Pentagon/ATM: FB", "GS Covox: B3", "Specdrum: DF"};
    for (int n = 1; n <= 7; ++n) {
        dac_family_[n] = mono_label(QString("dac_family_%1").arg(n), tr(kFamily[n - 1]), fam_box);
        set_lit(dac_family_[n], false);
        fam_layout->addWidget(dac_family_[n]);
    }
    layout->addWidget(fam_box);

    // --- Beeper ---
    auto* beep_box = new QGroupBox(tr("Beeper"), page);
    auto* beep_layout = new QVBoxLayout(beep_box);
    beep_layout->setContentsMargins(4, 4, 4, 4);
    beep_layout->setSpacing(2);
    beeper_ear_ = mono_label("beeper_ear", tr("EAR"), beep_box);
    beeper_mic_ = mono_label("beeper_mic", tr("MIC"), beep_box);
    beeper_tape_ear_ = mono_label("beeper_tape_ear", tr("Tape EAR"), beep_box);
    auto* lamps = new QHBoxLayout;
    for (QLabel* l : {beeper_ear_, beeper_mic_, beeper_tape_ear_}) {
        set_lit(l, false);
        lamps->addWidget(l);
    }
    lamps->addStretch(1);
    beep_layout->addLayout(lamps);
    speaker_enabled_ = mono_label("speaker_enabled", tr("Internal speaker: off"), beep_box);
    beep_speaker_only_ = mono_label("beep_speaker_only", tr("Beep to speaker only: off"), beep_box);
    beeper_excluded_ = mono_label("beeper_excluded", tr("EAR/MIC reach the mix: yes"), beep_box);
    for (QLabel* l : {speaker_enabled_, beep_speaker_only_, beeper_excluded_}) beep_layout->addWidget(l);
    layout->addWidget(beep_box);

    // --- Pi I2S ---
    auto* pi_box = new QGroupBox(tr("Pi I2S"), page);
    auto* pi_layout = new QHBoxLayout(pi_box);
    pi_layout->setContentsMargins(4, 4, 4, 4);
    pi_left_ = mono_label("pi_left", tr("Left"), pi_box);
    pi_right_ = mono_label("pi_right", tr("Right"), pi_box);
    pi_ear_ = mono_label("pi_ear", tr("Pi to EAR: off"), pi_box);
    set_lit(pi_left_, false);
    set_lit(pi_right_, false);
    for (QLabel* l : {pi_left_, pi_right_, pi_ear_}) pi_layout->addWidget(l);
    pi_layout->addStretch(1);
    layout->addWidget(pi_box);

    // --- Host settings ---
    auto* host_box = new QGroupBox(tr("Host"), page);
    auto* host_grid = new QGridLayout(host_box);
    host_grid->setContentsMargins(4, 4, 4, 4);
    static const char* const kGainName[6] = {"Output", "AY #0", "AY #1", "AY #2", "DAC", "Beeper"};
    static const char* const kGainObj[6] = {"gain_output", "gain_ay0", "gain_ay1", "gain_ay2", "gain_dac", "gain_beeper"};
    for (int i = 0; i < 6; ++i) {
        gain_[i] = mono_label(kGainObj[i], "+0.0 dB", host_box);
        host_grid->addWidget(plain_label(QString("%1_name").arg(kGainObj[i]), tr(kGainName[i]), host_box), i / 3, (i % 3) * 2);
        host_grid->addWidget(gain_[i], i / 3, (i % 3) * 2 + 1);
    }
    host_mutes_ = mono_label("host_mutes", tr("Muted: none"), host_box);
    host_synthesis_ = mono_label("host_synthesis", tr("Synthesis: on"), host_box);
    host_grid->addWidget(host_mutes_, 2, 0, 1, 6);
    host_grid->addWidget(host_synthesis_, 3, 0, 1, 6);
    layout->addWidget(host_box);

    // --- Dropped writes: one fixed row per reason (a QTreeWidget: the hex table
    //     is the panel's only QTableWidget) ---
    auto* drops_box = new QGroupBox(tr("Dropped writes"), page);
    auto* drops_layout = new QVBoxLayout(drops_box);
    drops_layout->setContentsMargins(4, 4, 4, 4);
    drops_tree_ = new QTreeWidget(drops_box);
    drops_tree_->setObjectName("audio_drops");
    drops_tree_->setRootIsDecorated(false);
    drops_tree_->setColumnCount(7);
    drops_tree_->setHeaderLabels({tr("Reason"), tr("Count"), tr("Port"), tr("Value"), tr("PC"), tr("Frame"), tr("Source")});
    drops_tree_->setSelectionMode(QAbstractItemView::NoSelection);
    drops_tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    drops_tree_->header()->setStretchLastSection(false);
    drops_tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);
    drops_tree_->setFont(mono);
    for (int i = 0; i < 8; ++i) {
        auto* item = new QTreeWidgetItem(drops_tree_);
        item->setText(0, audio_drop_reason_text(static_cast<jnext::dbg::AudioDropReason>(i + 1)));
        item->setText(1, "0");
        for (int col = 2; col < 7; ++col) item->setText(col, "-");
        item->setData(0, Qt::UserRole, -1);
    }
    drops_tree_->setFixedHeight(9 * 22 + 8);
    // A CPU-source row names the instruction that wrote: show it.
    connect(drops_tree_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) {
        const int pc = item->data(0, Qt::UserRole).toInt();
        if (pc >= 0) emit show_in_disassembly_requested(static_cast<uint16_t>(pc));
    });
    drops_layout->addWidget(drops_tree_);
    auto* clear = new QPushButton(tr("Clear"), drops_box);
    clear->setObjectName("audio_drops_clear");
    connect(clear, &QPushButton::clicked, this, [this] {
        if (!dbg_) return;
        dbg_->clear_audio_drops(client_);
        refresh();
    });
    drops_layout->addWidget(clear);
    layout->addWidget(drops_box);
    layout->addStretch(1);
    return page;
}

void AudioPanel::refresh_dac_(const jnext::dbg::AudioState& st) {
    dac_enabled_->setText(st.dac.enabled ? tr("DAC: on") : tr("DAC: off"));
    for (int i = 0; i < 4; ++i) {
        dac_value_[i]->setText(QString::asprintf("0x%02X", st.dac.channels[i]));
        dac_bar_[i]->setValue(st.dac.channels[i]);
    }
    // Lamp n = bit n of NR 0x84. Pentagon/ATM (b5) works only while Soundrive 2
    // (b2) is off: zxnext.vhd:2749.
    for (int n = 1; n <= 7; ++n) {
        bool lit = ((st.dac.port_families >> n) & 1) != 0;
        if (n == 5) lit = lit && !((st.dac.port_families >> 2) & 1);
        set_lit(dac_family_[n], lit);
    }

    set_lit(beeper_ear_, st.beeper.ear);
    set_lit(beeper_mic_, st.beeper.mic);
    set_lit(beeper_tape_ear_, st.beeper.tape_ear);
    speaker_enabled_->setText(tr("Internal speaker: %1").arg(st.beeper.speaker_enabled ? tr("on") : tr("off")));
    beep_speaker_only_->setText(tr("Beep to speaker only: %1").arg(st.beeper.beep_speaker_only ? tr("on") : tr("off")));
    beeper_excluded_->setText(tr("EAR/MIC reach the mix: %1").arg(st.beeper.excluded ? tr("no") : tr("yes")));

    set_lit(pi_left_, st.pi.left_enabled);
    set_lit(pi_right_, st.pi.right_enabled);
    pi_ear_->setText(tr("Pi to EAR: %1").arg(st.pi.to_ear ? tr("on") : tr("off")));

    const float g[6] = {st.host.output_gain_db, st.host.ay_gain_db[0], st.host.ay_gain_db[1],
                        st.host.ay_gain_db[2], st.host.dac_gain_db, st.host.beeper_gain_db};
    for (int i = 0; i < 6; ++i) gain_[i]->setText(QString::asprintf("%+.1f dB", static_cast<double>(g[i])));
    QStringList muted;
    static const struct { uint8_t bit; const char* name; } kMute[5] = {
        {AudioMute::AY0, "AY #0"}, {AudioMute::AY1, "AY #1"}, {AudioMute::AY2, "AY #2"},
        {AudioMute::DAC, "DAC"}, {AudioMute::BEEPER, "Beeper"}};
    for (const auto& m : kMute)
        if (st.host.mute_mask & m.bit) muted << tr(m.name);
    // The channel mutes (AUDIO_CHANNEL_MUTE): AY channels by chip and letter, DAC channels by letter.
    static const char* const kLetter[3] = {"A", "B", "C"};
    for (int i = 0; i < 9; ++i)
        if ((st.host.channel_mute_mask >> i) & 1)
            muted << tr("AY #%1 %2").arg(i / 3).arg(kLetter[i % 3]);
    for (int d = 0; d < 4; ++d)
        if ((st.host.channel_mute_mask >> (9 + d)) & 1)
            muted << tr("DAC %1").arg(QChar('A' + d));
    host_mutes_->setText(muted.isEmpty() ? tr("Muted: none") : tr("Muted: %1").arg(muted.join(", ")));
    host_synthesis_->setText(st.host.synthesis_on ? tr("Synthesis: on") : tr("Synthesis: off (--silent)"));

    for (int i = 0; i < 8; ++i) {
        const auto reason = static_cast<jnext::dbg::AudioDropReason>(i + 1);
        const jnext::dbg::AudioDropCounter& d = st.drops[static_cast<size_t>(reason)];
        QTreeWidgetItem* item = drops_tree_->topLevelItem(i);
        item->setText(1, QString::number(d.count));
        if (d.count == 0) {
            for (int col = 2; col < 7; ++col) item->setText(col, "-");
            item->setData(0, Qt::UserRole, -1);
            continue;
        }
        const bool cpu = d.last_source == jnext::dbg::EventSource::Cpu;
        item->setText(2, reason == jnext::dbg::AudioDropReason::DacMirrorDisabled
                             ? QString::asprintf("NR 0x%02X", d.last_port & 0xFF)
                             : QString::asprintf("0x%04X", d.last_port));
        item->setText(3, QString::asprintf("0x%02X", d.last_value));
        // The slot's PC names the writer only for a CPU write (a DMA, Copper or
        // debugger write is not made by that instruction).
        item->setText(4, cpu ? QString::asprintf("0x%04X", d.last_pc) : QStringLiteral("-"));
        item->setText(5, QString::number(d.last_frame));
        item->setText(6, tr(audio_source_text(d.last_source)));
        item->setData(0, Qt::UserRole, cpu ? static_cast<int>(d.last_pc) : -1);
    }
}
