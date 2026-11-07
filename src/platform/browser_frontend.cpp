#include "platform/browser_frontend.h"
#include "platform/browser_pacing.h"
#include "platform/sdl_app.h"
#include "core/emulator.h"
#include "core/log.h"
#include "core/warm_start_cache.h"

#include <SDL3/SDL.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <string>

namespace {

struct Context {
    SdlApp*               app = nullptr;
    std::function<int()>  finish;
    browser_pacing::Gate  gate;
    bool                  active = false;
};
Context g_ctx;

double now_us() { return emscripten_get_now() * 1000.0; }

void end_run()
{
    emscripten_cancel_main_loop();
    g_ctx.active = false;
    const int code = g_ctx.finish ? g_ctx.finish() : 0;
    emscripten_force_exit(code);
}

void frame_cb(void*)
{
    if (!g_ctx.app) return;
    SdlApp& app = *g_ctx.app;
    const auto r = browser_pacing::run_callback(
        g_ctx.gate,
        [] { return static_cast<int64_t>(now_us()); },
        [&app](uint32_t& s) { return app.tick(s); },
        [&app] { return app.emulator().fastload_active(); });
    if (r == browser_pacing::Result::End) end_run();
}

// The canvas lost focus to another element of the same page: SDL only watches
// the WINDOW, so it sends nothing and a held key would stick. Send the same
// pair SDL emits on a window blur: the key-ups (through the normal poll, so the
// Router and the latch release them) and FOCUS_LOST (on_keyboard_lost).
EM_BOOL canvas_blur(int, const EmscriptenFocusEvent*, void*)
{
    SDL_ResetKeyboard();
    int n = 0;
    SDL_Window** wins = SDL_GetWindows(&n);
    if (wins && n > 0) {
        SDL_Event e{};
        e.type = SDL_EVENT_WINDOW_FOCUS_LOST;
        e.window.windowID = SDL_GetWindowID(wins[0]);
        SDL_PushEvent(&e);
    }
    SDL_free(wins);
    return EM_FALSE;
}

}  // namespace

// A lock the browser ended itself (Escape) must release jnext's capture, as on
// the desktop. addEventListener does not displace SDL's own callback.
extern "C" EMSCRIPTEN_KEEPALIVE void jnext_web_pointer_lock_changed(int locked)
{
    if (!locked && g_ctx.app) g_ctx.app->release_mouse_capture();
}

EM_JS(void, jnext_web_listen_pointer_lock, (const char* selector), {
    const sel = UTF8ToString(selector);
    document.addEventListener('pointerlockchange', () => {
        const el = document.querySelector(sel);
        if (document.pointerLockElement !== el) _jnext_web_pointer_lock_changed(0);
    });
});

// 1 while the page's loop runs (callMain returned but the run has not ended).
extern "C" EMSCRIPTEN_KEEPALIVE int jnext_web_loop_active()
{
    return g_ctx.active ? 1 : 0;
}

extern "C" EMSCRIPTEN_KEEPALIVE double jnext_web_frame()
{
    return g_ctx.app ? static_cast<double>(g_ctx.app->emulator().frame_num()) : 0.0;
}

// Make a warm-start recording durable: the page mounts IndexedDB (IDBFS) on the
// warm-start directory only, and store() calls this once after writing a file.
// With no IDBFS mounted (node, a page that did not ask) syncfs has nothing to
// flush and calls back at once.
EM_JS(void, jnext_web_persist_warm_start, (), {
    if (typeof FS !== 'undefined' && FS.syncfs) {
        FS.syncfs(false, (e) => { if (e) console.warn('jnext: warm-start state not persisted: ' + e); });
    }
});

namespace browser_frontend {

bool active() { return g_ctx.active; }

void prepare()
{
    warm_start::set_persist_hook(jnext_web_persist_warm_start);
    // SDL's keyboard default is #window: it would capture every key of the
    // page. Follow the canvas selector instead (a page ENV value wins).
    const char* sel = SDL_GetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR);
    SDL_SetHint(SDL_HINT_EMSCRIPTEN_KEYBOARD_ELEMENT, sel ? sel : "#canvas");
}

void run(SdlApp& app, std::function<int()> finish)
{
    g_ctx.app = &app;
    g_ctx.finish = std::move(finish);
    g_ctx.gate = browser_pacing::Gate{};
    g_ctx.active = true;
    const char* sel = SDL_GetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR);
    jnext_web_listen_pointer_lock(sel ? sel : "#canvas");
    emscripten_set_blur_callback(sel ? sel : "#canvas", nullptr, EM_FALSE, canvas_blur);
    // fps 0 = requestAnimationFrame; simulate_infinite_loop 0 = main() returns.
    emscripten_set_main_loop_arg(frame_cb, nullptr, 0, 0);
}

}  // namespace browser_frontend
