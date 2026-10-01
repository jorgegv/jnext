#include "headless_app.h"
#include "platform/emulator_boot.h"
#include "platform/auto_exit.h"
#include "platform/cli_capture.h"
#include "platform/rzx_startup.h"
#include "core/log.h"
#include "input/keyboard.h"
#include "debug/inspect.h"
#include "debug/debugger.h"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <new>
#include <sched.h>

// Build type baked in by src/platform/CMakeLists.txt (Task 27 T1) so the
// BENCH line can state which optimisation level produced the numbers.
#ifndef JNEXT_BUILD_TYPE
#define JNEXT_BUILD_TYPE "unknown"
#endif

bool HeadlessApp::init(int argc, char* argv[]) {
    (void)argc; (void)argv;

    EmulatorConfig cfg = config_set_ ? config_ : EmulatorConfig{};
    if (!emulator_.init(cfg)) {
        Log::platform()->error("Emulator init failed");
        return false;
    }

    // GH #276 B4 — THE HOSTED DEBUGGER BACKEND (§10.1's B4 row; the plan's
    // §6.1). One per process, built on the initialised machine and kept across
    // every cold boot (placement-new keeps &emulator_). With no client attached
    // it arms nothing: `armed()`, `attached()`, `raster_live()` and the capture
    // render bit stay false and the coverage sink null (rows HOST-01..03), so a
    // run with it is the run without it.
    //
    //   * SES-07's driver: a client's `reset(Hard)` runs `boot_machine()` — the
    //     backend brackets that boot with its own capture — and `load(path)`
    //     runs the same format dispatch `--load` uses.
    //   * SES-04: `ExitNonZero` — a headless run is a CI verdict. What turns a
    //     `Stop` into an exit is an `ExitRequested` LISTENER, and a listener
    //     belongs to an attached client, and an attached client ARMS the machine
    //     (every instruction pays the armed block). So there is none yet: nothing
    //     can stop here until a client subscribes, and that client is where the
    //     listener belongs. B4 report, milestone 2 part 1, O5.
    debugger_ = std::make_unique<jnext::dbg::Debugger>(emulator_);
    debugger_->set_stop_policy(jnext::dbg::StopPolicy::ExitNonZero);
    jnext::dbg::LoopDriver driver;
    driver.cold_boot = [this]() {
        boot_machine(std::string());
        // `emulator_frontend_cold_boot()` returns void; SES-07 foresees the
        // loop owner synthesising the success flag.
        return true;
    };
    driver.load = [this](const std::string& path) {
        return emulator_apply_load(emulator_, path, tape_realtime_);
    };
    debugger_->set_loop_driver(driver);
    host_probe_ = HostProbe::from_env(emulator_, *debugger_);   // GH #276 B5
    // GH #12 (WP-5) — the socket debugger servers, on this loop's pump.
    if (!debug_servers_.start(*debugger_, config_)) return false;
    // GH #26 WP4 — the scripts, after the servers and before the machine runs:
    // a script that does not load is a startup failure (exit 1, §6.5).
    {
        jnext::script::ScriptHostOptions so;
        so.map_file = config_.map_file;
        so.scripts  = config_.script_files;
        so.keys     = config_.script_keys;
        so.exits    = true;
        if (!script_host_.start(*debugger_, so)) return false;
    }

    running_ = true;
    Log::platform()->info("Headless mode initialized");
    return true;
}

// Task 70 — power-on cold boot: reconstruct the emulator in place and re-run the
// proven startup init() path (shared with the Qt/SDL frontends,
// platform/emulator_boot.h). Empty load_file => clean NextZXOS boot; non-empty
// => boot as if launched with --load <file>.
void HeadlessApp::boot_machine(const std::string& load_file) {
    Log::platform()->info("Cold boot (reconstruct + init), load_file='{}'",
                          load_file.empty() ? "(none)" : load_file.c_str());
    EmulatorConfig cfg = config_;
    cfg.load_file = load_file;
    emulator_cold_boot(emulator_, cfg);
    config_.type = emulator_.config().type;   // a recording's machine stays
    inject_countdown_ = -1;
    load_countdown_   = -1;
    // GH #276 B4 — a --delayed-screenshot handed to the backend survives the
    // boot (the backend re-arms it on the rebuilt machine); the loop's reading of
    // the rendered-frame counter must be re-based with it, since the rebuilt
    // Emulator counts from 0 again.
    screenshot_queued_at_ = emulator_.rendered_frames();
    if (!load_file.empty()) {
        load_file_      = load_file;
        load_countdown_ = emulator_load_delay_frames(load_file);
    }
}

// GH #276 B3/B4 — CTL-12 rule 5, the path on which the LOOP OWNER decides to
// reboot. `on_cold_boot_begin()` while the machine still exists, so the backend
// captures the pause in force and whose it is; `on_cold_boot_done()` after, so
// the reconstruct contract's re-application (the publications, the page seed,
// the gates, the ring discard, the enables and the captured pause, the
// coverage sink, the queued captures) runs for it as for a client's
// `reset(Hard)`. B3 made these two calls for the `JNEXT_BENCH_WATCH` fixture
// only and said nothing pinned them; they are unconditional now that the
// backend is hosted, and rows HOST-04/05 pin both (through `HeadlessApp`).
void HeadlessApp::guest_cold_boot(const std::string& load_file) {
    debugger_->on_cold_boot_begin();
    boot_machine(load_file);
    debugger_->on_cold_boot_done();
}

void HeadlessApp::set_pending_inject(const std::string& file, uint16_t org,
                                     uint16_t pc, int delay_frames) {
    inject_file_ = file;
    inject_org_  = org;
    inject_pc_   = pc;
    inject_countdown_ = delay_frames;
    Log::platform()->info("--inject: will load '{}' at {:#06x} (PC={:#06x}) after {} frame(s)",
                           file, org, pc, delay_frames);
}

void HeadlessApp::set_pending_load(const std::string& file, int delay_frames) {
    load_file_ = file;
    load_countdown_ = delay_frames;
    Log::platform()->info("--load: will load '{}' after {} frame(s)", file, delay_frames);
}

void HeadlessApp::set_delayed_screenshot(const std::string& file, int delay_frames,
                                         uint8_t layer_mask) {
    screenshot_file_ = file;
    screenshot_countdown_ = delay_frames;
    screenshot_layers_ = layer_mask;
    Log::platform()->info("--delayed-screenshot: will save '{}' after {} frame(s) (layers: {})",
                           file, delay_frames,
                           Renderer::layer_mask_to_string(layer_mask));
}

void HeadlessApp::set_delayed_snapshot(const std::string& file, int delay_frames) {
    snapshot_file_ = file;
    snapshot_countdown_ = delay_frames;
    Log::platform()->info("--delayed-snapshot: will save '{}' after {} frame(s)",
                           file, delay_frames);
}

void HeadlessApp::set_benchmark(int frames, const std::string& workload) {
    benchmark_frames_ = frames;
    benchmark_workload_ = workload;
    Log::platform()->info("--benchmark: will run {} frame(s) uncapped (workload: {})",
                           frames, workload);
}

// Print the benchmark result (Task 27 T1). One machine-parseable BENCH line
// plus one human-readable summary line, both to stdout (all logging goes to
// stderr, so stdout carries exactly these two lines).
//
// T-states/frame is computed from the machine timing actually in effect at
// exit: master cycles per frame = (hc_max+1) pixel-ticks/line x (vc_max+1)
// lines x 4 (28 MHz master cycles per 7 MHz pixel tick, timing.h:36-37),
// divided by the effective CPU divisor (Clock::cpu_divisor — the committed
// NR 0x07 speed: 8=3.5MHz, 4=7MHz, 2=14MHz, 1=28MHz).
// Documented approximation: both the timing constants and the divisor are
// sampled AT EXIT and assumed constant over the run. A workload that switches
// CPU speed or video timing mid-run (e.g. the NextZXOS boot, which commits +3
// timing and 28 MHz during boot) is reported entirely at its final speed.
void HeadlessApp::print_benchmark_result(double wall_seconds) {
    const auto& vt = emulator_.video_timing();
    const uint64_t master_per_frame =
        (static_cast<uint64_t>(vt.hc_max()) + 1) *
        (static_cast<uint64_t>(vt.vc_max()) + 1) * 4;
    const int divisor = emulator_.clock().cpu_divisor();
    const uint64_t tstates_per_frame = master_per_frame / static_cast<uint64_t>(divisor);

    const double fps = (wall_seconds > 0.0) ? benchmark_frames_ / wall_seconds : 0.0;
    const double tstates_per_sec = static_cast<double>(tstates_per_frame) * fps;

    // Guest CPU speed from the effective divisor (28 MHz / divisor).
    char cpu_str[16];
    if (divisor == 8)
        std::snprintf(cpu_str, sizeof(cpu_str), "3.5MHz");
    else
        std::snprintf(cpu_str, sizeof(cpu_str), "%dMHz", 28 / divisor);

    // Host core the run ended on + its scaling_max_freq (kHz). Different
    // core classes on hybrid CPUs differ ~40%, so numbers are only
    // comparable when this field matches. Both sched_getcpu() and the
    // /sys cpufreq node are Linux-only; on other platforms (Windows/MinGW)
    // these fields are simply reported as -1 / 0.
    int  core = -1;
    long khz  = 0;
#if defined(__linux__)
    core = sched_getcpu();
    if (core >= 0) {
        char path[128];
        std::snprintf(path, sizeof(path),
                      "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", core);
        std::ifstream f(path);
        if (f) f >> khz;
    }
#endif

    std::printf("BENCH workload=%s frames=%d wall=%.3f fps=%.1f "
                "tstates_per_sec=%.0f tstates_per_frame=%llu cpu=%s core=%d@%ldkHz build=%s\n",
                benchmark_workload_.c_str(), benchmark_frames_, wall_seconds, fps,
                tstates_per_sec,
                static_cast<unsigned long long>(tstates_per_frame),
                cpu_str, core, khz, JNEXT_BUILD_TYPE);
    std::printf("Benchmark %s: %d frames in %.3f s = %.1f fps, %.1fM T-states/s "
                "(%llu T-states/frame @ %s), core %d @ %ld kHz, %s build\n",
                benchmark_workload_.c_str(), benchmark_frames_, wall_seconds, fps,
                tstates_per_sec / 1e6,
                static_cast<unsigned long long>(tstates_per_frame),
                cpu_str, core, khz, JNEXT_BUILD_TYPE);
    std::fflush(stdout);
}

void HeadlessApp::set_delayed_exit(int delay_frames) {
    exit_countdown_ = delay_frames;
    Log::platform()->info("--delayed-automatic-exit: will exit after {} frame(s)",
                           delay_frames);
}

// GH #276 §4.5 — the key-name vocabulary moved to the backend
// (`jnext::dbg::key_name_to_matrix`, src/debug/inspect.cpp). It was a pair of
// file-statics here, which made this file the owner of a table the DSL and both
// GUI frontends also need; the CLI is now one of four callers of one table.
static bool key_name_to_matrix(const std::string& name,
                               int& row1, int& col1, int& row2, int& col2) {
    jnext::dbg::MatrixKey k;
    if (!jnext::dbg::key_name_to_matrix(name, k)) return false;
    row1 = k.row1; col1 = k.col1; row2 = k.row2; col2 = k.col2;
    return true;
}

bool HeadlessApp::set_delayed_keypress(const std::string& key, int delay_frames) {
    DelayedKey dk;
    dk.name = key;
    dk.countdown = delay_frames;
    if (!key_name_to_matrix(key, dk.row1, dk.col1, dk.row2, dk.col2)) {
        Log::platform()->error("delayed-keypress: unknown key name '{}'", key);
        return false;
    }
    delayed_keys_.push_back(dk);
    Log::platform()->info("delayed-keypress: will press '{}' after {} frame(s)",
                           key, delay_frames);
    return true;
}

bool HeadlessApp::set_delayed_keypress_seconds(const std::string& key, int delay_seconds) {
    DelayedKey dk;
    dk.name = key;
    dk.countdown = -1;  // converted to frames in run()
    if (!key_name_to_matrix(key, dk.row1, dk.col1, dk.row2, dk.col2)) {
        Log::platform()->error("delayed-keypress: unknown key name '{}'", key);
        return false;
    }
    pending_seconds_keys_.push_back({dk, delay_seconds});
    Log::platform()->info("delayed-keypress: will press '{}' after {} emulated second(s)",
                           key, delay_seconds);
    return true;
}

// Parse a --delayed-nmi button name (GH #209). The Next has TWO NMI
// buttons with separate NextREG enable gates — so there is no sensible
// default and the name is mandatory.
//
// NAMED AFTER THE PHYSICAL CASE LABELS FIRST. A real Next has three
// buttons, NMI / DRIVE / RESET, and two of them raise an NMI:
//
//   NMI    the Multiface NMI button  -> `nmi`,   aliases `mf`, `m1`
//   DRIVE  the DivMMC NMI button     -> `drive`, alias `divmmc`
//   RESET  not an NMI at all (reset), so it is not a name here
//
// The VHDL agrees, and names the pins for the SUBSYSTEM while putting
// the case label in a comment: `-- multiface nmi button (nmi)` and
// `-- divmmc nmi button (drive)` (zxnext_top_issue2.vhd:1768, :1800),
// with `btn_multiface_n_i` / `btn_divmmc_n_i` at :94-95. The
// subsystem spellings are kept as aliases because they are what the
// emulator's own internals, the F-key comments (`F9 = m1 button`,
// `F10 = drive button`, :2277-2278) and the NextREG documentation
// use — but someone holding the hardware reads `NMI` off the case, so
// that spelling has to work, and is the one the docs lead with.
static bool nmi_button_from_name(const std::string& name,
                                 HeadlessApp::NmiButtonName& out) {
    std::string k;
    k.reserve(name.size());
    for (char c : name)
        k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (k == "nmi" || k == "mf" || k == "m1")  { out = HeadlessApp::NmiButtonName::Mf;     return true; }
    if (k == "drive" || k == "divmmc")         { out = HeadlessApp::NmiButtonName::DivMmc; return true; }
    return false;
}

bool HeadlessApp::set_delayed_nmi(const std::string& button, int delay_frames) {
    NmiButtonName which;
    if (!nmi_button_from_name(button, which)) {
        Log::platform()->error("delayed-nmi: unknown button name '{}' "
                               "(expected 'nmi' (aliases 'mf'/'m1') or 'drive' (alias 'divmmc'))", button);
        return false;
    }
    DelayedNmi dn;
    dn.name      = button;
    dn.button    = which;
    dn.countdown = delay_frames;
    delayed_nmis_.push_back(dn);
    Log::platform()->info("delayed-nmi: will press '{}' NMI button after {} frame(s)",
                          button, delay_frames);
    return true;
}

bool HeadlessApp::set_delayed_nmi_seconds(const std::string& button, int delay_seconds) {
    NmiButtonName which;
    if (!nmi_button_from_name(button, which)) {
        Log::platform()->error("delayed-nmi: unknown button name '{}' "
                               "(expected 'nmi' (aliases 'mf'/'m1') or 'drive' (alias 'divmmc'))", button);
        return false;
    }
    DelayedNmi dn;
    dn.name      = button;
    dn.button    = which;
    dn.countdown = -1;  // converted to frames in run()
    pending_seconds_nmis_.push_back({dn, delay_seconds});
    Log::platform()->info("delayed-nmi: will press '{}' NMI button after {} emulated second(s)",
                          button, delay_seconds);
    return true;
}

void HeadlessApp::set_delayed_sdcard_insert(const std::string& image, int delay_frames) {
    delayed_sd_inserts_.push_back({image, delay_frames});
    Log::platform()->info("delayed-sdcard-insert: will insert '{}' after {} frame(s)",
                          image, delay_frames);
}

void HeadlessApp::run() {
    // Convert any seconds-form delayed keypresses to frames now that the
    // emulator is fully initialized and the machine framerate is known.
    if (!pending_seconds_keys_.empty()) {
        const int fps = emulator_.video_timing().refresh_60hz() ? 60 : 50;
        for (const auto& pk : pending_seconds_keys_) {
            DelayedKey dk = pk.key;
            dk.countdown = pk.delay_seconds * fps;
            delayed_keys_.push_back(dk);
            Log::platform()->info("delayed-keypress: '{}' scheduled at frame {} ({} s × {} fps)",
                                   dk.name, dk.countdown, pk.delay_seconds, fps);
        }
        pending_seconds_keys_.clear();
    }

    // Same framerate-aware conversion for seconds-form NMI presses.
    if (!pending_seconds_nmis_.empty()) {
        const int fps = emulator_.video_timing().refresh_60hz() ? 60 : 50;
        for (const auto& pn : pending_seconds_nmis_) {
            DelayedNmi dn = pn.nmi;
            dn.countdown = pn.delay_seconds * fps;
            delayed_nmis_.push_back(dn);
            Log::platform()->info("delayed-nmi: '{}' scheduled at frame {} ({} s × {} fps)",
                                   dn.name, dn.countdown, pn.delay_seconds, fps);
        }
        pending_seconds_nmis_.clear();
    }

    // Command-line RZX play/record — shared with QtApp/SdlApp, and applied
    // here in run() for the reason given at emulator_start_rzx().
    // The recording starts later, once the command-line load is in: see
    // emulator_start_rzx_record_when_loaded() in the frame loop.
    if (!emulator_start_rzx(emulator_, rzx_play_file_, ""))
        exit_code_ = 1;   // same contract as a failed --load

    // --benchmark timing (Task 27 T1): the clock brackets exactly the
    // benchmark_frames_ run_frame() iterations below — headless already runs
    // uncapped, so wall time here is pure emulation throughput.
    using bench_clock = std::chrono::steady_clock;
    bench_clock::time_point bench_start{};
    int bench_frames_done = 0;
    if (benchmark_frames_ > 0)
        bench_start = bench_clock::now();

    // ── GH #276 §11 item 3 — THE HOT-LATCH BENCH FIXTURE ──────────────────
    //
    // §10.1 gates B2 on "the §11 item 3 hot-latch measurement" and §11 sets the
    // deadline at "before B2 merges". The measurement needs a RANGE `Mem` WATCH
    // THAT HITS, and `test/bench/bench.sh` cannot express one — so the first
    // round of B2 measured it with an uncommitted hook and the number survived
    // only as prose, which is exactly how an independent re-derivation came back
    // with a ~35 % different magnitude. This is that hook, in the tree.
    //
    // Env-gated in the established `JNEXT_G46B_*` style (zero cost unset), and
    // deliberately NOT a CLI flag: it is a measurement fixture, not a feature, so
    // it carries no `cli_options.h` row and no man-page obligation.
    //
    //   JNEXT_BENCH_WATCH=lo-hi   arm `Mem[lo,hi] Write` with a Continue handler
    //   JNEXT_BENCH_WATCH=p       arm nothing; --persistent-breakpoints only
    //
    //   JNEXT_BENCH_WATCH=c       arm nothing; ATTACH one client instead
    //
    // The `p` form exists because §6.3's armed rows conflated the latch cost with
    // the pre-existing `--persistent-breakpoints` per-instruction `should_break()`
    // lookup; A − P is what isolates the latch. The `c` form (GH #278 Q WP2) is
    // what a PROCESS-LIFETIME attach of the Qt adapter would leave the GUI in
    // with its window closed: a client attached and nothing subscribed, so
    // `armed()` and `attached()` are on through `clients_attached_` rather than
    // through `persistent_`. C − U is that attach's hot-path cost. The handler returns `Continue`, so
    // nothing pauses — §6.2 records a run where the watch paused the machine and
    // the benchmark "measured" 13x by emulating nothing.
    //
    // It prints `BENCHWATCH hits=N` to stderr at the end, so a run states its own
    // hit rate instead of leaving the reader to assume the watch fired.
    //
    // GH #276 B3 — the watch lives on a `Debugger` across every cold boot, which
    // made this fixture the first real consumer of CTL-12's reconstruct
    // contract. GH #276 B4 — and that `Debugger` is now the loop owner's hosted
    // one (`debugger_`, built by init()), whose `on_cold_boot_begin/done()`
    // bracket every guest boot below; the fixture no longer owns one of its own.
    // Without the re-application the subscription would still list as live and
    // never fire again, and `BENCHWATCH hits=0` would read as "the range was
    // never written" instead of "the watch was disconnected".
    unsigned long long* bench_watch_hits = nullptr;
    if (const char* bw = std::getenv("JNEXT_BENCH_WATCH")) {
        static unsigned long long hits = 0;
        bench_watch_hits = &hits;
        if (bw[0] == 'c') {
            const auto cid = debugger_->attach(
                jnext::dbg::ClientInfo{"bench", jnext::dbg::ClientKind::Test});
            Log::platform()->info("JNEXT_BENCH_WATCH: attached client {}, nothing "
                                  "subscribed (armed={})",
                                  cid.value, debugger_->armed() ? 1 : 0);
        } else {
            emulator_.debug_state().set_persistent_breakpoints(true);
        }
        if (bw[0] != 'p' && bw[0] != 'c') {
            unsigned lo = 0, hi = 0;
            if (std::sscanf(bw, "%x-%x", &lo, &hi) == 2) {
                jnext::dbg::Subscription bs;
                bs.kind      = jnext::dbg::EventKind::Mem;
                bs.access    = jnext::dbg::Access::Write;
                bs.filter.lo = static_cast<uint16_t>(lo);
                bs.filter.hi = static_cast<uint16_t>(hi);
                bs.action    = jnext::dbg::Action::Continue;
                bs.handler   = [](const jnext::dbg::Event&,
                                  jnext::dbg::Debugger&) {
                    ++hits;
                    return jnext::dbg::Action::Continue;
                };
                debugger_->subscribe(1, bs);
                Log::platform()->info(
                    "JNEXT_BENCH_WATCH: armed Mem[{:#06x},{:#06x}] Write", lo, hi);
            } else {
                Log::platform()->warn(
                    "JNEXT_BENCH_WATCH: expected 'lo-hi' in hex, 'p' or 'c'; got '{}'",
                    bw);
            }
        }
    }

    // G46(b) #102 investigation probe (env-gated, zero cost when unset).
    // JNEXT_G46B_PCTRACE=<path>: dump a one-line-per-frame CPU snapshot
    // (PC, opcode bytes at PC, key registers, halted/IM/IFF1) for a frame
    // window, to find the first frame at which a formerly-moving PC settles
    // into a static/repeating pattern (a "display frozen, CPU alive" bug
    // signature). Window controlled by JNEXT_G46B_PCTRACE_START (default 0)
    // and JNEXT_G46B_PCTRACE_FRAMES (default 1).
    // G46(b) #102 per-instruction probe. JNEXT_G46B_ITRACE=<path>,
    // JNEXT_G46B_ITRACE_START (default 0), JNEXT_G46B_ITRACE_FRAMES
    // (default 1). Enables Z80Cpu::set_g46b_itrace() only for that frame
    // window — per-instruction logging is far too high-volume to run for
    // a whole session.
    const char* g46b_itrace_path = std::getenv("JNEXT_G46B_ITRACE");
    FILE* g46b_itrace_file = nullptr;
    long  g46b_itrace_start = 0;
    long  g46b_itrace_frames = 1;
    if (g46b_itrace_path) {
        g46b_itrace_file = std::fopen(g46b_itrace_path, "w");
        if (const char* s = std::getenv("JNEXT_G46B_ITRACE_START"))
            g46b_itrace_start = std::atol(s);
        if (const char* f = std::getenv("JNEXT_G46B_ITRACE_FRAMES"))
            g46b_itrace_frames = std::atol(f);
        if (g46b_itrace_file) {
            std::fprintf(g46b_itrace_file,
                "tstates,pc,op0,op1,op2,op3,af,bc,de,hl,sp,ix,iy,halted,iff1\n");
        }
    }

    const char* g46b_pctrace_path = std::getenv("JNEXT_G46B_PCTRACE");
    FILE* g46b_pctrace_file = nullptr;
    long  g46b_pctrace_start = 0;
    long  g46b_pctrace_frames = 1;
    long  g46b_frame_no = 0;
    if (g46b_pctrace_path) {
        g46b_pctrace_file = std::fopen(g46b_pctrace_path, "w");
        if (const char* s = std::getenv("JNEXT_G46B_PCTRACE_START"))
            g46b_pctrace_start = std::atol(s);
        if (const char* f = std::getenv("JNEXT_G46B_PCTRACE_FRAMES"))
            g46b_pctrace_frames = std::atol(f);
        if (g46b_pctrace_file) {
            std::fprintf(g46b_pctrace_file,
                "frame,pc,op0,op1,op2,op3,af,bc,de,hl,sp,ix,iy,halted,im,iff1,fb_hash,dma_state,dma_counter,dma_block_len,im2_dma_delay,devstates\n");
        }
    }

    // G46(b) #102 session 3 probe. JNEXT_G46B_PORTTRACE=<path>,
    // JNEXT_G46B_PORTTRACE_START (default 0), JNEXT_G46B_PORTTRACE_FRAMES
    // (default 1). Logs every port read served by PortDispatch's DEFAULT
    // (no handler matched) path — PC/port/value/frame — via
    // Emulator::set_g46b_port_trace(). Answers "is this specific port
    // read genuinely undecoded (returns the 0xFF default, GH #109), or
    // does it hit a real registered handler" — see doc/issues/g46b-102-*.md.
    // NB since GH #109 port-0xFF (LSB) reads have a real handler and no
    // longer appear in this trace.
    const char* g46b_porttrace_path = std::getenv("JNEXT_G46B_PORTTRACE");
    FILE* g46b_porttrace_file = nullptr;
    long  g46b_porttrace_start = 0;
    long  g46b_porttrace_frames = 1;
    if (g46b_porttrace_path) {
        g46b_porttrace_file = std::fopen(g46b_porttrace_path, "w");
        if (const char* s = std::getenv("JNEXT_G46B_PORTTRACE_START"))
            g46b_porttrace_start = std::atol(s);
        if (const char* f = std::getenv("JNEXT_G46B_PORTTRACE_FRAMES"))
            g46b_porttrace_frames = std::atol(f);
        if (g46b_porttrace_file) {
            std::fprintf(g46b_porttrace_file, "frame,pc,port,value\n");
        }
    }

    // G46(b) #102 session 3 one-shot probe: JNEXT_G46B_MEMDUMP="frame,addr,len,path"
    // dumps `len` bytes at `addr` (CPU view) to `path` the moment g46b_frame_no
    // reaches `frame`. Used to decode the DMA WR-sequence table TX-1696 OTIRs
    // to port 0x0B — see doc/issues/g46b-102-*.md session 3.
    long g46b_memdump_frame = -1;
    uint16_t g46b_memdump_addr = 0;
    int g46b_memdump_len = 0;
    std::string g46b_memdump_path;
    if (const char* md = std::getenv("JNEXT_G46B_MEMDUMP")) {
        long f = 0, l = 0;
        unsigned long a = 0;
        char path[512] = {0};
        if (std::sscanf(md, "%ld,%lx,%ld,%511s", &f, &a, &l, path) == 4) {
            g46b_memdump_frame = f;
            g46b_memdump_addr = static_cast<uint16_t>(a);
            g46b_memdump_len = static_cast<int>(l);
            g46b_memdump_path = path;
        }
    }


    // GH #12 (WP-5) — wall time spent in the paused-with-a-remote wait below
    // that has not yet been charged to the exit countdown, in microseconds.
    long long paused_wait_us = 0;

    while (running_) {
        // GH #12 (WP-5) — PAUSED WITH A REMOTE ATTACHED: no frame and no frame
        // countdown, only the servers — `pump()` waits up to 50 ms for the next
        // command (transport.md §2 item 15). This is what turns a paused
        // headless run with DeZog attached from a busy spin into a poll.
        //
        // The ONE countdown that keeps running is the automatic exit, because
        // it is a hard bound that always fires: a client holding the machine
        // must not hold the process forever. It is charged in WALL time here,
        // one count per 20 ms (a 50 Hz frame — the same period the GUI ticks,
        // which count down paused or not), since a wait tick is not a frame.
        // When it comes due the tick falls through: run_frame() does nothing on
        // a paused machine, and the exit below fires as it always has.
        if (DebugServers::headless_should_wait(debugger_->state().paused, pump_hint_)) {
            const auto wait_start = std::chrono::steady_clock::now();
            pump_hint_ = debugger_->pump(DebugServers::headless_wait_budget());
            if (exit_countdown_ > 0) {
                paused_wait_us += std::chrono::duration_cast<std::chrono::microseconds>(
                                      std::chrono::steady_clock::now() - wait_start)
                                      .count();
                while (paused_wait_us >= 20000 && exit_countdown_ > 0) {
                    --exit_countdown_;
                    paused_wait_us -= 20000;
                }
            }
            if (exit_countdown_ != 0) continue;
        }

        // Headless reset facility (env-gated, zero cost when unset): --headless
        // has no Reset button, so this exercises the Task 70 cold-boot paths for
        // tests. JNEXT_DELAYED_RESET_FRAMES=N, JNEXT_DELAYED_RESET_TYPE =
        // hard (default) | soft | f4 | loadnex:/path.nex
        // "soft" calls Emulator::soft_reset() directly; "f4" goes through
        // the host-F4 hotkey dispatcher (config-mode gate + reset_type FSM
        // strobe), i.e. the exact GUI/SDL F4 path.
        static int t70_countdown = []() {
            const char* e = std::getenv("JNEXT_DELAYED_RESET_FRAMES");
            return e ? std::atoi(e) : -1;
        }();
        if (t70_countdown == 0) {
            const char* ty = std::getenv("JNEXT_DELAYED_RESET_TYPE");
            std::string t = ty ? ty : "hard";
            if (t == "soft") emulator_.soft_reset();
            else if (t == "f4") emulator_.on_hotkey_f4_soft_reset();
            else if (t.rfind("loadnex:", 0) == 0) guest_cold_boot(t.substr(8));
            else emulator_.request_hard_reset();  // flag -> polled after run_frame
            t70_countdown = -1;
        } else if (t70_countdown > 0) { --t70_countdown; }

        // Apply pending inject.
        if (inject_countdown_ == 0) {
            emulator_.inject_binary(inject_file_, inject_org_, inject_pc_);
            inject_countdown_ = -1;
        } else if (inject_countdown_ > 0) {
            --inject_countdown_;
        }

        // Apply pending load (shared format dispatch, incl. .rzx — see
        // platform/emulator_boot.h).
        if (load_countdown_ == 0) {
            const bool ok = emulator_apply_load(emulator_, load_file_, tape_realtime_);
            if (!ok) {
                // A failed load exits non-zero, whatever the format, so a
                // script can tell "loaded fine" from "failed to load, ran
                // anyway" (load-exit-status-func; snapshot-save-func relies
                // on it for a corrupt .szx reload).
                Log::platform()->error("--load: failed to load '{}'", load_file_);
                exit_code_ = 1;
            }
            load_countdown_ = -1;
        } else if (load_countdown_ > 0) {
            --load_countdown_;
        }

        // --rzx-record, once the load/inject above is in the machine.
        if (!emulator_start_rzx_record_when_loaded(
                emulator_, rzx_record_file_, rzx_record_started_,
                load_countdown_ >= 0 || inject_countdown_ >= 0))
            exit_code_ = 1;

        // Delayed keypresses. Matrix positions were resolved at schedule
        // time (unknown names are rejected there, never dropped here).
        for (auto it = delayed_keys_.begin(); it != delayed_keys_.end(); ) {
            if (it->countdown <= 0) {
                // GH #276 B4 (O2) — the ACTION through the backend's IN-01 verb:
                // a 5-frame pulse on the (appending) auto-type queue, the same
                // call every debugger client makes. The countdown stays here.
                debugger_->press_key(jnext::dbg::CLIENT_NONE,
                                     jnext::dbg::MatrixKey{it->row1, it->col1, it->row2, it->col2},
                                     5);   // press for 5 frames
                Log::platform()->info("Delayed keypress '{}' injected", it->name);
                it = delayed_keys_.erase(it);
            } else {
                --it->countdown;
                ++it;
            }
        }

        // Delayed NMI button presses (GH #209). Dispatched through the
        // same hotkey seam the GUI/SDL front-ends use for F9/F10, so
        // every enable gate and the arbitration chain are exercised —
        // GH #276 B4 (O2): by way of the backend's IN-04 verb, which IS that
        // seam (Debugger::press_nmi calls the two hotkey functions).
        // One press = one strobe (VHDL hotkey_m1 / hotkey_drive are
        // one-cycle edge pulses, zxnext.vhd:6348-6349), hence erase
        // after firing rather than holding a level down.
        for (auto it = delayed_nmis_.begin(); it != delayed_nmis_.end(); ) {
            if (it->countdown <= 0) {
                debugger_->press_nmi(jnext::dbg::CLIENT_NONE,
                                     it->button == NmiButtonName::Mf
                                         ? jnext::dbg::NmiButton::Mf
                                         : jnext::dbg::NmiButton::Drive);
                Log::platform()->info("Delayed NMI button '{}' pressed", it->name);
                it = delayed_nmis_.erase(it);
            } else {
                --it->countdown;
                ++it;
            }
        }

        // GH #93 — delayed SD-card inserts: REQUESTED here, as the GUI's File >
        // Insert SD Card Image… does, and performed after this tick's frame by
        // the same service call. A refusal fails the run, like a failed load.
        for (auto it = delayed_sd_inserts_.begin(); it != delayed_sd_inserts_.end(); ) {
            if (it->countdown <= 0) {
                if (!emulator_.request_sd_card_change(
                        {it->image, emulator_.config().sd_card_readonly}).empty())
                    exit_code_ = 1;
                it = delayed_sd_inserts_.erase(it);
            } else {
                --it->countdown;
                ++it;
            }
        }

        // --delayed-screenshot: when the countdown comes due, hand the capture
        // to the backend (GH #276 B4, O2; platform/cli_capture.h). It arms the
        // --delayed-screenshot-layers mask on the renderer NOW, for the frame
        // about to run, forces that frame to render, and the pump below writes
        // it and takes the mask down — exactly the frame, the mask and the file
        // this loop used to handle itself. Queued once: a tick that cold-boots
        // `continue`s past the rest of the loop and comes back here with the
        // capture still queued (the backend re-arms it on the rebuilt machine).
        if (screenshot_countdown_ == 0 && !screenshot_queued_) {
            screenshot_queued_at_ = emulator_.rendered_frames();
            if (queue_cli_screenshot(*debugger_, screenshot_file_, screenshot_layers_) ==
                jnext::dbg::Result::Ok)
                screenshot_queued_ = true;
            else
                screenshot_refused_ = true;   // reported below, as a failed write
        }

        if (g46b_itrace_file) {
            const bool in_window = g46b_frame_no >= g46b_itrace_start &&
                                    g46b_frame_no < g46b_itrace_start + g46b_itrace_frames;
            if (in_window) {
                // Session 3: unambiguous frame-boundary marker so the CSV
                // can be segmented by frame in post-processing without
                // relying on the (per-frame-resetting, but ambiguous
                // across multiple same-frame occurrences) tstates column.
                std::fprintf(g46b_itrace_file, "# FRAME %ld\n", g46b_frame_no);
            }
            emulator_.cpu().set_g46b_itrace(in_window ? g46b_itrace_file : nullptr);
        }

        if (g46b_porttrace_file) {
            const bool in_window = g46b_frame_no >= g46b_porttrace_start &&
                                    g46b_frame_no < g46b_porttrace_start + g46b_porttrace_frames;
            emulator_.set_g46b_port_trace(in_window ? g46b_porttrace_file : nullptr);
            emulator_.set_g46b_port_trace_frame(g46b_frame_no);
        }

        emulator_.run_frame();

        if (g46b_memdump_frame >= 0 && g46b_frame_no == g46b_memdump_frame) {
            std::FILE* mf = std::fopen(g46b_memdump_path.c_str(), "w");
            if (mf) {
                for (int i = 0; i < g46b_memdump_len; ++i) {
                    uint8_t b = emulator_.mmu().read(
                        static_cast<uint16_t>(g46b_memdump_addr + i));
                    std::fprintf(mf, "%02x ", b);
                }
                std::fprintf(mf, "\n");
                std::fclose(mf);
            }
            g46b_memdump_frame = -1;  // one-shot
        }

        if (g46b_pctrace_file &&
            g46b_frame_no >= g46b_pctrace_start &&
            g46b_frame_no < g46b_pctrace_start + g46b_pctrace_frames) {
            Z80Registers r = emulator_.cpu().get_registers();
            uint8_t op0 = emulator_.mmu().read(r.PC);
            uint8_t op1 = emulator_.mmu().read(static_cast<uint16_t>(r.PC + 1));
            uint8_t op2 = emulator_.mmu().read(static_cast<uint16_t>(r.PC + 2));
            uint8_t op3 = emulator_.mmu().read(static_cast<uint16_t>(r.PC + 3));
            // headless normally skips rendering most frames (perf; see
            // emulator.cpp render_this_frame gate) so get_framebuffer() is
            // stale except right before a requested screenshot. Force a
            // render here so every probed frame's hash is real.
            emulator_.renderer().render_frame(emulator_.get_framebuffer(),
                emulator_.mmu(), emulator_.ram(), emulator_.palette(),
                emulator_.layer2(), &emulator_.sprites(), &emulator_.tilemap());
            uint64_t fb_hash = 1469598103934665603ull;  // FNV-1a 64-bit offset basis
            const uint32_t* fb = emulator_.get_framebuffer();
            const size_t fb_pixels = static_cast<size_t>(emulator_.get_framebuffer_width()) *
                                      static_cast<size_t>(emulator_.get_framebuffer_height());
            for (size_t i = 0; i < fb_pixels; ++i) {
                fb_hash ^= fb[i];
                fb_hash *= 1099511628211ull;  // FNV-1a 64-bit prime
            }
            // Session 3: DMA state/counter + IM2 dma_delay + all 14 IM2
            // device states appended for the #102 investigation
            // (dma_state: 0=IDLE,1=TRANSFERRING per Dma::State enum order;
            // devstates: 14 hex digits, one per DevIdx 0..13, DevState
            // 0=S_0,1=S_REQ,2=S_ACK,3=S_ISR).
            char devstates[16] = {0};
            for (int di = 0; di < 14; ++di) {
                devstates[di] = static_cast<char>('0' +
                    static_cast<int>(emulator_.im2().state(
                        static_cast<Im2Controller::DevIdx>(di))));
            }
            devstates[14] = '\0';
            std::fprintf(g46b_pctrace_file,
                "%ld,%04x,%02x,%02x,%02x,%02x,%04x,%04x,%04x,%04x,%04x,%04x,%04x,%d,%d,%d,%016llx,%d,%d,%d,%d,%s\n",
                g46b_frame_no, r.PC, op0, op1, op2, op3,
                r.AF, r.BC, r.DE, r.HL, r.SP, r.IX, r.IY,
                r.halted ? 1 : 0, r.IM, r.IFF1,
                static_cast<unsigned long long>(fb_hash),
                static_cast<int>(emulator_.dma().state()),
                emulator_.dma().counter(), emulator_.dma().block_length(),
                emulator_.im2().dma_delay() ? 1 : 0, devstates);
            std::fflush(g46b_pctrace_file);
        }
        ++g46b_frame_no;

        // GH #93 — the card change requested above, before the cold-boot polls
        // so a reset raised in this frame boots the new card.
        if (auto sd = emulator_service_sd_card_change(emulator_, config_, config_set_);
            sd && !sd->error.empty())
            exit_code_ = 1;

        if (std::string load_file = emulator_.take_nex_load_request(); !load_file.empty()) {
            guest_cold_boot(load_file);
            continue;
        }

        // Task 70 — a program's NR 0x02 hard reset (set during run_frame) is a
        // power-on cold boot done here between frames. guest_cold_boot()
        // reconstructs the emulator, so continue to the next iteration with the
        // fresh machine.
        if (emulator_.take_hard_reset_request()) {
            guest_cold_boot(std::string());
            continue;
        }

        // GH #276 B4 — SES-03: the backend's service call, once per tick AFTER
        // the frame batch AND after the two cold-boot polls above (CTL-12's
        // ordering: a guest reset raised in this tick's frames is performed
        // before any client command in this pump, so a client's `reset(Hard)`
        // here reboots the freshly booted machine rather than subsuming it). A
        // tick that booted `continue`s, so its pump is the next tick's — still
        // after the boot. `PumpBudget{}` never blocks; the paused-with-a-remote
        // budget is the socket transport's to choose (package T). With no client
        // and no service it writes nothing and moves nothing (row HOST-02).
        pump_hint_ = debugger_->pump(jnext::dbg::PumpBudget{});

        // GH #26 WP4 — a script's verdict (§6.3): `exit n`, a stop or failed
        // assert (3), a run-time error (1). The first code wins, and it never
        // hides an earlier failure behind a 0.
        if (script_host_.exit_requested()) {
            const int code = script_host_.exit_code();
            Log::platform()->info("script requested exit {}", code);
            if (code != 0 || exit_code_ == 0) exit_code_ = code;
            running_ = false;
            continue;
        }

        // --benchmark: stop after exactly N frames and report.
        if (benchmark_frames_ > 0 && ++bench_frames_done >= benchmark_frames_) {
            const double wall =
                std::chrono::duration<double>(bench_clock::now() - bench_start).count();
            print_benchmark_result(wall);
            // The fixture states its own hit rate, so a reader never has to assume
            // the watch fired (§6.2's run 1 assumed it and was wrong).
            if (bench_watch_hits)
                std::fprintf(stderr, "BENCHWATCH hits=%llu\n", *bench_watch_hits);
            running_ = false;
        }

        // Delayed screenshot: the OUTCOME. The pump above has written the
        // capture if a frame was rendered since it was queued; flush_captures()
        // (the backend's exit bound for its CLIENT_NONE captures) says how it
        // ended. The write can fail (missing directory, no permission, disk
        // full). Same contract as the never-taken routes below: a screenshot
        // that was requested and did not appear is an error and a non-zero
        // exit, never a silent status-0 no-op. save_screenshot_*() has already
        // logged WHY; the format is the filename's extension (GH #18).
        //
        // No frame rendered (a paused machine — `--magic-breakpoint`): the
        // capture stays queued and the countdown stays at 0, as the GUI
        // frontends have always deferred it — "deferred to the next rendered
        // frame, never the stale framebuffer" (§4.5 CAP-01). Headless used to
        // write the stale framebuffer here; that is the one change of behaviour
        // O2 brings to this flag, and it is the design's (B4 report).
        if (screenshot_countdown_ == 0) {
            const bool rendered = emulator_.rendered_frames() != screenshot_queued_at_;
            if (screenshot_refused_ || rendered) {
                const bool ok = !screenshot_refused_ &&
                                debugger_->flush_captures(jnext::dbg::CLIENT_NONE) ==
                                    jnext::dbg::Result::Ok;
                if (!ok) {
                    Log::platform()->error(
                        "--delayed-screenshot: FAILED to write '{}' (layers: {}); "
                        "see the error above. Exiting non-zero.",
                        screenshot_file_, Renderer::layer_mask_to_string(screenshot_layers_));
                    exit_code_ = 1;
                }
                screenshot_countdown_ = -1;
                screenshot_queued_    = false;
                screenshot_refused_   = false;
            }
        } else if (screenshot_countdown_ > 0) {
            --screenshot_countdown_;
        }

        // Delayed snapshot save (Task 13b). Format by extension, same
        // dispatch as MainWindow::on_save_snapshot(); "requested but
        // never written" is a loud non-zero-exit failure, same contract
        // as --delayed-screenshot above.
        if (snapshot_countdown_ == 0) {
            // GH #27 S6 (design §10.2 P7) — ALWAYS ADVANCE, NEVER REFUSE.
            // A snapshot may only be taken at a frame boundary, and a
            // debugger break (a magic breakpoint, say) leaves the machine
            // half-way through a frame with run_frame() returning
            // immediately, so the capture would otherwise serialise a frame
            // in flight. Completing it through the ordinary path also keeps
            // the per-scanline change logs intact — re-running frame start
            // mid-frame is the Task 40 defect.
            //
            // GH #276 B4 (O2) — the ACTION is the backend's CAP-04
            // `save_snapshot()`: the same advance (ST-01's one frame-boundary
            // rule, now attributed in a MUTATE line) and the same savers,
            // chosen by extension through the one table (`save_snapshot_file`,
            // src/core). This loop keeps the countdown and its own messages.
            if (!debugger_->at_frame_boundary()) {
                Log::platform()->info(
                    "--delayed-snapshot: the machine was paused mid-frame; "
                    "advanced to the next frame boundary to save from "
                    "(the restored machine is up to one frame on)");
            }
            const bool saved = debugger_->save_snapshot(jnext::dbg::CLIENT_NONE,
                                                        snapshot_file_) ==
                               jnext::dbg::Result::Ok;
            std::string ext;
            auto dot = snapshot_file_.rfind('.');
            if (dot != std::string::npos) {
                ext = snapshot_file_.substr(dot);
                for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            // GH #27 S8 — `.jns` writes itself and reports its own failure with
            // a REASON, which the other formats cannot (and its success line is
            // save_jns_file()'s own).
            if (ext == ".jns") {
                if (!saved) {
                    Log::emulator()->error(
                        "--delayed-snapshot: could not write '{}': {}",
                        snapshot_file_, emulator_.last_jns_error());
                    exit_code_ = 1;
                }   // save_jns_file already logged the success
                snapshot_file_.clear();
                return;
            }
            if (!saved) {
                // GH #274 — a .sna or .szx of a machine the format cannot
                // represent is refused with its reason logged by the saver; an
                // I/O failure likewise. Either way the run fails.
                Log::platform()->error(
                    "--delayed-snapshot: FAILED to write '{}'. Exiting non-zero.",
                    snapshot_file_);
                exit_code_ = 1;
            } else {
                std::ifstream f(snapshot_file_, std::ios::binary | std::ios::ate);
                Log::platform()->info("--delayed-snapshot: saved '{}' ({} bytes)",
                                      snapshot_file_,
                                      f ? static_cast<long long>(f.tellg()) : 0LL);
            }
            snapshot_countdown_ = -1;
        } else if (snapshot_countdown_ > 0) {
            --snapshot_countdown_;
        }

        // Delayed automatic exit. Deferred command-line work it cuts off
        // fails the run (platform/auto_exit.h).
        if (exit_countdown_ == 0) {
            Log::platform()->info("automatic exit triggered");
            std::string keys, nmis;
            for (const auto& k : delayed_keys_) keys += (keys.empty() ? "" : ", ") + k.name;
            for (const auto& n : delayed_nmis_) nmis += (nmis.empty() ? "" : ", ") + n.name;
            std::string cards;
            for (const auto& c : delayed_sd_inserts_) cards += (cards.empty() ? "" : ", ") + c.image;
            if (!auto_exit_finds_no_deferred_work(emulator_, {
                    {"--load", load_file_, load_countdown_ >= 0},
                    {"--inject", inject_file_, inject_countdown_ >= 0},
                    {"--rzx-record", rzx_record_file_,
                     !rzx_record_file_.empty() && !rzx_record_started_},
                    {"--delayed-keypress", keys, !delayed_keys_.empty()},
                    {"--delayed-nmi", nmis, !delayed_nmis_.empty()},
                    {"--delayed-sdcard-insert-frames", cards, !delayed_sd_inserts_.empty()},
                }))
                exit_code_ = 1;
            // GH #26 WP4 (§7.3) — the watchdog fired before a script reached
            // its verdict: an `exit` / `compare_scr` that never ran, or a
            // deferred action still queued, is a failed run, never a 0.
            if (const std::size_t n = script_host_.unreached_verdicts()) {
                Log::platform()->error("SCRIPT: {} deferred actions never ran — exiting 3", n);
                if (exit_code_ == 0) exit_code_ = 3;
            }
            running_ = false;
        } else if (exit_countdown_ > 0) {
            --exit_countdown_;
        }
    }

    if (g46b_pctrace_file) {
        std::fclose(g46b_pctrace_file);
    }
    if (g46b_itrace_file) {
        emulator_.cpu().set_g46b_itrace(nullptr);
        std::fclose(g46b_itrace_file);
    }
    if (g46b_porttrace_file) {
        emulator_.set_g46b_port_trace(nullptr);
        std::fclose(g46b_porttrace_file);
    }
}

void HeadlessApp::shutdown() {
    // Same contract as the two GUI frontends: a screenshot that was asked for
    // and never taken is a failure. The countdown ticks whether or not the
    // machine is paused, so the only way to land in this first branch is a
    // --delayed-automatic-exit that fires before --delayed-screenshot-time /
    // -frames comes due. That misconfiguration used to exit 0 with no PNG and
    // no message — a silent no-op in the one mode built for scripting.
    if (screenshot_countdown_ > 0 && !screenshot_file_.empty()) {
        Log::platform()->error(
            "--delayed-screenshot: NO screenshot was written to '{}' (layers: {}); "
            "--delayed-automatic-exit fired {} frame(s) before the capture was due. "
            "Exiting non-zero.",
            screenshot_file_, Renderer::layer_mask_to_string(screenshot_layers_),
            screenshot_countdown_);
        exit_code_ = 1;
    } else if (screenshot_countdown_ == 0 && !screenshot_file_.empty()) {
        // GH #276 B4 — due, handed to the backend, and never taken: the machine
        // rendered no frame for it (paused) before the exit. The exit bound
        // drops it (flush_captures() → NoFrame), so it cannot land after the
        // verdict. The wording is SdlApp's, which has always had this case.
        if (debugger_) debugger_->flush_captures(jnext::dbg::CLIENT_NONE);
        Log::platform()->error(
            "--delayed-screenshot: NO screenshot was written to '{}' (layers: {}); "
            "the emulator exited with the capture still pending. Exiting non-zero.",
            screenshot_file_, Renderer::layer_mask_to_string(screenshot_layers_));
        exit_code_ = 1;
    }

    // Same "requested but never taken" contract as the screenshot check
    // above (see there for the misconfiguration this catches).
    if (snapshot_countdown_ >= 0 && !snapshot_file_.empty()) {
        Log::platform()->error(
            "--delayed-snapshot: NO snapshot was written to '{}'; "
            "--delayed-automatic-exit fired {} frame(s) before the save "
            "was due. Exiting non-zero.",
            snapshot_file_, snapshot_countdown_);
        exit_code_ = 1;
    }

    // Stop RZX recording if active (writes the file). A command-line recording
    // that did not reach the disk exits non-zero.
    if (!emulator_finish_rzx(emulator_, rzx_record_file_)) exit_code_ = 1;
    Log::platform()->info("Headless mode shutdown");
}
