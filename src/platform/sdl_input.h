#pragma once
#include <SDL2/SDL.h>
#include <functional>

class SdlInput {
public:
    // Returns false when SDL_QUIT received
    bool poll();

    bool is_key_down(SDL_Scancode sc) const;

    // Called for each KEYDOWN/KEYUP event: (scancode, pressed)
    std::function<void(SDL_Scancode, bool)> on_key;

    // Called on SDL_QUIT
    std::function<void()> on_quit;

    // Called for each SDL_MOUSEMOTION / SDL_MOUSEBUTTON{DOWN,UP} /
    // SDL_MOUSEWHEEL event with the raw SDL_Event. The host owner (SdlApp)
    // is expected to forward into a MouseDispatcher bound to the
    // emulator's KempstonMouse. See doc plan rows MOUSE-13/14/15 (G43).
    std::function<void(const SDL_Event&)> on_mouse;

    // Called for each SDL_CONTROLLERBUTTONDOWN/UP / SDL_CONTROLLERAXISMOTION
    // / SDL_CONTROLLERDEVICEADDED/REMOVED event with the raw SDL_Event. The
    // host owner (SdlApp) forwards into a JoystickDispatcher bound to the
    // emulator's Joystick. See doc plan rows JOY-WIRE-02/03/04 (G42).
    std::function<void(const SDL_Event&)> on_controller;
};
