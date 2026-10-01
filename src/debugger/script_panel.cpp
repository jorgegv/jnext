#include "debugger/script_panel.h"

#include "script/script_host.h"

#include <QFileDialog>
#include <QFont>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTextBlock>
#include <QVBoxLayout>

using jnext::script::LoadResult;
using jnext::script::ScriptEngine;

namespace {

QString errors_of(const LoadResult& r) {
    QStringList out;
    for (const auto& d : r.errors)
        out << QString::fromStdString(r.file + ":" + d.to_string());
    return out.join('\n');
}

}  // namespace

ScriptPanel::ScriptPanel(QWidget* parent) : QWidget(parent) {
    QFont mono("Monospace", 9);
    mono.setStyleHint(QFont::Monospace);

    auto* main_layout = new QVBoxLayout(this);
    main_layout->setSpacing(4);
    main_layout->setContentsMargins(4, 4, 4, 4);

    auto* buttons = new QHBoxLayout();
    load_btn_   = new QPushButton(tr("Load..."), this);
    reload_btn_ = new QPushButton(tr("Reload"), this);
    unload_btn_ = new QPushButton(tr("Unload All"), this);
    buttons->addWidget(load_btn_);
    buttons->addWidget(reload_btn_);
    buttons->addWidget(unload_btn_);
    buttons->addStretch();
    main_layout->addLayout(buttons);
    connect(load_btn_, &QPushButton::clicked, this, &ScriptPanel::on_load_clicked);
    connect(reload_btn_, &QPushButton::clicked, this, &ScriptPanel::on_reload_clicked);
    connect(unload_btn_, &QPushButton::clicked, this, &ScriptPanel::on_unload_clicked);

    verdict_ = new QLabel(this);
    verdict_->setWordWrap(true);
    main_layout->addWidget(verdict_);

    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({tr("File"), tr("Rule"), tr("Event"), tr("State"), tr("Hits")});
    table_->setFont(mono);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->verticalHeader()->setVisible(false);
    table_->verticalHeader()->setDefaultSectionSize(20);
    table_->horizontalHeader()->setStretchLastSection(true);
    main_layout->addWidget(table_, 2);

    log_ = new QPlainTextEdit(this);
    log_->setReadOnly(true);
    log_->setFont(mono);
    log_->setMaximumBlockCount(static_cast<int>(jnext::script::ScriptHost::MAX_LOG_LINES));
    main_layout->addWidget(log_, 3);

    refresh();
}

void ScriptPanel::set_host(jnext::script::ScriptHost* host) {
    host_    = host;
    log_seq_ = 0;
    log_->clear();
    refresh();
}

bool ScriptPanel::load_path(const QString& path, QString* errors) {
    if (!host_) {
        if (errors) *errors = tr("scripting is not available in this window");
        return false;
    }
    const LoadResult r = host_->load_file(path.toStdString());
    refresh();
    if (errors) *errors = errors_of(r);
    return r.ok();
}

QString ScriptPanel::reload_all() {
    if (!host_) return QString();
    QStringList errs;
    for (const LoadResult& r : host_->reload())
        if (!r.ok()) errs << errors_of(r);
    refresh();
    return errs.join('\n');
}

void ScriptPanel::unload_all() {
    if (host_) host_->unload_all();
    refresh();
}

void ScriptPanel::on_load_clicked() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Load Debugger Script"), QString(),
                                                      tr("Debugger scripts (*.jds);;All files (*)"));
    if (path.isEmpty()) return;
    QString errors;
    if (!load_path(path, &errors))
        QMessageBox::warning(this, tr("Script Not Loaded"),
                             tr("%1 was not loaded:\n\n%2").arg(path, errors));
}

void ScriptPanel::on_reload_clicked() {
    const QString errors = reload_all();
    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("Script Not Loaded"), tr("Not every script reloaded:\n\n%1").arg(errors));
}

void ScriptPanel::on_unload_clicked() { unload_all(); }

QString ScriptPanel::verdict_text() const { return verdict_->text(); }

QStringList ScriptPanel::log_lines() const {
    QStringList out;
    for (QTextBlock b = log_->document()->begin(); b.isValid(); b = b.next())
        if (!b.text().isEmpty()) out << b.text();
    return out;
}

void ScriptPanel::refresh() {
    const bool have = host_ != nullptr;
    load_btn_->setEnabled(have);
    const bool loaded = have && !host_->files().empty();
    reload_btn_->setEnabled(loaded);
    unload_btn_->setEnabled(loaded);

    // The rules.
    std::vector<ScriptEngine::RuleView> rules;
    if (have && host_->engine()) rules = host_->engine()->rules();
    table_->setRowCount(static_cast<int>(rules.size()));
    for (int i = 0; i < static_cast<int>(rules.size()); ++i) {
        const auto& r = rules[static_cast<size_t>(i)];
        const QString file = QString::fromStdString(r.file).section('/', -1);
        const QString name = r.label.empty()
                                 ? QStringLiteral("%1:%2").arg(r.pos.line).arg(r.pos.column)
                                 : QString::fromStdString(r.label);
        QString state = r.dead      ? tr("error (disabled)")
                      : !r.enabled  ? tr("disabled")
                      : r.spent     ? tr("spent (once)")
                                    : tr("armed");
        if (r.verdict && r.hits == 0 && !r.dead) state += tr(", verdict not reached");
        const QStringList cells = {file, name, QString::fromStdString(r.event), state,
                                   QString::number(static_cast<qulonglong>(r.hits))};
        for (int c = 0; c < cells.size(); ++c) {
            QTableWidgetItem* it = table_->item(i, c);
            if (!it) {
                it = new QTableWidgetItem;
                table_->setItem(i, c, it);
            }
            it->setText(cells[c]);
        }
    }

    // The verdict line.
    QString v;
    if (!have) {
        v = tr("Scripting is not available.");
    } else if (host_->files().empty()) {
        v = tr("No script loaded.");
    } else {
        const ScriptEngine::Status s = host_->engine()->status();
        QStringList parts;
        if (s.exit_code)
            parts << (*s.exit_code == 0 ? tr("PASS: exit 0") : tr("FAIL: exit %1").arg(*s.exit_code)) +
                         tr(" (the machine paused; the GUI never exits from a script)");
        if (s.stops)
            parts << tr("FAIL: %n stop(s), the last: %1", "", static_cast<int>(s.stops))
                         .arg(QString::fromStdString(s.last_stop));
        if (s.runtime_errors)
            parts << tr("ERROR: %n rule(s) disabled by a run-time error", "", static_cast<int>(s.runtime_errors));
        if (s.unreached) parts << tr("%n verdict(s) not reached yet", "", static_cast<int>(s.unreached));
        if (parts.isEmpty()) parts << tr("Running: no verdict declared or reached");
        v = tr("%n script(s): ", "", static_cast<int>(host_->files().size())) + parts.join(QStringLiteral("; "));
    }
    verdict_->setText(v);

    // The log, incrementally.
    if (have) {
        for (const std::string& line : host_->log_since(log_seq_)) log_->appendPlainText(QString::fromStdString(line));
        log_seq_ = host_->log_seq();
    }
}
