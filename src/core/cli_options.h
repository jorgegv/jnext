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
    Inject,
    InjectOrg,
    InjectPc,
    InjectDelay,
    Load,
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
    MagicBreakpoint,
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
    Joy1Source,
    Joy2Source,
    DelayedKeypress,
    DelayedKeypressFrames,
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
};

// The table. Order is the man page's reading order, which keeps a diff of the
// two sides readable when the check fires.
inline constexpr Option OPTIONS[] = {
    // Machine and program
    { "--machine",                   1, Doc::Documented, OptId::Machine },
    { "--load",                      1, Doc::Documented, OptId::Load },
    { "--sdcard",                    1, Doc::Documented, OptId::Sdcard },
    // Backward-compatibility alias for --sdcard, deliberately never documented:
    // it exists so older scripts keep working, and promoting it to the man page
    // would advertise a second spelling nobody should start using.
    { "--sd-card",                   1, Doc::UndocumentedAlias, OptId::Sdcard },
    { "--sdcard-download-confirm",   0, Doc::Documented, OptId::SdcardDownloadConfirm },
    { "--sdcard-download-force",     0, Doc::Documented, OptId::SdcardDownloadForce },
    { "--sdcard-readonly",           0, Doc::Documented, OptId::SdcardReadonly },
    { "--speed",                     1, Doc::Documented, OptId::Speed },
    { "--joy1-source",               1, Doc::Documented, OptId::Joy1Source },
    { "--joy2-source",               1, Doc::Documented, OptId::Joy2Source },
    { "--tape-realtime",             0, Doc::Documented, OptId::TapeRealtime },
    { "--tape-save",                 1, Doc::Documented, OptId::TapeSave },
    { "--esxdos-stub",               0, Doc::Documented, OptId::EsxdosStub },
    { "--rtc",                       1, Doc::Documented, OptId::Rtc },
    { "--silent",                    0, Doc::Documented, OptId::Silent },
    { "--inject",                    1, Doc::Documented, OptId::Inject },
    { "--inject-org",                1, Doc::Documented, OptId::InjectOrg },
    { "--inject-pc",                 1, Doc::Documented, OptId::InjectPc },
    { "--inject-delay",              1, Doc::Documented, OptId::InjectDelay },

    // Networking (ESP-01 WiFi). Default OFF, and BOTH directions are spelled
    // out: --no-esp exists because the enable can also come from the saved GUI
    // preference, and a security capability the user cannot force off for one
    // run is one they cannot audit. (Contrast --silent, whose saved form has no
    // CLI negation — but --silent cannot reach the network.)
    { "--esp",                       0, Doc::Documented, OptId::Esp },
    { "--no-esp",                    0, Doc::Documented, OptId::NoEsp },
    // Repeatable: each occurrence appends one host.
    { "--esp-allow",                 1, Doc::Documented, OptId::EspAllow },

    // Recording and playback
    { "--record",                    1, Doc::Documented, OptId::Record },
    { "--wav-record",                1, Doc::Documented, OptId::WavRecord },
    { "--dac-trace",                 1, Doc::Documented, OptId::DacTrace },
    { "--audio-gain-db",             1, Doc::Documented, OptId::AudioGainDb },
    { "--audio-gain-beeper-db",      1, Doc::Documented, OptId::AudioGainBeeperDb },
    { "--audio-gain-ay0-db",         1, Doc::Documented, OptId::AudioGainAy0Db },
    { "--audio-gain-ay1-db",         1, Doc::Documented, OptId::AudioGainAy1Db },
    { "--audio-gain-ay2-db",         1, Doc::Documented, OptId::AudioGainAy2Db },
    { "--audio-gain-dac-db",         1, Doc::Documented, OptId::AudioGainDacDb },
    { "--rzx-play",                  1, Doc::Documented, OptId::RzxPlay },
    { "--rzx-record",                1, Doc::Documented, OptId::RzxRecord },
    { "--rewind-buffer-size",        1, Doc::Documented, OptId::RewindBufferSize },
    { "--trace",                     0, Doc::Documented, OptId::Trace },

    // Headless and automation
    { "--headless",                  0, Doc::Documented, OptId::Headless },
    { "--benchmark",                 1, Doc::Documented, OptId::Benchmark },
    { "--benchmark-label",           1, Doc::Documented, OptId::BenchmarkLabel },
    { "--delayed-screenshot",        1, Doc::Documented, OptId::DelayedScreenshot },
    { "--delayed-screenshot-time",   1, Doc::Documented, OptId::DelayedScreenshotTime },
    { "--delayed-screenshot-frames", 1, Doc::Documented, OptId::DelayedScreenshotFrames },
    { "--delayed-screenshot-layers", 1, Doc::Documented, OptId::DelayedScreenshotLayers },
    { "--delayed-automatic-exit",    1, Doc::Documented, OptId::DelayedAutomaticExit },
    { "--delayed-automatic-exit-frames", 1, Doc::Documented, OptId::DelayedAutomaticExitFrames },
    { "--delayed-snapshot",          1, Doc::Documented, OptId::DelayedSnapshot },
    { "--delayed-snapshot-frames",   1, Doc::Documented, OptId::DelayedSnapshotFrames },
    // The only two-value options: SECS/N and KEY are consumed together.
    { "--delayed-keypress",          2, Doc::Documented, OptId::DelayedKeypress },
    { "--delayed-keypress-frames",   2, Doc::Documented, OptId::DelayedKeypressFrames },
    { "--compositor-trace",          1, Doc::Documented, OptId::CompositorTrace },
    { "--compositor-trace-frame",    1, Doc::Documented, OptId::CompositorTraceFrame },

    // Debugging
    { "--magic-breakpoint",          0, Doc::Documented, OptId::MagicBreakpoint },
    { "--magic-port",                1, Doc::Documented, OptId::MagicPort },
    { "--magic-port-mode",           1, Doc::Documented, OptId::MagicPortMode },
    { "--profile",                   0, Doc::Documented, OptId::Profile },
    { "--profile-output",            1, Doc::Documented, OptId::ProfileOutput },

    // Misc
    { "--log-level",                 1, Doc::Documented, OptId::LogLevel },
    { "--help",                      0, Doc::Documented, OptId::Help },
    { "-h",                          0, Doc::ShortAlias, OptId::Help },
    { "--version",                   0, Doc::Documented, OptId::Version },
    { "-V",                          0, Doc::ShortAlias, OptId::Version },
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

// The one option that also accepts an inline value (`--log-level=warn`). It
// predates the table and scripts rely on it, so it is handled ahead of lookup
// in main.cpp rather than generalised to every option (which WOULD be a
// behaviour change: no other flag accepts `=` today).
inline constexpr const char* INLINE_VALUE_PREFIX = "--log-level=";

}  // namespace cli

#endif  // JNEXT_CORE_CLI_OPTIONS_H
