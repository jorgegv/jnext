#pragma once

/// Where jnext's log output goes, and whether it is coloured (GH #235, GH #227).
///
/// Both questions are answered BEFORE any spdlog sink exists, by one pure
/// function over two inputs — the `--log-file` value (nullptr when the flag was
/// not given) and the value of the `NO_COLOR` environment variable (nullptr
/// when unset). That is deliberate, and it is why the two issues landed
/// together: they are two halves of a single decision about how to build the
/// sink, and answering them inside the spdlog construction would make them
/// implicit in *which sink type happened to be constructed* rather than
/// something a test can state and check.
///
/// The same convention as src/platform/audio_pacing.h, audio_fill.h and
/// render_policy.h: the decision is a pure function with its own unit rows
/// (log_test LOG-12..LOG-17), and src/core/log.h is a thin consumer of it.
///
/// getenv() is NOT called here. The caller reads the environment and passes the
/// value in, so every case — unset, empty, non-empty — is reachable from a test
/// without mutating the process environment (which is global state a parallel
/// test run would race on).

#include <string>

namespace log_sink_policy {

/// Where the log lines go.
///
/// `File` REPLACES the console rather than teeing to both. "Send output to a
/// file" is what the flag name promises and is the simpler contract; a tee
/// would double every line for the many users who already redirect, and would
/// leave them no way to get the single stream they asked for.
enum class Destination { Console, File };

/// Whether ANSI colour escapes may be emitted.
///
/// `Auto` is spdlog's own behaviour and jnext's historical one: colour when the
/// destination is a terminal, none when it is not. `Never` is unconditional.
/// There is deliberately no `Always`: no flag or variable in scope asks for
/// colour to be FORCED, and inventing one would be a third state nothing can
/// reach. (`--color=auto|never|always` is explicitly out of scope in GH #227,
/// and would take precedence over NO_COLOR if it is ever added.)
enum class Colour { Auto, Never };

struct Policy {
    Destination destination = Destination::Console;
    Colour      colour      = Colour::Auto;
    /// The path to write to. Meaningful only when `destination == File`, and
    /// empty otherwise — a Console policy carries no path to be misread.
    std::string path;
};

/// Does this NO_COLOR value suppress colour?
///
/// https://no-color.org/ : colour is suppressed when the variable is "present
/// and not an empty string", regardless of any terminal detection. An empty
/// value is explicitly the same as unset — that is how a script *re-enables*
/// colour for a child process it cannot unset the variable for, so treating
/// empty as "set" would break the one workaround the spec provides. The value
/// is never interpreted: `NO_COLOR=0` and `NO_COLOR=false` both suppress
/// colour, because they are non-empty.
inline bool no_color_suppresses(const char* no_color_env)
{
    return no_color_env != nullptr && no_color_env[0] != '\0';
}

/// The whole decision, in one place.
///
/// `log_file` is the `--log-file` value, or nullptr when the flag was absent.
/// `no_color_env` is `getenv("NO_COLOR")`, or nullptr when unset.
///
/// A file destination is recorded as `Colour::Never` whatever the environment
/// says. A file sink emits no escapes anyway, so this changes no output — but
/// it makes the answer to "is this run coloured?" a single field the test can
/// read, instead of a fact that only holds because of which sink class was
/// picked. It also means the two features cannot contradict each other: there
/// is exactly one place where colour is decided, and it is this function.
inline Policy decide(const char* log_file, const char* no_color_env)
{
    Policy p;
    if (log_file != nullptr) {
        // Presence of the flag decides, not the usefulness of its value. An
        // empty `--log-file ""` therefore becomes a File destination with an
        // unopenable path, and is reported as such — mapping it back to the
        // console here would be exactly the silent downgrade the error contract
        // exists to prevent.
        p.destination = Destination::File;
        p.path        = log_file;
    }
    p.colour = (p.destination == Destination::File || no_color_suppresses(no_color_env))
                   ? Colour::Never
                   : Colour::Auto;
    return p;
}

}  // namespace log_sink_policy
