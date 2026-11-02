#include "debugger/annotation_menu.h"

#include "debug/debugger.h"

#include <QAction>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>

using jnext::dbg::Debugger;
using jnext::dbg::Result;

namespace {

QString addr_text(uint16_t a) { return QStringLiteral("$%1").arg(a, 4, 16, QLatin1Char('0')).toUpper(); }

struct PlaceState {
    bool has_label = false;       // a label is shown there now (the user's or the toolchain's)
    bool has_user_label = false;
    bool has_comment = false;
};

PlaceState state_of(const Debugger& dbg, const AnnotationPlace& p)
{
    PlaceState s;
    s.has_label = dbg.symbols().lookup(p.space, p.addr).has_value();
    if (const auto* a = dbg.annotations().find(p.space, p.addr)) {
        s.has_user_label = !a->label.empty();
        s.has_comment = !a->comment.empty();
    }
    return s;
}

void edit_label(QWidget* parent, Debugger& dbg, const AnnotationPlace& p, const std::function<void()>& changed)
{
    const auto cur = dbg.symbols().lookup(p.space, p.addr);
    const bool rename = cur.has_value();
    bool ok = false;
    const QString name = QInputDialog::getText(
        parent, rename ? QObject::tr("Rename Label") : QObject::tr("Add Label"),
        QObject::tr("Label for %1:").arg(addr_text(p.addr)), QLineEdit::Normal,
        cur ? QString::fromStdString(*cur) : QString(), &ok);
    if (!ok) return;
    std::string why;
    if (dbg.set_user_label(p.space, p.addr, name.toStdString(), &why) != Result::Ok) {
        QMessageBox::warning(parent, QObject::tr("Label"), QString::fromStdString(why));
        return;
    }
    if (changed) changed();
}

void edit_comment(QWidget* parent, Debugger& dbg, const AnnotationPlace& p, const std::function<void()>& changed)
{
    const auto* a = dbg.annotations().find(p.space, p.addr);
    const QString cur = a ? QString::fromStdString(a->comment) : QString();
    bool ok = false;
    const QString text = QInputDialog::getMultiLineText(
        parent, cur.isEmpty() ? QObject::tr("Add Comment") : QObject::tr("Edit Comment"),
        QObject::tr("Comment for %1 (empty text removes it):").arg(addr_text(p.addr)), cur, &ok);
    if (!ok) return;
    std::string why;
    if (dbg.set_user_comment(p.space, p.addr, text.toStdString(), &why) != Result::Ok) {
        QMessageBox::warning(parent, QObject::tr("Comment"), QString::fromStdString(why));
        return;
    }
    if (changed) changed();
}

void remove_label(Debugger& dbg, const AnnotationPlace& p, const std::function<void()>& changed)
{
    if (dbg.remove_user_label(p.space, p.addr) == Result::Ok && changed) changed();
}

}  // namespace

void add_annotation_actions(QMenu& menu, QWidget* parent, Debugger& dbg,
                            const std::vector<AnnotationPlace>& places,
                            const std::function<void()>& changed, const QString& no_place_tip)
{
    menu.setToolTipsVisible(true);
    menu.addSeparator();

    std::vector<PlaceState> st;
    for (const auto& p : places) st.push_back(state_of(dbg, p));
    auto all = [&](bool PlaceState::*f) {
        if (st.empty()) return false;
        for (const auto& s : st) if (!(s.*f)) return false;
        return true;
    };
    const QString label_title = all(&PlaceState::has_label) ? QObject::tr("Rename Label...")
                                                            : QObject::tr("Add Label...");
    const QString comment_title = all(&PlaceState::has_comment) ? QObject::tr("Edit Comment...")
                                                                : QObject::tr("Add Comment...");
    const QString toolchain_note = dbg.toolchain_symbols().loaded_file().empty()
        ? QObject::tr("this label comes from the loaded symbols: rename it to override it")
        : QObject::tr("this label comes from %1: rename it to override it")
              .arg(QString::fromStdString(dbg.toolchain_symbols().loaded_file()));

    // One entry: a plain action for one place, a submenu of places for several, a disabled action for none.
    // `enabled(i)` and `tip(i)` say whether and why not for place i.
    auto entry = [&](const QString& title, auto act, auto enabled, auto tip) {
        if (places.empty()) {
            QAction* a = menu.addAction(title);
            a->setEnabled(false);
            a->setToolTip(no_place_tip);
            return;
        }
        if (places.size() == 1) {
            QAction* a = menu.addAction(title);
            const AnnotationPlace p = places[0];
            if (!enabled(0)) {
                a->setEnabled(false);
                a->setToolTip(tip(0));
            }
            QObject::connect(a, &QAction::triggered, &menu, [act, p]() { act(p); });
            return;
        }
        QMenu* sub = menu.addMenu(title);
        sub->setToolTipsVisible(true);
        for (size_t i = 0; i < places.size(); ++i) {
            QAction* a = sub->addAction(places[i].text);
            const AnnotationPlace p = places[i];
            if (!enabled(i)) {
                a->setEnabled(false);
                a->setToolTip(tip(i));
            }
            QObject::connect(a, &QAction::triggered, &menu, [act, p]() { act(p); });
        }
    };

    entry(label_title, [&dbg, parent, changed](const AnnotationPlace& p) { edit_label(parent, dbg, p, changed); },
          [](size_t) { return true; }, [](size_t) { return QString(); });
    entry(QObject::tr("Remove Label"), [&dbg, changed](const AnnotationPlace& p) { remove_label(dbg, p, changed); },
          [&](size_t i) { return st[i].has_user_label; },
          [&](size_t i) { return st[i].has_label ? toolchain_note : QObject::tr("no label here"); });
    entry(comment_title, [&dbg, parent, changed](const AnnotationPlace& p) { edit_comment(parent, dbg, p, changed); },
          [](size_t) { return true; }, [](size_t) { return QString(); });
}
