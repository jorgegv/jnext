// GH #498 - put a panel on show in the debugger window (its tab current). The ONE place a test
// does that; GH #377 (docked panels) changes this helper to raise the panel's dock instead.
#pragma once

#include <QTabWidget>
#include <QWidget>

#include "debugger/callstack_panel.h"
#include "debugger/debugger_window.h"

inline void put_on_show(DebuggerWindow* w, QWidget* panel) {
    for (QTabWidget* tw : w->findChildren<QTabWidget*>())
        if (tw->indexOf(panel) >= 0) {
            tw->setCurrentWidget(panel);
            return;
        }
}
