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
    /// Do NOT use spdlog::level::from_str() directly: it maps ANY unrecognised
    /// string to `off`. spdlog spells the warn level "warning", so
    /// `--log-level video=warn` — the example in our own --help text — was
    /// silently turning video logging OFF instead of setting it to warn.
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
    static void init() {
        cpu(); memory(); ula(); video(); audio(); port(); nextreg();
        dma(); copper(); uart(); input(); platform(); emulator();
        sdcard(); divmmc(); spi();
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
