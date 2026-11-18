#pragma once

#include <QWidget>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>

#include <array>

#include "debug/events.h"   // jnext::dbg::ClientId
#include "debugger/tstate_counter.h"

namespace jnext { namespace dbg { class Debugger; } }

/// CPU registers, flags, and MMU panel.
class CpuPanel : public QWidget {
    Q_OBJECT
public:
    /// @param dbg  the debugger backend every read goes through (GH #278 WP4a),
    ///             and, since GH #368, every register write (`set_register`);
    ///             null shows nothing and writes nothing.
    explicit CpuPanel(jnext::dbg::Debugger* dbg, QWidget* parent = nullptr);

    /// The client a register edit is attributed to (the backend's MUTATE line).
    /// The manager sets the debugger window's client while the window is open.
    void set_client(jnext::dbg::ClientId by) { client_ = by; }

    /// Update display with current CPU state and MMU.
    void refresh();

    /// Set paused state — when not paused, panel shows gray overlay and skips updates.
    /// Editing is a paused-only action: an open editor is cancelled, never committed,
    /// when the machine resumes.
    void set_paused(bool paused);

    /// GH #369: count T-states from the machine's current time (value 0), where
    /// the start matters and `time_epoch()` alone cannot give it: a cold boot
    /// while running (count from the reset, not from the next stop) and the
    /// window opening. Records a start point and draws one label; no paused gate.
    void restart_tstate_count();

signals:
    /// A refused or malformed edit, for the debugger window's status bar.
    void message(const QString& text);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void create_ui();

    // GH #368: edit in place. The value widgets stay QLabels; one QLineEdit
    // (editor_, in no layout) is laid over the label being edited.
    void begin_edit(int field);
    void commit_edit();
    void end_edit();
    void click_flag(int bit);
    void show_value_menu(int field, const QPoint& global_pos);

    jnext::dbg::Debugger* dbg_;
    jnext::dbg::ClientId client_ = jnext::dbg::CLIENT_NONE;
    bool paused_ = false;

    // The 16 value labels, in regedit::fields() order.
    std::array<QLabel*, 16> values_{};
    std::array<QLabel*, 6> flag_labels_{};
    QLineEdit* editor_ = nullptr;
    int edit_field_ = -1;

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
