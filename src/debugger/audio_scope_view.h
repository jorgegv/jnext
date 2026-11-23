#pragma once

#include <QScrollArea>
#include <QLabel>
#include <QProgressBar>

#include <array>
#include <cstdint>
#include <vector>

#include "debug/inspect.h"   // AudioSource, AUDIO_SOURCE_COUNT

namespace jnext { namespace dbg { class Debugger; } }

class ScopeTrace;

/// The Audio panel's Scopes sub-tab (GH #536): sixteen oscilloscope traces and level meters, in
/// `AudioSource` order, from `Debugger::audio_scope_samples()` (INS-24). The panel holds a
/// `Debugger*` only.
///
/// Each refresh fetches two windows of samples, finds the latest rising edge through the middle of the
/// block's own range (`scope_trigger`, so a quiet channel triggers as well as a loud one), and draws one
/// window from it (20 ms at 44.1 kHz); a block with no edge free-runs and the lamp says `free`. The
/// meter is the peak excursion from the source's resting level over the drawn window, as a percentage
/// of its range (`audio_source_scale`), with peak and RMS in native units beside it.
/// Under `--silent` the mixer does not run, so the tab says so instead.
class AudioScopeView : public QScrollArea {
    Q_OBJECT
public:
    /// The window drawn, in samples: 20 ms at 44.1 kHz.
    static constexpr size_t WINDOW = 882;

    /// What one trace shows after the last refresh (a test seam as much as a state).
    struct TraceState {
        bool triggered = false;      ///< a rising edge was found: the lamp reads `trig`, else `free`
        size_t start = 0;            ///< index of the first drawn sample within the fetched block
        uint64_t first_sample = 0;   ///< the block's `first_sample` (new data at each refresh)
        int meter_percent = 0;       ///< 0-100
    };

    explicit AudioScopeView(jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Fetch and redraw. A no-op while the backend's tap is off (nothing has been requested yet).
    void refresh();

    const TraceState& trace(int i) const { return state_[static_cast<size_t>(i)]; }
    /// The drawn window of source `i` (at most WINDOW samples).
    const std::vector<int16_t>& shown(int i) const { return shown_[static_cast<size_t>(i)]; }

private:
    jnext::dbg::Debugger* dbg_;
    QWidget* grid_ = nullptr;
    QLabel* off_label_ = nullptr;
    std::array<ScopeTrace*, jnext::dbg::AUDIO_SOURCE_COUNT> trace_{};
    std::array<QLabel*, jnext::dbg::AUDIO_SOURCE_COUNT> lamp_{};
    std::array<QProgressBar*, jnext::dbg::AUDIO_SOURCE_COUNT> meter_{};
    std::array<QLabel*, jnext::dbg::AUDIO_SOURCE_COUNT> meter_text_{};
    std::array<TraceState, jnext::dbg::AUDIO_SOURCE_COUNT> state_{};
    std::array<std::vector<int16_t>, jnext::dbg::AUDIO_SOURCE_COUNT> shown_{};
};
