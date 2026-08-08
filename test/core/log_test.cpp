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
#include "core/cli_options.h"
#include "core/log_sink_policy.h"

#include <spdlog/sinks/ringbuffer_sink.h>

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

int g_pass = 0, g_fail = 0;

std::string join(const std::vector<std::string>& v) {
    std::string s;
    for (const auto& e : v) { if (!s.empty()) s += ", "; s += e; }
    return s;
}

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

    // --- LOG-09/10/11: the man page's subsystem list matches the code ---------
    //
    // GH #25 branch 4. `esp01` existed as a real, reachable subsystem while the
    // man page's LOGGING list did not name it — the same undocumented-surface
    // drift `cli-check` was built for on the flag side, at a seam nothing
    // checked. `docs-check` cannot see it: it proves jnext.1 and USAGE.md were
    // regenerated from jnext.1.md, which stays true of an incomplete list.
    //
    // So this diffs `Log::SUBSYSTEMS` against the list in BOTH directions.
    {
        std::ifstream in(JNEXT_MAN_SRC);
        std::string   section;
        if (!in) {
            check("LOG-09", "the man page LOGGING section is readable", false,
                  std::string("cannot open ") + JNEXT_MAN_SRC);
            check("LOG-10", "no subsystem is implemented but undocumented", false, "no man page");
            check("LOG-11", "no subsystem is documented but unimplemented", false, "no man page");
        } else {
            // From "Subsystems are" to the end of that sentence: the list is
            // prose, so the sentence is the unit, not the line.
            std::string all((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
            const size_t start = all.find("Subsystems are");
            const size_t stop  = (start == std::string::npos) ? std::string::npos
                                                              : all.find('.', start);
            if (start != std::string::npos && stop != std::string::npos)
                section = all.substr(start, stop - start);

            // Every `backticked` word in that sentence is a claimed subsystem.
            std::vector<std::string> documented;
            for (size_t i = 0; i < section.size(); ++i) {
                if (section[i] != '`') continue;
                const size_t end = section.find('`', i + 1);
                if (end == std::string::npos) break;
                documented.push_back(section.substr(i + 1, end - i - 1));
                i = end;
            }

            // Without this the two diffs below pass vacuously on a broken scrape.
            check("LOG-09", "the man page LOGGING subsystem list was found and parsed",
                  documented.size() >= 15,
                  msg("scraped %zu names", documented.size()));

            std::vector<std::string> undocumented;
            for (const char* name : Log::SUBSYSTEMS) {
                bool found = false;
                for (const std::string& d : documented) found = found || d == name;
                if (!found) undocumented.push_back(name);
            }
            check("LOG-10", "no subsystem is implemented but undocumented",
                  undocumented.empty(), join(undocumented));

            std::vector<std::string> unimplemented;
            for (const std::string& d : documented) {
                bool found = false;
                for (const char* name : Log::SUBSYSTEMS) found = found || d == name;
                if (!found) unimplemented.push_back(d);
            }
            check("LOG-11", "no subsystem is documented but unimplemented",
                  unimplemented.empty(), join(unimplemented));
        }
    }

    // === Sink policy: where the log goes and whether it is coloured =========
    //
    // GH #235 (--log-file) and GH #227 (NO_COLOR) are one decision, made by the
    // pure log_sink_policy::decide() and consumed by Log::apply_sink_policy().
    // LOG-12..17 drive the pure half directly — no spdlog, no environment
    // mutation, every case reachable — and LOG-18/19 pin the two contracts that
    // only the consumer can express: the loud failure, and the ordering.

    using namespace log_sink_policy;

    // --- LOG-12: no NO_COLOR means the historical behaviour ------------------
    // Auto, not "colour on": spdlog still does its own tty detection, so a
    // redirected console stays uncoloured exactly as it always did.
    check("LOG-12", "NO_COLOR unset leaves colour on spdlog's tty detection (Auto)",
          decide(nullptr, nullptr).colour == Colour::Auto &&
          !no_color_suppresses(nullptr));

    // --- LOG-13: a non-empty NO_COLOR suppresses colour ----------------------
    // https://no-color.org/ — the VALUE is never interpreted. "0" and "false"
    // suppress colour just as "1" does, because the spec asks about presence
    // and non-emptiness only. A `if (getenv("NO_COLOR") == "1")` reading would
    // pass a naive test and fail every one of these.
    check("LOG-13", "any non-empty NO_COLOR suppresses colour, whatever it says",
          decide(nullptr, "1").colour     == Colour::Never &&
          decide(nullptr, "0").colour     == Colour::Never &&
          decide(nullptr, "false").colour == Colour::Never &&
          decide(nullptr, " ").colour     == Colour::Never,
          msg("1=%d 0=%d false=%d space=%d (want %d for all)",
              static_cast<int>(decide(nullptr, "1").colour),
              static_cast<int>(decide(nullptr, "0").colour),
              static_cast<int>(decide(nullptr, "false").colour),
              static_cast<int>(decide(nullptr, " ").colour),
              static_cast<int>(Colour::Never)));

    // --- LOG-14: an EMPTY NO_COLOR is the same as unset ----------------------
    // The spec's own explicit case, and not a corner: setting NO_COLOR= is how
    // a script re-enables colour for a child whose environment it cannot unset
    // the variable in. Treating empty as "set" breaks that one escape hatch.
    check("LOG-14", "an empty NO_COLOR counts as unset (the spec's explicit case)",
          decide(nullptr, "").colour == Colour::Auto &&
          !no_color_suppresses(""));

    // --- LOG-15: the two features do not contradict each other ---------------
    // A file destination is Never with NO_COLOR set AND unset. A file sink
    // emits no escapes either way; recording that as one readable field is what
    // stops "is this run coloured?" depending on which sink class was picked.
    check("LOG-15", "a file destination is uncoloured with NO_COLOR set and unset",
          decide("trace.log", nullptr).colour == Colour::Never &&
          decide("trace.log", "1").colour     == Colour::Never &&
          decide("trace.log", "").colour      == Colour::Never);

    // --- LOG-16: the destination follows the flag ----------------------------
    // Including the two degenerate values. An empty --log-file "" is still a
    // File destination (with an unopenable path, reported by LOG-18's contract)
    // rather than being quietly turned back into console logging.
    {
        const Policy console = decide(nullptr, nullptr);
        const Policy file    = decide("trace.log", nullptr);
        check("LOG-16", "File when --log-file is given (path kept), Console when not",
              console.destination == Destination::Console && console.path.empty() &&
              file.destination    == Destination::File    && file.path == "trace.log" &&
              decide("", nullptr).destination == Destination::File,
              msg("console=%d file=%d empty-arg=%d (Console=%d File=%d)",
                  static_cast<int>(console.destination), static_cast<int>(file.destination),
                  static_cast<int>(decide("", nullptr).destination),
                  static_cast<int>(Destination::Console), static_cast<int>(Destination::File)));
    }

    // --- LOG-17: the pre-scan is arity-aware ---------------------------------
    // --log-file has to be read before the parse loop (see LOG-19), so it is
    // read by a second walk over argv. A bare string search would find the
    // `--log-file` inside --nex-args' VALUE and write the log to "x"; walking
    // with the table's arity skips values as values.
    {
        const char* plain[]  = { "jnext", "--log-file", "out.log", "game.nex" };
        const char* hidden[] = { "jnext", "--nex-args", "--log-file", "game.nex" };
        const char* twice[]  = { "jnext", "--log-file", "first.log",
                                 "--log-file", "second.log" };
        const char* none[]   = { "jnext", "--headless", "game.nex" };
        const char* v_plain  = cli::prescan_value(4, plain,  cli::OptId::LogFile);
        const char* v_hidden = cli::prescan_value(4, hidden, cli::OptId::LogFile);
        const char* v_twice  = cli::prescan_value(5, twice,  cli::OptId::LogFile);
        const char* v_none   = cli::prescan_value(3, none,   cli::OptId::LogFile);
        check("LOG-17", "prescan reads --log-file, skips it inside another option's value, last wins",
              v_plain != nullptr && std::string(v_plain) == "out.log" &&
              v_hidden == nullptr &&
              v_twice != nullptr && std::string(v_twice) == "second.log" &&
              v_none == nullptr,
              msg("plain=%s hidden=%s twice=%s none=%s",
                  v_plain ? v_plain : "(null)", v_hidden ? v_hidden : "(null)",
                  v_twice ? v_twice : "(null)", v_none ? v_none : "(null)"));
    }

    // --- LOG-18: an unopenable log file FAILS, it does not fall back ---------
    // The outcome that must never happen: the user redirects a trace to a file,
    // the open fails, jnext keeps logging to the console it was redirected away
    // from, and the empty file reads as "the feature works". So the failure is
    // reported AND nothing is rebound — checked on the sink pointer itself,
    // because a fallback that constructed a console sink would replace it.
    {
        // Blocked by a regular FILE standing where a directory would have to
        // be. A merely absent directory is NOT unopenable: spdlog's file_helper
        // calls create_dir() on the parent first, so `no_such_dir/jnext.log`
        // succeeds and would have made this row pass vacuously.
        const char* blocker = "log_test_blocker.tmp";
        const char* blocked = "log_test_blocker.tmp/jnext.log";
        { std::ofstream mk(blocker); mk << "not a directory\n"; }

        auto  logger      = Log::emulator();
        auto  sink_before = logger->sinks().at(0);
        const Log::SinkResult r = Log::apply_sink_policy(decide(blocked, nullptr));
        check("LOG-18", "an unopenable --log-file is reported and never silently downgraded to console",
              !r.ok && !r.error.empty() &&
              r.error.find(blocked) != std::string::npos &&
              logger->sinks().size() == 1 && logger->sinks().at(0) == sink_before,
              msg("ok=%d error=\"%s\" sink_replaced=%d", static_cast<int>(r.ok),
                  r.error.c_str(),
                  static_cast<int>(logger->sinks().at(0) != sink_before)));
        std::remove(blocker);
    }

    // --- LOG-19: the file receives the lines, including from later loggers ---
    // The ordering contract. Loggers are function-local statics bound to
    // whatever sink was current when they were first touched, so main() applies
    // the policy BEFORE Log::init() and before the startup banner. Two halves
    // are checked here: an ALREADY-CREATED logger must be rebound in place (its
    // shared_ptr is cached in a static, so replacing the logger object would
    // leave that static pointing at the old one), and a logger created LATER
    // must be built on the new sink by make().
    {
        const char* path = "log_test_sink.tmp.log";
        std::remove(path);
        const Log::SinkResult r = Log::apply_sink_policy(decide(path, nullptr));

        Log::parse_levels("info");
        Log::emulator()->info("already-created-logger");

        // A logger make() builds AFTER the policy was applied: drop one from
        // the registry and let Log::init() re-create it.
        spdlog::drop("spi");
        Log::init();
        spdlog::get("spi")->info("later-created-logger");
        spdlog::apply_all([](std::shared_ptr<spdlog::logger> l) { l->flush(); });

        std::ifstream in(path);
        std::string   body((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
        check("LOG-19", "--log-file receives lines from loggers created before AND after it was applied",
              r.ok && body.find("already-created-logger") != std::string::npos &&
              body.find("later-created-logger") != std::string::npos &&
              body.find("\033[") == std::string::npos,
              msg("ok=%d bytes=%zu body=[%s]", static_cast<int>(r.ok), body.size(),
                  body.c_str()));

        // Put the console back so nothing after this writes into the temp file,
        // then remove it: the sink still holds the handle until it is replaced.
        Log::apply_sink_policy(decide(nullptr, nullptr));
        std::remove(path);
    }

    // --- LOG-20: main.cpp applies the policy BEFORE it logs anything ---------
    //
    // LOG-19 proves apply_sink_policy() works. It cannot prove main() CALLS it
    // early enough, and that is the whole defect: jnext emits its version
    // banner — and an ffmpeg warning — before the argument loop runs, so a
    // --log-file wired into that loop would silently lose them, and a test
    // written afterwards would see a mostly-correct file and pass. Nothing else
    // can see this seam, so it is checked where it lives: in the source order.
    // Same technique as cli_options_test's CLI-SRC-01.
    {
        // `//` comments are stripped first, exactly as CLI-SRC-01 does: the
        // prose explaining WHY the policy is applied early necessarily names
        // Log::init(), and an unstripped scan matches the comment instead of
        // the call. (It found that on its first run, which is the argument for
        // stripping rather than for wording comments around a checker.)
        std::ifstream in(JNEXT_MAIN_SRC);
        std::string   src;
        for (std::string line; std::getline(in, line); ) {
            size_t quotes = 0;
            for (size_t i = 0; i + 1 < line.size(); ++i) {
                if (line[i] == '"') ++quotes;
                if (line[i] == '/' && line[i + 1] == '/' && quotes % 2 == 0) {
                    line.erase(i);
                    break;
                }
            }
            src += line;
            src += '\n';
        }
        // Anchored at `int main(`, not at the start of the file: main.cpp also
        // defines helpers ABOVE main that log when they are later called, and
        // their source position says nothing about run order.
        const size_t main_pos  = src.find("int main(");
        const size_t apply     = src.find("Log::apply_sink_policy(", main_pos);
        const size_t init      = src.find("Log::init()", main_pos);
        // The first CALL main makes into the logging subsystem must be the one
        // that points it somewhere; anything earlier is a line --log-file
        // cannot catch. Calls only — `Log::SinkResult r = ...` names the type
        // before the call on the very same line, and a plain "Log::" search
        // reports that as an earlier use.
        size_t first_call = std::string::npos;
        for (size_t p = src.find("Log::", main_pos); p != std::string::npos;
             p = src.find("Log::", p + 1)) {
            size_t q = p + 5;
            while (q < src.size() && (std::isalnum(static_cast<unsigned char>(src[q])) ||
                                      src[q] == '_')) ++q;
            if (q < src.size() && src[q] == '(') { first_call = p; break; }
        }
        const size_t first_use = first_call;
        check("LOG-20", "main.cpp applies the sink policy before Log::init() and before any other Log:: use",
              !src.empty() && main_pos != std::string::npos &&
              apply != std::string::npos && init != std::string::npos &&
              apply == first_use && apply < init,
              msg("main@%zu apply@%zu init@%zu first_use@%zu", main_pos, apply, init,
                  first_use));
    }

    std::printf("\n====================================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_pass + g_fail, g_pass, g_fail, 0);
    return g_fail > 0 ? 1 : 0;
}
