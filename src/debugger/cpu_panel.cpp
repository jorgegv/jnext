#include "debugger/cpu_panel.h"
#include "core/emulator.h"
#include "cpu/z80_cpu.h"

#include <QGridLayout>
#include <QHBoxLayout>
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

    auto* layout = new QGridLayout(this);
    layout->setSpacing(4);
    layout->setContentsMargins(8, 8, 8, 8);

    int row = 0;

    // Helper to add a register row: name label (bold) + value label.
    auto add_reg = [&](const char* name, QLabel*& value_label) {
        auto* name_lbl = new QLabel(name, this);
        name_lbl->setFont(mono_bold);
        name_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        layout->addWidget(name_lbl, row, 0);

        value_label = new QLabel("----", this);
        value_label->setFont(mono);
        value_label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        layout->addWidget(value_label, row, 1);
        ++row;
    };

    // Main registers
    add_reg("AF", reg_af_);
    add_reg("BC", reg_bc_);
    add_reg("DE", reg_de_);
    add_reg("HL", reg_hl_);

    // Separator
    auto* sep1 = new QFrame(this);
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep1, row, 0, 1, 2);
    ++row;

    // Alternate registers
    add_reg("AF'", reg_af2_);
    add_reg("BC'", reg_bc2_);
    add_reg("DE'", reg_de2_);
    add_reg("HL'", reg_hl2_);

    // Separator
    auto* sep2 = new QFrame(this);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep2, row, 0, 1, 2);
    ++row;

    // Index and pointer registers
    add_reg("IX", reg_ix_);
    add_reg("IY", reg_iy_);
    add_reg("SP", reg_sp_);
    add_reg("PC", reg_pc_);

    // Separator
    auto* sep3 = new QFrame(this);
    sep3->setFrameShape(QFrame::HLine);
    sep3->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep3, row, 0, 1, 2);
    ++row;

    // Control registers
    add_reg("I", reg_i_);
    add_reg("R", reg_r_);
    add_reg("IFF", reg_iff_);
    add_reg("IM", reg_im_);

    // Separator
    auto* sep4 = new QFrame(this);
    sep4->setFrameShape(QFrame::HLine);
    sep4->setFrameShadow(QFrame::Sunken);
    layout->addWidget(sep4, row, 0, 1, 2);
    ++row;

    // Flags row
    auto* flags_lbl = new QLabel("Flags", this);
    flags_lbl->setFont(mono_bold);
    flags_lbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(flags_lbl, row, 0);

    auto* flags_container = new QWidget(this);
    auto* flags_layout = new QHBoxLayout(flags_container);
    flags_layout->setContentsMargins(0, 0, 0, 0);
    flags_layout->setSpacing(6);

    auto make_flag = [&](const char* name) -> QLabel* {
        auto* lbl = new QLabel(name, flags_container);
        lbl->setFont(mono);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setMinimumWidth(20);
        flags_layout->addWidget(lbl);
        return lbl;
    };

    flag_s_  = make_flag("S");
    flag_z_  = make_flag("Z");
    flag_h_  = make_flag("H");
    flag_pv_ = make_flag("PV");
    flag_n_  = make_flag("N");
    flag_c_  = make_flag("C");
    flags_layout->addStretch();

    layout->addWidget(flags_container, row, 1);
    ++row;

    // Halted indicator
    auto* halt_name = new QLabel("State", this);
    halt_name->setFont(mono_bold);
    halt_name->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(halt_name, row, 0);

    reg_halted_ = new QLabel("Running", this);
    reg_halted_->setFont(mono);
    reg_halted_->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->addWidget(reg_halted_, row, 1);
    ++row;

    // Push everything to the top.
    layout->setRowStretch(row, 1);

    setMinimumWidth(200);
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
