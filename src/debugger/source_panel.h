#pragma once

#include <QWidget>

#include <string>

class Emulator;
class QLabel;
class QPlainTextEdit;
class SourceMap;

/// Read-only BASIC/assembly source view driven by SLD trace records.
class SourcePanel : public QWidget {
    Q_OBJECT
public:
    explicit SourcePanel(Emulator* emulator, QWidget* parent = nullptr);

    void set_source_map(SourceMap* source_map) { source_map_ = source_map; }
    void refresh();

private:
    QString resolve_source_file(const std::string& source_name) const;
    void show_message(const QString& message);

    Emulator* emulator_ = nullptr;
    SourceMap* source_map_ = nullptr;
    QLabel* location_label_ = nullptr;
    QPlainTextEdit* editor_ = nullptr;
    QString loaded_source_;
    qint64 loaded_mtime_ = -1;
};
