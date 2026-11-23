#include "debugger/audio_log_view.h"

#include "debug/debugger.h"

#include <initializer_list>
#include <utility>

#include <QCoreApplication>
#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QVBoxLayout>

using jnext::dbg::AudioLogEntry;
using jnext::dbg::AudioMarker;
using jnext::dbg::AudioTarget;
using jnext::dbg::EventSource;

QString audio_drop_reason_text(jnext::dbg::AudioDropReason r)
{
    // AudioDropReason order from AyPortDisabled.
    static const char* const kText[8] = {
        QT_TRANSLATE_NOOP("AudioPanel", "AY ports off (NR 0x84 bit 0)"),
        QT_TRANSLATE_NOOP("AudioPanel", "AY held in reset (NR 0x06 = 11)"),
        QT_TRANSLATE_NOOP("AudioPanel", "Chip select, TurboSound off"),
        QT_TRANSLATE_NOOP("AudioPanel", "Ignored 0xFFFD value"),
        QT_TRANSLATE_NOOP("AudioPanel", "Register address 16-31"),
        QT_TRANSLATE_NOOP("AudioPanel", "DAC port family off (NR 0x84)"),
        QT_TRANSLATE_NOOP("AudioPanel", "DAC off (NR 0x08 bit 3)"),
        QT_TRANSLATE_NOOP("AudioPanel", "DAC mirror, DAC off (NR 0x08 bit 3)"),
    };
    const size_t i = size_t(r);
    if (i < 1 || i > 8) return QStringLiteral("-");
    return QCoreApplication::translate("AudioPanel", kText[i - 1]);
}

const char* audio_source_text(EventSource s)
{
    switch (s) {
        case EventSource::Cpu:      return "CPU";
        case EventSource::Dma:      return "DMA";
        case EventSource::Copper:   return "Copper";
        case EventSource::Debugger: return "Debugger";
        case EventSource::Any:      break;
    }
    return "-";
}

// ---------------------------------------------------------------------------
// The model
// ---------------------------------------------------------------------------

int AudioLogModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : int(entries_.size());
}

int AudioLogModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant AudioLogModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char* const kHead[ColumnCount] = {
        QT_TRANSLATE_NOOP("AudioPanel", "Frame"), QT_TRANSLATE_NOOP("AudioPanel", "Line"),
        QT_TRANSLATE_NOOP("AudioPanel", "HC"), QT_TRANSLATE_NOOP("AudioPanel", "T-state"),
        QT_TRANSLATE_NOOP("AudioPanel", "PC"), QT_TRANSLATE_NOOP("AudioPanel", "Source"),
        QT_TRANSLATE_NOOP("AudioPanel", "Target"), QT_TRANSLATE_NOOP("AudioPanel", "Chip"),
        QT_TRANSLATE_NOOP("AudioPanel", "Reg"), QT_TRANSLATE_NOOP("AudioPanel", "Value"),
        QT_TRANSLATE_NOOP("AudioPanel", "Prev"), QT_TRANSLATE_NOOP("AudioPanel", "Flags"),
    };
    if (section < 0 || section >= ColumnCount) return {};
    return QCoreApplication::translate("AudioPanel", kHead[section]);
}

QVariant AudioLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= int(entries_.size())) return {};
    if (role == Qt::TextAlignmentRole) return int(Qt::AlignLeft | Qt::AlignVCenter);
    if (role != Qt::DisplayRole) return {};
    const AudioLogEntry& e = entries_[size_t(index.row())];
    const bool marker = e.marker != AudioMarker::None;
    switch (index.column()) {
        case Frame:  return QString::number(e.frame);
        case Line:   return QString::number(e.vc);
        case Hc:     return QString::number(e.hc);
        case Tstate: return QString::number(e.tstate_in_frame);
        default: break;
    }
    if (marker) {
        if (index.column() == Source) return QStringLiteral("marker");
        if (index.column() == Target) {
            switch (e.marker) {
                case AudioMarker::Reset:     return QStringLiteral("reset");
                case AudioMarker::StateLoad: return QStringLiteral("state load");
                case AudioMarker::Rewind:    return QStringLiteral("rewind");
                case AudioMarker::None:      break;
            }
        }
        return QString();
    }
    const bool ay = e.target == AudioTarget::AyRegister || e.target == AudioTarget::AyChipSelect;
    switch (index.column()) {
        case Pc:
            // The slot's PC names the writer only for a CPU write.
            return e.source == EventSource::Cpu ? QString::asprintf("0x%04X", e.pc) : QStringLiteral("-");
        case Source: return QString::fromLatin1(audio_source_text(e.source));
        case Target:
            switch (e.target) {
                case AudioTarget::AyRegister:   return QCoreApplication::translate("AudioPanel", "AY register");
                case AudioTarget::AyChipSelect: return QCoreApplication::translate("AudioPanel", "AY select");
                case AudioTarget::Dac:          return QStringLiteral("DAC");
                case AudioTarget::DacMirror:    return QCoreApplication::translate("AudioPanel", "DAC mirror");
                case AudioTarget::Beeper:       return QCoreApplication::translate("AudioPanel", "Beeper");
                case AudioTarget::Count:        break;
            }
            return QStringLiteral("-");
        case Chip:
            if (ay) return QString::number(e.chip);
            if ((e.target == AudioTarget::Dac || e.target == AudioTarget::DacMirror) && e.chip < 4)
                return QString(QChar(char('A' + e.chip)));
            return QStringLiteral("-");
        case Reg:
            if (e.target == AudioTarget::AyRegister) return QString::asprintf("R%u", unsigned(e.reg));
            if (e.target == AudioTarget::DacMirror) return QString::asprintf("NR 0x%02X", unsigned(e.reg));
            return QStringLiteral("-");
        case Value: return QString::asprintf("0x%02X", unsigned(e.value));
        case Prev:  return QString::asprintf("0x%02X", unsigned(e.prev));
        case Flags:
            if (e.flags & jnext::dbg::AudioFlag::Dropped)
                return QCoreApplication::translate("AudioPanel", "dropped: ") + audio_drop_reason_text(e.drop_reason);
            if (e.flags & jnext::dbg::AudioFlag::EnvelopeRestart)
                return QCoreApplication::translate("AudioPanel", "restart");
            return QString();
        default: break;
    }
    return {};
}

void AudioLogModel::replace(std::vector<AudioLogEntry> entries)
{
    beginResetModel();
    entries_ = std::move(entries);
    endResetModel();
}

void AudioLogModel::append(const std::vector<AudioLogEntry>& entries)
{
    if (entries.empty()) return;
    beginInsertRows(QModelIndex(), int(entries_.size()), int(entries_.size() + entries.size()) - 1);
    entries_.insert(entries_.end(), entries.begin(), entries.end());
    endInsertRows();
}

void AudioLogModel::trim_front(size_t keep)
{
    if (entries_.size() <= keep) return;
    const int n = int(entries_.size() - keep);
    beginRemoveRows(QModelIndex(), 0, n - 1);
    entries_.erase(entries_.begin(), entries_.begin() + n);
    endRemoveRows();
}

// ---------------------------------------------------------------------------
// The filter
// ---------------------------------------------------------------------------

bool AudioLogProxy::filterAcceptsRow(int source_row, const QModelIndex&) const
{
    const auto* m = static_cast<const AudioLogModel*>(sourceModel());
    const AudioLogEntry& e = m->entry(source_row);
    if (e.marker != AudioMarker::None) return true;
    const bool ay = e.target == AudioTarget::AyRegister || e.target == AudioTarget::AyChipSelect;
    const bool dac = e.target == AudioTarget::Dac || e.target == AudioTarget::DacMirror;
    if (target_ >= 0 && int(e.target) != target_) return false;
    if (chip_ >= 0 && !(ay && e.chip == chip_)) return false;
    if (reg_ >= 0 && !(e.target == AudioTarget::AyRegister && e.reg == reg_)) return false;
    if (dac_ >= 0 && !(dac && e.chip == dac_)) return false;
    if (source_ >= 0 && int(e.source) != source_) return false;
    if (restarts_ && !(e.flags & jnext::dbg::AudioFlag::EnvelopeRestart)) return false;
    if (dropped_ && !(e.flags & jnext::dbg::AudioFlag::Dropped)) return false;
    return true;
}

// ---------------------------------------------------------------------------
// The view
// ---------------------------------------------------------------------------

AudioLogView::AudioLogView(jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent), dbg_(dbg)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(4);

    auto* top = new QHBoxLayout;
    status_ = new QLabel(tr("Not recording"), this);
    status_->setObjectName("audio_log_status");
    top->addWidget(status_);
    top->addStretch(1);
    export_ = new QPushButton(tr("Export CSV..."), this);
    export_->setObjectName("audio_log_export");
    top->addWidget(export_);
    layout->addLayout(top);

    auto combo = [this](const char* name, std::initializer_list<std::pair<QString, int>> items) {
        auto* c = new QComboBox(this);
        c->setObjectName(name);
        c->addItem(tr("Any"), -1);
        for (const auto& it : items) c->addItem(it.first, it.second);
        return c;
    };
    target_ = combo("audio_log_target", {{tr("AY register"), int(AudioTarget::AyRegister)},
                                         {tr("AY select"), int(AudioTarget::AyChipSelect)},
                                         {tr("DAC"), int(AudioTarget::Dac)},
                                         {tr("DAC mirror"), int(AudioTarget::DacMirror)},
                                         {tr("Beeper"), int(AudioTarget::Beeper)}});
    chip_ = combo("audio_log_chip", {{"0", 0}, {"1", 1}, {"2", 2}});
    std::initializer_list<std::pair<QString, int>> regs = {
        {"R0", 0}, {"R1", 1}, {"R2", 2}, {"R3", 3}, {"R4", 4}, {"R5", 5}, {"R6", 6}, {"R7", 7},
        {"R8", 8}, {"R9", 9}, {"R10", 10}, {"R11", 11}, {"R12", 12}, {"R13", 13}, {"R14", 14}, {"R15", 15}};
    reg_ = combo("audio_log_reg", regs);
    dac_ = combo("audio_log_dac", {{"A", 0}, {"B", 1}, {"C", 2}, {"D", 3}});
    source_ = combo("audio_log_source", {{tr("CPU"), int(EventSource::Cpu)}, {tr("DMA"), int(EventSource::Dma)},
                                         {tr("Copper"), int(EventSource::Copper)},
                                         {tr("Debugger"), int(EventSource::Debugger)}});
    auto* filters = new QHBoxLayout;
    filters->setSpacing(4);
    const struct { const char* label; QComboBox* box; } rows[] = {
        {"Target", target_}, {"AY chip", chip_}, {"Reg", reg_}, {"DAC", dac_}, {"Source", source_}};
    for (const auto& r : rows) {
        filters->addWidget(new QLabel(tr(r.label), this));
        filters->addWidget(r.box);
    }
    filters->addStretch(1);
    layout->addLayout(filters);

    restarts_ = new QCheckBox(tr("Envelope restarts only"), this);
    restarts_->setObjectName("audio_log_restarts");
    dropped_ = new QCheckBox(tr("Dropped only"), this);
    dropped_->setObjectName("audio_log_dropped");
    auto* checks = new QHBoxLayout;
    checks->addWidget(restarts_);
    checks->addWidget(dropped_);
    checks->addStretch(1);
    layout->addLayout(checks);

    model_ = new AudioLogModel(this);
    proxy_ = new AudioLogProxy(this);
    proxy_->setSourceModel(model_);
    table_ = new QTableView(this);
    table_->setObjectName("audio_log_table");
    table_->setModel(proxy_);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->hide();
    table_->horizontalHeader()->setStretchLastSection(true);
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);
    table_->setFont(mono);
    layout->addWidget(table_, 1);

    auto index_of = [](QComboBox* b) { return [b]() { return b->currentData().toInt(); }; };
    connect(target_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, f = index_of(target_)] { proxy_->set_target(f()); });
    connect(chip_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, f = index_of(chip_)] { proxy_->set_chip(f()); });
    connect(reg_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, f = index_of(reg_)] { proxy_->set_reg(f()); });
    connect(dac_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, f = index_of(dac_)] { proxy_->set_dac(f()); });
    connect(source_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this, f = index_of(source_)] { proxy_->set_source(f()); });
    connect(restarts_, &QCheckBox::toggled, this, [this](bool on) { proxy_->set_restarts_only(on); });
    connect(dropped_, &QCheckBox::toggled, this, [this](bool on) { proxy_->set_dropped_only(on); });
    connect(table_, &QTableView::doubleClicked, this, &AudioLogView::row_activated);
    connect(export_, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getSaveFileName(this, tr("Export audio log"),
                                                          QStringLiteral("audio-log.csv"), tr("CSV (*.csv)"));
        if (path.isEmpty()) return;
        if (!export_to(path))
            QMessageBox::warning(this, tr("Export audio log"),
                                 tr("The log could not be written to %1.").arg(path));
    });
}

void AudioLogView::row_activated(const QModelIndex& proxy_index)
{
    if (!proxy_index.isValid()) return;
    const AudioLogEntry& e = model_->entry(proxy_->mapToSource(proxy_index).row());
    if (e.marker != AudioMarker::None || e.source != EventSource::Cpu) return;
    emit show_in_disassembly_requested(e.pc);
}

void AudioLogView::refresh()
{
    if (!dbg_) return;
    status_->setText(dbg_->audio_log_enabled() ? tr("Recording") : tr("Not recording"));
    // From the newest row the view holds (it must still be there), so a ring that was freed and
    // rebuilt, or that overflowed past it, is seen even when the seq numbers happen to continue.
    const uint64_t have = model_->last_seq();
    auto r = dbg_->audio_log_entries(have > 0 ? have - 1 : 0);
    if (!r) {                               // the log is off: nothing is on show any more
        if (model_->rowCount() > 0) model_->replace({});
        return;
    }
    std::vector<AudioLogEntry>& v = r.value;
    bool resync = false;
    size_t first = 0;
    if (have > 0) {
        if (v.empty() || v.front().seq != have) resync = true;
        else first = 1;
    }
    if (resync) {
        auto all = dbg_->audio_log_entries(0);
        std::vector<AudioLogEntry> rows = all ? std::move(all.value) : std::vector<AudioLogEntry>{};
        if (rows.size() > row_cap_) rows.erase(rows.begin(), rows.end() - std::ptrdiff_t(row_cap_));
        model_->replace(std::move(rows));
        return;
    }
    if (first > 0) v.erase(v.begin());
    model_->append(v);
    // The ring overwrites its oldest writes; the rows for them go too, so the view stays bounded
    // however long the tab is open. The newest row (the resync key above) is never the one dropped.
    model_->trim_front(row_cap_);
}

bool AudioLogView::export_to(const QString& path)
{
    return dbg_ && dbg_->audio_log_export(path.toStdString()) == jnext::dbg::Result::Ok;
}
