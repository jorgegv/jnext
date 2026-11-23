#pragma once
#include <string>

namespace startup_report {

/// The SDL build's counterpart of gui::show_startup_error (GH #555): SDL_ShowSimpleMessageBox.
/// With SDL_VIDEO_DRIVER offscreen or dummy nobody can see a box, so it logs the same debug line
/// the Qt one does instead. A no-op in the browser build, which has no message box.
void show_error_sdl(const std::string& title, const std::string& text);

}  // namespace startup_report
