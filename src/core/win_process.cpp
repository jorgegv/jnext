#include "core/win_process.h"

#ifdef _WIN32

// <windows.h> is included ONLY here — it #defines macros (OUT, DELETE, ERROR,
// ...) that clash with project and spdlog identifiers, so it must not leak into
// any other translation unit. Same rule as src/platform/win_console.cpp.
#include <windows.h>

#include <vector>

int win_run_hidden(const std::string& command_line)
{
    // Single NUL handle used for the child's stdin, stdout and stderr: ffmpeg
    // is chatty on stderr and there is no console to print it to, and an
    // inherited-but-invalid stdin makes ffmpeg's interactive-key reader spin.
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE nul = CreateFileA("NUL", GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    const BOOL inherit = (nul != INVALID_HANDLE_VALUE) ? TRUE : FALSE;

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    if (inherit) {
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdInput  = nul;
        si.hStdOutput = nul;
        si.hStdError  = nul;
    }

    PROCESS_INFORMATION pi{};

    // CreateProcess may WRITE to lpCommandLine, so it gets a mutable copy.
    std::vector<char> cmd(command_line.begin(), command_line.end());
    cmd.push_back('\0');

    // lpApplicationName is null so the first token ("ffmpeg") is resolved
    // against PATH with ".exe" appended, matching what a shell would do.
    const BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr,
                                   inherit, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &si, &pi);
    if (!ok) {
        if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exit_code = static_cast<DWORD>(-1);
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul);
    return static_cast<int>(exit_code);
}

#endif  // _WIN32
