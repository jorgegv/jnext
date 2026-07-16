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
#include <iostream>

// Reopen one standard stream onto the newly-attached console, but ONLY when the
// shell did not already give it a handle. AttachConsole succeeding is
// independent of redirection: a GUI-subsystem child launched as
// `jnext.exe --version > out.txt` or `... | tee log` or `echo y | jnext.exe`
// still gets valid redirected/piped std handles from the shell, and those must
// be left alone — reopening onto CONOUT$/CONIN$ would silently discard the
// redirection/pipe. A stream the shell left unconnected reports FILE_TYPE_UNKNOWN
// (invalid handle); only then do we synthesize a console-backed stream.
static void reopen_if_unconnected(DWORD std_handle, const char* dev,
                                  const char* mode, FILE* stream) {
    if (GetFileType(GetStdHandle(std_handle)) == FILE_TYPE_UNKNOWN) {
        FILE* f;
        freopen_s(&f, dev, mode, stream);
    }
}

void win_attach_parent_console() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;   // launched without a console (e.g. Explorer double-click)
    reopen_if_unconnected(STD_OUTPUT_HANDLE, "CONOUT$", "w", stdout);
    reopen_if_unconnected(STD_ERROR_HANDLE,  "CONOUT$", "w", stderr);
    reopen_if_unconnected(STD_INPUT_HANDLE,  "CONIN$",  "r", stdin);
    std::ios::sync_with_stdio(true);
}

#endif // _WIN32
