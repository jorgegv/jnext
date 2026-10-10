// Run the real jnext binary with a time bound, on every OS (GH #214).
//
// cli_options_test drives the shipped binary. A mis-parsed flag can land in a
// normal emulator run that never returns, so every invocation is bounded --
// with SIGKILL escalation, because a bare SIGTERM bound is decorative (see the
// comment in cli_options_test.cpp and test/lint-timeouts.sh).
//
//   POSIX    the shell line the suite always used: `timeout --kill-after=5s
//            20s EXE ARGS >OUT 2>ERR </dev/null` (the caller has checked that
//            timeout(1) exists).
//   Windows  CreateProcess with the child's stdin on NUL and stdout / stderr on
//            the given files (NUL when empty), killed after the same 20 s.
//
// run_bounded() returns 0 when the program exited 0; anything else (a non-zero
// exit, a timeout, a failure to start) is non-zero. Only zero / non-zero is
// meaningful, as with the std::system() status it replaces.
#pragma once

#include <cstdlib>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace jtp {

inline int run_bounded(const std::string& exe, const std::string& args,
                       const std::string& out_path = {}, const std::string& err_path = {}) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};   // inheritable handles
    auto open_for_write = [&](const std::string& path) {
        return ::CreateFileA(path.empty() ? "NUL" : path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    };
    HANDLE in  = ::CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE out = open_for_write(out_path);
    HANDLE err = open_for_write(err_path);
    int rc = -1;
    if (in != INVALID_HANDLE_VALUE && out != INVALID_HANDLE_VALUE && err != INVALID_HANDLE_VALUE) {
        STARTUPINFOA si{};
        si.cb         = sizeof(si);
        si.dwFlags    = STARTF_USESTDHANDLES;
        si.hStdInput  = in;
        si.hStdOutput = out;
        si.hStdError  = err;
        PROCESS_INFORMATION pi{};
        std::string cmd = "\"" + exe + "\" " + args;   // CreateProcess may edit it
        if (::CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                             nullptr, nullptr, &si, &pi)) {
            if (::WaitForSingleObject(pi.hProcess, 20000) == WAIT_TIMEOUT) {
                ::TerminateProcess(pi.hProcess, 124);
                ::WaitForSingleObject(pi.hProcess, 5000);
                rc = 124;
            } else {
                DWORD code = 1;
                ::GetExitCodeProcess(pi.hProcess, &code);
                rc = static_cast<int>(code);
            }
            ::CloseHandle(pi.hProcess);
            ::CloseHandle(pi.hThread);
        }
    }
    for (HANDLE h : {in, out, err})
        if (h != INVALID_HANDLE_VALUE) ::CloseHandle(h);
    return rc;
#else
    const std::string o = out_path.empty() ? "/dev/null" : "'" + out_path + "'";
    const std::string e = err_path.empty() ? "/dev/null" : "'" + err_path + "'";
    return std::system(("timeout --kill-after=5s 20s '" + exe + "' " + args +
                        " >" + o + " 2>" + e + " </dev/null").c_str());
#endif
}

}  // namespace jtp
