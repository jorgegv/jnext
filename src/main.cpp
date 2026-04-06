#include "core/log.h"
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <string>

#include "platform/headless_app.h"
#ifdef ENABLE_QT_UI
#include "gui/qt_app.h"
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
        "Usage: %s [options]\n"
        "  --log-level SPEC     Set per-subsystem log levels (e.g. cpu=trace,video=warn)\n"
        "  --inject FILE        Load raw binary FILE into RAM (see --inject-org, --inject-pc)\n"
        "  --inject-org ADDR    Load address for --inject (hex, default 8000)\n"
        "  --inject-pc ADDR     Entry point for --inject (hex, default = --inject-org value)\n"
        "  --inject-delay N     Wait N frames before injecting (default 0; use ~100 if the\n"
        "                       binary calls ROM routines that need system variable setup)\n"
        "  --load FILE          Load a program file (auto-detect format by extension)\n"
        "                       Supported: .nex, .sna, .szx, .tap, .tzx\n"
        "  --boot-rom FILE      Load Next boot ROM from FILE (8K FPGA bootloader)\n"
        "  --divmmc-rom FILE    Load DivMMC ROM from FILE (enables DivMMC)\n"
        "  --sd-card FILE       Mount SD card image FILE (.img)\n"
        "  --machine-type TYPE  Machine type: 48k, 128k, plus3, pentagon, next (default)\n"
        "  --roms-directory DIR Directory containing ROM files (default: roms)\n"
        "  --delayed-screenshot FILE   Save a PNG screenshot after a delay\n"
        "  --delayed-screenshot-time N Delay in seconds (default 10)\n"
        "  --delayed-automatic-exit N  Exit the emulator after N seconds\n"
        "  --headless               Run without display/audio (for automated testing)\n"
        "  --tape-realtime          Use real-time tape loading (simulates actual loading speed)\n",
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
    std::string load_file;
    std::string boot_rom;
    std::string divmmc_rom;
    std::string sd_card_image;
    std::string screenshot_file;
    int         screenshot_delay = 10;  // default 10 seconds
    int         auto_exit_delay = -1;   // -1 = disabled
    MachineType machine_type = MachineType::ZXN_ISSUE2;
    bool        machine_type_set = false;
    std::string roms_directory = "/usr/share/fuse";
    bool        headless = false;
    bool        tape_realtime = false;

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
        } else if (arg == "--boot-rom" && i + 1 < argc) {
            boot_rom = argv[++i];
        } else if (arg == "--divmmc-rom" && i + 1 < argc) {
            divmmc_rom = argv[++i];
        } else if (arg == "--sd-card" && i + 1 < argc) {
            sd_card_image = argv[++i];
        } else if (arg == "--delayed-screenshot" && i + 1 < argc) {
            screenshot_file = argv[++i];
        } else if (arg == "--delayed-screenshot-time" && i + 1 < argc) {
            screenshot_delay = std::stoi(argv[++i]);
        } else if (arg == "--delayed-automatic-exit" && i + 1 < argc) {
            auto_exit_delay = std::stoi(argv[++i]);
        } else if (arg == "--machine-type" && i + 1 < argc) {
            if (!parse_machine_type(argv[++i], machine_type)) {
                fprintf(stderr, "Unknown machine type: %s (valid: 48k, 128k, plus3, pentagon, next)\n", argv[i]);
                return 1;
            }
            machine_type_set = true;
        } else if (arg == "--roms-directory" && i + 1 < argc) {
            roms_directory = argv[++i];
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--tape-realtime") {
            tape_realtime = true;
        } else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    if (!inject_pc_set) inject_pc = inject_org;

    // Helper lambda: configure and run any app object with the common interface.
    auto configure_and_run = [&](auto& app) -> int {
        // Configure emulator before init.
        EmulatorConfig cfg;
        cfg.type = machine_type;
        cfg.roms_directory = roms_directory;
        cfg.boot_rom_path = boot_rom;
        cfg.divmmc_rom_path = divmmc_rom;
        cfg.sd_card_image = sd_card_image;
        app.set_config(cfg);

        if (!app.init(argc, argv)) return 1;

        if (!screenshot_file.empty())
            app.set_delayed_screenshot(screenshot_file, screenshot_delay);
        if (auto_exit_delay >= 0)
            app.set_delayed_exit(auto_exit_delay);
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
            if (ext == ".nex" || ext == ".sna" || ext == ".szx") {
                app.set_pending_load(load_file, 0);
            } else if (ext == ".tap" || ext == ".tzx") {
                // Tape loading needs BASIC to be ready; delay ~2s (100 frames at 50Hz)
                app.set_pending_load(load_file, 100);
                app.set_tape_realtime(tape_realtime);
            } else {
                Log::emulator()->error("--load: unsupported file extension '{}' (supported: .nex, .sna, .szx, .tap, .tzx)", ext);
                return 1;
            }
        }

        app.run();
        app.shutdown();
        return 0;
    };

    int result;
    if (headless) {
        HeadlessApp app;
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
