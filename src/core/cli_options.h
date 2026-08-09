// The jnext command-line option table — the SINGLE SOURCE OF TRUTH for which
// flags exist (issue #43).
//
// Before this table the flag set existed only as control flow: a 47-arm
// `if (arg == "--x")` chain in main.cpp. Nothing could enumerate it, so nothing
// could compare it to the documentation, and the docs drifted twice without a
// single gate going red (five flags entirely undocumented in dca9dcf0; a wrong
// scale range, two missing GUI menus and a non-existent status-bar indicator in
// v0.98.60).
//
// Now the flag set is DATA. main.cpp's parser dispatches from this table, so a
// flag that is not here cannot be parsed, and test/core/cli_options_test.cpp
// diffs the table against the OPTIONS section of doc/man/jnext.1.md in BOTH
// directions. Implemented-but-undocumented and documented-but-unimplemented are
// both hard failures of `make cli-check`.
//
// ADDING A FLAG: add an OptId, add a row here, add a `case` in main.cpp's
// switch (the compiler enforces that via -Wswitch), and document it in
// doc/man/jnext.1.md. Skip the last step and cli-check fails.
//
// DELIBERATE EXCEPTIONS ARE DECLARED HERE, never hidden in a checker's grep
// exclusions: Doc::UndocumentedAlias marks a flag that is intentionally absent
// from the man page, and the reason goes in a comment on its row.

#ifndef JNEXT_CORE_CLI_OPTIONS_H
#define JNEXT_CORE_CLI_OPTIONS_H

#include <array>
#include <cstddef>
#include <cstring>

namespace cli {

// One per accepted option spelling. Aliases get their own id only where the
// handler needs to tell them apart (--joy1/--joy2, the two keypress forms);
// pure aliases share the canonical id.
enum class OptId {
    LogLevel,
    LogFile,
    Inject,
    InjectOrg,
    InjectPc,
    InjectDelay,
    Load,
    NexArgs,
    ExperimentalNexV13,
    Sdcard,
    SdcardDownloadConfirm,
    SdcardDownloadForce,
    SdcardReadonly,
    DelayedScreenshot,
    DelayedScreenshotTime,
    DelayedScreenshotFrames,
    DelayedScreenshotLayers,
    DelayedAutomaticExit,
    DelayedAutomaticExitFrames,
    DelayedSnapshot,
    DelayedSnapshotFrames,
    Machine,
    Headless,
    Benchmark,
    BenchmarkLabel,
    Silent,
    TapeRealtime,
    TapeSave,
    Esp,
    NoEsp,
    EspAllow,
    EspListenAddress,
    EspDelayedDisassociateFrames,
    EspDelayedAssociateFrames,
    EspIpAddress,
    MagicBreakpoint,
    PersistentBreakpoints,
    EsxdosStub,
    MagicPort,
    MagicPortMode,
    Record,
    WavRecord,
    DacTrace,
    AudioGainDb,
    AudioGainBeeperDb,
    AudioGainAy0Db,
    AudioGainAy1Db,
    AudioGainAy2Db,
    AudioGainDacDb,
    RzxPlay,
    RzxRecord,
    Speed,
    WhenSlowPrefer,
    Joy1Source,
    Joy2Source,
    DelayedKeypress,
    DelayedKeypressFrames,
    DelayedNmi,
    DelayedNmiFrames,
    RewindBufferSize,
    Trace,
    CompositorTrace,
    CompositorTraceFrame,
    Profile,
    ProfileOutput,
    Rtc,
    Help,
    Version,
};

// Parsed gain values plus the independent "CLI supplied this value" bits used
// for saved-configuration precedence. Keeping the option-to-slot mapping here
// makes it directly testable instead of hiding it in main.cpp control flow.
struct AudioGainArgs {
    float master = 0.0f;
    float beeper = 0.0f;
    std::array<float, 3> ay{0.0f, 0.0f, 0.0f};
    float dac = 0.0f;

    bool master_set = false;
    bool beeper_set = false;
    std::array<bool, 3> ay_set{false, false, false};
    bool dac_set = false;
};

inline bool assign_audio_gain(OptId id, float db, AudioGainArgs& gains) {
    switch (id) {
    case OptId::AudioGainDb:
        gains.master = db;
        gains.master_set = true;
        return true;
    case OptId::AudioGainBeeperDb:
        gains.beeper = db;
        gains.beeper_set = true;
        return true;
    case OptId::AudioGainAy0Db:
        gains.ay[0] = db;
        gains.ay_set[0] = true;
        return true;
    case OptId::AudioGainAy1Db:
        gains.ay[1] = db;
        gains.ay_set[1] = true;
        return true;
    case OptId::AudioGainAy2Db:
        gains.ay[2] = db;
        gains.ay_set[2] = true;
        return true;
    case OptId::AudioGainDacDb:
        gains.dac = db;
        gains.dac_set = true;
        return true;
    default:
        return false;
    }
}

// Documentation status of a spelling. Every value is a claim the alignment
// check enforces — none of them is a way to opt out of checking.
enum class Doc {
    Documented,         // MUST have an entry in the man page OPTIONS section
    UndocumentedAlias,  // deliberately absent from the man page; say why on the row
    ShortAlias,         // short form; documented inline on its long form's entry
};

struct Option {
    const char* name;   // exact spelling, including leading dashes
    int         arity;  // number of value arguments consumed after the flag
    Doc         doc;
    OptId       id;
    // The metavars shown after the name in `--help` ("FILE", "SECS KEY", ""),
    // and the description printed beside it. `\n` starts a continuation line;
    // print_usage() supplies the indentation.
    //
    // THEY LIVE HERE BECAUSE `--help` IS GENERATED FROM THIS TABLE. It used to
    // be a 149-line hand-written literal in main.cpp that nothing compared
    // against anything: `make cli-check` diffs this table against the MAN PAGE,
    // so three flags added in GH #246 were in the table, the man page, USAGE.md
    // and the user guide, and absent from `--help` — with every gate green.
    // That is exactly the drift this table was introduced to end, surviving on
    // the one surface it did not reach. A flag cannot now be added without its
    // help text, because the row will not compile without it.
    const char* args;
    const char* help;
};

// The table. Order is the man page's reading order, which keeps a diff of the
// two sides readable when the check fires.
inline constexpr Option OPTIONS[] = {
    // Machine and program
    { "--machine", 1, Doc::Documented, OptId::Machine,
      "TYPE",
      "Machine type: 48k, 128k, plus3, next (default)" },
    { "--load", 1, Doc::Documented, OptId::Load,
      "FILE",
      "Load a program file (auto-detect format by extension)\n"
      "Supported: .nex, .sna, .szx, .z80, .tap, .tzx, .wav, .rzx\n"
      "(.rzx is accepted here and plays back, as --rzx-play)" },
    { "--nex-args", 1, Doc::Documented, OptId::NexArgs,
      "LINE",
      "Argument line for a NEX V1.3 program: placed in the CLI\n"
      "buffer its header declares, with DE pointing at it.\n"
      "Truncated to that buffer's size. V1.3 files only." },
    // GH #228 — NEX V1.3 is experimental and unsupported; without this flag a
    // --load of a NEX whose header version exceeds V1.2 is refused.
    { "--experimental-nex-v1.3", 0, Doc::Documented, OptId::ExperimentalNexV13,
      "",
      "Allow loading NEX V1.3 files. V1.3 is an experimental\n"
      "format and is NOT supported in any way; without this flag\n"
      "a NEX above V1.2 is refused. V1.0-V1.2 are unaffected." },
    { "--sdcard", 1, Doc::Documented, OptId::Sdcard,
      "FILE",
      "Mount SD card image FILE (.img). If omitted, jnext falls\n"
      "back to ~/.jnext/sdcard/ (offering to download the image)." },
    // Backward-compatibility alias for --sdcard, deliberately never documented:
    // it exists so older scripts keep working, and promoting it to the man page
    // would advertise a second spelling nobody should start using.
    { "--sd-card", 1, Doc::UndocumentedAlias, OptId::Sdcard,
      "FILE",
      "Back-compatibility alias for --sdcard (hidden)." },
    { "--sdcard-download-confirm", 0, Doc::Documented, OptId::SdcardDownloadConfirm,
      "",
      "Skip the download prompt and proceed automatically" },
    { "--sdcard-download-force", 0, Doc::Documented, OptId::SdcardDownloadForce,
      "",
      "Force re-download + re-patch of the default-location\n"
      "image (~/.jnext/sdcard/) to recover a corrupted one. Ignored\n"
      "when --sdcard is given (an explicit path always wins)." },
    { "--sdcard-readonly", 0, Doc::Documented, OptId::SdcardReadonly,
      "",
      "Open the SD image read-only; the host file is never written" },
    { "--speed", 1, Doc::Documented, OptId::Speed,
      "PERCENT",
      "Emulator speed as %% (50=half, 100=normal, 200=2x, 400=4x)" },
    { "--when-slow-prefer", 1, Doc::Documented, OptId::WhenSlowPrefer,
      "WHAT",
      "What to sacrifice when the host cannot emulate in\n"
      "real time: 'audio' (default: keep the sound smooth,\n"
      "drop video frames) or 'video' (show every frame, run\n"
      "slower than real time, sound stutters)" },
    { "--joy1-source", 1, Doc::Documented, OptId::Joy1Source,
      "SRC",
      "Host source for Joy 1 (port 0x1F): 'sdl' (autodetected\n"
      "gamepad, default) or 'keys' (host arrow keys + Space=fire)" },
    { "--joy2-source", 1, Doc::Documented, OptId::Joy2Source,
      "SRC",
      "Host source for Joy 2 (port 0x37): 'sdl' (default) or 'keys'\n"
      "(only one connector may use 'keys')" },
    { "--tape-realtime", 0, Doc::Documented, OptId::TapeRealtime,
      "",
      "Use real-time tape loading (simulates actual loading speed)" },
    { "--tape-save", 1, Doc::Documented, OptId::TapeSave,
      "FILE",
      "Append blocks SAVEd via the 48K ROM SA-BYTES routine\n"
      "to FILE (.tap). Trap-based (G33 Phase 1): fires when the\n"
      "ROM save routine at 0x04C2 runs with ROM paged at slot 0.\n"
      "Without this option no SAVE capture happens." },
    { "--esxdos-stub", 0, Doc::Documented, OptId::EsxdosStub,
      "",
      "Intercept RST $08 calls; provide in-memory config I/O\n"
      "and .RUN sibling-NEX chaining without booting NextZXOS" },
    { "--rtc", 1, Doc::Documented, OptId::Rtc,
      "\"YYYY-MM-DD HH:MM:SS\"",
      "Pin the RTC to a fixed date/time (frozen clock)\n"
      "instead of following the host clock. Makes boot\n"
      "screenshots deterministic (regression tests).\n"
      "ISO form YYYY-MM-DDTHH:MM:SS also accepted." },
    { "--silent", 0, Doc::Documented, OptId::Silent,
      "",
      "Disable all sound output (beeper, AY/YM x3, DAC/\n"
      "Covox/Specdrum). No audio device is opened, and the\n"
      "emulator skips PSG/mixer sample synthesis entirely -\n"
      "can measurably speed up CPU-bound runs. Tape loading\n"
      "(EAR input) is unaffected." },
    { "--inject", 1, Doc::Documented, OptId::Inject,
      "FILE",
      "Load raw binary FILE into RAM (see --inject-org, --inject-pc)" },
    { "--inject-org", 1, Doc::Documented, OptId::InjectOrg,
      "ADDR",
      "Load address for --inject (hex, default 8000)" },
    { "--inject-pc", 1, Doc::Documented, OptId::InjectPc,
      "ADDR",
      "Entry point for --inject (hex, default = --inject-org value)" },
    { "--inject-delay", 1, Doc::Documented, OptId::InjectDelay,
      "N",
      "Wait N frames before injecting (default 0; use ~100 if the\n"
      "binary calls ROM routines that need system variable setup)" },

    // Networking (ESP-01 WiFi). Default OFF, and BOTH directions are spelled
    // out: --no-esp exists because the enable can also come from the saved GUI
    // preference, and a security capability the user cannot force off for one
    // run is one they cannot audit. (Contrast --silent, whose saved form has no
    // CLI negation — but --silent cannot reach the network.)
    { "--esp", 0, Doc::Documented, OptId::Esp,
      "",
      "Enable the emulated ESP-01 WiFi module on UART 0.\n"
      "OFF by default: it gives the guest an outbound TCP\n"
      "pipe out of the emulator. Loopback, link-local and\n"
      "cloud-metadata addresses are always refused; your\n"
      "own LAN (RFC1918) is reachable." },
    { "--no-esp", 0, Doc::Documented, OptId::NoEsp,
      "",
      "Force the ESP off for this run, overriding a saved\n"
      "GUI preference that enables it" },
    // Repeatable: each occurrence appends one host.
    { "--esp-allow", 1, Doc::Documented, OptId::EspAllow,
      "HOST",
      "Only let the guest connect to HOST. ONE host per\n"
      "option, repeat it for more (a comma is rejected, not\n"
      "a separator); matching is exact and case-insensitive,\n"
      "and an IP literal must be listed as itself. Without\n"
      "any --esp-allow the guest may name any host." },
    // The INBOUND half (GH #210). Defaults to loopback, so widening it is an
    // explicit act recorded on the command line — the whole of the owner
    // decision in ESP01-EMULATOR-DESIGN.md §13.4. There is deliberately no
    // saved-configuration form: a listening address that could arrive from a
    // config file is one the user cannot audit by reading the command they ran.
    { "--esp-listen-address", 1, Doc::Documented, OptId::EspListenAddress,
      "ADDR",
      "Bind AT+CIPSERVER to ADDR (default 127.0.0.1).\n"
      "A numeric IP, never a name. The default means a\n"
      "guest that opens a server is reachable only from\n"
      "this machine; widening it (e.g. 0.0.0.0) exposes\n"
      "the guest to the network and is deliberate." },
    // GH #246 — a scheduled WiFi outage, so a headless bench can execute the
    // guest code that handles one. Named to sit in BOTH families at once:
    // `--esp*` for the module, `--delayed-*-frames` for the scheduling.
    { "--esp-delayed-disassociate-frames", 1, Doc::Documented, OptId::EspDelayedDisassociateFrames,
      "N",
      "Take the module off its WiFi network after N frames,\n"
      "as if the access point had gone away. AT+CIFSR then\n"
      "reports 0.0.0.0. Nothing else changes: open connections\n"
      "keep running and no WIFI DISCONNECT is sent." },
    { "--esp-delayed-associate-frames", 1, Doc::Documented, OptId::EspDelayedAssociateFrames,
      "N",
      "Put it back on the network after N frames, with the same\n"
      "address. Requires --esp-delayed-disassociate-frames, and\n"
      "N must be greater than its value." },
    // GH #246 — the address the module REPORTS. Deliberately not next to
    // --esp-listen-address in meaning despite sitting next to it here: that
    // one is a socket boundary the kernel enforces, this one is a string in a
    // reply. Nothing binds it.
    { "--esp-ip-address", 1, Doc::Documented, OptId::EspIpAddress,
      "ADDR",
      "Station address the module REPORTS to the guest\n"
      "(AT+CIFSR / AT+CIPSTA?), default 192.168.1.50. Cosmetic:\n"
      "nothing binds it and nothing routes through it - that is\n"
      "--esp-listen-address. A name and 0.0.0.0 are refused." },

    // Recording and playback
    { "--record", 1, Doc::Documented, OptId::Record,
      "FILE",
      "Record video/audio to FILE (MP4, requires ffmpeg)" },
    { "--wav-record", 1, Doc::Documented, OptId::WavRecord,
      "FILE",
      "Record mixed stereo audio to a 44.1 kHz PCM WAV;\n"
      "works headless and does not require ffmpeg" },
    { "--dac-trace", 1, Doc::Documented, OptId::DacTrace,
      "FILE",
      "Record timestamped physical DAC writes to CSV" },
    { "--audio-gain-db", 1, Doc::Documented, OptId::AudioGainDb,
      "DB",
      "Host audio gain in dB (-24..+24, default 0);\n"
      "PCM overflow saturates" },
    { "--audio-gain-beeper-db", 1, Doc::Documented, OptId::AudioGainBeeperDb,
      "DB",
      "Beeper host gain (-24..+24 dB)" },
    { "--audio-gain-ay0-db", 1, Doc::Documented, OptId::AudioGainAy0Db,
      "DB",
      "TurboSound AY #0 host gain (-24..+24 dB)" },
    { "--audio-gain-ay1-db", 1, Doc::Documented, OptId::AudioGainAy1Db,
      "DB",
      "TurboSound AY #1 host gain (-24..+24 dB)" },
    { "--audio-gain-ay2-db", 1, Doc::Documented, OptId::AudioGainAy2Db,
      "DB",
      "TurboSound AY #2 host gain (-24..+24 dB)" },
    { "--audio-gain-dac-db", 1, Doc::Documented, OptId::AudioGainDacDb,
      "DB",
      "DAC-family host gain (-24..+24 dB)" },
    { "--rzx-play", 1, Doc::Documented, OptId::RzxPlay,
      "FILE",
      "Play back an RZX recording file" },
    { "--rzx-record", 1, Doc::Documented, OptId::RzxRecord,
      "FILE",
      "Record input to an RZX file" },
    { "--rewind-buffer-size", 1, Doc::Documented, OptId::RewindBufferSize,
      "N",
      "Number of frame snapshots to store for rewind (default 0=off)" },
    { "--trace", 0, Doc::Documented, OptId::Trace,
      "",
      "Enable the per-instruction trace log (10K-entry ring;\n"
      "implied by --rewind-buffer-size N with N>0)" },

    // Headless and automation
    { "--headless", 0, Doc::Documented, OptId::Headless,
      "",
      "Run without display/audio (for automated testing)" },
    { "--benchmark", 1, Doc::Documented, OptId::Benchmark,
      "N",
      "Headless-only: run exactly N frames uncapped, then\n"
      "print one machine-parseable BENCH line (wall s, fps,\n"
      "T-states/s, T-states/frame, CPU speed, host core, build\n"
      "type) plus a human summary to stdout, and exit" },
    { "--benchmark-label", 1, Doc::Documented, OptId::BenchmarkLabel,
      "NAME",
      "Workload label printed verbatim in the BENCH line\n"
      "(default: loaded file's basename, or boot-<machine>).\n"
      "No whitespace (the BENCH line is space-delimited)" },
    { "--delayed-screenshot", 1, Doc::Documented, OptId::DelayedScreenshot,
      "FILE",
      "Save a PNG screenshot after a delay" },
    { "--delayed-screenshot-time", 1, Doc::Documented, OptId::DelayedScreenshotTime,
      "N",
      "Delay in seconds (default 10)" },
    { "--delayed-screenshot-frames", 1, Doc::Documented, OptId::DelayedScreenshotFrames,
      "N",
      "Delay in frames (overrides --delayed-screenshot-time)" },
    { "--delayed-screenshot-layers", 1, Doc::Documented, OptId::DelayedScreenshotLayers,
      "LIST",
      "Layers to compose into the screenshot: a comma-separated\n"
      "list of ula, layer2, sprites, tiles, all (default: all).\n"
      "Excluded layers are treated as disabled, so the rest still\n"
      "composite per NR 0x15 priority and the NR 0x4A fallback\n"
      "colour shows through. Excluding 'ula' also removes the\n"
      "border (the ULA draws it)." },
    { "--delayed-automatic-exit", 1, Doc::Documented, OptId::DelayedAutomaticExit,
      "N",
      "Exit the emulator after N seconds" },
    { "--delayed-automatic-exit-frames", 1, Doc::Documented, OptId::DelayedAutomaticExitFrames,
      "N",
      "Exit after N frames (overrides --delayed-automatic-exit)" },
    { "--delayed-snapshot", 1, Doc::Documented, OptId::DelayedSnapshot,
      "FILE",
      "Headless-only: save a snapshot after a delay (frames);\n"
      "format chosen by FILE's extension (.szx/.nex/other->.sna)" },
    { "--delayed-snapshot-frames", 1, Doc::Documented, OptId::DelayedSnapshotFrames,
      "N",
      "Delay in frames for --delayed-snapshot (default 0)" },
    // The two-value options: SECS/N and KEY (or BUTTON) are consumed together.
    { "--delayed-keypress", 2, Doc::Documented, OptId::DelayedKeypress,
      "SECS KEY",
      "Press KEY after SECS seconds (headless only, repeatable).\n"
      "KEY (case-insensitive): single char (a-z 0-9 . , ; :),\n"
      "ENTER / RETURN / SPACE / UP / DOWN / LEFT / RIGHT, or a\n"
      "compound sym+<char> / caps+<char> (e.g. sym+m = '.')" },
    { "--delayed-keypress-frames", 2, Doc::Documented, OptId::DelayedKeypressFrames,
      "N KEY",
      "Press KEY after N emulated frames (frames-unit spelling\n"
      "of --delayed-keypress; both forms queue, not override)" },
    { "--delayed-nmi", 2, Doc::Documented, OptId::DelayedNmi,
      "SECS BUTTON",
      "Press an NMI BUTTON after SECS seconds (headless only,\n"
      "repeatable). BUTTON (case-insensitive) is the label on\n"
      "the real case: nmi (aliases mf, m1) = the NMI button,\n"
      "driven by the Multiface; drive (alias divmmc) = the DRIVE\n"
      "button, driven by DivMMC. RESET is not an NMI button" },
    { "--delayed-nmi-frames", 2, Doc::Documented, OptId::DelayedNmiFrames,
      "N BUTTON",
      "Press BUTTON after N emulated frames (frames-unit spelling\n"
      "of --delayed-nmi; both forms queue, not override)" },
    { "--compositor-trace", 1, Doc::Documented, OptId::CompositorTrace,
      "FILE",
      "Dump per-pixel compositor trace (CSV) for one frame to FILE" },
    { "--compositor-trace-frame", 1, Doc::Documented, OptId::CompositorTraceFrame,
      "N",
      "Target frame for --compositor-trace (default 250)" },

    // Debugging
    { "--magic-breakpoint", 0, Doc::Documented, OptId::MagicBreakpoint,
      "",
      "Enable magic breakpoints (ED FF / DD 01 trigger debugger)" },
    // GH #219. Accepted by EVERY build, including --headless, the SDL-only
    // frontend and -DENABLE_DEBUGGER=OFF, where it is simply inert: nothing
    // outside src/debugger/ can add a breakpoint, so the armed check never
    // matches. Making the row conditional would make `make cli-check` — which
    // diffs this table against ONE man page — disagree with itself per build
    // configuration, and would make the same command line valid or invalid
    // depending on how jnext was compiled. Same posture as --magic-breakpoint.
    { "--persistent-breakpoints", 0, Doc::Documented, OptId::PersistentBreakpoints,
      "",
      "Keep breakpoints armed while the debugger window is\n"
      "closed; a hit reopens it (GUI debugger builds only)" },
    { "--magic-port", 1, Doc::Documented, OptId::MagicPort,
      "PORT",
      "Enable magic debug port at PORT (hex, e.g. 0x00FF)" },
    { "--magic-port-mode", 1, Doc::Documented, OptId::MagicPortMode,
      "MODE",
      "Magic port output mode: hex, dec, ascii, line (default: hex)" },
    { "--profile", 0, Doc::Documented, OptId::Profile,
      "",
      "Enable the CPU T-state profiler (Task 21). Allocates an\n"
      "mmap'd histogram and accumulates one entry per executed\n"
      "instruction. On exit the histogram is written to\n"
      "--profile-output. Use 'tools/get-function-heatmap.pl\n"
      "-m FILE.map' to join it against a z88dk .map file." },
    { "--profile-output", 1, Doc::Documented, OptId::ProfileOutput,
      "FILE",
      "Output path for --profile (default: profile.dat)" },

    // Misc
    { "--log-level", 1, Doc::Documented, OptId::LogLevel,
      "SPEC",
      "Log levels: a bare level sets all subsystems (e.g. warn),\n"
      "name=level sets one (e.g. cpu=trace); mix them, applied\n"
      "left to right (e.g. warn,emulator=debug)" },
    // GH #235. Read by prescan_value() BEFORE the parse loop below runs, because
    // jnext logs its startup banner before it parses anything; the row exists so
    // the loop consumes the value instead of rejecting the flag as unknown.
    { "--log-file", 1, Doc::Documented, OptId::LogFile,
      "FILE",
      "Write the log to FILE instead of the console. Truncated on\n"
      "every run; jnext exits non-zero if FILE cannot be opened.\n"
      "Set NO_COLOR to a non-empty value for uncoloured logs." },
    { "--help", 0, Doc::Documented, OptId::Help,
      "",
      "Print this help and exit" },
    { "-h", 0, Doc::ShortAlias, OptId::Help,
      "",
      "Print this help and exit" },
    { "--version", 0, Doc::Documented, OptId::Version,
      "",
      "Print version and exit" },
    { "-V", 0, Doc::ShortAlias, OptId::Version,
      "",
      "Print version and exit" },
};

inline constexpr std::size_t OPTION_COUNT = sizeof(OPTIONS) / sizeof(OPTIONS[0]);

// Exact-match lookup. Returns nullptr for anything not in the table — which is
// what makes an unrecognised argument an error instead of a silent no-op.
inline const Option* find(const char* name) {
    for (const Option& o : OPTIONS) {
        if (std::strcmp(o.name, name) == 0) return &o;
    }
    return nullptr;
}

// Find the value of `id` without running the real parse loop (GH #235).
//
// Needed for exactly one reason: `--log-file` decides where the FIRST log line
// goes, and jnext emits its startup banner — and an ffmpeg warning — before the
// parse loop in main() has run a single iteration. So the flag has to be read
// ahead of everything, and then be consumed by the loop like any other option.
//
// It walks with the table's arity rather than searching argv for a string, so
// an option VALUE that happens to spell a flag (`--nex-args "--log-file x"`,
// `--rtc --log-file`) is skipped as the value it is instead of being mistaken
// for a flag. The last occurrence wins, matching the parse loop, where a
// repeated option simply overwrites the earlier one.
//
// Returns nullptr when the option is absent. An unknown argument stops nothing:
// the walk just advances by one, exactly as the parse loop does before it
// reports the argument as unrecognised.
inline const char* prescan_value(int argc, const char* const argv[], OptId id) {
    const char* found = nullptr;
    for (int i = 1; i < argc; ++i) {
        const Option* opt = find(argv[i]);
        if (!opt || i + opt->arity >= argc) continue;
        if (opt->id == id && opt->arity >= 1) found = argv[i + 1];
        i += opt->arity;
    }
    return found;
}

// The one option that also accepts an inline value (`--log-level=warn`). It
// predates the table and scripts rely on it, so it is handled ahead of lookup
// in main.cpp rather than generalised to every option (which WOULD be a
// behaviour change: no other flag accepts `=` today).
inline constexpr const char* INLINE_VALUE_PREFIX = "--log-level=";

}  // namespace cli

#endif  // JNEXT_CORE_CLI_OPTIONS_H
