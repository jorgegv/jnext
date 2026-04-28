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
