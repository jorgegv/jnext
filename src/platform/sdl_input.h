#pragma once
#include <SDL3/SDL.h>
#include <functional>

class SdlInput {
public:
    // Returns false when SDL_EVENT_QUIT received
    bool poll();

    bool is_key_down(SDL_Scancode sc) const;

    // Called for each KEYDOWN/KEYUP event: (scancode, pressed)
    std::function<void(SDL_Scancode, bool)> on_key;

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
