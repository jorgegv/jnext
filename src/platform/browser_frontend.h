#pragma once
#include <functional>

class SdlApp;

// The browser frontend: the requestAnimationFrame loop around SdlApp::tick().
// Compiled into the jnext_web target only (Emscripten).
namespace browser_frontend {

/// True once run() installed the loop.
bool active();

/// Before SDL_Init: the keyboard element follows the canvas selector.
void prepare();

/// Install the loop; main() returns normally afterwards and the loop keeps the
/// runtime alive. `finish` is the run's teardown (recorders, app.shutdown());
/// it runs once when a tick ends the run, and its result goes to
/// Module.onExit.
void run(SdlApp& app, std::function<int()> finish);

}  // namespace browser_frontend
