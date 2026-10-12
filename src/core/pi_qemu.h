#pragma once

#include "audio/pi_audio.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

struct EmulatorConfig;

/// NextPi under QEMU, launched and owned by jnext (`--nextpi`).
/// Design: doc/design/NEXTPI-DESIGN.md §3.5.
///
/// The Raspberry Pi on a real Next's GPIO header runs NextPi; QEMU's `raspi0`
/// machine boots the same SD-card image. This class starts that QEMU as a child
/// process, with the Pi's console UART (`ttyAMA0`, where the NextPi Supervisor
/// listens) on a QEMU `pipe` chardev — a pair of FIFOs jnext creates and then
/// opens as the far end of UART 1 (`EmulatorConfig::pi_uart_fifo_rx/tx`). So
/// the Next talks to a real NextPi with nothing to start or wire by hand.
///
/// THE DIRECTORY is the one `nextpi::provision` prepares (core/
/// nextpi_provisioner.h): `nextpi.img` (the release's SD-card image, never
/// written), `boot/kernel.img` and `boot/bcm2708-rpi-zero.dtb` (copied out of
/// its FAT partition). This class adds
/// `overlay.qcow2` — a 16 GB copy-on-write overlay, because QEMU's raspi SD
/// card must be a power-of-two size and the image is not — and `qemu.log`.
///
/// OWNED BY THE PROCESS, NOT BY `Emulator`. A hard reset reconstructs the
/// Emulator (`emulator_boot.h`); QEMU must not reboot with it, since NextPi
/// takes about a minute to reach its prompt and a real Pi does not see a Next
/// reset either. The FIFOs persist and QEMU holds both ends open, so the
/// rebuilt Emulator re-opens the same paths and the conversation continues.
///
/// LIFETIME. `start()` spawns QEMU under a small /bin/sh WATCHDOG that holds
/// the read end of a pipe whose write end only jnext has. When jnext goes away
/// for any reason — SIGKILL included — the kernel closes that end and the
/// watchdog sends QEMU SIGTERM, so QEMU is never orphaned holding the overlay's
/// lock. `stop()` (the destructor) closes the pipe the same way, waits up to
/// three seconds, then SIGKILLs the process group QEMU and its watchdog run in,
/// and removes the FIFOs. A reaper thread logs a QEMU that exits on its own, so
/// a Pi that has gone away says so instead of going quiet.
///
/// CHILD ENVIRONMENT. Inherited — QEMU's audio back-ends need the session's
/// (PULSE_SERVER, XDG_RUNTIME_DIR, ...) — with `LANG=C` and `LC_ALL=C` set in
/// the child only, per the project rule for spawned processes. Nothing parses
/// QEMU's output: its stdout and stderr go to `DIR/qemu.log`, and only the exit
/// status is consulted. No other descriptor of jnext's reaches QEMU (the SD
/// image, the FIFOs, sockets): `POSIX_SPAWN_CLOEXEC_DEFAULT` on macOS,
/// `posix_spawn_file_actions_addclosefrom_np` on glibc 2.34+, and elsewhere
/// `mark_close_on_exec_except` over the descriptors actually open.
///
/// TWO TRANSPORTS (GH #316). POSIX uses the FIFO pair above. Windows has no
/// FIFOs, and QEMU's Windows `pipe` chardev is a named-pipe SERVER that blocks
/// QEMU's startup until a client connects, takes one client and never listens
/// again, so a hard reset (a rebuilt Emulator) would lose the Pi for good. Its
/// `socket` chardev with `server=on,wait=off` listens at once and listens again
/// after a disconnect, so on Windows QEMU listens on a loopback TCP port jnext
/// picks, and the Emulator (`JoyUartEndpoint::open_tcp_client`) is the client.
/// The socket transport can be selected on POSIX too (`Spec::transport`); the
/// default there stays the FIFO pair, which exposes nothing to the network.
///
/// WINDOWS LAUNCH is in pi_qemu_win.cpp (the only TU that includes
/// <windows.h>): `CreateProcessW` with an explicit handle list, a job object
/// with KILL_ON_JOB_CLOSE in place of the watchdog (the kernel kills QEMU when
/// jnext goes, however it goes), and a graceful stop by `quit` on QEMU's stdin
/// (`-monitor stdio`) before the job is terminated. The environment is built
/// explicitly with `LANG=C` / `LC_ALL=C`, matched case-insensitively.
class PiQemu {
public:
    enum class Transport { Fifo, Socket };

    /// The transport `Spec` defaults to: the FIFO pair on POSIX, the loopback
    /// socket on Windows.
    static constexpr Transport default_transport() {
#ifdef _WIN32
        return Transport::Socket;
#else
        return Transport::Fifo;
#endif
    }

    struct Spec {
        std::string dir;                              ///< NextPi directory (layout above)
        /// PATH-searched unless it has a path separator (Windows: also tried
        /// with `.exe`).
        std::string qemu_binary = "qemu-system-arm";
        /// Pi audio: "" for the Next's mixer (the default — QEMU writes it to
        /// a FIFO that `audio()` reads, and the emulator mixes it over I2S;
        /// `mixer_audio_supported()`) — or, on Windows, where that path does
        /// not exist yet (GH #331), the host output; "host" for this
        /// platform's default host output (coreaudio on macOS, dsound on
        /// Windows, pa elsewhere); a QEMU -audiodev driver name to play it
        /// straight to the host ("none" mutes it); or "wav:FILE" to record it.
        std::string audio;
        Transport   transport = default_transport();
        /// How long `stop()` waits after asking QEMU to stop before it
        /// SIGKILLs it. A test seam; production uses the default.
        int stop_grace_ms = 3000;
        /// Test seam: replaces the launcher's poll sleeps (null: a real
        /// sleep). A row passes a sleep that overshoots, as a loaded or
        /// timer-coalescing host does, to prove the grace and the startup
        /// window are measured against the clock, not counted in sleeps.
        void (*poll_sleep)(int ms) = nullptr;
    };

    PiQemu() = default;
    ~PiQemu();

    PiQemu(const PiQemu&)            = delete;
    PiQemu& operator=(const PiQemu&) = delete;

    /// Check the directory, create the overlay if it is missing (`qemu-img`,
    /// found next to the QEMU binary or on PATH), create the FIFOs and spawn
    /// QEMU. False with `error` set on any failure, including QEMU not being
    /// installed or exiting at once. Nothing is left running then, and an
    /// overlay this call created is removed; `qemu.log` is kept, because the
    /// error points at it.
    bool start(const Spec& spec, std::string& error);

    bool running() const { return pid_ > 0; }
    int  pid() const { return pid_; }

    /// The wait status the watchdog exited with — which is QEMU's own exit
    /// status, passed on — or -1 while it runs (and before any start). On
    /// Windows it is the process's exit code.
    int exit_status() const { return exit_status_; }

    /// The loopback port QEMU's socket chardev listens on; 0 for the FIFO
    /// transport.
    std::uint16_t tcp_port() const { return tcp_port_; }

    /// Point `cfg` at this Pi: the FIFO paths for the FIFO transport, the TCP
    /// port for the socket one (the other fields cleared), and the audio
    /// reader when the sound goes to the mixer (else null). The one place the
    /// transport turns into Emulator configuration, so `main()` and the rows
    /// run the same code.
    void link_config(EmulatorConfig& cfg) const;

    /// A free loopback port, found by binding 127.0.0.1:0 and closing it again.
    /// 0 with `error` set on failure. Another process can take the port before
    /// QEMU binds it; QEMU then exits at once and `start()` says so.
    static std::uint16_t free_loopback_port(std::string& error);

    /// The FIFO jnext READS (QEMU's `<pipe>.out`: Pi → Next) and the one it
    /// WRITES (QEMU's `<pipe>.in`: Next → Pi).
    const std::string& rx_path() const { return rx_path_; }
    const std::string& tx_path() const { return tx_path_; }

    /// The reader of the Pi's audio when it goes to the Next's mixer (`audio`
    /// empty), else nullptr. Lives as long as this object.
    PiAudio* audio() const { return audio_.get(); }

    /// The QEMU argument vector, argv[0] excluded. Pure; exposed for tests.
    /// `pipe_base` is the chardev path whose `.in` / `.out` are the FIFOs.
    static std::vector<std::string> build_args(const Spec& spec, const std::string& pipe_base);
    /// The same with the console UART on a loopback TCP chardev QEMU listens
    /// on: `socket,host=127.0.0.1,port=<port>,server=on,wait=off,nodelay=on`.
    /// `host=` is not optional (QEMU's default is 0.0.0.0) and neither is
    /// `wait=off` (a listening chardev otherwise blocks QEMU's startup until
    /// a client connects). The monitor is `stdio` on Windows (where `quit`
    /// on stdin is the graceful stop) and `none` elsewhere.
    /// `audio_base` is the path whose `.audio` FIFO carries the Pi's sound
    /// when it goes to the mixer (`uses_mixer`); unused otherwise.
    static std::vector<std::string> build_args(const Spec& spec, std::uint16_t port,
                                               const std::string& audio_base = {});

    /// Whether the Pi's sound can go to the Next's mixer on this platform: it
    /// travels over a FIFO, which Windows lacks (GH #331 will add a socket).
    static constexpr bool mixer_audio_supported() {
#ifdef _WIN32
        return false;
#else
        return true;
#endif
    }

    /// True when `spec.audio` sends the Pi's sound to the Next's mixer: empty,
    /// on a platform that has the path. Empty elsewhere means the host output.
    static bool uses_mixer(const Spec& spec);

    /// The `-audiodev` value for a driver name or "wav:FILE"; "host" is the
    /// platform default driver (coreaudio on macOS, dsound on Windows, pa
    /// elsewhere), and so is "" (what an empty setting means on Windows).
    static std::string audiodev_arg(const std::string& audio);

    /// The `-audiodev` value that sends the Pi's sound to the Next's mixer:
    /// QEMU's `wav` back-end into the FIFO `<pipe_base>.audio`, 44.1 kHz s16
    /// stereo (PiAudio's format).
    static std::string mixer_audiodev_arg(const std::string& pipe_base);

    /// True when `dir` has the files QEMU boots from; otherwise false with
    /// `error` naming the first one missing and how to make it.
    static bool check_dir(const std::string& dir, std::string& error);

    /// The environment a child gets: `env` (a null-terminated `environ`-style
    /// array) without any LANG or LC_ALL entry, then LANG=C and LC_ALL=C — so
    /// each appears exactly once, set in the child only. Pure.
    static std::vector<std::string> child_environment(const char* const* env);

    /// Mark every open descriptor from 3 up close-on-exec, except `keep`. The
    /// fallback where posix_spawn cannot close them itself: it walks the
    /// descriptors that are open (/proc/self/fd, else /dev/fd) rather than
    /// every number up to the descriptor limit, which with a large `ulimit -n`
    /// is ~10^9 calls. `lists` are tried in order; only when none can be read
    /// does it walk the numbers, up to `fd_walk_limit`. Returns the list it
    /// read, or "" when it walked the numbers. POSIX only; `lists` is a test
    /// seam (an empty one forces the walk), production uses the default.
    static std::string mark_close_on_exec_except(
        int keep, const std::vector<std::string>& lists = {"/proc/self/fd", "/dev/fd"});

    /// The descriptors the directory `list` names (/proc/self/fd or /dev/fd),
    /// read while it is open and WITHOUT the directory's own descriptor —
    /// which is closed by the time anyone acts on the list, and whose number
    /// may by then belong to something else. False when it cannot be read.
    /// POSIX only; exposed for tests.
    static bool open_descriptors(const std::string& list, std::vector<int>& fds);

    /// How far that number walk goes for a `sysconf(_SC_OPEN_MAX)` of
    /// `open_max`: the limit itself, capped at 65536 — and 65536 when the
    /// limit is indeterminate (-1), since walking to it would mark nothing.
    /// Pure.
    static long fd_walk_limit(long open_max);

private:
    void stop();

    /// `build_args` with the console chardev already rendered.
    static std::vector<std::string> build_args_with_chardev(const Spec& spec,
                                                            const std::string& chardev,
                                                            const std::string& audio_base);

    int               pid_ = -1;
    std::uint16_t     tcp_port_ = 0;
    // Windows only (pi_qemu_win.cpp): HANDLEs, spelled void* so no header here
    // needs <windows.h>.
    void*             process_    = nullptr;
    void*             job_        = nullptr;
    void*             stdin_write_ = nullptr;
    std::string       runtime_dir_;   ///< holds the FIFOs; removed on stop
    std::string       rx_path_;
    std::string       tx_path_;
    std::thread       reaper_;
    std::atomic<bool> exited_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<int>  exit_status_{-1};
    int               watchdog_fd_ = -1;   ///< write end of the watchdog pipe
    std::unique_ptr<PiAudio> audio_;       ///< the Pi's audio, when it goes to the mixer
    int               stop_grace_ms_ = 3000;
    void            (*poll_sleep_)(int ms) = nullptr;

    /// Wait up to `ms` of wall-clock time (steady_clock) for QEMU to exit,
    /// polling every `step_ms`. Returns exited_. The bound is elapsed time,
    /// never a count of sleeps: a host that stretches short sleeps (macOS
    /// timer coalescing, GH #355) must not stretch the bound with them.
    bool wait_exited(int ms, int step_ms);
};
