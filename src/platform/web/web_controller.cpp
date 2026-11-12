// The browser page's controller (GH #465): what jnext_api.js (jnext.mount()) calls
// between two ticks of the frame loop. The page is single-threaded, so a call
// here never interleaves with SdlApp::tick(). Linked into the jnext_web target
// only (browser_frontend.cpp calls attach() and after_tick()).
#include "platform/web/web_controller.h"
#include "platform/browser_page.h"
#include "platform/web_api.h"
#include "platform/sdl_app.h"
#include "core/emulator.h"
#include "peripheral/sd_write_journal.h"

#include <SDL3/SDL.h>
#include <emscripten.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

SdlApp*     g_app = nullptr;
std::string g_program;          // the program a reset('program') restarts; empty = none
bool        g_paused = false;
bool        g_page_load = false;     // the load now pending was started by the page (startup, load(), reset('program'))
bool        g_want_frame = false;    // ready is pending: the program is in, a frame must follow
uint64_t    g_gen_mark = 0;
bool        g_load_failed = false;
std::string g_load_failed_file;
int         g_load_applied = 0;
double      g_gain_db = 0.0;

EM_JS(void, jnext_web_event, (const char* type, const char* text), {
    if (Module.jnextEvent) Module.jnextEvent(UTF8ToString(type), UTF8ToString(text));
});

void event(const char* type, const std::string& text = std::string())
{
    jnext_web_event(type, text.c_str());
}

// Arm "ready": a frame must be shown after this point.
void arm_ready()
{
    g_want_frame = true;
    g_gen_mark = g_app ? g_app->emulator().framebuffer_generation() : 0;
}

}  // namespace

namespace web_controller {

void attach(SdlApp& app)
{
    g_app = &app;
    g_program.clear();
    g_paused = false;
    app.set_on_load_applied([](const std::string& file, bool ok) {
        // A load the guest asked for (a NEX that runs another) is not the page's program:
        // it neither fires ready nor becomes what reset('program') restarts.
        const bool mine = g_page_load;
        g_page_load = false;
        if (!mine) return;
        if (ok) {
            g_program = file;
            ++g_load_applied;
            arm_ready();
        } else {
            g_load_failed = true;
            g_load_failed_file = file;
            g_want_frame = false;
        }
    });
    // No program: ready is the first frame shown.
    g_page_load = !app.pending_load_file().empty();
    if (!g_page_load) arm_ready();
    event("started");
}

void after_tick(SdlApp& app, bool /*more*/)
{
    // A page load still waiting (the .tzx/.wav delay, the warm-start recording) keeps
    // pending_load_file() non-empty. If it is empty while the hook never ran, a hard reset
    // dropped the load: say so, and stop treating the next load as the page's.
    if (g_page_load && app.pending_load_file().empty()) {
        g_page_load = false;
        g_want_frame = false;
        event("error", "load cancelled by a reset");
    }
    if (g_load_failed) {
        g_load_failed = false;
        event("error", "failed to load '" + g_load_failed_file + "'");
    }
    if (g_want_frame && app.emulator().framebuffer_generation() != g_gen_mark) {
        g_want_frame = false;
        event("ready");
    }
}

}  // namespace web_controller

// JSON options -> {"argv":[...]} or {"error":"..."}, malloc'd (JS frees it).
// Callable before callMain: it touches no emulator state.
extern "C" EMSCRIPTEN_KEEPALIVE char* jnext_web_argv(const char* json)
{
    std::vector<std::string> argv;
    std::string why;
    std::string out;
    auto quote = [](const std::string& s) {
        std::string q = "\"";
        for (unsigned char c : s) {
            if (c == '"' || c == '\\') { q += '\\'; q += static_cast<char>(c); }
            else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); q += b; }
            else q += static_cast<char>(c);
        }
        return q + "\"";
    };
    if (web_api::build_argv(json ? json : "", argv, why)) {
        out = "{\"argv\":[";
        for (size_t i = 0; i < argv.size(); ++i) out += (i ? "," : "") + quote(argv[i]);
        out += "]}";
    } else {
        out = "{\"error\":" + quote(why) + "}";
    }
    char* m = static_cast<char*>(std::malloc(out.size() + 1));
    if (m) std::memcpy(m, out.c_str(), out.size() + 1);
    return m;
}

extern "C" EMSCRIPTEN_KEEPALIVE void jnext_web_pause()
{
    if (!g_app || g_paused) return;
    g_paused = true;
    emscripten_pause_main_loop();
}

extern "C" EMSCRIPTEN_KEEPALIVE void jnext_web_resume()
{
    if (!g_app || !g_paused) return;
    g_paused = false;
    emscripten_resume_main_loop();
}

// 0 = program (cold boot and reload the current program; no program = hard),
// 1 = hard (F1), 2 = soft (F4). Returns 0 when serviced.
extern "C" EMSCRIPTEN_KEEPALIVE int jnext_web_reset(int kind)
{
    if (!g_app) return 1;
    if (kind == 1) {
        g_app->emulator().on_hotkey_f1_hard_reset();
    } else if (kind == 2) {
        g_app->emulator().on_hotkey_f4_soft_reset();
    } else if (!g_program.empty()) {
        g_page_load = true;
        g_app->cold_boot(g_program);
    } else {
        g_app->emulator().on_hotkey_f1_hard_reset();
        arm_ready();
    }
    return 0;
}

// resetCard(): discard the writes kept for the card (the persist hook deletes the
// stored record), then restart the program or hard-reset, as reset('program')
// does. 2 = the card has no write journal (keepWrites was not given).
extern "C" EMSCRIPTEN_KEEPALIVE int jnext_web_reset_card()
{
    if (!g_app) return 1;
    const auto journal = sd_write_journal::for_image(g_app->emulator().config().sd_card_image);
    if (!journal) return 2;
    journal->clear();
    return jnext_web_reset(0);
}

// nullptr = the load was started; else the reason it was refused. The program
// is in the machine when the next "ready" event arrives.
extern "C" EMSCRIPTEN_KEEPALIVE const char* jnext_web_load(const char* path)
{
    if (!g_app) return "the machine is not running";
    if (const char* why = browser_page::program_refusal(path ? path : "")) return why;
    // g_program becomes `path` only when the load succeeds (the hook), so a refused or
    // failed load never becomes the program a reset restarts.
    g_page_load = true;
    g_app->cold_boot(path);
    return nullptr;
}

extern "C" EMSCRIPTEN_KEEPALIVE void jnext_web_set_volume(double v)
{
    if (!g_app) return;
    const auto db = web_api::volume_to_gain_db(v);
    if (!db) return;
    g_gain_db = *db;
    g_app->emulator().mixer().set_output_gain_db(static_cast<float>(*db));
}

extern "C" EMSCRIPTEN_KEEPALIVE void jnext_web_quit()
{
    if (!g_app) return;
    SDL_Event e{};
    e.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&e);
    jnext_web_resume();   // a paused loop would never see it
}

extern "C" EMSCRIPTEN_KEEPALIVE int jnext_web_load_applied() { return g_load_applied; }

extern "C" EMSCRIPTEN_KEEPALIVE double jnext_web_gain_db()
{
    return g_app ? static_cast<double>(g_app->emulator().mixer().output_gain_db()) : g_gain_db;
}
