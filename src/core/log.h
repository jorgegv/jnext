#pragma once

/// Logging subsystem — thin wrapper around spdlog.
///
/// Usage:
///   #include "core/log.h"
///   Log::cpu()->info("PC={:#06x} opcode={:#04x}", pc, op);
///   Log::video()->trace("vc={} hc={}", vc, hc);
///
/// Each subsystem has a dedicated named logger whose level can be
/// changed independently at runtime via Log::set_level().
///
/// ---------------------------------------------------------------------------
/// LEVEL POLICY (Task 24, 2026-07-12). Read this before adding a log line.
///
/// The rule that decides almost every case:
///
///     *** info is for what the USER did. debug is for what the GUEST did. ***
///
/// Somebody playing a game does not want a running commentary on the register
/// writes the game is making. Somebody debugging the emulator does — and asks
/// for it with `--log-level emulator=debug`. The default level is info, so an
/// info line is something you are choosing to show every user, every run.
///
///   error    The requested operation FAILED. ROM missing, init failed, file
///            unreadable. The user asked for something and did not get it.
///
///   warn     Emulation continues, but degraded or surprising. A ROM the guest
///            will probably want is absent; an unimplemented feature was hit.
///
///   info     USER/HOST-initiated lifecycle, at most one line per user action:
///            startup banner, "file loaded", "recording started", a reset from
///            a HOST hotkey, a diagnostic the user explicitly switched on
///            (--magic-breakpoint, JNEXT_BOOT_PROBE).
///
///   debug    GUEST-initiated state changes: NextREG writes, CPU speed changes,
///            paging, a reset the guest triggered via NR 0x02. Also the
///            step-by-step detail behind an info summary (which bank went where
///            during a NEX load).
///
///   trace    Per-instruction / per-scanline firehose.
///
/// Two mechanical rules that stop log spam at the source:
///
///   1. NOTHING above `debug` in a per-frame or per-port-write path, and
///      nothing above `trace` in a per-instruction path.
///
///   2. A guest state change logs only when the value ACTUALLY CHANGES, never
///      on every write. Programs rewrite the same register every frame; without
///      this even `debug` is unusable. (This is what made NR 0x07 CPU-speed
///      writes flood the console at info — the original Task 24 complaint.)
/// ---------------------------------------------------------------------------

#include <memory>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

class Log {
public:
    // Per-subsystem loggers.
    static std::shared_ptr<spdlog::logger>& cpu()        { static auto l = make("cpu");        return l; }
    static std::shared_ptr<spdlog::logger>& memory()     { static auto l = make("memory");     return l; }
    static std::shared_ptr<spdlog::logger>& ula()        { static auto l = make("ula");        return l; }
    static std::shared_ptr<spdlog::logger>& video()      { static auto l = make("video");      return l; }
    static std::shared_ptr<spdlog::logger>& audio()      { static auto l = make("audio");      return l; }
    static std::shared_ptr<spdlog::logger>& port()       { static auto l = make("port");       return l; }
    static std::shared_ptr<spdlog::logger>& nextreg()    { static auto l = make("nextreg");    return l; }
    static std::shared_ptr<spdlog::logger>& dma()        { static auto l = make("dma");        return l; }
    static std::shared_ptr<spdlog::logger>& copper()     { static auto l = make("copper");     return l; }
    static std::shared_ptr<spdlog::logger>& uart()       { static auto l = make("uart");       return l; }
    static std::shared_ptr<spdlog::logger>& input()      { static auto l = make("input");      return l; }
    static std::shared_ptr<spdlog::logger>& platform()   { static auto l = make("platform");   return l; }
    static std::shared_ptr<spdlog::logger>& emulator()   { static auto l = make("emulator");   return l; }
    static std::shared_ptr<spdlog::logger>& sdcard()     { static auto l = make("sdcard");     return l; }
    static std::shared_ptr<spdlog::logger>& divmmc()     { static auto l = make("divmmc");     return l; }
    static std::shared_ptr<spdlog::logger>& spi()        { static auto l = make("spi");        return l; }
    static std::shared_ptr<spdlog::logger>& ctc()        { static auto l = make("ctc");        return l; }
    static std::shared_ptr<spdlog::logger>& i2c()        { static auto l = make("i2c");        return l; }
    static std::shared_ptr<spdlog::logger>& multiface()  { static auto l = make("multiface");  return l; }
    /// Emulated ESP-01 WiFi module (GH #25). Owner requirement: every detail of
    /// a program's ESP interaction must be traceable, but NOTHING is on by
    /// default except TCP connection open/close — those are user-visible,
    /// security-relevant events and pair with the "never silent about a
    /// connection made or refused" rule. So: connection opened/closed at info,
    /// policy refusals and DNS stalls at warn, socket failures at error, and
    /// everything else (byte counts, resolve results, poll state) at
    /// debug/trace. NOTE for the branch that makes the ESP user-reachable: the
    /// subsystem list in doc/man/jnext.1.md (--log-level) must gain `esp01`
    /// then — it is deliberately not listed yet, because nothing wires the
    /// transport up and documenting an inert logger would be a lie.
    static std::shared_ptr<spdlog::logger>& esp01()      { static auto l = make("esp01");      return l; }
    /// esxdos / NextZXOS syscall tracing (Task 85). At TRACE level every
    /// RST $08 call is logged with its arguments and result, including calls
    /// jnext does NOT implement — those are the interesting ones when working
    /// out why a program that expects NextZXOS fails under `--load`.
    static std::shared_ptr<spdlog::logger>& esxdos()     { static auto l = make("esxdos");     return l; }

    /// Set the log level for a specific subsystem by name.
    /// Returns false if the logger name is unknown.
    static bool set_level(const std::string& name, spdlog::level::level_enum level) {
        auto logger = spdlog::get(name);
        if (!logger) return false;
        logger->set_level(level);
        return true;
    }

    /// Set the default log level for all subsystems.
    static void set_default_level(spdlog::level::level_enum level) {
        spdlog::set_level(level);
    }

    /// Map a level name to a level, accepting the spellings people actually
    /// type. Returns false if `s` is not a level at all.
    ///
    /// Do NOT use spdlog::level::from_str() directly. It handles the real level
    /// names fine ("warn" and "warning" both work), but it maps every
    /// UNRECOGNISED string to `off` — so a typo silently MUTES a subsystem
    /// instead of being ignored: `--log-level video=warnn` turns video logging
    /// off entirely, and `cpu=fatal` turns cpu off rather than setting critical.
    /// to_level() rejects what it does not understand, and parse_levels() then
    /// skips the token, so a typo is a no-op rather than a blackout.
    static bool to_level(const std::string& s, spdlog::level::level_enum& out) {
        using namespace spdlog::level;
        if (s == "trace")                             { out = trace;    return true; }
        if (s == "debug")                             { out = debug;    return true; }
        if (s == "info")                              { out = info;     return true; }
        if (s == "warn" || s == "warning")            { out = warn;     return true; }
        if (s == "err"  || s == "error")              { out = err;      return true; }
        if (s == "critical" || s == "fatal")          { out = critical; return true; }
        if (s == "off"  || s == "none")               { out = off;      return true; }
        return false;
    }

    /// Parse a log-level spec string. Two token forms, freely mixed:
    ///
    ///   "warn"                     a bare level sets EVERY subsystem
    ///   "cpu=trace,video=warn"     name=level sets one subsystem
    ///   "warn,emulator=debug"      global floor, then a per-subsystem override
    ///
    /// Tokens apply left to right, so a bare level should come first if you want
    /// per-subsystem tokens to override it. Unrecognised logger names and
    /// unparseable levels are silently ignored.
    static void parse_levels(const std::string& spec) {
        std::string::size_type pos = 0;
        while (pos < spec.size()) {
            auto comma = spec.find(',', pos);
            auto token = spec.substr(pos, comma == std::string::npos ? comma : comma - pos);
            pos = (comma == std::string::npos) ? spec.size() : comma + 1;

            spdlog::level::level_enum level;

            auto eq = token.find('=');
            if (eq == std::string::npos) {
                // Bare level: applies to every subsystem.
                if (to_level(token, level)) {
                    set_default_level(level);
                    spdlog::apply_all([level](std::shared_ptr<spdlog::logger> l) {
                        l->set_level(level);
                    });
                }
                continue;
            }
            auto name = token.substr(0, eq);
            if (!to_level(token.substr(eq + 1), level)) continue;
            set_level(name, level);
        }
    }

    /// Force-create all loggers (call once at startup before parse_levels).
    /// Every subsystem name `--log-level` accepts, as DATA.
    ///
    /// It is a list rather than a sequence of calls because the same failure
    /// happened twice at this seam and neither time was visible to any gate:
    /// `ctc`/`i2c`/`multiface` were missing from `init()`, so `--log-level
    /// ctc=debug` silently did nothing (log_test's LOG-06); and `esp01` was
    /// missing from the man page's LOGGING list, so a real subsystem was
    /// undocumented (GH #25 branch 4). Both are now mechanical: `init()`
    /// iterates this array, and `log_test` diffs it against the man page in
    /// BOTH directions — exactly what `cli-check` does for the flag set.
    ///
    /// ADDING A SUBSYSTEM: add its accessor above, add its name here, and
    /// document it in the LOGGING section of `doc/man/jnext.1.md`. Skip the
    /// last step and `make unit-test` fails.
    static constexpr const char* SUBSYSTEMS[] = {
        "cpu",   "memory",  "ula",   "video",    "audio",     "port",  "nextreg",
        "dma",   "copper",  "uart",  "input",    "platform",  "emulator",
        "sdcard","divmmc",  "spi",   "ctc",      "i2c",       "multiface",
        "esp01", "esxdos",
    };

    static void init() {
        for (const char* name : SUBSYSTEMS) make(name);
    }

private:
    static std::shared_ptr<spdlog::logger> make(const char* name) {
        auto existing = spdlog::get(name);
        if (existing) return existing;
        auto logger = spdlog::stderr_color_mt(name);
        logger->set_pattern("[%H:%M:%S.%e] [%n] [%^%l%$] %v");
        return logger;
    }
};
