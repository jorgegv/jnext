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
static void settle_stream(DWORD std_id, HANDLE shell_handle,
                          const char* dev, const char* mode, FILE* stream) {
    if (crt_stream_connected(stream)) {
        SetStdHandle(std_id, shell_handle);
    } else {
        FILE* f;
        freopen_s(&f, dev, mode, stream);
    }
}

void win_attach_parent_console() {
    // Snapshot before attaching — this is the last moment at which the standard
    // handles still describe what the shell handed us.
    const HANDLE shell_in  = GetStdHandle(STD_INPUT_HANDLE);
    const HANDLE shell_out = GetStdHandle(STD_OUTPUT_HANDLE);
    const HANDLE shell_err = GetStdHandle(STD_ERROR_HANDLE);

    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;   // launched without a console (e.g. Explorer double-click)

    settle_stream(STD_OUTPUT_HANDLE, shell_out, "CONOUT$", "w", stdout);
    settle_stream(STD_ERROR_HANDLE,  shell_err, "CONOUT$", "w", stderr);
    settle_stream(STD_INPUT_HANDLE,  shell_in,  "CONIN$",  "r", stdin);
    std::ios::sync_with_stdio(true);
}

#endif // _WIN32
