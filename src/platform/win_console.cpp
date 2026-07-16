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

void win_attach_parent_console() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS))
        return;   // launched without a console (e.g. Explorer double-click)
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    freopen_s(&f, "CONOUT$", "w", stderr);
    freopen_s(&f, "CONIN$",  "r", stdin);
    std::ios::sync_with_stdio(true);
}

#endif // _WIN32
