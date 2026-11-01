#pragma once

// GH #388 (E23-25) - the one place the memory-coverage colours are named, so the
// Disassembly bar, the Memory panel's X|R|W underline and legend and the Source
// tab's tint cannot drift apart. Each goes through palette_tint, so a dark
// palette gets a readable counterpart (QDARK rows).

#include <QColor>
#include <QPalette>

#include "qt/palette_tint_qt.h"

namespace coverage_colors {

/// X: executed (green).
inline QColor exec(const QPalette& p) { return palette_tint::ink(p, QColor(0x2E, 0x9E, 0x44)); }
/// R: read, operands included (blue).
inline QColor read(const QPalette& p) { return palette_tint::ink(p, QColor(0x2F, 0x6F, 0xD6)); }
/// W: written (orange-red).
inline QColor write(const QPalette& p) { return palette_tint::ink(p, QColor(0xD9, 0x48, 0x0F)); }
/// The Source tab's whole-line background for an executed line (pale green).
inline QColor exec_line(const QPalette& p) {
    return palette_tint::surface(p, QColor(0xE0, 0xF5, 0xE3));
}

}  // namespace coverage_colors
