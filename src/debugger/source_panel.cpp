#include "debugger/source_panel.h"

#include "core/emulator.h"
#include "debug/source_map.h"

#include <QFile>
#include <QDateTime>
#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextEdit>
#include <QVBoxLayout>

#include <filesystem>

SourcePanel::SourcePanel(Emulator* emulator, QWidget* parent)
    : QWidget(parent), emulator_(emulator)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    location_label_ = new QLabel(tr("No source map loaded"), this);
    editor_ = new QPlainTextEdit(this);
    editor_->setReadOnly(true);
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono("Monospace", 10);
    mono.setStyleHint(QFont::Monospace);
    editor_->setFont(mono);
    layout->addWidget(location_label_);
    layout->addWidget(editor_, 1);
}

QString SourcePanel::resolve_source_file(const std::string& source_name) const
{
    namespace fs = std::filesystem;
    const fs::path source(source_name);
    std::error_code ec;
    if (source.is_absolute() && fs::is_regular_file(source, ec))
        return QString::fromStdString(source.string());
    if (!source_map_ || source_map_->loaded_file().empty()) return {};

    fs::path root = fs::path(source_map_->loaded_file()).parent_path();
    while (!root.empty()) {
        const fs::path candidate = root / source;
        ec.clear();
        if (fs::is_regular_file(candidate, ec))
            return QString::fromStdString(candidate.string());
        const fs::path parent = root.parent_path();
        if (parent == root) break;
        root = parent;
    }
    return {};
}

void SourcePanel::show_message(const QString& message)
{
    location_label_->setText(message);
    if (!loaded_source_.isEmpty()) {
        editor_->clear();
        loaded_source_.clear();
        loaded_mtime_ = -1;
    }
    editor_->setExtraSelections({});
}

void SourcePanel::refresh()
{
    if (!emulator_ || !emulator_->debug_state().paused()) return;
    if (!source_map_ || source_map_->empty()) {
        show_message(tr("No source map loaded"));
        return;
    }

    const uint16_t pc = emulator_->cpu().get_registers().PC;
    const uint8_t page = emulator_->mmu().get_effective_page(pc >> 13);
    const auto location = source_map_->lookup(page, pc);
    if (!location) {
        show_message(tr("$%1 (page %2): no source mapping; use disassembly")
                         .arg(pc, 4, 16, QLatin1Char('0')).arg(page));
        return;
    }

    const QString source_path = resolve_source_file(location->file);
    if (source_path.isEmpty()) {
        show_message(tr("%1:%2 — source file not found")
                         .arg(QString::fromStdString(location->file))
                         .arg(location->line));
        return;
    }
    const qint64 source_mtime = QFileInfo(source_path).lastModified().toMSecsSinceEpoch();
    if (source_path != loaded_source_ || source_mtime != loaded_mtime_) {
        QFile file(source_path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            show_message(tr("Could not open %1").arg(source_path));
            return;
        }
        editor_->setPlainText(QString::fromUtf8(file.readAll()));
        loaded_source_ = source_path;
        loaded_mtime_ = source_mtime;
    }

    location_label_->setText(
        tr("%1:%2  $%3  page %4")
            .arg(QString::fromStdString(location->file))
            .arg(location->line)
            .arg(pc, 4, 16, QLatin1Char('0'))
            .arg(page));
    const QTextBlock block = editor_->document()->findBlockByNumber(location->line - 1);
    if (!block.isValid()) return;

    QTextEdit::ExtraSelection selection;
    selection.cursor = QTextCursor(block);
    selection.cursor.clearSelection();
    selection.format.setBackground(editor_->palette().highlight());
    selection.format.setForeground(editor_->palette().highlightedText());
    selection.format.setProperty(QTextFormat::FullWidthSelection, true);
    editor_->setExtraSelections({selection});
    editor_->setTextCursor(selection.cursor);
    editor_->centerCursor();
}
