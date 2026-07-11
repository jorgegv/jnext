// Logging level-policy test runner (Task 24, 2026-07-12).
//
// This suite has NO VHDL oracle: logging is host-side tooling, not emulated
// hardware. What it asserts is the contract documented in src/core/log.h — the
// level policy, the level-name parser, and the --log-level spec parser.
//
// Two of these rows guard bugs that actually shipped:
//
//   LOG-02  spdlog::level::from_str() maps every UNRECOGNISED string to `off`.
//           The real level names are fine ("warn" and "warning" both work) —
//           but a TYPO silently MUTES a subsystem instead of being ignored:
//           `--log-level video=warnn` turns video logging off entirely, and
//           `cpu=fatal` turns cpu off rather than setting critical.
//           Log::to_level() rejects what it does not understand so that a typo
//           is a no-op, not a blackout.
//
//   LOG-06  ctc / i2c / multiface create their spdlog logger lazily inside
//           their own *_log() helpers. They were missing from Log::init(), so
//           at --log-level parse time spdlog::get(name) returned null and
//           Log::set_level() failed SILENTLY: `--log-level ctc=debug` did
//           nothing at all. Harmless while those lines sat at info; fatal once
//           the level policy demoted them to debug, because it made them
//           unreachable.
//
// Run: ./build/test/log_test

#include "core/log.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {})
{
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string msg(const char* f, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, f);
    vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

/// Level of a logger by name, or `off` if it does not exist.
spdlog::level::level_enum level_of(const char* name)
{
    auto l = spdlog::get(name);
    return l ? l->level() : spdlog::level::off;
}

}  // namespace

int main()
{
    std::printf("Logging level policy (Task 24) — host-side, no VHDL oracle\n");
    std::printf("====================================================\n\n");

    Log::init();

    using namespace spdlog::level;
    level_enum lv;

    // --- LOG-01: the level names spdlog itself uses ---------------------------
    check("LOG-01", "canonical level names parse",
          Log::to_level("trace", lv)    && lv == trace &&
          Log::to_level("debug", lv)    && lv == debug &&
          Log::to_level("info", lv)     && lv == info &&
          Log::to_level("warning", lv)  && lv == warn &&
          Log::to_level("error", lv)    && lv == err &&
          Log::to_level("critical", lv) && lv == critical &&
          Log::to_level("off", lv)      && lv == off);

    // --- LOG-02: a typo must not MUTE a subsystem -----------------------------
    // The trap this guards: spdlog::level::from_str() returns `off` for anything
    // it does not recognise. So delegating to it means `video=warnn` silently
    // kills video logging. to_level() must reject instead.
    {
        check("LOG-02", "\"warn\" is a real level (both spellings work)",
              Log::to_level("warn", lv) && lv == warn,
              msg("to_level(\"warn\") -> %d, want %d", static_cast<int>(lv),
                  static_cast<int>(warn)));
        // Guard the guard: prove the trap we work around actually exists, so
        // LOG-04 is testing something real rather than a hypothetical.
        check("LOG-02b", "spdlog::from_str() really does map an unknown token to off",
              spdlog::level::from_str("warnn") == off &&
              spdlog::level::from_str("fatal") == off);
    }

    // --- LOG-03: the aliases people actually type ------------------------------
    check("LOG-03", "err/fatal/none aliases parse",
          Log::to_level("err", lv)   && lv == err &&
          Log::to_level("fatal", lv) && lv == critical &&
          Log::to_level("none", lv)  && lv == off);

    // --- LOG-04: an unknown token is REJECTED, never silently `off` ------------
    check("LOG-04", "an unrecognised level is rejected, not treated as off",
          !Log::to_level("warnn", lv) &&
          !Log::to_level("", lv) &&
          !Log::to_level("verbose", lv));

    // --- LOG-05: --log-level spec parsing --------------------------------------
    {
        Log::parse_levels("info");                 // reset to a known state
        Log::parse_levels("warn");                 // bare level = every subsystem
        const bool all_warn = level_of("emulator") == warn && level_of("video") == warn &&
                              level_of("cpu") == warn;
        check("LOG-05a", "a bare level sets every subsystem",
              all_warn, msg("emulator=%d video=%d cpu=%d",
                            static_cast<int>(level_of("emulator")),
                            static_cast<int>(level_of("video")),
                            static_cast<int>(level_of("cpu"))));

        Log::parse_levels("warn,emulator=debug");  // global floor + one override
        check("LOG-05b", "tokens apply left to right, so name=level overrides a bare level",
              level_of("emulator") == debug && level_of("video") == warn,
              msg("emulator=%d video=%d", static_cast<int>(level_of("emulator")),
                  static_cast<int>(level_of("video"))));
    }

    // --- LOG-06: the lazily-created loggers are reachable ----------------------
    // ctc / i2c / multiface build their logger on first use, inside their own
    // *_log() helper. If Log::init() does not pre-register them, spdlog::get()
    // returns null at --log-level parse time and set_level() fails SILENTLY.
    {
        Log::parse_levels("warn");
        Log::parse_levels("ctc=debug,i2c=trace,multiface=debug");
        check("LOG-06", "per-subsystem level reaches the lazily-created loggers",
              level_of("ctc") == debug && level_of("i2c") == trace &&
              level_of("multiface") == debug,
              msg("ctc=%d i2c=%d multiface=%d (off=%d means the logger was never registered)",
                  static_cast<int>(level_of("ctc")), static_cast<int>(level_of("i2c")),
                  static_cast<int>(level_of("multiface")), static_cast<int>(off)));
    }

    // --- LOG-07: malformed specs are ignored, never destructive ----------------
    // The failure mode that matters: a typo must not mute the emulator.
    {
        Log::parse_levels("info");
        Log::parse_levels("");                 // empty
        Log::parse_levels("video=bogus");      // unknown level
        Log::parse_levels("nosuchsystem=debug");  // unknown subsystem
        Log::parse_levels("cpu=");             // empty level
        Log::parse_levels("=trace");           // empty name
        Log::parse_levels("info,");            // trailing comma
        check("LOG-07", "malformed spec tokens are ignored and do not mute anything",
              level_of("video") == info && level_of("cpu") == info &&
              level_of("emulator") == info,
              msg("video=%d cpu=%d emulator=%d (want info=%d)",
                  static_cast<int>(level_of("video")), static_cast<int>(level_of("cpu")),
                  static_cast<int>(level_of("emulator")), static_cast<int>(info)));
    }

    // --- LOG-08: the level actually gates output ------------------------------
    // Everything above is bookkeeping; this proves the level is enforced.
    {
        auto ring = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(32);
        auto probe = std::make_shared<spdlog::logger>("log_test_probe", ring);
        spdlog::register_logger(probe);

        Log::parse_levels("log_test_probe=info");
        probe->debug("guest did a thing");   // must be suppressed
        probe->info("user did a thing");     // must pass
        const size_t after_info = ring->last_formatted().size();

        Log::parse_levels("log_test_probe=debug");
        probe->debug("guest did a thing");   // must now pass
        const size_t after_debug = ring->last_formatted().size();

        check("LOG-08", "the configured level gates output (debug hidden at info, shown at debug)",
              after_info == 1 && after_debug == 2,
              msg("messages after info-level=%zu (want 1), after debug-level=%zu (want 2)",
                  after_info, after_debug));
        spdlog::drop("log_test_probe");
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
