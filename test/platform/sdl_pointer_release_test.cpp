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
// The key surface is driven through the REAL wire_release_host_keys template,
// the very call SdlApp::init makes, so the consumed-key return and the
// keyboard-loss chaining are pinned (MRS-01/02/08). Only the argument list
// SdlApp passes (its own getters) is outside any suite.
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

// SdlApp's key wiring, via the REAL wire_release_host_keys (the very template
// SdlApp::init calls), with a fake Router and a counting "hotkeys" stage.
struct FakeRouter {
    int keys = 0;          // key events that reached the Router
    int lost = 0;          // release_all() calls (GH #268)
    void on_host_key(SDL_Scancode, bool) { ++keys; }
    void release_all() { ++lost; }
};

struct Rig {
    SdlInput input;
    pointer_capture::ReleaseFilter filter;
    FakeDispatcher disp;
    FakeRouter router;
    bool captured = true;
    SDL_Keymod mods = SDL_KMOD_NONE;

    explicit Rig(pointer_capture::ReleaseMethod m)
    {
        filter.set_method(m);
        auto set_captured = [this](bool on) { captured = on; };
        sdl_pointer_release::wire_release_host_keys(
            input, router, filter,
            [this]() { return mods; },
            [this]() { return captured; },
            set_captured,
            [this](SDL_Scancode sc, bool pressed) { router.on_host_key(sc, pressed); });
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
    return "captured=" + std::to_string((int)r.captured) + " sink=" + std::to_string(r.router.keys) +
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
              p && !r.captured && r.router.keys == 0, state(r));
    }
    // MRS-02: an SDL repeat (a further press) is swallowed too; later Esc is BREAK.
    {
        Rig r(RM::Esc);
        const bool p = Rig::push_key(SDL_SCANCODE_ESCAPE, true) && Rig::push_key(SDL_SCANCODE_ESCAPE, true) &&
                       Rig::push_key(SDL_SCANCODE_ESCAPE, false);
        r.input.poll();
        const int after_first = r.router.keys;
        const bool p2 = Rig::push_key(SDL_SCANCODE_ESCAPE, true) && Rig::push_key(SDL_SCANCODE_ESCAPE, false);
        r.input.poll();
        check("MRS-02", "Esc method: press, repeat, release give the sink 0; a later uncaptured Esc pair gives 2",
              p && p2 && !r.captured && after_first == 0 && r.router.keys == 2, state(r));
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
              p && !r.captured && r.router.keys == 1, state(r));
    }
    // MRS-07: under the default the middle button stays a Kempston button.
    {
        Rig r(RM::CtrlAlt);
        const bool p = Rig::push_button(SDL_BUTTON_MIDDLE, true) && Rig::push_button(SDL_BUTTON_MIDDLE, false);
        r.input.poll();
        check("MRS-07", "CtrlAlt: a captured middle press/release reach the dispatcher, still captured",
              p && r.captured && r.disp.events == 2, state(r));
    }

    // MRS-08: a lost keyboard forgets a swallowed Esc AND still runs the callback
    // wire_host_keys bound (the Router's release_all, GH #268).
    {
        Rig r(RM::Esc);
        const bool p = Rig::push_key(SDL_SCANCODE_ESCAPE, true);   // releases; its key-up goes elsewhere
        SDL_Event ev{};
        ev.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        ev.window.windowID = 1;
        const bool pl = SDL_PushEvent(&ev);
        const bool p2 = Rig::push_key(SDL_SCANCODE_ESCAPE, true);  // uncaptured: BREAK again
        r.input.poll();
        check("MRS-08", "focus loss runs the Router's release_all and forgets the swallowed Esc",
              p && pl && p2 && r.router.lost == 1 && r.router.keys == 1 && !r.captured, state(r) +
              " lost=" + std::to_string(r.router.lost));
    }
    // MRS-09: the right-hand keys release under the default, not consumed.
    {
        Rig r(RM::CtrlAlt);
        r.mods = static_cast<SDL_Keymod>(SDL_KMOD_RCTRL | SDL_KMOD_RALT);
        const bool p = Rig::push_key(SDL_SCANCODE_RCTRL, true);
        r.input.poll();
        const bool released = !r.captured;
        const int kept = r.router.keys;
        r.captured = true;
        const bool p2 = Rig::push_key(SDL_SCANCODE_RALT, true);
        r.input.poll();
        check("MRS-09", "CtrlAlt: RCTRL down and RALT down with right-hand modstate release, both still reach the Router",
              p && p2 && released && kept == 1 && !r.captured && r.router.keys == 2, state(r));
    }
    // MRS-10: either side's modifier in the modstate counts (SDL_KMOD_CTRL / _ALT).
    {
        Rig r(RM::CtrlAlt);
        r.mods = static_cast<SDL_Keymod>(SDL_KMOD_LCTRL | SDL_KMOD_RALT);
        const bool p = Rig::push_key(SDL_SCANCODE_RALT, true);
        r.input.poll();
        check("MRS-10", "CtrlAlt: LCTRL held + RALT pressed (mixed sides) releases",
              p && !r.captured, state(r));
    }
    // MRS-11: under middle-click an uncaptured middle click captures and its
    // button-UP (now captured) does NOT release.
    {
        Rig r(RM::MiddleClick);
        r.captured = false;
        const bool p = Rig::push_button(SDL_BUTTON_MIDDLE, true) && Rig::push_button(SDL_BUTTON_MIDDLE, false);
        r.input.poll();
        check("MRS-11", "MiddleClick: a middle click on the free pointer captures and stays captured (UP forwarded)",
              p && r.captured && r.disp.events == 1, state(r));
    }

    SDL_Quit();
    std::printf("\n==============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
