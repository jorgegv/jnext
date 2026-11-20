// GH #498 - put a panel on show in the debugger window (its tab current). The ONE place a test
// does that. GH #377: the panels are docks, so it raises the panel's dock (reopening a closed one);
// the Disassembly, the central widget, is always on show.
#pragma once

#include <QDockWidget>
#include <QWidget>

#include "debugger/callstack_panel.h"
#include "debugger/debugger_window.h"

inline void put_on_show(DebuggerWindow* w, QWidget* panel) {
    for (QDockWidget* d : w->findChildren<QDockWidget*>())
        if (d->widget() == panel) {
            if (d->isHidden()) d->show();
            d->raise();
            return;
        }
}
