#include "debugger/debugger_window.h"
#include "debugger/cpu_panel.h"
#include "debugger/disasm_panel.h"
#include "debugger/memory_panel.h"
#include "debugger/video_panel.h"
#include "debugger/sprite_panel.h"
#include "debugger/copper_panel.h"
#include "debugger/nextreg_panel.h"
#include "debugger/audio_panel.h"
#include "debugger/watch_panel.h"
#include "debugger/breakpoint_panel.h"
#include "debugger/mmu_panel.h"
#include "debugger/stack_panel.h"
#include "debugger/callstack_panel.h"
#include "core/emulator.h"
#include "core/rzx_player.h"
#include "debug/breakpoints.h"
#include "debug/debug_state.h"
#include "debug/rewind_buffer.h"

#include "debugger/debugger_manager.h"
#include "debugger/window_attach.h"

#include <QCloseEvent>
#include <QGuiApplication>
#include <QScreen>
#include <QTimer>
#include <QToolBar>
#include <QMenuBar>
#include <QPushButton>
#include <QLabel>
#include <QPainter>
#include <QSettings>
#include <QDataStream>
#include <QSplitter>
#include <QScrollArea>
#include <QStyle>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QDialog>
#include <QFormLayout>
#include <QLineEdit>
#include <QDialogButtonBox>
#include <QSlider>
#include <QStatusBar>
#include <QSpinBox>
#include <QSignalBlocker>
#include <QDir>

namespace {
// Debugger window geometry lives alongside the main GUI config under ~/.jnext
// (see src/gui/app_config.cpp), a plain INI file rather than the platform
// default (~/.config/JNEXT/...), so all of jnext's state sits in one place.
// JNEXT_CONFIG_DIR overrides the directory (matches AppConfig) for test
// isolation.
QString jnext_config_dir() {
    QString dir = qEnvironmentVariable("JNEXT_CONFIG_DIR");
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.jnext");
    return dir;
}
QString debugger_config_path() {
    return jnext_config_dir() + QStringLiteral("/Debugger.conf");
}

// GH #114 — the size the debugger opens at when nothing is saved. Unchanged
// from before the issue: a user with room must see exactly what they saw.
constexpr int kDefaultWidth  = 1170;
constexpr int kDefaultHeight = 900;

// Floor for a size read back from the config file. Not a usability minimum
// (the window may legitimately be tiny now — that is the whole point of the
// issue); purely a sanity gate against a corrupt or truncated entry.
constexpr int kSaneMinWidth  = 200;
constexpr int kSaneMinHeight = 150;

jnext::AttachRect to_attach_rect(const QRect& r) {
    return jnext::AttachRect{r.x(), r.y(), r.width(), r.height()};
}

/// Work area of the screen `w` is on (falling back to the primary screen for a
/// window that has not been shown yet). An empty rect when Qt knows of no
/// screen at all — the clamp helpers treat that as "do not clamp".
jnext::AttachRect work_area_of(const QWidget* w) {
    const QScreen* scr = w->screen() ? w->screen() : QGuiApplication::primaryScreen();
    return scr ? to_attach_rect(scr->availableGeometry()) : jnext::AttachRect{};
}

/// Height the window manager will add above the client area for the title bar.
/// resize() sizes the CLIENT area but it is the FRAME that has to fit on the
/// screen, and PM_TitleBarHeight is the only decoration metric available before
/// the window has been mapped.
int title_bar_height(const QWidget* w) {
    return w->style()->pixelMetric(QStyle::PM_TitleBarHeight, nullptr, w);
}
} // namespace

DebuggerWindow::DebuggerWindow(Emulator* emulator, QWidget* parent)
    : QMainWindow(parent)
    , emulator_(emulator)
{
    setWindowTitle(tr("JNEXT Debugger"));
    create_panels();

    // GH #114: no hard minimum width. The panel area scrolls (see
    // create_panels), so the window may be shrunk to whatever the menu bar and
    // the button bar need, and the panels are panned instead of cut off.
    const jnext::AttachRect work = work_area_of(this);
    const int deco_h = title_bar_height(this);

    // Issue #39: first gate on whether this window may position itself. On
    // Wayland a client cannot place its own toplevel (xdg-shell has no such
    // request) and Qt drops the move silently, so attachment is not merely off
    // there — it is impossible, and the menu says so rather than offering a
    // switch that does nothing. This is only a SEED: position_next_to() also
    // measures whether moves actually land and latches this false if they do
    // not, which is what catches backends that claim to support positioning and
    // then ignore it anyway.
    attach_platform_blocked_ = !jnext::platform_supports_window_positioning(
        QGuiApplication::platformName().toUtf8().constData());
    attach_supported_ = !attach_platform_blocked_;

    QSettings settings(debugger_config_path(), QSettings::IniFormat);

    // Restore saved size. Position is restored only when the window is NOT
    // attached — an attached window's position is derived from the emulator
    // window, so restoring one would just be overwritten on the first move.
    int want_w = kDefaultWidth;
    int want_h = kDefaultHeight;
    QByteArray saved_size = settings.value("debugger/size").toByteArray();
    if (!saved_size.isEmpty()) {
        QDataStream ds(saved_size);
        int w = 0, h = 0;
        ds >> w >> h;
        // GH #114: any sane saved size is honoured, not just one at least as
        // wide as the old 1170 minimum — deliberately shrinking the window and
        // having it come back that way is the point of the issue.
        if (w >= kSaneMinWidth && h >= kSaneMinHeight) {
            want_w = w;
            want_h = h;
            size_is_default_ = false;
        }
    }
    // GH #114: and whichever size that is — default or saved — is clamped to
    // the screen. A geometry saved on a large monitor and restored on a small
    // one is the back door through which the unreachable window returns.
    const jnext::WindowSize fit =
        jnext::clamp_window_size_to_screen(want_w, want_h, work, deco_h);
    resize(fit.w, fit.h);

    attach_enabled_ = settings.value("debugger/attached", true).toBool();

    if (!attach_enabled_ || !attach_supported_) {
        // Detached (by choice or by platform): honour the saved position so a
        // debugger deliberately parked on a second monitor comes back there.
        QByteArray saved_pos = settings.value("debugger/position").toByteArray();
        if (!saved_pos.isEmpty()) {
            QDataStream ds(saved_pos);
            int x, y;
            ds >> x >> y;
            // Only accept a position that still lands on a screen that exists —
            // a monitor may have been unplugged since it was saved, and an
            // unreachable debugger window is precisely what this issue forbids.
            for (const QScreen* s : QGuiApplication::screens()) {
                const QRect avail = s->availableGeometry();
                if (avail.contains(QPoint(x, y))) {
                    // GH #114: an on-screen top-left is not enough — the window
                    // restored onto a smaller monitor can still hang off the
                    // right or the bottom, taking the button bar with it. Pull
                    // it fully inside that screen's work area.
                    const jnext::AttachPlacement p = jnext::clamp_window_to_work_area(
                        x, y, width(), height(), to_attach_rect(avail), deco_h);
                    if (p.reposition)
                        move(p.x, p.y);
                    break;
                }
            }
        }
    }
}

void DebuggerWindow::closeEvent(QCloseEvent* event) {
    // Task 60e: window_closed() runs DebuggerManager::set_enabled(false)
    // synchronously (direct connection), which auto-resumes the machine. That
    // path is gated — if the user declines to resume a corrupt machine the
    // debugger stays enabled. Detect that via is_enabled() and keep this window
    // OPEN (ignore the close) rather than hiding a still-live debugger.
    emit window_closed();
    if (debugger_mgr_ && debugger_mgr_->is_enabled()) {
        event->ignore();
        return;
    }
    save_geometry();
    event->accept();
}

void DebuggerWindow::set_debugger_manager(DebuggerManager* mgr) {
    debugger_mgr_ = mgr;

    // Create the menu bar with debug actions.
    create_menus();

    // Create the debug controls toolbar at the bottom.
    // Layout: F2:Trace(ball) | Export Trace | --> | Continue | Single Step | Step Over | Step Out | Break |
    auto* toolbar = new QToolBar(tr("Debug Controls"), this);
    toolbar->setMovable(false);

    // F2: Trace On/Off toggle with integrated ball icon
    trace_toggle_btn_ = new QPushButton(tr("F2: Trace"), this);
    update_trace_indicator();
    connect(trace_toggle_btn_, &QPushButton::clicked, this, [this]() {
        if (!emulator_) return;
        bool new_state = !emulator_->trace_log().enabled();
        emulator_->trace_log().set_enabled(new_state);
        if (trace_enable_action_)
            trace_enable_action_->setChecked(new_state);
        update_trace_indicator();
        // Step Back's enabled-state is gated on the trace (Task 27 A1b) —
        // refresh it now rather than waiting for the next pause/step.
        update_actions(emulator_->debug_state().paused());
    });
    toolbar->addWidget(trace_toggle_btn_);

    // F3: Export Trace
    auto* export_trace_btn = new QPushButton(tr("F3: Export Trace"), this);
    connect(export_trace_btn, &QPushButton::clicked, this, [this]() {
        if (!emulator_) return;
        QString path = QFileDialog::getSaveFileName(
            this, tr("Export Trace Log"), QString(),
            tr("Text Files (*.txt);;All Files (*)"));
        if (!path.isEmpty()) {
            bool ok = emulator_->trace_log().export_to_file(path.toStdString());
            if (!ok) {
                QMessageBox::warning(this, tr("Export Failed"),
                    tr("Could not write trace log to:\n%1").arg(path));
            }
        }
    });
    toolbar->addWidget(export_trace_btn);

    // Spacer to push execution controls to the right
    auto* spacer = new QWidget(this);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolbar->addWidget(spacer);

    // Execution controls on the right
    auto* continue_btn = new QPushButton(tr("F5: Continue"), this);
    connect(continue_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_run);
    toolbar->addWidget(continue_btn);

    auto* frame_back_btn = new QPushButton(tr("|\u25C4 Frame Back"), this);
    frame_back_btn->setToolTip(tr("Rewind to previous frame (Shift+F6)"));
    connect(frame_back_btn, &QPushButton::clicked, this, [this]() {
        if (debugger_mgr_ && emulator_ && emulator_->rewind_buffer()
                && !emulator_->rewind_buffer()->empty()) {
            uint32_t prev = emulator_->frame_num() > 0
                ? emulator_->frame_num() - 1 : 0;
            debugger_mgr_->on_rewind_to_frame(prev);
        }
    });
    toolbar->addWidget(frame_back_btn);

    auto* step_back_btn = new QPushButton(tr("\u25C4 Step Back"), this);
    step_back_btn->setToolTip(tr("Step back one instruction (Shift+F7)"));
    connect(step_back_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_back);
    toolbar->addWidget(step_back_btn);

    auto* step_into_btn = new QPushButton(tr("F6: Single Step"), this);
    connect(step_into_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_into);
    toolbar->addWidget(step_into_btn);

    auto* step_over_btn = new QPushButton(tr("F7: Step Over"), this);
    connect(step_over_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_over);
    toolbar->addWidget(step_over_btn);

    auto* step_out_btn = new QPushButton(tr("F8: Step Out"), this);
    connect(step_out_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_step_out);
    toolbar->addWidget(step_out_btn);

    auto* run_to_eosl_btn = new QPushButton(tr("Run to EOSL"), this);
    run_to_eosl_btn->setToolTip(tr("Run to End of Scan Line"));
    connect(run_to_eosl_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_run_to_eosl);
    toolbar->addWidget(run_to_eosl_btn);

    auto* run_to_eof_btn = new QPushButton(tr("Run to EOF"), this);
    run_to_eof_btn->setToolTip(tr("Run to End of Frame"));
    connect(run_to_eof_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_run_to_eof);
    toolbar->addWidget(run_to_eof_btn);

    auto* break_btn = new QPushButton(tr("F9: Break"), this);
    connect(break_btn, &QPushButton::clicked, mgr, &DebuggerManager::on_pause);
    toolbar->addWidget(break_btn);

    addToolBar(Qt::BottomToolBarArea, toolbar);

    // --- Rewind slider toolbar (second bottom toolbar, initially hidden) ---
    rewind_toolbar_ = new QToolBar(tr("Rewind"), this);
    rewind_toolbar_->setMovable(false);

    auto* rewind_label = new QLabel(tr("  \u23EE Rewind — Frame: "), this);
    rewind_toolbar_->addWidget(rewind_label);

    rewind_slider_ = new QSlider(Qt::Horizontal, this);
    rewind_slider_->setMinimumWidth(300);
    rewind_slider_->setToolTip(tr("Drag to select a frame to rewind to"));
    connect(rewind_slider_, &QSlider::sliderPressed, this, [this]() {
        rewind_slider_dragging_ = true;
    });
    connect(rewind_slider_, &QSlider::sliderMoved, this, [this](int value) {
        if (rewind_frame_label_)
            rewind_frame_label_->setText(tr("Frame %1").arg(value));
    });
    connect(rewind_slider_, &QSlider::sliderReleased, this, [this]() {
        rewind_slider_dragging_ = false;
        // Trigger rewind immediately on release (no need to press Jump Here separately).
        if (debugger_mgr_)
            debugger_mgr_->on_rewind_to_frame(
                static_cast<uint32_t>(rewind_slider_->value()));
    });
    rewind_toolbar_->addWidget(rewind_slider_);

    rewind_frame_label_ = new QLabel(tr("---"), this);
    rewind_frame_label_->setMinimumWidth(120);
    rewind_toolbar_->addWidget(rewind_frame_label_);

    rewind_jump_btn_ = new QPushButton(tr("Jump Here"), this);
    connect(rewind_jump_btn_, &QPushButton::clicked, this, [this]() {
        if (!debugger_mgr_ || !rewind_slider_) return;
        uint32_t frame = static_cast<uint32_t>(rewind_slider_->value());
        debugger_mgr_->on_rewind_to_frame(frame);
    });
    rewind_toolbar_->addWidget(rewind_jump_btn_);

    rewind_toolbar_->setVisible(false);
    addToolBar(Qt::BottomToolBarArea, rewind_toolbar_);
}

void DebuggerWindow::showEvent(QShowEvent* event) {
    QMainWindow::showEvent(event);
    if (initial_fit_done_)
        return;
    initial_fit_done_ = true;
    // Queued, not immediate: the scroll range this reads is only correct once
    // the shown window has been laid out.
    QTimer::singleShot(0, this, [this]() { grow_default_size_to_natural(); });
}

void DebuggerWindow::grow_default_size_to_natural() {
    // GH #114: a window opening at its DEFAULT size still opens at the size
    // where nothing has to scroll, whenever the screen has room for it. That
    // size used to be enforced by Qt as the window's minimum (1170x1069 with
    // this panel set), which is exactly what made the window unshrinkable — so
    // reproducing it here is what keeps the promise that a user with a big
    // enough monitor sees precisely what they saw before.
    //
    // MEASURED, not estimated: the scroll range IS the amount of panel area
    // currently out of view, so adding it to the window size is the size at
    // which the scrollbars disappear. Summing the panels' minimums and the
    // chrome around them was tried first and got the right answer only on the
    // machine it was written on — the panels' minimums are not final until the
    // widgets have been polished and laid out, which is why this runs off the
    // first show rather than at construction.
    //
    // A size restored from the config file is left alone: a window the user
    // deliberately made small must come back small.
    auto* scroll = qobject_cast<QScrollArea*>(centralWidget());
    if (!size_is_default_ || !scroll)
        return;

    const int hidden_w = scroll->horizontalScrollBar()->maximum();
    const int hidden_h = scroll->verticalScrollBar()->maximum();
    if (hidden_w <= 0 && hidden_h <= 0)
        return;   // nothing is out of view; the window is already big enough

    const jnext::WindowSize fit = jnext::clamp_window_size_to_screen(
        width() + hidden_w, height() + hidden_h,
        work_area_of(this), title_bar_height(this));
    if (fit.w == width() && fit.h == height())
        return;   // the screen has no more room to give
    resize(fit.w, fit.h);

    // A panel's minimum can itself change as the window grows (a wider panel
    // may need more height), so one pass lands close but not always exactly.
    // Repeat while it keeps helping, with a hard bound: each pass either grows
    // the window or returns above, so this terminates on its own — the bound is
    // there so a pathological layout cannot ping-pong forever.
    if (++initial_fit_passes_ < kMaxInitialFitPasses)
        QTimer::singleShot(0, this, [this]() { grow_default_size_to_natural(); });
}

void DebuggerWindow::create_menus() {
    QMenuBar* bar = menuBar();

    // --- Debug menu ---
    QMenu* debug_menu = bar->addMenu(tr("&Debug"));

    run_action_ = debug_menu->addAction(tr("Run / &Continue"));
    run_action_->setShortcut(QKeySequence(Qt::Key_F5));
    connect(run_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_run);

    // Mnemonic on "Pause", not on "Break": Alt+B belongs to "Step &Back"
    // below. Two items in one menu cannot share a mnemonic (issue #124).
    pause_action_ = debug_menu->addAction(tr("&Pause / Break"));
    pause_action_->setShortcut(QKeySequence(Qt::Key_F9));
    connect(pause_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_pause);

    debug_menu->addSeparator();

    step_into_action_ = debug_menu->addAction(tr("&Single Step"));
    step_into_action_->setShortcut(QKeySequence(Qt::Key_F6));
    connect(step_into_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_step_into);

    step_over_action_ = debug_menu->addAction(tr("Step &Over"));
    step_over_action_->setShortcut(QKeySequence(Qt::Key_F7));
    connect(step_over_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_step_over);

    // Alt+U, not Alt+T: "&Trace" below owns T (issue #124).
    step_out_action_ = debug_menu->addAction(tr("Step O&ut"));
    step_out_action_->setShortcut(QKeySequence(Qt::Key_F8));
    connect(step_out_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_step_out);

    // Alt+K: F is reserved for "Run to End of &Frame", which pairs with
    // "Run to End of Scan &Line" (issue #124).
    QAction* frame_back_action = debug_menu->addAction(tr("|< Frame Bac&k"));
    frame_back_action->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F6));
    connect(frame_back_action, &QAction::triggered, this, [this]() {
        if (debugger_mgr_ && emulator_ && emulator_->rewind_buffer()
                && !emulator_->rewind_buffer()->empty()) {
            uint32_t prev = emulator_->frame_num() > 0
                ? emulator_->frame_num() - 1 : 0;
            debugger_mgr_->on_rewind_to_frame(prev);
        }
    });

    step_back_action_ = debug_menu->addAction(tr("Step &Back"));
    step_back_action_->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F7));
    connect(step_back_action_, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_step_back);

    debug_menu->addSeparator();

    QAction* run_to_eof_action = debug_menu->addAction(tr("Run to End of &Frame"));
    connect(run_to_eof_action, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_run_to_eof);

    QAction* run_to_eosl_action = debug_menu->addAction(tr("Run to End of Scan &Line"));
    connect(run_to_eosl_action, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_run_to_eosl);

    debug_menu->addSeparator();

    // Trace submenu
    QMenu* trace_menu = debug_menu->addMenu(tr("&Trace"));

    trace_enable_action_ = trace_menu->addAction(tr("&Enable Trace"));
    trace_enable_action_->setCheckable(true);
    trace_enable_action_->setShortcut(QKeySequence(Qt::Key_F2));
    connect(trace_enable_action_, &QAction::triggered, this, [this](bool checked) {
        if (emulator_) {
            emulator_->trace_log().set_enabled(checked);
            update_trace_indicator();
            // Step Back's enabled-state is gated on the trace (Task 27 A1b).
            update_actions(emulator_->debug_state().paused());
        }
    });

    QAction* clear_trace = trace_menu->addAction(tr("&Clear Trace"));
    connect(clear_trace, &QAction::triggered, this, [this]() {
        if (emulator_)
            emulator_->trace_log().clear();
    });

    QAction* export_trace = trace_menu->addAction(tr("E&xport Trace..."));
    export_trace->setShortcut(QKeySequence(Qt::Key_F3));
    connect(export_trace, &QAction::triggered, this, [this]() {
        if (!emulator_) return;
        QString path = QFileDialog::getSaveFileName(
            this, tr("Export Trace Log"), QString(),
            tr("Text Files (*.txt);;All Files (*)"));
        if (!path.isEmpty()) {
            bool ok = emulator_->trace_log().export_to_file(path.toStdString());
            if (!ok) {
                QMessageBox::warning(this, tr("Export Failed"),
                    tr("Could not write trace log to:\n%1").arg(path));
            }
        }
    });

    // Rewind submenu
    debug_menu->addSeparator();
    QMenu* rewind_menu = debug_menu->addMenu(tr("&Rewind"));

    rewind_enable_action_ = rewind_menu->addAction(tr("&Enable Rewind"));
    rewind_enable_action_->setCheckable(true);
    rewind_enable_action_->setChecked(emulator_ && emulator_->rewind_buffer() != nullptr);
    connect(rewind_enable_action_, &QAction::triggered, this, [this](bool checked) {
        if (!emulator_) return;
        if (checked) {
            if (!emulator_->rewind_buffer()) {
                // Live allocation (Task 27 A1b): no restart needed. The
                // buffer is mmap-backed (Task 27 A1), so the memory is
                // faulted lazily as snapshots are taken. This also enables
                // snapshotting and the instruction trace (step_back() needs
                // it) — see Emulator::resize_rewind_buffer(). Snapshots
                // start from the next completed frame.
                emulator_->resize_rewind_buffer(last_rewind_frames_);
            } else {
                // Buffer exists but snapshotting is paused — resume it.
                emulator_->set_rewind_enabled(true);
                // Re-assert the instruction trace: step_back() cannot work
                // without it, and it may have been manually disabled via
                // Debug > Trace while rewind was off.
                emulator_->trace_log().set_enabled(true);
            }
            update_trace_indicator();
        } else {
            // Disable = keep-but-pause (the pre-A1b semantics of this
            // toggle): snapshotting stops but the buffer and its recorded
            // history are retained, so the user can still rewind into the
            // past. To free the memory, use Rewind Buffer Size... = 0.
            emulator_->set_rewind_enabled(false);
        }
        update_rewind_ui();
        // The toggle can flip the trace on (resume branch), which gates
        // Step Back's enabled-state — refresh it immediately.
        update_actions(emulator_->debug_state().paused());
    });

    QAction* rewind_size_action = rewind_menu->addAction(tr("Rewind &Buffer Size..."));
    connect(rewind_size_action, &QAction::triggered, this, &DebuggerWindow::show_rewind_buffer_size_dialog);

    // --- Map menu ---
    QMenu* map_menu = bar->addMenu(tr("&Map"));

    QMenu* load_map_menu = map_menu->addMenu(tr("&Load MAP File"));
    QAction* z88dk_action = load_map_menu->addAction(tr("&Z88DK Format..."));
    connect(z88dk_action, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_load_map_z88dk);
    QAction* simple_action = load_map_menu->addAction(tr("&Simple Format (48K ROM)..."));
    connect(simple_action, &QAction::triggered, debugger_mgr_, &DebuggerManager::on_load_map_simple);

    // --- Breakpoints menu ---
    QMenu* bp_menu = bar->addMenu(tr("&Breakpoints"));

    // GH #215 — Execute (the ordinary PC breakpoint) was reachable only from
    // the Breakpoints PANEL's Add dialog, so the menu offered every type
    // EXCEPT the one users reach for first. Listed first, and lettered E:
    // R/W/B/C are already taken by the four entries below (issue #124).
    QAction* add_exec_bp = bp_menu->addAction(tr("Add &Execute Breakpoint..."));
    connect(add_exec_bp, &QAction::triggered, this, [this]() {
        show_add_exec_bp_dialog();
    });

    QAction* add_read_bp = bp_menu->addAction(tr("Add &Read Breakpoint..."));
    connect(add_read_bp, &QAction::triggered, this, [this]() {
        show_add_data_bp_dialog(WatchType::READ);
    });

    QAction* add_write_bp = bp_menu->addAction(tr("Add &Write Breakpoint..."));
    connect(add_write_bp, &QAction::triggered, this, [this]() {
        show_add_data_bp_dialog(WatchType::WRITE);
    });

    // Alt+B: W already belongs to "Add &Write Breakpoint..." (issue #124).
    QAction* add_rw_bp = bp_menu->addAction(tr("Add Read/Write &Breakpoint..."));
    connect(add_rw_bp, &QAction::triggered, this, [this]() {
        show_add_data_bp_dialog(WatchType::READ_WRITE);
    });

    bp_menu->addSeparator();

    QAction* clear_all_bp = bp_menu->addAction(tr("&Clear All Breakpoints"));
    connect(clear_all_bp, &QAction::triggered, this, [this]() {
        emulator_->debug_state().breakpoints().clear_all_pc();
        emulator_->debug_state().breakpoints().clear_all_watchpoints();
    });

    // --- Watches menu ---
    QMenu* watches_menu = bar->addMenu(tr("&Watches"));

    QAction* add_watch = watches_menu->addAction(tr("&Add Watch..."));
    connect(add_watch, &QAction::triggered, this, [this]() {
        if (watch_panel_)
            watch_panel_->on_add_watch();
    });

    // --- Window menu (issue #39) ---
    // Alt+N, not Alt+W: "&Watches" above already claims W. When both did,
    // Qt saw an AMBIGUOUS shortcut and opened them alternately — Watches on
    // odd presses, Window on even — so neither could be opened on purpose
    // (issue #124). Watches keeps W as the incumbent.
    QMenu* window_menu = bar->addMenu(tr("Wi&ndow"));

    attach_action_ = window_menu->addAction(tr("&Attach to Emulator Window"));
    attach_action_->setCheckable(true);
    attach_action_->setChecked(attach_enabled_ && attach_supported_);
    connect(attach_action_, &QAction::toggled, this, &DebuggerWindow::set_attach_enabled);

    if (attach_platform_blocked_) {
        // Do not offer a switch that cannot do anything. Wayland gives a client
        // no way to position its own toplevel, and a move() there is dropped
        // without error — silently misbehaving is exactly what issue #39
        // reports, so say so instead.
        attach_action_->setEnabled(false);
        attach_action_->setToolTip(
            tr("Unavailable: %1")
                .arg(QString::fromUtf8(
                    jnext::attach_status_reason(jnext::AttachStatus::Unsupported))));
        window_menu->setToolTipsVisible(true);
    }
}

void DebuggerWindow::update_actions(bool is_paused) {
    if (run_action_)       run_action_->setEnabled(is_paused);
    if (pause_action_)     pause_action_->setEnabled(!is_paused);
    if (step_into_action_) step_into_action_->setEnabled(is_paused);
    if (step_over_action_) step_over_action_->setEnabled(is_paused);
    if (step_out_action_)  step_out_action_->setEnabled(is_paused);

    // Rewinding (frame jump) is only available when paused, the rewind
    // buffer has snapshots, and RZX playback is not active.
    bool can_rewind = is_paused
        && emulator_
        && emulator_->rewind_buffer()
        && !emulator_->rewind_buffer()->empty()
        && !emulator_->rzx_player().is_playing();
    // Step Back additionally needs the instruction trace to locate the
    // target instruction's cycle — Emulator::step_back() fails loudly
    // without it (Task 27 A2), so grey the action out instead.
    bool can_step_back = can_rewind && emulator_->trace_log().enabled();
    if (step_back_action_) step_back_action_->setEnabled(can_step_back);
    if (rewind_jump_btn_)  rewind_jump_btn_->setEnabled(can_rewind);
}

void DebuggerWindow::save_position() {
    save_geometry();
}

void DebuggerWindow::save_geometry() {
    // Ensure the config dir exists — QSettings will not persist to a missing dir.
    QDir().mkpath(jnext_config_dir());
    QSettings settings(debugger_config_path(), QSettings::IniFormat);
    QByteArray data;
    QDataStream ds(&data, QIODevice::WriteOnly);
    ds << width() << height();
    settings.setValue("debugger/size", data);

    settings.setValue("debugger/attached", attach_enabled_);

    // Issue #39: only a DETACHED window owns its position. An attached one
    // derives it from the emulator window, so saving it would pin a stale
    // coordinate that the next attach immediately overrides.
    if (!attach_enabled_ || !attach_supported_) {
        QByteArray pos;
        QDataStream pds(&pos, QIODevice::WriteOnly);
        pds << x() << y();
        settings.setValue("debugger/position", pos);
    }
}

void DebuggerWindow::restore_geometry() {
    // No-op: size is restored in constructor, position is set by MainWindow.
}

void DebuggerWindow::update_trace_indicator() {
    if (!trace_toggle_btn_) return;
    bool active = emulator_ && emulator_->trace_log().enabled();
    QColor color = active ? QColor(0x00, 0xC0, 0x00) : QColor(0xC0, 0x00, 0x00);

    QPixmap pix(14, 14);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setBrush(color);
    p.setPen(QPen(QColor(0x40, 0x40, 0x40), 1));
    p.drawEllipse(1, 1, 12, 12);
    p.end();

    trace_toggle_btn_->setIcon(QIcon(pix));
    trace_toggle_btn_->setIconSize(QSize(14, 14));

    if (trace_enable_action_)
        trace_enable_action_->setChecked(active);
}

void DebuggerWindow::update_rewind_ui() {
    if (!emulator_) return;

    auto* rb = emulator_->rewind_buffer();
    bool has_rewind = rb && !rb->empty() && rb->depth() > 1;

    // Show/hide the rewind toolbar
    if (rewind_toolbar_)
        rewind_toolbar_->setVisible(has_rewind);

    bool is_paused = emulator_->debug_state().paused();
    if (has_rewind && !rewind_slider_dragging_) {
        // Always update the range; only move the thumb when running (not paused).
        // When paused, the user controls the slider position.
        if (rewind_slider_) {
            rewind_slider_->blockSignals(true);
            rewind_slider_->setRange(
                static_cast<int>(rb->oldest_frame_num()),
                static_cast<int>(rb->newest_frame_num()));
            if (!is_paused)
                rewind_slider_->setValue(static_cast<int>(emulator_->frame_num()));
            rewind_slider_->blockSignals(false);
        }
        if (rewind_frame_label_) {
            rewind_frame_label_->setText(
                tr("Frame %1 / %2")
                    .arg(emulator_->frame_num())
                    .arg(rb->newest_frame_num()));
        }
    }

    // Status bar indicator
    if (rb && !rb->empty()) {
        bool is_rewound = emulator_->frame_num() < rb->newest_frame_num();
        if (is_rewound) {
            statusBar()->showMessage(
                tr("\u23EE Rewound: frame %1 of %2  (F5 / Continue to resume)")
                    .arg(emulator_->frame_num())
                    .arg(rb->newest_frame_num()));
        } else {
            size_t mb = (rb->depth() * rb->snapshot_bytes() + 524288) / 1048576;
            statusBar()->showMessage(
                tr("\u23EE Rewind: %1 frames / %2 MB")
                    .arg(rb->depth())
                    .arg(mb));
        }
    } else {
        statusBar()->clearMessage();
    }

    // Sync menu checkmark
    if (rewind_enable_action_)
        rewind_enable_action_->setChecked(rb != nullptr && emulator_->rewind_enabled());
}

void DebuggerWindow::show_rewind_buffer_size_dialog() {
    if (!emulator_) return;

    QDialog dlg(this);
    dlg.setWindowTitle(tr("Rewind Buffer Size"));
    dlg.setMinimumWidth(360);

    auto* form = new QFormLayout(&dlg);

    auto* spin = new QSpinBox(&dlg);
    spin->setRange(0, 2000);
    spin->setSuffix(tr(" frames"));
    int current_frames = emulator_->rewind_buffer()
        ? static_cast<int>(emulator_->rewind_buffer()->depth()) : 0;
    // Show the max capacity (depth() is current fill, not capacity)
    // Use snapshot_bytes to estimate from allocated memory
    // Since we don't expose max_frames, use config value as best estimate
    spin->setValue(current_frames > 0 ? current_frames : last_rewind_frames_);

    size_t snap_bytes = emulator_->rewind_buffer()
        ? emulator_->rewind_buffer()->snapshot_bytes() : 0;
    QString info;
    if (snap_bytes > 0) {
        size_t est_mb = (static_cast<uint64_t>(spin->value()) * snap_bytes + 524288) / 1048576;
        info = tr("~%1 MB at %2 frames").arg(est_mb).arg(spin->value());
    } else {
        info = tr("Rewind is currently disabled. Enter frame count to enable.");
    }
    auto* info_label = new QLabel(info, &dlg);
    info_label->setWordWrap(true);

    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, [&](int v) {
        if (snap_bytes > 0) {
            size_t est_mb = (static_cast<uint64_t>(v) * snap_bytes + 524288) / 1048576;
            info_label->setText(tr("~%1 MB at %2 frames").arg(est_mb).arg(v));
        }
    });

    form->addRow(tr("Buffer size:"), spin);
    form->addRow(info_label);

    auto* note = new QLabel(
        tr("Note: resizing clears all existing snapshots."), &dlg);
    note->setWordWrap(true);
    form->addRow(note);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return;

    int new_frames = spin->value();
    if (new_frames > 0)
        last_rewind_frames_ = new_frames;  // Session default for Enable Rewind
    emulator_->resize_rewind_buffer(new_frames);
    update_rewind_ui();
}

void DebuggerWindow::position_next_to(QWidget* main_win) {
    if (!main_win) return;

    // Frame geometry, not the client rect: attaching to the client right edge
    // tucks this window under the emulator's decoration by the border width.
    const QRect mf = main_win->frameGeometry();

    // The work area of the screen the emulator window is actually on — not the
    // primary screen, which would fling the debugger to another monitor.
    const QScreen* scr = main_win->screen();
    if (!scr) scr = QGuiApplication::primaryScreen();
    const QRect avail = scr ? scr->availableGeometry() : QRect();

    const jnext::AttachDecision d = jnext::compute_attached_placement(
        jnext::AttachRect{mf.x(), mf.y(), mf.width(), mf.height()},
        frameGeometry().width(), frameGeometry().height(),
        jnext::AttachRect{avail.x(), avail.y(), avail.width(), avail.height()},
        attach_enabled_, main_win->isFullScreen(), attach_supported_);

    if (!d.placement.reposition)
        return;

    move(d.placement.x, d.placement.y);

    // Second gate (see window_attach.h): the platform NAME is not proof the
    // move took effect. On the development machine an xcb (XWayland) session
    // reported "xcb" and dropped the move anyway. So check that the window
    // actually landed where it was told, and if it repeatedly did not, stop
    // pretending — latch attachment as unsupported and say so, rather than
    // issuing moves into the void on every drag.
    //
    // Deferred, not immediate: X11 needs a server round trip before the new
    // geometry is readable, so an immediate compare would report failure for
    // moves that are about to succeed.
    //
    // Sequenced through AttachMoveTracker, NOT with a captured target: a drag
    // delivers Move events every ~30 ms, so several checks are in flight at
    // once and all but the newest are comparing against a target the user has
    // already moved on from. Scoring those as misses turned an ordinary drag
    // into a permanent disable on a perfectly working platform (found in
    // review; see window_attach.h). Only the newest request is ever checked,
    // and it compares against the tracker's live target.
    const unsigned long long token =
        attach_tracker_.issue_move(d.placement.x, d.placement.y);

    QTimer::singleShot(250, this, [this, token]() {
        if (!attach_tracker_.check_is_current(token))
            return;   // superseded by a newer move — this check is meaningless
        if (!attach_supported_ || !attach_enabled_ || !isVisible())
            return;

        const QPoint got = frameGeometry().topLeft();
        const bool landed = jnext::move_landed(
            attach_tracker_.want_x, attach_tracker_.want_y, got.x(), got.y());
        if (!attach_tracker_.record_landing(landed))
            return;

        give_up_on_attachment();
    });
}

void DebuggerWindow::give_up_on_attachment() {
    // `attach_supported_ = false` alone stops every further move:
    // compute_attached_placement()'s FIRST check is positioning_supported, so
    // no placement is issued regardless of the user's toggle.
    //
    // `attach_enabled_` is deliberately NOT touched. It is the user's stored
    // preference — save_geometry() writes it to "debugger/attached" on every
    // close — and this is the PLATFORM giving up, not the user turning
    // attachment off. Clearing it here silently downgraded that preference to
    // false for the next session, so a machine where attachment works (or a
    // transiently uncooperative WM) would come back detached and never retry.
    // Review round 2 caught this contradicting the very comment that used to
    // sit above it. The unchecked checkbox below is presentation only.
    attach_supported_ = false;

    if (attach_action_) {
        const QSignalBlocker blocker(attach_action_);
        attach_action_->setChecked(false);
        // Deliberately left ENABLED. A measured give-up is recoverable — the
        // window system may have been transiently uncooperative, or the user
        // may have moved the window to a different screen — so re-ticking this
        // retries from a clean slate. Only a platform that CANNOT position
        // windows at all (Wayland, detected by name) leaves the item disabled,
        // because there retrying is guaranteed to fail.
        attach_action_->setEnabled(!attach_platform_blocked_);
        attach_action_->setToolTip(
            tr("Attachment stopped: the window manager ignored the last %1 move "
               "requests. Re-enable to try again.")
                .arg(jnext::kAttachFailuresBeforeGivingUp));
    }
    if (statusBar()) {
        statusBar()->showMessage(
            tr("Debugger window attachment stopped: the window manager ignored "
               "the move requests. Re-enable it from the Window menu to retry."),
            8000);
    }
}

void DebuggerWindow::set_attach_enabled(bool on) {
    attach_enabled_ = on;

    if (on && !attach_platform_blocked_) {
        // An explicit re-enable is the recovery path from a measured give-up:
        // clear the failure history and let the platform prove itself again.
        // Without this the latch was permanent for the life of the process —
        // toggling the debugger off and on does not help, because
        // DebuggerManager::ensure_window() reuses the existing window.
        attach_supported_ = true;
        attach_tracker_.reset_failures();
        if (attach_action_) attach_action_->setToolTip(QString());
    }

    if (attach_action_) attach_action_->setChecked(on);

    QSettings settings(debugger_config_path(), QSettings::IniFormat);
    settings.setValue("debugger/attached", on);

    if (on && debugger_mgr_) {
        // Snap back into place immediately rather than waiting for the user to
        // drag the emulator window.
        debugger_mgr_->reposition_debugger_window();
    } else if (!on) {
        // Remember where the user leaves it from here on.
        save_geometry();
    }

    if (statusBar()) {
        const char* reason = on
            ? nullptr
            : jnext::attach_status_reason(jnext::AttachStatus::Disabled);
        statusBar()->showMessage(
            on ? tr("Debugger window attached to the emulator window")
               : tr("Debugger window detached (%1)").arg(QString::fromUtf8(reason)),
            4000);
    }
}

void DebuggerWindow::create_panels() {
    // --- Create panels ---
    cpu_panel_ = new CpuPanel(emulator_);
    disasm_panel_ = new DisasmPanel(emulator_);
    memory_panel_ = new MemoryPanel(emulator_);
    memory_panel_->setMinimumHeight(320);
    video_panel_ = new VideoPanel(emulator_);
    sprite_panel_ = new SpritePanel(emulator_);
    copper_panel_ = new CopperPanel(emulator_);
    nextreg_panel_ = new NextRegPanel(emulator_);
    audio_panel_ = new AudioPanel(emulator_);
    watch_panel_ = new WatchPanel(emulator_);

    // --- Helper: wrap a widget in a titled QGroupBox ---
    auto make_group = [](const QString& title, QWidget* content) -> QGroupBox* {
        auto* box = new QGroupBox(title);
        box->setStyleSheet(
            "QGroupBox { font-weight: bold; border: 1px solid #C0C0C0;"
            "  margin-top: 18px; padding-top: 8px; }"
            "QGroupBox::title { subcontrol-origin: margin;"
            "  subcontrol-position: top left; padding: 3px 8px;"
            "  background: #D8D8D8; }");
        auto* lay = new QVBoxLayout(box);
        lay->setContentsMargins(4, 6, 4, 4);
        lay->addWidget(content);
        return box;
    };

    // --- Tab widget for left-side panels (tabs at bottom) ---
    tab_widget_ = new QTabWidget();
    tab_widget_->setTabPosition(QTabWidget::North);
    tab_widget_->addTab(video_panel_, tr("Video"));
    tab_widget_->addTab(sprite_panel_, tr("Sprites"));
    tab_widget_->addTab(copper_panel_, tr("Copper"));
    tab_widget_->addTab(nextreg_panel_, tr("NextREG"));
    tab_widget_->addTab(audio_panel_, tr("Audio"));

    tab_widget_->setMinimumWidth(380);

    stack_panel_ = new StackPanel(emulator_);
    callstack_panel_ = new CallStackPanel(emulator_);
    breakpoint_panel_ = new BreakpointPanel(emulator_);

    // GH #220 — no panel-to-panel wiring here any more. Both panels subscribe
    // to the BreakpointSet in their own constructors, so the list and the
    // gutter track it without either of them (or any mutation route) knowing
    // the other exists. The two hand-wired directions this replaced had each
    // shipped broken once.

    mmu_panel_ = new MmuPanel(emulator_);

    auto* cpu_box = make_group(tr("CPU Registers"), cpu_panel_);
    auto* mmu_box = make_group(tr("MMU"), mmu_panel_);
    auto* disasm_box = make_group(tr("Disassembly"), disasm_panel_);

    // Right column: CPU Registers (top half) + MMU (bottom half)
    auto* right_col = new QSplitter(Qt::Vertical);
    right_col->addWidget(cpu_box);
    right_col->addWidget(mmu_box);
    right_col->setStretchFactor(0, 1);
    right_col->setStretchFactor(1, 1);
    right_col->setHandleWidth(1);
    right_col->setChildrenCollapsible(false);

    auto* right_widget = new QWidget();
    auto* right_layout = new QHBoxLayout(right_widget);
    right_layout->setContentsMargins(0, 0, 0, 0);
    right_layout->setSpacing(2);
    right_layout->addWidget(disasm_box, 1);  // disasm gets stretch
    right_layout->addWidget(right_col, 0);   // CPU+MMU on the right, narrow

    // --- Top: tabs (left) | CPU+disasm (right) ---
    top_splitter_ = new QSplitter(Qt::Horizontal);
    top_splitter_->addWidget(tab_widget_);
    top_splitter_->addWidget(right_widget);
    top_splitter_->setStretchFactor(0, 3);  // left tabs get more space
    top_splitter_->setStretchFactor(1, 2);  // right side (disasm+CPU)
    top_splitter_->setSizes({450, 300});
    top_splitter_->setHandleWidth(1);
    top_splitter_->setChildrenCollapsible(false);

    // --- Bottom: memory tabs (left) | watches+breakpoints tabs (right) ---
    auto* memory_tab_widget = new QTabWidget();
    memory_tab_widget->setTabPosition(QTabWidget::North);
    memory_tab_widget->addTab(stack_panel_, tr("Stack"));
    memory_tab_widget->addTab(callstack_panel_, tr("Call Stack"));
    memory_tab_widget->addTab(memory_panel_, tr("Memory"));

    auto* bottom_tab_widget = new QTabWidget();
    bottom_tab_widget->setTabPosition(QTabWidget::North);
    bottom_tab_widget->addTab(watch_panel_, tr("Watches"));
    bottom_tab_widget->addTab(breakpoint_panel_, tr("Breakpoints"));

    auto* bottom_splitter = new QSplitter(Qt::Horizontal);
    bottom_splitter->addWidget(memory_tab_widget);
    bottom_splitter->addWidget(bottom_tab_widget);
    bottom_splitter->setStretchFactor(0, 1);
    bottom_splitter->setStretchFactor(1, 1);
    bottom_splitter->setSizes({600, 400});
    bottom_splitter->setHandleWidth(1);
    bottom_splitter->setChildrenCollapsible(false);

    // --- Main: top area | bottom (memory + watches) ---
    main_splitter_ = new QSplitter(Qt::Vertical);
    main_splitter_->addWidget(top_splitter_);
    main_splitter_->addWidget(bottom_splitter);
    main_splitter_->setStretchFactor(0, 1);  // top gets stretch
    main_splitter_->setStretchFactor(1, 0);  // bottom stays at preferred size
    main_splitter_->setHandleWidth(1);
    main_splitter_->setChildrenCollapsible(false);

    // GH #114: the panels live inside a scroll area so the window can be made
    // SMALLER than their combined minimum size and panned instead. The panel
    // minimums are real (a hex dump or a disassembly listing below a certain
    // size is useless), so they stay — what changes is that falling below them
    // now produces scrollbars rather than a window that refuses to shrink and
    // hangs off a small screen.
    //
    // Only the central widget is scrolled. The debug-controls toolbar is a
    // QMainWindow toolbar (addToolBar, see set_debugger_manager) and therefore
    // OUTSIDE this scroll area, which is what keeps the button bar on screen at
    // every window size.
    auto* scroll = new QScrollArea();
    scroll->setWidget(main_splitter_);
    scroll->setWidgetResizable(true);   // splitter still fills a roomy window
    scroll->setFrameShape(QFrame::NoFrame);
    setCentralWidget(scroll);
}

void DebuggerWindow::activate_follow_pc() {
    if (disasm_panel_) disasm_panel_->activate_follow_pc();
}

bool DebuggerWindow::prompt_bp_address(const QString& title, uint16_t& addr) {
    QDialog dlg(this);
    dlg.setWindowTitle(title);
    dlg.setMinimumWidth(400);

    auto* form = new QFormLayout(&dlg);
    auto* addr_edit = new QLineEdit(&dlg);
    addr_edit->setPlaceholderText("e.g. 4000 or $4000");
    form->addRow(tr("Address (hex):"), addr_edit);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(buttons);

    if (dlg.exec() != QDialog::Accepted) return false;

    QString addr_text = addr_edit->text().trimmed();
    if (addr_text.startsWith('$')) addr_text = addr_text.mid(1);
    if (addr_text.startsWith("0x", Qt::CaseInsensitive)) addr_text = addr_text.mid(2);

    bool ok = false;
    addr = static_cast<uint16_t>(addr_text.toUInt(&ok, 16));
    return ok;
}

void DebuggerWindow::show_add_data_bp_dialog(WatchType type) {
    QString type_name;
    switch (type) {
        case WatchType::READ:       type_name = "Read"; break;
        case WatchType::WRITE:      type_name = "Write"; break;
        case WatchType::READ_WRITE: type_name = "Read/Write"; break;
        default:                    type_name = "Data"; break;
    }

    uint16_t addr = 0;
    if (!prompt_bp_address(tr("Add %1 Breakpoint").arg(type_name), addr)) return;

    // GH #220 — no repaint here. add_watchpoint() notifies, the Breakpoints
    // panel redraws, and the disassembly gutter correctly does not: it draws
    // bps.has_pc(addr) only, and this route touches no PC breakpoint.
    emulator_->debug_state().breakpoints().add_watchpoint(addr, type);
}

// GH #215 — an Execute breakpoint is a PC breakpoint (add_pc), not a
// watchpoint, which is why it needs its own entry point rather than a fourth
// WatchType. That difference is now expressed once, by add_pc() notifying
// PcBreakpoints, rather than by this site remembering to repaint the gutter.
void DebuggerWindow::show_add_exec_bp_dialog() {
    uint16_t addr = 0;
    if (!prompt_bp_address(tr("Add Execute Breakpoint"), addr)) return;

    emulator_->debug_state().breakpoints().add_pc(addr);
}

void DebuggerWindow::refresh_panels() {
    if (cpu_panel_) cpu_panel_->refresh();
    if (disasm_panel_) disasm_panel_->refresh();
    if (memory_panel_) memory_panel_->refresh();
    if (video_panel_) video_panel_->refresh();
    if (sprite_panel_) sprite_panel_->refresh();
    if (copper_panel_) copper_panel_->refresh();
    if (nextreg_panel_) nextreg_panel_->refresh();
    if (audio_panel_) audio_panel_->refresh();
    if (watch_panel_) watch_panel_->refresh();
    if (mmu_panel_) mmu_panel_->refresh();
    if (stack_panel_) stack_panel_->refresh();
    if (callstack_panel_) callstack_panel_->refresh();
    if (breakpoint_panel_) breakpoint_panel_->refresh();
    update_rewind_ui();
}
