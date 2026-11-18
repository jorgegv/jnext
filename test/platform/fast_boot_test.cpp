// GH #499 - fast boot: the first emulated seconds of a plain Next start run at
// maximum speed.
//
// No hardware counterpart (host pacing, like the tape fast-load rows of GH #408).
// The oracles are the plan's decision table (fast_boot::decide(): the first
// matching reason wins; seconds are 50 frames each) and its measured NextZXOS
// boot timeline (the firmware's soft reset at about frame 494, the welcome
// screen complete at frame 577), plus the window rules: it ends at time up, a
// guest key (delivered as usual), a debugger stop, init() and load_state(); it
// survives a soft reset; it is host pacing only and never changes the machine.
//
// Run: ./build/test/fast_boot_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/saveable.h"
#include "input/keyboard.h"
#include "platform/fast_boot.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <sstream>
#include <string>
#include <vector>
#include <spdlog/sinks/ostream_sink.h>
#include "core/log.h"
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

using fast_boot::Launch;
using fast_boot::Why;

Launch next_launch(int seconds = 15)
{
    Launch l;
    l.seconds = seconds;
    l.next_machine = true;
    return l;
}

std::unique_ptr<Emulator> make48k()
{
    auto emu = std::make_unique<Emulator>();
    EmulatorConfig cfg;
    cfg.type = MachineType::ZX48K;
    cfg.rewind_buffer_frames = 0;
    if (!emu->init(cfg)) emu.reset();
    return emu;
}

std::vector<uint8_t> state_of(const Emulator& e)
{
    StateWriter measure;
    e.save_state(measure);
    std::vector<uint8_t> buf(measure.position());
    StateWriter w(buf.data(), buf.size());
    e.save_state(w);
    return buf;
}

std::string sd_image()
{
    if (const char* env = std::getenv("JNEXT_TEST_SD_IMAGE")) return env;
    const char* home = std::getenv("HOME");
    return std::string(home ? home : ".") + "/.jnext/sdcard/cspect-next-1gb-fixed.img";
}

} // namespace

int main()
{
    std::printf("GH #499 fast boot test\n");
    std::printf("===============================================\n\n");

    // ---- decide() ----------------------------------------------------------
    {
        const auto d = fast_boot::decide(next_launch());
        check("FBT-01", "plan table: Next, 15 s, nothing else -> Armed, 750 frames",
              d.why == Why::Armed && d.frames == 750 && fast_boot::is_next(MachineType::ZXN_ISSUE2));
    }
    {
        const auto d = fast_boot::decide(next_launch(0));
        check("FBT-02", "plan table: seconds 0 -> Off, 0 frames", d.why == Why::Off && d.frames == 0);
    }
    {
        Launch l = next_launch(); l.headless = true;
        const auto d = fast_boot::decide(l);
        check("FBT-03", "plan table: headless -> Headless, 0 frames", d.why == Why::Headless && d.frames == 0);
    }
    {
        Launch l = next_launch(); l.next_machine = fast_boot::is_next(MachineType::ZX48K);
        const auto d = fast_boot::decide(l);
        check("FBT-04a", "plan table: 48k -> NotNext, 0 frames (and is_next() says no)",
              d.why == Why::NotNext && d.frames == 0);
    }
    {
        Launch l = next_launch(); l.next_machine = fast_boot::is_next(MachineType::ZX128K);
        const auto d = fast_boot::decide(l);
        check("FBT-04b", "plan table: 128k -> NotNext, 0 frames (and is_next() says no)",
              d.why == Why::NotNext && d.frames == 0);
    }
    {
        Launch l = next_launch(); l.next_machine = fast_boot::is_next(MachineType::ZX_PLUS3);
        const auto d = fast_boot::decide(l);
        check("FBT-04c", "plan table: plus3 -> NotNext, 0 frames (and is_next() says no)",
              d.why == Why::NotNext && d.frames == 0);
    }
    {
        Launch l = next_launch(); l.next_machine = fast_boot::is_next(MachineType::PENTAGON);
        const auto d = fast_boot::decide(l);
        check("FBT-04d", "plan table: pentagon -> NotNext, 0 frames (and is_next() says no)",
              d.why == Why::NotNext && d.frames == 0);
    }
    {
        Launch l = next_launch(); l.program = true;
        const auto d = fast_boot::decide(l);
        check("FBT-05", "plan table: a program loaded at start -> Program", d.why == Why::Program && d.frames == 0);
    }
    {
        Launch l = next_launch(); l.scheduled = true;
        const auto d = fast_boot::decide(l);
        check("FBT-06", "plan table: scheduled command-line work -> Scheduled", d.why == Why::Scheduled && d.frames == 0);
    }
    {
        Launch l = next_launch(); l.recording = true;
        const auto d = fast_boot::decide(l);
        check("FBT-07", "plan table: a recording -> Recording", d.why == Why::Recording && d.frames == 0);
    }
    {
        const auto d = fast_boot::decide(next_launch(1));
        check("FBT-08a", "plan table: 1 s -> 50 frames", d.why == Why::Armed && d.frames == 50);
    }
    {
        const auto d = fast_boot::decide(next_launch(60));
        check("FBT-08b", "plan table: 60 s -> 3000 frames", d.why == Why::Armed && d.frames == 3000);
    }
    {
        Launch l = next_launch(0); l.headless = true; l.program = true;
        const auto d = fast_boot::decide(l);
        check("FBT-09", "plan table: seconds 0 with headless and program -> Off (first reason wins)",
              d.why == Why::Off);
    }

    // ---- the core window ---------------------------------------------------
    {
        auto emu = make48k();
        bool ok = emu != nullptr;
        const uint32_t N = 10;
        bool mid = false;
        if (ok) {
            emu->arm_fast_boot(N);
            for (uint32_t i = 0; i + 1 < N; ++i) emu->run_frame();
            mid = emu->fastload_active() && emu->fast_boot_frames_left() == 1;
        }
        check("FBT-10a", "after arm_fast_boot(N) and N-1 frames the window is still open (fastload_active)", ok && mid);
        bool end = false;
        if (ok) {
            emu->run_frame();
            end = !emu->fastload_active() && emu->fast_boot_frames_left() == 0;
        }
        check("FBT-10b", "after N frames it is closed: fastload_active() false, 0 frames left", ok && end);
    }
    {
        // Every membrane position (8 rows x 5 columns), a fresh machine each, and the guest still
        // reads Caps Shift down after the window closed on it.
        std::string bad;
        bool still_down = false;
        for (int row = 0; row < 8; ++row) {
            for (int col = 0; col < 5; ++col) {
                auto emu = make48k();
                if (!emu) { bad += "init "; continue; }
                emu->arm_fast_boot(500);
                emu->run_frame();
                emu->keyboard().set_matrix_bit(row, col, true);
                emu->run_frame();
                if (emu->fast_boot_frames_left() != 0 || emu->fastload_active())
                    bad += "r" + std::to_string(row) + "c" + std::to_string(col) + " ";
                if (row == 0 && col == 0) {
                    for (int i = 0; i < 4; ++i) emu->run_frame();   // the membrane's two-scan hysteresis
                    still_down = (emu->keyboard().read_rows(0xFE) & 0x01) == 0;
                }
            }
        }
        check("FBT-11", "a key at ANY of the 40 matrix positions closes the window after that frame, and the guest still reads Caps Shift down",
              bad.empty() && still_down, bad);
    }
    {
        // Every extended-key bit Keyboard defines (ExtKey 0..15).
        std::string bad;
        for (int id = 0; id < 16; ++id) {
            auto emu = make48k();
            if (!emu) { bad += "init "; continue; }
            emu->arm_fast_boot(500);
            emu->run_frame();
            emu->keyboard().set_extended_key(id, true);
            emu->run_frame();
            if (emu->fast_boot_frames_left() != 0 || emu->fastload_active()) bad += std::to_string(id) + " ";
        }
        check("FBT-12", "an extended key closes the window too, every one of the 16 bits", bad.empty(), bad);
    }
    {
        // The sequence the frontends run: pause BETWEEN frames (Break, Emulation > Pause, a remote
        // client), no run_frame() while paused, resume, run on. A resume() of a RUNNING machine is routine
        // and must not close it.
        auto emu = make48k();
        bool routine_ok = false, closed = false, stays = false;
        if (emu) {
            emu->arm_fast_boot(500);
            emu->run_frame();
            emu->debug_state().resume();
            emu->run_frame();
            routine_ok = emu->fast_boot_frames_left() > 0;
            emu->debug_state().pause();
            emu->debug_state().resume();
            emu->run_frame();
            closed = emu->fast_boot_frames_left() == 0 && !emu->fastload_active();
            emu->run_frame();
            stays = emu->fast_boot_frames_left() == 0 && !emu->fastload_active();
        }
        check("FBT-13", "a pause between frames then a resume closes the window (and it stays closed); a resume of a running machine does not",
              emu && routine_ok && closed && stays);
    }
    {
        // A breakpoint inside a frame: the frame is left half-run, then resumed.
        auto emu = make48k();
        bool closed = false, stays = false;
        if (emu) {
            emu->debug_state().set_persistent_breakpoints(true);   // arms the hot loop's pause check
            emu->arm_fast_boot(500);
            emu->run_frame();
            const bool open_before = emu->fast_boot_frames_left() > 0;
            emu->debug_state().pause();
            emu->run_frame();                    // returns at once, the frame half-run
            emu->debug_state().resume();
            emu->run_frame();
            closed = open_before && emu->fast_boot_frames_left() == 0 && !emu->fastload_active();
            emu->run_frame();
            stays = emu->fast_boot_frames_left() == 0 && !emu->fastload_active();
        }
        check("FBT-13b", "a stop inside a frame (the frame left half-run) closes the window after the resume, and it stays closed",
              emu && closed && stays);
    }
    {
        auto emu = make48k();
        uint32_t before = 0, after = 0;
        if (emu) {
            emu->arm_fast_boot(500);
            for (int i = 0; i < 3; ++i) emu->run_frame();
            before = emu->fast_boot_frames_left();
            emu->soft_reset();
            after = emu->fast_boot_frames_left();
        }
        check("FBT-14", "soft_reset() (the firmware's own hand-over to NextZXOS) leaves the frames left unchanged",
              emu && before == 497 && after == before);
    }
    {
        auto emu = make48k();
        bool closed = false;
        if (emu) {
            emu->arm_fast_boot(500);
            emu->run_frame();
            EmulatorConfig cfg = emu->config();
            closed = emu->init(cfg) && emu->fast_boot_frames_left() == 0 && !emu->fastload_active();
        }
        check("FBT-15", "init(config()), what every load and machine change does, closes the window",
              emu && closed);
    }
    {
        auto emu = make48k();
        bool closed = false, restored = false;
        if (emu) {
            emu->run_frame();
            const std::vector<uint8_t> saved = state_of(*emu);
            emu->arm_fast_boot(500);
            StateReader r(saved.data(), saved.size());
            restored = emu->load_state(r);
            closed = emu->fast_boot_frames_left() == 0 && !emu->fastload_active();
        }
        check("FBT-16", "load_state() (rewind, a debugger state load, .jns) closes the window",
              emu && restored && closed);
    }
    {
        // One emulator at a time: the CPU core keeps global state, so two machines stepped in
        // alternation are not independent.
        bool open = false, built = false;
        std::vector<uint8_t> armed_state, plain_state;
        {
            auto a = make48k();
            built = a != nullptr;
            if (a) {
                a->arm_fast_boot(200);
                for (int i = 0; i < 100; ++i) a->run_frame();
                open = a->fastload_active();
                armed_state = state_of(*a);
            }
        }
        {
            auto b = make48k();
            built = built && b != nullptr;
            if (b) {
                for (int i = 0; i < 100; ++i) b->run_frame();
                plain_state = state_of(*b);
            }
        }
        check("FBT-17", "host pacing only: an armed (window open) and an unarmed emulator have identical save_state streams after 100 frames",
              built && open && !armed_state.empty() && armed_state == plain_state);
    }

    // ---- a real NextZXOS boot ---------------------------------------------
    {
        auto emu = std::make_unique<Emulator>();
        EmulatorConfig cfg;
        cfg.type = MachineType::ZXN_ISSUE2;
        cfg.rewind_buffer_frames = 0;
        cfg.sd_card_image = sd_image();
        cfg.sd_card_readonly = true;
        cfg.rtc_fixed = true;
        std::tm tm{};
        tm.tm_year = 2026 - 1900; tm.tm_mon = 0; tm.tm_mday = 2;
        tm.tm_hour = 3; tm.tm_min = 4; tm.tm_sec = 5;
        cfg.rtc_fixed_tm = tm;
        // The firmware's soft reset is observable only in the log (init(preserve_memory=true)).
        std::ostringstream log_out;
        auto log_sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(log_out, true);
        log_sink->set_pattern("%v");
        Log::emulator()->sinks().push_back(log_sink);
        bool built = emu->init(cfg);
        bool soft_reset_seen = false, open_at_600 = false, closed_at_750 = false;
        int reset_frame = -1;
        if (built) {
            emu->arm_fast_boot(750);
            for (int f = 1; f <= 750; ++f) {
                emu->run_frame();
                if (reset_frame < 0 && log_out.str().find("soft-reset") != std::string::npos) reset_frame = f;
                if (f == 600) open_at_600 = emu->fastload_active();
            }
            soft_reset_seen = reset_frame > 450 && reset_frame <= 560;
            closed_at_750 = !emu->fastload_active() && emu->fast_boot_frames_left() == 0;
        }
        check("FBT-20a", "real boot, armed for 750 frames: the firmware's soft reset falls between frames 450 and 560 and the window is still open at frame 600",
              built && soft_reset_seen && open_at_600, "soft reset at frame " + std::to_string(reset_frame));
        check("FBT-20b", "real boot: the window is closed after frame 750 (time up)", built && closed_at_750);
    }

    std::printf("\n==============================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
