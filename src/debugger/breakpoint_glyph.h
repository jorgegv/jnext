#pragma once

// GH #421 -- the breakpoint mark of a debugger gutter, ONE implementation for
// the Disassembly and the Source tab, so "the same dot" holds by construction.

#include <QBrush>
#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPoint>
#include <QRect>

/// Width of the BP column, in pixels.
constexpr int kBreakpointColumnWidth = 20;

/// A breakpoint mark centred on `centre`, in the same red on every palette.
///  - `live`: a filled disc (it can fire); else a hollow ring (suspended, GH #225);
///  - `partial`: only the LEFT HALF of that glyph (a Source line some of whose
///    places carry a breakpoint and some do not): shape, not colour, carries it.
inline void paint_breakpoint_glyph(QPainter& painter, QPoint centre, bool live, bool partial)
{
    const QColor red(255, 0, 0);
    if (live) {
        painter.setBrush(red);
        painter.setPen(Qt::NoPen);
        if (partial)
            painter.drawPie(QRect(centre.x() - 5, centre.y() - 5, 10, 10), 90 * 16, 180 * 16);
        else
            painter.drawEllipse(centre, 5, 5);
    } else {
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(red, 2));
        if (partial)
            painter.drawArc(QRect(centre.x() - 4, centre.y() - 4, 8, 8), 90 * 16, 180 * 16);
        else
            painter.drawEllipse(centre, 4, 4);
    }
}
