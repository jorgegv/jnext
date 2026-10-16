#pragma once

#include <SDL3/SDL.h>

#include "platform/host_key_wiring.h"
#include "platform/pointer_capture.h"

// The SDL frontend's route for the pointer-release gesture (GH #365).
//
// These were the bodies of SdlApp's on_mouse lambda and of the Ctrl+Alt block
// in its key callback. They are templates in a header, like host_key_wiring.h,
// so a suite drives the REAL functions through the real SdlInput::poll()
// instead of a copy: left inline in SdlApp::init they are reachable only
// through a live SDL window, i.e. by nothing. The DECISION is
// pointer_capture::ReleaseFilter; this is only the SDL event -> decision
// mapping and what is done with the verdict.

namespace sdl_pointer_release {

inline pointer_capture::HostButton to_host_button(Uint8 b) {
    switch (b) {
        case SDL_BUTTON_LEFT:   return pointer_capture::HostButton::Left;
        case SDL_BUTTON_MIDDLE: return pointer_capture::HostButton::Middle;
        case SDL_BUTTON_RIGHT:  return pointer_capture::HostButton::Right;
        default:                return pointer_capture::HostButton::Other;
    }
}

inline pointer_capture::HostKey to_host_key(SDL_Scancode sc) {
    switch (sc) {
        case SDL_SCANCODE_LCTRL: case SDL_SCANCODE_RCTRL: return pointer_capture::HostKey::Ctrl;
        case SDL_SCANCODE_LALT:  case SDL_SCANCODE_RALT:  return pointer_capture::HostKey::Alt;
        case SDL_SCANCODE_ESCAPE:                         return pointer_capture::HostKey::Esc;
        default:                                          return pointer_capture::HostKey::Other;
    }
}

/// An SDL mouse event. SetCaptured: void(bool). Dispatcher:
/// handle_sdl_event(const SDL_Event&).
template <class SetCaptured, class Dispatcher>
void route_sdl_mouse(const SDL_Event& e, pointer_capture::ReleaseFilter& f, bool captured,
                     SetCaptured&& set_captured, Dispatcher& d)
{
    if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN || e.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        const pointer_capture::Verdict v = f.on_button(
            to_host_button(e.button.button), e.type == SDL_EVENT_MOUSE_BUTTON_DOWN, captured);
        if (v.release) set_captured(false);
        if (v.consume) return;
    }
    // Uncaptured the pointer belongs to the desktop; clicking the window is
    // how you hand it to the guest (same gesture as the Qt frontend).
    if (!captured) {
        if (e.type == SDL_EVENT_MOUSE_BUTTON_DOWN) set_captured(true);
        return;
    }
    d.handle_sdl_event(e);
}

/// An SDL key event, before hotkeys and the key Router. Returns true when the
/// key is consumed (it must reach neither). `mods` is SDL_GetModState().
template <class SetCaptured>
bool route_sdl_release_key(pointer_capture::ReleaseFilter& f, SDL_Scancode sc, bool pressed,
                           SDL_Keymod mods, bool captured, SetCaptured&& set_captured)
{
    const pointer_capture::Verdict v = f.on_key(
        to_host_key(sc), pressed, (mods & SDL_KMOD_CTRL) != 0, (mods & SDL_KMOD_ALT) != 0, captured);
    if (v.release) set_captured(false);
    return v.consume;
}

/// SdlApp's key surface, in ONE call: the key callback gated by the release
/// filter (a consumed key never reaches `next`, which holds the hotkeys and the
/// Router forward), and the keyboard-loss binding extended to forget a swallowed
/// Esc whose release went elsewhere BEFORE the previously bound callback (the
/// Router's release_all, GH #268) runs.
template <class Source, class Router, class Mods, class Captured, class SetCaptured, class Next>
void wire_release_host_keys(Source& source, Router& router, pointer_capture::ReleaseFilter& f,
                            Mods mods, Captured captured, SetCaptured set_captured, Next next)
{
    wire_host_keys(source, router,
        [&f, mods, captured, set_captured, next](SDL_Scancode sc, bool pressed) {
            if (route_sdl_release_key(f, sc, pressed, mods(), captured(), set_captured))
                return;
            next(sc, pressed);
        });
    auto router_lost = source.on_keyboard_lost;
    source.on_keyboard_lost = [&f, router_lost]() {
        f.reset();
        if (router_lost) router_lost();
    };
}

}  // namespace sdl_pointer_release
