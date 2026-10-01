#pragma once

#include <QWidget>
#include <QTableWidget>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>

#include "debug/events.h"   // jnext::dbg::ClientId

namespace jnext { namespace dbg { class Debugger; } }

/// Debugger panel showing AY register state and audio source controls.
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

    QSize sizeHint() const override { return QSize(400, 600); }
    QSize minimumSizeHint() const override { return QSize(350, 580); }

private:
    void create_ui();

    /// Collect the five source checkboxes into an AudioMute mask (unchecked =
    /// muted) and push it to the backend. Called on every toggle.
    void apply_source_mutes();

    jnext::dbg::Debugger* dbg_;
    jnext::dbg::ClientId  client_ = jnext::dbg::CLIENT_NONE;
    QTableWidget* ay_table_ = nullptr;

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
};
