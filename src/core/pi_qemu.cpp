#include "core/pi_qemu.h"

#include "core/log.h"

#include <chrono>
#include <filesystem>
#include <system_error>

#ifndef _WIN32
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
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

#ifdef _WIN32

PiQemu::~PiQemu() = default;

bool PiQemu::start(const Spec&, std::string& error) {
    error = "launching NextPi under QEMU is not supported on Windows";
    return false;
}

void PiQemu::stop() {}

#else   // POSIX

namespace {

/// The parent's environment with LANG and LC_ALL forced to C — set in the
/// child's copy only, never in jnext's own.
std::vector<std::string> child_environment() {
    std::vector<std::string> env;
    for (char** e = environ; e && *e; ++e) {
        const std::string var(*e);
        if (var.rfind("LANG=", 0) == 0 || var.rfind("LC_ALL=", 0) == 0) continue;
        env.push_back(var);
    }
    env.push_back("LANG=C");
    env.push_back("LC_ALL=C");
    return env;
}

std::vector<char*> c_strings(std::vector<std::string>& v) {
    std::vector<char*> out;
    for (std::string& s : v) out.push_back(s.data());
    out.push_back(nullptr);
    return out;
}

/// Spawn `program` (PATH-searched) with `args`, stdin from /dev/null and
/// stdout + stderr appended to `log_path`. Returns the pid, or -1 with `error`.
int spawn(const std::string& program, const std::vector<std::string>& args,
          const std::string& log_path, std::string& error) {
    std::vector<std::string> argv_s{program};
    argv_s.insert(argv_s.end(), args.begin(), args.end());
    std::vector<std::string> env_s = child_environment();
    std::vector<char*> argv = c_strings(argv_s);
    std::vector<char*> envp = c_strings(env_s);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, log_path.c_str(),
                                     O_WRONLY | O_CREAT | O_APPEND, 0644);
    posix_spawn_file_actions_adddup2(&actions, 1, 2);

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
#ifdef POSIX_SPAWN_CLOEXEC_DEFAULT
    // macOS: hand QEMU only the three descriptors above, not the SD image or
    // anything else jnext has open.
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_CLOEXEC_DEFAULT);
#endif

    pid_t pid = -1;
    const int rc = ::posix_spawnp(&pid, program.c_str(), &actions, &attr, argv.data(), envp.data());
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) {
        error = (rc == ENOENT)
                    ? program + " not found — install QEMU (macOS: brew install qemu; "
                                "Fedora: dnf install qemu-system-arm; Debian/Ubuntu: apt "
                                "install qemu-system-arm)"
                    : program + ": " + std::strerror(rc);
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

PiQemu::~PiQemu() { stop(); }

bool PiQemu::start(const Spec& spec, std::string& error) {
    if (running()) {
        error = "already running";
        return false;
    }
    if (!check_dir(spec.dir, error)) return false;

    const fs::path dir(spec.dir);
    const std::string log_path = (dir / "qemu.log").string();
    {
        // A fresh log per run, so it describes this run only.
        std::error_code ec;
        fs::remove(log_path, ec);
    }

    // The overlay: all of NextPi's writes go here, never to the release image.
    // The backing path is relative to the overlay, so the directory can move.
    const fs::path overlay = dir / "overlay.qcow2";
    std::error_code ec;
    if (!fs::exists(overlay, ec)) {
        std::string qemu_img = "qemu-img";
        if (spec.qemu_binary.find('/') != std::string::npos) {
            const fs::path sibling = fs::path(spec.qemu_binary).parent_path() / "qemu-img";
            if (fs::exists(sibling, ec)) qemu_img = sibling.string();
        }
        const int img_pid = spawn(qemu_img,
                                  {"create", "-q", "-f", "qcow2", "-b", "nextpi.img", "-F", "raw",
                                   overlay.string(), "16G"},
                                  log_path, error);
        if (img_pid < 0) return false;
        int status = 0;
        while (::waitpid(img_pid, &status, 0) < 0 && errno == EINTR) {}
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            error = qemu_img + " could not create " + overlay.string() + " (" +
                    describe_status(status) + "); see " + log_path;
            return false;
        }
    }

    // The FIFOs live in a private directory of their own: QEMU's `pipe` chardev
    // opens `<base>.in` (it reads: Next → Pi) and `<base>.out` (it writes).
    std::string tmpl = (fs::temp_directory_path() / "jnext-pi-XXXXXX").string();
    if (::mkdtemp(tmpl.data()) == nullptr) {
        error = std::string("mkdtemp: ") + std::strerror(errno);
        return false;
    }
    runtime_dir_ = tmpl;
    const std::string base = (fs::path(runtime_dir_) / "uart").string();
    tx_path_ = base + ".in";
    rx_path_ = base + ".out";
    if (::mkfifo(tx_path_.c_str(), 0600) != 0 || ::mkfifo(rx_path_.c_str(), 0600) != 0) {
        error = std::string("mkfifo: ") + std::strerror(errno);
        stop();
        return false;
    }

    pid_ = spawn(spec.qemu_binary, build_args(spec, base), log_path, error);
    if (pid_ < 0) {
        stop();
        return false;
    }

    exited_   = false;
    stopping_ = false;
    reaper_ = std::thread([this, pid = pid_, log_path] {
        int status = 0;
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
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
    if (exited_) {
        error = spec.qemu_binary + " exited at once; see " + log_path;
        stopping_ = true;
        stop();
        return false;
    }

    Log::uart()->info("NextPi: QEMU raspi0 started (pid {}) from {}; log {}", pid_, spec.dir,
                      log_path);
    return true;
}

void PiQemu::stop() {
    stopping_ = true;
    if (pid_ > 0) {
        if (!exited_) {
            ::kill(pid_, SIGTERM);
            for (int i = 0; i < 60 && !exited_; ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            if (!exited_) ::kill(pid_, SIGKILL);
        }
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
