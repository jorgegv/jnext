// SdlApp::tick() test (GH #462).
//
// No VHDL oracle: this is the host frame loop, not emulated hardware. The
// oracle is the pre-refactor SdlApp::run(), measured on a real SdlApp with the
// dummy SDL drivers, 48K, --silent: `set_delayed_exit(N)` ran N+1 iterations
// (N=0: 1 frame / 20 ms, N=1: 2 / 40 ms, N=3: 4 / 81 ms, N=5: 6 / 124 ms), every
// iteration slept to the 20 ms frame period (the one where the exit fires
// included), a pushed SDL_EVENT_QUIT ran 0 frames, and a hard reset cold-booted
// without sleeping or counting down (exit 1 after a reset ran 2 more frames).
// tick() is one such iteration; it reports the sleep instead of doing it.

#include "platform/sdl_app.h"
#include "../test_portable.h"
#include "../row_id.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

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

using SteadyClock = std::chrono::steady_clock;

double ms_since(SteadyClock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(SteadyClock::now() - t0).count();
}

// A real SdlApp on the dummy SDL drivers: 48K, silent, no SD image.
std::unique_ptr<SdlApp> make_app(bool& ok)
{
    jtp::set_env("SDL_VIDEODRIVER", "dummy");
    jtp::set_env("SDL_AUDIODRIVER", "dummy");
    auto app = std::make_unique<SdlApp>();   // heap: Emulator is large (1 MB Windows stack)
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.silent = true;
    app->set_config(cfg);
    ok = app->init(0, nullptr);
    return app;
}

long frame_ms_of(SdlApp& app)
{
    return std::lround(app.emulator().frame_period_ms());
}

}  // namespace

int main()
{
    std::printf("SdlApp::tick() test (GH #462)\n");
    std::printf("====================================================\n");

    // --- SDLT-01: one tick runs exactly one frame ----------------------------
    {
        bool ok = false;
        auto app = make_app(ok);
        const long frame_ms = frame_ms_of(*app);
        const uint32_t f0 = app->emulator().frame_num();
        uint32_t s = 12345;
        const bool more = app->tick(s);
        const uint32_t frames = app->emulator().frame_num() - f0;
        app->shutdown();
        check("SDLT-01", "no exit armed: one tick runs one frame and returns true; "
                         "the frame period is 20 ms",
              ok && more && frames == 1 && frame_ms == 20,
              "ok=" + std::to_string(ok) + " more=" + std::to_string(more) +
              " frames=" + std::to_string(frames) + " frame_ms=" + std::to_string(frame_ms));
    }

    // --- SDLT-02: tick() reports the sleep, it does not perform it ------------
    {
        bool ok = false;
        auto app = make_app(ok);
        const long frame_ms = frame_ms_of(*app);
        bool bounded = true;
        int sleeping = 0, unslept = 0;
        for (int i = 0; i < 10; ++i) {
            uint32_t s = 12345;
            const auto t0 = SteadyClock::now();
            const bool more = app->tick(s);
            const double w = ms_since(t0);
            if (!more || s > static_cast<uint32_t>(frame_ms)) bounded = false;
            if (s > 0) {
                ++sleeping;
                // A tick that slept inside lasts the whole frame period; one that
                // only reports lasts frame_ms - s. A loaded host can stretch any
                // single tick, so the bound is asked of most of them, not each.
                if (w <= static_cast<double>(frame_ms) - s + 5.0) ++unslept;
            }
        }
        app->shutdown();
        check("SDLT-02", "10 ticks: each sleep <= the frame period, at least one > 0, "
                         "and most ticks that report a sleep did not also perform it",
              ok && bounded && sleeping > 0 && 2 * unslept > sleeping,
              "sleeping=" + std::to_string(sleeping) + " unslept=" + std::to_string(unslept));
    }

    // --- SDLT-03: an automatic exit ends the loop on the right tick -----------
    {
        bool ok = false;
        auto app = make_app(ok);
        app->set_delayed_exit(3);
        const uint32_t f0 = app->emulator().frame_num();
        bool r[4];
        for (bool& x : r) { uint32_t s = 12345; x = app->tick(s); }
        const uint32_t frames = app->emulator().frame_num() - f0;
        app->shutdown();
        check("SDLT-03", "exit 3: the first three ticks go on, the fourth ends it; 4 frames in total",
              ok && r[0] && r[1] && r[2] && !r[3] && frames == 4,
              "frames=" + std::to_string(frames));
    }

    // --- SDLT-04: exit 0 ends on the first tick -------------------------------
    {
        bool ok = false;
        auto app = make_app(ok);
        const long frame_ms = frame_ms_of(*app);
        app->set_delayed_exit(0);
        const uint32_t f0 = app->emulator().frame_num();
        uint32_t s = 12345;
        const bool more = app->tick(s);
        const uint32_t frames = app->emulator().frame_num() - f0;
        app->shutdown();
        check("SDLT-04", "exit 0: the first tick returns false after 1 frame, sleep <= frame period",
              ok && !more && frames == 1 && s <= static_cast<uint32_t>(frame_ms),
              "frames=" + std::to_string(frames) + " s=" + std::to_string(s));
    }

    // --- SDLT-05: run() is the loop of tick() --------------------------------
    {
        bool ok = false;
        auto app = make_app(ok);
        const long frame_ms = frame_ms_of(*app);
        app->set_delayed_exit(3);
        const uint32_t f0 = app->emulator().frame_num();
        const auto t0 = SteadyClock::now();
        app->run();
        const double w = ms_since(t0);
        const uint32_t frames = app->emulator().frame_num() - f0;
        const int code = app->exit_code();
        app->shutdown();
        check("SDLT-05", "run() with exit 3: init ok, 4 frames, exit code 0, every iteration "
                         "sleeps to the frame period (the last included)",
              ok && frames == 4 && code == 0 && w >= 4.0 * frame_ms - 4.0,
              "ok=" + std::to_string(ok) + " frames=" + std::to_string(frames) +
              " code=" + std::to_string(code) + " wall=" + std::to_string(w));
    }

    // --- SDLT-06: a quit event ends the loop before any frame -----------------
    {
        bool ok = false;
        auto app = make_app(ok);
        SDL_Event ev;
        SDL_zero(ev);
        ev.type = SDL_EVENT_QUIT;
        const bool pushed = SDL_PushEvent(&ev);
        const uint32_t f0 = app->emulator().frame_num();
        uint32_t s = 12345;
        const bool more = app->tick(s);
        const uint32_t frames = app->emulator().frame_num() - f0;
        app->shutdown();
        check("SDLT-06", "SDL_EVENT_QUIT pending: tick returns false, 0 frames, sleep written as 0",
              ok && pushed && !more && frames == 0 && s == 0,
              "pushed=" + std::to_string(pushed) + " frames=" + std::to_string(frames) +
              " s=" + std::to_string(s));
    }

    // --- SDLT-07: a hard reset cold-boots without sleeping or counting down ---
    {
        bool ok = false;
        auto app = make_app(ok);
        app->set_delayed_exit(1);
        app->emulator().request_hard_reset();
        uint32_t s = 12345;
        const bool t1 = app->tick(s);
        const uint32_t s1 = s;
        const uint32_t fn1 = app->emulator().frame_num();
        s = 12345;
        const bool t2 = app->tick(s);
        s = 12345;
        const bool t3 = app->tick(s);
        const uint32_t fn3 = app->emulator().frame_num();
        app->shutdown();
        check("SDLT-07", "exit 1 + hard reset: tick 1 cold-boots (true, sleep 0, frame 0), "
                         "then true, then false; 2 frames after the reset",
              ok && t1 && s1 == 0 && fn1 == 0 && t2 && !t3 && fn3 == 2,
              "t=" + std::to_string(t1) + std::to_string(t2) + std::to_string(t3) +
              " s1=" + std::to_string(s1) + " fn1=" + std::to_string(fn1) +
              " fn3=" + std::to_string(fn3));
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
