// ===========================================================================
// SDL window title suite (GitHub issue #155).
//
// No VHDL oracle: this is host UI, not emulated hardware. The oracle is the
// project's single source for the version — version.yaml, generated into
// JNEXT_VERSION_STRING, the string `--version` prints.
//
// The SDL frontend's window is created by SdlDisplay::init(), which titles it
// itself (SdlApp passes no title), so driving the REAL init() under SDL's
// `dummy` video driver reads the title the user's window gets. The SDL
// frontend sets no other title. The Qt frontend's half is window_title_test.
//
// "JNEXT" and "ZX Spectrum Next Emulator" stay in the title on purpose: the
// regression scripts find the SDL window with `xdotool search --name JNEXT`
// (sdl-keypress-func.sh, sdl-reset-hotkey-func.sh).
//
// Discriminative — mutation applied to the product, reverted from a file copy:
//   SdlDisplay::init back to the pre-#155 literal title -> WTS-02 fails
//   JNEXT_WINDOW_TITLE cut to "JNEXT <version>"         -> WTS-03 fails
// ===========================================================================

#include "platform/sdl_display.h"
#include "version.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond,
           const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s: %s", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
    }
    if (!detail.empty()) std::printf(" [%s]", detail.c_str());
    std::printf("\n");
}

} // namespace

int main()
{
    std::printf("Issue #155 - the SDL window title names the version\n");
    std::printf("=========================================================================\n\n");

    // No display needed: the dummy driver creates real SDL_Window objects.
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    const bool sdl_ok = SDL_Init(SDL_INIT_VIDEO);

    std::string title;
    bool init_ok = false;
    {
        SdlDisplay display;
        if (sdl_ok) {
            init_ok = display.init(640, 256, 512);
            if (init_ok && display.window()) {
                const char* t = SDL_GetWindowTitle(display.window());
                title = t ? t : "";
            }
        }

        // ── WTS-01 — the fixture's own witness ──────────────────────
        // Without a window the two rows below would read an empty title and
        // fail for the wrong reason; say so on a row of its own.
        check("WTS-01", "SdlDisplay::init() creates its window under the dummy video driver",
              sdl_ok && init_ok && display.window() != nullptr,
              sdl_ok ? (init_ok ? "" : std::string("init failed: ") + SDL_GetError())
                     : std::string("SDL_Init(VIDEO) failed: ") + SDL_GetError());
    }

    // Built from JNEXT_VERSION_STRING here, not from the constant the product
    // titles the window with — comparing with that would pass whatever it said.
    const std::string version_tag = std::string("JNEXT ") + JNEXT_VERSION_STRING;

    // ── WTS-02 — the title names the build ──────────────────────────
    check("WTS-02", "the SDL window title carries \"JNEXT <version>\" from version.yaml",
          title.rfind(version_tag, 0) == 0 && std::string(JNEXT_VERSION_STRING).size() > 0,
          "title='" + title + "' want prefix '" + version_tag + "'");

    // ── WTS-03 — the regression scripts can still find the window ───
    check("WTS-03", "and keeps \"ZX Spectrum Next Emulator\" after it",
          title.find("ZX Spectrum Next Emulator") != std::string::npos,
          "title='" + title + "'");

    if (sdl_ok) SDL_Quit();

    std::printf("\n=========================================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped:    0\n",
                g_total, g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
