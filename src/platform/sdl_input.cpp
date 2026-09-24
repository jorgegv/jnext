#include "sdl_input.h"

bool SdlInput::poll() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_EVENT_QUIT:
            if (on_quit) on_quit();
            return false;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (on_key) on_key(e.key.scancode, e.type == SDL_EVENT_KEY_DOWN);
            break;
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
        case SDL_EVENT_MOUSE_WHEEL:
            // Host adapter for Kempston mouse — forward to MouseDispatcher
            // via the on_mouse callback (set by SdlApp). Closes G43.
            if (on_mouse) on_mouse(e);
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
        case SDL_EVENT_GAMEPAD_ADDED:
        case SDL_EVENT_GAMEPAD_REMOVED:
        // Task 83 — the raw SDL_JOY* family must be forwarded too. Devices
        // with no SDL game-controller mapping emit ONLY these, so without
        // them a raw pad is opened and logged but never produces input; and
        // since GamepadHost now opens devices on JOYDEVICEADDED (one path for
        // mapped and unmapped alike), dropping them here would also break
        // hot-plug for ordinary mapped controllers. The Qt frontend pumps
        // SDL_PollEvent itself and gets all of this for free — this switch is
        // the SDL-only frontend's equivalent and must stay in step with it.
        case SDL_EVENT_JOYSTICK_ADDED:
        case SDL_EVENT_JOYSTICK_REMOVED:
        case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
        case SDL_EVENT_JOYSTICK_BUTTON_UP:
        case SDL_EVENT_JOYSTICK_AXIS_MOTION:
        case SDL_EVENT_JOYSTICK_HAT_MOTION:
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
    // SDL3 returns `const bool*` here; SDL2 returned `const Uint8*`.
    const bool* state = SDL_GetKeyboardState(nullptr);
    return state && state[sc];
}
