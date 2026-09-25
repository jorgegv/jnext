#pragma once

#include <utility>

// The ONE place a frontend's host-key surface is connected to the key Router.
//
// WHY THIS IS A FUNCTION AND NOT A FEW LINES INSIDE EACH FRONTEND.
// `host_key_latch.h` makes the case at length for its own existence: the policy
// objects have suites, and what such a suite cannot reach is the code that
// CONNECTS them. This is that code. Left inline it is reachable only through a
// live QApplication (or a live SDL window) — i.e. by nothing — and deleting a
// callback is then a silent change every test passes. Measured, twice: with the
// Qt keyboard-lost wiring inline, removing it failed no row in any suite.
//
// It lives in src/platform/ because BOTH frontends call it and neither may
// depend on the other; jnext_gui already links jnext_platform.
//
// Templated on the source and the router so a suite drives the REAL function
// with the real types rather than a copy of it — a copy is exactly the thing
// that goes stale.

/// Bind "this window can no longer see key-ups" to the Router (GH #268).
///
/// A Qt popup, a focus change, or the SDL window losing focus all take the
/// keyboard away mid-keystroke, and the key-up that follows is delivered to
/// whatever took it. Anything the guest is holding must be let go of now, while
/// the Router still knows what that is — see `host_key_latch::Router::
/// release_all()` for what goes wrong otherwise.
template <typename Source, typename Router>
void wire_keyboard_loss(Source& source, Router& router)
{
    source.set_keyboard_lost_callback([&router]() {
        router.release_all();
    });
}

/// Both directions, for a frontend whose key path is a PLAIN FORWARD (the Qt
/// window: its host hotkeys are QActions and the `switch` in keyPressEvent, so
/// nothing is left to do here).
template <typename Source, typename Router>
void wire_host_keys(Source& source, Router& router)
{
    // Generic in the scancode so the function is not welded to SDL_Scancode:
    // production passes one, and a suite can drive the REAL function with a
    // fake source rather than a copy of it. Both frontends' setters take a
    // std::function, and this converts to either.
    source.set_key_callback([&router](auto sc, bool pressed) {
        router.on_host_key(sc, pressed);
    });
    wire_keyboard_loss(source, router);
}

/// Both directions, for a frontend that supplies its own key callback (the SDL
/// window: its host hotkeys — F1..F11, Ctrl+Alt — are filtered inside that
/// callback before it forwards to the Router).
///
/// Taking the key callback rather than letting the caller bind it separately is
/// the point: it makes the two bindings ONE call, so a frontend cannot end up
/// with keys wired and the focus-loss forgotten. Dropping the call entirely
/// takes the keyboard with it, which the `sdl-keypress-func` and
/// `qt-keypress-func` regression rows already fail on.
template <typename Source, typename Router, typename KeyFn>
void wire_host_keys(Source& source, Router& router, KeyFn&& on_key)
{
    source.set_key_callback(std::forward<KeyFn>(on_key));
    wire_keyboard_loss(source, router);
}
