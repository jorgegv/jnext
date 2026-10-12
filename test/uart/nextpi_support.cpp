#include "nextpi_support.h"

#include <chrono>
#include <cstring>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fstream>
extern char** environ;
#endif

namespace nxs {

namespace {
// The child logs to stdout too; its signal is the line `ready` (or `failed: ...`).
bool is_signal(const std::string& l) { return l.rfind("ready", 0) == 0 || l.rfind("failed", 0) == 0; }
}

#ifdef _WIN32

bool process_alive(unsigned long pid) {
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
}

void kill_hard(unsigned long pid) {
    if (HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, static_cast<DWORD>(pid))) {
        TerminateProcess(h, 137);
        CloseHandle(h);
    }
}

unsigned long self_pid() { return GetCurrentProcessId(); }

long process_cpu_ms() {
    FILETIME create, exit_t, kernel, user;
    if (!GetProcessTimes(GetCurrentProcess(), &create, &exit_t, &kernel, &user)) return -1;
    auto ms = [](const FILETIME& f) {
        return static_cast<long long>((static_cast<unsigned long long>(f.dwHighDateTime) << 32 | f.dwLowDateTime) / 10000);
    };
    return static_cast<long>(ms(kernel) + ms(user));
}

bool Child::start(const std::string& exe, const std::vector<std::string>& args,
                  std::string& first_line, int timeout_ms) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return false;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOA si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = wr;
    si.hStdError  = wr;
    std::string cmd = "\"" + exe + "\"";
    for (const std::string& a : args) cmd += " \"" + a + "\"";
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                   nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) {
        CloseHandle(rd);
        return false;
    }
    CloseHandle(pi.hThread);
    process_  = pi.hProcess;
    pid_      = pi.dwProcessId;
    read_end_ = reinterpret_cast<intptr_t>(rd);
    first_line.clear();
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(timeout_ms)) {
        DWORD avail = 0;
        if (!PeekNamedPipe(rd, nullptr, 0, nullptr, &avail, nullptr)) break;   // the child ended
        if (avail > 0) {
            char c;
            DWORD got = 0;
            while (avail-- > 0 && ReadFile(rd, &c, 1, &got, nullptr) && got == 1) {
                if (c == '\n') {
                    if (is_signal(first_line)) return true;
                    first_line.clear();
                } else if (c != '\r') {
                    first_line.push_back(c);
                }
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return false;
}

void Child::kill() {
    if (pid_) kill_hard(pid_);
    if (process_) {
        WaitForSingleObject(static_cast<HANDLE>(process_), 5000);
        CloseHandle(static_cast<HANDLE>(process_));
        process_ = nullptr;
    }
    if (read_end_ >= 0) {
        CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(read_end_)));
        read_end_ = -1;
    }
    pid_ = 0;
}

MagicPipe::MagicPipe() {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return;
    DWORD put = 0;
    WriteFile(wr, "JNEXTMAG", 8, &put, nullptr);
    CloseHandle(wr);
    number_ = static_cast<long>(reinterpret_cast<intptr_t>(rd));
}

MagicPipe::~MagicPipe() {
    if (number_ >= 0) CloseHandle(reinterpret_cast<HANDLE>(static_cast<intptr_t>(number_)));
}

#else

bool process_alive(unsigned long pid) {
    if (::kill(static_cast<pid_t>(pid), 0) != 0) return false;
#ifdef __linux__
    // An exited child nobody has reaped yet still answers kill(2).
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    std::getline(f, line);
    const std::size_t at = line.rfind(')');
    if (at != std::string::npos && at + 2 < line.size() && line[at + 2] == 'Z') return false;
#endif
    return true;
}

void kill_hard(unsigned long pid) { ::kill(static_cast<pid_t>(pid), SIGKILL); }

unsigned long self_pid() { return static_cast<unsigned long>(::getpid()); }

long process_cpu_ms() {
    rusage ru{};
    if (::getrusage(RUSAGE_SELF, &ru) != 0) return -1;
    return static_cast<long>(ru.ru_utime.tv_sec * 1000 + ru.ru_utime.tv_usec / 1000 +
                             ru.ru_stime.tv_sec * 1000 + ru.ru_stime.tv_usec / 1000);
}

bool Child::start(const std::string& exe, const std::vector<std::string>& args,
                  std::string& first_line, int timeout_ms) {
    int fds[2];
    if (::pipe(fds) != 0) return false;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);
    std::vector<std::string> argv_s{exe};
    argv_s.insert(argv_s.end(), args.begin(), args.end());
    std::vector<char*> argv;
    for (std::string& a : argv_s) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = -1;
    const int rc = ::posix_spawn(&pid, exe.c_str(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);
    if (rc != 0) {
        ::close(fds[0]);
        return false;
    }
    pid_      = static_cast<unsigned long>(pid);
    read_end_ = fds[0];
    first_line.clear();
    const auto t0 = std::chrono::steady_clock::now();
    for (;;) {
        const long left = timeout_ms - std::chrono::duration_cast<std::chrono::milliseconds>(
                                           std::chrono::steady_clock::now() - t0).count();
        if (left <= 0) return false;
        pollfd p{fds[0], POLLIN, 0};
        if (::poll(&p, 1, static_cast<int>(left)) <= 0) return false;
        char c;
        if (::read(fds[0], &c, 1) != 1) return false;      // EOF: the child ended
        if (c == '\n') {
            if (is_signal(first_line)) return true;
            first_line.clear();
        } else {
            first_line.push_back(c);
        }
    }
}

void Child::kill() {
    if (pid_) {
        kill_hard(pid_);
        int st = 0;
        ::waitpid(static_cast<pid_t>(pid_), &st, 0);
    }
    if (read_end_ >= 0) ::close(static_cast<int>(read_end_));
    read_end_ = -1;
    pid_      = 0;
}

MagicPipe::MagicPipe() {
    int fds[2];
    if (::pipe(fds) != 0) return;
    if (::write(fds[1], "JNEXTMAG", 8) != 8) {
        ::close(fds[0]);
        ::close(fds[1]);
        return;
    }
    ::close(fds[1]);
    // Above the descriptors the launcher itself keeps, and with no
    // close-on-exec flag: F_DUPFD, unlike F_DUPFD_CLOEXEC, leaves it clear.
    const int high = ::fcntl(fds[0], F_DUPFD, 10);
    ::close(fds[0]);
    number_ = high;
}

MagicPipe::~MagicPipe() {
    if (number_ >= 0) ::close(static_cast<int>(number_));
}

#endif

bool gone_within(unsigned long pid, int ms) {
    const auto t0 = std::chrono::steady_clock::now();
    do {
        if (!process_alive(pid)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    } while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(ms));
    return !process_alive(pid);
}

Child::~Child() { kill(); }

PipeSquat::PipeSquat(const std::vector<std::string>& names) {
#ifdef _WIN32
    for (const std::string& n : names) {
        HANDLE h = CreateNamedPipeA(n.c_str(), PIPE_ACCESS_DUPLEX, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0,
                                    nullptr);
        if (h != INVALID_HANDLE_VALUE) handles_.push_back(h);
    }
#else
    (void)names;
#endif
}

PipeSquat::~PipeSquat() {
#ifdef _WIN32
    for (void* h : handles_) CloseHandle(static_cast<HANDLE>(h));
#endif
}

} // namespace nxs
