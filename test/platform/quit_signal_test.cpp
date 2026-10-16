// ===========================================================================
// quit_signal suite (GH #364): SIGINT/SIGTERM as a quit request.
//
// No VHDL oracle: host process behaviour. Oracles:
//   POSIX sigaction  -- SA_RESETHAND resets the disposition to SIG_DFL on
//                       delivery; a SIG_IGN disposition discards the signal;
//                       a process killed by signal N has WIFSIGNALED and
//                       WTERMSIG == N.
//   SDL3 3.4 SDL_quit.c -- SDL_EventSignal_Init installs its handler only over
//                       SIG_DFL; SDL_EventSignal_Quit restores only its own;
//                       SDL_SendPendingSignalEvents queues SDL_EVENT_QUIT on
//                       the next pump.
//
// Every row runs in a fork()ed child so it starts from fresh dispositions; the
// child reports through its exit status and the parent reaps it with a bound.
//
// Discriminative -- mutation applied to the product, reverted from a file copy:
//   drop SA_RESETHAND                         -> QSIG-03 fails
//   install over an inherited SIG_IGN         -> QSIG-04 fails
//   install only over SIG_DFL (SDL's rule)    -> QSIG-07 fails
//   reraise_if_pending() does nothing         -> QSIG-05 fails
//   handler re-raises at once                 -> QSIG-01/02 fail
//   reraise_if_pending() without its SIG_DFL   -> QSIG-10 fails
//   handler keeps the last signal / does not reset the OTHER signal -> QSIG-11/12 fail
// ===========================================================================

#include "platform/quit_signal.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <sys/wait.h>
#include <unistd.h>

#include "../row_id.h"

namespace {

int g_total = 0, g_pass = 0, g_fail = 0;

void check(const char* id, const char* desc, bool cond) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
        std::printf("  PASS %s: %s\n", id, desc);
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s\n", id, desc);
    }
}

// Run `body` in a child; its return value is the child's exit status. Bounded:
// a child still running after 5 s is SIGKILLed and reported as status -1.
template <class F>
int run_child(F body, bool* signalled = nullptr, int* termsig = nullptr) {
    std::fflush(stdout);
    const pid_t pid = ::fork();
    if (pid < 0) return -2;
    if (pid == 0) {
        ::_exit(body());
    }
    int status = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    for (;;) {
        const pid_t r = ::waitpid(pid, &status, WNOHANG);
        if (r == pid) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (signalled) *signalled = WIFSIGNALED(status);
    if (termsig) *termsig = WIFSIGNALED(status) ? WTERMSIG(status) : 0;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -3;
}

// Child exit codes: 0 = expectation held, 1 = it did not.
int survives_with(int sig) {
    quit_signal::install();
    std::raise(sig);
    return quit_signal::pending() == sig ? 0 : 1;
}

int drain_quit_events() {
    int quits = 0;
    SDL_Event e;
    while (SDL_PollEvent(&e)) if (e.type == SDL_EVENT_QUIT) ++quits;
    return quits;
}

}  // namespace

int main() {
    std::printf("GH #364 - SIGINT/SIGTERM as a quit request\n");
    std::printf("=========================================================================\n\n");

    ::setenv("SDL_VIDEODRIVER", "dummy", 1);
    ::setenv("SDL_AUDIODRIVER", "dummy", 1);

    check("QSIG-01", "install(), raise(SIGTERM): survives, pending()==SIGTERM",
          run_child([] { return survives_with(SIGTERM); }) == 0);

    check("QSIG-02", "install(), raise(SIGINT): survives, pending()==SIGINT",
          run_child([] {
              // A harness may start us with SIGINT ignored (background job of a
              // non-interactive shell); undo that, QSIG-04 owns the ignored case.
              std::signal(SIGINT, SIG_DFL);
              return survives_with(SIGINT);
          }) == 0);

    {
        bool sigd = false; int ts = 0;
        run_child([] {
            quit_signal::install();
            std::raise(SIGTERM);
            std::raise(SIGTERM);   // SA_RESETHAND: default action now
            return 0;
        }, &sigd, &ts);
        check("QSIG-03", "a second SIGTERM kills by the default action (SA_RESETHAND)",
              sigd && ts == SIGTERM);
    }

    check("QSIG-04", "an inherited SIG_IGN is kept: signal discarded, nothing pending",
          run_child([] {
              std::signal(SIGINT, SIG_IGN);
              quit_signal::install();
              std::raise(SIGINT);
              struct sigaction cur;
              sigaction(SIGINT, nullptr, &cur);
              return (quit_signal::pending() == 0 && cur.sa_handler == SIG_IGN) ? 0 : 1;
          }) == 0);

    {
        bool sigd = false; int ts = 0;
        run_child([] {
            quit_signal::install();
            std::raise(SIGTERM);
            quit_signal::reraise_if_pending();
            return 0;
        }, &sigd, &ts);
        check("QSIG-05", "reraise_if_pending() ends the process by the pending signal",
              sigd && ts == SIGTERM);
    }

    check("QSIG-06", "reraise_if_pending() with nothing pending is a no-op",
          run_child([] {
              quit_signal::install();
              quit_signal::reraise_if_pending();
              return 0;
          }) == 0);

    check("QSIG-07", "SDL_Init first, then install(): our handler wins, no SDL_EVENT_QUIT",
          run_child([] {
              if (!SDL_Init(SDL_INIT_EVENTS)) return 2;
              quit_signal::install();
              std::raise(SIGTERM);
              const int quits = drain_quit_events();
              return (quit_signal::pending() == SIGTERM && quits == 0) ? 0 : 1;
          }) == 0);

    check("QSIG-08", "install() first, SDL_Init + SDL_Quit after: SDL leaves the handler alone",
          run_child([] {
              quit_signal::install();
              if (!SDL_Init(SDL_INIT_EVENTS)) return 2;
              SDL_Quit();
              std::raise(SIGTERM);
              return quit_signal::pending() == SIGTERM ? 0 : 1;
          }) == 0);

    check("QSIG-09", "control, no install(): SDL swallows SIGTERM into one SDL_EVENT_QUIT",
          run_child([] {
              if (!SDL_Init(SDL_INIT_EVENTS)) return 2;
              std::raise(SIGTERM);
              return drain_quit_events() == 1 ? 0 : 1;
          }) == 0);

    {
        bool sigd = false; int ts = 0;
        run_child([] {
            quit_signal::install();
            std::raise(SIGTERM);             // handler ran; SA_RESETHAND -> SIG_DFL
            if (!SDL_Init(SDL_INIT_EVENTS)) return 2;   // installs over SIG_DFL, no SDL_Quit
            quit_signal::reraise_if_pending();
            return 0;
        }, &sigd, &ts);
        check("QSIG-10", "a signal before SDL_Init, then init fails without SDL_Quit: the re-raise still kills (SDL's handler is bypassed)",
              sigd && ts == SIGTERM);
    }

    {
        bool sigd = false; int ts = 0;
        run_child([] {
            std::signal(SIGINT, SIG_DFL);   // a harness may hand us SIGINT ignored
            quit_signal::install();
            std::raise(SIGINT);
            std::raise(SIGTERM);            // the OTHER signal is a second signal too
            return 0;
        }, &sigd, &ts);
        check("QSIG-11", "SIGINT then SIGTERM: the second, different signal kills by the default action",
              sigd && ts == SIGTERM);
    }

    {
        bool sigd = false; int ts = 0;
        run_child([] {
            std::signal(SIGINT, SIG_DFL);
            quit_signal::install();
            std::raise(SIGTERM);
            std::raise(SIGINT);
            return 0;
        }, &sigd, &ts);
        check("QSIG-12", "SIGTERM then SIGINT: the second, different signal kills by the default action",
              sigd && ts == SIGINT);
    }

    check("QSIG-13", "an inherited SIGTERM ignore survives a SIGINT: INT pending, TERM discarded and still SIG_IGN",
          run_child([] {
              std::signal(SIGINT, SIG_DFL);
              std::signal(SIGTERM, SIG_IGN);
              quit_signal::install();
              std::raise(SIGINT);
              std::raise(SIGTERM);
              struct sigaction cur;
              sigaction(SIGTERM, nullptr, &cur);
              return (quit_signal::pending() == SIGINT && cur.sa_handler == SIG_IGN) ? 0 : 1;
          }) == 0);

    std::printf("\nTotal: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n",
                g_total, g_pass, g_fail, 0);
    return g_fail == 0 ? 0 : 1;
}
