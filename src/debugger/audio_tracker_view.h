#pragma once

// GH #534 -- the Audio panel's Tracker sub-tab: the AY tune one FRAME per row, the three chips
// side by side in Vortex Tracker column order, built on the per-frame grouping of the audio write
// log (debug/audio_frames.h). It mirrors the log by the Log view's own rule (fetch_audio_log()),
// so a marker, an overflow and a rewind landing resync it the same way, and it keeps at most
// kMaxRows rows. Double-clicking a row while paused asks the window to rewind to that frame.
//
// Not a QTableWidget: the hex register table is the panel's only one.

#include <QAbstractTableModel>
#include <QLabel>
#include <QString>
#include <QTableView>
#include <QWidget>

#include <optional>
#include <vector>

#include "debug/audio_frames.h"
#include "debug/result.h"

namespace jnext { namespace dbg { class Debugger; } }

class AudioTrackerModel : public QAbstractTableModel {
public:
    enum Role { ChangedRole = Qt::UserRole + 1, RewindableRole = Qt::UserRole + 2, RewindFrameRole = Qt::UserRole + 3 };

    /// Columns: Frame, Mark, then per chip 11: Env, Sh, then A, B, C of Note, V, N.
    static constexpr int kChipColumns = 11;
    static constexpr int kColumnCount = 2 + kChipColumns * int(jnext::dbg::AY_CHIP_COUNT);
    /// The most rows kept; beyond it the oldest go, so the view is bounded however long it stays open.
    static constexpr int kMaxRows = 65536;

    explicit AudioTrackerModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    /// Full rebuild: a fresh accumulator, every entry, the open frame as the last row.
    void set_entries(const std::vector<jnext::dbg::AudioLogEntry>& entries);
    /// Feed more entries; the open frame's row is replaced, not added to.
    void append_entries(const std::vector<jnext::dbg::AudioLogEntry>& entries);
    /// The PSG clock the notes are computed at (`AudioState::psg_clock_hz`).
    void set_clock(uint32_t hz);
    /// The greying snapshot: what the rewind buffer holds, why a rewind would be refused, and
    /// whether the machine is paused.
    void set_rewind(const jnext::dbg::RewindRange& range, std::optional<jnext::dbg::Result> blocked, bool paused);

    /// The seq of the newest entry fed, 0 when none (the fetch's continuity key).
    uint64_t last_seq() const { return last_seq_; }
    /// The frame of a row (the first of an empty run); 0 for a marker row.
    uint32_t frame_row_frame(int row) const;
    /// The grouped frame behind a row; null for an empty-run row and a marker row.
    const jnext::dbg::AudioFrame* frame_at(int row) const;

private:
    struct Row {
        enum Kind : uint8_t { FrameRow, EmptyRun, MarkerRow } kind = FrameRow;
        jnext::dbg::AudioFrame frame;                         // FrameRow
        uint32_t first = 0, last = 0;                         // EmptyRun: frames first..last
        jnext::dbg::AudioMarker marker = jnext::dbg::AudioMarker::None;
        uint32_t timeline = 0;                                // bumped by every Reset and StateLoad
    };

    void feed(const std::vector<jnext::dbg::AudioLogEntry>& entries, std::vector<Row>& done);
    void add_frame_rows(const jnext::dbg::AudioFrame& f, std::vector<Row>& out) const;
    void splice_tail(std::vector<Row>&& seg, size_t new_tail);
    void trim();
    bool rewindable(const Row& r, QString* reason) const;
    QString cell_text(const Row& r, int col) const;
    bool cell_changed(const Row& r, int col) const;

    std::vector<Row> rows_;
    size_t tail_n_ = 0;                  // trailing rows that belong to the open frame
    jnext::dbg::AudioFrameAccumulator acc_;
    uint64_t last_seq_ = 0;
    uint32_t timeline_ = 0;              // the current timeline: a row of an older one is not rewindable
    uint32_t clock_hz_ = 0;

    jnext::dbg::RewindRange range_;
    std::optional<jnext::dbg::Result> blocked_;
    bool paused_ = false;
};

class AudioTrackerView : public QWidget {
    Q_OBJECT
public:
    explicit AudioTrackerView(jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Bring the rows up to date with the backend's log.
    void refresh();

    AudioTrackerModel* model() { return model_; }

signals:
    /// A rewindable row was double-clicked: rewind to the start of that frame.
    void rewind_to_frame_requested(uint32_t frame);

private:
    jnext::dbg::Debugger* dbg_;
    AudioTrackerModel* model_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* status_ = nullptr;
};
