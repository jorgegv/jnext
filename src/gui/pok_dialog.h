#pragma once

#include <QDialog>

class QLabel;
class QPushButton;
class QTreeWidget;

namespace jnext { namespace pok { class Session; } }

/// GH #381 - Machine > POK Trainers...: the trainers of an open .pok file, with
/// their state in the running machine, and Apply / Remove for the selected one.
///
/// A view of a jnext::pok::Session and nothing more: every decision (what a poke
/// writes, where, what undo restores, what state a trainer is in) is the
/// session's, in src/platform, and is unit-tested there without Qt. This class
/// only asks for a value when a trainer leaves one open, shows a refusal, and
/// keeps its rows in step with memory - which a program can change under it, so
/// refresh() re-derives every status rather than trusting what an action left.
///
/// The session is owned by MainWindow, which replaces it when another file is
/// opened (set_session()) and drops its undo records when the machine is rebuilt.
class PokDialog : public QDialog {
    Q_OBJECT
public:
    explicit PokDialog(QWidget* parent = nullptr);

    /// Show `session` (null = none). Not owned: it must outlive this binding.
    void set_session(jnext::pok::Session* session);

    /// Re-derive every row's status, and the buttons, from the machine.
    void refresh();

signals:
    /// "Open POK File..." was pressed: the window runs its file picker.
    void open_file_requested();

private:
    int  selected_trainer() const;
    void update_buttons();
    void on_apply();
    void on_remove();
    void refuse(const QString& why);

    jnext::pok::Session* session_ = nullptr;
    QLabel*      path_label_     = nullptr;
    QTreeWidget* trainers_       = nullptr;
    QLabel*      problems_label_ = nullptr;
    QPushButton* open_button_    = nullptr;
    QPushButton* apply_button_   = nullptr;
    QPushButton* remove_button_  = nullptr;
};
