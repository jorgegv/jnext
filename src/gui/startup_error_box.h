#pragma once
#include <string>

namespace gui {

/// Show `text` in an error message box (GH #555: jnext failed to start and the window never came
/// up). Makes a temporary QApplication when none exists. On the offscreen platform nobody can see
/// or dismiss a dialog, so it logs a debug line instead (the SdcardGuiProvisioner::warn
/// precedent): "dialog not shown on the offscreen platform: <title>: <text>".
void show_startup_error(const std::string& title, const std::string& text);

}  // namespace gui
