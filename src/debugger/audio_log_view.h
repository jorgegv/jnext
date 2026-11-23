#pragma once

// GH #531 -- the Audio panel's Log sub-tab: a table over the backend's audio
// write log (`Debugger::audio_log_entries()`), with filters, a double-click to
// the Disassembly for CPU rows, and an Export CSV button.
//
// The view MIRRORS the backend's ring: `refresh()` appends the entries newer than
// the last one it holds, and replaces every row when the ring no longer holds the
// newest row it has (an overflow, a rebuild after the request was dropped, or a
// rewind landing, which removes entries from the NEWEST end and so removes the
// view's newest row first). The seq of that row is never reused, so "still there"
// means "the same entry". There is no second copy of the truncation rule here, and
// the rows are capped (set_row_cap) so the view stays bounded.
//
// Not a QTableWidget: the hex register table is the panel's only one.

#include <QAbstractTableModel>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QString>
#include <QTableView>
#include <QWidget>

#include <vector>

#include "debug/events.h"

namespace jnext { namespace dbg { class Debugger; } }

/// The text of one discard reason: the DAC & Beeper table and the Log tab read it
/// from here, so the two cannot disagree.
QString audio_drop_reason_text(jnext::dbg::AudioDropReason r);

/// The text of a write's source ("CPU", "DMA", "Copper", "Debugger"), shared likewise.
const char* audio_source_text(jnext::dbg::EventSource s);

class AudioLogModel : public QAbstractTableModel {
public:
    enum Column { Frame, Line, Hc, Tstate, Pc, Source, Target, Chip, Reg, Value, Prev, Flags, ColumnCount };

    explicit AudioLogModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void replace(std::vector<jnext::dbg::AudioLogEntry> entries);
    void append(const std::vector<jnext::dbg::AudioLogEntry>& entries);
    /// Drop the oldest rows until at most `keep` remain.
    void trim_front(size_t keep);
    const jnext::dbg::AudioLogEntry& entry(int row) const { return entries_[size_t(row)]; }
    /// The seq of the newest row, 0 when empty.
    uint64_t last_seq() const { return entries_.empty() ? 0 : entries_.back().seq; }

private:
    std::vector<jnext::dbg::AudioLogEntry> entries_;
};

/// Narrows the rows; every filter at its default (-1 / false) passes everything.
/// A marker row always passes: it explains a discontinuity in the rows around it.
class AudioLogProxy : public QSortFilterProxyModel {
public:
    explicit AudioLogProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    void set_target(int t)    { target_ = t; invalidate(); }      ///< AudioTarget, or -1
    void set_chip(int c)      { chip_ = c; invalidate(); }        ///< AY chip 0-2 (AY targets), or -1
    void set_reg(int r)       { reg_ = r; invalidate(); }         ///< AY register 0-15, or -1
    void set_dac(int d)       { dac_ = d; invalidate(); }         ///< DAC channel 0-3 (Dac, DacMirror), or -1
    void set_source(int s)    { source_ = s; invalidate(); }      ///< EventSource, or -1
    void set_restarts_only(bool b) { restarts_ = b; invalidate(); }
    void set_dropped_only(bool b)  { dropped_ = b; invalidate(); }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& parent) const override;

private:
    int target_ = -1, chip_ = -1, reg_ = -1, dac_ = -1, source_ = -1;
    bool restarts_ = false, dropped_ = false;
};

class AudioLogView : public QWidget {
    Q_OBJECT
public:
    explicit AudioLogView(jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Bring the rows up to date with the backend's log (see the file header).
    void refresh();

    /// The most rows the view holds (default: the log's own default size). The backend's
    /// ring forgets its oldest writes, and so does the view, whatever the machine runs
    /// for. The test seam; the oldest rows go first.
    void set_row_cap(size_t rows) { row_cap_ = rows; }

    /// Write the backend's log as CSV to `path`. False when the log is off or the
    /// file cannot be written. The Export button's body, and the tests' seam.
    bool export_to(const QString& path);

signals:
    /// A CPU row was double-clicked: show the instruction that wrote it.
    void show_in_disassembly_requested(uint16_t addr);

private:
    void row_activated(const QModelIndex& proxy_index);

    jnext::dbg::Debugger* dbg_;
    AudioLogModel* model_ = nullptr;
    AudioLogProxy* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* status_ = nullptr;
    QPushButton* export_ = nullptr;
    QComboBox *target_ = nullptr, *chip_ = nullptr, *reg_ = nullptr, *dac_ = nullptr, *source_ = nullptr;
    QCheckBox *restarts_ = nullptr, *dropped_ = nullptr;
    size_t row_cap_ = jnext::dbg::AUDIO_LOG_DEFAULT_ENTRIES;
};
