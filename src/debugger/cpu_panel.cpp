#include "debugger/cpu_panel.h"
#include "core/emulator.h"
#include "cpu/z80_cpu.h"
#include "video/renderer.h"
#include "video/ula.h"
#include "memory/mmu.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>
#include <QPainter>
#include <QString>

CpuPanel::CpuPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
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
        line->setStyleSheet("color: #C0C0C0;");
        return line;
    };

    // Helper: horizontal separator.
    auto make_hsep = [&]() -> QFrame* {
        auto* line = new QFrame(this);
        line->setFrameShape(QFrame::HLine);
        line->setStyleSheet("color: #D0D0D0;");
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
    top_layout->addStretch();

    setMinimumWidth(200);
    setMaximumWidth(260);
}

void CpuPanel::set_paused(bool paused) {
    if (paused_ == paused) return;
    paused_ = paused;
    update();
}

void CpuPanel::paintEvent(QPaintEvent* event) {
    QWidget::paintEvent(event);
    if (!paused_) {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(192, 192, 192, 80));
    }
}

void CpuPanel::refresh() {
    if (!emulator_) return;
    if (!paused_) return;

    auto regs = emulator_->cpu().get_registers();

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
    auto set_flag = [](QLabel* lbl, bool active) {
        if (active) {
            lbl->setStyleSheet("QLabel { font-weight: bold; color: #00AA00; }");
        } else {
            lbl->setStyleSheet("QLabel { color: #888888; }");
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
        reg_halted_->setStyleSheet("QLabel { color: #CC0000; font-weight: bold; }");
    } else {
        reg_halted_->setText("Running");
        reg_halted_->setStyleSheet("");
    }

    // ULA active screen
    bool shadow = (emulator_->mmu().port_7ffd() >> 3) & 1;
    uint8_t screen_mode_reg = emulator_->renderer().ula().get_screen_mode_reg();
    TimexScreenMode mode = static_cast<TimexScreenMode>((screen_mode_reg >> 3) & 0x07);

    QString screen_text = shadow ? "Bank 7" : "Bank 5";
    switch (mode) {
        case TimexScreenMode::STANDARD_1: screen_text += " Alt"; break;
        case TimexScreenMode::HI_COLOUR:  screen_text += " HiCol"; break;
        case TimexScreenMode::HI_RES:     screen_text += " HiRes"; break;
        default: break;
    }
    ula_screen_->setText(screen_text);
}
