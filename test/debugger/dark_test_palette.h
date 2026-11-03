#pragma once

// A dark application palette (Fusion's dark roles that the panels read), shared
// by the suites that switch the application palette with a window open.
#include <QColor>
#include <QPalette>

static inline QPalette dark_test_palette() {
    QPalette p;
    for (auto g : {QPalette::Active, QPalette::Inactive}) {
        p.setColor(g, QPalette::Window, QColor(0x32, 0x32, 0x32));
        p.setColor(g, QPalette::WindowText, QColor(0xF0, 0xF0, 0xF0));
        p.setColor(g, QPalette::Base, QColor(0x24, 0x24, 0x24));
        p.setColor(g, QPalette::Text, QColor(0xF0, 0xF0, 0xF0));
    }
    return p;
}
