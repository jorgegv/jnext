#include "debugger/cpu_panel.h"
#include "qt/palette_tint_qt.h"   // GH #363
#include <QApplication>
#include "debug/debugger.h"
#include "debugger/reg_edit.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>
#include <QPainter>
#include <QString>
#include <QMenu>
#include <QContextMenuEvent>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMouseEvent>

#include <algorithm>

namespace {
// The value of a 16-bit register (the navigable ones; GH #366).
uint16_t reg_value(const Z80Registers& r, jnext::dbg::RegId id) {
    using R = jnext::dbg::RegId;
    switch (id) {
    case R::AF: return r.AF;   case R::BC: return r.BC;   case R::DE: return r.DE;   case R::HL: return r.HL;
    case R::AF2: return r.AF2; case R::BC2: return r.BC2; case R::DE2: return r.DE2; case R::HL2: return r.HL2;
    case R::IX: return r.IX;   case R::IY: return r.IY;   case R::SP: return r.SP;   case R::PC: return r.PC;
    default: return 0;
    }
}
// F bits of S Z H PV N C, the order of the flag labels (cpu_panel.cpp refresh()).
constexpr int kFlagBit[6] = {7, 6, 4, 2, 1, 0};
}  // namespace

CpuPanel::CpuPanel(jnext::dbg::Debugger* dbg, QWidget* parent)
    : QWidget(parent)
    , dbg_(dbg)
{
    create_ui();
}

void CpuPanel::create_ui() {
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    QFont mono_bold = mono;
    mono_bold.setBold(true);
    QFont mono_small("Monospace", 9);
    mono_small.setStyleHint(QFont::Monospace);

    auto* top_layout = new QVBoxLayout(this);
    top_layout->setSpacing(2);
    top_layout->setContentsMargins(4, 4, 4, 4);

    // Helper: create a QGridLayout group.
    auto make_group = [&]() -> QGridLayout* {
        auto* grid = new QGridLayout();
        grid->setSpacing(1);
        grid->setContentsMargins(2, 0, 2, 0);
        return grid;
    };

    // Helper: vertical separator.
    auto make_vsep = [&]() -> QFrame* {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::VLine);
        line->setFrameShadow(QFrame::Plain);
        palette_tint::follow(line, [](const QPalette& p) {
            return "color: " + palette_tint::css(palette_tint::surface(p, QColor(0xC0, 0xC0, 0xC0))) + ";";
        });
        return line;
    };

    // Helper: horizontal separator.
    auto make_hsep = [&]() -> QFrame* {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        palette_tint::follow(line, [](const QPalette& p) {
            return "color: " + palette_tint::css(palette_tint::surface(p, QColor(0xD0, 0xD0, 0xD0))) + ";";
        });
        return line;
    };

    int grow;

    auto add_reg = [&](QGridLayout* grid, int& r, const char* name, QLabel*& value_label) {
        auto* name_lbl = new QLabel(QString("%1: ").arg(name), this);
        name_lbl->setFont(mono_bold);
        name_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(name_lbl, r, 0);

        value_label = new QLabel("----", this);
        value_label->setFont(mono);
        value_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(value_label, r, 1);
        ++r;
    };

    // === Row 1: AF/BC/DE/HL | AF'/BC'/DE'/HL' ===
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(0);

    auto* g1 = make_group();
    grow = 0;
    add_reg(g1, grow, "AF", reg_af_);
    add_reg(g1, grow, "BC", reg_bc_);
    add_reg(g1, grow, "DE", reg_de_);
    add_reg(g1, grow, "HL", reg_hl_);
    row1->addLayout(g1, 1);
    row1->addWidget(make_vsep());

    auto* g2 = make_group();
    grow = 0;
    add_reg(g2, grow, "AF'", reg_af2_);
    add_reg(g2, grow, "BC'", reg_bc2_);
    add_reg(g2, grow, "DE'", reg_de2_);
    add_reg(g2, grow, "HL'", reg_hl2_);
    row1->addLayout(g2, 1);

    top_layout->addLayout(row1);
    top_layout->addWidget(make_hsep());

    // === Row 2: IX/IY/SP/PC | I/R/IFF/IM ===
    auto* row2 = new QHBoxLayout();
    row2->setSpacing(0);

    auto* g3 = make_group();
    grow = 0;
    add_reg(g3, grow, "IX", reg_ix_);
    add_reg(g3, grow, "IY", reg_iy_);
    add_reg(g3, grow, "SP", reg_sp_);
    add_reg(g3, grow, "PC", reg_pc_);
    row2->addLayout(g3, 1);
    row2->addWidget(make_vsep());

    auto* g4 = make_group();
    grow = 0;
    add_reg(g4, grow, "I", reg_i_);
    add_reg(g4, grow, "R", reg_r_);
    add_reg(g4, grow, "IFF", reg_iff_);
    add_reg(g4, grow, "IM", reg_im_);
    row2->addLayout(g4, 1);

    top_layout->addLayout(row2);
    top_layout->addWidget(make_hsep());

    // === Row 3: Flags ===
    auto* flags_row = new QHBoxLayout();
    flags_row->setSpacing(1);

    auto* flags_lbl = new QLabel("Flags: ", this);
    flags_lbl->setFont(mono_bold);
    flags_row->addWidget(flags_lbl);

    auto make_flag = [&](const char* name) -> QLabel* {
        auto* lbl = new QLabel(name, this);
        lbl->setFont(mono);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setMinimumWidth(16);
        flags_row->addWidget(lbl);
        return lbl;
    };

    flag_s_  = make_flag("S");
    flag_z_  = make_flag("Z");
    flag_h_  = make_flag("H");
    flag_pv_ = make_flag("PV");
    flag_n_  = make_flag("N");
    flag_c_  = make_flag("C");
    flags_row->addStretch();

    top_layout->addLayout(flags_row);

    // === Row 4: State + Screen ===
    auto* state_row = new QHBoxLayout();
    state_row->setSpacing(4);

    auto* state_lbl = new QLabel("State: ", this);
    state_lbl->setFont(mono_bold);
    state_row->addWidget(state_lbl);

    reg_halted_ = new QLabel("Running", this);
    reg_halted_->setFont(mono);
    state_row->addWidget(reg_halted_);

    state_row->addSpacing(8);

    auto* screen_lbl = new QLabel("Screen: ", this);
    screen_lbl->setFont(mono_bold);
    state_row->addWidget(screen_lbl);

    ula_screen_ = new QLabel("5", this);
    ula_screen_->setFont(mono);
    state_row->addWidget(ula_screen_);

    state_row->addStretch();

    top_layout->addLayout(state_row);

    // === Rows 5-6: T-states since the last stop, and its Accumulate box (GH #369) ===
    auto* tstates_row = new QHBoxLayout();
    tstates_row->setSpacing(4);

    auto* tstates_lbl = new QLabel("T-states: ", this);
    tstates_lbl->setFont(mono_bold);
    tstates_row->addWidget(tstates_lbl);

    tstates_value_ = new QLabel("0", this);
    tstates_value_->setFont(mono);
    tstates_row->addWidget(tstates_value_);

    tstates_row->addStretch();
    top_layout->addLayout(tstates_row);

    // The check box has its own line: beside a 12-digit value it needs 271 px
    // and the panel is capped at 260 (QPN-CPU-05).
    auto* accumulate_row = new QHBoxLayout();
    tstates_accumulate_ = new QCheckBox(tr("Accumulate"), this);
    tstates_accumulate_->setToolTip(
        tr("Add up the T-states across stops instead of counting from the last one"));
    accumulate_row->addWidget(tstates_accumulate_);
    accumulate_row->addStretch();
    // Straight to the counter: no refresh() and no manager, so no redraw count moves.
    connect(tstates_accumulate_, &QCheckBox::toggled, this, [this](bool on) {
        tstates_.set_accumulate(on);
        tstates_value_->setText(QString::number(tstates_.value()));
    });

    top_layout->addLayout(accumulate_row);
    top_layout->addStretch();

    setMinimumWidth(200);
    setMaximumWidth(260);

    // GH #368: editing. The values stay the QLabels above, in their layouts; one
    // QLineEdit, in no layout, is laid over the label being edited.
    values_ = {reg_af_, reg_bc_, reg_de_, reg_hl_, reg_af2_, reg_bc2_, reg_de2_, reg_hl2_,
               reg_ix_, reg_iy_, reg_sp_, reg_pc_, reg_i_, reg_r_, reg_iff_, reg_im_};
    flag_labels_ = {flag_s_, flag_z_, flag_h_, flag_pv_, flag_n_, flag_c_};
    for (QLabel* l : values_) {
        l->installEventFilter(this);
        l->setToolTip(tr("Double-click to edit (while paused)"));
    }
    for (QLabel* l : flag_labels_) {
        l->installEventFilter(this);
        l->setToolTip(tr("Click to toggle (while paused)"));
    }
    editor_ = new QLineEdit(this);
    editor_->setFont(mono);
    editor_->setFrame(false);
    editor_->hide();
    editor_->installEventFilter(this);
    connect(editor_, &QLineEdit::returnPressed, this, &CpuPanel::commit_edit);
}

void CpuPanel::set_paused(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;
    // The machine is about to run: a value typed against the last stop would be
    // written into a different state, so an open editor is cancelled, not committed.
    if (!paused_) end_edit();
    update();
}

bool CpuPanel::eventFilter(QObject* watched, QEvent* event) {
    if (watched == editor_) {
        if (event->type() == QEvent::KeyPress &&
            static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
            end_edit();
            return true;
        }
        if (event->type() == QEvent::FocusOut &&
            static_cast<QFocusEvent*>(event)->reason() != Qt::PopupFocusReason) {
            end_edit();   // clicking away cancels
        }
        return false;
    }
    for (size_t i = 0; i < values_.size(); ++i) {
        if (watched != values_[i]) continue;
        if (event->type() == QEvent::MouseButtonDblClick &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            begin_edit(static_cast<int>(i));
            return true;
        }
        // GH #366: a single click on an address-like value shows it in the Disassembly. The first
        // click of a double-click (the edit gesture) does it too; Back undoes it.
        if (event->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            if (dbg_ && paused_ && jnext::regedit::fields()[i].navigable)
                emit show_in_disassembly_requested(reg_value(dbg_->registers(), jnext::regedit::fields()[i].reg));
            return false;
        }
        if (event->type() == QEvent::ContextMenu) {
            show_value_menu(static_cast<int>(i), static_cast<QContextMenuEvent*>(event)->globalPos());
            return true;
        }
        return false;
    }
    for (size_t i = 0; i < flag_labels_.size(); ++i) {
        if (watched != flag_labels_[i]) continue;
        if (event->type() == QEvent::MouseButtonPress &&
            static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
            click_flag(kFlagBit[i]);
            return true;
        }
        return false;
    }
    return QWidget::eventFilter(watched, event);
}

void CpuPanel::begin_edit(int field) {
    if (!dbg_ || !paused_ || field < 0 || field >= static_cast<int>(values_.size())) return;
    QLabel* lbl = values_[static_cast<size_t>(field)];
    edit_field_ = field;
    // Over the label, wide enough for a symbol name: out to the panel's right edge.
    const QRect g = lbl->geometry();
    editor_->setGeometry(g.x(), g.y(), std::max(g.width(), width() - g.x() - 2), g.height());
    editor_->setText(lbl->text());
    editor_->show();
    editor_->raise();
    editor_->setFocus(Qt::OtherFocusReason);
    editor_->selectAll();
}

void CpuPanel::end_edit() {
    if (edit_field_ < 0) return;
    edit_field_ = -1;   // first: hiding the editor sends it a FocusOut
    editor_->hide();
}

void CpuPanel::commit_edit() {
    if (edit_field_ < 0) return;
    const jnext::regedit::Field& f = jnext::regedit::fields()[static_cast<size_t>(edit_field_)];
    const QString text = editor_->text();
    end_edit();   // closes in every case; the next paused refresh redraws the label
    if (!dbg_ || !paused_) return;

    const jnext::regedit::Parsed p =
        jnext::regedit::parse(f.kind, text.toStdString(), dbg_->symbols());
    if (!p.ok) {
        emit message(QString("%1: '%2' %3").arg(QString::fromLatin1(f.label), text,
                                                QString::fromStdString(p.why)));
        return;
    }

    using jnext::dbg::RegId;
    const Z80Registers r = dbg_->registers();
    // Each write only when the value changed: no MUTATE line, no revision bump.
    auto write = [&](RegId reg, uint16_t now, uint16_t want) {
        if (now == want) return true;
        const jnext::dbg::Result res = dbg_->set_register(client_, reg, want);
        if (jnext::dbg::ok(res)) return true;
        emit message(QString::fromStdString(jnext::regedit::refusal_text(f.label, res)));
        return false;
    };
    switch (f.reg) {
    case RegId::AF:  write(f.reg, r.AF,  p.v0); break;
    case RegId::BC:  write(f.reg, r.BC,  p.v0); break;
    case RegId::DE:  write(f.reg, r.DE,  p.v0); break;
    case RegId::HL:  write(f.reg, r.HL,  p.v0); break;
    case RegId::AF2: write(f.reg, r.AF2, p.v0); break;
    case RegId::BC2: write(f.reg, r.BC2, p.v0); break;
    case RegId::DE2: write(f.reg, r.DE2, p.v0); break;
    case RegId::HL2: write(f.reg, r.HL2, p.v0); break;
    case RegId::IX:  write(f.reg, r.IX,  p.v0); break;
    case RegId::IY:  write(f.reg, r.IY,  p.v0); break;
    case RegId::SP:  write(f.reg, r.SP,  p.v0); break;
    case RegId::PC:  write(f.reg, r.PC,  p.v0); break;
    case RegId::I:   write(f.reg, r.I,   p.v0); break;
    case RegId::R:   write(f.reg, r.R,   p.v0); break;
    case RegId::IM:  write(f.reg, r.IM,  p.v0); break;
    case RegId::IFF1:
        if (write(RegId::IFF1, r.IFF1, p.v0)) write(RegId::IFF2, r.IFF2, p.v1);
        break;
    default: break;
    }
}

void CpuPanel::click_flag(int bit) {
    if (!dbg_ || !paused_) return;
    const uint8_t f = static_cast<uint8_t>(dbg_->registers().AF & 0xFF);
    const jnext::dbg::Result res =
        dbg_->set_register(client_, jnext::dbg::RegId::F, jnext::regedit::toggle_flag(f, bit));
    if (!jnext::dbg::ok(res))
        emit message(QString::fromStdString(jnext::regedit::refusal_text("F", res)));
}

void CpuPanel::show_value_menu(int field, const QPoint& global_pos) {
    const jnext::regedit::Field& f = jnext::regedit::fields()[static_cast<size_t>(field)];
    const bool live = dbg_ && paused_;
    QMenu menu(this);
    menu.setToolTipsVisible(true);
    QAction* dis = nullptr;
    QAction* mem = nullptr;
    QAction* src = nullptr;
    uint16_t v = 0;
    if (f.navigable) {
        if (live) v = reg_value(dbg_->registers(), f.reg);
        dis = menu.addAction(tr("Show in Disassembly"));
        mem = menu.addAction(tr("Show in Memory"));
        if (dbg_ && !dbg_->source_map().empty()) {
            src = menu.addAction(tr("Show in Source"));
            src->setEnabled(live && dbg_->source_map().lookup(dbg_->source_page(v), v));
        }
        dis->setEnabled(live);
        mem->setEnabled(live);
        menu.addSeparator();
    }
    QAction* edit = menu.addAction(tr("Edit Value\u2026"));
    edit->setEnabled(live);
    QAction* chosen = nullptr;
    for (QAction* a : {dis, mem, src, edit})
        if (a) connect(a, &QAction::triggered, &menu, [&chosen, a]() { chosen = a; });
    menu.exec(global_pos);
    // after the popup closed: it gives the focus back on close, which would cancel a new editor
    if (!chosen) return;
    if (chosen == edit) begin_edit(field);
    else if (chosen == dis) emit show_in_disassembly_requested(v);
    else if (chosen == mem) emit show_in_memory_requested(v);
    else if (chosen == src) emit show_in_source_requested(v);
}

void CpuPanel::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    if (!paused_) {
        QPainter painter(this);
        painter.fillRect(rect(), palette_tint::surface(palette(), QColor(192, 192, 192, 80)));
    }
}

void CpuPanel::restart_tstate_count() {
    if (!dbg_) return;
    tstates_.restart(dbg_->time().tstates_total, dbg_->time_epoch());
    tstates_value_->setText(QString::number(tstates_.value()));
}

void CpuPanel::refresh() {
    if (!dbg_) return;
    if (!paused_) return;

    // GH #369: a paused observation is a stop.
    tstates_.observe(dbg_->time().tstates_total, dbg_->time_epoch());   // INS-07
    tstates_value_->setText(QString::number(tstates_.value()));

    const Z80Registers regs = dbg_->registers();   // INS-01

    // 16-bit registers
    reg_af_->setText(QString::asprintf("%04X", regs.AF));
    reg_bc_->setText(QString::asprintf("%04X", regs.BC));
    reg_de_->setText(QString::asprintf("%04X", regs.DE));
    reg_hl_->setText(QString::asprintf("%04X", regs.HL));
    reg_af2_->setText(QString::asprintf("%04X", regs.AF2));
    reg_bc2_->setText(QString::asprintf("%04X", regs.BC2));
    reg_de2_->setText(QString::asprintf("%04X", regs.DE2));
    reg_hl2_->setText(QString::asprintf("%04X", regs.HL2));
    reg_ix_->setText(QString::asprintf("%04X", regs.IX));
    reg_iy_->setText(QString::asprintf("%04X", regs.IY));
    reg_sp_->setText(QString::asprintf("%04X", regs.SP));
    reg_pc_->setText(QString::asprintf("%04X", regs.PC));

    // 8-bit registers
    reg_i_->setText(QString::asprintf("%02X", regs.I));
    reg_r_->setText(QString::asprintf("%02X", regs.R));
    reg_iff_->setText(QString::asprintf("%d/%d", regs.IFF1, regs.IFF2));
    reg_im_->setText(QString::asprintf("%d", regs.IM));

    // Flags
    uint8_t f = regs.AF & 0xFF;
    // The application palette, not the panel's: inside a style-sheeted box the
    // panel's own palette lags a light/dark switch (qt/palette_tint_qt.h).
    const QPalette pal = QApplication::palette();
    auto set_flag = [&pal](QLabel* lbl, bool active) {
        if (active) {
            lbl->setStyleSheet("QLabel { font-weight: bold; color: " +
                               palette_tint::css(palette_tint::ink(pal, QColor(0x00, 0xAA, 0x00))) + "; }");
        } else {
            lbl->setStyleSheet("QLabel { color: " +
                               palette_tint::css(palette_tint::ink(pal, QColor(0x88, 0x88, 0x88))) + "; }");
        }
    };

    set_flag(flag_s_,  f & 0x80);
    set_flag(flag_z_,  f & 0x40);
    set_flag(flag_h_,  f & 0x10);
    set_flag(flag_pv_, f & 0x04);
    set_flag(flag_n_,  f & 0x02);
    set_flag(flag_c_,  f & 0x01);

    // Halted state
    if (regs.halted) {
        reg_halted_->setText("HALTED");
        reg_halted_->setStyleSheet("QLabel { color: " +
                                   palette_tint::css(palette_tint::ink(pal, QColor(0xCC, 0x00, 0x00))) +
                                   "; font-weight: bold; }");
    } else {
        reg_halted_->setText("Running");
        reg_halted_->setStyleSheet("");
    }

    // ULA active screen. The bank is port 0x7FFD bit 3 as last written
    // (INS-03 `paging_ports()` — the byte this label has always read; the ULA
    // holds a copy of the bit, `ula_screen_regs().active_bank`). The mode bits
    // live in port_ff(2:0) per VHDL zxula.vhd:191 (`screen_mode_s <=
    // i_port_ff_reg(2 downto 0)`), INS-15.
    const bool shadow = (dbg_->paging_ports().port_7ffd >> 3) & 1;
    const uint8_t mode_bits = static_cast<uint8_t>(dbg_->ula_screen_regs().port_ff & 0x07);

    // The mode each value selects mirrors the case-statement in
    // Ula::set_screen_mode: 1 the alternate screen, 2/3 hi-colour, 6/7 hi-res,
    // anything else (0, 4, 5) standard.
    QString screen_text = shadow ? "Bank 7" : "Bank 5";
    switch (mode_bits) {
        case 1:           screen_text += " Alt";   break;
        case 2: case 3:   screen_text += " HiCol"; break;
        case 6: case 7:   screen_text += " HiRes"; break;
        default: break;
    }
    ula_screen_->setText(screen_text);
}
