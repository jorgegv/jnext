#include "platform/startup_error_sdl.h"

#include "core/log.h"

#ifndef __EMSCRIPTEN__
#include <SDL3/SDL.h>
#include <cstdlib>
#include <cstring>
#endif

namespace startup_report {

void show_error_sdl(const std::string& title, const std::string& text)
{
#ifdef __EMSCRIPTEN__
    (void)title; (void)text;
#else
    const char* drv = std::getenv("SDL_VIDEO_DRIVER");
    if (!drv) drv = std::getenv("SDL_VIDEODRIVER");   // the SDL 2 spelling, which sdl2-compat honours
    if (drv && (std::strcmp(drv, "offscreen") == 0 || std::strcmp(drv, "dummy") == 0)) {
        Log::platform()->debug("dialog not shown on the offscreen platform: {}: {}", title, text);
        return;
    }
    if (!SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, title.c_str(), text.c_str(), nullptr))
        Log::platform()->debug("the start-up error box could not be shown: {}", SDL_GetError());
#endif
}

}  // namespace startup_report
