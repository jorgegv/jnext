#pragma once

// macOS-only cursor warp for pointer capture (issue #303).
//
// QCursor::setPos on macOS is CGEventPost of a synthetic mouse-moved event
// (qtbase src/plugins/platforms/cocoa/qcocoacursor.mm). That event lands late
// in the window server's stream, and since macOS 10.14 it is silently dropped
// unless the app holds the Accessibility permission. CGWarpMouseCursorPosition
// needs no permission and moves the cursor at once, without generating an
// event. Kept in its own file so the CoreGraphics headers stay out of the Qt
// translation units.

#ifdef __APPLE__
namespace mac_cursor {

/// Move the host cursor to (x, y) in global display coordinates (points,
/// origin at the top-left of the main display — the space QWidget::mapToGlobal
/// returns on macOS).
void warp(int x, int y);

}  // namespace mac_cursor
#endif
