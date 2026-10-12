// The Windows launch of NextPi under QEMU (GH #316). The POSIX twin is in
// pi_qemu.cpp; the design is doc/design/NEXTPI-DESIGN.md §3.5 and the comment
// on the class in pi_qemu.h.
//
// <windows.h> is included ONLY here (the rule win_process.cpp states): it
// #defines names that clash with project and spdlog identifiers.
#include "core/pi_qemu.h"

#ifdef _WIN32

#include "core/log.h"
#include "core/video_recorder.h"

#include <windows.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <system_error>

namespace fs = std::filesystem;

namespace {

const std::string kInstallHint =
    " not found — install QEMU for Windows (https://qemu.weilnetz.de/w64/) and either add its "
    "folder to PATH or set the QEMU binary under Settings > Preferences > NextPi";

/// A narrow string as the wide one the filesystem layer would make of it, so a
/// path means the same to CreateProcessW as it does to std::filesystem.
std::wstring to_wide(const std::string& s) { return fs::path(s).wstring(); }

bool usable_file(const fs::path& p) {
    std::error_code ec;
    return fs::exists(p, ec) && !fs::is_directory(p, ec);
}

bool ends_with_exe(const std::string& s) {
    if (s.size() < 4) return false;
    std::string tail = s.substr(s.size() - 4);
    std::transform(tail.begin(), tail.end(), tail.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return tail == ".exe";
}

/// `name` as an executable path: itself when it names a location (contains a
/// separator or a drive colon), else the first match on PATH; `.exe` is tried
/// first for a name without it. A directory is never taken. "" when none.
std::string find_program(const std::string& name) {
    const auto try_dir = [&](const std::string& dir) -> std::string {
        const fs::path d(dir);
        if (!ends_with_exe(name)) {
            const fs::path exe = d / (name + ".exe");
            if (usable_file(exe)) return exe.string();
        }
        const fs::path plain = d / name;
        return usable_file(plain) ? plain.string() : std::string();
    };
    if (name.find_first_of("\\/:") != std::string::npos) {
        if (!ends_with_exe(name) && usable_file(name + ".exe")) return name + ".exe";
        return usable_file(name) ? name : std::string();
    }
    const char* env = std::getenv("PATH");
    if (!env || !*env) return {};
    const std::string path = env;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = path.find(';', start);
        std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        dir.erase(std::remove(dir.begin(), dir.end(), '"'), dir.end());
        if (!dir.empty()) {
            const std::string found = try_dir(dir);
            if (!found.empty()) return found;
        }
        if (end == std::string::npos) return {};
        start = end + 1;
    }
}

std::string win_error(DWORD code) { return "error " + std::to_string(code); }

/// The child's environment block: jnext's own without any LANG or LC_ALL entry
/// (Windows names are case-insensitive, so `Lang=` goes too), then LANG=C and
/// LC_ALL=C, sorted by name the way Windows documents an environment block.
std::vector<wchar_t> child_environment_block() {
    std::vector<std::wstring> vars;
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
            const std::wstring var(p);
            // A leading '=' belongs to the name ("=C:=C:\dir" is a per-drive cwd).
            const std::size_t eq = var.find(L'=', var.empty() ? 0 : 1);
            const std::wstring name = var.substr(0, eq);
            if (CompareStringOrdinal(name.c_str(), -1, L"LANG", -1, TRUE) == CSTR_EQUAL ||
                CompareStringOrdinal(name.c_str(), -1, L"LC_ALL", -1, TRUE) == CSTR_EQUAL)
                continue;
            vars.push_back(var);
        }
        FreeEnvironmentStringsW(block);
    }
    vars.push_back(L"LANG=C");
    vars.push_back(L"LC_ALL=C");
    const auto name_of = [](const std::wstring& v) {
        return v.substr(0, v.find(L'=', v.empty() ? 0 : 1));
    };
    std::sort(vars.begin(), vars.end(), [&](const std::wstring& a, const std::wstring& b) {
        return CompareStringOrdinal(name_of(a).c_str(), -1, name_of(b).c_str(), -1, TRUE) == CSTR_LESS_THAN;
    });
    std::vector<wchar_t> out;
    for (const std::wstring& v : vars) {
        out.insert(out.end(), v.begin(), v.end());
        out.push_back(L'\0');
    }
    out.push_back(L'\0');
    return out;
}

struct Child {
    HANDLE process = nullptr;
    DWORD  pid     = 0;
};

/// Start `exe` with `args`: stdout and stderr appended to `log_path`, stdin from
/// a pipe whose write end is returned in `stdin_write` (or from NUL when that is
/// null), and NO other handle of jnext's — an explicit
/// PROC_THREAD_ATTRIBUTE_HANDLE_LIST, the counterpart of POSIX's
/// addclosefrom_np. With `job`, the child is created suspended, put in the job
/// and only then resumed, so it is never running outside it. False with `error`.
bool spawn_child(const std::string& exe, const std::vector<std::string>& args,
                 const std::string& log_path, HANDLE job, HANDLE* stdin_write, Child& child,
                 std::string& error) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength        = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE log = CreateFileW(to_wide(log_path).c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &sa, OPEN_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        error = log_path + ": cannot open the log (" + win_error(GetLastError()) + ")";
        return false;
    }
    HANDLE in_read = nullptr, in_write = nullptr;
    if (stdin_write) {
        if (!CreatePipe(&in_read, &in_write, &sa, 0)) {
            error = "CreatePipe: " + win_error(GetLastError());
            CloseHandle(log);
            return false;
        }
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);   // only the read end is the child's
    } else {
        in_read = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (in_read == INVALID_HANDLE_VALUE) {
            error = "cannot open NUL: " + win_error(GetLastError());
            CloseHandle(log);
            return false;
        }
    }
    const auto close_ours = [&] {
        CloseHandle(log);
        CloseHandle(in_read);
        if (in_write) CloseHandle(in_write);
    };

    HANDLE inherit_list[2] = {in_read, log};
    SIZE_T attr_size = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attr_size);
    std::vector<char> attr_storage(attr_size);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attr_storage.data());
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attr_size) ||
        !UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherit_list,
                                   sizeof(inherit_list), nullptr, nullptr)) {
        error = "handle list: " + win_error(GetLastError());
        close_ours();
        return false;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb         = sizeof(si);
    si.StartupInfo.dwFlags    = STARTF_USESTDHANDLES;
    si.StartupInfo.hStdInput  = in_read;
    si.StartupInfo.hStdOutput = log;
    si.StartupInfo.hStdError  = log;
    si.lpAttributeList        = attrs;

    std::string cmd = VideoRecorder::quote_arg(exe, VideoRecorder::CmdStyle::Windows);
    for (const std::string& a : args) cmd += " " + VideoRecorder::quote_arg(a, VideoRecorder::CmdStyle::Windows);
    std::wstring wcmd = to_wide(cmd);
    wcmd.push_back(L'\0');
    std::vector<wchar_t> env = child_environment_block();

    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(to_wide(exe).c_str(), wcmd.data(), nullptr, nullptr, TRUE,
                                   CREATE_SUSPENDED | CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT |
                                       CREATE_UNICODE_ENVIRONMENT,
                                   env.data(), nullptr, &si.StartupInfo, &pi);
    const DWORD create_error = GetLastError();
    DeleteProcThreadAttributeList(attrs);
    // The child has its own copies; ours are closed either way.
    CloseHandle(log);
    CloseHandle(in_read);
    if (!ok) {
        if (in_write) CloseHandle(in_write);
        error = exe + ": " + win_error(create_error);
        return false;
    }
    if (job && !AssignProcessToJobObject(job, pi.hProcess)) {
        // On Windows 7 a jnext already inside a job (no nested jobs there)
        // cannot do this. Without it a killed jnext would orphan QEMU, so
        // refuse rather than run without the guarantee.
        error = "cannot put QEMU in a job object (" + win_error(GetLastError()) +
                "), so a killed jnext could leave it running; refusing to start it";
        TerminateProcess(pi.hProcess, 1);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        if (in_write) CloseHandle(in_write);
        return false;
    }
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    child.process = pi.hProcess;
    child.pid     = pi.dwProcessId;
    if (stdin_write) *stdin_write = in_write;
    return true;
}

} // namespace

PiQemu::~PiQemu() { stop(); }

std::string PiQemu::mark_close_on_exec_except(int, const std::vector<std::string>&) { return {}; }

bool PiQemu::open_descriptors(const std::string&, std::vector<int>&) { return false; }

bool PiQemu::start(const Spec& spec, std::string& error) {
    if (running()) {
        error = "already running";
        return false;
    }
    if (!check_dir(spec.dir, error)) return false;
    const std::string qemu = find_program(spec.qemu_binary);
    if (qemu.empty()) {
        error = spec.qemu_binary + kInstallHint;
        return false;
    }
    stop_grace_ms_ = spec.stop_grace_ms;
    poll_sleep_    = spec.poll_sleep;

    const fs::path dir(spec.dir);
    const std::string log_path = (dir / "qemu.log").string();
    std::error_code ec;
    fs::remove(log_path, ec);   // a fresh log per run, so it describes this run only

    const fs::path overlay = dir / "overlay.qcow2";
    bool created_overlay = false;
    auto fail = [&](const std::string& why) {
        error = why;
        stopping_ = true;
        stop();
        if (created_overlay) fs::remove(overlay, ec);
        return false;
    };

    // The overlay: all of NextPi's writes go here, never to the release image.
    // The backing path is relative to the overlay, so the directory can move.
    if (!fs::exists(overlay, ec)) {
        std::string qemu_img = (fs::path(qemu).parent_path() / "qemu-img.exe").string();
        if (!usable_file(qemu_img)) qemu_img = find_program("qemu-img");
        if (qemu_img.empty()) return fail("qemu-img" + kInstallHint);
        Child img;
        std::string spawn_error;
        if (!spawn_child(qemu_img,
                         {"create", "-q", "-f", "qcow2", "-b", "nextpi.img", "-F", "raw",
                          overlay.string(), "16G"},
                         log_path, nullptr, nullptr, img, spawn_error))
            return fail(spawn_error);
        const DWORD waited = WaitForSingleObject(img.process, 60000);
        DWORD code = static_cast<DWORD>(-1);
        if (waited == WAIT_OBJECT_0) GetExitCodeProcess(img.process, &code);
        else TerminateProcess(img.process, 1);
        CloseHandle(img.process);
        created_overlay = fs::exists(overlay, ec);
        if (code != 0)
            return fail(qemu_img + " could not create " + overlay.string() + " (" +
                        (waited == WAIT_OBJECT_0 ? "exit code " + std::to_string(code)
                                                 : std::string("timed out")) +
                        "); see " + log_path);
    }

    std::string port_error;
    tcp_port_ = free_loopback_port(port_error);
    if (tcp_port_ == 0) return fail(port_error);
    rx_path_.clear();
    tx_path_.clear();

    // The job: QEMU is killed when the last handle to it closes, which the
    // kernel does when jnext exits however it exits.
    job_ = CreateJobObjectW(nullptr, nullptr);
    if (!job_) return fail("CreateJobObject: " + win_error(GetLastError()));
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
        return fail("SetInformationJobObject: " + win_error(GetLastError()));

    // The Pi's sound reaches the mixer over a named pipe jnext serves. The pipe
    // must exist BEFORE QEMU is spawned: its wav back-end fopen()s the path at
    // startup and fails with ENOENT when there is no server.
    std::string audio_base;
    if (uses_mixer(spec)) {
        static std::atomic<unsigned> pipe_counter{0};
        audio_base = audio_pipe_base(GetCurrentProcessId(), pipe_counter++);
        audio_ = std::make_unique<PiAudio>();
        std::string audio_error;
        if (!audio_->open(audio_base + ".audio", audio_error)) return fail(audio_error);
    }

    Child child;
    std::string spawn_error;
    HANDLE in_write = nullptr;
    if (!spawn_child(qemu, build_args(spec, tcp_port_, audio_base), log_path, job_, &in_write, child, spawn_error))
        return fail(spawn_error);
    process_     = child.process;
    stdin_write_ = in_write;
    pid_         = static_cast<int>(child.pid);

    exited_      = false;
    stopping_    = false;
    exit_status_ = -1;
    reaper_ = std::thread([this, process = process_, pid = pid_, log_path] {
        WaitForSingleObject(process, INFINITE);
        DWORD code = 0;
        GetExitCodeProcess(process, &code);
        exit_status_ = static_cast<int>(code);
        exited_ = true;
        if (!stopping_) {
            Log::uart()->error(
                "NextPi: QEMU (pid {}) exited on its own (exit status {}); the Pi on UART 1 is "
                "gone. See {}",
                pid, static_cast<int>(code), log_path);
        }
    });

    // A QEMU that rejects its arguments or the image dies at once; report that
    // here, as a startup failure, rather than as a Pi that never answers.
    // Starting a process on Windows (and under wine) is slower than a fork, and
    // a QEMU rejecting its arguments dies only once it is loaded, so the window
    // is a second rather than POSIX's 300 ms.
    wait_exited(1000, 50);
    if (exited_) return fail(spec.qemu_binary + " exited at once; see " + log_path);

    Log::uart()->info("NextPi: QEMU raspi0 started (pid {}) from {}; log {}", pid_, spec.dir,
                      log_path);
    return true;
}

void PiQemu::stop() {
    stopping_ = true;
    if (process_) {
        // `quit` on the monitor on stdin is the orderly stop, the same path
        // SIGTERM takes on POSIX: QEMU flushes the overlay and exits.
        if (stdin_write_) {
            DWORD written = 0;
            WriteFile(stdin_write_, "quit\n", 5, &written, nullptr);
            CloseHandle(stdin_write_);
            stdin_write_ = nullptr;
        }
        wait_exited(stop_grace_ms_, 10);
        // A QEMU that ignores it: end everything in the job.
        if (!exited_ && job_) TerminateJobObject(job_, 1);
        if (reaper_.joinable()) reaper_.join();
        CloseHandle(process_);
        process_ = nullptr;
        pid_     = -1;
    }
    if (stdin_write_) {
        CloseHandle(stdin_write_);
        stdin_write_ = nullptr;
    }
    if (audio_) {   // QEMU is gone, so no writer is left on the pipe
        audio_->close();
        audio_.reset();
    }
    if (job_) {     // last: closing it kills anything still in it
        CloseHandle(job_);
        job_ = nullptr;
    }
}

#endif  // _WIN32
