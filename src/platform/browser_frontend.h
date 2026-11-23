#pragma once
#include <functional>

class SdlApp;

// The browser frontend: the requestAnimationFrame loop around SdlApp::tick().
// Compiled into the jnext_web target only (Emscripten).
namespace browser_frontend {

/// True once run() installed the loop.
bool active();

/// First thing in main(): every file the run writes for an output option (core/output_files.h)
/// is reported to the page's Module.jnextFile(path, complete), for player.listFiles().
void install_output_sink();

/// Before SDL_Init: the keyboard element follows the canvas selector.
void prepare();

/// Install the loop; main() returns normally afterwards and the loop keeps the
/// runtime alive. `finish` is the run's teardown (recorders, app.shutdown());
/// it runs once when a tick ends the run, and its result goes to
/// Module.onExit.
void run(SdlApp& app, std::function<int()> finish);

/// --touch-keyboard: 0 auto, 1 on, 2 off (cli::TouchKeyboard). The last call wins; the page reads
/// it back through jnext_web_touch_keyboard().
void set_touch_keyboard(int mode);

}  // namespace browser_frontend
