#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <sstream>
#include <string>

/// The module's LOGGING SEAM (GH #25, branch 3.5 of 6).
///
/// WHY THIS EXISTS. The ESP-01 emulation is meant to be reusable by other
/// projects, and until this header it logged through jnext's `core/log.h` —
/// 21 call sites hard-wired to another project's spdlog wrapper. A reusable
/// module cannot require its host's logging framework, so everything it has to
/// say now goes through the four functions below and the host decides where
/// that lands. jnext binds the sink to its own `esp01` logger IN THE ADAPTER
/// (`src/peripheral/esp_uart_adapter.cpp`), not in here.
///
/// THIS IS NOT A LOGGING FRAMEWORK and must not become one. There is no
/// timestamp, no logger name, no file/line, no sink chaining, no ownership of
/// an output stream. The module produces a level and a formatted line; the
/// host's real logger supplies everything else, because it already does.
///
/// UNBOUND MEANS SILENT. With no sink installed nothing is emitted and nothing
/// is even formatted, so a consumer that ignores this header pays nothing and
/// hears nothing. That is the correct default for a library: a component that
/// writes to stderr uninvited is a component you have to fight.
///
/// WHY THE BINDING IS GLOBAL, not per-instance. Three reasons, in order of
/// weight:
///   1. The module models ONE physical device on ONE UART. There is no
///      real scenario with two ESPs wanting two different sinks.
///   2. The transport is created by a FREE FUNCTION (`make_socket_transport`)
///      and logs from a `SocketTransport` the engine never sees, and the
///      address policy is pure free functions. A per-instance sink would have
///      to be threaded through both, plus every future free function, to cover
///      the same call sites a global covers for nothing.
///   3. It is what every logging library in existence does, so it is what a
///      consumer expects to find.
/// The cost of that choice is that the sink is shared mutable state, so it is
/// guarded (see the .cpp) and the threshold is atomic — the threaded wrapper
/// logs from its own thread.
///
/// THE THRESHOLD IS A CHEAP GATE, not policy. `log_*()` returns before doing
/// any formatting when the level is below it, which is what keeps the tracing
/// calls on the per-tick pacing path free when tracing is off. The host still
/// applies its own filtering downstream; the threshold exists so the module
/// does not pay to build a string the host will throw away. A host that wants
/// everything sets it to `Trace` and filters itself.
namespace esp {

/// Deliberately the classic five, and no more. They map onto every logging
/// framework worth binding to, which is the only requirement.
enum class LogLevel : std::uint8_t { Trace = 0, Debug = 1, Info = 2, Warn = 3, Error = 4 };

/// Lower-case, stable, safe to print. Never returns null.
const char* log_level_text(LogLevel level);

/// Where a line goes. `message` is already formatted and carries no trailing
/// newline; the sink owns presentation.
using LogSink = std::function<void(LogLevel level, const std::string& message)>;

/// Install the sink, or clear it with `nullptr` (which restores silence).
/// Safe to call at any time from any thread.
void set_log_sink(LogSink sink);

/// Drop everything below `level`. Default `Info`, which is what makes the
/// module's own "nothing on by default except connection open/close" contract
/// hold for a host that does not filter: those two events are the only `Info`
/// lines, and everything chatty is `Debug`/`Trace`.
void     set_log_threshold(LogLevel level);
LogLevel log_threshold();

namespace detail {

/// True when a sink is installed AND `level` passes the threshold. Checked
/// before any argument is turned into text.
bool log_wanted(LogLevel level);
void log_emit(LogLevel level, const std::string& message);

/// Minimal `{}` substitution — the same call-site shape spdlog/fmt use, so the
/// module's log lines did not have to be rewritten (and, more to the point,
/// their exact wording did not have to change) when the seam replaced
/// `core/log.h`. `{{` and `}}` are literal braces, as in fmt; anything between
/// a `{` and the next `}` is a format spec and is IGNORED rather than honoured,
/// because supporting specs would be the first step towards the framework this
/// header says it is not. Surplus arguments are dropped and surplus
/// placeholders are left as they are; neither is worth an assertion, because a
/// malformed trace line must never be an error — the code being traced is the
/// thing under test, not the trace.
///
/// Copies the literal run one char at a time. This is a log formatter behind a
/// level gate, so the loop never runs when tracing is off, and readability wins
/// over the memchr it could be.

/// Emit literal text up to the next placeholder. Returns a pointer to the `{`
/// that opens it, or to the terminating NUL if there is none.
inline const char* copy_literal(std::ostringstream& os, const char* f) {
    for (; *f != '\0'; ++f) {
        if (f[0] == '{' && f[1] == '{') { os << '{'; ++f; continue; }
        if (f[0] == '}' && f[1] == '}') { os << '}'; ++f; continue; }
        if (f[0] == '{') {
            // A `{` with no closing `}` is not a placeholder; print it.
            const char* close = f + 1;
            while (*close != '\0' && *close != '}') ++close;
            if (*close == '}') return f;
        }
        os << *f;
    }
    return f;
}

inline void format_into(std::ostringstream& os, const char* f) {
    // No arguments left, so any remaining `{}` is literal text.
    while (*f != '\0') {
        f = copy_literal(os, f);
        if (*f == '\0') return;
        const char* close = f + 1;
        while (*close != '\0' && *close != '}') ++close;
        os.write(f, close - f + 1);  // the whole unconsumed `{...}`
        f = close + 1;
    }
}

template <typename T, typename... Rest>
void format_into(std::ostringstream& os, const char* f, const T& value, const Rest&... rest) {
    f = copy_literal(os, f);
    if (*f == '\0') return;  // ran out of placeholders; surplus values dropped
    const char* close = f + 1;
    while (*close != '\0' && *close != '}') ++close;
    os << value;
    format_into(os, close + 1, rest...);
}

template <typename... Args>
void log_at(LogLevel level, const char* fmt, const Args&... args) {
    if (!log_wanted(level)) return;
    std::ostringstream os;
    format_into(os, fmt, args...);
    log_emit(level, os.str());
}

}  // namespace detail

template <typename... Args> void log_trace(const char* f, const Args&... a) {
    detail::log_at(LogLevel::Trace, f, a...);
}
template <typename... Args> void log_debug(const char* f, const Args&... a) {
    detail::log_at(LogLevel::Debug, f, a...);
}
template <typename... Args> void log_info(const char* f, const Args&... a) {
    detail::log_at(LogLevel::Info, f, a...);
}
template <typename... Args> void log_warn(const char* f, const Args&... a) {
    detail::log_at(LogLevel::Warn, f, a...);
}
template <typename... Args> void log_error(const char* f, const Args&... a) {
    detail::log_at(LogLevel::Error, f, a...);
}

/// `std::ostringstream` renders `std::uint8_t` as a CHARACTER, so a raw byte
/// has to be widened or hexed explicitly. Kept here rather than duplicated in
/// each .cpp because both halves of the module log bytes.
std::string log_hex_byte(std::uint8_t value);

}  // namespace esp
