#pragma once

#include <QtGlobal>

// Qt on macOS maps the Command key to Qt::Key_Control / ControlModifier (and
// Control to Meta) unless AA_MacDontSwapCtrlAndMeta is set; jnext does not set
// it. So the Ctrl+Alt release chord matched in MainWindow::keyPressEvent is
// Alt+Command there, and the Preferences labels must spell it the same way
// (GH #307, GH #365).
// JNEXT_TEST_FORCE_CTRL_IS_COMMAND: test-only (host_shortcuts_mac_test), so the
// macOS value of every default argument is asserted on a Linux host.
#if defined(Q_OS_MACOS) || defined(JNEXT_TEST_FORCE_CTRL_IS_COMMAND)
inline constexpr bool kQtCtrlIsCommand = true;
#else
inline constexpr bool kQtCtrlIsCommand = false;
#endif
