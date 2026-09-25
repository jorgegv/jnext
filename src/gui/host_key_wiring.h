#pragma once

#include <SDL3/SDL.h>

/// The ONE place the emulator window's host-key surface is connected to the
/// key Router — both directions of it.
///
/// WHY THIS IS A FUNCTION AND NOT TWO LINES INSIDE QtApp. `host_key_latch.h`
/// makes the case at length for its own existence: the policy objects have
/// suites, and what such a suite cannot reach is the code that CONNECTS them.
/// This is that code. Left inline in `QtApp::init()` it is reachable only
/// through a live QApplication + Emulator + SDL audio device, i.e. by nothing,
/// and deleting either callback is then a silent change that every test passes.
/// Measured, not assumed: with the keyboard-lost wiring inline, removing it
/// failed no row in either suite.
///
/// Templated on both types so the suite can drive the REAL function with the
/// real MainWindow and the real Router rather than a copy of it — a copy is
/// exactly the thing that goes stale.
///
/// GH #268 for `set_keyboard_lost_callback`: a Qt popup or a focus change takes
/// the keyboard away mid-keystroke and the key-up goes there instead, so the
/// guest must be told to let go of everything it holds. See
/// `host_key_latch::Router::release_all()`.
template <typename Window, typename Router>
void wire_host_keys(Window& window, Router& router)
{
    window.set_key_callback([&router](SDL_Scancode sc, bool pressed) {
        router.on_host_key(sc, pressed);
    });
    window.set_keyboard_lost_callback([&router]() {
        router.release_all();
    });
}
