#pragma once

#include <QWidget>
#include <QLabel>
#include <QCheckBox>

#include "debugger/tstate_counter.h"

namespace jnext { namespace dbg { class Debugger; } }

/// CPU registers, flags, and MMU panel.
class CpuPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through (GH #278 WP4a);
    ///             null shows nothing.
    explicit CpuPanel(const jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// Update display with current CPU state and MMU.
    void refresh();

    /// Set paused state — when not paused, panel shows gray overlay and skips updates.
    void set_paused(bool paused);

    /// GH #369: count T-states from the machine's current time (value 0), for
    /// the discontinuities the counter cannot see from a paused observation
    /// (a cold boot while running, a rewind jumping forward, the window
    /// opening). Records a start point and draws one label; no paused gate.
    void restart_tstate_count();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void create_ui();

    const jnext::dbg::Debugger* dbg_;
    bool paused_ = false;

    // Register value labels (hex display)
    QLabel* reg_af_ = nullptr;
    QLabel* reg_bc_ = nullptr;
    QLabel* reg_de_ = nullptr;
    QLabel* reg_hl_ = nullptr;
    QLabel* reg_af2_ = nullptr;
    QLabel* reg_bc2_ = nullptr;
    QLabel* reg_de2_ = nullptr;
    QLabel* reg_hl2_ = nullptr;
    QLabel* reg_ix_ = nullptr;
    QLabel* reg_iy_ = nullptr;
    QLabel* reg_sp_ = nullptr;
    QLabel* reg_pc_ = nullptr;
    QLabel* reg_i_ = nullptr;
    QLabel* reg_r_ = nullptr;
    QLabel* reg_iff_ = nullptr;
    QLabel* reg_im_ = nullptr;
    QLabel* reg_halted_ = nullptr;
    QLabel* ula_screen_ = nullptr;

    // GH #369: T-states since the last stop (start point lives in the frontend).
    jnext::TstateCounter tstates_;
    QLabel* tstates_value_ = nullptr;
    QCheckBox* tstates_accumulate_ = nullptr;

    // Individual flag labels
    QLabel* flag_s_ = nullptr;
    QLabel* flag_z_ = nullptr;
    QLabel* flag_h_ = nullptr;
    QLabel* flag_pv_ = nullptr;
    QLabel* flag_n_ = nullptr;
    QLabel* flag_c_ = nullptr;

};
