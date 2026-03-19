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
