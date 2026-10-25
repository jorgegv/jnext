#pragma once

#include <SDL3/SDL.h>

#include "platform/quick_snapshot_chord.h"

// The SDL frontend's F1 / F4 host hotkeys (GH #380).
//
// This was a pair of bare lines in SdlApp's key callback, with no modifier test
// at all: Alt+F4 soft-reset the machine instead of closing the window, and
// Alt+F1 hard-reset it. A template in a header, like sdl_pointer_release.h, so
// a suite drives the REAL decision through the real SdlInput::poll() instead of
// a copy: left inline in SdlApp::init it is reachable only through a live SDL
// window, i.e. by nothing.

namespace sdl_reset_keys {

/// LAlt | RAlt in QuickChordTracker::held_modifiers() (bit n = scancode 224 + n).
inline constexpr unsigned kAltHeld = (1u << (SDL_SCANCODE_LALT - SDL_SCANCODE_LCTRL)) |
                                     (1u << (SDL_SCANCODE_RALT - SDL_SCANCODE_LCTRL));
static_assert(kAltHeld == host_modifier::kAltBits, "host_modifier.h Alt bits must match SDL");
static_assert(host_modifier::kGuiBits ==
                  ((1u << (SDL_SCANCODE_LGUI - SDL_SCANCODE_LCTRL)) |
                   (1u << (SDL_SCANCODE_RGUI - SDL_SCANCODE_LCTRL))),
              "host_modifier.h GUI bits must match SDL");

/// A PRESS of F1 or F4. Without Alt they are the hard and soft reset
/// (zxnext.vhd:6340/6343 -> 6371/6370). With Alt held, left or right and
/// whatever else is held, neither resets, and F4 quits through SDL_EVENT_QUIT,
/// the event a window close raises, so it takes the normal quit path
/// (SdlInput::poll() -> on_quit; SDL delivers an event pushed during a poll on
/// the next one, i.e. the next iteration of SdlApp::run()).
///
/// `chords` is the tracker SdlApp feeds every key event first, so a whole
/// Alt+F4 delivered in one poll batch still counts as Alt held, which
/// SDL_GetModState() would not say. Returns true for F1 and F4: the press
/// belongs to the host either way and never reaches the guest.
///
/// Logs nothing, deliberately: the suite that drives this builds without the
/// core or spdlog. SDL_PushEvent fails only with the events subsystem down.
template <class Emu>
bool on_press(Emu& emu, SDL_Scancode sc, const jnext::QuickChordTracker& chords)
{
    if (sc != SDL_SCANCODE_F1 && sc != SDL_SCANCODE_F4) return false;
    // GH #403: the tracker knows which modifier is the host's (Alt; GUI on macOS).
    if (!chords.host_modifier_held()) {
        if (sc == SDL_SCANCODE_F1) emu.on_hotkey_f1_hard_reset();
        else                       emu.on_hotkey_f4_soft_reset();
    } else if (sc == SDL_SCANCODE_F4) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        SDL_PushEvent(&quit);
    }
    return true;
}

}  // namespace sdl_reset_keys
