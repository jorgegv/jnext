#pragma once

// GH #537 -- the Pacing and Sample stream groups of the Audio panel's DAC & Beeper tab.
//
//   Pacing         the CTC's four channels and the DMA (`Debugger::ctc_state()`, `dma_state()`): mode,
//                  prescaler, time constant, count, state and the computed rate of each.
//   Sample stream  one row per DAC writer over the last second (`dac_stream_stats()`): the measured rate,
//                  interval min / average / max, jitter and gaps, and the pacing engine nearest that rate.
//
// The stream is computed from the audio write log. The view keeps only the markers and the DAC entries of
// the last second, fetched incrementally (the same continuity rule as AudioLogView: ask from the newest
// seq held, start over when the ring no longer holds it), so memory and work are bounded whatever the ring
// size. It fetches nothing while it is not on show: AudioPanel::refresh() calls it only then, and the
// DebuggerManager asks the backend to record the log while DAC & Beeper (or Log) is on show.
//
// Not QTableWidget: the hex register table is the panel's only one.

#include <QLabel>
#include <QTreeWidget>
#include <QWidget>

#include <array>
#include <vector>

#include "debug/events.h"
#include "debug/inspect.h"

namespace jnext { namespace dbg { class Debugger; } }

class AudioStreamView : public QWidget {
    Q_OBJECT
public:
    explicit AudioStreamView(jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Bring the pacing rows and the stream up to date.
    void refresh();

signals:
    /// A CPU writer's row was double-clicked: show the instruction that writes the DAC.
    void show_in_disassembly_requested(uint16_t pc);

private:
    void row_activated(QTreeWidgetItem* item);

    jnext::dbg::Debugger* dbg_;
    QTreeWidget* pacing_ = nullptr;
    QTreeWidget* stream_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* window_ = nullptr;
    uint64_t last_seq_ = 0;                          ///< the newest ring entry seen (any kind)
    std::vector<jnext::dbg::AudioLogEntry> cache_;   ///< markers and DAC entries of the last second
};
