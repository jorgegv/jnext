#include "debugger/sprite_panel.h"
#include "core/emulator.h"
#include "video/sprites.h"

#include <QVBoxLayout>
#include <QHeaderView>
#include <QFont>
#include <QString>

SpritePanel::SpritePanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent)
    , emulator_(emulator)
{
    create_ui();
}

void SpritePanel::create_ui() {
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);

    table_ = new QTableWidget(SpriteEngine::NUM_SPRITES, 10, this);
    table_->setHorizontalHeaderLabels({"#", "X", "Y", "Pat", "Pal", "Vis", "Mir", "Rot", "XS", "YS"});
    table_->setFont(mono);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setAlternatingRowColors(true);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(20);

    // Set column widths
    table_->setColumnWidth(0, 30);   // #
    table_->setColumnWidth(1, 45);   // X
    table_->setColumnWidth(2, 45);   // Y
    table_->setColumnWidth(3, 40);   // Pat
    table_->setColumnWidth(4, 35);   // Pal
    table_->setColumnWidth(5, 35);   // Vis
    table_->setColumnWidth(6, 35);   // Mir
    table_->setColumnWidth(7, 35);   // Rot
    table_->setColumnWidth(8, 30);   // XS
    table_->setColumnWidth(9, 30);   // YS

    // Pre-populate the index column (static)
    for (int i = 0; i < SpriteEngine::NUM_SPRITES; ++i) {
        auto* item = new QTableWidgetItem(QString::number(i));
        item->setTextAlignment(Qt::AlignCenter);
        table_->setItem(i, 0, item);
    }

    // Create remaining cells
    for (int i = 0; i < SpriteEngine::NUM_SPRITES; ++i) {
        for (int col = 1; col < 10; ++col) {
            auto* item = new QTableWidgetItem("--");
            item->setTextAlignment(Qt::AlignCenter);
            table_->setItem(i, col, item);
        }
    }

    layout->addWidget(table_);
}

void SpritePanel::refresh() {
    if (!emulator_ || !table_) return;

    for (int i = 0; i < SpriteEngine::NUM_SPRITES; ++i) {
        auto info = emulator_->sprites().get_sprite_info(static_cast<uint8_t>(i));

        table_->item(i, 1)->setText(QString::number(info.x));
        table_->item(i, 2)->setText(QString::number(info.y));
        table_->item(i, 3)->setText(QString::asprintf("%02X", info.pattern));
        table_->item(i, 4)->setText(QString::number(info.palette_offset));
        table_->item(i, 5)->setText(info.visible ? "Y" : "-");
        table_->item(i, 6)->setText(
            QString("%1%2").arg(info.x_mirror ? "X" : "-")
                           .arg(info.y_mirror ? "Y" : "-"));
        table_->item(i, 7)->setText(info.rotate ? "R" : "-");

        static const char* scale_labels[] = {"1", "2", "4", "8"};
        table_->item(i, 8)->setText(scale_labels[info.x_scale & 3]);
        table_->item(i, 9)->setText(scale_labels[info.y_scale & 3]);
    }
}
