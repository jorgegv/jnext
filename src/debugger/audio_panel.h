#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QProgressBar>
#include <QScrollArea>
#include <QTabWidget>
#include <QToolButton>

#include <array>
#include <cstdint>

#include "debug/events.h"   // jnext::dbg::ClientId

namespace jnext { namespace dbg { class Debugger; } }

/// Debugger panel showing the decoded AY chips and the audio source controls.
///
/// The Sources strip (the five mute checkboxes) sits above a tab widget:
/// Chips, DAC & Beeper, Log, Tracker, Scopes. Chips shows each AY chip decoded
/// (period, note, cents, tone/noise gates, volume or envelope, level, the
/// envelope) from `Debugger::audio_state()`; its Hex toggle shows the raw
/// 16 x 3 register table instead. Each of the 13 channels (9 AY, 4 DAC) has an
/// M (mute) and an S (solo) button, which act on `AUDIO_CHANNEL_MUTE` only: the
/// Chips tab holds the AY ones, the DAC & Beeper tab the DAC ones. The other
/// tabs are placeholders until their features land.
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

    QSize sizeHint() const override { return QSize(400, 600); }
    QSize minimumSizeHint() const override { return QSize(350, 580); }

private:
    void create_ui();
    QWidget* create_chips_tab();
    QWidget* placeholder_tab();

    /// DAC & Beeper tab: the DAC channel group, the rest not available yet.
    QWidget* create_dac_tab();
    /// The "DAC channels" group: rows A-D, each with M and S.
    QGroupBox* create_dac_channel_group();
    /// M and S buttons of channel `index` (0-8 = 3*chip+channel, 9-12 = DAC A-D)
    /// into `grid` at (`row`, `col`) and (`row`, `col` + 1). `stem` is the
    /// objectName stem ("chip1_c", "dac_d").
    void add_channel_buttons(QGridLayout* grid, int row, int col, int index, const QString& stem);

    /// Mute / solo clicks on channel `index` (see the .cpp for the rules).
    void channel_mute_clicked(int index);
    void channel_solo_clicked(int index);
    /// Cancel the solo when `mask` is no longer the one it set, then show `mask`
    /// on every button.
    void settle_channel_buttons(uint16_t mask);

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

    // Per-channel mute / solo. The solo is the panel's: `solo_` is the soloed
    // channel (-1 = none) and `pre_solo_mask_` the channel mask to restore when
    // it ends. Neither is machine state.
    std::array<QToolButton*, 13> mute_btn_{};
    std::array<QToolButton*, 13> solo_btn_{};
    int      solo_ = -1;
    uint16_t pre_solo_mask_ = 0;

    // Info labels
    QLabel* turbosound_label_ = nullptr;
    QLabel* ay_ym_label_ = nullptr;
    QLabel* stereo_label_ = nullptr;
};
