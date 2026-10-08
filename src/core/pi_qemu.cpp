#include "core/pi_qemu.h"

#include "core/log.h"

#include <chrono>
#include <filesystem>
#include <system_error>

#ifndef _WIN32
#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

namespace fs = std::filesystem;

namespace {

/// The kernel command line run-nextpi has always used. `dwc_otg.*` turns off
/// the Pi USB driver's FIQ path, which hangs device enumeration under QEMU;
/// `snd_usb_audio.index=0` makes QEMU's USB sound card ALSA's default, which on
/// a real Pi is the I2S DAC — so NextPi's `nextpi-play_*` work unchanged.
constexpr const char* KERNEL_APPEND =
    "console=ttyAMA0,115200 earlyprintk root=/dev/mmcblk0p2 rootfstype=ext4 rootwait "
    "fsck.repair=yes dwc_otg.fiq_enable=0 dwc_otg.fiq_fsm_enable=0 dwc_otg.nak_holdoff=0 "
    "snd_usb_audio.index=0";

/// QEMU option values are comma-separated, and a literal comma is written
/// twice. A path is the only free text that lands inside one.
std::string qemu_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        out += c;
        if (c == ',') out += ',';
    }
    return out;
}

} // namespace

std::string PiQemu::audiodev_arg(const std::string& audio) {
    if (audio.rfind("wav:", 0) == 0)
        return "wav,id=snd0,path=" + qemu_escape(audio.substr(4));
    std::string driver = audio;
    if (driver.empty()) {
#ifdef __APPLE__
        driver = "coreaudio";
#else
        driver = "pa";
#endif
    }
    // The emulated USB card delivers audio unevenly, and CoreAudio clicks
    // whenever its buffer runs dry; large host buffers absorb it.
    if (driver == "coreaudio")
        return "coreaudio,id=snd0,out.buffer-count=16,out.buffer-length=90000";
    return driver + ",id=snd0";
}

std::vector<std::string> PiQemu::build_args(const Spec& spec, const std::string& pipe_base) {
    const fs::path dir(spec.dir);
    return {
        "-M", "raspi0",
        "-kernel", (dir / "boot" / "kernel.img").string(),
        "-dtb", (dir / "boot" / "bcm2708-rpi-zero.dtb").string(),
        "-drive", "file=" + qemu_escape((dir / "overlay.qcow2").string()) + ",if=sd,format=qcow2",
        "-append", KERNEL_APPEND,
        "-audiodev", audiodev_arg(spec.audio),
        "-device", "usb-audio,audiodev=snd0,buffer=16384",
        "-chardev", "pipe,id=pi,path=" + qemu_escape(pipe_base),
        "-serial", "chardev:pi",
        "-monitor", "none",
        "-display", "none",
    };
}

bool PiQemu::check_dir(const std::string& dir, std::string& error) {
    const char* needed[] = {"nextpi.img", "boot/kernel.img", "boot/bcm2708-rpi-zero.dtb"};
    for (const char* rel : needed) {
        std::error_code ec;
        if (!fs::exists(fs::path(dir) / rel, ec)) {
            error = (fs::path(dir) / rel).string() + " missing — the NextPi directory is "
                    "incomplete; remove it and jnext downloads NextPi again";
            return false;
        }
    }
    return true;
}

std::vector<std::string> PiQemu::child_environment(const char* const* env) {
    std::vector<std::string> out;
    for (const char* const* e = env; e && *e; ++e) {
        const std::string var(*e);
        if (var.rfind("LANG=", 0) == 0 || var.rfind("LC_ALL=", 0) == 0) continue;
        out.push_back(var);
    }
    out.push_back("LANG=C");
    out.push_back("LC_ALL=C");
    return out;
}

#ifdef _WIN32

PiQemu::~PiQemu() = default;

void PiQemu::mark_close_on_exec_except(int) {}

bool PiQemu::start(const Spec&, std::string& error) {
    error = "launching NextPi under QEMU is not supported on Windows";
    return false;
}

void PiQemu::stop() {}

#else   // POSIX

namespace {

std::vector<char*> c_strings(std::vector<std::string>& v) {
    std::vector<char*> out;
    for (std::string& s : v) out.push_back(s.data());
    out.push_back(nullptr);
    return out;
}

const std::string kInstallHint =
    " not found — install QEMU (macOS: brew install qemu; Fedora: dnf install "
    "qemu-system-arm qemu-img; Debian/Ubuntu: apt install qemu-system-arm qemu-utils)";

/// `name` as an executable path: itself when it contains a '/', else the
/// first match on $PATH. "" when there is none. Resolved here rather than by
/// posix_spawnp because QEMU is started through /bin/sh (the watchdog below),
/// where a missing program would only show as an exit status.
std::string find_program(const std::string& name) {
    std::error_code ec;
    if (name.find('/') != std::string::npos)
        return (::access(name.c_str(), X_OK) == 0 && !fs::is_directory(name, ec)) ? name : "";
    const char* env = std::getenv("PATH");
    const std::string path = (env && *env) ? env : "/usr/bin:/bin";
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = path.find(':', start);
        std::string dir = path.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (dir.empty()) dir = ".";
        const std::string candidate = dir + "/" + name;
        if (::access(candidate.c_str(), X_OK) == 0 && !fs::is_directory(candidate, ec)) return candidate;
        if (end == std::string::npos) return "";
        start = end + 1;
    }
}

/// THE WATCHDOG. QEMU runs under this /bin/sh script, whose fd 3 is the read
/// end of a pipe only jnext holds the write end of. When jnext goes away —
/// however it goes, SIGKILL included — the kernel closes that end, `read` sees
/// EOF, and QEMU is sent SIGTERM; without this a killed jnext would leave QEMU
/// running and holding the overlay's lock, and the next start would fail.
/// QEMU itself gets fd 3 closed. The script waits for QEMU and exits with its
/// status, so the reaper still learns when QEMU dies on its own.
constexpr const char* kWatchdog =
    "\"$@\" 3<&- &\n"
    "q=$!\n"
    "( read _ <&3; kill -TERM \"$q\" 2>/dev/null ) &\n"
    "w=$!\n"
    "exec 3<&-\n"
    "wait \"$q\"; s=$?\n"
    "kill \"$w\" 2>/dev/null\n"
    "exit \"$s\"\n";

/// Spawn the executable at `path` with `args`: stdin from /dev/null, stdout +
/// stderr appended to `log_path`, `watchdog_fd` (when >= 0) as its fd 3, and
/// NOTHING ELSE of jnext's — not the SD image, not the FIFOs, not a socket.
/// `own_group` puts it in a process group of its own, so a SIGKILL can reach
/// everything it started. Returns the pid, or -1 with `error`.
int spawn(const std::string& path, const std::vector<std::string>& args, const std::string& log_path,
          int watchdog_fd, bool own_group, std::string& error) {
    std::vector<std::string> argv_s{path};
    argv_s.insert(argv_s.end(), args.begin(), args.end());
    std::vector<std::string> env_s = PiQemu::child_environment(environ);
    std::vector<char*> argv = c_strings(argv_s);
    std::vector<char*> envp = c_strings(env_s);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, log_path.c_str(),
                                     O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);
    if (watchdog_fd >= 0) posix_spawn_file_actions_adddup2(&actions, watchdog_fd, 3);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    short flags = 0;
    if (own_group) {
        flags |= POSIX_SPAWN_SETPGROUP;
        posix_spawnattr_setpgroup(&attr, 0);
    }
#if defined(POSIX_SPAWN_CLOEXEC_DEFAULT)
    // macOS: only the descriptors the file actions above set up survive.
    flags |= POSIX_SPAWN_CLOEXEC_DEFAULT;
#elif defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 34))
    // glibc 2.34+: close everything past the ones set up above.
    posix_spawn_file_actions_addclosefrom_np(&actions, watchdog_fd >= 0 ? 4 : 3);
#else
    // Elsewhere: mark every other descriptor close-on-exec in the parent. That
    // only affects exec, which jnext does nowhere else that needs them.
    PiQemu::mark_close_on_exec_except(watchdog_fd);
#endif
    posix_spawnattr_setflags(&attr, flags);

    pid_t pid = -1;
    const int rc = ::posix_spawn(&pid, path.c_str(), &actions, &attr, argv.data(), envp.data());
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        error = path + ": " + std::strerror(rc);
        return -1;
    }
    return pid;
}

std::string describe_status(int status) {
    if (WIFEXITED(status)) return "exit status " + std::to_string(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return "signal " + std::to_string(WTERMSIG(status));
    return "status " + std::to_string(status);
}

} // namespace

void PiQemu::mark_close_on_exec_except(int keep) {
    auto mark = [keep](int fd) {
        if (fd < 3 || fd == keep) return;
        const int f = ::fcntl(fd, F_GETFD);
        if (f >= 0) ::fcntl(fd, F_SETFD, f | FD_CLOEXEC);
    };
    // The descriptors that are open, from the kernel's own list. The list is
    // read whole before marking, so the directory's own descriptor (which is
    // in it) is marked too, harmlessly, and closed right after.
    for (const char* list : {"/proc/self/fd", "/dev/fd"}) {
        DIR* d = ::opendir(list);
        if (!d) continue;
        std::vector<int> fds;
        while (const dirent* e = ::readdir(d)) {
            char* end = nullptr;
            const long fd = std::strtol(e->d_name, &end, 10);
            if (end != e->d_name && *end == '\0') fds.push_back(static_cast<int>(fd));
        }
        ::closedir(d);
        for (int fd : fds) mark(fd);
        return;
    }
    // Neither list exists: walk the numbers, but never past a bound a large
    // `ulimit -n` cannot turn into a billion system calls.
    const long limit = std::min<long>(::sysconf(_SC_OPEN_MAX), 65536);
    for (int fd = 3; fd < limit; ++fd) mark(fd);
}

PiQemu::~PiQemu() { stop(); }

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

    const fs::path dir(spec.dir);
    const std::string log_path = (dir / "qemu.log").string();
    std::error_code ec;
    fs::remove(log_path, ec);   // a fresh log per run, so it describes this run only

    // Anything this call creates and then fails after is undone, except the
    // log: the error message points the user at it.
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
        std::string qemu_img = (fs::path(qemu).parent_path() / "qemu-img").string();
        if (::access(qemu_img.c_str(), X_OK) != 0) qemu_img = find_program("qemu-img");
        if (qemu_img.empty()) return fail("qemu-img" + kInstallHint);
        std::string spawn_error;
        const int img_pid = spawn(qemu_img,
                                  {"create", "-q", "-f", "qcow2", "-b", "nextpi.img", "-F", "raw",
                                   overlay.string(), "16G"},
                                  log_path, -1, false, spawn_error);
        if (img_pid < 0) return fail(spawn_error);
        int status = 0;
        while (::waitpid(img_pid, &status, 0) < 0 && errno == EINTR) {}
        created_overlay = fs::exists(overlay, ec);
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
            return fail(qemu_img + " could not create " + overlay.string() + " (" +
                        describe_status(status) + "); see " + log_path);
    }

    // The FIFOs live in a private directory of their own: QEMU's `pipe` chardev
    // opens `<base>.in` (it reads: Next → Pi) and `<base>.out` (it writes).
    // The error_code overload: with $TMPDIR naming no directory the other
    // one throws, and nothing above main() would catch it.
    const fs::path tmp = fs::temp_directory_path(ec);
    if (ec) return fail("no temporary directory for the NextPi FIFOs: " + ec.message() +
                        " (check $TMPDIR)");
    std::string tmpl = (tmp / "jnext-pi-XXXXXX").string();
    if (::mkdtemp(tmpl.data()) == nullptr) return fail(std::string("mkdtemp: ") + std::strerror(errno));
    runtime_dir_ = tmpl;
    const std::string base = (fs::path(runtime_dir_) / "uart").string();
    tx_path_ = base + ".in";
    rx_path_ = base + ".out";
    if (::mkfifo(tx_path_.c_str(), 0600) != 0 || ::mkfifo(rx_path_.c_str(), 0600) != 0)
        return fail(std::string("mkfifo: ") + std::strerror(errno));

    // The watchdog pipe. Both ends close-on-exec in jnext, so no other child
    // (ffmpeg, a second QEMU) can hold the write end open and defeat it; the
    // read end is moved above fd 3 so the dup2 onto 3 always clears that flag.
    // pipe2 sets the flag atomically, so a child spawned by another thread in
    // between cannot inherit an end; macOS has no pipe2, and the window that
    // leaves is the two fcntl calls below.
    int fds[2];
#if defined(__linux__) || defined(__FreeBSD__)
    if (::pipe2(fds, O_CLOEXEC) != 0) return fail(std::string("pipe2: ") + std::strerror(errno));
#else
    if (::pipe(fds) != 0) return fail(std::string("pipe: ") + std::strerror(errno));
    ::fcntl(fds[1], F_SETFD, FD_CLOEXEC);
#endif
    const int rd = ::fcntl(fds[0], F_DUPFD_CLOEXEC, 10);
    ::close(fds[0]);
    watchdog_fd_ = fds[1];
    if (rd < 0) return fail(std::string("fcntl: ") + std::strerror(errno));

    std::vector<std::string> sh_args{"-c", kWatchdog, "jnext-nextpi", qemu};
    const std::vector<std::string> qemu_args = build_args(spec, base);
    sh_args.insert(sh_args.end(), qemu_args.begin(), qemu_args.end());
    std::string spawn_error;
    pid_ = spawn("/bin/sh", sh_args, log_path, rd, true, spawn_error);
    ::close(rd);
    if (pid_ < 0) return fail(spawn_error);

    exited_      = false;
    stopping_    = false;
    exit_status_ = -1;
    reaper_ = std::thread([this, pid = pid_, log_path] {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
        exit_status_ = status;
        exited_ = true;
        if (!stopping_) {
            Log::uart()->error(
                "NextPi: QEMU (pid {}) exited on its own ({}); the Pi on UART 1 is gone. "
                "See {}",
                pid, describe_status(status), log_path);
        }
    });

    // A QEMU that rejects its arguments or the image dies at once; report that
    // here, as a startup failure, rather than as a Pi that never answers.
    for (int i = 0; i < 6 && !exited_; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    if (exited_) return fail(spec.qemu_binary + " exited at once; see " + log_path);

    Log::uart()->info("NextPi: QEMU raspi0 started (pid {}) from {}; log {}", pid_, spec.dir,
                      log_path);
    return true;
}

void PiQemu::stop() {
    stopping_ = true;
    // Closing the watchdog pipe IS the stop request: the watchdog sends QEMU
    // SIGTERM, exactly as when jnext dies.
    if (watchdog_fd_ >= 0) {
        ::close(watchdog_fd_);
        watchdog_fd_ = -1;
    }
    if (pid_ > 0) {
        for (int waited = 0; waited < stop_grace_ms_ && !exited_; waited += 10)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        // A QEMU that ignores SIGTERM: kill its whole process group — QEMU, the
        // watchdog shell and its subshell together.
        if (!exited_) ::kill(-pid_, SIGKILL);
        if (reaper_.joinable()) reaper_.join();
        pid_ = -1;
    }
    if (!runtime_dir_.empty()) {
        std::error_code ec;
        fs::remove_all(runtime_dir_, ec);
        runtime_dir_.clear();
    }
}

#endif  // _WIN32
