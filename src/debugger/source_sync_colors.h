#pragma once

// GH #347 (E23-16) - the one place the colour of the Source <-> Disassembly sync is
// named, so the Disassembly's row fill and the Source tab's line tint (per scheme,
// see SourceHighlighter::SchemeColours::linked_line) cannot drift apart. A pale
// violet: it differs from the Disassembly's PC yellow (#FFFFCC), its blue selection
// and #388's pale green coverage tint (QSRC rows pin the distance).

#include <QColor>
#include <QPalette>

#include "qt/palette_tint_qt.h"

namespace source_sync_colors {

/// The tint of the lines linked to the other view's selection.
inline QColor linked(const QPalette& p) {
    return palette_tint::surface(p, QColor(0xEA, 0xDC, 0xF8));
}

}  // namespace source_sync_colors
