#include "sdl_input.h"

bool SdlInput::poll() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            if (on_quit) on_quit();
            return false;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (on_key) on_key(e.key.keysym.scancode, e.type == SDL_KEYDOWN);
            break;
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
        case SDL_MOUSEWHEEL:
            // Host adapter for Kempston mouse — forward to MouseDispatcher
            // via the on_mouse callback (set by SdlApp). Closes G43.
            if (on_mouse) on_mouse(e);
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERBUTTONUP:
        case SDL_CONTROLLERAXISMOTION:
        case SDL_CONTROLLERDEVICEADDED:
        case SDL_CONTROLLERDEVICEREMOVED:
        // Task 83 — the raw SDL_JOY* family must be forwarded too. Devices
        // with no SDL game-controller mapping emit ONLY these, so without
        // them a raw pad is opened and logged but never produces input; and
        // since GamepadHost now opens devices on JOYDEVICEADDED (one path for
        // mapped and unmapped alike), dropping them here would also break
        // hot-plug for ordinary mapped controllers. The Qt frontend pumps
        // SDL_PollEvent itself and gets all of this for free — this switch is
        // the SDL-only frontend's equivalent and must stay in step with it.
        case SDL_JOYDEVICEADDED:
        case SDL_JOYDEVICEREMOVED:
        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
        case SDL_JOYAXISMOTION:
        case SDL_JOYHATMOTION:
            // Host adapter for joystick / gamepad — forward to
            // JoystickDispatcher via the on_controller callback (set by
            // SdlApp). Closes G42 (JOY-WIRE-02/03/04).
            if (on_controller) on_controller(e);
            break;
        default:
            break;
        }
    }
    return true;
}

bool SdlInput::is_key_down(SDL_Scancode sc) const {
    const uint8_t* state = SDL_GetKeyboardState(nullptr);
    return state && state[sc];
}
