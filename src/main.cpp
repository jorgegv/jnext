#include "core/log.h"
#include "platform/sdl_app.h"
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <string>

static void crash_handler(int sig) {
    Log::emulator()->critical("signal {} received", sig);
    spdlog::shutdown();
    _Exit(1);
}

static void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "  --log-level SPEC     Set per-subsystem log levels (e.g. cpu=trace,video=warn)\n"
        "  --inject FILE        Load raw binary FILE into RAM (see --inject-org, --inject-pc)\n"
        "  --inject-org ADDR    Load address for --inject (hex, default 8000)\n"
        "  --inject-pc ADDR     Entry point for --inject (hex, default = --inject-org value)\n"
        "  --inject-delay N     Wait N frames before injecting (default 0; use ~100 if the\n"
        "                       binary calls ROM routines that need system variable setup)\n",
        prog);
}

static uint16_t parse_hex16(const char* s) {
    return static_cast<uint16_t>(std::stoul(s, nullptr, 16));
}

int main(int argc, char* argv[]) {
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE,  crash_handler);

    // Initialize all loggers at default level (info).
    Log::init();

    std::string inject_file;
    uint16_t inject_org = 0x8000;
    bool     inject_pc_set = false;
    uint16_t inject_pc  = 0;
    int      inject_delay = 0;

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
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!inject_pc_set) inject_pc = inject_org;

    SdlApp app;
    if (!app.init(argc, argv)) return 1;

    // Set up pending inject (applied after inject_delay frames in the main loop).
    if (!inject_file.empty()) {
        app.set_pending_inject(inject_file, inject_org, inject_pc, inject_delay);
    }

    app.run();
    app.shutdown();
    spdlog::shutdown();
    return 0;
}
