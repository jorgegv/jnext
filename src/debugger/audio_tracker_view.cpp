#include "debugger/audio_tracker_view.h"

#include "debug/audio_math.h"
#include "debug/debugger.h"
#include "debugger/audio_log_view.h"

#include <algorithm>
#include <cstring>

#include <QBrush>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPainter>
#include <QPalette>
#include <QScrollBar>
#include <QVBoxLayout>

using jnext::dbg::AudioFrame;
using jnext::dbg::AudioLogEntry;
using jnext::dbg::AudioMarker;
using jnext::dbg::Debugger;
using jnext::dbg::Result;

namespace {

constexpr int kEnvLo = 11, kEnvHi = 12, kShape = 13, kNoiseReg = 6, kMixerReg = 7;

int16_t masked(int16_t v, int mask) { return v < 0 ? int16_t(-1) : int16_t(v & mask); }

bool changed_masked(const AudioFrame& f, size_t chip, size_t reg, int mask)
{
    return masked(f.after(chip, reg), mask) != masked(f.before[chip][reg], mask);
}

bool written(const AudioFrame& f, size_t chip, int reg) { return (f.written_mask[chip] >> reg) & 1; }

bool period_changed(const AudioFrame& f, size_t chip, int ch)
{
    return changed_masked(f, chip, size_t(2 * ch), 0xFF) || changed_masked(f, chip, size_t(2 * ch + 1), 0x0F);
}

/// The noise generator of channel `ch` is on after this frame: R7 known and its enable bit (3 + ch,
/// active low) clear (ym2149.vhd:469-471).
bool noise_on(const AudioFrame& f, size_t chip, int ch)
{
    const int16_t m = f.after(chip, kMixerReg);
    return m >= 0 && !((m >> (3 + ch)) & 1);
}

bool noise_changed(const AudioFrame& f, size_t chip, int ch)
{
    const int16_t a = f.after(chip, kMixerReg), b = f.before[chip][kMixerReg];
    if (a >= 0 && b >= 0 && (((a ^ b) >> (3 + ch)) & 1)) return true;
    return noise_on(f, chip, ch) && changed_masked(f, chip, kNoiseReg, 0x1F);
}

QString hex(int v, int digits) { return QString::number(v, 16).toUpper().rightJustified(digits, '0'); }

QString pc_range(uint16_t lo, uint16_t hi)
{
    if (lo > hi) return QStringLiteral("none (no CPU write)");
    if (lo == hi) return "0x" + hex(lo, 4);
    return "0x" + hex(lo, 4) + "-0x" + hex(hi, 4);
}

QString avg1(double v) { return QString::number(v, 'f', 1); }

QString triple(const jnext::dbg::AudioTimingRange& r)
{
    return QStringLiteral("%1 / %2 / %3").arg(r.min).arg(avg1(r.avg)).arg(r.max);
}

}  // namespace

// ── The model ───────────────────────────────────────────────────────────

int AudioTrackerModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(rows_.size());
}

int AudioTrackerModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : kColumnCount;
}

QVariant AudioTrackerModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole || section < 0 || section >= kColumnCount)
        return {};
    if (section == 0) return QStringLiteral("Frame");
    if (section == 1) return QStringLiteral("Mark");
    const int c = (section - 2) / kChipColumns, i = (section - 2) % kChipColumns;
    if (i == 0) return QStringLiteral("%1 Env").arg(c);
    if (i == 1) return QStringLiteral("%1 Sh").arg(c);
    const QChar letter = QChar('A' + (i - 2) / 3);
    static const char* const kind[] = {"Note", "V", "N"};
    return QStringLiteral("%1%2 %3").arg(c).arg(letter).arg(kind[(i - 2) % 3]);
}

uint32_t AudioTrackerModel::frame_row_frame(int row) const
{
    if (row < 0 || row >= int(rows_.size())) return 0;
    const Row& r = rows_[size_t(row)];
    switch (r.kind) {
    case Row::FrameRow:  return r.frame.frame;
    case Row::EmptyRun:  return r.first;
    case Row::MarkerRow: return 0;
    }
    return 0;
}

const AudioFrame* AudioTrackerModel::frame_at(int row) const
{
    if (row < 0 || row >= int(rows_.size()) || rows_[size_t(row)].kind != Row::FrameRow) return nullptr;
    return &rows_[size_t(row)].frame;
}

std::vector<const AudioFrame*> AudioTrackerModel::timing_frames(uint32_t window) const
{
    std::vector<const AudioFrame*> out;
    if (window == 0 || tail_n_ > rows_.size()) return out;
    bool have_newest = false;
    uint32_t start = 0;
    for (size_t i = rows_.size() - tail_n_; i-- > 0;) {
        const Row& r = rows_[i];
        if (r.timeline != timeline_) break;                          // before a reset or a state load
        if (r.kind != Row::FrameRow) continue;                       // an empty run, or a rewind landing
        if (!have_newest) {
            have_newest = true;
            start = r.frame.frame >= window - 1 ? r.frame.frame - (window - 1) : 0;
        } else if (r.frame.frame < start) {
            break;
        }
        out.push_back(&r.frame);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

QString AudioTrackerModel::timing_text(int row) const
{
    if (row < 0 || row >= int(rows_.size())) return {};
    const Row& r = rows_[size_t(row)];
    switch (r.kind) {
    case Row::MarkerRow: return {};
    case Row::EmptyRun:
        return r.first == r.last ? QStringLiteral("Frame %1: no AY write").arg(r.first)
                                 : QStringLiteral("Frames %1-%2: no AY write").arg(r.first).arg(r.last);
    case Row::FrameRow: {
        const AudioFrame& f = r.frame;
        return QStringLiteral("Frame %1: first write line %2, T-state %3; last write line %4, T-state %5; writes %6; PC %7")
            .arg(f.frame).arg(f.first.vc).arg(f.first.tstate).arg(f.last.vc).arg(f.last.tstate).arg(f.writes)
            .arg(pc_range(f.pc_min, f.pc_max));
    }
    }
    return {};
}

QString AudioTrackerModel::cell_text(const Row& r, int col) const
{
    if (col == 0) {
        switch (r.kind) {
        case Row::FrameRow:  return QString::number(r.frame.frame);
        case Row::EmptyRun:  return r.first == r.last ? QString::number(r.first)
                                                      : QStringLiteral("%1-%2").arg(r.first).arg(r.last);
        case Row::MarkerRow: return {};
        }
    }
    if (col == 1) {
        switch (r.kind) {
        case Row::FrameRow:
            for (uint16_t m : r.frame.double_mask) if (m) return QStringLiteral("double");
            return {};
        case Row::EmptyRun:
            return r.first == r.last ? QStringLiteral("empty")
                                     : QStringLiteral("empty x%1").arg(r.last - r.first + 1);
        case Row::MarkerRow:
            switch (r.marker) {
            case AudioMarker::Reset:     return QStringLiteral("reset");
            case AudioMarker::StateLoad: return QStringLiteral("state load");
            case AudioMarker::Rewind:    return QStringLiteral("rewind");
            case AudioMarker::None:      break;
            }
            return {};
        }
    }
    if (r.kind == Row::MarkerRow) return {};

    const size_t c = size_t((col - 2) / kChipColumns);
    const int i = (col - 2) % kChipColumns;
    const AudioFrame& f = r.frame;
    const bool frame_row = r.kind == Row::FrameRow;
    if (i == 0) {                                                    // Env
        if (!frame_row || !(written(f, c, kEnvLo) || written(f, c, kEnvHi))) return QStringLiteral("....");
        const int lo = f.after(c, kEnvLo), hi = f.after(c, kEnvHi);
        return lo < 0 || hi < 0 ? QStringLiteral("????") : hex(hi << 8 | lo, 4);
    }
    if (i == 1) {                                                    // Sh
        if (!frame_row || !written(f, c, kShape)) return QStringLiteral(".");
        return hex(f.after(c, kShape) & 0x0F, 1);
    }
    const int ch = (i - 2) / 3;
    switch ((i - 2) % 3) {
    case 0: {                                                        // Note
        if (!frame_row || !period_changed(f, c, ch)) return QStringLiteral("---");
        const int lo = f.after(c, size_t(2 * ch)), hi = f.after(c, size_t(2 * ch + 1));
        if (lo < 0 || hi < 0) return QStringLiteral("???");
        const PsgNote n = psg_note(psg_tone_hz(uint16_t((hi & 0x0F) << 8 | lo), clock_hz_));
        if (!n.valid || n.octave < 0 || n.octave > 9) return QStringLiteral("^^^");
        return QString::fromLatin1(n.name) + (std::strlen(n.name) == 1 ? QStringLiteral("-") : QString()) +
               QString::number(n.octave);
    }
    case 1: {                                                        // V
        if (!frame_row || !written(f, c, 8 + ch)) return QStringLiteral(".");
        const int v = f.after(c, size_t(8 + ch));
        return v & 0x10 ? QStringLiteral("E") : hex(v & 0x0F, 1);
    }
    default: {                                                       // N
        if (!frame_row || !(written(f, c, kNoiseReg) || written(f, c, kMixerReg))) return QStringLiteral("..");
        if (f.after(c, kMixerReg) < 0) return QStringLiteral("??");
        if (!noise_on(f, c, ch)) return QStringLiteral("--");
        const int p = f.after(c, kNoiseReg);
        return p < 0 ? QStringLiteral("??") : hex(p & 0x1F, 2);
    }
    }
}

bool AudioTrackerModel::cell_changed(const Row& r, int col) const
{
    if (r.kind != Row::FrameRow || col < 2) return false;
    const AudioFrame& f = r.frame;
    const size_t c = size_t((col - 2) / kChipColumns);
    const int i = (col - 2) % kChipColumns;
    if (i == 0) {
        return (written(f, c, kEnvLo) || written(f, c, kEnvHi)) &&
               (f.changed(c, kEnvLo) || f.changed(c, kEnvHi));
    }
    if (i == 1) return written(f, c, kShape) && changed_masked(f, c, kShape, 0x0F);
    const int ch = (i - 2) / 3;
    switch ((i - 2) % 3) {
    case 0:  return period_changed(f, c, ch);
    case 1:  return written(f, c, 8 + ch) && changed_masked(f, c, size_t(8 + ch), 0x1F);
    default: return (written(f, c, kNoiseReg) || written(f, c, kMixerReg)) && noise_changed(f, c, ch);
    }
}

bool AudioTrackerModel::rewindable(const Row& r, QString* reason) const
{
    if (r.kind == Row::MarkerRow) return false;
    const uint32_t frame = r.kind == Row::FrameRow ? r.frame.frame : r.first;
    QString why;
    if (!paused_)
        why = QObject::tr("Pause the machine to rewind to a frame");
    else if (blocked_ && *blocked_ == Result::RefusedRzx)
        why = QObject::tr("Rewind is not available while an RZX is recording or playing");
    else if (blocked_ || range_.depth == 0)
        why = QObject::tr("The rewind buffer is empty");
    else if (r.timeline != timeline_)
        why = QObject::tr("This frame is from before a reset or a state load");
    else if (frame < range_.oldest_frame || frame > range_.newest_frame)
        why = QObject::tr("Frame %1 is outside the rewind buffer (frames %2 to %3)")
                  .arg(frame).arg(range_.oldest_frame).arg(range_.newest_frame);
    if (reason) *reason = why;
    return why.isEmpty();
}

QVariant AudioTrackerModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= int(rows_.size()) || index.column() >= kColumnCount) return {};
    const Row& r = rows_[size_t(index.row())];
    switch (role) {
    case Qt::DisplayRole:
        return cell_text(r, index.column());
    case ChangedRole:
        return cell_changed(r, index.column());
    case Qt::BackgroundRole:
        if (!cell_changed(r, index.column())) return {};
        {
            QColor c = QGuiApplication::palette().color(QPalette::Highlight);
            c.setAlpha(110);
            return QBrush(c);
        }
    case RewindableRole:
        return rewindable(r, nullptr);
    case RewindFrameRole:
        return frame_row_frame(index.row());
    case Qt::ForegroundRole:
        if (index.column() == 0 && r.kind != Row::MarkerRow && !rewindable(r, nullptr))
            return QBrush(QGuiApplication::palette().color(QPalette::Disabled, QPalette::WindowText));
        return {};
    case Qt::ToolTipRole: {
        if (r.kind == Row::MarkerRow) return {};
        QString why;
        if (rewindable(r, &why))
            return QObject::tr("Double-click to rewind to the start of frame %1").arg(frame_row_frame(index.row()));
        return why;
    }
    default:
        return {};
    }
}

void AudioTrackerModel::add_frame_rows(const AudioFrame& f, std::vector<Row>& out) const
{
    if (f.empty_before > 0) {
        Row e;
        e.kind = Row::EmptyRun;
        e.first = f.frame - f.empty_before;
        e.last = f.frame - 1;
        e.timeline = timeline_;
        out.push_back(std::move(e));
    }
    Row r;
    r.kind = Row::FrameRow;
    r.frame = f;
    r.timeline = timeline_;
    out.push_back(std::move(r));
}

void AudioTrackerModel::feed(const std::vector<AudioLogEntry>& entries, std::vector<Row>& done)
{
    for (const AudioLogEntry& e : entries) {
        last_seq_ = e.seq;
        const auto f = acc_.feed(e);
        if (f) add_frame_rows(*f, done);
        if (e.marker == AudioMarker::None) continue;
        Row m;
        m.kind = Row::MarkerRow;
        m.marker = e.marker;
        m.timeline = timeline_;
        done.push_back(std::move(m));
        // A reset or a load can reuse frame numbers of an earlier timeline; a landing cannot.
        if (e.marker != AudioMarker::Rewind) ++timeline_;
    }
}

void AudioTrackerModel::trim()
{
    if (rows_.size() <= size_t(kMaxRows)) return;
    const size_t excess = rows_.size() - size_t(kMaxRows);
    beginRemoveRows({}, 0, int(excess) - 1);
    rows_.erase(rows_.begin(), rows_.begin() + std::ptrdiff_t(excess));
    endRemoveRows();
}

void AudioTrackerModel::set_entries(const std::vector<AudioLogEntry>& entries)
{
    beginResetModel();
    rows_.clear();
    tail_n_ = 0;
    acc_ = jnext::dbg::AudioFrameAccumulator();
    last_seq_ = 0;
    timeline_ = 0;
    feed(entries, rows_);
    const size_t committed = rows_.size();
    if (const AudioFrame* o = acc_.open()) add_frame_rows(*o, rows_);
    tail_n_ = rows_.size() - committed;
    if (rows_.size() > size_t(kMaxRows))
        rows_.erase(rows_.begin(), rows_.begin() + std::ptrdiff_t(rows_.size() - size_t(kMaxRows)));
    endResetModel();
}

void AudioTrackerModel::append_entries(const std::vector<AudioLogEntry>& entries)
{
    if (entries.empty()) return;
    std::vector<Row> seg;
    feed(entries, seg);
    const size_t committed = seg.size();
    if (const AudioFrame* o = acc_.open()) add_frame_rows(*o, seg);
    const size_t new_tail = seg.size() - committed;

    // The open frame's rows are replaced by the new ones, so a frame still being written is one row.
    const size_t base = rows_.size() - tail_n_;
    const size_t common = std::min(tail_n_, seg.size());
    for (size_t i = 0; i < common; ++i) rows_[base + i] = std::move(seg[i]);
    if (common) dataChanged(index(int(base), 0), index(int(base + common - 1), kColumnCount - 1));
    if (seg.size() > common) {
        beginInsertRows({}, int(base + common), int(base + seg.size() - 1));
        for (size_t i = common; i < seg.size(); ++i) rows_.push_back(std::move(seg[i]));
        endInsertRows();
    } else if (tail_n_ > common) {
        beginRemoveRows({}, int(base + common), int(rows_.size() - 1));
        rows_.erase(rows_.begin() + std::ptrdiff_t(base + common), rows_.end());
        endRemoveRows();
    }
    tail_n_ = new_tail;
    trim();
}

void AudioTrackerModel::set_clock(uint32_t hz)
{
    if (hz == clock_hz_) return;
    clock_hz_ = hz;
    if (!rows_.empty())
        dataChanged(index(0, 0), index(int(rows_.size()) - 1, kColumnCount - 1), {Qt::DisplayRole});
}

void AudioTrackerModel::set_rewind(const jnext::dbg::RewindRange& range, std::optional<Result> blocked, bool paused)
{
    const bool same = range.oldest_frame == range_.oldest_frame && range.newest_frame == range_.newest_frame &&
                      range.depth == range_.depth && blocked == blocked_ && paused == paused_;
    range_ = range;
    blocked_ = blocked;
    paused_ = paused;
    if (!same && !rows_.empty())
        dataChanged(index(0, 0), index(int(rows_.size()) - 1, kColumnCount - 1),
                    {Qt::ForegroundRole, Qt::ToolTipRole, RewindableRole});
}

// ── The jitter plot ─────────────────────────────────────────────────────

AudioJitterPlot::AudioJitterPlot(QWidget* parent) : QWidget(parent)
{
    setMinimumHeight(48);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}

void AudioJitterPlot::set_data(uint32_t first, uint32_t window, const std::vector<std::pair<uint32_t, int>>& dots)
{
    first_ = first;
    window_ = window ? window : 1;
    dots_ = dots;
    lo_ = hi_ = 0;
    if (!dots_.empty()) {
        lo_ = hi_ = dots_[0].second;
        for (const auto& d : dots_) { lo_ = std::min(lo_, d.second); hi_ = std::max(hi_, d.second); }
        hi_ = std::max(hi_, lo_ + kMinSpan - 1);
    }
    update();
}

std::vector<AudioJitterPlot::Dot> AudioJitterPlot::dots() const
{
    std::vector<Dot> out;
    const double w = width(), h = height();
    for (const auto& d : dots_) {
        const int x = int((double(d.first - first_) + 0.5) * w / double(window_));
        const int y = int((double(d.second - lo_) + 0.5) * h / double(hi_ - lo_ + 1));
        out.push_back({d.first, d.second, QPoint(x, y)});
    }
    return out;
}

void AudioJitterPlot::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, false);
    p.fillRect(rect(), palette().color(QPalette::Base));
    p.setPen(palette().color(QPalette::Mid));
    p.drawRect(0, 0, width() - 1, height() - 1);
    const QColor dot = palette().color(QPalette::Highlight);
    for (const Dot& d : dots()) p.fillRect(QRect(d.at.x() - 1, d.at.y() - 1, 3, 3), dot);
}

// ── The view ────────────────────────────────────────────────────────────

AudioTrackerView::AudioTrackerView(Debugger* dbg, QWidget* parent) : QWidget(parent), dbg_(dbg)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    status_ = new QLabel(tr("Not recording"), this);
    status_->setObjectName("audio_tracker_status");
    layout->addWidget(status_);

    model_ = new AudioTrackerModel(this);
    table_ = new QTableView(this);
    table_->setObjectName("audio_tracker_table");
    table_->setModel(model_);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->hide();
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);
    table_->setFont(mono);
    const QFontMetrics fm(mono);
    const int pad = 12;
    for (int col = 0; col < AudioTrackerModel::kColumnCount; ++col) {
        int chars = 4;                                               // Note, Env, Frame
        if (col == 0) chars = 9;
        else if (col == 1) chars = 10;
        else {
            const int i = (col - 2) % AudioTrackerModel::kChipColumns;
            chars = i == 0 ? 6 : i == 1 ? 3 : (i - 2) % 3 == 0 ? 6 : (i - 2) % 3 == 1 ? 3 : 4;
        }
        table_->setColumnWidth(col, fm.horizontalAdvance(QString(chars, '0')) + pad);
    }
    layout->addWidget(table_, 1);

    auto* wrow = new QHBoxLayout;
    wrow->addWidget(new QLabel(tr("Window:"), this));
    window_ = new QSpinBox(this);
    window_->setObjectName("audio_timing_window");
    window_->setRange(1, 1000);
    window_->setValue(50);
    window_->setSuffix(tr(" frames"));
    wrow->addWidget(window_);
    wrow->addStretch(1);
    layout->addLayout(wrow);
    auto label = [&](const char* name) {
        auto* l = new QLabel(this);
        l->setObjectName(name);
        l->setFont(mono);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        layout->addWidget(l);
        return l;
    };
    span_ = label("audio_timing_span");
    first_ = label("audio_timing_first");
    last_ = label("audio_timing_last");
    writes_ = label("audio_timing_writes");
    frame_ = label("audio_timing_frame");
    plot_ = new AudioJitterPlot(this);
    plot_->setObjectName("audio_timing_plot");
    layout->addWidget(plot_);
    plot_range_ = label("audio_timing_plot_range");
    auto* note = new QLabel(tr("Exact routine cost: the CPU panel's T-states since the last stop, or --profile."), this);
    note->setObjectName("audio_timing_note");
    note->setWordWrap(true);
    layout->addWidget(note);
    update_timing();

    connect(window_, qOverload<int>(&QSpinBox::valueChanged), this, [this](int) { update_timing(); });
    connect(table_->selectionModel(), &QItemSelectionModel::currentRowChanged, this,
            [this](const QModelIndex&, const QModelIndex&) { update_timing(); });

    connect(table_, &QTableView::doubleClicked, this, [this](const QModelIndex& idx) {
        if (!idx.isValid() || !idx.data(AudioTrackerModel::RewindableRole).toBool()) return;
        emit rewind_to_frame_requested(idx.data(AudioTrackerModel::RewindFrameRole).toUInt());
    });
}

void AudioTrackerView::refresh()
{
    if (!dbg_) return;
    status_->setText(dbg_->audio_log_enabled() ? tr("Recording") : tr("Not recording"));
    model_->set_clock(dbg_->audio_state().psg_clock_hz);

    QScrollBar* bar = table_->verticalScrollBar();
    const bool at_end = bar->value() >= bar->maximum();
    std::vector<AudioLogEntry> entries;
    switch (fetch_audio_log(*dbg_, model_->last_seq(), entries)) {
    case AudioLogFetch::Off:
        if (model_->rowCount() > 0 || model_->last_seq() != 0) model_->set_entries({});
        break;
    case AudioLogFetch::Resync:
        model_->set_entries(entries);
        break;
    case AudioLogFetch::Append:
        model_->append_entries(entries);
        break;
    }
    model_->set_rewind(dbg_->rewind_range(), dbg_->rewind_blocked(), dbg_->state().paused);
    if (at_end) table_->scrollToBottom();
    update_timing();
}

void AudioTrackerView::update_timing()
{
    const uint32_t n = uint32_t(window_->value());
    const auto frames = model_->timing_frames(n);
    timing_ = jnext::dbg::audio_timing_stats(frames, n);
    const jnext::dbg::AudioTimingStats& s = timing_;

    if (s.frames > 0) {
        span_->setText(tr("Last %1 %6: %2 to %3, %4 with AY writes, %5 without")
                           .arg(n).arg(s.window_first).arg(s.window_last).arg(s.frames).arg(s.empty).arg(n == 1 ? tr("frame") : tr("frames")));
        first_->setText(QStringLiteral("First write: line %1, T-state %2 (min / avg / max)")
                            .arg(triple(s.first_line), triple(s.first_tstate)));
        last_->setText(QStringLiteral("Last write: line %1, T-state %2 (min / avg / max)")
                           .arg(triple(s.last_line), triple(s.last_tstate)));
        writes_->setText(QStringLiteral("Writes per frame: %1 (min / avg / max); PC %2")
                             .arg(triple(s.writes), pc_range(s.pc_min, s.pc_max)));
    } else {
        span_->setText(tr("No AY writes in the last %1 %2").arg(n).arg(n == 1 ? tr("frame") : tr("frames")));
        first_->setText(QStringLiteral("First write: -"));
        last_->setText(QStringLiteral("Last write: -"));
        writes_->setText(QStringLiteral("Writes per frame: -"));
    }

    const QModelIndex cur = table_->currentIndex();
    frame_->setText(model_->timing_text(cur.isValid() ? cur.row() : model_->rowCount() - 1));

    // The dots: the frames of the run the statistics used, within the nominal window.
    std::vector<std::pair<uint32_t, int>> dots;
    uint32_t first = 0;
    if (s.frames > 0) {
        first = s.window_last >= n - 1 ? s.window_last - (n - 1) : 0;
        for (size_t i = frames.size() - s.frames; i < frames.size(); ++i)
            dots.push_back({frames[i]->frame, int(frames[i]->first.vc)});
    }
    plot_->set_data(first, n, dots);
    plot_range_->setText(dots.empty() ? QString()
                                      : QStringLiteral("Dots: first write per frame, line %1 at the top to %2 at the bottom")
                                            .arg(plot_->range_lo()).arg(plot_->range_hi()));
}
