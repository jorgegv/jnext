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
#include "platform/host_modifier.h"
#include "platform/fast_boot.h"
#include "../test_portable.h"
#include "../row_id.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
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

// A clock and a sleep the suite drives (SdlApp::set_pacing_time_for_test). Each read of the clock costs `work_us`
// (the tick's own work, simulated); a sleep the app performs through the seam advances the clock by what it asked
// and is counted. Nothing here looks at the wall clock, so no row depends on how fast the host runs a tick.
struct FakeTime {
    int64_t t = 0;
    int64_t work_us = 4000;
    int sleeps = 0;
    int64_t slept_ms = 0;
    void attach(SdlApp& app)
    {
        app.set_pacing_time_for_test([this] { t += work_us; return t; },
                                     [this](uint32_t ms) { ++sleeps; slept_ms += ms; t += static_cast<int64_t>(ms) * 1000; });
    }
};

// A real SdlApp on the dummy SDL drivers: 48K, silent, no SD image.
std::unique_ptr<SdlApp> make_app(bool& ok, bool esxdos_stub = false)
{
    jtp::set_env("SDL_VIDEODRIVER", "dummy");
    jtp::set_env("SDL_AUDIODRIVER", "dummy");
    auto app = std::make_unique<SdlApp>();   // heap: Emulator is large (1 MB Windows stack)
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.silent = true;
    cfg.esxdos_stub = esxdos_stub;
    app->set_config(cfg);
    ok = app->init(0, nullptr);
    return app;
}

bool push_key(SDL_Scancode sc, bool down)
{
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    ev.key.scancode = sc;
    return SDL_PushEvent(&ev);
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

    // --- SDLT-02: tick() reports the sleep, it does not PERFORM it ------------
    // Time is driven by the row (FakeTime): every read of the pacing clock costs 4 ms (the tick's own work, e), and
    // the row performs each reported sleep itself afterwards, as SdlApp::run() does, because the sleep is the
    // distance to an absolute fractional deadline (frame_deadline.h): sleeps nobody performs pile up (200, 395, 591
    // ms ...) instead of each being "the rest of this period". At --speed 10 % (period 199.68 ms) a tick that only
    // reports has w = e = 4 ms and s = 200 - e + (n-1) x 0.32 ms ~ the period; one that also sleeps (through the
    // seam, or at all: SDL_Delay is poisoned in sdl_app.cpp) has w >= 100 ms and a counted sleep. A GOOD tick has
    // s >= 50, w < 175 ms and w + s <= 205 ms (the sleep it reports is the rest of the period after the time it
    // took); 3 of the 5 ticks must be good and the seam must not have been called by any tick. The bound on the
    // sleep (s <= the period) and the return value are checked on every tick. (GH #468: this row used to time the
    // tick on the wall clock and failed on a CI runner running every suite at once; it was changed twice for the
    // sleeps it is told, then for a driven clock. The checks and thresholds are the same.)
    {
        bool ok = false;
        auto app = make_app(ok);
        FakeTime ft;
        ft.attach(*app);
        app->set_speed_percent(10);
        const long frame_ms = std::lround(app->emulator().frame_period_ms() / 0.1);
        bool bounded = true;
        int good = 0;
        std::string detail = "frame_ms=" + std::to_string(frame_ms);
        for (int ticks = 1; ticks <= 5; ++ticks) {
            uint32_t s = 12345;
            const int64_t t0 = ft.t;
            const bool more = app->tick(s);
            const double w = static_cast<double>(ft.t - t0) / 1000.0;
            if (!more || s > static_cast<uint32_t>(frame_ms)) bounded = false;
            if (s >= 50 && w < static_cast<double>(frame_ms) - 25.0 &&
                w + s <= static_cast<double>(frame_ms) + 5.0) ++good;
            detail += " w/s=" + std::to_string(static_cast<int>(w)) + "/" + std::to_string(s);
            if (more && s > 0) ft.t += static_cast<int64_t>(s) * 1000;   // what SdlApp::run() does with it
        }
        const int in_tick_sleeps = ft.sleeps;
        app->shutdown();
        detail += " good=" + std::to_string(good) + " sleeps_in_ticks=" + std::to_string(in_tick_sleeps);
        check("SDLT-02", "speed 10%: each sleep <= the frame period; 3 of 5 ticks report a sleep "
                         ">= 50 ms, return well before it and have w + s ~ the period, "
                         "without having performed it (no sleep through the seam)",
              ok && frame_ms == 200 && bounded && good >= 3 && in_tick_sleeps == 0, detail);
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

    // --- SDLT-08: a NEX .RUN request cold-boots and returns early -------------
    // Same shape as SDLT-07 (the hard-reset sibling): the request is raised by
    // the esxdos stub's M_EXECCMD ".RUN x.nex" (the only producer of
    // take_nex_load_request()), the iteration that services it skips the pacing
    // sleep and the exit countdown, so exit 1 still runs two more frames.
    {
        bool ok = false;
        auto app = make_app(ok, /*esxdos_stub=*/true);
        app->set_delayed_exit(1);
        const char cmd[] = "run tick.nex";
        for (size_t i = 0; i < sizeof cmd; ++i)
            app->emulator().mmu().write(static_cast<uint16_t>(0x8000 + i),
                                        static_cast<uint8_t>(cmd[i]));
        Z80Registers regs{};
        regs.IX = 0x8000;
        const bool raised = app->emulator().cpu().on_esxdos_call &&
                            app->emulator().cpu().on_esxdos_call(0x8F, regs) &&
                            (regs.AF & 1) == 0;
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
        check("SDLT-08", "NEX .RUN request + exit 1: tick 1 cold-boots (true, sleep 0, frame 0), "
                         "then true, then false; 2 frames after the boot",
              ok && raised && t1 && s1 == 0 && fn1 == 0 && t2 && !t3 && fn3 == 2,
              "raised=" + std::to_string(raised) + " t=" + std::to_string(t1) +
              std::to_string(t2) + std::to_string(t3) + " s1=" + std::to_string(s1) +
              " fn1=" + std::to_string(fn1) + " fn3=" + std::to_string(fn3));
    }

    // --- SDLT-09: a Quick Load cold-boots and returns early -------------------
    // Host-modifier+Shift+F5 (save) in one tick, then +F9 (load) with the exit
    // armed after the save: the load tick cold-boots through service_quick_
    // requests() and returns early (true, sleep 0, countdown untouched), so
    // exit 1 still runs two more frames. The snapshots go to a private
    // $JNEXT_CONFIG_DIR, never the developer's.
    {
        namespace fs = std::filesystem;
        const fs::path cfgdir = fs::temp_directory_path() /
                                ("jnext-sdlt09-" + jtp::process_id_string());
        std::error_code ec;
        fs::remove_all(cfgdir, ec);
        fs::create_directories(cfgdir, ec);
        jtp::set_env("JNEXT_CONFIG_DIR", cfgdir.string().c_str());
        bool ok = false;
        auto app = make_app(ok);
        // The frontend's host modifier: Alt, or Command on macOS (GH #403).
        const SDL_Scancode host_mod =
            host_modifier::kIsGui ? SDL_SCANCODE_LGUI : SDL_SCANCODE_LALT;
        bool pushed = push_key(host_mod, true) && push_key(SDL_SCANCODE_LSHIFT, true) &&
                      push_key(SDL_SCANCODE_F5, true) && push_key(SDL_SCANCODE_F5, false);
        uint32_t s = 12345;
        app->tick(s);                                   // the save
        std::error_code ec2;
        int snapshots = 0;
        for (const auto& e : fs::directory_iterator(cfgdir / "snapshots", ec2)) { (void)e; ++snapshots; }
        app->set_delayed_exit(1);
        pushed = pushed && push_key(SDL_SCANCODE_F9, true) && push_key(SDL_SCANCODE_F9, false);
        s = 12345;
        const bool t1 = app->tick(s);                   // the load
        const uint32_t s1 = s;
        const uint32_t fn1 = app->emulator().frame_num();
        s = 12345;
        const bool t2 = app->tick(s);
        s = 12345;
        const bool t3 = app->tick(s);
        app->shutdown();
        jtp::unset_env("JNEXT_CONFIG_DIR");
        fs::remove_all(cfgdir, ec);
        check("SDLT-09", "Quick Load + exit 1: the load tick cold-boots (true, sleep 0, "
                         "countdown untouched), then true, then false",
              ok && pushed && snapshots == 1 && t1 && s1 == 0 && t2 && !t3,
              "pushed=" + std::to_string(pushed) + " snapshots=" + std::to_string(snapshots) +
              " t=" + std::to_string(t1) + std::to_string(t2) + std::to_string(t3) +
              " s1=" + std::to_string(s1) + " fn1=" + std::to_string(fn1));
    }

    // --- SDLT-10: the frame period the loop paces on is exact, not whole milliseconds ---------------------
    // Oracle: the 48K machine's frame is 448 x 312 x 4 master cycles at 28 MHz = 19.968 ms (zxula_timing.vhd:262,270),
    // scaled by --speed. The loop used lround() of it: 20 ms, 0.16% off at 48K and 1.3% at Next 50 Hz.
    {
        bool ok = false;
        auto app = make_app(ok);
        const int64_t p100 = app->effective_frame_period_us();
        app->set_speed_percent(200);
        const int64_t p200 = app->effective_frame_period_us();
        app->set_speed_percent(10);
        const int64_t p10 = app->effective_frame_period_us();
        app->shutdown();
        check("SDLT-10", "effective_frame_period_us: 19968 us at 100%, 9984 at 200%, 199680 at 10% (the exact period, not 20/10/200 ms)",
              ok && p100 == 19968 && p200 == 9984 && p10 == 199680,
              "p100=" + std::to_string(p100) + " p200=" + std::to_string(p200) + " p10=" + std::to_string(p10));
    }

    // --- SDLT-11: the reported sleeps follow the fractional grid ----------------------------------------
    // The sleep is the distance from the end of the tick to an absolute deadline that advances by the exact period.
    // Time is driven by the row (FakeTime: every clock read costs 4 ms of simulated work) and nobody sleeps, so the
    // deadline of tick n is the first tick's end + (n+1) x period whatever the work costs: s_n + t_n = (n+1) x
    // 99.84 ms (t_n: simulated time since the first tick's end), to within the whole-ms rounding of the sleep (1 ms).
    // At --speed 20 % the period is 99.84 ms; the old whole-ms rule reported frame_ms - elapsed (at most 100 ms) on
    // every tick, so s_n + t_n stayed near the work done, not near (n+1) x 99.8. No wall-clock threshold, no retry:
    // the grid is checked on a clock the row owns (GH #468: the wall-clock form failed on a CI runner whose ticks
    // took 160-424 ms, past the 2-period stall limit).
    {
        bool ok = false;
        auto app = make_app(ok);
        FakeTime ft;
        ft.attach(*app);
        app->set_speed_percent(20);
        uint32_t s = 0;
        app->tick(s);
        const int64_t t0 = ft.t;
        std::string detail;
        bool on_grid = true;
        for (int n = 1; n <= 20; ++n) {
            app->tick(s);
            const double t = static_cast<double>(ft.t - t0) / 1000.0;
            const double err = static_cast<double>(s) + t - (n + 1) * 99.84;
            if (n == 5 || n == 10 || n == 20) detail += " n=" + std::to_string(n) + " s+t-(n+1)*period=" + std::to_string(err);
            if (err < -1.0 || err > 1.0) on_grid = false;
        }
        app->shutdown();
        check("SDLT-11", "speed 20%, no sleep performed, a driven clock: after tick n the reported sleep plus the elapsed time is (n+1) x 99.84 ms within 1 ms (an absolute fractional deadline)",
              ok && on_grid, detail);
    }

    // --- SDLT-12: a fastload tick re-anchors the grid --------------------------------------------------
    // A fastload tick reports no sleep and forgets the deadline; the next paced tick starts a new grid (about one
    // period) instead of continuing the old one (whose next slot is two periods away) or sleeping the 1 ms minimum.
    // --speed 20 %: period 99.84 ms. Nobody sleeps, so a tick that kept the old deadline would report about 200 ms.
    {
        bool ok = false;
        auto app = make_app(ok);
        app->set_speed_percent(20);
        uint32_t s1 = 0, s2 = 12345, s3 = 0;
        app->tick(s1);                                              // paced: anchors, about 100 ms
        app->emulator().phantom_typist().arm(MachineType::ZX48K);   // wants_fastload(): the next tick is unpaced
        const bool fast = app->emulator().fastload_active();
        app->tick(s2);                                              // fastload: sleep 0
        app->emulator().phantom_typist().reset();
        const bool paced_again = !app->emulator().fastload_active();
        app->tick(s3);                                              // paced again: a fresh grid
        app->shutdown();
        check("SDLT-12", "a fastload tick reports no sleep; the paced tick after it starts a new grid: about one period (80-110 ms at 20%), not 1 ms and not the old grid's two periods",
              ok && fast && paced_again && s1 >= 80 && s1 <= 110 && s2 == 0 && s3 >= 80 && s3 <= 110,
              "s1=" + std::to_string(s1) + " s2=" + std::to_string(s2) + " s3=" + std::to_string(s3) +
              " fast=" + std::to_string(fast) + " paced_again=" + std::to_string(paced_again));
    }

    // --- SDLT-13: during fast load / fast boot the present is throttled, and its frames skip the compositor ----
    // GH #499. The renderer is vsynced, so presenting every frame of a sprint would cap it near the display's
    // refresh; the ticks inside RENDER_INTERVAL_MS (20 ms) of the last present neither present nor composite
    // (Emulator::rendered_frames() counts the composited frames). The clock is driven (FakeTime, no cost per
    // read): tick 1 at t = 1 s presents, tick 2 at the same instant must not, tick 3 25 ms later presents.
    {
        bool ok = false;
        auto app = make_app(ok);
        FakeTime ft;
        ft.work_us = 0;
        ft.t = 1000000;
        ft.attach(*app);
        app->emulator().arm_fast_boot(1000);
        const bool fast = app->emulator().fastload_active();
        uint32_t s = 0;
        const uint64_t r0 = app->emulator().rendered_frames();
        const uint64_t u0 = app->uploads_for_test();
        app->tick(s);
        const uint64_t r1 = app->emulator().rendered_frames();
        const uint64_t u1 = app->uploads_for_test();
        app->tick(s);
        const uint64_t r2 = app->emulator().rendered_frames();
        const uint64_t u2 = app->uploads_for_test();
        ft.t += 25000;
        app->tick(s);
        const uint64_t r3 = app->emulator().rendered_frames();
        const uint64_t u3 = app->uploads_for_test();
        const uint32_t frames = app->emulator().frame_num();
        app->shutdown();
        check("SDLT-13", "during fast load a tick inside RENDER_INTERVAL_MS of the last present does not present, and its frame skips the compositor (3 frames run: composited and presented 1, 0, then 1 after 25 ms)",
              ok && fast && frames == 3 && r1 - r0 == 1 && r2 == r1 && r3 - r2 == 1 &&
              u1 - u0 == 1 && u2 == u1 && u3 - u2 == 1,
              "fast=" + std::to_string(fast) + " frames=" + std::to_string(frames) + " r=" +
              std::to_string(r0) + "/" + std::to_string(r1) + "/" + std::to_string(r2) + "/" + std::to_string(r3) +
              " u=" + std::to_string(u0) + "/" + std::to_string(u1) + "/" + std::to_string(u2) + "/" + std::to_string(u3));
    }

    // --- SDLT-14: a due screenshot still presents during fast load ---------------------------------------------
    {
        namespace fs = std::filesystem;
        const fs::path shot = fs::temp_directory_path() / ("jnext-sdlt14-" + jtp::process_id_string() + ".png");
        std::error_code ec;
        fs::remove(shot, ec);
        bool ok = false;
        auto app = make_app(ok);
        FakeTime ft;
        ft.work_us = 0;
        ft.t = 1000000;
        ft.attach(*app);
        app->set_delayed_screenshot(shot.string(), 1, Renderer::LAYER_ALL);
        app->emulator().arm_fast_boot(1000);
        uint32_t s = 0;
        app->tick(s);                                    // presents (the first of the window)
        const uint64_t r1 = app->emulator().rendered_frames();
        const uint64_t u1 = app->uploads_for_test();
        app->tick(s);                                    // same instant: only the due screenshot lets it present
        const uint64_t r2 = app->emulator().rendered_frames();
        const uint64_t u2 = app->uploads_for_test();
        app->shutdown();
        const bool written = fs::exists(shot, ec);
        fs::remove(shot, ec);
        check("SDLT-14", "a due screenshot still presents and composites during fast load, inside the throttle interval, and the file is written",
              ok && r2 - r1 == 1 && u2 - u1 == 1 && written,
              "r=" + std::to_string(r1) + "/" + std::to_string(r2) + " u=" + std::to_string(u1) + "/" + std::to_string(u2) +
              " written=" + std::to_string(written));
    }

    // --- SDLT-15..17: GH #544, the hidden screen. Oracle: the plan's behaviour table (no VHDL counterpart). ----
    {
        bool ok = false;
        auto app = make_app(ok);
        FakeTime ft;
        ft.work_us = 0;
        ft.t = 1000000;
        ft.attach(*app);
        app->emulator().arm_fast_boot(1000, true);
        uint32_t s = 0;
        const uint64_t r0 = app->emulator().rendered_frames();
        const uint64_t u0 = app->uploads_for_test();
        app->tick(s);
        const uint64_t r1 = app->emulator().rendered_frames();
        const uint64_t u1 = app->uploads_for_test();
        const uint64_t g1 = app->last_upload_generation_for_test();
        const uint32_t* px = app->last_upload_pixels_for_test();
        const bool black = px && px != app->emulator().get_framebuffer() && px[0] == 0xFF000000u &&
                           px[640 * 256 - 1] == 0xFF000000u;
        ft.t += 25000;
        app->tick(s);
        const uint64_t r2 = app->emulator().rendered_frames();
        const uint64_t u2 = app->uploads_for_test();
        app->shutdown();
        check("SDLT-15", "armed with hide: tick 1 composites and uploads once, a black frame (not the emulator's pixels) under HIDDEN_GENERATION; a tick 25 ms later composites but uploads nothing (the black frame is held)",
              ok && r1 - r0 == 1 && u1 - u0 == 1 && g1 == fast_boot::HIDDEN_GENERATION && black && r2 - r1 == 1 && u2 == u1,
              "r=" + std::to_string(r0) + "/" + std::to_string(r1) + "/" + std::to_string(r2) +
              " u=" + std::to_string(u0) + "/" + std::to_string(u1) + "/" + std::to_string(u2) +
              " gen=" + std::to_string(g1));
    }
    {
        bool ok = false;
        auto app = make_app(ok);
        FakeTime ft;
        ft.work_us = 0;
        ft.t = 1000000;
        ft.attach(*app);
        app->emulator().arm_fast_boot(1, true);
        uint32_t s = 0;
        app->tick(s);                    // runs the one frame that closes the window (time up)
        const bool closed = !app->emulator().fast_boot_hides_screen();
        ft.t += 25000;
        const uint64_t u1 = app->uploads_for_test();
        app->tick(s);
        const uint64_t u2 = app->uploads_for_test();
        const uint64_t g2 = app->last_upload_generation_for_test();
        const uint64_t fbgen = app->emulator().framebuffer_generation();
        app->shutdown();
        check("SDLT-16", "armed with hide for 1 frame: the tick after the window closes uploads the real frame (its own generation, not the sentinel)",
              ok && closed && u2 - u1 == 1 && g2 == fbgen && g2 != fast_boot::HIDDEN_GENERATION,
              "closed=" + std::to_string(closed) + " u=" + std::to_string(u1) + "/" + std::to_string(u2) +
              " gen=" + std::to_string(g2) + " fbgen=" + std::to_string(fbgen));
    }
    {
        bool ok = false;
        auto app = make_app(ok);
        FakeTime ft;
        ft.work_us = 0;
        ft.t = 1000000;
        ft.attach(*app);
        app->emulator().arm_fast_boot(1000);
        uint32_t s = 0;
        const uint64_t u0 = app->uploads_for_test();
        app->tick(s);
        const uint64_t u1 = app->uploads_for_test();
        const uint64_t g1 = app->last_upload_generation_for_test();
        const uint64_t fbgen = app->emulator().framebuffer_generation();
        const bool real_px = app->last_upload_pixels_for_test() == app->emulator().get_framebuffer();
        app->shutdown();
        check("SDLT-17", "armed without hide: tick 1 uploads the emulator's own pixels and generation (the default path is untouched)",
              ok && u1 - u0 == 1 && g1 == fbgen && g1 != fast_boot::HIDDEN_GENERATION && real_px,
              "u=" + std::to_string(u0) + "/" + std::to_string(u1) + " gen=" + std::to_string(g1) +
              " fbgen=" + std::to_string(fbgen));
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
