#include "platform/quit_signal.h"

#include <csignal>

#ifndef _WIN32

#include <atomic>
#include <csignal>

namespace {

// A lock-free atomic is async-signal-safe and visible across threads (on macOS
// a signal can be delivered to any thread).
std::atomic<int> g_signal{0};
static_assert(std::atomic<int>::is_always_lock_free);

extern "C" void on_quit_signal(int sig) {
    int expected = 0;
    g_signal.compare_exchange_strong(expected, sig);   // keep the first one
}

void install_one(int sig) {
    struct sigaction cur;
    if (sigaction(sig, nullptr, &cur) != 0) return;
    if (cur.sa_handler == SIG_IGN) return;   // inherited ignore stays
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_quit_signal;
    sa.sa_flags   = SA_RESETHAND | SA_RESTART;
    sigaction(sig, &sa, nullptr);
}

}  // namespace

namespace quit_signal {

void install() {
    install_one(SIGINT);
    install_one(SIGTERM);
}

int pending() { return g_signal.load(); }

void reraise_if_pending() {
    const int sig = g_signal.load();
    if (sig == 0) return;
    std::signal(sig, SIG_DFL);
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, sig);
    sigprocmask(SIG_UNBLOCK, &set, nullptr);
    std::raise(sig);
}

}  // namespace quit_signal

#else   // _WIN32: see the header

namespace quit_signal {
void install() {}
int pending() { return 0; }
void reraise_if_pending() {}
}  // namespace quit_signal

#endif

namespace quit_signal {

const char* name(int sig) {
    switch (sig) {
        case SIGINT:  return "SIGINT";
        case SIGTERM: return "SIGTERM";
        default: return "signal";
    }
}

}  // namespace quit_signal
