#include "debugger/audio_stream_view.h"

#include <QGroupBox>
#include <QHeaderView>
#include <QStringList>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>

#include "debug/dac_stream.h"
#include "debug/debugger.h"

using namespace jnext::dbg;

namespace {

constexpr int kPcRole = Qt::UserRole;   // a CPU row's writing instruction

QString rate_text(uint32_t millihz) {
    return millihz ? QString::number(millihz / 1000.0, 'f', 3) + QStringLiteral(" Hz") : QStringLiteral("-");
}

QString hex4(unsigned v) { return QString::asprintf("0x%04X", v); }

QString dma_addr(unsigned addr, bool io) {
    return (io ? QStringLiteral("port ") : QString()) + hex4(addr);
}

QTreeWidget* make_tree(const QString& name, const QStringList& header, QWidget* parent) {
    auto* t = new QTreeWidget(parent);
    t->setObjectName(name);
    t->setRootIsDecorated(false);
    t->setColumnCount(header.size());
    t->setHeaderLabels(header);
    t->setUniformRowHeights(true);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->header()->setStretchLastSection(true);
    return t;
}

QString channels_text(uint8_t mask) {
    QStringList l;
    for (int n = 0; n < 4; ++n) if (mask & (1u << n)) l << QString(QChar('A' + n));
    return l.join(' ');
}

QString writer_text(const DacWriterStats& w) {
    switch (w.source) {
    case EventSource::Cpu:      return QString("CPU %1").arg(hex4(w.pc));
    case EventSource::Dma:      return QStringLiteral("DMA");
    case EventSource::Copper:   return QStringLiteral("Copper");
    default:                    return QStringLiteral("Debugger");
    }
}

}  // namespace

AudioStreamView::AudioStreamView(Debugger* dbg, QWidget* parent)
    : QWidget(parent), dbg_(dbg)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);

    auto* pace_box = new QGroupBox(tr("Pacing (CTC, DMA)"), this);
    auto* pace_layout = new QVBoxLayout(pace_box);
    pace_layout->setContentsMargins(4, 4, 4, 4);
    pacing_ = make_tree("audio_pacing", {tr("Engine"), tr("Mode"), tr("Prescaler"), tr("TC"), tr("Count"),
                                         tr("State"), tr("Int"), tr("Rate")}, pace_box);
    for (int i = 0; i < 4; ++i) new QTreeWidgetItem(pacing_, QStringList(QString("CTC %1").arg(i)));
    new QTreeWidgetItem(pacing_, QStringList(QStringLiteral("DMA")));
    pacing_->setMaximumHeight(pacing_->header()->height() + 6 * 20);
    pace_layout->addWidget(pacing_);
    layout->addWidget(pace_box);

    auto* stream_box = new QGroupBox(tr("Sample stream"), this);
    auto* stream_layout = new QVBoxLayout(stream_box);
    stream_layout->setContentsMargins(4, 4, 4, 4);
    status_ = new QLabel(tr("Not recording"), stream_box);
    status_->setObjectName("audio_stream_status");
    window_ = new QLabel(stream_box);
    window_->setObjectName("audio_stream_window");
    window_->setText(tr("Window: 1.000 s (covered 0.000 s); a gap is an interval above %1 x the median")
                         .arg(DAC_STREAM_GAP_FACTOR, 0, 'f', 1));
    stream_ = make_tree("audio_stream", {tr("Writer"), tr("Channels"), tr("Samples"), tr("Rate"), tr("Min (us)"),
                                         tr("Avg (us)"), tr("Max (us)"), tr("Jitter (us)"), tr("Gaps"),
                                         tr("Pacing")}, stream_box);
    stream_->setMinimumHeight(80);
    stream_layout->addWidget(status_);
    stream_layout->addWidget(window_);
    stream_layout->addWidget(stream_);
    layout->addWidget(stream_box);

    connect(stream_, &QTreeWidget::itemDoubleClicked, this,
            [this](QTreeWidgetItem* item, int) { row_activated(item); });
}

void AudioStreamView::row_activated(QTreeWidgetItem* item) {
    const QVariant pc = item ? item->data(0, kPcRole) : QVariant();
    if (pc.isValid()) emit show_in_disassembly_requested(static_cast<uint16_t>(pc.toInt()));
}

void AudioStreamView::refresh() {
    if (!dbg_) return;

    // --- Pacing: two direct-value reads ---
    const std::array<CtcChannelState, CTC_CHANNEL_COUNT> ctc = dbg_->ctc_state();
    const DmaState dma = dbg_->dma_state();
    for (int i = 0; i < int(CTC_CHANNEL_COUNT); ++i) {
        const CtcChannelState& c = ctc[size_t(i)];
        QTreeWidgetItem* it = pacing_->topLevelItem(i);
        it->setText(1, c.counter_mode ? QString("counter <- CTC %1").arg((i + 3) & 3) : QStringLiteral("timer"));
        it->setText(2, c.counter_mode ? QStringLiteral("-") : QString::number(c.prescaler));
        it->setText(3, QString::number(c.time_constant));
        it->setText(4, QString::number(c.count));
        it->setText(5, c.running ? QStringLiteral("running") : QStringLiteral("stopped"));
        it->setText(6, c.int_enabled ? QStringLiteral("on") : QStringLiteral("off"));
        it->setText(7, rate_text(c.zc_rate_millihz));
    }
    {
        QTreeWidgetItem* it = pacing_->topLevelItem(int(CTC_CHANNEL_COUNT));
        static const char* const kMode[3] = {"byte", "continuous", "burst"};
        it->setText(1, dma.mode < 3 ? QString(kMode[dma.mode]) : QStringLiteral("mode 3"));
        it->setText(2, dma.prescaler ? QString::number(dma.prescaler) : QStringLiteral("-"));
        it->setText(3, QString("%1/%2").arg(dma.counter).arg(dma.length));
        it->setText(4, QString("%1 -> %2").arg(dma_addr(dma.src, dma.src_io), dma_addr(dma.dst, dma.dst_io)));
        it->setText(5, dma.active ? QStringLiteral("active") : QStringLiteral("idle"));
        it->setText(6, QStringLiteral("-"));
        it->setText(7, rate_text(dma.rate_millihz));
    }

    // --- Sample stream: the log, fetched incrementally ---
    auto r = dbg_->audio_log_entries(last_seq_ > 0 ? last_seq_ - 1 : 0);
    if (!r) {                                  // not recording: nothing on show is current any more
        cache_.clear();
        last_seq_ = 0;
        status_->setText(tr("Not recording"));
        stream_->clear();
        return;
    }
    status_->setText(tr("Recording"));
    std::vector<AudioLogEntry> v = std::move(r.value);
    size_t first = 0;
    if (last_seq_ > 0) {
        if (v.empty() || v.front().seq != last_seq_) {   // the ring overflowed, was rebuilt, or a rewind
            cache_.clear();                              // landing removed the newest entry: start over
            last_seq_ = 0;
            auto all = dbg_->audio_log_entries(0);
            v = all ? std::move(all.value) : std::vector<AudioLogEntry>{};
        } else {
            first = 1;
        }
    }
    for (size_t i = first; i < v.size(); ++i) {
        const AudioLogEntry& e = v[i];
        last_seq_ = e.seq;
        if (e.marker != AudioMarker::None || e.target == AudioTarget::Dac || e.target == AudioTarget::DacMirror)
            cache_.push_back(e);
    }

    // Trim: nothing before the newest marker matters, and nothing older than the window.
    const uint64_t end = dbg_->time().master_cycle;
    const uint64_t window = dac_stream_window_cycles();
    size_t newest_marker = 0;
    for (size_t i = 0; i < cache_.size(); ++i)
        if (cache_[i].marker != AudioMarker::None) newest_marker = i;
    cache_.erase(cache_.begin(), cache_.begin() + std::ptrdiff_t(newest_marker));
    if (end >= window) {                       // (end - window, end]; a strictly younger clock keeps everything
        const uint64_t start = end - window;
        cache_.erase(std::remove_if(cache_.begin(), cache_.end(),
                                    [&](const AudioLogEntry& e) { return e.marker == AudioMarker::None && e.cycle <= start; }),
                     cache_.end());
    }

    const DacStreamStats st = dac_stream_stats(cache_, end, window);
    stream_->clear();
    uint64_t oldest = end;
    for (const DacWriterStats& w : st.writers) {
        oldest = std::min(oldest, w.first_cycle);
        QString pacing = QStringLiteral("-");
        if (const auto m = nearest_pacing(w, ctc, dma)) {
            pacing = QString::asprintf("%s: %.3f Hz (%+.3f%%)", m->engine < 4 ? QString("CTC %1").arg(m->engine).toLatin1().constData() : "DMA",
                                       m->rate_millihz / 1000.0, m->rel_diff * 100.0);
        }
        auto* it = new QTreeWidgetItem(stream_);
        it->setText(0, writer_text(w));
        if (w.source == EventSource::Cpu) it->setData(0, kPcRole, int(w.pc));
        it->setText(1, channels_text(w.channels));
        it->setText(2, QString::number(w.samples));
        it->setText(3, w.rate_millihz ? QString::number(w.rate_millihz / 1000.0, 'f', 3) : QStringLiteral("-"));
        it->setText(4, QString::number(w.interval_min_us, 'f', 2));
        it->setText(5, QString::number(w.interval_mean_us, 'f', 2));
        it->setText(6, QString::number(w.interval_max_us, 'f', 2));
        it->setText(7, QString::number(w.jitter_us, 'f', 2));
        it->setText(8, QString::number(w.gaps));
        it->setText(9, pacing);
    }
    const double covered = st.writers.empty() ? 0.0 : double(std::min(window, end - oldest)) / double(window);
    // The window is one second of master cycles (dac_stream_window_cycles()).
    window_->setText(tr("Window: 1.000 s (covered %1 s); a gap is an interval above %2 x the median")
                         .arg(covered, 0, 'f', 3)
                         .arg(DAC_STREAM_GAP_FACTOR, 0, 'f', 1));
}
