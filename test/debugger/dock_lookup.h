// GH #377: how a test finds a debugger panel's dock and its group's shown tab, through Qt's own API
// (QDockWidget, QMainWindow::tabifiedDockWidgets, the group's QTabBar), never through the window's
// private members. Before docking a panel was a page of a QTabWidget; these helpers answer the same two
// questions ("which tab holds it", "which tab is shown") for a panel that is now the widget of a dock.
#pragma once

#include <QDockWidget>
#include <QList>
#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QTabBar>
#include <QWidget>

/// The dock whose widget is `panel`, found under `root`; nullptr when there is none.
inline QDockWidget* dock_of_panel(QWidget* root, QWidget* panel)
{
    if (!root || !panel) return nullptr;
    for (QDockWidget* d : root->findChildren<QDockWidget*>())
        if (d->widget() == panel) return d;
    return nullptr;
}

/// The title of the tab SHOWN in `d`'s group: the group's tab bar current text, or `d`'s own title when
/// it is alone in its group.
inline QString dock_group_shown_title(QDockWidget* d)
{
    auto* host = d ? qobject_cast<QMainWindow*>(d->parentWidget()) : nullptr;
    if (!host || host->tabifiedDockWidgets(d).isEmpty()) return d ? d->windowTitle() : QString();
    for (QTabBar* bar : host->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly))
        for (int i = 0; i < bar->count(); ++i)
            if (bar->tabText(i) == d->windowTitle())
                return bar->tabText(bar->currentIndex());
    return QString();
}

/// The titles of the tabs of `d`'s group, in tab order (just its own title when alone).
inline QStringList dock_group_titles(QDockWidget* d)
{
    auto* host = d ? qobject_cast<QMainWindow*>(d->parentWidget()) : nullptr;
    if (!host || host->tabifiedDockWidgets(d).isEmpty()) return d ? QStringList{d->windowTitle()} : QStringList();
    for (QTabBar* bar : host->findChildren<QTabBar*>(QString(), Qt::FindDirectChildrenOnly)) {
        QStringList t;
        for (int i = 0; i < bar->count(); ++i) t << bar->tabText(i);
        if (t.contains(d->windowTitle())) return t;
    }
    return QStringList();
}

/// Another dock of `d`'s group (nullptr when alone).
inline QDockWidget* dock_group_other(QDockWidget* d)
{
    auto* host = d ? qobject_cast<QMainWindow*>(d->parentWidget()) : nullptr;
    if (!host) return nullptr;
    const QList<QDockWidget*> g = host->tabifiedDockWidgets(d);
    return g.isEmpty() ? nullptr : g.first();
}
