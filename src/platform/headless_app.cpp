#include "headless_app.h"
#include "platform/emulator_boot.h"
#include "core/log.h"
#include "core/sna_saver.h"
#include "core/szx_saver.h"
#include "core/nex_saver.h"
#include "input/keyboard.h"
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
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

    running_ = true;
    Log::platform()->info("Headless mode initialized");
    return true;
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

// Map a character to ZX Spectrum keyboard matrix position (row, col).
// Returns false if the key is not recognised.
static bool char_to_matrix(char key, int& row, int& col) {
    // Row 0: SHIFT Z X C V
    // Row 1: A S D F G
    // Row 2: Q W E R T
    // Row 3: 1 2 3 4 5
    // Row 4: 0 9 8 7 6
    // Row 5: P O I U Y
    // Row 6: ENTER L K J H
    // Row 7: SPACE SYM M N B
    switch (key) {
        // digits
        case '1': row=3; col=0; return true;
        case '2': row=3; col=1; return true;
        case '3': row=3; col=2; return true;
        case '4': row=3; col=3; return true;
        case '5': row=3; col=4; return true;
        case '6': row=4; col=4; return true;
        case '7': row=4; col=3; return true;
        case '8': row=4; col=2; return true;
        case '9': row=4; col=1; return true;
        case '0': row=4; col=0; return true;
        // row 1 letters
        case 'a': row=1; col=0; return true;
        case 's': row=1; col=1; return true;
        case 'd': row=1; col=2; return true;
        case 'f': row=1; col=3; return true;
        case 'g': row=1; col=4; return true;
        // row 2 letters
        case 'q': row=2; col=0; return true;
        case 'w': row=2; col=1; return true;
        case 'e': row=2; col=2; return true;
        case 'r': row=2; col=3; return true;
        case 't': row=2; col=4; return true;
        // row 5 letters
        case 'p': row=5; col=0; return true;
        case 'o': row=5; col=1; return true;
        case 'i': row=5; col=2; return true;
        case 'u': row=5; col=3; return true;
        case 'y': row=5; col=4; return true;
        // row 6 letters
        case 'l': row=6; col=1; return true;
        case 'k': row=6; col=2; return true;
        case 'j': row=6; col=3; return true;
        case 'h': row=6; col=4; return true;
        // row 0 letters
        case 'z': row=0; col=1; return true;
        case 'x': row=0; col=2; return true;
        case 'c': row=0; col=3; return true;
        case 'v': row=0; col=4; return true;
        // row 7 letters
        case 'm': row=7; col=2; return true;
        case 'n': row=7; col=3; return true;
        case 'b': row=7; col=4; return true;
        // specials
        case ' ': row=7; col=0; return true;  // SPACE
        case '\n': row=6; col=0; return true;  // ENTER
        default: return false;
    }
}

// Parse a --delayed-keypress key name into matrix positions (Task 57).
// Accepts (case-insensitive): a single alnum char, punctuation with a
// well-known SYMBOL SHIFT compound ('.' ',' ';' ':'), the named keys
// enter/return/space/up/down/left/right (cursors = CAPS SHIFT + 7/6/5/8,
// mirroring the s_compound PC-arrow table in src/input/keyboard.cpp), or
// an explicit compound "sym+<char>" / "caps+<char>".
// Matrix constants: CAPS SHIFT = (0,0), SYMBOL SHIFT = (7,1).
static bool key_name_to_matrix(const std::string& name,
                               int& row1, int& col1, int& row2, int& col2) {
    row2 = col2 = -1;
    std::string k;
    k.reserve(name.size());
    for (char c : name)
        k.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (k.empty()) return false;

    // Named keys.
    if (k == "enter" || k == "return") return char_to_matrix('\n', row1, col1);
    if (k == "space")                  return char_to_matrix(' ', row1, col1);
    if (k == "left")  { row2=0; col2=0; row1=3; col1=4; return true; }  // CAPS + 5
    if (k == "down")  { row2=0; col2=0; row1=4; col1=4; return true; }  // CAPS + 6
    if (k == "up")    { row2=0; col2=0; row1=4; col1=3; return true; }  // CAPS + 7
    if (k == "right") { row2=0; col2=0; row1=4; col1=2; return true; }  // CAPS + 8

    // Explicit compounds: "sym+x" / "caps+x".
    auto plus = k.find('+');
    if (plus != std::string::npos && plus + 2 == k.size()) {
        const std::string mod = k.substr(0, plus);
        const char c = k[plus + 1];
        if (!char_to_matrix(c, row1, col1)) return false;
        if (mod == "sym"  || mod == "ss") { row2=7; col2=1; return true; }
        if (mod == "caps" || mod == "cs") { row2=0; col2=0; return true; }
        return false;
    }

    // Single characters. Punctuation maps to its SYMBOL SHIFT compound
    // (keyword table in the 48K ROM / NextZXOS editor):
    //   '.' = SYM+M   ',' = SYM+N   ';' = SYM+O   ':' = SYM+Z
    if (k.size() == 1) {
        switch (k[0]) {
            case '.': row1=7; col1=2; row2=7; col2=1; return true;  // SYM + M
            case ',': row1=7; col1=3; row2=7; col2=1; return true;  // SYM + N
            case ';': row1=5; col1=1; row2=7; col2=1; return true;  // SYM + O
            case ':': row1=0; col1=1; row2=7; col2=1; return true;  // SYM + Z
            default:  return char_to_matrix(k[0], row1, col1);
        }
    }
    return false;
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

    // Apply RZX play/record at startup.
    if (!rzx_play_file_.empty()) {
        if (!emulator_.load_rzx(rzx_play_file_)) {
            Log::platform()->error("RZX: failed to load '{}'", rzx_play_file_);
        }
    }
    if (!rzx_record_file_.empty()) {
        emulator_.start_rzx_recording(rzx_record_file_);
    }

    // --benchmark timing (Task 27 T1): the clock brackets exactly the
    // benchmark_frames_ run_frame() iterations below — headless already runs
    // uncapped, so wall time here is pure emulation throughput.
    using bench_clock = std::chrono::steady_clock;
    bench_clock::time_point bench_start{};
    int bench_frames_done = 0;
    if (benchmark_frames_ > 0)
        bench_start = bench_clock::now();

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

    // Task 70 — power-on cold boot: reconstruct the emulator in place and
    // re-run the proven startup init() path (shared with the Qt/SDL frontends,
    // platform/emulator_boot.h). Empty load_file => clean NextZXOS boot;
    // non-empty => boot as if launched with --load <file>.
    auto cold_boot = [this](const std::string& load_file) {
        Log::platform()->info("Cold boot (reconstruct + init), load_file='{}'",
                              load_file.empty() ? "(none)" : load_file.c_str());
        EmulatorConfig cfg = config_;
        cfg.load_file = load_file;
        emulator_cold_boot(emulator_, cfg);
        inject_countdown_ = -1;
        load_countdown_   = -1;
        if (!load_file.empty()) {
            load_file_      = load_file;
            load_countdown_ = emulator_load_delay_frames(load_file);
        }
    };

    while (running_) {
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
            else if (t.rfind("loadnex:", 0) == 0) cold_boot(t.substr(8));
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
                Log::platform()->error("--load: failed to load '{}'", load_file_);
                // Task 13b review round 2: a corrupt/rejected .szx reload must
                // set a non-zero exit so a script can tell "loaded fine" from
                // "failed to load, ran anyway" (snapshot-save-func depends on
                // this). Scope the exit to .szx to preserve the prior contract.
                std::string ext;
                auto dot = load_file_.rfind('.');
                if (dot != std::string::npos) {
                    ext = load_file_.substr(dot);
                    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                if (ext == ".szx") exit_code_ = 1;
            }
            load_countdown_ = -1;
        } else if (load_countdown_ > 0) {
            --load_countdown_;
        }

        // Delayed keypresses. Matrix positions were resolved at schedule
        // time (unknown names are rejected there, never dropped here).
        for (auto it = delayed_keys_.begin(); it != delayed_keys_.end(); ) {
            if (it->countdown <= 0) {
                std::vector<Keyboard::AutoKey> keys = {
                    {it->row1, it->col1, it->row2, it->col2, 5}  // press for 5 frames
                };
                emulator_.keyboard().queue_auto_type(keys);
                Log::platform()->info("Delayed keypress '{}' injected", it->name);
                it = delayed_keys_.erase(it);
            } else {
                --it->countdown;
                ++it;
            }
        }

        // Delayed NMI button presses (GH #209). Dispatched through the
        // same hotkey seam the GUI/SDL front-ends use for F9/F10, so
        // every enable gate and the arbitration chain are exercised.
        // One press = one strobe (VHDL hotkey_m1 / hotkey_drive are
        // one-cycle edge pulses, zxnext.vhd:6348-6349), hence erase
        // after firing rather than holding a level down.
        for (auto it = delayed_nmis_.begin(); it != delayed_nmis_.end(); ) {
            if (it->countdown <= 0) {
                if (it->button == NmiButtonName::Mf)
                    emulator_.on_hotkey_f9_mf_nmi();
                else
                    emulator_.on_hotkey_f10_divmmc_nmi();
                Log::platform()->info("Delayed NMI button '{}' pressed", it->name);
                it = delayed_nmis_.erase(it);
            } else {
                --it->countdown;
                ++it;
            }
        }

        // --delayed-screenshot-layers: arm the compositor layer mask for the
        // one frame that is about to be captured, and take it down again
        // immediately after. Default LAYER_ALL => both calls are no-ops.
        if (screenshot_countdown_ == 0)
            emulator_.renderer().set_layer_mask(screenshot_layers_);

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

        if (std::string load_file = emulator_.take_nex_load_request(); !load_file.empty()) {
            cold_boot(load_file);
            continue;
        }

        // Task 70 — a program's NR 0x02 hard reset (set during run_frame) is a
        // power-on cold boot done here between frames. cold_boot() reconstructs
        // the emulator, so continue to the next iteration with the fresh machine.
        if (emulator_.take_hard_reset_request()) {
            cold_boot(std::string());
            continue;
        }

        // --benchmark: stop after exactly N frames and report.
        if (benchmark_frames_ > 0 && ++bench_frames_done >= benchmark_frames_) {
            const double wall =
                std::chrono::duration<double>(bench_clock::now() - bench_start).count();
            print_benchmark_result(wall);
            running_ = false;
        }

        // Delayed screenshot.
        if (screenshot_countdown_ == 0) {
            // The write can fail (missing directory, no permission, disk full).
            // Same contract as the never-taken routes below: a screenshot that
            // was requested and did not appear is an error and a non-zero exit,
            // never a silent status-0 no-op. save_screenshot_png() has already
            // logged WHY.
            if (!save_screenshot_png(screenshot_file_, emulator_.get_framebuffer(),
                                     emulator_.get_framebuffer_width(),
                                     emulator_.get_framebuffer_height())) {
                Log::platform()->error(
                    "--delayed-screenshot: FAILED to write '{}' (layers: {}); "
                    "see the error above. Exiting non-zero.",
                    screenshot_file_, Renderer::layer_mask_to_string(screenshot_layers_));
                exit_code_ = 1;
            }
            emulator_.renderer().set_layer_mask(Renderer::LAYER_ALL);
            screenshot_countdown_ = -1;
        } else if (screenshot_countdown_ > 0) {
            --screenshot_countdown_;
        }

        // Delayed snapshot save (Task 13b). Format by extension, same
        // dispatch as MainWindow::on_save_snapshot(); "requested but
        // never written" is a loud non-zero-exit failure, same contract
        // as --delayed-screenshot above.
        if (snapshot_countdown_ == 0) {
            std::string ext;
            auto dot = snapshot_file_.rfind('.');
            if (dot != std::string::npos) {
                ext = snapshot_file_.substr(dot);
                for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            std::vector<uint8_t> bytes;
            if (ext == ".szx") {
                // .szx is a classic-Spectrum interchange format: it can
                // only represent 48K/128K/+2A/+3 — see SzxSaver class
                // doc-comment SCOPE. save() already logs a clear error and
                // returns no data when the current machine (e.g. jnext's
                // default, Next) can't be represented.
                bytes = SzxSaver::save(emulator_).data;
            } else if (ext == ".nex") {
                bytes = NexSaver::save(emulator_).data;
            } else {
                bytes = SnaSaver::save(emulator_);
            }
            bool ok = !bytes.empty();
            if (ok) {
                std::ofstream f(snapshot_file_, std::ios::binary);
                if (f) {
                    f.write(reinterpret_cast<const char*>(bytes.data()),
                            static_cast<std::streamsize>(bytes.size()));
                    ok = static_cast<bool>(f);
                } else {
                    ok = false;
                }
            }
            if (!ok) {
                Log::platform()->error(
                    "--delayed-snapshot: FAILED to write '{}'. Exiting non-zero.",
                    snapshot_file_);
                exit_code_ = 1;
            } else {
                Log::platform()->info("--delayed-snapshot: saved '{}' ({} bytes)",
                                      snapshot_file_, bytes.size());
            }
            snapshot_countdown_ = -1;
        } else if (snapshot_countdown_ > 0) {
            --snapshot_countdown_;
        }

        // Delayed automatic exit.
        if (exit_countdown_ == 0) {
            Log::platform()->info("automatic exit triggered");
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
    // and never taken is a failure. Headless always renders (there is no
    // debugger pause here), so the only way to land in this branch is a
    // --delayed-automatic-exit that fires before --delayed-screenshot-time /
    // -frames comes due. That misconfiguration used to exit 0 with no PNG and
    // no message — a silent no-op in the one mode built for scripting.
    if (screenshot_countdown_ >= 0 && !screenshot_file_.empty()) {
        Log::platform()->error(
            "--delayed-screenshot: NO screenshot was written to '{}' (layers: {}); "
            "--delayed-automatic-exit fired {} frame(s) before the capture was due. "
            "Exiting non-zero.",
            screenshot_file_, Renderer::layer_mask_to_string(screenshot_layers_),
            screenshot_countdown_);
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

    // Stop RZX recording if active (writes the file).
    if (emulator_.rzx_recorder().is_recording()) {
        emulator_.stop_rzx_recording();
    }
    Log::platform()->info("Headless mode shutdown");
}
