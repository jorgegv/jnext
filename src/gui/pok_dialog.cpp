#include "gui/pok_dialog.h"

#include <QByteArray>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <optional>

#include "platform/pok_trainers.h"

namespace {

/// How many file-level problems the dialog lists before saying "and N more".
constexpr int kMaxProblemsShown = 20;

/// A trainer name is the bytes of the file: UTF-8 in some, Latin-1 in others.
QString decode_name(const std::string& raw) {
    const QByteArray bytes(raw.data(), static_cast<int>(raw.size()));
    return jnext::pok::is_valid_utf8(raw) ? QString::fromUtf8(bytes)
                                          : QString::fromLatin1(bytes);
}

}  // namespace

PokDialog::PokDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("pok_trainers_dialog"));
    setWindowTitle(tr("POK Trainers"));
    resize(560, 380);

    auto* layout = new QVBoxLayout(this);

    path_label_ = new QLabel(this);
    path_label_->setObjectName(QStringLiteral("path"));
    path_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(path_label_);

    trainers_ = new QTreeWidget(this);
    trainers_->setObjectName(QStringLiteral("trainers"));
    trainers_->setColumnCount(2);
    trainers_->setHeaderLabels({tr("Trainer"), tr("Status")});
    trainers_->setRootIsDecorated(false);
    trainers_->setUniformRowHeights(true);
    trainers_->setSelectionMode(QAbstractItemView::SingleSelection);
    trainers_->header()->setStretchLastSection(false);
    trainers_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    trainers_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    layout->addWidget(trainers_, 1);

    problems_label_ = new QLabel(this);
    problems_label_->setObjectName(QStringLiteral("problems"));
    problems_label_->setWordWrap(true);
    problems_label_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(problems_label_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    open_button_   = buttons->addButton(tr("Open POK File..."), QDialogButtonBox::ActionRole);
    apply_button_  = buttons->addButton(tr("Apply"), QDialogButtonBox::ActionRole);
    remove_button_ = buttons->addButton(tr("Remove"), QDialogButtonBox::ActionRole);
    open_button_->setObjectName(QStringLiteral("open"));
    apply_button_->setObjectName(QStringLiteral("apply"));
    remove_button_->setObjectName(QStringLiteral("remove"));
    // Enter must not poke the machine: only Close answers it.
    for (QPushButton* b : {open_button_, apply_button_, remove_button_}) b->setAutoDefault(false);
    layout->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(open_button_, &QPushButton::clicked, this, &PokDialog::open_file_requested);
    connect(apply_button_, &QPushButton::clicked, this, &PokDialog::on_apply);
    connect(remove_button_, &QPushButton::clicked, this, &PokDialog::on_remove);
    connect(trainers_, &QTreeWidget::currentItemChanged, this, [this]() { update_buttons(); });

    update_buttons();
}

void PokDialog::set_session(jnext::pok::Session* session) {
    session_ = session;
    trainers_->clear();
    if (session_) {
        path_label_->setText(QString::fromStdString(session_->path()));
        for (const jnext::pok::Trainer& t : session_->file().trainers) {
            auto* item = new QTreeWidgetItem(trainers_);
            item->setText(0, t.name.empty() ? tr("(no name)") : decode_name(t.name));
        }
        if (trainers_->topLevelItemCount() > 0)
            trainers_->setCurrentItem(trainers_->topLevelItem(0));
    } else {
        path_label_->clear();
    }
    refresh();
}

void PokDialog::refresh() {
    if (!session_) {
        problems_label_->clear();
        problems_label_->setVisible(false);
        update_buttons();
        return;
    }
    const auto& trainers = session_->file().trainers;
    for (int i = 0; i < trainers_->topLevelItemCount() && i < static_cast<int>(trainers.size());
         ++i) {
        const jnext::pok::Trainer& t = trainers[static_cast<size_t>(i)];
        QString status;
        if (!t.problem.empty()) {
            status = tr("invalid - %1").arg(QString::fromStdString(t.problem));
        } else {
            switch (session_->state(static_cast<size_t>(i))) {
            case jnext::pok::State::Applied:      status = tr("applied"); break;
            case jnext::pok::State::AppliedFound: status = tr("applied (found in memory)"); break;
            case jnext::pok::State::NotApplied:   status = tr("not applied"); break;
            }
        }
        QTreeWidgetItem* item = trainers_->topLevelItem(i);
        item->setText(1, status);
        item->setToolTip(1, status);
    }

    const auto& problems = session_->file().problems;
    QStringList lines;
    for (size_t k = 0; k < problems.size() && k < static_cast<size_t>(kMaxProblemsShown); ++k)
        lines << QString::fromStdString(problems[k]);
    if (problems.size() > static_cast<size_t>(kMaxProblemsShown))
        lines << tr("and %1 more").arg(static_cast<int>(problems.size()) - kMaxProblemsShown);
    problems_label_->setText(lines.join(QLatin1Char('\n')));
    problems_label_->setVisible(!lines.isEmpty());
    update_buttons();
}

int PokDialog::selected_trainer() const {
    QTreeWidgetItem* item = trainers_->currentItem();
    return item ? trainers_->indexOfTopLevelItem(item) : -1;
}

void PokDialog::update_buttons() {
    bool can_apply = false, can_remove = false;
    const int i = selected_trainer();
    if (session_ && i >= 0 && i < static_cast<int>(session_->file().trainers.size()) &&
        session_->file().trainers[static_cast<size_t>(i)].problem.empty()) {
        const bool applied = session_->state(static_cast<size_t>(i)) != jnext::pok::State::NotApplied;
        can_apply  = !applied;
        can_remove = applied;
    }
    apply_button_->setEnabled(can_apply);
    remove_button_->setEnabled(can_remove);
}

void PokDialog::refuse(const QString& why) {
    QMessageBox::warning(this, tr("POK Trainers"), why);
}

void PokDialog::on_apply() {
    const int i = selected_trainer();
    if (!session_ || i < 0) return;
    const size_t index = static_cast<size_t>(i);
    const jnext::pok::Trainer& t = session_->file().trainers[index];

    std::optional<uint8_t> value;
    if (t.asks_value()) {
        // One byte for every poke the file leaves open, offered as what memory
        // holds now (the game's current lives, say).
        const std::optional<uint8_t> current = session_->value_default(index);
        if (!current) {
            // apply() names the poke that cannot land, before it asks for a value.
            refuse(QString::fromStdString(session_->apply(index, std::nullopt)));
            refresh();
            return;
        }
        bool ok = false;
        const int v = QInputDialog::getInt(
            this, tr("POK Trainers"),
            tr("Value for \"%1\" (0-255):").arg(decode_name(t.name)),
            *current, 0, 255, 1, &ok);
        if (!ok) return;
        value = static_cast<uint8_t>(v);
    }
    const std::string why = session_->apply(index, value);
    if (!why.empty()) refuse(QString::fromStdString(why));
    refresh();
}

void PokDialog::on_remove() {
    const int i = selected_trainer();
    if (!session_ || i < 0) return;
    const std::string why = session_->remove(static_cast<size_t>(i));
    if (!why.empty()) refuse(QString::fromStdString(why));
    refresh();
}
