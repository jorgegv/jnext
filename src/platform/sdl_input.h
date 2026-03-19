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
};
