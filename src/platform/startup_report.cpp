#include "platform/startup_report.h"

#include "core/cli_options.h"
#include "core/log.h"

#include <spdlog/sinks/base_sink.h>

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <vector>

namespace startup_report {

namespace {

constexpr size_t kMaxLines = 200;   // a runaway loop must not grow the buffer without bound

struct State {
    std::mutex mu;
    bool running = false;
    bool tap_installed = false;
    bool dialog_shown = false;
    std::vector<std::string> errors, warnings;
};
State& st() { static State s; return s; }

void keep(std::vector<std::string>& v, std::string line)
{
    while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) line.pop_back();
    if (line.empty() || v.size() >= kMaxLines) return;
    for (const auto& e : v) if (e == line) return;   // the same words from stderr and from a log
    v.push_back(std::move(line));
}

bool is_warning(const std::string& s) { return s.compare(0, 7, "warning") == 0; }

class Tap : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg& m) override {
        if (m.level < spdlog::level::warn) return;
        State& s = st();
        std::lock_guard<std::mutex> g(s.mu);
        if (s.running) return;
        std::string line(m.payload.data(), m.payload.size());
        keep(m.level >= spdlog::level::err ? s.errors : s.warnings, std::move(line));
    }
    void flush_() override {}
};

}  // namespace

void report(const char* fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    std::vfprintf(stderr, fmt, ap);
    va_end(ap);
    // The same formatting for the copy.
    const int n = std::vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    if (n <= 0) return;
    std::string text(static_cast<size_t>(n) + 1, '\0');
    va_list ap3;
    va_start(ap3, fmt);
    std::vsnprintf(text.data(), text.size(), fmt, ap3);
    va_end(ap3);
    text.resize(static_cast<size_t>(n));

    State& s = st();
    std::lock_guard<std::mutex> g(s.mu);
    if (s.running) return;
    // A message may hold several lines (a usage error and its hint): each is kept.
    size_t pos = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        const std::string line = text.substr(pos, nl == std::string::npos ? nl : nl - pos);
        pos = nl == std::string::npos ? text.size() : nl + 1;
        keep(is_warning(line) ? s.warnings : s.errors, line);
    }
}

void mark_running()
{
    State& s = st();
    std::lock_guard<std::mutex> g(s.mu);
    s.running = true;
}

void note_dialog_shown() { std::lock_guard<std::mutex> g(st().mu); st().dialog_shown = true; }
bool dialog_shown() { std::lock_guard<std::mutex> g(st().mu); return st().dialog_shown; }

void install_log_tap()
{
    State& s = st();
    {
        std::lock_guard<std::mutex> g(s.mu);
        if (s.tap_installed) return;
        s.tap_installed = true;
    }
    Log::add_tap(std::make_shared<Tap>());
}

bool should_show(int rc, bool windowed, bool unattended, bool running)
{
    return rc != 0 && windowed && !unattended && !running;
}

std::string text(int rc)
{
    State& s = st();
    std::lock_guard<std::mutex> g(s.mu);
    std::string out;
    for (const auto& e : s.errors) { out += e; out += '\n'; }
    for (const auto& w : s.warnings) { out += w; out += '\n'; }
    if (out.empty())
        return "jnext could not start (exit status " + std::to_string(rc) +
               "). Start it from a terminal, or with --log-file FILE, to see why.";
    while (!out.empty() && out.back() == '\n') out.pop_back();
    return out;
}

Launch classify(int argc, const char* const argv[])
{
    Launch l{true, false};
    for (int i = 1; i < argc; ++i) {
        const cli::Option* opt = cli::find(argv[i]);
        if (!opt) continue;
        if (opt->terminal) l.windowed = false;   // the command table says so (cli_options.h), not a list here
        switch (opt->id) {
        case cli::OptId::DelayedAutomaticExit:
        case cli::OptId::DelayedAutomaticExitFrames:
            l.unattended = true; break;
        default: break;
        }
        i += opt->arity;
    }
#ifdef __EMSCRIPTEN__
    l.windowed = false;
#endif
    return l;
}

void reset_for_test()
{
    State& s = st();
    std::lock_guard<std::mutex> g(s.mu);
    s.running = false;
    s.dialog_shown = false;
    s.errors.clear();
    s.warnings.clear();
}

bool running() { std::lock_guard<std::mutex> g(st().mu); return st().running; }

}  // namespace startup_report
