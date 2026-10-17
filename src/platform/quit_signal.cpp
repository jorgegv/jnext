#include "platform/quit_signal.h"

#include <cerrno>
#include <csignal>
#include <signal.h>

#ifndef _WIN32

#include <atomic>
#include <csignal>

namespace {

// A lock-free atomic is async-signal-safe and visible across threads (on macOS
// a signal can be delivered to any thread).
std::atomic<int> g_signal{0};
static_assert(std::atomic<int>::is_always_lock_free);

// Which of SIGINT / SIGTERM install() took over (not an inherited SIG_IGN).
std::atomic<bool> g_ours_int{false};
std::atomic<bool> g_ours_term{false};
static_assert(std::atomic<bool>::is_always_lock_free);

extern "C" void on_quit_signal(int sig) {
    const int saved_errno = errno;   // handler hygiene: sigaction below may set it
    int expected = 0;
    g_signal.compare_exchange_strong(expected, sig);   // keep the first one
    // Any SECOND quit signal is the escape hatch, whichever one it is: put the
    // OTHER signal back to its default action too (SA_RESETHAND only resets
    // the one being delivered). sigaction is async-signal-safe. An inherited
    // SIG_IGN was never taken over, so it stays ignored.
    const int  other = (sig == SIGINT) ? SIGTERM : SIGINT;
    const bool ours  = (other == SIGINT) ? g_ours_int.load() : g_ours_term.load();
    if (ours) {
        struct sigaction dfl;
        sigemptyset(&dfl.sa_mask);
        dfl.sa_handler = SIG_DFL;
        dfl.sa_flags   = 0;
        sigaction(other, &dfl, nullptr);
    }
    errno = saved_errno;
}

void install_one(int sig) {
    struct sigaction cur;
    if (sigaction(sig, nullptr, &cur) != 0) return;
    if (cur.sa_handler == SIG_IGN) return;   // inherited ignore stays
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = on_quit_signal;
    sa.sa_flags   = SA_RESETHAND | SA_RESTART;
    (sig == SIGINT ? g_ours_int : g_ours_term).store(true);
    sigaction(sig, &sa, nullptr);
}

}  // namespace

namespace quit_signal {

void install() {
    // Block both while the two handlers go in, so a signal landing between
    // them is delivered once both are in place (and still gets the
    // second-signal reset); restore the caller's mask, not a blind unblock.
    sigset_t both, old;
    sigemptyset(&both);
    sigaddset(&both, SIGINT);
    sigaddset(&both, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &both, &old);
    install_one(SIGINT);
    install_one(SIGTERM);
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
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
