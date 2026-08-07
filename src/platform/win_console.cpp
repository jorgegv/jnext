#include "win_console.h"

#ifdef _WIN32

// jnext.exe is linked as a GUI-subsystem binary so a double-click launch from
// Explorer opens no console window. That, however, also detaches stdout/stderr
// when the exe *is* launched from an existing console (cmd/PowerShell), so CLI
// output (--version, --headless, --help) would otherwise vanish. Reattach to the
// parent console if there is one and reopen the C stdio streams onto it, so
// terminal invocations still print. A pure GUI launch has no parent console;
// AttachConsole fails harmlessly and we stay silent (the desired behaviour).
//
// <windows.h> is included ONLY here — it #defines macros (OUT, DELETE, ...) that
// clash with project identifiers, so it must not leak into any other TU.
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <io.h>
#include <iostream>

// Reopen one standard stream onto the newly-attached console, but ONLY when the
// shell did not already connect it. A GUI-subsystem child launched as
// `jnext.exe --version > out.txt` or `... | more` or `... > NUL` DOES get a
// valid redirected/piped handle from the shell, and that must be left alone —
// reopening onto CONOUT$/CONIN$ would silently discard the redirection.
//
// The question has to be asked of the CRT STREAM, not of GetStdHandle (GH #212).
// AttachConsole rewrites all three Win32 standard handles to fresh console
// handles unconditionally — including the ones the shell had redirected — so
// after the attach GetFileType() reports FILE_TYPE_CHAR for every stream and an
// "is it still unconnected?" test phrased in those terms can never fire. That is
// exactly what shipped: the guard always said "already connected", the freopen
// never ran, and `jnext --help` on Windows printed nothing at all.
//
// The CRT's own binding is the honest witness and AttachConsole does not touch
// it: a stream the shell left unconnected keeps _fileno() == -2 across the call,
// while a redirected one keeps its file/pipe/NUL handle on fd 1 or 2. Measured
// on all five launch shapes (plain console, `>file`, `|pipe`, `>NUL`, `2>file`).
static bool crt_stream_connected(FILE* stream) {
    const int fd = _fileno(stream);
    return fd >= 0 && _get_osfhandle(fd) != -1;
}

// Settle one standard stream after the attach. The two cases are exactly
// complementary, and the CRT binding decides which one applies:
//
//   connected   the shell gave this stream a file/pipe/NUL. The CRT still holds
//               it, but AttachConsole just pointed the WIN32 standard handle at
//               the console — so put the shell's handle back. Writers that go
//               through the Win32 handle rather than the CRT stream (spdlog's
//               wincolor sink is one) would otherwise print to the console and
//               leave the user's redirect file empty.
//   unconnected nothing to preserve; bind the CRT stream to the console that
//               AttachConsole just gave us.
//
// Returns true when the stream was bound to the console (the unconnected case)
// — the completion signal below only makes sense when at least one OUTPUT
// stream actually prints onto the parent console.
static bool settle_stream(DWORD std_id, HANDLE shell_handle,
                          const char* dev, const char* mode, FILE* stream) {
    if (crt_stream_connected(stream)) {
        SetStdHandle(std_id, shell_handle);
        return false;
    }
    FILE* f;
    freopen_s(&f, dev, mode, stream);
    return true;
}

// --- CLI completion signal (GH #212, reopened) -------------------------------
//
// Getting the text onto the console (above) is only half of a working CLI
// invocation. cmd.exe does not WAIT for a GUI-subsystem child launched from an
// interactive prompt: it prints its next prompt immediately, jnext's output
// lands AFTER that prompt, and when jnext exits nothing redraws it — so the
// session looks stuck until the user presses Enter. That is exactly what the
// reporter observed ("--help ... doesn't exit. It waits on a keypress").
//
// The child cannot make cmd wait, but it can hand the user their prompt back:
// on exit, post one Enter key into the console INPUT buffer
// (WriteConsoleInputW). cmd is already blocked in ReadConsole on that same
// buffer, reads an empty line, and prints a fresh prompt — the ordinary
// "command finished" experience. PowerShell plausibly benefits the same way,
// but that is unverified: the reporter's case and the packaging rows exercise
// cmd.exe only.
//
// Deliberately narrow, because an injected Enter submits whatever is on the
// shell's pending line:
//   * only when the attach succeeded AND at least one output stream was bound
//     to the console — if everything was redirected, nothing printed after the
//     prompt, the screen is not disturbed, and no signal is needed;
//   * only for invocations that exit during argument parsing/validation
//     (--help, --version, unknown options, bad values):
//     win_console_note_app_running() disarms the signal immediately before
//     the first potentially-slow phase — SD-card provisioning, which can sit
//     in a confirm prompt or a download for minutes — and everything after it
//     (the GUI/headless session) stays disarmed too. Anything slow is long
//     enough for the user to have typed a real command into the waiting
//     shell, which the injected Enter would submit; quick errors that happen
//     past provisioning therefore do not signal either — the safe direction.
//     The armed window is milliseconds.
static bool s_signal_cli_completion = false;

void win_console_note_app_running() {
    s_signal_cli_completion = false;
}

static void signal_cli_completion() {
    if (!s_signal_cli_completion)
        return;
    // Everything must be on the screen before the prompt returns under it.
    fflush(stdout);
    fflush(stderr);
    // Fresh CONIN$ handle: stdin may have been left on a shell redirect.
    // WriteConsoleInput needs GENERIC_WRITE on a console INPUT handle.
    HANDLE con_in = CreateFileW(L"CONIN$", GENERIC_WRITE,
                                FILE_SHARE_READ | FILE_SHARE_WRITE,
                                nullptr, OPEN_EXISTING, 0, nullptr);
    if (con_in == INVALID_HANDLE_VALUE)
        return;
    INPUT_RECORD rec[2] = {};
    for (int i = 0; i < 2; ++i) {
        rec[i].EventType                        = KEY_EVENT;
        rec[i].Event.KeyEvent.bKeyDown          = (i == 0);
        rec[i].Event.KeyEvent.wRepeatCount      = 1;
        rec[i].Event.KeyEvent.wVirtualKeyCode   = VK_RETURN;
        rec[i].Event.KeyEvent.wVirtualScanCode  =
            static_cast<WORD>(MapVirtualKeyW(VK_RETURN, MAPVK_VK_TO_VSC));
        rec[i].Event.KeyEvent.uChar.UnicodeChar = L'\r';
    }
    DWORD written = 0;
    WriteConsoleInputW(con_in, rec, 2, &written);
    CloseHandle(con_in);
}

void win_attach_parent_console() {
    // Snapshot before attaching — this is the last moment at which the standard
    // handles still describe what the shell handed us.
    const HANDLE shell_in  = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE shell_out = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE shell_err = GetStdHandle(STD_ERROR_HANDLE);

    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;   // launched without a console (e.g. Explorer double-click)

    const bool out_on_console =
        settle_stream(STD_OUTPUT_HANDLE, shell_out, "CONOUT$", "w", stdout);
    const bool err_on_console =
        settle_stream(STD_ERROR_HANDLE,  shell_err, "CONOUT$", "w", stderr);
    settle_stream(STD_INPUT_HANDLE,  shell_in,  "CONIN$",  "r", stdin);
    std::ios::sync_with_stdio(true);

    if (out_on_console || err_on_console) {
        s_signal_cli_completion = true;
        // atexit covers both exit() and returning from main(); a crash path
        // (abort/SIGSEGV) skips it, which is correct — a crashed CLI should
        // not pretend to have completed.
        std::atexit(signal_cli_completion);
    }
}

#endif // _WIN32
