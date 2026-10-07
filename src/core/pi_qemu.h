#pragma once
#include <atomic>
#include <string>
#include <thread>
#include <vector>

/// NextPi under QEMU, launched and owned by jnext — the Pi Zero (`--pizero`).
/// Design: doc/design/PIZERO-DESIGN.md §3.5.
///
/// The Pi Zero on a real Next's GPIO header runs NextPi; QEMU's `raspi0`
/// machine boots the same SD-card image. This class starts that QEMU as a child
/// process, with the Pi's console UART (`ttyAMA0`, where the NextPi Supervisor
/// listens) on a QEMU `pipe` chardev — a pair of FIFOs jnext creates and then
/// opens as the far end of UART 1 (`EmulatorConfig::pi_uart_fifo_rx/tx`). So
/// the Next talks to a real NextPi with nothing to start or wire by hand.
///
/// THE DIRECTORY is the one `pizero::provision` prepares (core/
/// pizero_provisioner.h): `nextpi.img` (the release's SD-card image, never
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
/// LIFETIME. `start()` spawns QEMU; the destructor sends SIGTERM, waits up to
/// three seconds, then SIGKILL, and removes the FIFOs. A reaper thread waits on
/// the child and logs its exit if QEMU dies on its own, so a Pi that has gone
/// away says so instead of going quiet. QEMU stays in jnext's process group, so
/// a Ctrl-C at the terminal stops both.
///
/// CHILD ENVIRONMENT. Inherited — QEMU's audio back-ends need the session's
/// (PULSE_SERVER, XDG_RUNTIME_DIR, ...) — with `LANG=C` and `LC_ALL=C` set in
/// the child only, per the project rule for spawned processes. Nothing parses
/// QEMU's output: its stdout and stderr go to `DIR/qemu.log`, and only the exit
/// status is consulted.
///
/// POSIX ONLY, like the FIFO endpoint it feeds. `start()` refuses on Windows.
class PiQemu {
public:
    struct Spec {
        std::string dir;                              ///< NextPi directory (layout above)
        std::string qemu_binary = "qemu-system-arm";  ///< PATH-searched unless it has a '/'
        /// Pi audio: "" for the platform default (coreaudio on macOS, pa
        /// elsewhere), a QEMU -audiodev driver name ("none" mutes), or
        /// "wav:FILE" to record it.
        std::string audio;
    };

    PiQemu() = default;
    ~PiQemu();

    PiQemu(const PiQemu&)            = delete;
    PiQemu& operator=(const PiQemu&) = delete;

    /// Check the directory, create the overlay if it is missing (`qemu-img`,
    /// found next to the QEMU binary or on PATH), create the FIFOs and spawn
    /// QEMU. False with `error` set on any failure, including QEMU not being
    /// installed or exiting at once; nothing is left running or on disk then.
    bool start(const Spec& spec, std::string& error);

    bool running() const { return pid_ > 0; }
    int  pid() const { return pid_; }

    /// The FIFO jnext READS (QEMU's `<pipe>.out`: Pi → Next) and the one it
    /// WRITES (QEMU's `<pipe>.in`: Next → Pi).
    const std::string& rx_path() const { return rx_path_; }
    const std::string& tx_path() const { return tx_path_; }

    /// The QEMU argument vector, argv[0] excluded. Pure; exposed for tests.
    /// `pipe_base` is the chardev path whose `.in` / `.out` are the FIFOs.
    static std::vector<std::string> build_args(const Spec& spec, const std::string& pipe_base);

    /// The `-audiodev` value for `audio` as `Spec::audio` describes it.
    static std::string audiodev_arg(const std::string& audio);

    /// True when `dir` has the files QEMU boots from; otherwise false with
    /// `error` naming the first one missing and how to make it.
    static bool check_dir(const std::string& dir, std::string& error);

private:
    void stop();

    int               pid_ = -1;
    std::string       runtime_dir_;   ///< holds the FIFOs; removed on stop
    std::string       rx_path_;
    std::string       tx_path_;
    std::thread       reaper_;
    std::atomic<bool> exited_{false};
    std::atomic<bool> stopping_{false};
};
