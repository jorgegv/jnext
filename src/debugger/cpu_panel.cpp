#include "debugger/cpu_panel.h"
#include "core/emulator.h"
#include "cpu/z80_cpu.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFrame>
#include <QFont>
#include <QString>

CpuPanel::CpuPanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    create_ui();
}

void CpuPanel::create_ui() {
    // Monospace font for the entire panel.
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);

    QFont mono_bold = mono;
    mono_bold.setBold(true);

    auto* top_layout = new QVBoxLayout(this);
    top_layout->setSpacing(4);
    top_layout->setContentsMargins(8, 8, 8, 8);

    // --- Top row: 4 register groups side by side ---
    auto* regs_row = new QHBoxLayout();
    regs_row->setSpacing(24);

    // Helper: create a QGridLayout group and add register rows to it.
    // Returns the grid so caller can add more if needed.
    auto make_group = [&]() -> QGridLayout* {
        auto* grid = new QGridLayout();
        grid->setSpacing(2);
        grid->setContentsMargins(0, 0, 0, 0);
        return grid;
    };

    int grow; // grid row counter, reused per group

    // Helper to add a register row into a specific grid.
    auto add_reg = [&](QGridLayout* grid, int& r, const char* name, QLabel*& value_label) {
        auto* name_lbl = new QLabel(name, this);
        name_lbl->setFont(mono_bold);
        name_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        grid->addWidget(name_lbl, r, 0);

        value_label = new QLabel("----", this);
        value_label->setFont(mono);
        value_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        grid->addWidget(value_label, r, 1);
        ++r;
    };

    // Group 1: AF/BC/DE/HL
    auto* g1 = make_group();
    grow = 0;
    add_reg(g1, grow, "AF", reg_af_);
    add_reg(g1, grow, "BC", reg_bc_);
    add_reg(g1, grow, "DE", reg_de_);
    add_reg(g1, grow, "HL", reg_hl_);
    regs_row->addLayout(g1);

    // Group 2: AF'/BC'/DE'/HL'
    auto* g2 = make_group();
    grow = 0;
    add_reg(g2, grow, "AF'", reg_af2_);
    add_reg(g2, grow, "BC'", reg_bc2_);
    add_reg(g2, grow, "DE'", reg_de2_);
    add_reg(g2, grow, "HL'", reg_hl2_);
    regs_row->addLayout(g2);

    // Group 3: IX/IY/SP/PC
    auto* g3 = make_group();
    grow = 0;
    add_reg(g3, grow, "IX", reg_ix_);
    add_reg(g3, grow, "IY", reg_iy_);
    add_reg(g3, grow, "SP", reg_sp_);
    add_reg(g3, grow, "PC", reg_pc_);
    regs_row->addLayout(g3);

    // Group 4: I/R/IFF/IM
    auto* g4 = make_group();
    grow = 0;
    add_reg(g4, grow, "I", reg_i_);
    add_reg(g4, grow, "R", reg_r_);
    add_reg(g4, grow, "IFF", reg_iff_);
    add_reg(g4, grow, "IM", reg_im_);
    regs_row->addLayout(g4);

    regs_row->addStretch();
    top_layout->addLayout(regs_row);

    // --- Separator ---
    auto* sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setFrameShadow(QFrame::Sunken);
    top_layout->addWidget(sep);

    // --- Bottom row: Flags + State ---
    auto* bottom_row = new QHBoxLayout();
    bottom_row->setSpacing(6);

    auto* flags_lbl = new QLabel("Flags", this);
    flags_lbl->setFont(mono_bold);
    bottom_row->addWidget(flags_lbl);

    auto make_flag = [&](const char* name) -> QLabel* {
        auto* lbl = new QLabel(name, this);
        lbl->setFont(mono);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setMinimumWidth(20);
        bottom_row->addWidget(lbl);
        return lbl;
    };

    flag_s_  = make_flag("S");
    flag_z_  = make_flag("Z");
    flag_h_  = make_flag("H");
    flag_pv_ = make_flag("PV");
    flag_n_  = make_flag("N");
    flag_c_  = make_flag("C");

    bottom_row->addSpacing(20);

    auto* state_lbl = new QLabel("State", this);
    state_lbl->setFont(mono_bold);
    bottom_row->addWidget(state_lbl);

    reg_halted_ = new QLabel("Running", this);
    reg_halted_->setFont(mono);
    bottom_row->addWidget(reg_halted_);

    bottom_row->addStretch();
    top_layout->addLayout(bottom_row);

    // Push everything to the top.
    top_layout->addStretch();

    setMinimumWidth(380);
}

void CpuPanel::refresh() {
    if (!emulator_) return;

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

    // Flags — extract from F register (low byte of AF).
    uint8_t f = regs.AF & 0xFF;
    auto set_flag = [](QLabel* lbl, bool active) {
        if (active) {
            lbl->setStyleSheet("QLabel { font-weight: bold; color: #00AA00; }");
        } else {
            lbl->setStyleSheet("QLabel { color: #888888; }");
        }
    };

    set_flag(flag_s_,  f & 0x80);  // bit 7: Sign
    set_flag(flag_z_,  f & 0x40);  // bit 6: Zero
    set_flag(flag_h_,  f & 0x10);  // bit 4: Half-carry
    set_flag(flag_pv_, f & 0x04);  // bit 2: Parity/Overflow
    set_flag(flag_n_,  f & 0x02);  // bit 1: Subtract
    set_flag(flag_c_,  f & 0x01);  // bit 0: Carry

    // Halted state
    if (regs.halted) {
        reg_halted_->setText("HALTED");
        reg_halted_->setStyleSheet("QLabel { color: #CC0000; font-weight: bold; }");
    } else {
        reg_halted_->setText("Running");
        reg_halted_->setStyleSheet("");
    }
}
