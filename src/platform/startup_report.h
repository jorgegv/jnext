#pragma once
// Fatal start-up errors the user can see (GH #555).
//
// A desktop launch has no terminal, so a start-up error that only goes to stderr (an unknown
// option left in a launcher, an SD card that cannot be used, an image another jnext holds) looks
// like jnext doing nothing. main() sends every message it used to fprintf(stderr, ...) through
// report(), which prints it BYTE-IDENTICALLY and keeps a copy until the frontend's loop starts.
// A logger tap keeps the error and warning lines the loggers emit meanwhile (Emulator::init, the
// ROM extraction). When a windowed run then ends with a non-zero status, main() shows the copy in
// the frontend's own message box.
//
// Toolkit-free: the box itself is in src/gui/startup_error_box (Qt) and startup_error_sdl (SDL).
#include <string>

namespace startup_report {

/// printf to stderr, byte-identical to fprintf(stderr, ...), and a copy in the buffer while the
/// run has not started. A line that begins "warning" is a warning, any other line an error.
#if defined(__GNUC__)
void report(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#else
void report(const char* fmt, ...);
#endif

/// The frontend's loop is about to start: nothing is buffered from now on.
void mark_running();

/// An error dialog of its own was already shown (the SD download failure's): the start-up box
/// must not stack a second one on it.
void note_dialog_shown();
bool dialog_shown();

/// Install the logger tap (warn and error lines buffered while not running). Once.
void install_log_tap();

/// Whether a box should be shown for an exit status: only a failed, windowed, attended run that
/// never started its loop.
bool should_show(int rc, bool windowed, bool unattended, bool running);

/// What the box says: the errors first, then the warnings, one per line; with nothing buffered,
/// the fallback that points at a terminal and --log-file.
std::string text(int rc);

/// The two facts about a command line that decide whether a box is ever shown, read without
/// running the parse loop: a windowed run is one with no --headless that is not a terminal
/// command (--help, --version, --sdcard-file-add); an unattended one has a --delayed-automatic-exit*.
/// A run whose stderr is a terminal is never windowed here: whoever started it from a shell reads
/// the error where they are, and a blocking box would only wait for a click in front of a script
/// (or, in a test run, on a desktop nobody is looking at). The box is for a desktop launch, whose
/// stderr goes nowhere.
struct Launch { bool windowed; bool unattended; };
Launch classify(int argc, const char* const argv[], bool stderr_is_tty = false);

/// Drop everything (the unit suite, between cases).
void reset_for_test();
bool running();   // mark_running() was called

}  // namespace startup_report
