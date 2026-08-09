#include "audio/audio_recorder.h"
#include "audio/dac_trace_recorder.h"
#include "core/cli_options.h"
#include "core/log.h"
#include "core/nex_loader.h"   // probe_version + nex_version_needs_v13_optin (GH #228)
#include "core/sdcard_provisioner.h"
#include "core/video_recorder.h"
#include "esp01/esp_at.h"          // AtEngine::UNASSOCIATED_IP, for --esp-ip-address
#include "peripheral/esp_host_policy.h"
// Issue #35 — audio_pacing::WhenSlowPrefer. Header-only and dependency-free;
// included unconditionally because the parsed value is declared alongside the
// other options, before the frontend type is known.
#include "platform/audio_pacing.h"
#include "video/renderer.h"
#include "version.h"
#include <cctype>
#include <cerrno>
#include <climits>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <stdexcept>
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

// stdout, not stderr (GH #216). This text is only ever printed because the user
// asked for it with --help, which is a successful result and exits 0; POSIX/GNU
// convention puts that on stdout, so `jnext --help > file` and `jnext --help |
// less` work. Usage ERRORS are a different thing and are NOT printed by this
// function — each one prints its own short message and stays on stderr, which is
// where a diagnostic belongs. Keep it that way: making an error path call this
// would move the diagnostic to stdout too.
static void print_usage(const char* prog) {
    // GENERATED FROM cli::OPTIONS — see the Option::help comment in
    // cli_options.h for why. Nothing about the flag set is written here: add a
    // row there and it appears in this output automatically, which is what
    // stopped being true long enough for GH #246's three flags to be missing.
    //
    // Layout: the name column is sized from the widest spelling actually in the
    // table, so it cannot drift out of alignment when a long flag is added.
    fprintf(stdout,
        "  jnext is a ZX Spectrum Next emulator.\n"
        "\n"
        "  Usage: %s [options] [file]\n"
        "\n"
        "  [file]  Program to load (NEX, TAP, TZX, SNA, SZX, Z80, WAV, RZX).\n"
        "          Equivalent to --load FILE, so 'jnext game.tap' just works.\n"
        "\n",
        prog);

    // Widest "name ARGS" among the spellings we actually print, capped so that
    // one very long flag cannot push every description off the screen.
    std::size_t width = 0;
    for (const cli::Option& o : cli::OPTIONS) {
        if (o.doc == cli::Doc::UndocumentedAlias) continue;
        if (o.doc == cli::Doc::ShortAlias) continue;   // printed on its long form
        std::size_t w = std::strlen(o.name);
        if (o.args[0]) w += 1 + std::strlen(o.args);
        if (w > width) width = w;
    }
    const std::size_t MAX_INLINE = 30;
    if (width > MAX_INLINE) width = MAX_INLINE;

    for (const cli::Option& o : cli::OPTIONS) {
        // Hidden by design: an alias that exists only so old scripts keep
        // working must not be advertised (see its row for the reason).
        if (o.doc == cli::Doc::UndocumentedAlias) continue;
        // A short alias is shown ON its long form's line, not as its own entry.
        if (o.doc == cli::Doc::ShortAlias) continue;

        std::string left = o.name;
        if (o.args[0]) { left += ' '; left += o.args; }
        // Append any short alias sharing this id, e.g. "--help, -h".
        for (const cli::Option& a : cli::OPTIONS) {
            if (a.doc == cli::Doc::ShortAlias && a.id == o.id) {
                left += ", ";
                left += a.name;
            }
        }

        // The description, one line at a time, indented under the name column.
        const std::string indent(2 + width + 2, ' ');
        std::string       first_gap;
        if (left.size() <= width) first_gap.assign(width - left.size() + 2, ' ');
        else                      first_gap = "\n" + indent;   // too long: wrap

        const char* p = o.help;
        bool        first = true;
        while (*p) {
            const char* nl = std::strchr(p, '\n');
            const std::string line = nl ? std::string(p, nl) : std::string(p);
            if (first) {
                fprintf(stdout, "  %s%s%s\n", left.c_str(), first_gap.c_str(), line.c_str());
                first = false;
            } else {
                fprintf(stdout, "%s%s\n", indent.c_str(), line.c_str());
            }
            if (!nl) break;
            p = nl + 1;
        }
        if (first) fprintf(stdout, "  %s\n", left.c_str());   // no help text at all
    }
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

    // Where the log goes, and whether it is coloured (GH #235, GH #227).
    //
    // This runs BEFORE Log::init() and before the banner below, and that is the
    // whole point: loggers are built lazily on first use and bound to whatever
    // sink is current at that moment, so a --log-file established after the
    // parse loop would silently drop every line jnext emits while parsing —
    // starting with the version banner. Hence the arity-aware pre-scan of argv
    // rather than a case in the loop; the loop's case below only consumes the
    // value. Applies to every frontend (GUI, SDL, headless): the destination of
    // the log is a logging concern, not a frontend one.
    {
        const auto policy = log_sink_policy::decide(
            cli::prescan_value(argc, argv, cli::OptId::LogFile), std::getenv("NO_COLOR"));
        const Log::SinkResult r = Log::apply_sink_policy(policy);
        if (!r.ok) {
            // Not a log line: the log is precisely what could not be opened.
            std::fprintf(stderr, "jnext: %s\n", r.error.c_str());
            return 1;
        }
    }

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
    std::string nex_cli_args;      // --nex-args: argument line for a V1.3 CLI buffer
    bool     experimental_nex_v13 = false;  // --experimental-nex-v1.3 (GH #228)
    std::string positional_file;   // bare filename arg; shorthand for --load
    std::string sd_card_image;
    bool        sdcard_download_confirm = false;
    bool        sdcard_readonly = false;
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
    bool        persistent_breakpoints = false;
    bool        esxdos_stub = false;
    // GH #25 — emulated ESP-01. `esp_enabled_set` is what makes --no-esp mean
    // something: without it the saved GUI preference could not be told apart
    // from "the user did not ask", and the negation would be unrepresentable.
    bool        esp_enabled = false;
    bool        esp_enabled_set = false;
    // Deduplication and blank rejection live in EspHostPolicy::add, so the CLI
    // and the saved-config reader cannot disagree about what an entry means.
    EspHostPolicy esp_allow;
    bool        esp_allow_set = false;
    // GH #210 — where AT+CIPSERVER binds. Empty means "the user did not say",
    // which leaves EmulatorConfig's loopback default in place; the flag is
    // CLI-only, so there is no saved preference to merge against.
    std::string esp_listen_address;
    // GH #246 — the two edges of a scheduled WiFi outage, in frames. -1 is
    // "never", which is what a run that asks for neither gets.
    int         esp_disassociate_frame = -1;
    int         esp_associate_frame = -1;
    // GH #246 — the station address the module reports. Empty means the
    // module's own synthetic default.
    std::string esp_ip_address;
    bool        magic_port_enabled = false;
    uint16_t    magic_port_address = 0;
    EmulatorConfig::MagicPortMode magic_port_mode = EmulatorConfig::MagicPortMode::HEX;
    std::string record_file;
    std::string wav_record_file;
    std::string dac_trace_file;
    cli::AudioGainArgs audio_gain;
    std::string rzx_play_file;
    std::string rzx_record_file;
    int         speed_percent = 100;
    bool        speed_percent_set = false;  // Task 66: was --speed given?
    // Issue #35 — what a host that cannot emulate in real time sacrifices.
    audio_pacing::WhenSlowPrefer when_slow_prefer =
        audio_pacing::WhenSlowPrefer::Audio;
    bool        when_slow_prefer_set = false;  // was --when-slow-prefer given?
    int         rewind_buffer_frames = 0;
    bool        trace_enabled = false;
    std::string compositor_trace_path;
    int         compositor_trace_frame = 250;
    bool        profile_enabled = false;
    std::string profile_output_path = "profile.dat";
    std::string rtc_fixed_arg;
    std::tm     rtc_fixed_tm{};
    // Task 79 — per-connector host input source (Joy 1 / Joy 2). Defaults Sdl;
    // *_set tracks whether the CLI gave a value (for CLI-wins config merge).
    JoySource   joy_source[2]   = { JoySource::Sdl, JoySource::Sdl };
    bool        joy_source_set[2] = { false, false };
    struct DelayedKeyArg { int delay; std::string key; bool in_frames; };
    std::vector<DelayedKeyArg> delayed_keys;
    struct DelayedNmiArg { int delay; std::string button; bool in_frames; };
    std::vector<DelayedNmiArg> delayed_nmis;

    // Parse command-line arguments.
    //
    // Dispatch is driven by cli::OPTIONS (src/core/cli_options.h), which is the
    // single source of truth for which flags exist and how many values each one
    // takes (issue #43). A flag absent from that table cannot be parsed here, so
    // the table can be diffed against the man page and the two cannot drift.
    //
    // Argument-shortage behaviour is deliberately unchanged: an option whose
    // values run past the end of argv is reported as an unknown option, exactly
    // as the previous `arg == "--x" && i + 1 < argc` chain did.
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        // The one inline-value form, handled ahead of the table lookup.
        if (arg.rfind(cli::INLINE_VALUE_PREFIX, 0) == 0) {
            Log::parse_levels(arg.substr(std::strlen(cli::INLINE_VALUE_PREFIX)));
            continue;
        }

        const cli::Option* opt = cli::find(arg.c_str());
        if (opt && i + opt->arity < argc) {
            // v[0], v[1] are this option's values; advance past them now so the
            // loop's ++i lands on the next argument.
            const char* const* v = &argv[i + 1];
            i += opt->arity;

            switch (opt->id) {
            case cli::OptId::LogLevel:
                Log::parse_levels(v[0]);
                break;
            case cli::OptId::LogFile:
                // Already applied by the pre-scan above — it had to be, to
                // catch the lines emitted before this loop started. The case
                // exists so the value is consumed rather than reported as an
                // unknown argument (-Wswitch requires it too).
                break;
            case cli::OptId::Inject:
                inject_file = v[0];
                break;
            case cli::OptId::InjectOrg:
                inject_org = parse_hex16(v[0]);
                break;
            case cli::OptId::InjectPc:
                inject_pc = parse_hex16(v[0]);
                inject_pc_set = true;
                break;
            case cli::OptId::InjectDelay:
                inject_delay = std::stoi(v[0]);
                break;
            case cli::OptId::Load:
                load_file = v[0];
                break;
            case cli::OptId::NexArgs:
                // Taken verbatim, including spaces and an empty string: the
                // value IS the argument line the guest reads. Whether it can
                // be delivered depends on the NEX header, so the diagnostics
                // live in NexLoader::apply(), which knows the version and the
                // declared buffer (GH #172).
                nex_cli_args = v[0];
                break;
            case cli::OptId::ExperimentalNexV13:
                experimental_nex_v13 = true;
                break;
            case cli::OptId::Sdcard:
                // --sdcard is canonical; --sd-card is a silent (undocumented)
                // alias kept for backward compatibility. Both land here and
                // both consume the FILE argument.
                sd_card_image = v[0];
                break;
            case cli::OptId::SdcardDownloadConfirm:
                sdcard_download_confirm = true;
                break;
            case cli::OptId::SdcardDownloadForce:
                sdcard_download_force = true;
                break;
            case cli::OptId::SdcardReadonly:
                sdcard_readonly = true;
                break;
            case cli::OptId::DelayedScreenshot:
                screenshot_file = v[0];
                break;
            case cli::OptId::DelayedScreenshotTime:
                screenshot_delay = std::stoi(v[0]);
                break;
            case cli::OptId::DelayedScreenshotFrames:
                screenshot_delay_frames = std::stoi(v[0]);
                break;
            case cli::OptId::DelayedScreenshotLayers: {
                std::string err;
                if (!Renderer::parse_layer_mask(v[0], screenshot_layers, err)) {
                    fprintf(stderr, "--delayed-screenshot-layers: %s\n", err.c_str());
                    return 1;
                }
                screenshot_layers_set = true;
                break;
            }
            case cli::OptId::DelayedAutomaticExit:
                auto_exit_delay = std::stoi(v[0]);
                break;
            case cli::OptId::DelayedAutomaticExitFrames:
                auto_exit_delay_frames = std::stoi(v[0]);
                break;
            case cli::OptId::DelayedSnapshot:
                snapshot_file = v[0];
                break;
            case cli::OptId::DelayedSnapshotFrames:
                snapshot_delay_frames = std::stoi(v[0]);
                break;
            case cli::OptId::Machine:
                if (!parse_machine_type(v[0], machine_type)) {
                    fprintf(stderr, "Unknown machine type: %s (valid: 48k, 128k, plus3, next)\n", v[0]);
                    return 1;
                }
                machine_type_set = true;
                machine_arg = v[0];
                break;
            case cli::OptId::Headless:
                headless = true;
                break;
            case cli::OptId::Benchmark:
                benchmark_frames = std::stoi(v[0]);
                if (benchmark_frames <= 0) {
                    fprintf(stderr, "--benchmark: frame count must be > 0\n");
                    return 1;
                }
                break;
            case cli::OptId::BenchmarkLabel:
                // Canonical workload name for the BENCH line (Task 27 T1 review):
                // bench.sh passes its workload names through so BENCH lines and
                // baseline files agree; grep-ability across tasks depends on it.
                benchmark_label = v[0];
                if (benchmark_label.empty() ||
                    benchmark_label.find_first_of(" \t") != std::string::npos) {
                    fprintf(stderr, "--benchmark-label: label must be non-empty with no "
                            "whitespace (the BENCH line is space-delimited)\n");
                    return 1;
                }
                break;
            case cli::OptId::Silent:
                silent = true;
                break;
            case cli::OptId::TapeRealtime:
                tape_realtime = true;
                break;
            case cli::OptId::TapeSave:
                tape_save_file = v[0];
                break;
            case cli::OptId::MagicBreakpoint:
                magic_breakpoint = true;
                break;
            case cli::OptId::PersistentBreakpoints:
                persistent_breakpoints = true;
                break;
            case cli::OptId::EsxdosStub:
                esxdos_stub = true;
                break;
            // --esp / --no-esp: last one wins, like any other repeated flag.
            case cli::OptId::Esp:
                esp_enabled = true;
                esp_enabled_set = true;
                break;
            case cli::OptId::NoEsp:
                esp_enabled = false;
                esp_enabled_set = true;
                break;
            case cli::OptId::EspAllow:
                // ONE host per occurrence; repeat the option for more. A comma
                // is rejected rather than taken literally, because taking it
                // literally is the dangerous reading: `--esp-allow a.com,b.com`
                // would be stored as the single unmatchable host name
                // "a.com,b.com", every connection would be refused, and the
                // user would be looking at an allowlist that appears to list
                // the two hosts they wanted. The mistake is invited by the
                // config file, whose `allowed_hosts` IS comma-separated (it is
                // a QSettings string list), so the two formats must not fail
                // silently at each other. No legal host name contains a comma.
                if (std::strchr(v[0], ',') != nullptr) {
                    fprintf(stderr,
                            "--esp-allow: HOST must be a single host name; commas are not "
                            "separators here.\n"
                            "  Repeat the option instead: --esp-allow a.example "
                            "--esp-allow b.example\n");
                    return 1;
                }
                // A blank entry is rejected rather than dropped: a silently
                // ignored allowlist entry would leave the user believing a
                // restriction is in force that is not.
                if (!esp_allow.add(v[0])) {
                    fprintf(stderr, "--esp-allow: HOST must not be empty.\n");
                    return 1;
                }
                esp_allow_set = true;
                break;
            case cli::OptId::EspListenAddress: {
                // Validated HERE rather than at bind time, because a typo in a
                // security control must be a usage error the user sees at once,
                // not an `ERROR` the guest gets minutes later with no way to
                // tell it from a port already in use.
                esp::IpAddress parsed;
                if (!esp::parse_ip(v[0], parsed)) {
                    fprintf(stderr,
                            "--esp-listen-address: ADDR must be a numeric IP address "
                            "(e.g. 127.0.0.1 or 0.0.0.0), not \"%s\".\n"
                            "  A name is rejected on purpose: a bind address resolved "
                            "through DNS could change under you.\n",
                            v[0]);
                    return 1;
                }
                esp_listen_address = v[0];
                break;
            }
            case cli::OptId::EspDelayedDisassociateFrames:
            case cli::OptId::EspDelayedAssociateFrames: {
                // Parsed here rather than stored raw, because a schedule that
                // silently does nothing is the failure this option exists to
                // avoid: the whole point is a bench that can execute the
                // guest's "the address went away" path, and a run that never
                // reached the edge would report a green pass for a path it
                // never took. A negative frame is refused for the same reason
                // (there is no frame before the first one), and so is a value
                // with trailing junk — `10s` reading as frame 10 would be a
                // different outage than the one asked for.
                const bool down = (opt->id == cli::OptId::EspDelayedDisassociateFrames);
                char* end = nullptr;
                errno = 0;
                const long n = std::strtol(v[0], &end, 10);
                if (errno != 0 || end == v[0] || *end != '\0' || n < 0 || n > INT_MAX) {
                    fprintf(stderr,
                            "%s: N must be a non-negative frame number, not \"%s\".\n",
                            arg.c_str(), v[0]);
                    return 1;
                }
                (down ? esp_disassociate_frame : esp_associate_frame) = static_cast<int>(n);
                break;
            }
            case cli::OptId::EspIpAddress: {
                // Numeric only, and validated HERE for the same reason
                // --esp-listen-address is: a typo must be a usage error the
                // user sees at once, not a malformed string the guest is
                // handed minutes later inside an otherwise-normal AT reply.
                // A NAME is refused too — this is what the module CLAIMS its
                // address is, and a claim that depends on DNS is one that
                // could differ between two runs of the same command.
                esp::IpAddress parsed;
                if (!esp::parse_ip(v[0], parsed)) {
                    fprintf(stderr,
                            "--esp-ip-address: ADDR must be a numeric IP address "
                            "(e.g. 192.168.1.50), not \"%s\".\n",
                            v[0]);
                    return 1;
                }
                // 0.0.0.0 is the address the module reports while it is OFF
                // its network (GH #246). Accepting it as the configured one
                // would make "associated" and "not associated" indis-
                // tinguishable to the guest, which is the single observation
                // this whole feature exists to make.
                //
                // Compared NUMERICALLY, not as text: `000.000.000.000` and
                // `0.0.0.0` are the same address, and a string compare accepts
                // the first (review finding, GH #246).
                esp::IpAddress unassociated;
                esp::parse_ip(esp::AtEngine::UNASSOCIATED_IP, unassociated);
                if (parsed == unassociated) {
                    fprintf(stderr,
                            "--esp-ip-address: %s is what the module reports while it is "
                            "NOT associated, so it cannot also be its address.\n",
                            esp::AtEngine::UNASSOCIATED_IP);
                    return 1;
                }
                esp_ip_address = v[0];
                break;
            }
            case cli::OptId::MagicPort:
                magic_port_enabled = true;
                magic_port_address = static_cast<uint16_t>(std::stoul(v[0], nullptr, 0));
                break;
            case cli::OptId::MagicPortMode: {
                std::string mode = v[0];
                for (auto& c : mode) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (mode == "hex") magic_port_mode = EmulatorConfig::MagicPortMode::HEX;
                else if (mode == "dec") magic_port_mode = EmulatorConfig::MagicPortMode::DEC;
                else if (mode == "ascii") magic_port_mode = EmulatorConfig::MagicPortMode::ASCII;
                else if (mode == "line") magic_port_mode = EmulatorConfig::MagicPortMode::LINE;
                else { fprintf(stderr, "Unknown magic port mode: %s (valid: hex, dec, ascii, line)\n", v[0]); return 1; }
                break;
            }
            case cli::OptId::Record:
                record_file = v[0];
                break;
            case cli::OptId::WavRecord:
                wav_record_file = v[0];
                break;
            case cli::OptId::DacTrace:
                dac_trace_file = v[0];
                break;
            case cli::OptId::AudioGainDb:
            case cli::OptId::AudioGainBeeperDb:
            case cli::OptId::AudioGainAy0Db:
            case cli::OptId::AudioGainAy1Db:
            case cli::OptId::AudioGainAy2Db:
            case cli::OptId::AudioGainDacDb: {
                const std::string value = v[0];
                float parsed = 0.0f;
                try {
                    size_t consumed = 0;
                    parsed = std::stof(value, &consumed);
                    if (consumed != value.size() || !std::isfinite(parsed) ||
                        parsed < -24.0f || parsed > 24.0f) {
                        throw std::invalid_argument("range");
                    }
                } catch (const std::exception&) {
                    fprintf(stderr, "%s: expected a number from -24 to +24 dB\n",
                            arg.c_str());
                    return 1;
                }
                (void)cli::assign_audio_gain(opt->id, parsed, audio_gain);
                break;
            }
            case cli::OptId::RzxPlay:
                rzx_play_file = v[0];
                break;
            case cli::OptId::RzxRecord:
                rzx_record_file = v[0];
                break;
            case cli::OptId::Speed:
                speed_percent = std::stoi(v[0]);
                if (speed_percent < 10) speed_percent = 10;
                if (speed_percent > 1000) speed_percent = 1000;
                speed_percent_set = true;
                break;
            case cli::OptId::WhenSlowPrefer: {
                std::string what = v[0];
                for (auto& c : what)
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (what == "audio") {
                    when_slow_prefer = audio_pacing::WhenSlowPrefer::Audio;
                } else if (what == "video") {
                    when_slow_prefer = audio_pacing::WhenSlowPrefer::Video;
                } else {
                    fprintf(stderr,
                            "Unknown --when-slow-prefer value: %s (valid: audio, video)\n",
                            v[0]);
                    return 1;
                }
                when_slow_prefer_set = true;
                break;
            }
            case cli::OptId::Joy1Source:
            case cli::OptId::Joy2Source: {
                const int idx = (opt->id == cli::OptId::Joy1Source) ? 0 : 1;
                JoySource src;
                if (!parse_joy_source(v[0], src)) {
                    std::fprintf(stderr, "Invalid %s value '%s' (expected 'sdl' or 'keys')\n",
                                 arg.c_str(), v[0]);
                    return 1;
                }
                joy_source[idx]     = src;
                joy_source_set[idx] = true;
                break;
            }
            case cli::OptId::DelayedKeypress:
            case cli::OptId::DelayedKeypressFrames: {
                // SECS form: stored as-is; HeadlessApp converts to frames at
                // run() start using the active machine's framerate.
                // FRAMES form: stored as frames directly.
                const bool in_frames = (opt->id == cli::OptId::DelayedKeypressFrames);
                int dk_n = std::stoi(v[0]);
                std::string dk_key = v[1];
                // Key-name parsing (single char, ENTER/SPACE/cursor names,
                // punctuation, sym+X / caps+X compounds) lives in
                // HeadlessApp::set_delayed_keypress, which rejects unknown
                // names loudly (Task 57). Just store the raw name here.
                if (!dk_key.empty()) {
                    delayed_keys.push_back({dk_n, dk_key, in_frames});
                }
                break;
            }
            case cli::OptId::DelayedNmi:
            case cli::OptId::DelayedNmiFrames: {
                // Same two-form shape as --delayed-keypress: SECS is
                // converted to frames by HeadlessApp at run() start
                // using the active machine's framerate; FRAMES is
                // stored directly. Button-name parsing (mf/m1 vs
                // divmmc/drive) lives in HeadlessApp::set_delayed_nmi,
                // which rejects unknown names loudly. Store the raw
                // name here (GH #209).
                const bool in_frames = (opt->id == cli::OptId::DelayedNmiFrames);
                int dn_n = std::stoi(v[0]);
                // Queued UNCONDITIONALLY, including an empty name: the
                // parser below rejects what it does not recognise, and an
                // empty BUTTON is exactly such a name. Skipping it here
                // instead would drop the press silently, which is the one
                // outcome this option must never produce.
                delayed_nmis.push_back({dn_n, v[1], in_frames});
                break;
            }
            case cli::OptId::RewindBufferSize:
                rewind_buffer_frames = std::stoi(v[0]);
                if (rewind_buffer_frames < 0) rewind_buffer_frames = 0;
                break;
            case cli::OptId::Trace:
                trace_enabled = true;
                break;
            case cli::OptId::CompositorTrace:
                compositor_trace_path = v[0];
                break;
            case cli::OptId::CompositorTraceFrame:
                compositor_trace_frame = std::stoi(v[0]);
                break;
            case cli::OptId::Profile:
                profile_enabled = true;
                break;
            case cli::OptId::ProfileOutput:
                profile_output_path = v[0];
                break;
            case cli::OptId::Rtc:
                rtc_fixed_arg = v[0];
                if (!parse_rtc_datetime(rtc_fixed_arg, rtc_fixed_tm)) {
                    fprintf(stderr, "Invalid --rtc value: '%s' (expected \"YYYY-MM-DD HH:MM:SS\" "
                            "or YYYY-MM-DDTHH:MM:SS)\n",
                            rtc_fixed_arg.c_str());
                    return 1;
                }
                break;
            case cli::OptId::Help:
                print_usage(argv[0]);
                return 0;
            case cli::OptId::Version:
                fprintf(stdout, "jnext %s\n", JNEXT_VERSION_STRING);
                return 0;
            }
        } else if (!opt && !arg.empty() && arg[0] != '-') {
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
    if (silent && !wav_record_file.empty()) {
        fprintf(stderr,
                "--wav-record cannot be combined with --silent because --silent disables mixer synthesis.\n");
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
#ifdef _WIN32
    // Past this point the process is no longer a quick CLI command: SD-card
    // provisioning can sit in a confirm prompt or a download for minutes, and
    // everything after it is the app session. Disarm the GH #212 completion
    // signal here — every exit it exists for (--help, --version, unknown
    // option, argument validation) has already happened during parsing,
    // milliseconds ago (see win_console.h).
    win_console_note_app_running();
#endif
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
            opts.busy     = [&](const std::string& p, const std::function<bool()>& w) {
                return gui_prov.busy(p, w);
            };
        } else {
            opts.confirm  = sdcard::cli_confirm;
            opts.progress = sdcard::cli_progress;
            opts.busy     = sdcard::cli_busy;
        }
#else
        opts.confirm  = sdcard::cli_confirm;
        opts.progress = sdcard::cli_progress;
        opts.busy     = sdcard::cli_busy;
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

    // Task 79 — --joyN-source only has an effect in the interactive frontends
    // (SDL / Qt); headless has no host input dispatcher, so warn loudly rather
    // than silently accepting an inert flag.
    if (headless && (joy_source_set[0] || joy_source_set[1])) {
        std::fprintf(stderr,
            "warning: --joy1-source/--joy2-source have no effect with --headless "
            "(no host joystick input in headless mode); ignoring.\n");
    }

    // Issue #35 — same shape: the degradation policy only means anything where
    // emulation is paced against real time. Headless is uncapped and opens no
    // audio device, so there is nothing to trade off and nothing to degrade.
    if (headless && when_slow_prefer_set) {
        std::fprintf(stderr,
            "warning: --when-slow-prefer has no effect with --headless "
            "(headless runs uncapped with no audio device); ignoring.\n");
    }

    // Task 79 — cursor keys can drive only one connector.
    if (joy_source[0] == JoySource::CursorKeys && joy_source[1] == JoySource::CursorKeys) {
        std::fprintf(stderr,
            "error: --joy1-source and --joy2-source cannot both be 'keys' "
            "(the host cursor keys can drive only one connector).\n");
        return 1;
    }
    AudioRecorder audio_recorder;
    DacTraceRecorder dac_trace_recorder;

    // Helper lambda: configure and run any app object with the common interface.
    auto configure_and_run = [&](auto& app) -> int {
        // Configure emulator before init.
        EmulatorConfig cfg;
        cfg.type = machine_type;
        cfg.sd_card_image = sd_card_image;
        cfg.sd_card_readonly = sdcard_readonly;
        // Propagate --load path to EmulatorConfig so the boot-ROM auto-load
        // gate (Emulator::init) can distinguish "firmware boot" from
        // "direct NEX/TAP launch": when a --load is present, the embedded
        // nextboot.rom default-load is skipped to avoid corrupting the
        // demo's reset vector at 0x0000-0x1FFF (Wave 0.1 follow-up,
        // 2026-05-04). Pure --sdcard boots leave load_file empty and
        // get the firmware overlay.
        cfg.load_file = load_file;
        cfg.nex_cli_args = nex_cli_args;
        cfg.allow_experimental_nex_v13 = experimental_nex_v13;
        cfg.magic_breakpoint = magic_breakpoint;
        cfg.persistent_breakpoints = persistent_breakpoints;
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
        cfg.audio_gain_db          = audio_gain.master;
        cfg.audio_gain_beeper_db   = audio_gain.beeper;
        for (int chip = 0; chip < 3; ++chip)
            cfg.audio_gain_ay_db[chip] = audio_gain.ay[chip];
        cfg.audio_gain_dac_db      = audio_gain.dac;
        cfg.joy_source[0]          = joy_source[0];   // Task 79 (CLI value)
        cfg.joy_source[1]          = joy_source[1];
        cfg.esp_enabled            = esp_enabled;     // GH #25 (CLI value)
        cfg.esp_allowed_hosts      = esp_allow.allowed_hosts;
        // GH #210. No merge and no saved form: an unset flag leaves the
        // config's own loopback default, which is the safe value.
        if (!esp_listen_address.empty()) cfg.esp_listen_address = esp_listen_address;
        // GH #246. CLI-only, like the listen address: an outage that could
        // arrive from a config file is one the user cannot see in the command
        // they ran, and every consumer of this is a scripted bench anyway.
        cfg.esp_disassociate_frame = esp_disassociate_frame;
        cfg.esp_associate_frame    = esp_associate_frame;
        cfg.esp_ip_address         = esp_ip_address;

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
            cfg.audio_gain_db = merge_cli_precedence(
                audio_gain.master_set, audio_gain.master,
                gui_app_config.data().audio_gain_db);
            cfg.audio_gain_beeper_db = merge_cli_precedence(
                audio_gain.beeper_set, audio_gain.beeper,
                gui_app_config.data().audio_gain_beeper_db);
            for (int chip = 0; chip < 3; ++chip)
                cfg.audio_gain_ay_db[chip] = merge_cli_precedence(
                    audio_gain.ay_set[chip], audio_gain.ay[chip],
                    gui_app_config.data().audio_gain_ay_db[chip]);
            cfg.audio_gain_dac_db = merge_cli_precedence(
                audio_gain.dac_set, audio_gain.dac,
                gui_app_config.data().audio_gain_dac_db);
            // Task 79 — per-connector input source: CLI wins, else saved config.
            cfg.joy_source[0] = merge_cli_precedence(joy_source_set[0], joy_source[0],
                                                     gui_app_config.data().joy_source[0]);
            cfg.joy_source[1] = merge_cli_precedence(joy_source_set[1], joy_source[1],
                                                     gui_app_config.data().joy_source[1]);
            // The merged pair could disagree with the one-cursor rule (e.g.
            // saved keys on Joy 2 + CLI keys on Joy 1); resolve to CLI intent
            // by keeping the CLI-provided connector and reverting the other.
            if (cfg.joy_source[0] == JoySource::CursorKeys &&
                cfg.joy_source[1] == JoySource::CursorKeys) {
                cfg.joy_source[joy_source_set[0] ? 1 : 0] = JoySource::Sdl;
            }
            // GH #25 — the ESP. CLI wins in BOTH directions: --esp turns it on
            // when the preference is off, --no-esp turns it off when the
            // preference is on. The allowlist is REPLACED rather than merged
            // when the CLI gives any --esp-allow, because a merge would mean a
            // saved entry the user cannot get rid of from the command line.
            cfg.esp_enabled = merge_cli_precedence(esp_enabled_set, esp_enabled,
                                                   gui_app_config.data().esp_enabled);
            cfg.esp_allowed_hosts = merge_cli_precedence(
                esp_allow_set, esp_allow.allowed_hosts,
                gui_app_config.data().esp_allowed_hosts);
        }
#endif
        // An allowlist with nothing to restrict is a user error worth naming:
        // it reads as "I have restricted the ESP" when in fact the ESP is off.
        if (esp_allow_set && !cfg.esp_enabled) {
            fprintf(stderr, "--esp-allow requires the ESP to be enabled (--esp).\n");
            return 1;
        }
        // Same reasoning: a bind address given for a module that is off reads
        // as "I have configured where it listens" when nothing will listen.
        if (!esp_listen_address.empty() && !cfg.esp_enabled) {
            fprintf(stderr, "--esp-listen-address requires the ESP to be enabled (--esp).\n");
            return 1;
        }
        // GH #246, and the same reasoning a third time: a scheduled outage for
        // a module that is off is a user who believes their bench exercised
        // the disconnected path when nothing was ever connected.
        if (!esp_ip_address.empty() && !cfg.esp_enabled) {
            fprintf(stderr, "--esp-ip-address requires the ESP to be enabled (--esp).\n");
            return 1;
        }
        if ((esp_disassociate_frame >= 0 || esp_associate_frame >= 0) && !cfg.esp_enabled) {
            fprintf(stderr,
                    "--esp-delayed-disassociate-frames / --esp-delayed-associate-frames "
                    "require the ESP to be enabled (--esp).\n");
            return 1;
        }
        // The module STARTS associated, so an association with nothing to undo
        // changes nothing, and a re-association at or before the outage means
        // there is no outage. Both spell a bench that reports a pass for a
        // path it never took, which is the one outcome this feature must not
        // produce — so both are refused rather than run.
        if (esp_associate_frame >= 0 && esp_disassociate_frame < 0) {
            fprintf(stderr,
                    "--esp-delayed-associate-frames needs an outage to end: give "
                    "--esp-delayed-disassociate-frames too (the module starts associated).\n");
            return 1;
        }
        if (esp_associate_frame >= 0 && esp_associate_frame <= esp_disassociate_frame) {
            fprintf(stderr,
                    "--esp-delayed-associate-frames N must be GREATER than "
                    "--esp-delayed-disassociate-frames M (%d <= %d leaves no outage).\n",
                    esp_associate_frame, esp_disassociate_frame);
            return 1;
        }
        if (cfg.silent && !wav_record_file.empty()) {
            fprintf(stderr,
                    "--wav-record cannot be used while audio is disabled in preferences.\n");
            return 1;
        }
        if (!wav_record_file.empty()) {
            if (!audio_recorder.start(wav_record_file)) {
                Log::audio()->error("Failed to start WAV recording: {}",
                                    audio_recorder.last_error());
                return 1;
            }
            cfg.audio_capture_callback = [&audio_recorder](const int16_t* samples,
                                                           int count) {
                audio_recorder.capture(samples, count);
            };
        }
        if (!dac_trace_file.empty()) {
            if (!dac_trace_recorder.start(dac_trace_file)) {
                Log::audio()->error("Failed to start DAC trace: {}",
                                    dac_trace_recorder.last_error());
                audio_recorder.stop();
                return 1;
            }
            cfg.dac_write_callback = [&dac_trace_recorder]
                                     (uint64_t tstate, int channel, uint8_t value) {
                dac_trace_recorder.capture(tstate, channel, value);
            };
        }
        app.set_config(cfg);

        if (!app.init(argc, argv)) {
            audio_recorder.stop();
            dac_trace_recorder.stop();
            return 1;
        }

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
            // Preserve CLI precedence when the common live-apply path below
            // configures the mixer from AppConfigData.
            gui_app_config.data().audio_gain_db = cfg.audio_gain_db;
            gui_app_config.data().audio_gain_beeper_db = cfg.audio_gain_beeper_db;
            for (int chip = 0; chip < 3; ++chip)
                gui_app_config.data().audio_gain_ay_db[chip] =
                    cfg.audio_gain_ay_db[chip];
            gui_app_config.data().audio_gain_dac_db = cfg.audio_gain_dac_db;
            // Issue #35 — the degradation policy has a CLI competitor too, and
            // is resolved the same way. It is NOT carried in EmulatorConfig:
            // the emulated machine has no opinion about which of picture and
            // sound the host sacrifices, so it is a frontend knob like --speed
            // and --tape-realtime. apply_startup_config() pushes the merged
            // value into the frame sequencer.
            gui_app_config.data().when_slow_prefer = merge_cli_precedence(
                when_slow_prefer_set, when_slow_prefer,
                gui_app_config.data().when_slow_prefer);
            if (auto* mw = app.main_window()) mw->apply_startup_config(gui_app_config.data());
        }
#else
        // The SDL frontend has no Preferences dialog and no saved config, so
        // the CLI value is the whole story.
        if constexpr (std::is_same_v<std::decay_t<decltype(app)>, SdlApp>) {
            app.set_when_slow_prefer(when_slow_prefer);
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
            } else if (ext == ".nex") {
                // GH #228 — NEX V1.3 is an experimental format jnext does not
                // officially support: refuse it up front, with exit 1, unless
                // --experimental-nex-v1.3 was given. The probe reads only the
                // 8-byte magic+version prologue; a file it cannot read or that
                // is not NEX at all falls through, and the loader's own error
                // reports the real problem when the pending load fires.
                // Emulator::load_nex() enforces the same policy as a backstop.
                if (!experimental_nex_v13) {
                    uint8_t ver_bcd = 0;
                    char    ver[5]  = {0};
                    if (NexLoader::probe_version(load_file, ver_bcd, ver) &&
                        nex_version_needs_v13_optin(ver_bcd)) {
                        fprintf(stderr,
                                "--load: '%s' is NEX version %s — NEX V1.3 is an experimental "
                                "format and is not supported in any way.\n"
                                "Pass --experimental-nex-v1.3 to load it anyway (unsupported; "
                                "V1.0-V1.2 files load normally).\n",
                                load_file.c_str(), ver);
                        audio_recorder.stop();
                        dac_trace_recorder.stop();
                        return 1;
                    }
                }
                app.set_pending_load(load_file, 0);
            } else if (ext == ".sna" || ext == ".szx" || ext == ".z80") {
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
                audio_recorder.stop();
                dac_trace_recorder.stop();
                return 1;
            }
        }

        // Capture requests are part of the CLI contract: if one cannot start
        // or finalize, an otherwise-clean run must not report success.
        bool capture_ok = true;

        // Start video recording if requested (after init, before run).
        // A start failure aborts the run RIGHT HERE (GH #86), exactly like
        // the --wav-record/--dac-trace start blocks above: running the whole
        // requested duration only to report at exit that no recording ever
        // began wastes the full wall-clock of every failed CI/automation run.
        if (!record_file.empty()) {
            if (!app.emulator().start_recording(record_file)) {
                Log::emulator()->error("Failed to start recording to {}", record_file);
                audio_recorder.stop();
                dac_trace_recorder.stop();
                return 1;
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
            capture_ok = app.emulator().stop_recording() && capture_ok;
        }
        // GH #86: a stop whose result was discarded (MainWindow::closeEvent)
        // is latched per output path — see VideoRecorder::output_failed().
        if (!record_file.empty() &&
            app.emulator().video_recorder().output_failed(record_file)) {
            capture_ok = false;
        }

        if (audio_recorder.is_recording()) {
            app.emulator().mixer().set_capture_callback({});
            const bool audio_ok = audio_recorder.stop();
            capture_ok = audio_ok && capture_ok;
            if (!audio_ok) {
                Log::audio()->error("Failed to finalize WAV recording: {}",
                                    audio_recorder.last_error());
            }
        }
        if (dac_trace_recorder.is_recording()) {
            app.emulator().dac().set_write_callback({});
            const bool trace_ok = dac_trace_recorder.stop();
            capture_ok = capture_ok && trace_ok;
            if (!trace_ok) {
                Log::audio()->error("Failed to finalize DAC trace: {}",
                                    dac_trace_recorder.last_error());
            }
        }

        app.shutdown();
        // shutdown() decides the status: a --delayed-screenshot that was
        // requested and never written exits non-zero, so a CI script cannot
        // mistake a missing PNG for success. A capture-finalize failure makes
        // an otherwise-clean run exit 1, but never replaces the app's own
        // non-zero code with a less specific one.
        if (app.exit_code() != 0) return app.exit_code();
        return capture_ok ? 0 : 1;
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
        for (auto& dn : delayed_nmis) {
            const bool ok = dn.in_frames
                ? app.set_delayed_nmi(dn.button, dn.delay)
                : app.set_delayed_nmi_seconds(dn.button, dn.delay);
            if (!ok) {
                fprintf(stderr, "Unknown --delayed-nmi button name: '%s'\n"
                        "Valid: nmi (or mf, m1)  = the NMI button   (Multiface)\n"
                        "       drive (or divmmc) = the DRIVE button (DivMMC)\n"
                        "(RESET is not an NMI button)\n",
                        dn.button.c_str());
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
