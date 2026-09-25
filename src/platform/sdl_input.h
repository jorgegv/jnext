#pragma once
#include <SDL3/SDL.h>
#include <functional>
#include <utility>

class SdlInput {
public:
    // Returns false when SDL_EVENT_QUIT received
    bool poll();

    bool is_key_down(SDL_Scancode sc) const;

    // Called for each KEYDOWN/KEYUP event: (scancode, pressed)
    std::function<void(SDL_Scancode, bool)> on_key;

    // GH #268 — called when this window loses the keyboard
    // (SDL_EVENT_WINDOW_FOCUS_LOST). Every key-up it is still owed goes to
    // whatever took the keyboard, so the guest must let go of what it holds:
    // hold a key, alt-tab away, release it there, and without this the matrix
    // bit stays set and the ROM's own auto-repeat runs for the rest of the
    // session. The Qt frontend answers the equivalent focus-out the same way.
    std::function<void()> on_keyboard_lost;

    // Setters with the names wire_host_keys() expects, so ONE wiring function
    // serves both frontends and one mutation covers both. See
    // platform/host_key_wiring.h for why the wiring is a function at all.
    void set_key_callback(std::function<void(SDL_Scancode, bool)> cb) {
        on_key = std::move(cb);
    }
    void set_keyboard_lost_callback(std::function<void()> cb) {
        on_keyboard_lost = std::move(cb);
    }

    // Called on SDL_EVENT_QUIT
    std::function<void()> on_quit;

    // Called for each SDL_EVENT_MOUSE_MOTION / SDL_MOUSEBUTTON{DOWN,UP} /
    // SDL_EVENT_MOUSE_WHEEL event with the raw SDL_Event. The host owner (SdlApp)
    // is expected to forward into a MouseDispatcher bound to the
    // emulator's KempstonMouse. See doc plan rows MOUSE-13/14/15 (G43).
    std::function<void(const SDL_Event&)> on_mouse;

    // Called for each SDL_EVENT_GAMEPAD_BUTTON_DOWN/UP / SDL_EVENT_GAMEPAD_AXIS_MOTION
    // / SDL_EVENT_GAMEPAD_ADDED/REMOVED event with the raw SDL_Event. The
    // host owner (SdlApp) forwards into a JoystickDispatcher bound to the
    // emulator's Joystick. See doc plan rows JOY-WIRE-02/03/04 (G42).
    std::function<void(const SDL_Event&)> on_controller;
};
