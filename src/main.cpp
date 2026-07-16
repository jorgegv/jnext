#include "core/log.h"
#include "core/sdcard_provisioner.h"
#include "core/video_recorder.h"
#include "video/renderer.h"
#include "version.h"
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>
#include <type_traits>

#ifdef _WIN32
#include "platform/win_console.h"
#endif

#include "platform/headless_app.h"
#ifdef ENABLE_QT_UI
#include "gui/qt_app.h"
#include "gui/sdcard_download_dialog.h"
#include "gui/main_window.h"
#include "gui/app_config.h"
#else
#include "platform/sdl_app.h"
#endif

static void crash_handler(int sig) {
    Log::emulator()->critical("signal {} received", sig);
    spdlog::shutdown();
    _Exit(1);
}

static void print_usage(const char* prog) {
    fprintf(stderr,
        "  jnext is a ZX Spectrum Next emulator.\n"
        "\n"
        "  Usage: %s [options] [file]\n"
        "\n"
        "  [file]               Program to load (NEX, TAP, TZX, SNA, SZX, Z80, WAV, RZX).\n"
        "                       Equivalent to --load FILE, so 'jnext game.tap' just works.\n"
        "  --log-level SPEC     Log levels: a bare level sets all subsystems (e.g. warn),\n"
        "                       name=level sets one (e.g. cpu=trace); mix them, applied\n"
        "                       left to right (e.g. warn,emulator=debug)\n"
        "  --inject FILE        Load raw binary FILE into RAM (see --inject-org, --inject-pc)\n"
        "  --inject-org ADDR    Load address for --inject (hex, default 8000)\n"
        "  --inject-pc ADDR     Entry point for --inject (hex, default = --inject-org value)\n"
        "  --inject-delay N     Wait N frames before injecting (default 0; use ~100 if the\n"
        "                       binary calls ROM routines that need system variable setup)\n"
        "  --load FILE          Load a program file (auto-detect format by extension)\n"
        "                       Supported: .nex, .sna, .szx, .z80, .tap, .tzx, .wav, .rzx\n"
        "                       (.rzx is accepted here and plays back, as --rzx-play)\n"
        "  --sdcard FILE        Mount SD card image FILE (.img). If omitted, jnext falls\n"
        "                       back to ~/.jnext/sdcard/ (offering to download the image).\n"
        "  --sdcard-download-confirm  Skip the download prompt and proceed automatically\n"
        "  --sdcard-download-force    Force re-download + re-patch of the default-location\n"
        "                       image (~/.jnext/sdcard/) to recover a corrupted one. Ignored\n"
        "                       when --sdcard is given (an explicit path always wins).\n"
        "  --machine TYPE       Machine type: 48k, 128k, plus3, next (default)\n"
        "  --delayed-screenshot FILE   Save a PNG screenshot after a delay\n"
        "  --delayed-screenshot-time N Delay in seconds (default 10)\n"
        "  --delayed-screenshot-frames N  Delay in frames (overrides --delayed-screenshot-time)\n"
        "  --delayed-screenshot-layers LIST  Layers to compose into the screenshot:\n"
        "                       a comma-separated list of ula, layer2, sprites, tiles, all\n"
        "                       (default: all). Excluded layers are treated as disabled, so\n"
        "                       the rest still composite per NR 0x15 priority and the NR 0x4A\n"
        "                       fallback colour shows through. Excluding 'ula' also removes\n"
        "                       the border (the ULA draws it). E.g. --delayed-screenshot-layers\n"
        "                       layer2 captures Layer 2 alone; 'ula,sprites' captures both.\n"
        "  --delayed-automatic-exit N  Exit the emulator after N seconds\n"
        "  --delayed-automatic-exit-frames N  Exit after N frames (overrides\n"
        "                       --delayed-automatic-exit)\n"
        "  --delayed-snapshot FILE     Headless-only: save a snapshot after a delay (frames);\n"
        "                       format chosen by FILE's extension (.szx/.nex/other->.sna)\n"
        "  --delayed-snapshot-frames N Delay in frames for --delayed-snapshot (default 0)\n"
        "  --headless               Run without display/audio (for automated testing)\n"
        "  --benchmark N            Headless-only: run exactly N frames uncapped, then\n"
        "                       print one machine-parseable BENCH line (wall s, fps,\n"
        "                       T-states/s, T-states/frame, CPU speed, host core, build\n"
        "                       type) plus a human summary to stdout, and exit\n"
        "  --benchmark-label NAME   Workload label printed verbatim in the BENCH line\n"
        "                       (default: loaded file's basename, or boot-<machine>).\n"
        "                       No whitespace (the BENCH line is space-delimited)\n"
        "  --silent                 Disable all sound output (beeper, AY/YM x3, DAC/\n"
        "                       Covox/Specdrum). No audio device is opened, and the\n"
        "                       emulator skips PSG/mixer sample synthesis entirely —\n"
        "                       can measurably speed up CPU-bound runs. Tape loading\n"
        "                       (EAR input) is unaffected.\n"
        "  --tape-realtime          Use real-time tape loading (simulates actual loading speed)\n"
        "  --tape-save FILE         Append blocks SAVEd via the 48K ROM SA-BYTES routine\n"
        "                       to FILE (.tap). Trap-based (G33 Phase 1): fires when the\n"
        "                       ROM save routine at 0x04C2 runs with ROM paged at slot 0.\n"
        "                       Without this option no SAVE capture happens.\n"
        "  --magic-breakpoint       Enable magic breakpoints (ED FF / DD 01 trigger debugger)\n"
        "  --esxdos-stub            Intercept RST $08 calls; let NEX games without\n"
        "                           NextZXOS-loaded esxdos boot through stubbed file I/O\n"
        "  --magic-port PORT        Enable magic debug port at PORT (hex, e.g. 0x00FF)\n"
        "  --magic-port-mode MODE   Magic port output mode: hex, dec, ascii, line (default: hex)\n"
        "  --record FILE            Record video/audio to FILE (MP4, requires ffmpeg)\n"
        "  --rzx-play FILE         Play back an RZX recording file\n"
        "  --rzx-record FILE       Record input to an RZX file\n"
        "  --speed PERCENT         Emulator speed as %% (50=half, 100=normal, 200=2x, 400=4x)\n"
        "  --rewind-buffer-size N  Number of frame snapshots to store for rewind (default 0=off)\n"
        "  --trace                 Enable the per-instruction trace log (10K-entry ring;\n"
        "                          implied by --rewind-buffer-size N with N>0)\n"
        "  --delayed-keypress SECS KEY  Press KEY after SECS seconds (headless only, repeatable)\n"
        "  --delayed-keypress-frames N KEY  Press KEY after N emulated frames (overrides SECS form)\n"
        "                               KEY (case-insensitive): single char (a-z 0-9 . , ; :),\n"
        "                               ENTER / RETURN / SPACE / UP / DOWN / LEFT / RIGHT,\n"
        "                               or a compound sym+<char> / caps+<char> (e.g. sym+m = '.')\n"
        "  --compositor-trace FILE  Dump per-pixel compositor trace (CSV) for one frame to FILE\n"
        "  --compositor-trace-frame N  Target frame for --compositor-trace (default 250)\n"
        "  --profile               Enable the CPU T-state profiler (Task 21).\n"
        "                          Allocates an mmap'd histogram and accumulates one\n"
        "                          entry per executed instruction. On exit the histogram\n"
        "                          is written to --profile-output. Use\n"
        "                          'tools/get-function-heatmap.pl -m FILE.map' to join\n"
        "                          the output against a z88dk .map file.\n"
        "  --profile-output FILE   Output path for --profile (default: profile.dat)\n"
        "  --rtc \"YYYY-MM-DD HH:MM:SS\"  Pin the RTC to a fixed date/time (frozen clock)\n"
        "                          instead of following the host clock. Makes boot\n"
        "                          screenshots deterministic (regression tests).\n"
        "                          ISO form YYYY-MM-DDTHH:MM:SS also accepted.\n"
        "  --help, -h              Print this help and exit\n"
        "  --version, -V           Print version and exit\n",
        prog);
}

static uint16_t parse_hex16(const char* s) {
    return static_cast<uint16_t>(std::stoul(s, nullptr, 16));
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    // Must run before any stdout/stderr output and before Qt/SDL init.
    win_attach_parent_console();
#endif

    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE,  crash_handler);

    // Initialize all loggers at default level (info).
    Log::init();
    Log::emulator()->info("jnext {}", JNEXT_VERSION_STRING);

    // Packaging Task 67 follow-up: warn once, in every mode (headless
    // included), when ffmpeg is not on PATH — --record / File > Record
    // MPEG4 Video will not work without it.
    if (!VideoRecorder::ffmpeg_available()) {
        Log::emulator()->warn("ffmpeg not found in PATH; MPEG4 video recording will be unavailable.");
    }

    std::string inject_file;
    uint16_t inject_org = 0x8000;
    bool     inject_pc_set = false;
    uint16_t inject_pc  = 0;
    int      inject_delay = 0;
    std::string load_file;
    std::string positional_file;   // bare filename arg; shorthand for --load
    std::string sd_card_image;
    bool        sdcard_download_confirm = false;
    bool        sdcard_download_force   = false;
    std::string screenshot_file;
    int         screenshot_delay = 10;        // seconds (used unless screenshot_delay_frames is set)
    int         screenshot_delay_frames = -1; // -1 = unset; if set, overrides screenshot_delay
    uint8_t     screenshot_layers = Renderer::LAYER_ALL;  // --delayed-screenshot-layers
    bool        screenshot_layers_set = false;
    int         auto_exit_delay = -1;         // seconds; -1 = disabled
    int         auto_exit_delay_frames = -1;  // -1 = unset; if set, overrides auto_exit_delay
    std::string snapshot_file;
    int         snapshot_delay_frames = 0;    // --delayed-snapshot-frames
    MachineType machine_type = MachineType::ZXN_ISSUE2;
    bool        machine_type_set = false;
    std::string machine_arg = "next";  // raw --machine string, for the benchmark label
    bool        headless = false;
    int         benchmark_frames = 0;  // --benchmark N; 0 = disabled
    std::string benchmark_label;       // --benchmark-label; empty = derive
    bool        silent = false;
    bool        tape_realtime = false;
    std::string tape_save_file;
    bool        magic_breakpoint = false;
    bool        esxdos_stub = false;
    bool        magic_port_enabled = false;
    uint16_t    magic_port_address = 0;
    EmulatorConfig::MagicPortMode magic_port_mode = EmulatorConfig::MagicPortMode::HEX;
    std::string record_file;
    std::string rzx_play_file;
    std::string rzx_record_file;
    int         speed_percent = 100;
    bool        speed_percent_set = false;  // Task 66: was --speed given?
    int         rewind_buffer_frames = 0;
    bool        trace_enabled = false;
    std::string compositor_trace_path;
    int         compositor_trace_frame = 250;
    bool        profile_enabled = false;
    std::string profile_output_path = "profile.dat";
    std::string rtc_fixed_arg;
    std::tm     rtc_fixed_tm{};
    struct DelayedKeyArg { int delay; std::string key; bool in_frames; };
    std::vector<DelayedKeyArg> delayed_keys;

    // Parse command-line arguments.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--log-level=", 0) == 0) {
            Log::parse_levels(arg.substr(12));
        } else if (arg == "--log-level" && i + 1 < argc) {
            Log::parse_levels(argv[++i]);
        } else if (arg == "--inject" && i + 1 < argc) {
            inject_file = argv[++i];
        } else if (arg == "--inject-org" && i + 1 < argc) {
            inject_org = parse_hex16(argv[++i]);
        } else if (arg == "--inject-pc" && i + 1 < argc) {
            inject_pc = parse_hex16(argv[++i]);
            inject_pc_set = true;
        } else if (arg == "--inject-delay" && i + 1 < argc) {
            inject_delay = std::stoi(argv[++i]);
        } else if (arg == "--load" && i + 1 < argc) {
            load_file = argv[++i];
        } else if ((arg == "--sdcard" || arg == "--sd-card") && i + 1 < argc) {
            // --sdcard is canonical; --sd-card is a silent (undocumented) alias
            // kept for backward compatibility. Both consume the FILE argument.
            sd_card_image = argv[++i];
        } else if (arg == "--sdcard-download-confirm") {
            sdcard_download_confirm = true;
        } else if (arg == "--sdcard-download-force") {
            sdcard_download_force = true;
        } else if (arg == "--delayed-screenshot" && i + 1 < argc) {
            screenshot_file = argv[++i];
        } else if (arg == "--delayed-screenshot-time" && i + 1 < argc) {
            screenshot_delay = std::stoi(argv[++i]);
        } else if (arg == "--delayed-screenshot-frames" && i + 1 < argc) {
            screenshot_delay_frames = std::stoi(argv[++i]);
        } else if (arg == "--delayed-screenshot-layers" && i + 1 < argc) {
            std::string err;
            if (!Renderer::parse_layer_mask(argv[++i], screenshot_layers, err)) {
                fprintf(stderr, "--delayed-screenshot-layers: %s\n", err.c_str());
                return 1;
            }
            screenshot_layers_set = true;
        } else if (arg == "--delayed-automatic-exit" && i + 1 < argc) {
            auto_exit_delay = std::stoi(argv[++i]);
        } else if (arg == "--delayed-automatic-exit-frames" && i + 1 < argc) {
            auto_exit_delay_frames = std::stoi(argv[++i]);
        } else if (arg == "--delayed-snapshot" && i + 1 < argc) {
            snapshot_file = argv[++i];
        } else if (arg == "--delayed-snapshot-frames" && i + 1 < argc) {
            snapshot_delay_frames = std::stoi(argv[++i]);
        } else if (arg == "--machine" && i + 1 < argc) {
            if (!parse_machine_type(argv[++i], machine_type)) {
                fprintf(stderr, "Unknown machine type: %s (valid: 48k, 128k, plus3, next)\n", argv[i]);
                return 1;
            }
            machine_type_set = true;
            machine_arg = argv[i];
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--benchmark" && i + 1 < argc) {
            benchmark_frames = std::stoi(argv[++i]);
            if (benchmark_frames <= 0) {
                fprintf(stderr, "--benchmark: frame count must be > 0\n");
                return 1;
            }
        } else if (arg == "--benchmark-label" && i + 1 < argc) {
            // Canonical workload name for the BENCH line (Task 27 T1 review):
            // bench.sh passes its workload names through so BENCH lines and
            // baseline files agree; grep-ability across tasks depends on it.
            benchmark_label = argv[++i];
            if (benchmark_label.empty() ||
                benchmark_label.find_first_of(" \t") != std::string::npos) {
                fprintf(stderr, "--benchmark-label: label must be non-empty with no "
                        "whitespace (the BENCH line is space-delimited)\n");
                return 1;
            }
        } else if (arg == "--silent") {
            silent = true;
        } else if (arg == "--tape-realtime") {
            tape_realtime = true;
        } else if (arg == "--tape-save" && i + 1 < argc) {
            tape_save_file = argv[++i];
        } else if (arg == "--magic-breakpoint") {
            magic_breakpoint = true;
        } else if (arg == "--esxdos-stub") {
            esxdos_stub = true;
        } else if (arg == "--magic-port" && i + 1 < argc) {
            magic_port_enabled = true;
            magic_port_address = static_cast<uint16_t>(std::stoul(argv[++i], nullptr, 0));
        } else if (arg == "--magic-port-mode" && i + 1 < argc) {
            std::string mode = argv[++i];
            for (auto& c : mode) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if (mode == "hex") magic_port_mode = EmulatorConfig::MagicPortMode::HEX;
            else if (mode == "dec") magic_port_mode = EmulatorConfig::MagicPortMode::DEC;
            else if (mode == "ascii") magic_port_mode = EmulatorConfig::MagicPortMode::ASCII;
            else if (mode == "line") magic_port_mode = EmulatorConfig::MagicPortMode::LINE;
            else { fprintf(stderr, "Unknown magic port mode: %s (valid: hex, dec, ascii, line)\n", argv[i]); return 1; }
        } else if (arg == "--record" && i + 1 < argc) {
            record_file = argv[++i];
        } else if (arg == "--rzx-play" && i + 1 < argc) {
            rzx_play_file = argv[++i];
        } else if (arg == "--rzx-record" && i + 1 < argc) {
            rzx_record_file = argv[++i];
        } else if (arg == "--speed" && i + 1 < argc) {
            speed_percent = std::stoi(argv[++i]);
            if (speed_percent < 10) speed_percent = 10;
            if (speed_percent > 1000) speed_percent = 1000;
            speed_percent_set = true;
        } else if ((arg == "--delayed-keypress" || arg == "--delayed-keypress-frames")
                   && i + 2 < argc) {
            // SECS form: stored as-is; HeadlessApp converts to frames at
            // run() start using the active machine's framerate.
            // FRAMES form: stored as frames directly.
            const bool in_frames = (arg == "--delayed-keypress-frames");
            int dk_n = std::stoi(argv[++i]);
            std::string dk_key = argv[++i];
            // Key-name parsing (single char, ENTER/SPACE/cursor names,
            // punctuation, sym+X / caps+X compounds) lives in
            // HeadlessApp::set_delayed_keypress, which rejects unknown
            // names loudly (Task 57). Just store the raw name here.
            if (!dk_key.empty()) {
                delayed_keys.push_back({dk_n, dk_key, in_frames});
            }
        } else if (arg == "--rewind-buffer-size" && i + 1 < argc) {
            rewind_buffer_frames = std::stoi(argv[++i]);
            if (rewind_buffer_frames < 0) rewind_buffer_frames = 0;
        } else if (arg == "--trace") {
            trace_enabled = true;
        } else if (arg == "--compositor-trace" && i + 1 < argc) {
            compositor_trace_path = argv[++i];
        } else if (arg == "--compositor-trace-frame" && i + 1 < argc) {
            compositor_trace_frame = std::stoi(argv[++i]);
        } else if (arg == "--profile") {
            profile_enabled = true;
        } else if (arg == "--profile-output" && i + 1 < argc) {
            profile_output_path = argv[++i];
        } else if (arg == "--rtc" && i + 1 < argc) {
            rtc_fixed_arg = argv[++i];
            if (!parse_rtc_datetime(rtc_fixed_arg, rtc_fixed_tm)) {
                fprintf(stderr, "Invalid --rtc value: '%s' (expected \"YYYY-MM-DD HH:MM:SS\" "
                        "or YYYY-MM-DDTHH:MM:SS)\n",
                        rtc_fixed_arg.c_str());
                return 1;
            }
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--version" || arg == "-V") {
            fprintf(stdout, "jnext %s\n", JNEXT_VERSION_STRING);
            return 0;
        } else if (!arg.empty() && arg[0] != '-') {
            // A bare argument is the program to load, so `jnext game.tap` works
            // like `jnext --load game.tap`. Anything starting with '-' is still an
            // unknown option, not a filename — a mistyped flag must not be
            // silently swallowed as a file.
            if (!positional_file.empty()) {
                fprintf(stderr,
                        "Only one file may be given ('%s' and '%s').\n"
                        "Run '%s --help' for a list of available options.\n",
                        positional_file.c_str(), arg.c_str(), argv[0]);
                return 1;
            }
            positional_file = arg;
        } else {
            fprintf(stderr, "Unknown option: %s\nRun '%s --help' for a list of available options.\n", arg.c_str(), argv[0]);
            return 1;
        }
    }

    // A bare filename is shorthand for --load. Giving both is ambiguous — say so
    // rather than silently picking one.
    if (!positional_file.empty()) {
        if (!load_file.empty()) {
            fprintf(stderr,
                    "--load %s and the file argument %s cannot be combined; give only one.\n",
                    load_file.c_str(), positional_file.c_str());
            return 1;
        }
        load_file = positional_file;
    }

    // --delayed-screenshot-layers only means anything with a screenshot to
    // apply it to. Say so instead of quietly doing nothing.
    if (screenshot_layers_set && screenshot_file.empty()) {
        fprintf(stderr, "--delayed-screenshot-layers requires --delayed-screenshot FILE.\n");
        return 1;
    }

    if (!inject_pc_set) inject_pc = inject_org;

    // --benchmark is headless-only (Task 27 T1): the GUI frontends pace to
    // wall clock, so a "benchmark" there would measure the frame timer.
    if (benchmark_frames > 0 && !headless) {
        fprintf(stderr, "--benchmark requires --headless.\n");
        return 1;
    }
    if (!benchmark_label.empty() && benchmark_frames <= 0) {
        fprintf(stderr, "--benchmark-label requires --benchmark N.\n");
        return 1;
    }

    // Task 66 (Configurability) — load saved GUI preferences once, early
    // enough to seed the SD-card resolution below. Only ever done for the
    // real (non-headless) Qt GUI path: HeadlessApp never runs through here
    // with headless==false, and the SDL-only build has no AppConfig at all.
    // CLI always wins: sd_card_path only fills in when --sdcard was NOT
    // given (sd_card_image still empty at this point). The regression suite
    // sets $JNEXT_CONFIG_DIR so this never reads a developer's real
    // ~/.jnext/jnext.conf during automated non-headless runs.
#ifdef ENABLE_QT_UI
    AppConfig gui_app_config;
    if (!headless) {
        gui_app_config.load();
        if (sd_card_image.empty() && !gui_app_config.data().sd_card_path.isEmpty()) {
            sd_card_image = gui_app_config.data().sd_card_path.toStdString();
        }
    }
#endif

    // --sdcard is mandatory at the CLI level: jnext is a ZX Spectrum Next
    // emulator and the SD-card image is the canonical source for all peripheral
    // ROMs (DivMMC, Multiface, NextZXOS) and machine ROMs (48K, 128K, +3) —
    // same as real Next hardware. Unit-test fixtures construct Emulator
    // directly with their own EmulatorConfig and may legitimately leave
    // sd_card_image empty for hermetic tests; that path remains intact (the
    // check lives only here in main.cpp, not in Emulator::init).
    // Resolve the SD-card image (Task 27). Precedence:
    //   1. explicit --sdcard FILE (unless --sdcard-download-force is set)
    //   2. the default location $HOME/.jnext/sdcard/cspect-next-1gb-fixed.img
    //   3. offer to download the canonical distribution image (kept pristine as
    //      cspect-next-1gb.img) and produce the patched cspect-next-1gb-fixed.img
    // Unit-test fixtures construct Emulator directly and never reach this
    // path. When resolution ultimately fails, fall back to the historical
    // "--sdcard is required" error so behaviour is unchanged for scripts.
    {
        sdcard::ProvisionOptions opts;
        opts.explicit_path   = sd_card_image;
        opts.auto_confirm    = sdcard_download_confirm;
        opts.force_download  = sdcard_download_force;
        opts.download        = sdcard::default_http_download;
#ifdef ENABLE_QT_UI
        // GUI helper owns a single temporary QApplication spanning both the
        // confirm prompt and the progress dialog (torn down when it leaves
        // this scope, before QtApp constructs its own QApplication).
        SdcardGuiProvisioner gui_prov;
        if (!headless) {
            opts.confirm  = [&](const std::string& m) { return gui_prov.confirm(m); };
            opts.progress = [&](uint64_t d, uint64_t t) { return gui_prov.progress(d, t); };
        } else {
            opts.confirm  = sdcard::cli_confirm;
            opts.progress = sdcard::cli_progress;
        }
#else
        opts.confirm  = sdcard::cli_confirm;
        opts.progress = sdcard::cli_progress;
#endif
        sdcard::ProvisionResult res = sdcard::provision_sd_card(opts);
        if (res.status == sdcard::ProvisionStatus::Ok) {
            sd_card_image = res.path;
        } else {
            if (!res.error.empty())
                std::fprintf(stderr, "error: SD-card image: %s\n", res.error.c_str());
            std::fprintf(stderr,
                "error: no SD-card image available.\n"
                "jnext is a ZX Spectrum Next emulator and the SD card image is the canonical\n"
                "source for all peripheral ROMs (DivMMC, Multiface, NextZXOS) and machine\n"
                "ROMs (48K, 128K, +3) - same as real hardware. Provide one with --sdcard FILE,\n"
                "or accept the download prompt to install it at ~/.jnext/sdcard/cspect-next-1gb-fixed.img.\n"
                "Use --sdcard-download-confirm to confirm download without asking.\n");
            return 1;
        }
    }

    // Helper lambda: configure and run any app object with the common interface.
    auto configure_and_run = [&](auto& app) -> int {
        // Configure emulator before init.
        EmulatorConfig cfg;
        cfg.type = machine_type;
        cfg.sd_card_image = sd_card_image;
        // Propagate --load path to EmulatorConfig so the boot-ROM auto-load
        // gate (Emulator::init) can distinguish "firmware boot" from
        // "direct NEX/TAP launch": when a --load is present, the embedded
        // nextboot.rom default-load is skipped to avoid corrupting the
        // demo's reset vector at 0x0000-0x1FFF (Wave 0.1 follow-up,
        // 2026-05-04). Pure --sdcard boots leave load_file empty and
        // get the firmware overlay.
        cfg.load_file = load_file;
        cfg.magic_breakpoint = magic_breakpoint;
        cfg.esxdos_stub = esxdos_stub;
        cfg.tape_save_file = tape_save_file;
        cfg.magic_port_enabled = magic_port_enabled;
        cfg.magic_port_address = magic_port_address;
        cfg.magic_port_mode = magic_port_mode;
        cfg.rewind_buffer_frames = rewind_buffer_frames;
        cfg.trace = trace_enabled;
        cfg.compositor_trace_path  = compositor_trace_path;
        cfg.compositor_trace_frame = compositor_trace_frame;
        cfg.profile                = profile_enabled;
        cfg.profile_output_path    = profile_output_path;
        cfg.rtc_fixed              = !rtc_fixed_arg.empty();
        cfg.rtc_fixed_tm           = rtc_fixed_tm;
        cfg.silent                 = silent;

        // Task 66 — saved GUI preferences fill in fields the CLI left at
        // their default; merge_cli_precedence() (src/gui/app_config.h) always
        // prefers the CLI value when it was explicitly provided. `silent` can
        // only ever be set to true by --silent (never explicitly back to
        // false), so `silent` itself doubles as both the "was --silent given"
        // flag and its own CLI value.
#ifdef ENABLE_QT_UI
        if constexpr (std::is_same_v<std::decay_t<decltype(app)>, QtApp>) {
            cfg.type   = merge_cli_precedence(machine_type_set, machine_type,
                                               gui_app_config.data().machine_type);
            cfg.silent = merge_cli_precedence(silent, true, gui_app_config.data().silent);
        }
#endif
        app.set_config(cfg);

        if (!app.init(argc, argv)) return 1;

        // Task 66 — apply the saved GUI preferences that have no CLI
        // competitor (CPU speed, window scale, CRT filter, tape fast-load)
        // now that the main window exists. Machine type/silent were already
        // merged into cfg above (or overridden by the CLI) before init().
        // Emulator speed DOES have a CLI competitor (--speed): resolve it
        // here too (CLI wins when speed_percent_set) so apply_startup_config
        // gets one already-merged value instead of applying the saved one
        // and then having the later --speed handling silently re-override it
        // (or fail to, when --speed happened to repeat the default 100).
#ifdef ENABLE_QT_UI
        if constexpr (std::is_same_v<std::decay_t<decltype(app)>, QtApp>) {
            gui_app_config.data().emulator_speed_percent = merge_cli_precedence(
                speed_percent_set, speed_percent, gui_app_config.data().emulator_speed_percent);
            if (auto* mw = app.main_window()) mw->apply_startup_config(gui_app_config.data());
        }
#endif

        if (!screenshot_file.empty()) {
            int frames = (screenshot_delay_frames >= 0) ? screenshot_delay_frames
                                                        : screenshot_delay * 50;
            app.set_delayed_screenshot(screenshot_file, frames, screenshot_layers);
        }
        // Same shape as --delayed-screenshot above: the frontends count frames,
        // and the -frames form overrides the seconds form when both are given.
        if (auto_exit_delay >= 0 || auto_exit_delay_frames >= 0) {
            int frames = (auto_exit_delay_frames >= 0) ? auto_exit_delay_frames
                                                       : auto_exit_delay * 50;
            app.set_delayed_exit(frames);
        }
        // --delayed-snapshot is headless-only (Task 13b) — HeadlessApp is
        // the only frontend that implements it.
        if constexpr (std::is_same_v<std::decay_t<decltype(app)>, HeadlessApp>) {
            if (!snapshot_file.empty())
                app.set_delayed_snapshot(snapshot_file, snapshot_delay_frames);
        }
        if (!inject_file.empty())
            app.set_pending_inject(inject_file, inject_org, inject_pc, inject_delay);

        // Set up pending load (auto-detect format by extension).
        if (!load_file.empty()) {
            std::string ext;
            auto dot = load_file.rfind('.');
            if (dot != std::string::npos) {
                ext = load_file.substr(dot);
                for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            if (ext == ".rzx") {
                // RZX files are handled via --rzx-play, not --load.
                // But support it here for convenience.
                rzx_play_file = load_file;
            } else if (ext == ".nex" || ext == ".sna" || ext == ".szx" || ext == ".z80") {
                app.set_pending_load(load_file, 0);
            } else if (ext == ".tap") {
                // Task 19: TAP loading uses the phantom typist
                // (src/input/phantom_typist.h) — no boot-delay needed.
                // load_tap() arms the typist, which watches for the
                // first full keyboard scan from the ROM and types
                // LOAD"" the moment BASIC is ready.
                app.set_pending_load(load_file, 0);
                app.set_tape_realtime(tape_realtime);
            } else if (ext == ".tzx") {
                // TZX still uses the legacy immediate-keystroke path
                // (Task 19 scope is .tap only). Keep the 100-frame
                // delay so BASIC has time to reach its prompt.
                app.set_pending_load(load_file, 100);
                app.set_tape_realtime(tape_realtime);
            } else if (ext == ".wav") {
                // WAV is always real-time; needs BASIC delay like tape files
                app.set_pending_load(load_file, 100);
                app.set_tape_realtime(true);
            } else {
                Log::emulator()->error("--load: unsupported file extension '{}' (supported: .nex, .sna, .szx, .z80, .tap, .tzx, .wav, .rzx)", ext);
                return 1;
            }
        }

        // Start video recording if requested (after init, before run).
        if (!record_file.empty()) {
            if (!app.emulator().start_recording(record_file)) {
                Log::emulator()->error("Failed to start recording to {}", record_file);
            }
        }

        // Emulator speed (--speed / saved default) was already resolved and
        // applied via MainWindow::apply_startup_config() above (Task 66).

        // Set up RZX playback or recording.
        if (!rzx_play_file.empty()) {
            app.set_rzx_play(rzx_play_file);
        }
        if (!rzx_record_file.empty()) {
            app.set_rzx_record(rzx_record_file);
        }

        app.run();

        // Stop recording before shutdown (encodes the MP4).
        if (app.emulator().video_recorder().is_recording()) {
            app.emulator().stop_recording();
        }

        app.shutdown();
        // shutdown() decides the status: a --delayed-screenshot that was
        // requested and never written exits non-zero, so a CI script cannot
        // mistake a missing PNG for success.
        return app.exit_code();
    };

    int result;
    if (headless) {
        HeadlessApp app;
        if (benchmark_frames > 0) {
            // Workload label for the BENCH line: --benchmark-label verbatim
            // when given (bench.sh passes its canonical workload names, so
            // BENCH lines and baseline files agree); otherwise derived — the
            // loaded file's basename, or "boot-<machine>" for a bare boot.
            std::string label = benchmark_label;
            if (label.empty()) {
                if (!load_file.empty()) {
                    auto slash = load_file.find_last_of('/');
                    label = (slash == std::string::npos) ? load_file
                                                         : load_file.substr(slash + 1);
                } else {
                    label = "boot-" + machine_arg;
                }
            }
            app.set_benchmark(benchmark_frames, label);
        }
        for (auto& dk : delayed_keys) {
            const bool ok = dk.in_frames
                ? app.set_delayed_keypress(dk.key, dk.delay)
                : app.set_delayed_keypress_seconds(dk.key, dk.delay);
            if (!ok) {
                fprintf(stderr, "Unknown --delayed-keypress key name: '%s'\n"
                        "Valid: single char (a-z 0-9 . , ; :), ENTER, SPACE, "
                        "UP, DOWN, LEFT, RIGHT, sym+<char>, caps+<char>\n",
                        dk.key.c_str());
                return 1;
            }
        }
        result = configure_and_run(app);
    } else {
#ifdef ENABLE_QT_UI
        QtApp app;
        result = configure_and_run(app);
#else
        SdlApp app;
        result = configure_and_run(app);
#endif
    }

    spdlog::shutdown();
    return result;
}
