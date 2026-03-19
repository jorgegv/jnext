#include "core/log.h"
#include "platform/sdl_app.h"
#include <csignal>
#include <cstdlib>

static void crash_handler(int sig) {
    Log::emulator()->critical("signal {} received", sig);
    spdlog::shutdown();
    _Exit(1);
}

int main(int argc, char* argv[]) {
    signal(SIGSEGV, crash_handler);
    signal(SIGABRT, crash_handler);
    signal(SIGFPE,  crash_handler);

    // Initialize all loggers at default level (info).
    Log::init();

    // Parse --log-level argument: --log-level cpu=trace,video=warn,...
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg.rfind("--log-level=", 0) == 0) {
            Log::parse_levels(arg.substr(12));
        } else if (arg == "--log-level" && i + 1 < argc) {
            Log::parse_levels(argv[++i]);
        }
    }

    SdlApp app;
    if (!app.init(argc, argv)) return 1;
    app.run();
    app.shutdown();
    spdlog::shutdown();
    return 0;
}
