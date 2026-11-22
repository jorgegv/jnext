#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QTabWidget>
#include <QTreeWidget>

#include <array>

#include "debug/events.h"   // jnext::dbg::ClientId

class AudioScopeView;

namespace jnext { namespace dbg { class Debugger; struct AudioState; } }

/// Debugger panel showing the decoded AY chips and the audio source controls.
///
/// The Sources strip (the five mute checkboxes) sits above a tab widget:
/// Chips, DAC & Beeper, Log, Tracker, Scopes. Chips shows each AY chip decoded
/// (period, note, cents, tone/noise gates, volume or envelope, level, the
/// envelope) from `Debugger::audio_state()`; its Hex toggle shows the raw
/// 16 x 3 register table instead. DAC & Beeper shows the four DAC channels, the
/// NR 0x84 port families, the beeper and Pi inputs, the host gains and mutes and
/// the table of discarded writes. Scopes shows an oscilloscope trace and a level
/// meter for each of the sixteen mixer sources (AudioScopeView). Log and Tracker
/// are placeholders until their features land.
class AudioPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend (GH #278 WP4b): the AY registers, the
    ///             three live mode signals and the mute mask. Null shows
    ///             nothing and mutes nothing.
    explicit AudioPanel(jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// The client a mute change is attributed to (the backend's MUTATE line).
    /// The manager sets the debugger window's client while the window is open;
    /// CLIENT_NONE otherwise.
    void set_client(jnext::dbg::ClientId by) { client_ = by; }

    /// Update display with current audio state.
    void refresh();

    /// The refresh the manager runs every frame while this panel is on show
    /// with the Chips tab current (the same work as refresh()).
    void refresh_live() { refresh(); }

    /// True while the Chips tab is the current one: the only tab with live data.
    bool chips_on_show() const;

    /// True while the Scopes tab is the current one (GH #536): the debugger window asks the
    /// backend for the source tap only then.
    bool scopes_on_show() const;

    /// The Scopes view's per-frame refresh, run by the manager while the machine runs with this
    /// panel on show and the Scopes tab current (refresh() does the same on a paused redraw).
    void refresh_scopes();

    QSize sizeHint() const override { return QSize(400, 600); }
    QSize minimumSizeHint() const override { return QSize(350, 580); }

signals:
    /// The current sub-tab changed: what the window asks of the backend depends on it (GH #498).
    void sub_tab_changed();

    /// A dropped-write row of CPU source was double-clicked: show the OUT or
    /// NEXTREG that was discarded in the Disassembly (the same signal the
    /// Symbols, CPU and Memory panels use).
    void show_in_disassembly_requested(uint16_t addr);

private:
    void create_ui();
    QWidget* create_chips_tab();
    QWidget* create_dac_tab();
    void refresh_dac_(const jnext::dbg::AudioState& st);
    QWidget* placeholder_tab();

    /// Collect the five source checkboxes into an AudioMute mask (unchecked =
    /// muted) and push it to the backend. Called on every toggle.
    void apply_source_mutes();

    jnext::dbg::Debugger* dbg_;
    jnext::dbg::ClientId  client_ = jnext::dbg::CLIENT_NONE;
    QTableWidget* ay_table_ = nullptr;   // the hex view: the panel's only QTableWidget
    QWidget* decoded_ = nullptr;         // the decoded view
    QCheckBox* hex_box_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QScrollArea* chips_scroll_ = nullptr;
    QScrollArea* dac_scroll_ = nullptr;
    AudioScopeView* scope_view_ = nullptr;

    // The decoded widgets of one chip, found again by refresh().
    struct ChannelWidgets {
        QLabel *period, *note, *cents, *tone, *noise, *vol;
        QProgressBar* level;
    };
    struct ChipWidgets {
        QLabel *active, *addr, *pan, *mono, *mix, *noise_period;
        QLabel *env_glyph, *env_period, *env_note, *env_step;
        std::array<ChannelWidgets, 3> ch;
    };
    std::array<ChipWidgets, 3> chips_{};
    QLabel* psg_mode_label_ = nullptr;
    QLabel* ay_ports_label_ = nullptr;

    // Source checkboxes: CHECKED = audible, UNCHECKED = muted.
    QCheckBox* mute_ay0_ = nullptr;
    QCheckBox* mute_ay1_ = nullptr;
    QCheckBox* mute_ay2_ = nullptr;
    QCheckBox* mute_dac_ = nullptr;
    QCheckBox* mute_beeper_ = nullptr;

    // Info labels
    QLabel* turbosound_label_ = nullptr;
    QLabel* ay_ym_label_ = nullptr;
    QLabel* stereo_label_ = nullptr;
    QLabel* ay_drops_label_ = nullptr;   // Chips header: the AY dropped-write total, a link to DAC & Beeper

    // The DAC & Beeper tab, found again by refresh_dac_().
    QLabel* dac_enabled_ = nullptr;
    std::array<QLabel*, 4> dac_value_{};
    std::array<QProgressBar*, 4> dac_bar_{};
    std::array<QLabel*, 8> dac_family_{};          // [1]..[7]; [0] unused
    QLabel *beeper_ear_ = nullptr, *beeper_mic_ = nullptr, *beeper_tape_ear_ = nullptr;
    QLabel *speaker_enabled_ = nullptr, *beep_speaker_only_ = nullptr, *beeper_excluded_ = nullptr;
    QLabel *pi_left_ = nullptr, *pi_right_ = nullptr, *pi_ear_ = nullptr;
    std::array<QLabel*, 6> gain_{};                // output, AY0, AY1, AY2, DAC, beeper
    QLabel *host_mutes_ = nullptr, *host_synthesis_ = nullptr;
    QTreeWidget* drops_tree_ = nullptr;
};
