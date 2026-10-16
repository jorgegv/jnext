#pragma once

// SIGINT / SIGTERM as a request to quit (GH #364).
//
// SDL3 installs a persistent SIGINT/SIGTERM handler of its own in SDL_Init()
// that only queues SDL_EVENT_QUIT. The Qt frontend never reads that event, so
// the signal was swallowed and the GUI could only be killed with SIGKILL; the
// SDL frontend exited with status 0; headless died with its recordings
// unfinalized. install() takes both signals over for every frontend, each
// frontend's frame loop polls pending() once per iteration and leaves through
// its normal teardown, and main() ends with reraise_if_pending() so the parent
// sees the signal (status 128+N, WIFSIGNALED), never a success.
//
// Rules: a signal that was SIG_IGN at start stays ignored (nohup, background
// jobs of a non-interactive shell; SDL applies the same rule). Anything else is
// taken over whether or not SDL's handler is already there, so the result does
// not depend on the call order relative to SDL_Init. SA_RESETHAND: a second
// signal gets the default action at once, the escape hatch when the orderly
// exit hangs. SIGHUP is untouched.
//
// Windows: all four functions are no-ops, exempt for cause. No SIGTERM can be
// delivered to a Windows process from outside (taskkill without /F posts
// WM_CLOSE, which Qt already handles as a window close); Ctrl+C reaches the
// GUI exe only through the borrowed parent console and is addressed to the
// shell, so SDL's CRT handler swallowing it is the right result.

namespace quit_signal {

// Install the handlers. Call once, before any frontend's init() (SDL_Init).
void install();

// The first signal received (SIGINT or SIGTERM), or 0. Sticky.
int pending();

// "SIGTERM" / "SIGINT" (strsignal is locale-dependent); "signal" otherwise.
const char* name(int sig);

// If a signal is pending: restore its default action, unblock it and raise it.
// Returns only if the process survived (or nothing was pending).
void reraise_if_pending();

}  // namespace quit_signal
