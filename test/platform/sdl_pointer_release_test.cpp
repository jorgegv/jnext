// SDL pointer-release route test (GitHub issue #365).
//
// No VHDL oracle: a HOST input route. The oracle is the design table of the
// issue plan: three exclusive release methods (ctrl-alt default, not consumed;
// middle-click and esc consumed whole); uncaptured, a click captures and Esc is
// BREAK.
//
// Like host_key_latch_test's HKW-04 this drives the REAL SdlInput::poll() with
// REAL SDL events pushed onto SDL's own queue, and the REAL
// sdl_pointer_release::route_sdl_mouse / route_sdl_release_key that SdlApp
// calls (src/platform/sdl_pointer_release.h). Only the events subsystem is
// needed — no window, no video driver. `pushed` is part of every row's
// condition: a push that silently failed would read exactly like a missing
// route.
//
// Not covered here (declared): the one-line SdlApp::init composition that wraps
// input_.on_keyboard_lost to call ReleaseFilter::reset(). Its failure mode is
// one later uncaptured Esc being eaten.
//
// Run: ./build/test/sdl_pointer_release_test

#include "platform/sdl_input.h"
#include "platform/sdl_pointer_release.h"

#include <cstdio>
#include <string>
#include "../row_id.h"

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    report_row_id(id);
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

struct FakeDispatcher {
    int events = 0;
    void handle_sdl_event(const SDL_Event&) { ++events; }
};

// The frontend's wiring, with the real route functions and real SdlInput.
struct Rig {
    SdlInput input;
    pointer_capture::ReleaseFilter filter;
    FakeDispatcher disp;
    bool captured = true;
    int sink_keys = 0;            // key events that reached "hotkeys / Router"
    SDL_Keymod mods = SDL_KMOD_NONE;

    explicit Rig(pointer_capture::ReleaseMethod m)
    {
        filter.set_method(m);
        auto set_captured = [this](bool on) { captured = on; };
        input.on_key = [this, set_captured](SDL_Scancode sc, bool pressed) {
            if (sdl_pointer_release::route_sdl_release_key(filter, sc, pressed, mods,
                                                           captured, set_captured))
                return;
            ++sink_keys;
        };
        input.on_mouse = [this, set_captured](const SDL_Event& e) {
            sdl_pointer_release::route_sdl_mouse(e, filter, captured, set_captured, disp);
        };
    }

    static bool push_key(SDL_Scancode sc, bool down)
    {
        SDL_Event ev{};
        ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        ev.key.scancode = sc;
        return SDL_PushEvent(&ev);
    }
    static bool push_button(Uint8 b, bool down)
    {
        SDL_Event ev{};
        ev.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        ev.button.button = b;
        return SDL_PushEvent(&ev);
    }
};

std::string state(const Rig& r)
{
    return "captured=" + std::to_string((int)r.captured) + " sink=" + std::to_string(r.sink_keys) +
           " disp=" + std::to_string(r.disp.events);
}

}  // namespace

int main()
{
    using RM = pointer_capture::ReleaseMethod;
    if (!SDL_Init(SDL_INIT_EVENTS)) {
        std::printf("SDL_Init(EVENTS) failed: %s\n", SDL_GetError());
        return 1;
    }

    // MRS-01: Esc releases, and neither its press nor its release is a key.
    {
        Rig r(RM::Esc);
        const bool p = Rig::push_key(SDL_SCANCODE_ESCAPE, true) && Rig::push_key(SDL_SCANCODE_ESCAPE, false);
        r.input.poll();
        check("MRS-01", "Esc method: a captured Esc press+release releases and reaches no key sink",
              p && !r.captured && r.sink_keys == 0, state(r));
    }
    // MRS-02: an SDL repeat (a further press) is swallowed too; later Esc is BREAK.
    {
        Rig r(RM::Esc);
        const bool p = Rig::push_key(SDL_SCANCODE_ESCAPE, true) && Rig::push_key(SDL_SCANCODE_ESCAPE, true) &&
                       Rig::push_key(SDL_SCANCODE_ESCAPE, false);
        r.input.poll();
        const int after_first = r.sink_keys;
        const bool p2 = Rig::push_key(SDL_SCANCODE_ESCAPE, true) && Rig::push_key(SDL_SCANCODE_ESCAPE, false);
        r.input.poll();
        check("MRS-02", "Esc method: press, repeat, release give the sink 0; a later uncaptured Esc pair gives 2",
              p && p2 && !r.captured && after_first == 0 && r.sink_keys == 2, state(r));
    }
    // MRS-03: a middle press releases and is not forwarded.
    {
        Rig r(RM::MiddleClick);
        const bool p = Rig::push_button(SDL_BUTTON_MIDDLE, true) && Rig::push_button(SDL_BUTTON_MIDDLE, false);
        r.input.poll();
        check("MRS-03", "MiddleClick: a captured middle press/release releases, dispatcher sees 0",
              p && !r.captured && r.disp.events == 0, state(r));
    }
    // MRS-04: left stays a Kempston button.
    {
        Rig r(RM::MiddleClick);
        const bool p = Rig::push_button(SDL_BUTTON_LEFT, true) && Rig::push_button(SDL_BUTTON_LEFT, false);
        r.input.poll();
        check("MRS-04", "MiddleClick: captured left press/release reach the dispatcher, still captured",
              p && r.captured && r.disp.events == 2, state(r));
    }
    // MRS-05: uncaptured, a middle press captures.
    {
        Rig r(RM::MiddleClick);
        r.captured = false;
        const bool p = Rig::push_button(SDL_BUTTON_MIDDLE, true);
        r.input.poll();
        check("MRS-05", "MiddleClick: an uncaptured middle press captures, dispatcher sees 0",
              p && r.captured && r.disp.events == 0, state(r));
    }
    // MRS-06: the default chord releases and the Alt key is NOT consumed.
    {
        Rig r(RM::CtrlAlt);
        r.mods = static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_LALT);
        const bool p = Rig::push_key(SDL_SCANCODE_LALT, true);
        r.input.poll();
        check("MRS-06", "CtrlAlt: LALT down with Ctrl+Alt in the modstate releases and still reaches the sink",
              p && !r.captured && r.sink_keys == 1, state(r));
    }
    // MRS-07: under the default the middle button stays a Kempston button.
    {
        Rig r(RM::CtrlAlt);
        const bool p = Rig::push_button(SDL_BUTTON_MIDDLE, true) && Rig::push_button(SDL_BUTTON_MIDDLE, false);
        r.input.poll();
        check("MRS-07", "CtrlAlt: a captured middle press/release reach the dispatcher, still captured",
              p && r.captured && r.disp.events == 2, state(r));
    }

    SDL_Quit();
    std::printf("\n==============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
