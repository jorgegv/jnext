// NextPi under QEMU on every OS (GH #316): the Windows launch and the socket
// transport, tested natively on Linux and macOS and under wine for Windows.
//
// `# os: all` in test/unit-tests.conf. The FIFO transport and its shell-script
// QEMU stand-in stay in uart_posix_test (PI-*, `# os: posix`); this suite is the
// part of the feature that exists on Windows. It runs the SOCKET transport, which
// POSIX can select too (PiQemu::Spec::transport), so Linux and macOS exercise the
// same code path the Windows build ships.
//
// Since GH #331 it also holds the rows of the provisioner, the archive reader and the
// pure policy functions (moved from uart_posix_test, unchanged) and the Pi's audio:
// the stand-in opens the `-audiodev wav` path as QEMU does, and PiAudio serves a FIFO
// (POSIX) or a named pipe (Windows) that this process's own fopen("wb") writes.
//
// The QEMU stand-in is a program (nextpi_qemu_stub.cpp), copied as
// `qemu-system-arm[.exe]` and `qemu-img[.exe]`, because the Windows launcher uses
// CreateProcess. It plays the Pi on QEMU's `socket` chardev: listens on the port
// jnext gave it, sends `SUP> ` to each client and accepts again after one leaves.
//
// Oracles: RFC 1321 for the digests; qemu(1) 10.2 `-chardev socket` and QEMU's
// char-socket.c (host defaults to 0.0.0.0, a listener waits for a client unless
// wait=off, the listener re-arms after a disconnect); zxnext.vhd:2278-2281 for the
// NR 0xA0 gate that routes UART 1 to the Pi pins; the JoyUartLink contract
// (peripheral/joy_uart_link.h) for what a cable that comes and goes does.
//
// Run: ./build/test/nextpi_test

#include "core/emulator.h"
#include "core/emulator_config.h"
#include "core/nextpi_provisioner.h"
#include "audio/pi_audio.h"
#include "core/pi_qemu.h"
#include "esp01/esp_socket.h"
#include "esp01/esp_socket_platform.h"
#include "peripheral/joy_uart_link.h"
#include "peripheral/pi_uart_device.h"

#include "nextpi_fixtures.h"
#include "nextpi_support.h"
#include "../sdcard_provisioner/range_server.h"   // PIT-43: the loopback server that goes silent

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <climits>
#include <csignal>
#include <sys/wait.h>
#endif

#include "../row_id.h"
#include "../test_portable.h"

namespace fs = std::filesystem;

namespace {

int g_pass = 0, g_fail = 0, g_total = 0;

void check(const char* id, const char* desc, bool cond, const std::string& detail = {}) {
    report_row_id(id);
    ++g_total;
    if (cond) {
        ++g_pass;
    } else {
        ++g_fail;
        std::printf("  FAIL %s: %s", id, desc);
        if (!detail.empty()) std::printf(" [%s]", detail.c_str());
        std::printf("\n");
    }
}

std::string fmt(const char* f, ...) {
    char buf[768];
    va_list ap;
    va_start(ap, f);
    std::vsnprintf(buf, sizeof(buf), f, ap);
    va_end(ap);
    return std::string(buf);
}

std::string bytes_hex(const std::vector<uint8_t>& v) {
    std::string out;
    for (std::size_t i = 0; i < v.size(); ++i) {
        char b[8];
        std::snprintf(b, sizeof(b), "%s%02X", i ? " " : "", v[i]);
        out += b;
    }
    return out;
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// PiQemu::Spec::poll_sleep for PIT-24/25: every sleep the launcher asks for takes
// five times as long (a host that stretches short sleeps), and is counted, so a
// row can tell that the launcher's waits really went through it.
std::atomic<int> g_stretched_sleeps{0};
void stretched_sleep(int ms) {
    ++g_stretched_sleeps;
    sleep_ms(ms * 5);
}

long ms_since(std::chrono::steady_clock::time_point t0) {
    return static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                 std::chrono::steady_clock::now() - t0).count());
}

#ifdef _WIN32
constexpr const char* kExe = ".exe";
constexpr char kPathSep    = ';';
#else
constexpr const char* kExe = "";
constexpr char kPathSep    = ':';
#endif

/// An environment variable set for the life of the object, then put back.
class EnvVar {
public:
    EnvVar(const char* name, const std::string& value) : name_(name) {
        if (const char* old = std::getenv(name)) {
            had_ = true;
            old_ = old;
        }
        jtp::set_env(name, value.c_str());
    }
    ~EnvVar() {
        if (had_) jtp::set_env(name_.c_str(), old_.c_str());
        else      jtp::unset_env(name_.c_str());
    }
    EnvVar(const EnvVar&)            = delete;
    EnvVar& operator=(const EnvVar&) = delete;

private:
    std::string name_, old_;
    bool        had_ = false;
};

// ── a NextPi directory with the stand-in QEMU in its bin/ ───────────────────

class Fixture {
public:
    /// `dist_name` is the NextPi directory's name: a space and a comma in it
    /// exercise the quoting of every argument that carries a path.
    explicit Fixture(const std::string& tag, const std::string& dist_name = "dist") {
        root_ = fs::path(jtp::make_temp_dir("jnext-pit-" + tag + "-"));
        if (root_.empty()) return;
        std::error_code ec;
        dist_ = root_ / dist_name;
        bin_  = root_ / "bin";
        fs::create_directories(dist_ / "boot", ec);
        fs::create_directories(bin_, ec);
        for (const char* f : {"nextpi.img", "boot/kernel.img", "boot/bcm2708-rpi-zero.dtb"})
            std::ofstream(dist_ / f).put('\0');
        ok_ = !ec;
        for (const char* name : {"qemu-system-arm", "qemu-img"}) {
            fs::copy_file(JNEXT_NEXTPI_STUB, bin_ / (std::string(name) + kExe),
                          fs::copy_options::overwrite_existing, ec);
            if (ec) ok_ = false;
        }
    }
    ~Fixture() {
        std::error_code ec;
        if (!root_.empty()) fs::remove_all(root_, ec);
    }
    Fixture(const Fixture&)            = delete;
    Fixture& operator=(const Fixture&) = delete;

    bool        ok() const { return ok_; }
    std::string dir() const { return dist_.string(); }
    std::string bin_dir() const { return bin_.string(); }
    std::string bin(const char* name) const { return (bin_ / (std::string(name) + kExe)).string(); }

    /// What the stand-in wrote in bin/ ("args", "env", "probe", "received", ...).
    std::string file(const char* name) const {
        std::ifstream f(bin_ / name, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }
    std::vector<std::string> arg_lines() const {
        std::vector<std::string> out;
        std::string all = file("args"), line;
        for (char c : all) {
            if (c == '\n') { out.push_back(line); line.clear(); }
            else if (c != '\r') line.push_back(c);
        }
        return out;
    }
    /// The stand-in's pid once it has started (0 if it never did). `pid` is the
    /// last file it writes, so the others are complete when this returns.
    unsigned long child_pid() const {
        for (int i = 0; i < 1000; ++i) {
            const std::string p = file("pid");
            if (!p.empty() && p.back() == '\n') return std::strtoul(p.c_str(), nullptr, 10);
            sleep_ms(10);
        }
        return 0;
    }

private:
    fs::path root_, dist_, bin_;
    bool     ok_ = false;
};

// ── the host side of the console chardev, for rows that play QEMU ────────────

class Listener {
public:
    ~Listener() { close(); }
    bool open(std::uint16_t port = 0) {
        std::string err, warn;
        if (!esp::net::init(err)) return false;
        sock_ = esp::net::open_listener(esp::ipv4(127, 0, 0, 1), port, port_, warn, err);
        return sock_ != esp::net::kInvalidSocket;
    }
    void close() {
        if (sock_ != esp::net::kInvalidSocket) esp::net::close(sock_);
        sock_ = esp::net::kInvalidSocket;
    }
    std::uint16_t port() const { return port_; }
    /// One pending connection, or kInvalidSocket. Never waits.
    esp::net::NativeSocket accept() {
        esp::IpAddress peer;
        std::uint16_t peer_port = 0;
        bool failed = false;
        std::string err;
        return esp::net::accept_nonblocking(sock_, peer, peer_port, failed, err);
    }

private:
    esp::net::NativeSocket sock_ = esp::net::kInvalidSocket;
    std::uint16_t          port_ = 0;
};

bool peer_send(esp::net::NativeSocket s, const std::string& data) {
    bool failed = false;
    std::string err;
    std::size_t done = 0;
    for (int spins = 0; done < data.size() && spins < 500; ++spins) {
        done += esp::net::send(s, reinterpret_cast<const std::uint8_t*>(data.data()) + done,
                               data.size() - done, failed, err);
        if (failed) return false;
        if (done < data.size()) sleep_ms(2);
    }
    return done == data.size();
}

std::string peer_recv(esp::net::NativeSocket s) {
    std::uint8_t buf[256];
    bool eof = false, failed = false, reset = false;
    std::string err;
    const std::size_t n = esp::net::recv(s, buf, sizeof(buf), true, eof, failed, reset, err);
    return std::string(reinterpret_cast<char*>(buf), n);
}

// ── the guest side, as the PI rows drive it ──────────────────────────────────

EmulatorConfig tcp_config(std::uint16_t port) {
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    cfg.pi_uart_tcp_port     = port;
    return cfg;
}

EmulatorConfig launcher_config(const PiQemu& qemu) {
    EmulatorConfig cfg;
    cfg.type                 = MachineType::ZXN_ISSUE2;
    cfg.rewind_buffer_frames = 0;
    qemu.link_config(cfg);
    return cfg;
}

// One frame with NR 0xA0 held at `nr_a0` and UART 1 selected, then drain UART 1's
// RX FIFO through port 0x143B. NR 0xA0 is re-written every frame because the
// guest's own ROM runs inside run_frame() and owns the register too.
std::vector<uint8_t> pi_frame(Emulator& emu, uint8_t nr_a0 = 0x30) {
    emu.nextreg().write(0xA0, nr_a0);
    emu.nextreg().write(0x0B, 0x00);
    emu.run_frame();
    emu.port().out(0x153B, 0x40);
    std::vector<uint8_t> got;
    while (!emu.uart().channel(1).rx_empty()) got.push_back(emu.port().in(0x143B));
    return got;
}

void pi_transmit_frame(Emulator& emu, uint8_t byte, uint8_t nr_a0 = 0x30) {
    emu.nextreg().write(0xA0, nr_a0);
    emu.nextreg().write(0x0B, 0x00);
    emu.port().out(0x153B, 0x40);
    emu.port().out(0x133B, byte);
    emu.run_frame();
}

/// Run frames until `done()` or `timeout_ms`; everything the guest read from
/// UART 1 is appended to `heard`. The link only talks at the frame seam, so a
/// row that waits for the Pi has to keep the machine running while it waits.
template <class Done>
bool pump(Emulator& emu, std::vector<uint8_t>& heard, int timeout_ms, Done done) {
    const auto t0 = std::chrono::steady_clock::now();
    while (ms_since(t0) < timeout_ms) {
        const std::vector<uint8_t> got = pi_frame(emu);
        heard.insert(heard.end(), got.begin(), got.end());
        if (done()) return true;
        sleep_ms(5);
    }
    return done();
}

const std::vector<uint8_t> kSup = {'S', 'U', 'P', '>', ' '};

bool wait_for_file_text(const Fixture& fx, const char* name, const std::string& want, int ms) {
    const auto t0 = std::chrono::steady_clock::now();
    while (ms_since(t0) < ms) {
        if (fx.file(name) == want) return true;
        sleep_ms(20);
    }
    return fx.file(name) == want;
}

/// Pump the machine until the stand-in has received `want`.
bool pump_until_received(Emulator& emu, const Fixture& fx, const std::string& want, int ms) {
    std::vector<uint8_t> ignored;
    return pump(emu, ignored, ms, [&] { return fx.file("received") == want; });
}

std::string prepend_path(const std::string& dir) {
    const char* old = std::getenv("PATH");
    return dir + kPathSep + (old ? old : "");
}

// ── the holder process of PIT-13 ──────────────────────────────────────────────

int holder_main(int argc, char** argv) {
    if (argc < 4) return 2;
    PiQemu::Spec spec;
    spec.dir         = argv[2];
    spec.qemu_binary = argv[3];
    spec.audio       = "none";
    spec.transport   = PiQemu::Transport::Socket;
    auto* qemu = new PiQemu();          // never destroyed: the row kills this process
    std::string error;
    if (!qemu->start(spec, error)) {
        std::printf("failed: %s\n", error.c_str());
        return 3;
    }
    std::printf("ready\n");
    std::fflush(stdout);
    sleep_ms(100000);                   // bounded; the row kills it long before
    return 0;
}

} // namespace

static void test_nextpi_provisioner() {
    namespace fs = std::filesystem;
    const std::vector<uint8_t> disk_a = fake_nextpi_disk("kernel image a", "device tree a");
    const std::vector<uint8_t> disk_b = fake_nextpi_disk("kernel image b", "device tree b");

    // ── PI-10 — releases: the mirror listing yields each NextPi-<name>.tar.gz
    // once (not its .md5), and names compare as `sort -V` does, so "latest"
    // picks 1_100 over 1_93D and 1_93D over 1_93C.
    {
        const std::vector<std::string> names = nextpi::parse_release_listing(
            "<a href=\"NextPi-1_93C.tar.gz\">x</a> <a href=\"NextPi-1_93C.tar.gz.md5\">m</a>"
            "<a href=\"NextPi-1_93D.tar.gz\">NextPi-1_93D.tar.gz</a> <a href=\"NextPi-1_100.tar.gz\">");
        const bool listing = names == std::vector<std::string>({"1_93C", "1_93D", "1_100"});
        const bool order = nextpi::compare_release("1_100", "1_93D") > 0
                        && nextpi::compare_release("1_93D", "1_93C") > 0
                        && nextpi::compare_release("1_93D", "1_93D") == 0
                        && nextpi::compare_release("1_9", "1_10") < 0;
        std::string joined;
        for (const std::string& n : names) joined += n + " ";
        check("PI-10",
              "NextPi's release list is read from the mirror's NextPi-<name>.tar.gz "
              "links, once each, and release names order numerically (1_100 after 1_93D)",
              listing && order, fmt("listed [%s] order=%d", joined.c_str(), order ? 1 : 0));
    }

    // ── PI-11 — the tar stream: the image entry is found by suffix through a pax
    // path record, its size read from a GNU base-256 field, and written out
    // byte for byte; an archive without such an entry is an error.
    {
        FakeMirror mirror("tar");
        const fs::path archive = mirror.dir / "a.tar.gz";
        const fs::path out = mirror.dir / "out.img";
        std::string err, err_missing;
        const bool wrote = write_fake_release(archive.string(), "T", disk_a);
        const bool ok = wrote && nextpi::extract_tar_gz_entry(archive.string(), ".img", out.string(), {}, err);
        const bool same = ok && slurp(out) == std::string(disk_a.begin(), disk_a.end());
        const bool missing = !nextpi::extract_tar_gz_entry(archive.string(), ".iso", out.string(), {}, err_missing)
                           && err_missing.find(".iso") != std::string::npos;
        check("PI-11",
              "the NextPi archive reader finds the image through a pax path record, "
              "reads its GNU base-256 size, and writes it byte for byte; a missing "
              "entry is an error",
              ok && same && missing,
              fmt("ok=%d (%s) same=%d missing='%s'", ok ? 1 : 0, err.c_str(), same ? 1 : 0,
                  err_missing.c_str()));
    }

    // ── PI-12 — FIRST USE: asked once, the checksum and archive fetched, the
    // image and both boot files installed, the release recorded, the archive
    // deleted. The next run finds it ready: no question, no network.
    {
        FakeMirror mirror("first");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "nextpi";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const bool installed = r1.status == sdcard::ProvisionStatus::Ok && r1.release == "1_93D"
            && slurp(dir / "nextpi.img") == std::string(disk_a.begin(), disk_a.end())
            && slurp(dir / "boot" / "kernel.img") == "kernel image a"
            && slurp(dir / "boot" / "bcm2708-rpi-zero.dtb") == "device tree a"
            && nextpi::prepared_release(dir.string()) == "1_93D"
            && !fs::exists(dir / "NextPi-1_93D.tar.gz");
        const std::size_t fetched_first = mirror.fetched.size();
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const bool second_quiet = r2.status == sdcard::ProvisionStatus::Ok && confirms == 1
                                && mirror.fetched.size() == fetched_first;
        check("PI-12",
              "first use of NextPi asks once, downloads the default release with "
              "its MD5, installs the image and the two boot files from its FAT32 "
              "partition and deletes the archive; the next run asks nothing and "
              "fetches nothing",
              fixture && installed && fetched_first == 2 && second_quiet,
              fmt("fixture=%d installed=%d (%s) fetched=%zu (want 2) second_quiet=%d confirms=%d",
                  fixture ? 1 : 0, installed ? 1 : 0, r1.error.c_str(), fetched_first,
                  second_quiet ? 1 : 0, confirms));
    }

    // ── PI-13 — the refusals: declining downloads nothing and installs nothing;
    // an archive whose MD5 does not match is deleted and nothing is installed.
    {
        FakeMirror mirror("refuse");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir_no = mirror.dir / "declined";
        int confirms = 0;
        const nextpi::ProvisionResult declined =
            nextpi::provision(nextpi_options(mirror, dir_no.string(), "", confirms, false));
        const bool declined_ok = declined.status == sdcard::ProvisionStatus::Declined
                               && mirror.fetched.empty() && nextpi::prepared_release(dir_no.string()).empty();

        std::ofstream(mirror.dir / "NextPi-1_93D.tar.gz.md5") << "00000000000000000000000000000000\n";
        const fs::path dir_bad = mirror.dir / "corrupt";
        const nextpi::ProvisionResult corrupt =
            nextpi::provision(nextpi_options(mirror, dir_bad.string(), "", confirms));
        const bool corrupt_ok = corrupt.status == sdcard::ProvisionStatus::Failed
                              && corrupt.error.find("MD5") != std::string::npos
                              && !fs::exists(dir_bad / "NextPi-1_93D.tar.gz")
                              && nextpi::prepared_release(dir_bad.string()).empty();
        check("PI-13",
              "declining the NextPi download fetches and installs nothing; a download "
              "whose MD5 does not match is deleted and nothing is installed",
              fixture && declined_ok && corrupt_ok,
              fmt("fixture=%d declined=%d corrupt=%d ('%s')", fixture ? 1 : 0,
                  declined_ok ? 1 : 0, corrupt_ok ? 1 : 0, corrupt.error.c_str()));
    }

    // ── PI-14 — CHANGING RELEASE, and "latest". A directory holding another
    // release is replaced after asking, and its overlay (made over the old
    // image) discarded. "latest" picks the newest listed release; with the
    // mirror unreachable it keeps the installed one, with a warning, asking
    // nothing.
    {
        FakeMirror mirror("change");
        const bool fixture = mirror.add_release("1_93D", disk_a) && mirror.add_release("1_100", disk_b);
        const fs::path dir = mirror.dir / "nextpi";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "1_93D", confirms));
        std::ofstream(dir / "overlay.qcow2") << "old overlay";
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        const bool upgraded = r1.status == sdcard::ProvisionStatus::Ok
            && r2.status == sdcard::ProvisionStatus::Ok && r2.release == "1_100" && confirms == 2
            && slurp(dir / "boot" / "kernel.img") == "kernel image b" && !fs::exists(dir / "overlay.qcow2");
        mirror.offline = true;
        const nextpi::ProvisionResult r3 = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        const bool offline_kept = r3.status == sdcard::ProvisionStatus::Ok && r3.release == "1_100"
                                && !r3.warning.empty() && confirms == 2;
        check("PI-14",
              "the NextPi provisioner replaces a directory holding another release after asking, "
              "discarding its overlay; \"latest\" installs the newest listed release, and "
              "with the mirror unreachable keeps the installed one with a warning",
              fixture && upgraded && offline_kept,
              fmt("fixture=%d upgraded=%d (%s / %s, confirms=%d) offline_kept=%d ('%s')",
                  fixture ? 1 : 0, upgraded ? 1 : 0, r1.error.c_str(), r2.release.c_str(),
                  confirms, offline_kept ? 1 : 0, r3.warning.c_str()));
    }
}

static void test_nextpi_review_rows() {
    namespace fs = std::filesystem;

    // ── PI-16 — the warm-start recording machine (Emulator::warm_start_boot_config)
    // must not open the NextPi FIFOs: a second reader would steal the Pi's bytes.
    {
        EmulatorConfig live;
        live.type            = MachineType::ZXN_ISSUE2;
        live.sd_card_image   = "card.img";
        live.pi_uart_fifo_rx = "/tmp/x/uart.out";
        live.pi_uart_fifo_tx = "/tmp/x/uart.in";
        const EmulatorConfig b = Emulator::warm_start_boot_config(live);
        check("PI-16",
              "the warm-start recording boot gets no NextPi link (both FIFO paths cleared); the "
              "machine itself is kept",
              b.pi_uart_fifo_rx.empty() && b.pi_uart_fifo_tx.empty() && b.sd_card_image == "card.img",
              fmt("rx='%s' tx='%s'", b.pi_uart_fifo_rx.c_str(), b.pi_uart_fifo_tx.c_str()));
    }

    // ── PI-21 — the free-space check: with less free space than unpacking needs,
    // the download is refused before anything is fetched.
    {
        FakeMirror mirror("space");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        int confirms = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, (mirror.dir / "np").string(), "", confirms);
        o.space_needed = std::numeric_limits<uint64_t>::max();
        const nextpi::ProvisionResult r = nextpi::provision(o);
        check("PI-21",
              "NextPi is not downloaded when the directory lacks the free space unpacking needs: "
              "refused with the amounts, nothing fetched",
              fixture && r.status == sdcard::ProvisionStatus::Failed &&
                  r.error.find("free") != std::string::npos && mirror.fetched.empty(),
              fmt("status=%d error='%s' fetched=%zu", static_cast<int>(r.status), r.error.c_str(),
                  mirror.fetched.size()));
    }

    // ── PI-22 — long names: a GNU 'L' record, and a POSIX ustar prefix, each
    // supply the directory part a suffix match needs (neither short name has it).
    {
        FakeMirror mirror("names");
        const std::string data = "image bytes";
        std::string gnu = tar_entry("././@LongLink", "NextPi-T/very/deep/name.img", 'L');
        gnu += tar_entry("short-name", data);
        std::string ustar_hdr = tar_header("x.img", data.size(), '0');
        std::memcpy(&ustar_hdr[257], "ustar\0" "00", 8);           // POSIX, not GNU
        std::memcpy(&ustar_hdr[345], "NextPi-T/dir", 12);         // the prefix field
        std::memset(&ustar_hdr[148], ' ', 8);
        unsigned sum = 0;
        for (unsigned char c : ustar_hdr) sum += c;
        std::snprintf(&ustar_hdr[148], 8, "%06o", sum);
        std::string ustar = ustar_hdr + data;
        ustar.resize((ustar.size() + 511) / 512 * 512, '\0');
        const fs::path a_gnu = mirror.dir / "gnu.tar.gz", a_ustar = mirror.dir / "ustar.tar.gz";
        const fs::path out = mirror.dir / "out";
        std::string e1, e2;
        const bool ok_gnu = write_tar_gz(a_gnu.string(), gnu) &&
            nextpi::extract_tar_gz_entry(a_gnu.string(), "deep/name.img", out.string(), {}, e1) &&
            slurp(out) == data;
        const bool ok_ustar = write_tar_gz(a_ustar.string(), ustar) &&
            nextpi::extract_tar_gz_entry(a_ustar.string(), "dir/x.img", out.string(), {}, e2) &&
            slurp(out) == data;
        check("PI-22",
              "the NextPi archive reader takes an entry's full name from a GNU long-name record "
              "and from a POSIX ustar prefix",
              ok_gnu && ok_ustar, fmt("gnu=%d (%s) ustar=%d (%s)", ok_gnu ? 1 : 0, e1.c_str(),
                                      ok_ustar ? 1 : 0, e2.c_str()));
    }

    // ── PI-23 — A MALFORMED ARCHIVE IS AN ERROR, NOT A CRASH. Each bound has
    // its own case, and each case must fail with THAT bound's message: a
    // long-name record claiming ~2^64 bytes and an entry of 2^40+1 hit the
    // entry-size bound, an `x` header of 2^40 the metadata bound, and a pax
    // `size=` record of 2^40+1 the pax size bound. No exception escapes. (The
    // fixture's tag must not contain any of the words searched for: it is in
    // every archive path, so in every message.)
    {
        FakeMirror mirror("bounds");
        const fs::path out = mirror.dir / "out";
        auto try_one = [&](const char* name, const std::string& body, const char* want,
                           std::string& why) {
            const fs::path a = mirror.dir / name;
            if (!write_tar_gz(a.string(), body)) { why = "fixture"; return false; }
            try {
                return !nextpi::extract_tar_gz_entry(a.string(), ".img", out.string(), {}, why) &&
                       why.find(want) != std::string::npos;
            } catch (const std::exception& ex) {
                why = std::string("threw ") + ex.what();
                return false;
            }
        };
        std::string huge_l = tar_header("././@LongLink", 0, 'L');
        huge_l[124] = static_cast<char>(0x80);
        for (int i = 125; i < 136; ++i) huge_l[i] = static_cast<char>(0xFF);
        huge_l[135] = static_cast<char>(0x00);
        std::string big_x = tar_header("PaxHeaders/x", 1ull << 40, 'x', true);
        std::string big_file = tar_header("big.img", (1ull << 40) + 1, '0', true);
        std::string pax_size = tar_entry("PaxHeaders/big.img", pax_record("size", "1099511627777"), 'x') +
                               tar_entry("big.img", "");
        std::string w1, w2, w3, w4;
        const bool l_ok = try_one("l.tar.gz", huge_l, "an entry claims", w1);
        const bool x_ok = try_one("x.tar.gz", big_x, "name/metadata record", w2);
        const bool f_ok = try_one("f.tar.gz", big_file, "an entry claims", w3);
        const bool p_ok = try_one("p.tar.gz", pax_size, "a pax size of", w4);
        check("PI-23",
              "a NextPi archive with an absurd long-name, pax header, entry or pax size= record "
              "fails with that bound's \"malformed\" message instead of throwing (no crash after a "
              "6 GB download)",
              l_ok && x_ok && f_ok && p_ok,
              fmt("L: %d (%s) x: %d (%s) entry: %d (%s) pax size: %d (%s)", l_ok ? 1 : 0, w1.c_str(),
                  x_ok ? 1 : 0, w2.c_str(), f_ok ? 1 : 0, w3.c_str(), p_ok ? 1 : 0, w4.c_str()));
    }

    // ── PI-24 — a FAILED UPGRADE KEEPS THE OLD RELEASE. 1_93D is installed;
    // "latest" finds 1_100 whose archive (with a valid MD5) has no image: the
    // upgrade fails, and 1_93D is still prepared, marker and files intact.
    {
        FakeMirror mirror("keep");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("kernel old", "dtb old"));
        const fs::path broken = mirror.dir / "NextPi-1_100.tar.gz";
        const bool broken_ok = write_tar_gz(broken.string(), tar_entry("NextPi-1_100/readme.txt", "no image"));
        std::ofstream(broken.string() + ".md5") << nextpi::md5_file(broken.string()) << "\n";
        mirror.listing += "<a href=\"NextPi-1_100.tar.gz\">x</a>\n";
        const fs::path dir = mirror.dir / "np";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        const bool kept = r1.status == sdcard::ProvisionStatus::Ok && r2.status == sdcard::ProvisionStatus::Failed &&
                          nextpi::prepared_release(dir.string()) == "1_93D" &&
                          slurp(dir / "boot" / "kernel.img") == "kernel old" &&
                          !fs::exists(dir / "nextpi.img.part") && !fs::exists(dir / "boot.part");
        check("PI-24",
              "a NextPi upgrade that fails while unpacking leaves the installed release prepared "
              "and intact, with no partial files",
              fixture && broken_ok && kept,
              fmt("fixture=%d r1=%d r2=%d ('%s') prepared='%s'", fixture && broken_ok ? 1 : 0,
                  static_cast<int>(r1.status), static_cast<int>(r2.status), r2.error.c_str(),
                  nextpi::prepared_release(dir.string()).c_str()));
    }

    // ── PI-25 — the release name from Preferences goes into a file name and a
    // URL, so anything but a mirror-style name is refused before either.
    {
        FakeMirror mirror("names-check");
        int confirms = 0;
        bool all_refused = true;
        std::string detail;
        for (const char* bad : {"../escape", "a/b", "a b", ".hidden", "x?y=1"}) {
            const nextpi::ProvisionResult r =
                nextpi::provision(nextpi_options(mirror, (mirror.dir / "np").string(), bad, confirms));
            if (r.status != sdcard::ProvisionStatus::Failed || r.error.find("release name") == std::string::npos)
                all_refused = false;
            detail += std::string(bad) + "->" + std::to_string(static_cast<int>(r.status)) + " ";
        }
        const bool good = nextpi::valid_release_name("1_93D") && nextpi::valid_release_name("2.0-beta");
        check("PI-25",
              "a NextPi release name with a path separator, space, leading dot or URL syntax is "
              "refused before anything is asked or fetched; real names pass",
              all_refused && good && confirms == 0 && mirror.fetched.empty(),
              fmt("%sconfirms=%d fetched=%zu good=%d", detail.c_str(), confirms, mirror.fetched.size(),
                  good ? 1 : 0));
    }

    // ── PI-27 — THE START POLICY (main.cpp, through nextpi::start_outcome):
    // running is running; declining starts jnext without NextPi either way; a
    // failure is an error only when --nextpi asked for it, and from Preferences
    // a warning that lets jnext start.
    {
        using nextpi::StartOutcome;
        const auto Ok = sdcard::ProvisionStatus::Ok, No = sdcard::ProvisionStatus::Declined,
                   Bad = sdcard::ProvisionStatus::Failed;
        const bool table =
            nextpi::start_outcome(true, Ok, true) == StartOutcome::Started &&
            nextpi::start_outcome(false, Ok, true) == StartOutcome::Started &&
            nextpi::start_outcome(true, No, false) == StartOutcome::Declined &&
            nextpi::start_outcome(false, No, false) == StartOutcome::Declined &&
            nextpi::start_outcome(true, Bad, false) == StartOutcome::Exit &&
            nextpi::start_outcome(true, Ok, false) == StartOutcome::Exit &&
            nextpi::start_outcome(false, Bad, false) == StartOutcome::WarnAndContinue &&
            nextpi::start_outcome(false, Ok, false) == StartOutcome::WarnAndContinue;
        check("PI-27",
              "NextPi's start policy: a failure exits only when --nextpi asked for it (from "
              "Preferences it warns and continues); declining continues either way",
              table, "");
    }
}

static void test_nextpi_review2_rows() {
    namespace fs = std::filesystem;

    // ── PI-28 — THE INSTALL IS CRASH-SAFE (R2-3). The `release` marker is what
    // makes a directory "prepared", so it is removed BEFORE anything is
    // replaced and written LAST, through release.part and a rename. Two
    // installs are made to fail part-way, with obstacles that stop root too:
    //   a. the image cannot be put in place (a directory holds its name): the
    //      old marker must already be gone;
    //   b. the marker cannot be written (a directory holds release.part): the
    //      new release is in place, but the directory must not claim it.
    // Either way the next start re-provisions instead of booting a mixture.
    {
        FakeMirror mirror("swap");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        int confirms = 0;

        const fs::path dir_a = mirror.dir / "a";
        std::error_code ec;
        fs::create_directories(dir_a / "nextpi.img" / "in-the-way", ec);
        std::ofstream(dir_a / "release") << "1_92\n";
        const nextpi::ProvisionResult ra = nextpi::provision(nextpi_options(mirror, dir_a.string(), "", confirms));
        const bool a_ok = ra.status == sdcard::ProvisionStatus::Failed &&
                          ra.error.find("cannot install") != std::string::npos &&
                          !fs::exists(dir_a / "release");

        const fs::path dir_b = mirror.dir / "b";
        fs::create_directories(dir_b / "release.part" / "in-the-way", ec);
        const nextpi::ProvisionResult rb = nextpi::provision(nextpi_options(mirror, dir_b.string(), "", confirms));
        const bool b_ok = rb.status == sdcard::ProvisionStatus::Failed &&
                          fs::is_regular_file(dir_b / "nextpi.img") && !fs::exists(dir_b / "release") &&
                          nextpi::prepared_release(dir_b.string()).empty();
        check("PI-28",
              "a NextPi install that fails part-way leaves no release marker: it is removed before "
              "the image is replaced and written last through release.part, so the directory is "
              "never taken as prepared",
              fixture && a_ok && b_ok,
              fmt("fixture=%d a=%d ('%s', marker %s) b=%d ('%s', marker %s)", fixture ? 1 : 0,
                  a_ok ? 1 : 0, ra.error.c_str(), fs::exists(dir_a / "release") ? "kept" : "gone",
                  b_ok ? 1 : 0, rb.error.c_str(), fs::exists(dir_b / "release") ? "written" : "absent"));
    }

    // ── PI-29 — NAMES FROM THE MIRROR ARE CHECKED (R2-4). For "latest" the
    // release name comes from the mirror's listing, not from the user, and the
    // listing is the only place it is checked: a leading dot or dash, or more
    // than 64 characters, is not a release. The 65-digit name would win
    // "latest" numerically, so the provisioner must never fetch it.
    {
        const std::string long_name(65, '9');
        const std::string html = "<a href=\"NextPi-.hidden.tar.gz\">x</a> <a href=\"NextPi--rf.tar.gz\">x</a> "
                                 "<a href=\"NextPi-" + long_name + ".tar.gz\">x</a> "
                                 "<a href=\"NextPi-1_93D.tar.gz\">x</a>";
        const std::vector<std::string> names = nextpi::parse_release_listing(html);
        const bool listed_ok = names == std::vector<std::string>({"1_93D"});

        FakeMirror mirror("listing");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        mirror.listing += "<a href=\"NextPi-" + long_name + ".tar.gz\">x</a>\n";
        int confirms = 0;
        const fs::path dir = mirror.dir / "np";
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "latest", confirms));
        bool fetched_bad = false;
        for (const std::string& u : mirror.fetched)
            if (u.find(long_name) != std::string::npos) fetched_bad = true;
        std::string joined;
        for (const std::string& n : names) joined += n + " ";
        check("PI-29",
              "release names from the mirror's listing are checked like typed ones: a leading dot "
              "or dash or a 65-character name is not listed, and \"latest\" never fetches one",
              fixture && listed_ok && r.status == sdcard::ProvisionStatus::Ok && r.release == "1_93D" &&
                  !fetched_bad,
              fmt("listed [%s] (want 1_93D) latest=%s status=%d fetched_bad=%d", joined.c_str(),
                  r.release.c_str(), static_cast<int>(r.status), fetched_bad ? 1 : 0));
    }

    // ── PI-31 — A PAX RECORD CANNOT REACH OUTSIDE ITS HEADER (R2-8). Its length
    // is checked against what is left; 2^64-1 used to wrap `pos + len` and be
    // taken. Such a header is malformed, whatever the records after it say.
    {
        FakeMirror mirror("paxlen");
        const fs::path a = mirror.dir / "wrap.tar.gz", out = mirror.dir / "out";
        const std::string body = tar_entry("PaxHeaders/x", "18446744073709551615 path=EVIL.img\n", 'x') +
                                 tar_entry("data.bin", "payload");
        std::string why;
        bool refused = false;
        try {
            refused = write_tar_gz(a.string(), body) &&
                      !nextpi::extract_tar_gz_entry(a.string(), ".img", out.string(), {}, why) &&
                      why.find("pax record overruns") != std::string::npos;
        } catch (const std::exception& ex) {
            why = std::string("threw ") + ex.what();
        }
        check("PI-31",
              "a pax record whose length runs past its header (18446744073709551615) makes the "
              "archive malformed instead of wrapping and renaming the next entry",
              refused, fmt("error='%s'", why.c_str()));
    }

    // ── PI-39..42 — EACH WELL-FORMEDNESS CHECK OF THE PAX PARSER (R3-2). A pax
    // record is "<len> <key>=<value>\n", <len> counting the whole record. Each
    // archive below breaks exactly one rule, after a valid first record, and
    // must be refused as malformed; with that one check gone it would be read.
    {
        FakeMirror mirror("paxshape");
        const fs::path out = mirror.dir / "out";
        // `pad_newline_at` puts a '\n' at that offset of the header's data
        // block, in the zero padding past its end.
        auto refused = [&](const char* name, const std::string& records, std::string& why,
                           std::size_t pad_newline_at = 0) {
            const fs::path a = mirror.dir / name;
            std::string x = tar_entry("PaxHeaders/x", records, 'x');
            if (pad_newline_at) x[512 + pad_newline_at] = '\n';
            const std::string body = x + tar_entry("data.bin", "payload");
            try {
                return write_tar_gz(a.string(), body) &&
                       !nextpi::extract_tar_gz_entry(a.string(), ".img", out.string(), {}, why) &&
                       why.find("pax record overruns") != std::string::npos;
            } catch (const std::exception& ex) {
                why = std::string("threw ") + ex.what();
                return false;
            }
        };
        const std::string valid = pax_record("comment", "ok");

        // PI-39 — a record claiming more than what is left of the header: 99
        // bytes, and 2^64-1 (which wrapped `pos + len` in the original code).
        // The wrapped case is also refused by the space rule: a wrapped end
        // lies before `pos`, and so before the record's own space. And one
        // claiming 30 whose 30th byte, in the padding past the header, is a
        // '\n': only the length check stands between it and being read.
        std::string w1, w2, w6;
        const bool short_ok = refused("long.tar.gz", valid + "99 path=EVIL.img\n", w1);
        const bool wrap_ok  = refused("wrap.tar.gz", valid + "18446744073709551615 path=EVIL.img\n", w2);
        const bool pad_ok   = refused("pad.tar.gz", valid + "30 path=EVIL.img\n", w6, valid.size() + 29);
        check("PI-39",
              "a pax record after a valid one that claims more than is left of the header (99, "
              "2^64-1, or 30 with a '\\n' in the padding where it would end) makes the archive "
              "malformed",
              short_ok && wrap_ok && pad_ok,
              fmt("99: %d (%s) 2^64-1: %d (%s) padded: %d (%s)", short_ok ? 1 : 0, w1.c_str(),
                  wrap_ok ? 1 : 0, w2.c_str(), pad_ok ? 1 : 0, w6.c_str()));

        // PI-40 — the right length, but the record does not end on '\n'.
        std::string w3;
        const bool nl_ok = refused("nl.tar.gz", valid + "19 path=NextPi.imgX", w3);
        check("PI-40", "a pax record that does not end on '\\n' makes the archive malformed", nl_ok,
              fmt("error='%s'", w3.c_str()));

        // PI-41 — the length ends before the record's first space: "3\n\n" is
        // three bytes ending on '\n', and the space found is the next record's.
        std::string w4;
        const bool sp_ok = refused("space.tar.gz", valid + "3\n\n" + pax_record("path", "x.img"), w4);
        check("PI-41",
              "a pax record whose length ends before its first space (the space found belongs to the "
              "next record) makes the archive malformed",
              sp_ok, fmt("error='%s'", w4.c_str()));

        // PI-42 — no space at all in what is left.
        std::string w5;
        const bool nosp_ok = refused("nospace.tar.gz", valid + "4ab\n", w5);
        check("PI-42", "a pax record with no space after its length makes the archive malformed", nosp_ok,
              fmt("error='%s'", w5.c_str()));
    }

    // ── PI-43 — THE MARKER CANNOT BE REMOVED (R3-4). Removing `release` is the
    // install's first step; when it fails (here a non-empty directory holds
    // the name) the install stops there, before the image is put in place.
    {
        FakeMirror mirror("marker");
        const bool fixture = mirror.add_release("1_93D", fake_nextpi_disk("k", "d"));
        const fs::path dir = mirror.dir / "np";
        std::error_code ec;
        fs::create_directories(dir / "release" / "in-the-way", ec);
        int confirms = 0;
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        check("PI-43",
              "a NextPi install whose release marker cannot be removed fails at once, before the "
              "image is put in place",
              fixture && r.status == sdcard::ProvisionStatus::Failed &&
                  r.error.find("cannot install") != std::string::npos && !fs::exists(dir / "nextpi.img"),
              fmt("status=%d error='%s' image %s", static_cast<int>(r.status), r.error.c_str(),
                  fs::exists(dir / "nextpi.img") ? "installed" : "absent"));
    }

    // ── PI-44 — THE NAME LENGTH LIMIT, AT ITS BOUNDARY (R3-5): a release name
    // of 64 characters is accepted, one of 65 refused.
    {
        const bool at_64 = nextpi::valid_release_name(std::string(64, 'a'));
        const bool at_65 = nextpi::valid_release_name(std::string(65, 'a'));
        check("PI-44", "a NextPi release name of 64 characters is valid and one of 65 is not",
              at_64 && !at_65, fmt("64: %d 65: %d", at_64 ? 1 : 0, at_65 ? 1 : 0));
    }

    // ── PI-33 — THE CHILD ENVIRONMENT ITSELF (R2-10). PiQemu::child_environment
    // drops every LANG and LC_ALL entry before adding its own, so the child
    // gets each exactly once. (A shell in between, as PI-18 sees it, would
    // hide a duplicate; qemu-img is spawned directly and would not.)
    {
        const char* env[] = {"PATH=/bin", "LANG=es_ES.UTF-8", "LC_ALL=es_ES.UTF-8", "LANGUAGE=es",
                             "LC_ALL=fr_FR", nullptr};
        const std::vector<std::string> got = PiQemu::child_environment(env);
        const std::vector<std::string> want = {"PATH=/bin", "LANGUAGE=es", "LANG=C", "LC_ALL=C"};
        std::string joined;
        for (const std::string& v : got) joined += v + " ";
        check("PI-33",
              "a spawned NextPi child's environment is the parent's without any LANG or LC_ALL "
              "entry, then LANG=C and LC_ALL=C, each exactly once (LANGUAGE is not LANG)",
              got == want, fmt("got [%s]", joined.c_str()));
    }

    // ── PI-38 — WHETHER TO START NEXTPI (R2-5), main.cpp's decision through
    // nextpi::start_request. The command line wins either way; without it a
    // GUI session follows Preferences and a headless one starts nothing.
    // Only --nextpi makes a failure an error (asked_on_cli; PI-27).
    {
        using nextpi::start_request;
        auto is = [](nextpi::StartRequest r, bool wanted, bool asked) {
            return r.wanted == wanted && r.asked_on_cli == asked;
        };
        const bool table =
            is(start_request(true, true, true, false), true, true) &&     // GUI --nextpi, pref off
            is(start_request(true, true, false, true), false, false) &&   // GUI --no-nextpi, pref on
            is(start_request(true, false, false, true), true, false) &&   // GUI, pref on
            is(start_request(true, false, false, false), false, false) && // GUI, pref off
            is(start_request(false, true, true, false), true, true) &&    // headless --nextpi
            is(start_request(false, false, false, true), false, false) && // headless: no Preferences
            is(start_request(false, true, false, true), false, false);    // headless --no-nextpi
        check("PI-38",
              "whether NextPi starts: --nextpi and --no-nextpi win, otherwise a GUI session follows "
              "the [nextpi] preference and a headless one starts nothing; only --nextpi makes a "
              "failure an error",
              table, "");
    }
}

// ── PiAudio over its pipe: a FIFO on POSIX, a named pipe on Windows (GH #331) ──
//
// The writer is this process's own fopen("wb") on the path, the call QEMU's wav
// back-end makes (audio/wavaudio.c:116), unbuffered so each fwrite reaches the
// pipe. jnext-only rows: no VHDL counterpart; the stream format is QEMU's
// (wavaudio.c:78-90: a 44-byte header, then s16 stereo at 44.1 kHz).

namespace {

/// The path a row's PiAudio serves: a FIFO in a directory of its own on POSIX,
/// a pipe name unique to this process and row on Windows.
struct AudioPath {
    std::string path, dir;
    explicit AudioPath(int k) {
#ifdef _WIN32
        path = "\\\\.\\pipe\\jnext-pit-" + std::to_string(nxs::self_pid()) + "-" + std::to_string(k) + ".audio";
#else
        dir  = jtp::make_temp_dir("jnext-pit-au" + std::to_string(k) + "-");
        path = dir + "/audio.fifo";
#endif
    }
    ~AudioPath() {
        if (!dir.empty()) { std::error_code ec; fs::remove_all(dir, ec); }
    }
};

std::string wav_header() {
    std::string h;
    auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) h.push_back(static_cast<char>(v >> (8 * i))); };
    auto u16 = [&](uint16_t v) { h.push_back(static_cast<char>(v)); h.push_back(static_cast<char>(v >> 8)); };
    h += "RIFF"; u32(0); h += "WAVEfmt "; u32(16); u16(1); u16(2);
    u32(44100); u32(44100 * 4); u16(4); u16(16); h += "data"; u32(0);
    return h;
}

void put_frame(std::string& s, int16_t l, int16_t r) {
    s.push_back(static_cast<char>(l)); s.push_back(static_cast<char>(l >> 8));
    s.push_back(static_cast<char>(r)); s.push_back(static_cast<char>(r >> 8));
}

/// fopen(path, "wb") as QEMU does, retried for up to `ms` (a Windows pipe that
/// has just lost its writer is busy for a moment); unbuffered.
std::FILE* open_writer(const std::string& path, int ms) {
    for (int waited = 0;; waited += 5) {
        if (std::FILE* f = std::fopen(path.c_str(), "wb")) {
            std::setvbuf(f, nullptr, _IONBF, 0);
            return f;
        }
        if (waited >= ms) return nullptr;
        sleep_ms(5);
    }
}

template <class Pred>
bool wait_for(Pred p, int ms) {
    for (int waited = 0; waited <= ms; waited += 5) {
        if (p()) return true;
        sleep_ms(5);
    }
    return p();
}

} // namespace

static void test_pi_audio_pipe() {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);   // a reader closed under a writer is a write error, not a kill
#endif

    // ── PIT-19 — A WRITER THAT LEAVES, AND THE NEXT ONE (MX-34 on every OS). The
    // first stream ends with half a frame; the second starts with a header of its
    // own. Nothing of the first may leak into the second: the header is skipped
    // again and the stray bytes are not glued to its first frame. Oracle: QEMU's
    // wav stream (wavaudio.c:78-90); kPrebuffer = 2205 frames before playback.
    {
        AudioPath ap(19);
        PiAudio au;
        std::string err;
        const bool opened = au.open(ap.path, err);
        bool a_ok = false, got_a = false, left = false, b_ok = false, got_b = false;
        std::FILE* a = opened ? open_writer(ap.path, 2000) : nullptr;
        if (a) {
            std::string s = wav_header();
            put_frame(s, 500, -500);
            s += "xy";
            a_ok = std::fwrite(s.data(), 1, s.size(), a) == s.size();
            got_a = wait_for([&] { return au.frames_received() >= 1; }, 2000);
            const uint64_t eof0 = au.eof_reads();
            std::fclose(a);
            left = wait_for([&] { return au.eof_reads() > eof0; }, 2000);
        }
        std::FILE* b = (opened && left) ? open_writer(ap.path, 2000) : nullptr;
        if (b) {
            std::string s = wav_header();
            for (int i = 0; i < 2205; ++i)
                put_frame(s, static_cast<int16_t>(1000 + i), static_cast<int16_t>(-(1000 + i)));
            b_ok = std::fwrite(s.data(), 1, s.size(), b) == s.size();
            got_b = wait_for([&] { return au.frames_received() >= 2206; }, 2000);
            std::fclose(b);
        }
        const uint32_t avail = au.available();
        bool exact = avail == 2206;
        int16_t l = 0, r = 0;
        exact = exact && au.pop(l, r) && l == 500 && r == -500;
        for (int i = 0; exact && i < 2205; ++i)
            exact = au.pop(l, r) && l == 1000 + i && r == -(1000 + i);
        check("PIT-19",
              "a writer that leaves and the next one that connects: the second stream's header is "
              "skipped and the first's half frame is not glued to it (jnext-only, no VHDL counterpart)",
              opened && a && a_ok && got_a && left && b && b_ok && got_b && exact,
              fmt("opened=%d (%s) a=%d got_a=%d left=%d b=%d got_b=%d avail=%u (want 2206) exact=%d",
                  opened ? 1 : 0, err.c_str(), a ? 1 : 0, got_a ? 1 : 0, left ? 1 : 0, b ? 1 : 0,
                  got_b ? 1 : 0, avail, exact ? 1 : 0));
    }

    // ── PIT-20 — A WRITER IS NEVER BLOCKED BY A FULL RING (MX-33 + MX-42 on every
    // OS). Nothing pops while 200 000 frames are written: the reader keeps
    // draining, the ring keeps its capacity (131 072 frames), the rest is
    // counted as dropped, and the next pop flushes what is stale.
    {
        AudioPath ap(20);
        PiAudio au;
        std::string err;
        const bool opened = au.open(ap.path, err);
        std::FILE* w = opened ? open_writer(ap.path, 2000) : nullptr;
        std::atomic<bool> done{false};
        std::thread t;
        if (w) {
            t = std::thread([&] {
                std::string s = wav_header();
                std::fwrite(s.data(), 1, s.size(), w);
                for (int sent = 0; sent < 200000;) {
                    std::string chunk;
                    for (int i = 0; i < 4096 && sent < 200000; ++i, ++sent)
                        put_frame(chunk, static_cast<int16_t>(sent), static_cast<int16_t>(-sent));
                    if (std::fwrite(chunk.data(), 1, chunk.size(), w) != chunk.size()) break;
                }
                done = true;
            });
        }
        const bool finished = w && wait_for([&] { return done.load(); }, 5000);
        const bool counted  = finished && wait_for([&] {
            return au.frames_received() + au.frames_dropped() == 200000; }, 2000);
        const uint64_t recv = au.frames_received(), drop = au.frames_dropped();
        int16_t l = 0, r = 0;
        const bool flushed = counted && !au.pop(l, r) && au.frames_dropped() == 200000;
        if (!finished) au.close();            // breaks the pipe so the writer cannot hang the row
        if (t.joinable()) t.join();
        if (w) std::fclose(w);
        check("PIT-20",
              "a writer sending far more than the ring holds is never blocked: the ring keeps its "
              "131072 frames, the rest is counted as dropped, and the next pop flushes it "
              "(jnext-only, no VHDL counterpart)",
              opened && w && finished && counted && recv == PiAudio::kCapacity && drop == 200000 - PiAudio::kCapacity &&
                  flushed,
              fmt("opened=%d (%s) finished=%d counted=%d received=%llu dropped=%llu flushed=%d",
                  opened ? 1 : 0, err.c_str(), finished ? 1 : 0, counted ? 1 : 0,
                  static_cast<unsigned long long>(recv), static_cast<unsigned long long>(drop),
                  flushed ? 1 : 0));
    }

    // ── PIT-21 — close() IS PROMPT AND RELEASES THE NAME, with no writer ever
    // connected and with one connected but silent. A reader that waited for data
    // would hang here; one that leaked its handle would leave the name taken (on
    // Windows a second server on a 1-instance name is refused, ERROR_PIPE_BUSY).
    {
        AudioPath ap(21);
        std::string err1, err2, err3;
        long ms_a = -1, ms_b = -1;
        bool reopened_a = false, reopened_b = false;
        {
            PiAudio au;
            const bool o = au.open(ap.path, err1);
            const auto t0 = std::chrono::steady_clock::now();
            au.close();
            ms_a = o ? ms_since(t0) : -1;
        }
        {
            PiAudio au;
            reopened_a = au.open(ap.path, err2);
            std::FILE* w = reopened_a ? open_writer(ap.path, 2000) : nullptr;
            if (w) {
                const std::string h = wav_header();
                std::fwrite(h.data(), 1, h.size(), w);
                sleep_ms(50);
                const auto t0 = std::chrono::steady_clock::now();
                au.close();
                ms_b = ms_since(t0);
                std::fclose(w);
            }
        }
        {
            PiAudio au;
            reopened_b = au.open(ap.path, err3);
        }
        check("PIT-21",
              "closing the Pi audio reader returns within a second whether or not a writer ever "
              "connected, and the name can be served again afterwards (jnext-only, no VHDL counterpart)",
              ms_a >= 0 && ms_a < 1000 && reopened_a && ms_b >= 0 && ms_b < 1000 && reopened_b,
              fmt("close without writer=%ld ms, with silent writer=%ld ms, reopen=%d/%d (%s / %s)", ms_a,
                  ms_b, reopened_a ? 1 : 0, reopened_b ? 1 : 0, err2.c_str(), err3.c_str()));
    }

    // ── PIT-22 — NO SPINNING WITHOUT A WRITER. After the writer has left, a
    // second of waiting costs under 100 ms of CPU (every OS), and at most 60
    // writer-gone reads (POSIX: bounds the poll rate, a 20 ms pause after each,
    // about 50; Windows: counts departures, 1) and at most 60 wakeups (Windows: each
    // return of the reader's one wait, so a timed poll in it shows; POSIX: none while
    // there is no writer). A poll built elsewhere than in that wait is not seen.
    {
        AudioPath ap(22);
        PiAudio au;
        std::string err;
        const bool opened = au.open(ap.path, err);
        std::FILE* w = opened ? open_writer(ap.path, 2000) : nullptr;
        bool got = false;
        if (w) {
            std::string s = wav_header();
            put_frame(s, 1, -1);
            std::fwrite(s.data(), 1, s.size(), w);
            got = wait_for([&] { return au.frames_received() >= 1; }, 2000);
            std::fclose(w);
        }
        sleep_ms(200);
        const long cpu0 = nxs::process_cpu_ms();
        const uint64_t eof0 = au.eof_reads(), idle0 = au.idle_reads();
        sleep_ms(1000);
        const long cpu = nxs::process_cpu_ms() - cpu0;
        const uint64_t eofs = au.eof_reads() - eof0, idles = au.idle_reads() - idle0;
        check("PIT-22",
              "with no writer connected the Pi audio reader neither spins nor polls fast: under 100 ms of "
              "CPU, at most 60 writer-gone reads and at most 60 wakeups in a second (jnext-only, no VHDL "
              "counterpart)",
              opened && w && got && cpu >= 0 && cpu < 100 && eofs <= 60 && idles <= 60,
              fmt("opened=%d (%s) got=%d cpu=%ld ms (want <100) eof_reads=%llu idle_reads=%llu (want <=60)",
                  opened ? 1 : 0, err.c_str(), got ? 1 : 0, cpu, static_cast<unsigned long long>(eofs),
                  static_cast<unsigned long long>(idles)));
    }
}

// GH #358 — an archive already in the NextPi directory, a download step that fails
// after a complete transfer, and an unusable directory. Offline: FakeMirror seam.
static void test_nextpi_local_archive_rows() {
    namespace fs = std::filesystem;
    const std::vector<uint8_t> disk_a = fake_nextpi_disk("kernel image a", "device tree a");
    const std::vector<uint8_t> disk_b = fake_nextpi_disk("kernel image b", "device tree b");
    const std::string disk_a_bytes(disk_a.begin(), disk_a.end());
    // What `wget` gives a user: the mirror's archive and its .md5 in the NextPi directory.
    auto place = [](FakeMirror& m, const fs::path& dir, const std::string& rel, bool with_md5) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string name = "NextPi-" + rel + ".tar.gz";
        fs::copy_file(m.dir / name, dir / name, fs::copy_options::overwrite_existing, ec);
        if (with_md5)
            fs::copy_file(m.dir / (name + ".md5"), dir / (name + ".md5"),
                          fs::copy_options::overwrite_existing, ec);
        return !ec;
    };
    auto installed_a = [&](const fs::path& dir) {
        return slurp(dir / "nextpi.img") == disk_a_bytes &&
               slurp(dir / "boot" / "kernel.img") == "kernel image a" &&
               slurp(dir / "boot" / "bcm2708-rpi-zero.dtb") == "device tree a" &&
               nextpi::prepared_release(dir.string()) == "1_93D" &&
               !fs::exists(dir / "NextPi-1_93D.tar.gz") && !fs::exists(dir / "NextPi-1_93D.tar.gz.md5");
    };
    const std::string md5_url = "https://mirror.test/NextPi2/NextPi-1_93D.tar.gz.md5";
    const std::string tgz_url = "https://mirror.test/NextPi2/NextPi-1_93D.tar.gz";

    // ── PIT-27 — JANKO'S CASE (GH #358): the mirror's archive and its .md5 are already in
    // the empty NextPi directory. Nothing is asked, nothing fetched, it is installed.
    {
        FakeMirror mirror("localboth");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        const bool placed = place(mirror, dir, "1_93D", true);
        int confirms = 0;
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        check("PIT-27",
              "an archive and its .md5 already in the NextPi directory are checked and installed with no "
              "question and no download, then deleted (GH #358; jnext-only, no VHDL counterpart)",
              fixture && placed && r.status == sdcard::ProvisionStatus::Ok && confirms == 0 &&
                  mirror.fetched.empty() && installed_a(dir),
              fmt("fixture=%d placed=%d status=%d (%s) confirms=%d fetched=%zu installed=%d", fixture ? 1 : 0,
                  placed ? 1 : 0, static_cast<int>(r.status), r.error.c_str(), confirms, mirror.fetched.size(),
                  installed_a(dir) ? 1 : 0));
    }

    // ── PIT-28 — the archive alone: only the (tiny) .md5 is fetched; still no question.
    {
        FakeMirror mirror("localarch");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        const bool placed = place(mirror, dir, "1_93D", false);
        int confirms = 0;
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        check("PIT-28",
              "an archive with no .md5 beside it is checked against the mirror's .md5, which is the only "
              "thing fetched, and installed with no question (GH #358; jnext-only, no VHDL counterpart)",
              fixture && placed && r.status == sdcard::ProvisionStatus::Ok && confirms == 0 &&
                  mirror.fetched == std::vector<std::string>({md5_url}) && installed_a(dir),
              fmt("fixture=%d placed=%d status=%d (%s) confirms=%d fetched=%zu", fixture ? 1 : 0, placed ? 1 : 0,
                  static_cast<int>(r.status), r.error.c_str(), confirms, mirror.fetched.size()));
    }

    // ── PIT-29 — THE WINDOWS FAILURE, MODELLED (GH #358): the backend transfers the whole
    // archive, then reports failure ("produced no file"). The complete download must
    // survive, and the next run must find and install it without fetching it again.
    {
        FakeMirror mirror("lostdl");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        int confirms = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "", confirms);
        sdcard::DownloadFn real = mirror.download();
        o.download = [real](const std::string& u, const std::string& dest, const sdcard::ProgressFn& p,
                            std::string& e) {
            if (!real(u, dest, p, e)) return false;
            if (u.size() > 7 && u.compare(u.size() - 7, 7, ".tar.gz") == 0) {
                e = "produced no file: " + dest;
                return false;
            }
            return true;
        };
        const nextpi::ProvisionResult r1 = nextpi::provision(o);
        const std::string mirror_bytes = slurp(mirror.dir / "NextPi-1_93D.tar.gz");
        const bool kept = r1.status == sdcard::ProvisionStatus::Failed &&
                          r1.error.find("produced no file") != std::string::npos &&
                          slurp(dir / "NextPi-1_93D.tar.gz") == mirror_bytes && !mirror_bytes.empty() &&
                          fs::exists(dir / "NextPi-1_93D.tar.gz.md5");
        mirror.fetched.clear();
        const nextpi::ProvisionResult r2 = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        check("PIT-29",
              "a download step that fails after the whole archive arrived does not delete it: the next run "
              "checks it, fetches nothing and installs it without asking again (GH #358; jnext-only, no "
              "VHDL counterpart)",
              fixture && kept && r2.status == sdcard::ProvisionStatus::Ok && mirror.fetched.empty() &&
                  confirms == 1 && installed_a(dir),
              fmt("fixture=%d kept=%d (%s) second status=%d (%s) fetched=%zu confirms=%d (want 1)",
                  fixture ? 1 : 0, kept ? 1 : 0, r1.error.c_str(), static_cast<int>(r2.status),
                  r2.error.c_str(), mirror.fetched.size(), confirms));
    }

    // ── PIT-30 — a pre-existing archive that fails its MD5 (truncated): the question names
    // it and the MD5; declining deletes nothing and fetches nothing; accepting downloads over it.
    {
        FakeMirror mirror("truncated");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        const bool placed = place(mirror, dir, "1_93D", true);
        std::string whole = slurp(dir / "NextPi-1_93D.tar.gz");
        const std::string half = whole.substr(0, whole.size() / 2);
        { std::ofstream(dir / "NextPi-1_93D.tar.gz", std::ios::binary | std::ios::trunc) << half; }
        std::string message;
        int asked = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "", asked, false);
        o.confirm = [&](const std::string& m) { ++asked; message = m; return false; };
        const nextpi::ProvisionResult declined = nextpi::provision(o);
        const bool decl_ok = declined.status == sdcard::ProvisionStatus::Declined && asked == 1 &&
                             message.find("NextPi-1_93D.tar.gz") != std::string::npos &&
                             message.find("MD5") != std::string::npos &&
                             message.find("cannot be used") == std::string::npos &&
                             slurp(dir / "NextPi-1_93D.tar.gz") == half && mirror.fetched.empty();
        int confirms = 0;
        const nextpi::ProvisionResult accepted = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        const bool acc_ok = accepted.status == sdcard::ProvisionStatus::Ok && confirms == 1 &&
                            mirror.fetched == std::vector<std::string>({md5_url, tgz_url}) && installed_a(dir);
        check("PIT-30",
              "a pre-existing archive that fails its MD5 is named in the question with the MD5, is not "
              "deleted or replaced before the user agrees to a download, and is then downloaded over "
              "(GH #358; jnext-only, no VHDL counterpart)",
              fixture && placed && decl_ok && acc_ok,
              fmt("fixture=%d placed=%d declined=%d (status=%d asked=%d fetched=%zu) accepted=%d (%s) msg='%.120s'",
                  fixture ? 1 : 0, placed ? 1 : 0, decl_ok ? 1 : 0, static_cast<int>(declined.status), asked,
                  mirror.fetched.size(), acc_ok ? 1 : 0, accepted.error.c_str(), message.c_str()));
    }

    // ── PIT-31 — an unusable directory says why: the first missing piece, in the question.
    {
        FakeMirror mirror("reason");
        const fs::path d1 = mirror.dir / "imgonly", d2 = mirror.dir / "nomarker", d3 = mirror.dir / "complete",
                       d4 = mirror.dir / "emptymarker";
        std::error_code ec;
        for (const fs::path& d : {d1, d2, d3, d4}) fs::create_directories(d / "boot", ec);
        for (const fs::path& d : {d1, d2, d3, d4}) std::ofstream(d / "nextpi.img").put('\0');
        for (const fs::path& d : {d2, d3, d4}) {
            std::ofstream(d / "boot" / "kernel.img").put('\0');
            std::ofstream(d / "boot" / "bcm2708-rpi-zero.dtb").put('\0');
        }
        std::ofstream(d3 / "release") << "1_93D\n";
        std::ofstream(d4 / "release") << "\n";
        const std::string r1 = nextpi::unprepared_reason(d1.string());
        const std::string r2 = nextpi::unprepared_reason(d2.string());
        const std::string r3 = nextpi::unprepared_reason(d3.string());
        const std::string r4 = nextpi::unprepared_reason(d4.string());
        std::string message;
        int asked = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, d1.string(), "", asked, false);
        o.confirm = [&](const std::string& m) { ++asked; message = m; return false; };
        const nextpi::ProvisionResult r = nextpi::provision(o);
        check("PIT-31",
              "unprepared_reason names the first missing piece (kernel.img, the release marker) and is empty "
              "for a complete directory; the question about an unusable directory says which piece is "
              "missing (GH #358; jnext-only, no VHDL counterpart)",
              r1.find("kernel.img") != std::string::npos && r1.find("missing") != std::string::npos &&
                  r2.find("release") != std::string::npos && r3.empty() && r4.find("empty") != std::string::npos &&
                  r.status == sdcard::ProvisionStatus::Declined && asked == 1 &&
                  message.find("kernel.img") != std::string::npos,
              fmt("image-only='%s' no-marker='%s' complete='%s' empty-marker='%s' msg='%.160s'", r1.c_str(),
                  r2.c_str(), r3.c_str(), r4.c_str(), message.c_str()));
    }

    // ── PIT-32 — a release switch with a verified local archive: one question that does not
    // promise a 6 GB download; yes installs it with nothing fetched and discards the overlay.
    {
        FakeMirror mirror("switch");
        const bool fixture = mirror.add_release("1_93D", disk_a) && mirror.add_release("1_100", disk_b);
        const fs::path dir = mirror.dir / "np";
        int confirms = 0;
        const nextpi::ProvisionResult r1 = nextpi::provision(nextpi_options(mirror, dir.string(), "1_93D", confirms));
        std::ofstream(dir / "overlay.qcow2") << "old overlay";
        const bool placed = place(mirror, dir, "1_100", true);
        mirror.fetched.clear();
        confirms = 0;
        std::string message;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "1_100", confirms);
        o.confirm = [&](const std::string& m) { ++confirms; message = m; return true; };
        const nextpi::ProvisionResult r2 = nextpi::provision(o);
        check("PIT-32",
              "switching release with a verified archive already in the directory asks once, without "
              "promising a 6 GB download, then installs it with nothing fetched and discards the old "
              "overlay (GH #358; jnext-only, no VHDL counterpart)",
              fixture && placed && r1.status == sdcard::ProvisionStatus::Ok && confirms == 1 &&
                  message.find("about 6 GB") == std::string::npos &&
                  r2.status == sdcard::ProvisionStatus::Ok && r2.release == "1_100" &&
                  mirror.fetched.empty() && !fs::exists(dir / "overlay.qcow2") &&
                  slurp(dir / "boot" / "kernel.img") == "kernel image b",
              fmt("fixture=%d placed=%d first=%d second=%d (%s) release=%s confirms=%d fetched=%zu msg='%.100s'",
                  fixture ? 1 : 0, placed ? 1 : 0, static_cast<int>(r1.status), static_cast<int>(r2.status),
                  r2.error.c_str(), r2.release.c_str(), confirms, mirror.fetched.size(), message.c_str()));
    }

    // ── PIT-33 — A DOWNLOAD THAT STOPPED (GH #358): the backend left <archive>.part and its
    // resume record. The question says jnext continues it (and names the .part), it is
    // never taken for an archive (no "cannot be used" warning, no MD5 note), and
    // declining leaves both files untouched.
    {
        FakeMirror mirror("partial");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string archive = (dir / "NextPi-1_93D.tar.gz").string();
        const std::string partial(1000, 'p');
        { std::ofstream(sdcard::partial_path(archive), std::ios::binary) << partial; }
        sdcard::ResumeInfo ri;
        ri.url = mirror.url() + "/NextPi-1_93D.tar.gz";
        ri.validator_kind = "etag";
        ri.validator = "\"abc\"";
        ri.total = 2000;
        const bool rec = sdcard::write_resume_info(archive, ri);
        const std::string rec_before = slurp(sdcard::partial_path(archive) + ".resume");
        std::string message;
        int asked = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "", asked, false);
        o.confirm = [&](const std::string& m) { ++asked; message = m; return false; };
        const nextpi::ProvisionResult r = nextpi::provision(o);
        check("PIT-33",
              "a partial download left by the backend (<archive>.part + record) is offered to be continued: "
              "the question says jnext continues it and names the .part, it is never taken for an archive, "
              "and declining leaves both files byte for byte (GH #358; jnext-only, no VHDL counterpart)",
              fixture && rec && r.status == sdcard::ProvisionStatus::Declined && asked == 1 &&
                  message.find("continues") != std::string::npos && message.find(".part") != std::string::npos &&
                  message.find("MD5") == std::string::npos &&
                  message.find("cannot be used") == std::string::npos && mirror.fetched.empty() &&
                  slurp(sdcard::partial_path(archive)) == partial &&
                  slurp(sdcard::partial_path(archive) + ".resume") == rec_before && !fs::exists(archive),
              fmt("fixture=%d rec=%d status=%d asked=%d msg='%.140s'", fixture ? 1 : 0, rec ? 1 : 0,
                  static_cast<int>(r.status), asked, message.c_str()));
    }

    // ── PIT-34 — a download that fails but leaves a resumable partial says so in the error,
    // and the archive's own name stays empty (only the partial exists).
    {
        FakeMirror mirror("partialfail");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        int confirms = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "", confirms);
        sdcard::DownloadFn real = mirror.download();
        o.download = [real](const std::string& u, const std::string& dest, const sdcard::ProgressFn& p,
                            std::string& e) {
            if (u.size() > 7 && u.compare(u.size() - 7, 7, ".tar.gz") == 0) {
                { std::ofstream(sdcard::partial_path(dest), std::ios::binary) << std::string(1000, 'p'); }
                sdcard::ResumeInfo ri;
                ri.url = u;
                ri.validator_kind = "etag";
                ri.validator = "\"abc\"";
                ri.total = 2000;
                sdcard::write_resume_info(dest, ri);
                e = "connection closed after 1000 of 2000 bytes";
                return false;
            }
            return real(u, dest, p, e);
        };
        const nextpi::ProvisionResult r = nextpi::provision(o);
        const std::string archive = (dir / "NextPi-1_93D.tar.gz").string();
        // R7: nothing kept -> no "kept" claim. A seam that fails and leaves nothing.
        const fs::path dir_n = mirror.dir / "np_none";
        nextpi::ProvisionOptions on = nextpi_options(mirror, dir_n.string(), "", confirms);
        on.download = [real](const std::string& u, const std::string& dest, const sdcard::ProgressFn& p,
                             std::string& e) {
            if (u.size() > 7 && u.compare(u.size() - 7, 7, ".tar.gz") == 0) { e = "Could not resolve hostname"; return false; }
            return real(u, dest, p, e);
        };
        const nextpi::ProvisionResult rn = nextpi::provision(on);
        const bool none_ok = rn.status == sdcard::ProvisionStatus::Failed &&
                             rn.error.find("continues next time") == std::string::npos &&
                             rn.error.find("is kept in") == std::string::npos;
        // A seam that leaves a COMPLETE partial and fails (its rename failed).
        const fs::path dir_c = mirror.dir / "np_complete";
        nextpi::ProvisionOptions oc = nextpi_options(mirror, dir_c.string(), "", confirms);
        oc.download = [real, &mirror](const std::string& u, const std::string& dest, const sdcard::ProgressFn& p,
                                      std::string& e) {
            if (u.size() > 7 && u.compare(u.size() - 7, 7, ".tar.gz") == 0) {
                std::error_code ec2;
                fs::copy_file(mirror.dir / "NextPi-1_93D.tar.gz", sdcard::partial_path(dest),
                              fs::copy_options::overwrite_existing, ec2);
                sdcard::ResumeInfo ri;
                ri.url = u; ri.validator_kind = "etag"; ri.validator = "\"abc\"";
                ri.total = fs::file_size(mirror.dir / "NextPi-1_93D.tar.gz", ec2);
                sdcard::write_resume_info(dest, ri);
                e = "cannot move the partial: Permission denied";
                return false;
            }
            return real(u, dest, p, e);
        };
        const nextpi::ProvisionResult rc = nextpi::provision(oc);
        const bool complete_ok = rc.status == sdcard::ProvisionStatus::Failed &&
                                 rc.error.find("the complete download is kept in") != std::string::npos;
        check("PIT-34",
              "a failed download that left a resumable partial says the fetched bytes are kept and the "
              "download continues next time; the archive's own name is not created (GH #358; jnext-only, "
              "no VHDL counterpart)",
              fixture && r.status == sdcard::ProvisionStatus::Failed &&
                  r.error.find("continues next time") != std::string::npos && !fs::exists(archive) &&
                  fs::exists(sdcard::partial_path(archive)) && none_ok && complete_ok,
              fmt("fixture=%d status=%d error='%s' none_ok=%d (%s) complete_ok=%d (%s)", fixture ? 1 : 0,
                  static_cast<int>(r.status), r.error.c_str(), none_ok ? 1 : 0, rn.error.c_str(),
                  complete_ok ? 1 : 0, rc.error.c_str()));
    }

    // ── PIT-35 — JANKO'S MANUAL UNPACK (GH #358): `NextPi-1_93D.tar` and a `NextPi-1_93D/`
    // folder holding the .img are NOT the provisioner's fetch files, so the directory
    // counts as a setup that was tried: the question says what is missing.
    {
        FakeMirror mirror("manual");
        const fs::path dir = mirror.dir / "np";
        std::error_code ec;
        fs::create_directories(dir / "NextPi-1_93D", ec);
        std::ofstream(dir / "NextPi-1_93D.tar") << "tar";
        std::ofstream(dir / "NextPi-1_93D" / "NextPi2-20260924.img") << "img";
        std::string message;
        int asked = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "", asked, false);
        o.confirm = [&](const std::string& m) { ++asked; message = m; return false; };
        const nextpi::ProvisionResult r = nextpi::provision(o);
        check("PIT-35",
              "a directory holding a manual unpack (NextPi-1_93D.tar and a NextPi-1_93D folder with the .img) "
              "is reported as unusable: the question names the missing nextpi.img (GH #358; jnext-only, no "
              "VHDL counterpart)",
              r.status == sdcard::ProvisionStatus::Declined && asked == 1 &&
                  message.find("cannot be used") != std::string::npos &&
                  message.find("nextpi.img") != std::string::npos && message.find("missing") != std::string::npos,
              fmt("status=%d asked=%d msg='%.200s'", static_cast<int>(r.status), asked, message.c_str()));
    }

    // ── PIT-36 — only the provisioner's own fetch files: no "cannot be used"; an interrupted
    // install's leftovers (nextpi.img.part) beside them: "cannot be used".
    {
        FakeMirror mirror("fetchonly");
        const fs::path dir = mirror.dir / "np";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string archive = (dir / "NextPi-1_93D.tar.gz").string();
        std::ofstream(sdcard::partial_path(archive), std::ios::binary) << std::string(10, 'p');
        sdcard::ResumeInfo ri;
        ri.url = mirror.url() + "/NextPi-1_93D.tar.gz";
        ri.validator_kind = "etag"; ri.validator = "\"abc\""; ri.total = 20;
        sdcard::write_resume_info(archive, ri);
        std::ofstream(archive + ".md5") << "x";
        std::ofstream(dir / ".listing.html.part") << "x";
        auto ask = [&](const fs::path& d) {
            std::string message;
            int asked = 0;
            nextpi::ProvisionOptions o = nextpi_options(mirror, d.string(), "", asked, false);
            o.confirm = [&](const std::string& m) { ++asked; message = m; return false; };
            (void)nextpi::provision(o);
            return message;
        };
        const std::string only = ask(dir);
        std::ofstream(dir / "nextpi.img.part") << "x";
        const std::string leftovers = ask(dir);
        check("PIT-36",
              "a directory holding only the provisioner's own fetch files (archive .part + record, .md5, "
              "listing partial) gets no \"cannot be used\"; an interrupted install's nextpi.img.part beside "
              "them does (GH #358; jnext-only, no VHDL counterpart)",
              !only.empty() && only.find("cannot be used") == std::string::npos &&
                  leftovers.find("cannot be used") != std::string::npos,
              fmt("only='%.100s' leftovers='%.100s'", only.c_str(), leftovers.c_str()));
    }

    // ── PIT-37 — an archive that cannot be verified (no .md5 beside it, the mirror gives none) is
    // never installed silently: one question that says jnext cannot check it; declined keeps it.
    {
        FakeMirror mirror("unverifiable");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        const bool placed = place(mirror, dir, "1_93D", false);
        std::remove((mirror.dir / "NextPi-1_93D.tar.gz.md5").string().c_str());   // the mirror has none either
        const std::string before = slurp(dir / "NextPi-1_93D.tar.gz");
        std::string message;
        int asked = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "", asked, false);
        o.confirm = [&](const std::string& m) { ++asked; message = m; return false; };
        const nextpi::ProvisionResult r = nextpi::provision(o);
        check("PIT-37",
              "an archive with no .md5 beside it and none on the mirror is not installed silently: one "
              "question says jnext cannot check it, declining installs nothing and keeps the archive "
              "(GH #358; jnext-only, no VHDL counterpart)",
              fixture && placed && r.status == sdcard::ProvisionStatus::Declined && asked == 1 &&
                  message.find("cannot check") != std::string::npos &&
                  message.find("NextPi-1_93D.tar.gz") != std::string::npos &&
                  slurp(dir / "NextPi-1_93D.tar.gz") == before && !before.empty() &&
                  nextpi::prepared_release(dir.string()).empty(),
              fmt("fixture=%d placed=%d status=%d asked=%d msg='%.140s'", fixture ? 1 : 0, placed ? 1 : 0,
                  static_cast<int>(r.status), asked, message.c_str()));
    }

    // ── PIT-38 — a COMPLETE <archive>.part (its rename failed last time) is finished with no
    // request and then takes the archive check: installed, no question, nothing fetched.
    {
        FakeMirror mirror("completepart");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        std::error_code ec;
        fs::create_directories(dir, ec);
        const std::string archive = (dir / "NextPi-1_93D.tar.gz").string();
        fs::copy_file(mirror.dir / "NextPi-1_93D.tar.gz", sdcard::partial_path(archive), ec);
        fs::copy_file(mirror.dir / "NextPi-1_93D.tar.gz.md5", archive + ".md5", ec);
        sdcard::ResumeInfo ri;
        ri.url = mirror.url() + "/NextPi-1_93D.tar.gz";
        ri.validator_kind = "etag"; ri.validator = "\"abc\"";
        ri.total = fs::file_size(mirror.dir / "NextPi-1_93D.tar.gz", ec);
        const bool rec = sdcard::write_resume_info(archive, ri);
        int confirms = 0;
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        check("PIT-38",
              "a complete <archive>.part with its record is promoted to the archive with no request, then "
              "checked and installed with no question; the partial and record are gone (GH #358; jnext-only, "
              "no VHDL counterpart)",
              fixture && rec && r.status == sdcard::ProvisionStatus::Ok && confirms == 0 &&
                  mirror.fetched.empty() && installed_a(dir) && !fs::exists(sdcard::partial_path(archive)) &&
                  !fs::exists(sdcard::resume_record_path(archive)),
              fmt("fixture=%d rec=%d status=%d (%s) confirms=%d fetched=%zu", fixture ? 1 : 0, rec ? 1 : 0,
                  static_cast<int>(r.status), r.error.c_str(), confirms, mirror.fetched.size()));
    }

    // ── PIT-39 — NO SECOND 5.7 GB HASH OUTSIDE THE BUSY DIALOG: a rejected local archive that the
    // user agrees to replace is hashed once (inside "Checking the NextPi download"), and the
    // fresh download once more (inside it too); nothing is hashed with the GUI frozen.
    {
        FakeMirror mirror("hashes");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        const bool placed = place(mirror, dir, "1_93D", true);
        std::string whole = slurp(dir / "NextPi-1_93D.tar.gz");
        { std::ofstream(dir / "NextPi-1_93D.tar.gz", std::ios::binary | std::ios::trunc) << whole.substr(0, whole.size() / 2); }
        int confirms = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "", confirms);
        uint64_t inside = 0;
        int checking = 0;
        o.busy = [&](const std::string& phase, const std::function<bool()>& work) {
            if (phase != "Checking the NextPi download") return work();
            ++checking;
            const uint64_t before = nextpi::md5_file_calls();
            const bool ok = work();
            inside += nextpi::md5_file_calls() - before;
            return ok;
        };
        const uint64_t calls0 = nextpi::md5_file_calls();
        const nextpi::ProvisionResult r = nextpi::provision(o);
        const uint64_t total_calls = nextpi::md5_file_calls() - calls0;
        check("PIT-39",
              "replacing a rejected local archive hashes it once and the fresh download once, both inside the "
              "\"Checking the NextPi download\" busy dialog: no hash is taken outside it (GH #358; jnext-only, "
              "no VHDL counterpart)",
              fixture && placed && r.status == sdcard::ProvisionStatus::Ok && checking >= 2 && total_calls == inside &&
                  total_calls >= 2 && installed_a(dir),
              fmt("fixture=%d placed=%d status=%d (%s) checking=%d hashes=%llu inside=%llu", fixture ? 1 : 0,
                  placed ? 1 : 0, static_cast<int>(r.status), r.error.c_str(), checking,
                  static_cast<unsigned long long>(total_calls), static_cast<unsigned long long>(inside)));
    }

    // ── PIT-40 — a COMPLETE <archive>.part whose promotion fails (the archive's name is a
    // non-empty directory) stops there: Failed, "the complete download is kept in", nothing asked.
    {
        FakeMirror mirror("promotefail");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        std::error_code ec;
        const std::string archive = (dir / "NextPi-1_93D.tar.gz").string();
        fs::create_directories(archive + "/obstacle", ec);
        fs::copy_file(mirror.dir / "NextPi-1_93D.tar.gz", sdcard::partial_path(archive), ec);
        fs::copy_file(mirror.dir / "NextPi-1_93D.tar.gz.md5", archive + ".md5", ec);
        sdcard::ResumeInfo ri;
        ri.url = mirror.url() + "/NextPi-1_93D.tar.gz";
        ri.validator_kind = "etag"; ri.validator = "\"abc\"";
        ri.total = fs::file_size(mirror.dir / "NextPi-1_93D.tar.gz", ec);
        const bool rec = sdcard::write_resume_info(archive, ri);
        int confirms = 0;
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        check("PIT-40",
              "a complete <archive>.part that cannot be promoted (its name is taken) ends in Failed saying the "
              "complete download is kept, asks nothing and keeps the partial and its record (GH #358; "
              "jnext-only, no VHDL counterpart)",
              fixture && rec && r.status == sdcard::ProvisionStatus::Failed && confirms == 0 &&
                  r.error.find("the complete download is kept in") != std::string::npos &&
                  fs::exists(sdcard::partial_path(archive)) && fs::exists(sdcard::resume_record_path(archive)),
              fmt("fixture=%d rec=%d status=%d confirms=%d error='%.160s'", fixture ? 1 : 0, rec ? 1 : 0,
                  static_cast<int>(r.status), confirms, r.error.c_str()));
    }

    // ── PIT-41 — a GOOD archive beside a WRONG local .md5: the user agrees, the mirror's .md5 matches
    // the digest already computed, and the archive is installed with no archive download.
    {
        FakeMirror mirror("wrongmd5");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        const bool placed = place(mirror, dir, "1_93D", true);
        { std::ofstream(dir / "NextPi-1_93D.tar.gz.md5") << std::string(32, '0') << "\n"; }
        int confirms = 0;
        const nextpi::ProvisionResult r = nextpi::provision(nextpi_options(mirror, dir.string(), "", confirms));
        check("PIT-41",
              "a good archive beside a wrong local .md5, once the user agrees, is installed after fetching only "
              "the mirror's .md5: the digest already computed is reused, the archive is not downloaded again "
              "(GH #358; jnext-only, no VHDL counterpart)",
              fixture && placed && r.status == sdcard::ProvisionStatus::Ok && confirms == 1 &&
                  mirror.fetched == std::vector<std::string>({md5_url}) && installed_a(dir),
              fmt("fixture=%d placed=%d status=%d (%s) confirms=%d fetched=%zu", fixture ? 1 : 0, placed ? 1 : 0,
                  static_cast<int>(r.status), r.error.c_str(), confirms, mirror.fetched.size()));
    }

    // ── PIT-42 — the sibling hash path: an archive that could not be checked (the first .md5 fetch
    // failed), then the user agrees and the second fetch works: it is hashed once, INSIDE the busy dialog.
    {
        FakeMirror mirror("lateMd5");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const fs::path dir = mirror.dir / "np";
        const bool placed = place(mirror, dir, "1_93D", false);
        int confirms = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "", confirms);
        sdcard::DownloadFn real = mirror.download();
        int md5_fetches = 0;
        o.download = [real, &md5_fetches](const std::string& u, const std::string& dest, const sdcard::ProgressFn& p,
                                          std::string& e) {
            if (u.size() > 4 && u.compare(u.size() - 4, 4, ".md5") == 0 && ++md5_fetches == 1) {
                e = "Could not resolve hostname";
                return false;
            }
            return real(u, dest, p, e);
        };
        uint64_t inside = 0;
        int checking = 0;
        o.busy = [&](const std::string& phase, const std::function<bool()>& work) {
            if (phase != "Checking the NextPi download") return work();
            ++checking;
            const uint64_t before = nextpi::md5_file_calls();
            const bool ok = work();
            inside += nextpi::md5_file_calls() - before;
            return ok;
        };
        const uint64_t calls0 = nextpi::md5_file_calls();
        const nextpi::ProvisionResult r = nextpi::provision(o);
        const uint64_t total_calls = nextpi::md5_file_calls() - calls0;
        check("PIT-42",
              "an archive that could not be checked, once the user agrees and the .md5 arrives, is hashed "
              "inside the \"Checking the NextPi download\" busy dialog and installed without a download "
              "(GH #358; jnext-only, no VHDL counterpart)",
              fixture && placed && r.status == sdcard::ProvisionStatus::Ok && md5_fetches == 2 && checking >= 1 &&
                  total_calls >= 1 && total_calls == inside && installed_a(dir),
              fmt("fixture=%d placed=%d status=%d (%s) md5_fetches=%d checking=%d hashes=%llu inside=%llu",
                  fixture ? 1 : 0, placed ? 1 : 0, static_cast<int>(r.status), r.error.c_str(), md5_fetches, checking,
                  static_cast<unsigned long long>(total_calls), static_cast<unsigned long long>(inside)));
    }

    // ── PIT-43 — JANKO'S UNPLUGGED CABLE (GH #358 point 2), through the real backend: the archive
    // transfer goes silent halfway (a loopback server that stops sending and holds the connection).
    // The attempt must end with the stall reason instead of hanging, keep the half it has, and the
    // next provision must continue from there. Only the archive goes to the network backend (with
    // short timings); the .md5 still comes from the fake mirror.
    {
        FakeMirror mirror("stalled");
        const bool fixture = mirror.add_release("1_93D", disk_a);
        const std::string archive_bytes = slurp(mirror.dir / "NextPi-1_93D.tar.gz");
        const std::size_t half = archive_bytes.size() / 2;
        rs::Server srv;
        const fs::path dir = mirror.dir / "np";
        int confirms = 0;
        nextpi::ProvisionOptions o = nextpi_options(mirror, dir.string(), "", confirms);
        o.mirror = srv.url("");
        sdcard::DownloadFn fake = mirror.download();
        sdcard::DownloadTimings timings{200, 2000};
        int archive_fetches = 0;
        o.download = [&](const std::string& u, const std::string& dest, const sdcard::ProgressFn& p, std::string& e) {
            if (u.size() > 7 && u.compare(u.size() - 7, 7, ".tar.gz") == 0) {
                ++archive_fetches;
                return sdcard::http_download(u, dest, p, e, timings);
            }
            return fake(u, dest, p, e);
        };
        rs::Opts so;
        so.body = archive_bytes;
        so.etag = "\"np1\"";
        so.stall_after = half;
        so.stall_ms = 8000;
        srv.set(so);
        const nextpi::ProvisionResult r1 = nextpi::provision(o);
        const std::string archive = (dir / "NextPi-1_93D.tar.gz").string();
        const std::string archive_url = srv.url("") + "/NextPi-1_93D.tar.gz";
        uint64_t have = 0, total = 0;
        const sdcard::PartialState state = sdcard::partial_state(archive, archive_url, have, total);
        const bool stopped = r1.status == sdcard::ProvisionStatus::Failed &&
                             r1.error.find("NextPi download failed: stalled: no data received for 2 s") != std::string::npos &&
                             r1.error.find("kept and the download continues next time") != std::string::npos &&
                             state == sdcard::PartialState::Resumable && have == half && total == archive_bytes.size();

        // The network is back (the same server, healthy). The abandoned connection may still hold the
        // single-threaded server for a few seconds (WinHTTP under wine), so give this run a long timeout.
        rs::Opts healthy;
        healthy.body = archive_bytes;
        healthy.etag = "\"np1\"";
        srv.set(healthy);
        srv.clear_requests();
        timings = sdcard::DownloadTimings{};
        const nextpi::ProvisionResult r2 = nextpi::provision(o);
        const auto reqs = srv.requests();
        const bool resumed = r2.status == sdcard::ProvisionStatus::Ok && reqs.size() == 1 &&
                             reqs[0].range == "bytes=" + std::to_string(half) + "-" && reqs[0].if_range == "\"np1\"" &&
                             archive_fetches == 2 && installed_a(dir);
        check("PIT-43",
              "a NextPi archive transfer that goes silent halfway ends with 'NextPi download failed: stalled: no "
              "data received for 2 s' and that the half fetched is kept and the download continues next time "
              "(a resumable .part of exactly that half); the next provision asks for the rest with Range and "
              "If-Range and installs (GH #358; jnext-only, no VHDL counterpart)",
              fixture && srv.ok() && stopped && resumed,
              fmt("fixture=%d server=%d stopped=%d (%s; state=%d have=%llu of %zu) resumed=%d (status=%d %s reqs=%zu "
                  "range='%s' fetches=%d)",
                  fixture ? 1 : 0, srv.ok() ? 1 : 0, stopped ? 1 : 0, r1.error.substr(0, 120).c_str(),
                  static_cast<int>(state), static_cast<unsigned long long>(have), half, resumed ? 1 : 0,
                  static_cast<int>(r2.status), r2.error.substr(0, 80).c_str(), reqs.size(),
                  reqs.empty() ? "" : reqs[0].range.c_str(), archive_fetches));
    }
}

int main(int argc, char** argv) {
    if (argc >= 2 && std::strcmp(argv[1], "--holder") == 0) return holder_main(argc, argv);
    const std::string self = fs::absolute(argv[0]).string();

    // ── PIT-01 — the archive digest, on every OS. Literal RFC 1321 §A.5 values
    // (cross-checked with coreutils md5sum), because the fake mirrors of the PI
    // rows compute their .md5 with md5_file itself and so cannot tell a
    // consistent-but-wrong digest from the right one. 1 048 577 bytes crosses the
    // 1 MiB read buffer.
    {
        const std::string tmp = jtp::make_temp_dir("jnext-pit-md5-");
        auto digest_of = [&](const std::string& content) {
            const std::string path = tmp + "/f.bin";
            std::ofstream(path, std::ios::binary) << content;
            return nextpi::md5_file(path);
        };
        const std::string empty = digest_of("");
        const std::string abc   = digest_of("abc");
        const std::string msg   = digest_of("message digest");
        const std::string big   = digest_of(std::string(1048577, 'a'));
        const std::string none  = nextpi::md5_file(tmp + "/does-not-exist");
        std::error_code ec;
        fs::remove_all(tmp, ec);
        check("PIT-01",
              "nextpi::md5_file returns the RFC 1321 digest of a file (empty, short, and one "
              "crossing the 1 MiB read buffer) and \"\" for a file that does not exist",
              empty == "d41d8cd98f00b204e9800998ecf8427e" && abc == "900150983cd24fb0d6963f7d28e17f72" &&
                  msg == "f96b697d7cb7938d525a2f31aaf161d0" && big == "6f0555ac53cecbf068d354c08863805a" &&
                  none.empty(),
              fmt("empty=%s abc=%s msg=%s big=%s missing='%s'", empty.c_str(), abc.c_str(), msg.c_str(),
                  big.c_str(), none.c_str()));
    }

    // ── PIT-02 — the QEMU command line for the socket transport: the console
    // UART on a chardev QEMU LISTENS on, on the loopback address only (host= is
    // not optional, QEMU's default is 0.0.0.0) and without waiting for a client
    // (wait=off; a listener otherwise blocks QEMU's startup, char-socket.c:1512),
    // and no pipe chardev. Everything else is the PI-06 command line.
    {
        PiQemu::Spec spec;
        spec.dir = "n";
        const std::vector<std::string> a = PiQemu::build_args(spec, std::uint16_t{40123});
        auto has = [&](const std::string& flag, const std::string& value) {
            for (std::size_t i = 0; i + 1 < a.size(); ++i)
                if (a[i] == flag && a[i + 1] == value) return true;
            return false;
        };
        bool no_pipe = true;
        for (const std::string& s : a) no_pipe = no_pipe && s.find("pipe,") == std::string::npos;
        const fs::path d("n");
#ifdef _WIN32
        const char* monitor = "stdio";
#else
        const char* monitor = "none";
#endif
        const bool shape =
            has("-M", "raspi0") && has("-kernel", (d / "boot" / "kernel.img").string()) &&
            has("-dtb", (d / "boot" / "bcm2708-rpi-zero.dtb").string()) &&
            has("-drive", "file=" + (d / "overlay.qcow2").string() + ",if=sd,format=qcow2") &&
            has("-chardev", "socket,id=pi,host=127.0.0.1,port=40123,server=on,wait=off,nodelay=on") &&
            has("-serial", "chardev:pi") && has("-monitor", monitor) && has("-display", "none") &&
            has("-device", "usb-audio,audiodev=snd0,buffer=16384");
        check("PIT-02",
              "the socket transport's QEMU command line puts the console UART on a loopback-only, "
              "non-waiting listening socket chardev (host=127.0.0.1, server=on, wait=off) with no "
              "pipe chardev, the monitor on stdio on Windows and off elsewhere, and the rest of "
              "the raspi0 command line unchanged",
              shape && no_pipe, fmt("shape=%d no_pipe=%d", shape ? 1 : 0, no_pipe ? 1 : 0));
    }

    // ── PIT-03 — the host back-end per OS ("host" is the platform's own): qemu(1)
    // lists dsound as the Windows one, coreaudio on macOS, pa elsewhere.
    {
        const std::string dflt = PiQemu::audiodev_arg("host");
#ifdef _WIN32
        const bool ok = dflt == "dsound,id=snd0";
#elif defined(__APPLE__)
        const bool ok = dflt.rfind("coreaudio,id=snd0", 0) == 0;
#else
        const bool ok = dflt == "pa,id=snd0";
#endif
        check("PIT-03",
              "the host back-end per OS: dsound on Windows, coreaudio on macOS, pa elsewhere",
              ok, "default=" + dflt);
    }

    // ── PIT-04 — THE TCP ENDPOINT carries both directions through UART 1, like
    // PI-01 does for the FIFO pair. NR 0xA0 = 0x30 connects UART 1 to the Pi pins
    // (zxnext.vhd:2278-2281). The host's bytes are read by the guest at 0x143B, the
    // guest's reach the host, UART 0 hears none of it.
    {
        Listener lis;
        const bool listening = lis.open();
        Emulator emu;
        emu.init(tcp_config(lis.port()));
        const PiUartDevice* pi = emu.pi_uart();
        const std::string want_desc = "TCP 127.0.0.1:" + std::to_string(lis.port());

        std::vector<uint8_t> heard;
        esp::net::NativeSocket peer = esp::net::kInvalidSocket;
        const auto t0 = std::chrono::steady_clock::now();
        while (ms_since(t0) < 8000 && heard.size() < kSup.size()) {
            if (peer == esp::net::kInvalidSocket) {
                peer = lis.accept();
                if (peer != esp::net::kInvalidSocket) peer_send(peer, "SUP> ");
            }
            const std::vector<uint8_t> got = pi_frame(emu);
            heard.insert(heard.end(), got.begin(), got.end());
            sleep_ms(5);
        }
        std::string to_host;
        if (peer != esp::net::kInvalidSocket) {
            pi_transmit_frame(emu, 0x0D);
            const auto t1 = std::chrono::steady_clock::now();
            while (ms_since(t1) < 5000 && to_host.empty()) {
                pi_frame(emu);
                to_host += peer_recv(peer);
                sleep_ms(5);
            }
            esp::net::close(peer);
        }
        const bool uart0_quiet = emu.uart().channel(0).rx_empty();
        check("PIT-04",
              "the TCP endpoint carries both directions over UART 1 while NR 0xA0 = 0x30 connects it "
              "to the Pi GPIO pins (zxnext.vhd:2278-2281): the host's bytes reach port 0x143B, the "
              "guest's reach the host, UART 0 hears nothing, and the link is described as "
              "TCP 127.0.0.1:<port>",
              listening && pi && pi->link().describe() == want_desc && heard == kSup && to_host == "\r" &&
                  uart0_quiet && pi->link().faults() == 0,
              fmt("listening=%d pi=%d describe='%s' (want '%s') heard [%s] (want 53 55 50 3E 20) host got "
                  "'%s' (want CR) uart0_quiet=%d",
                  listening ? 1 : 0, pi ? 1 : 0, pi ? pi->link().describe().c_str() : "",
                  want_desc.c_str(), bytes_hex(heard).c_str(), to_host.c_str(), uart0_quiet ? 1 : 0));
    }

    // ── PIT-05 — A CABLE WITH NOTHING AT THE FAR END IS NOT A FAULT (the
    // JoyUartLink contract: "nothing plugged in" is the normal state before QEMU is
    // up). For over two seconds there is no listener: nothing is heard, nothing is
    // faulted, nothing is thrown away (the guest's byte stays queued, as the link
    // keeps it while the peer is absent). When the listener appears, the link
    // connects by itself and both directions flow, the early byte arriving first.
    {
        std::string perr;
        const std::uint16_t port = PiQemu::free_loopback_port(perr);
        Emulator emu;
        emu.init(tcp_config(port));
        const PiUartDevice* pi = emu.pi_uart();
        pi_transmit_frame(emu, 'E');
        std::vector<uint8_t> early;
        const auto t0 = std::chrono::steady_clock::now();
        while (ms_since(t0) < 2200) {
            const std::vector<uint8_t> got = pi_frame(emu);
            early.insert(early.end(), got.begin(), got.end());
            sleep_ms(5);
        }
        const bool quiet = early.empty() && pi && pi->link().faults() == 0 && pi->link().unsent() == 0;

        Listener lis;
        const bool listening = port != 0 && lis.open(port);
        std::vector<uint8_t> heard;
        std::string to_host;
        esp::net::NativeSocket peer = esp::net::kInvalidSocket;
        const auto t1 = std::chrono::steady_clock::now();
        while (listening && ms_since(t1) < 8000 && (heard.size() < kSup.size() || to_host.empty())) {
            if (peer == esp::net::kInvalidSocket) {
                peer = lis.accept();
                if (peer != esp::net::kInvalidSocket) peer_send(peer, "SUP> ");
            } else {
                to_host += peer_recv(peer);
            }
            const std::vector<uint8_t> got = pi_frame(emu);
            heard.insert(heard.end(), got.begin(), got.end());
            sleep_ms(5);
        }
        if (peer != esp::net::kInvalidSocket) esp::net::close(peer);
        check("PIT-05",
              "a TCP cable with no listener is not a fault: for over two seconds nothing is heard, "
              "nothing is faulted or discarded; once a listener appears the link connects by itself, "
              "both directions flow and the byte sent in the meantime arrives",
              port != 0 && quiet && listening && heard == kSup && to_host == "E" && pi->link().faults() == 0,
              fmt("port=%u quiet=%d (faults=%zu unsent=%zu early=%zu) listening=%d heard [%s] host got '%s' "
                  "(want E) faults=%zu",
                  port, quiet ? 1 : 0, pi ? pi->link().faults() : 0, pi ? pi->link().unsent() : 0,
                  early.size(), listening ? 1 : 0, bytes_hex(heard).c_str(), to_host.c_str(),
                  pi ? pi->link().faults() : 0));
    }

    // ── PIT-06 — A PEER THAT GOES AWAY (JoyUartLink::flush_tx contract; QEMU's
    // listener re-arms after a disconnect, char-socket.c:467-470). The conversation
    // with it is discarded, not handed to whoever connects next: a byte the dead
    // peer had sent and a byte the link could not deliver are gone (the second
    // counted in unsent()), nothing is a fault, and a new peer on the same listener
    // gets traffic again and receives only what was sent after the loss. The first
    // peer is closed with unread data, which makes the kernel reset the connection,
    // so the failing send is deterministic. A second loss, an orderly close seen by
    // a read, is reconnected too.
    {
        Listener lis;
        const bool listening = lis.open();
        std::string err;
        std::unique_ptr<JoyUartEndpoint> ep =
            JoyUartEndpoint::open_tcp_client("127.0.0.1", lis.port(), err);
        JoyUartLink link(std::move(ep), 0);
        auto accept_while_polling = [&](esp::net::NativeSocket& out) {
            const auto t0 = std::chrono::steady_clock::now();
            while (ms_since(t0) < 8000 && out == esp::net::kInvalidSocket) {
                link.poll();
                out = lis.accept();
                sleep_ms(5);
            }
            return out != esp::net::kInvalidSocket;
        };
        esp::net::NativeSocket p1 = esp::net::kInvalidSocket, p2 = esp::net::kInvalidSocket,
                               p3 = esp::net::kInvalidSocket;
        const bool got1 = listening && accept_while_polling(p1);
        bool rx_queued = false;
        if (got1) {
            peer_send(p1, "R");
            const auto t0 = std::chrono::steady_clock::now();
            while (ms_since(t0) < 3000 && !link.rx_pending()) { link.poll(); sleep_ms(5); }
            rx_queued = link.rx_pending();
            link.send_to_host('W');            // unread by p1, so closing p1 resets the connection
            sleep_ms(100);
            esp::net::close(p1);
            sleep_ms(200);
            link.send_to_host('Y');            // fails: the peer is gone
            link.send_to_host('V');            // a second byte right behind it: the dead socket must be gone
        }
        const bool discarded = got1 && link.unsent() == 1 && !link.rx_pending() && link.faults() == 0;

        const bool got2 = got1 && accept_while_polling(p2);
        std::string at_p2;
        if (got2) {
            link.send_to_host('Z');
            const auto t0 = std::chrono::steady_clock::now();
            while (ms_since(t0) < 3000 && at_p2.empty()) { link.poll(); at_p2 += peer_recv(p2); sleep_ms(5); }
            sleep_ms(200);
            link.poll();
            at_p2 += peer_recv(p2);
        }
        // An orderly close, seen by a read with nothing queued to send.
        bool eof_discarded = false, got3 = false;
        esp::net::NativeSocket p4 = esp::net::kInvalidSocket;
        bool reset_leg = false;
        if (got2) {
            peer_send(p2, "Q");
            const auto t0 = std::chrono::steady_clock::now();
            while (ms_since(t0) < 3000 && link.received() < 2) { link.poll(); sleep_ms(5); }
            esp::net::close(p2);
            for (int i = 0; i < 100 && link.rx_pending(); ++i) { link.poll(); sleep_ms(10); }
            eof_discarded = link.received() == 2 && !link.rx_pending();
            got3 = accept_while_polling(p3);
        }
        // A reset (peer closed with unread data) seen by a READ, with nothing
        // queued to send, is a lost peer and not a fault; the link reconnects.
        if (got3) {
            link.send_to_host('U');
            sleep_ms(100);
            esp::net::close(p3);
            p3 = esp::net::kInvalidSocket;
            sleep_ms(200);
            const bool got4 = accept_while_polling(p4);
            reset_leg = got4 && link.faults() == 0 && !link.rx_pending();
        }
        for (esp::net::NativeSocket s : {p1, p2, p3, p4})
            if (s != esp::net::kInvalidSocket) esp::net::close(s);
        check("PIT-06",
              "when a TCP peer goes away its conversation is discarded, not handed to the next one: "
              "the byte it had sent and the byte that could not be delivered are dropped (the latter "
              "counted in unsent()) without a fault, and a new peer on the same listener gets traffic "
              "again and receives only what was sent after the loss, including a second byte sent "
              "straight after it; an orderly close and a reset seen by a read are reconnected too "
              "without a fault",
              listening && got1 && rx_queued && discarded && got2 && at_p2 == "VZ" && eof_discarded && got3 &&
                  reset_leg && link.faults() == 0,
              fmt("listening=%d accepted=%d rx_queued=%d discarded=%d (unsent=%zu rx_pending=%d faults=%zu) "
                  "second peer=%d got '%s' (want VZ) eof_discarded=%d third peer=%d reset_leg=%d",
                  listening ? 1 : 0, got1 ? 1 : 0, rx_queued ? 1 : 0, discarded ? 1 : 0, link.unsent(),
                  link.rx_pending() ? 1 : 0, link.faults(), got2 ? 1 : 0, at_p2.c_str(),
                  eof_discarded ? 1 : 0, got3 ? 1 : 0, reset_leg ? 1 : 0));
    }

    // ── PIT-07 — link_config is the one place a launcher's transport becomes
    // Emulator configuration (main() calls it): the socket transport yields the
    // port and no FIFO paths, the FIFO transport (POSIX) the two paths and no
    // port. The warm-start recording machine opens neither, so a second reader
    // cannot steal the Pi's bytes (PI-16 for the new field), and the machine itself
    // is kept.
    {
        Fixture fx("linkcfg");
        PiQemu::Spec spec;
        spec.dir         = fx.dir();
        spec.qemu_binary = fx.bin("qemu-system-arm");
        spec.audio       = "none";
        spec.transport   = PiQemu::Transport::Socket;
        PiQemu sock;
        std::string error;
        const bool started = fx.ok() && sock.start(spec, error);
        EmulatorConfig cfg;
        cfg.type              = MachineType::ZXN_ISSUE2;
        cfg.sd_card_image     = "card.img";
        cfg.pi_uart_fifo_rx   = "stale.out";      // link_config must clear what it does not use
        cfg.pi_uart_fifo_tx   = "stale.in";
        sock.link_config(cfg);
        const bool socket_ok = started && sock.tcp_port() != 0 && cfg.pi_uart_tcp_port == sock.tcp_port() &&
                               cfg.pi_uart_fifo_rx.empty() && cfg.pi_uart_fifo_tx.empty();
        const EmulatorConfig warm = Emulator::warm_start_boot_config(cfg);
        const bool warm_ok = warm.pi_uart_tcp_port == 0 && warm.pi_uart_fifo_rx.empty() &&
                             warm.pi_uart_fifo_tx.empty() && warm.sd_card_image == "card.img";
        bool fifo_ok = true;
        std::string fifo_error;
#ifndef _WIN32
        Fixture fx2("linkcfg-fifo");
        PiQemu::Spec fspec = spec;
        fspec.dir         = fx2.dir();
        fspec.qemu_binary = fx2.bin("qemu-system-arm");
        fspec.transport   = PiQemu::Transport::Fifo;
        PiQemu fifo;
        EmulatorConfig fcfg;
        fcfg.pi_uart_tcp_port = 1234;             // stale; the FIFO transport must clear it
        fifo.start(fspec, fifo_error);
        fifo.link_config(fcfg);
        fifo_ok = fifo.running() && !fcfg.pi_uart_fifo_rx.empty() && fcfg.pi_uart_fifo_rx == fifo.rx_path() &&
                  fcfg.pi_uart_fifo_tx == fifo.tx_path() && fcfg.pi_uart_tcp_port == 0;
        // With both set (a hand-built config), the FIFO pair wins.
        if (fifo_ok) {
            EmulatorConfig both = fcfg;
            both.type                 = MachineType::ZXN_ISSUE2;
            both.rewind_buffer_frames = 0;
            both.pi_uart_tcp_port     = 1234;
            Emulator emu;
            emu.init(both);
            fifo_ok = emu.pi_uart() && emu.pi_uart()->link().describe().rfind("FIFO pair ", 0) == 0;
        }
#endif
        check("PIT-07",
              "link_config turns the launcher's transport into Emulator configuration (the port for the "
              "socket transport, the two FIFO paths for the FIFO one, never both: stale values of the "
              "other are cleared, and with both set the FIFO pair wins) and the warm-start "
              "recording machine gets neither while the machine itself is kept",
              socket_ok && warm_ok && fifo_ok,
              fmt("started=%d (%s) socket_ok=%d warm_ok=%d fifo_ok=%d (%s)", started ? 1 : 0, error.c_str(),
                  socket_ok ? 1 : 0, warm_ok ? 1 : 0, fifo_ok ? 1 : 0, fifo_error.c_str()));
    }

    // ── PIT-08 — THE FEATURE, end to end with the stand-in over the socket
    // transport. The NextPi directory's name has a SPACE and a COMMA, and QEMU is
    // given as a bare name found on PATH (with .exe on Windows). jnext creates the
    // overlay through the stand-in qemu-img and starts "QEMU", whose recorded
    // arguments are exactly build_args(spec, port) (so every path survived the
    // command-line quoting); an Emulator built through link_config hears the Pi's
    // `SUP> `, the Pi receives the guest's CR; and destroying the launcher stops the
    // stand-in the ORDERLY way (SIGTERM on POSIX, `quit` on the monitor on Windows),
    // not by force, in well under the 3 s grace.
    {
        Fixture fx("e2e", "nextpi dir,1");
        EnvVar path("PATH", prepend_path(fx.bin_dir()));
        PiQemu::Spec spec;
        spec.dir         = fx.dir();
        spec.qemu_binary = "qemu-system-arm";
        spec.audio       = "none";
        spec.transport   = PiQemu::Transport::Socket;
        auto qemu = std::make_unique<PiQemu>();
        std::string error;
        const bool started = fx.ok() && qemu->start(spec, error);
        const bool overlay = fs::exists(fs::path(fx.dir()) / "overlay.qcow2");
        const unsigned long pid = started ? fx.child_pid() : 0;
        const std::vector<std::string> want_args = PiQemu::build_args(spec, qemu->tcp_port());

        std::vector<uint8_t> heard;
        bool sent = false;
        if (started) {
            Emulator emu;
            emu.init(launcher_config(*qemu));
            pump(emu, heard, 10000, [&] { return heard.size() >= kSup.size(); });
            pi_transmit_frame(emu, 0x0D);
            sent = pump_until_received(emu, fx, "\r", 10000);
        }
        const bool args_ok = fx.arg_lines() == want_args;

        const auto t0 = std::chrono::steady_clock::now();
        qemu.reset();
        const long stop_ms = ms_since(t0);
        const bool graceful = fx.file("graceful") == "1\n";
        const bool gone     = pid != 0 && nxs::gone_within(pid, 2000);
        check("PIT-08",
              "NextPi end to end over the socket transport: with a space and a comma in the directory "
              "name and QEMU found on PATH, jnext creates the overlay and starts QEMU with exactly the "
              "documented arguments, the guest reads the Pi's SUP> on UART 1 and the Pi receives the "
              "guest's byte; destroying the launcher stops QEMU the orderly way, not by force",
              started && overlay && args_ok && heard == kSup && sent && graceful && gone && stop_ms < 2000,
              fmt("started=%d (%s) overlay=%d args_ok=%d heard [%s] pi received=%d graceful=%d gone=%d "
                  "stop %ld ms (want < 2000)",
                  started ? 1 : 0, error.c_str(), overlay ? 1 : 0, args_ok ? 1 : 0, bytes_hex(heard).c_str(),
                  sent ? 1 : 0, graceful ? 1 : 0, gone ? 1 : 0, stop_ms));
    }

    // ── PIT-09 — A HARD RESET DOES NOT REBOOT THE PI (PI-09 for the socket
    // transport). A second Emulator built from the same link_config (what a cold
    // boot does) connects to the SAME listening stand-in, which re-listened after
    // the first one left and never noticed a reboot.
    {
        Fixture fx("coldboot");
        PiQemu::Spec spec;
        spec.dir         = fx.dir();
        spec.qemu_binary = fx.bin("qemu-system-arm");
        spec.audio       = "none";
        spec.transport   = PiQemu::Transport::Socket;
        PiQemu qemu;
        std::string error;
        const bool started = fx.ok() && qemu.start(spec, error);
        const unsigned long pid = started ? fx.child_pid() : 0;
        bool first = false, second = false;
        if (started) {
            {
                Emulator emu;
                emu.init(launcher_config(qemu));
                pi_transmit_frame(emu, 'A');
                first = pump_until_received(emu, fx, "A", 10000);
            }
            Emulator emu;
            emu.init(launcher_config(qemu));
            pi_transmit_frame(emu, 'B');
            second = pump_until_received(emu, fx, "AB", 10000);
        }
        check("PIT-09",
              "a rebuilt Emulator (a hard reset) reconnects to the same running Pi over the socket; "
              "the Pi received both machines' bytes in order",
              started && first && second && pid != 0 && qemu.pid() > 0 && nxs::process_alive(pid),
              fmt("started=%d (%s) first=%d second=%d received='%s' (want AB) alive=%d", started ? 1 : 0,
                  error.c_str(), first ? 1 : 0, second ? 1 : 0, fx.file("received").c_str(),
                  pid != 0 && nxs::process_alive(pid) ? 1 : 0));
    }

    // ── PIT-10 — the refusals (PI-07 for the portable launcher), each leaving
    // nothing running: QEMU not on PATH, a DIRECTORY named like the QEMU binary
    // on PATH (not an executable), and a QEMU that dies at once.
    {
        Fixture fx("refuse");
        const fs::path decoy = fs::path(fx.dir()).parent_path() / "decoy";
        std::error_code ec;
        fs::create_directories(decoy / (std::string("qemu-system-arm") + kExe), ec);
        std::string err_none, err_dir, err_dies;
        PiQemu::Spec spec;
        spec.dir       = fx.dir();
        spec.audio     = "none";
        spec.transport = PiQemu::Transport::Socket;
        bool none_refused = false, dir_refused = false;
        {
            EnvVar path("PATH", (fs::path(fx.dir()).parent_path() / "empty").string());
            PiQemu a;
            none_refused = !a.start(spec, err_none) && err_none.find("qemu-system-arm not found") == 0 &&
                           !a.running();
        }
        {
            EnvVar path("PATH", decoy.string());
            PiQemu b;
            dir_refused = !b.start(spec, err_dir) && err_dir.find("qemu-system-arm not found") == 0 &&
                          !b.running();
        }
        bool dies_refused = false;
        {
            EnvVar mode("JNEXT_NEXTPI_STUB_MODE", "fails");
            PiQemu::Spec s = spec;
            s.qemu_binary  = fx.bin("qemu-system-arm");
            PiQemu c;
            dies_refused = !c.start(s, err_dies) && err_dies.find("exited at once") != std::string::npos &&
                           !c.running();
        }
        // A QEMU given as a path without its extension (Windows: Preferences > QEMU
        // = C:\\...\\qemu-system-arm) is completed with .exe; on POSIX it is the file itself.
        bool noext_ok = false;
        {
            std::string full = fx.bin("qemu-system-arm");
            if (std::strlen(kExe) && full.size() > std::strlen(kExe)) full.resize(full.size() - std::strlen(kExe));
            PiQemu::Spec s = spec;
            s.qemu_binary  = full;
            PiQemu d;
            std::string err_noext;
            noext_ok = d.start(s, err_noext) && d.running();
        }
        check("PIT-10",
              "starting NextPi is refused, leaving nothing running, when QEMU is not on PATH, when the "
              "only match on PATH is a directory, and when QEMU exits at once; a QEMU path given without "
              "its .exe is completed",
              fx.ok() && none_refused && dir_refused && dies_refused && noext_ok,
              fmt("fixture=%d none='%s' dir='%s' dies='%s' no_extension=%d", fx.ok() ? 1 : 0, err_none.c_str(),
                  err_dir.c_str(), err_dies.c_str(), noext_ok ? 1 : 0));
    }

    // ── PIT-11 — FORCE AFTER THE GRACE PERIOD (PI-17 for the portable launcher): a
    // QEMU that ignores the orderly stop is killed once the grace is over and stop()
    // returns. The row bounds its own waits, so a launcher that never forces fails
    // it instead of hanging the suite (the stand-in also ends itself after two
    // minutes).
    {
        Fixture fx("stubborn");
        EnvVar mode("JNEXT_NEXTPI_STUB_MODE", "stubborn");
        PiQemu::Spec spec;
        spec.dir           = fx.dir();
        spec.qemu_binary   = fx.bin("qemu-system-arm");
        spec.audio         = "none";
        spec.transport     = PiQemu::Transport::Socket;
        spec.stop_grace_ms = 300;
        auto qemu = std::make_unique<PiQemu>();
        std::string error;
        const bool started = fx.ok() && qemu->start(spec, error);
        const unsigned long pid = started ? fx.child_pid() : 0;
        const auto t0 = std::chrono::steady_clock::now();
        std::atomic<bool> stopped_flag{false};
        std::thread stopper([&] { qemu.reset(); stopped_flag = true; });
        const bool returned_in_time = [&] {
            while (ms_since(t0) < 8000) {
                if (stopped_flag) return true;
                sleep_ms(10);
            }
            return false;
        }();
        const long ms = ms_since(t0);
        if (!returned_in_time && pid != 0) nxs::kill_hard(pid);   // so the join below ends
        if (stopper.joinable()) stopper.join();
        const bool killed = pid != 0 && nxs::gone_within(pid, 1500);
        if (!killed && pid != 0) nxs::kill_hard(pid);    // never leave the stand-in behind
        check("PIT-11",
              "a QEMU that ignores the orderly stop is killed once the stop grace period is over, and "
              "stop() returns",
              started && returned_in_time && killed && ms >= 300 && ms < 300 + 1500,
              fmt("started=%d (%s) returned=%d killed=%d stop took %ld ms (want 300..1800)", started ? 1 : 0,
                  error.c_str(), returned_in_time ? 1 : 0, killed ? 1 : 0, ms));
    }

    // ── PIT-12 — QEMU'S OWN EXIT STATUS reaches jnext (PI-34 for the portable
    // launcher): a QEMU that ends on its own with status 7 leaves exit_status()
    // decoding to 7 (the wait status on POSIX, the exit code on Windows).
    {
        Fixture fx("exit7");
        EnvVar mode("JNEXT_NEXTPI_STUB_MODE", "exit7");
        PiQemu::Spec spec;
        spec.dir       = fx.dir();
        spec.qemu_binary = fx.bin("qemu-system-arm");
        spec.audio     = "none";
        spec.transport = PiQemu::Transport::Socket;
        PiQemu qemu;
        std::string error;
        const bool started = fx.ok() && qemu.start(spec, error);
        const auto t0 = std::chrono::steady_clock::now();
        while (started && qemu.exit_status() == -1 && ms_since(t0) < 8000) sleep_ms(20);
        const int status = qemu.exit_status();
#ifdef _WIN32
        const int code = status;
        const bool exited_normally = status != -1;
#else
        const int code = WEXITSTATUS(status);
        const bool exited_normally = status != -1 && WIFEXITED(status);
#endif
        check("PIT-12",
              "the exit status of a QEMU that ends on its own reaches jnext: exit_status() decodes to "
              "the status QEMU exited with",
              started && exited_normally && code == 7,
              fmt("started=%d (%s) raw status=%d decoded=%d (want 7)", started ? 1 : 0, error.c_str(), status,
                  code));
    }

    // ── PIT-13 — A KILLED jnext DOES NOT ORPHAN QEMU (PI-20 for the portable
    // launcher). A second process holds a running launcher and is killed with no
    // chance to run a destructor; QEMU, which would otherwise keep the overlay
    // locked, is stopped by the watchdog (POSIX) or by the job object's
    // kill-on-close (Windows) within seconds.
    {
        Fixture fx("orphan");
        nxs::Child holder;
        std::string line;
        const bool held = fx.ok() && holder.start(self, {"--holder", fx.dir(), fx.bin("qemu-system-arm")},
                                                  line, 20000) && line == "ready";
        const unsigned long qemu_pid = held ? fx.child_pid() : 0;
        const bool running = qemu_pid != 0 && nxs::process_alive(qemu_pid);
        holder.kill();
        const bool stopped = qemu_pid != 0 && nxs::gone_within(qemu_pid, 3000);
        if (!stopped && qemu_pid != 0) nxs::kill_hard(qemu_pid);
        check("PIT-13",
              "when the process running NextPi is killed outright, QEMU is stopped with it instead of "
              "being left orphaned with the overlay locked",
              held && running && stopped,
              fmt("holder said '%s' qemu pid=%lu running while held=%d stopped after the kill=%d", line.c_str(),
                  qemu_pid, running ? 1 : 0, stopped ? 1 : 0));
    }

    // ── PIT-14 — QEMU INHERITS NONE OF jnext's HANDLES (PI-19 for the portable
    // launcher). An inheritable pipe holding the magic string JNEXTMAG is open in
    // this process; its number is handed to the stand-in, which tries to read it.
    // Asking whether the number is a valid handle would be fooled by the child's own
    // handles reusing small values, so the child must READ the magic.
    {
        Fixture fx("handles");
        nxs::MagicPipe pipe;
        EnvVar probe("JNEXT_NEXTPI_STUB_PROBE", std::to_string(pipe.number()));
        PiQemu::Spec spec;
        spec.dir         = fx.dir();
        spec.qemu_binary = fx.bin("qemu-system-arm");
        spec.audio       = "none";
        spec.transport   = PiQemu::Transport::Socket;
        PiQemu qemu;
        std::string error;
        const bool started = fx.ok() && pipe.ok() && qemu.start(spec, error) && fx.child_pid() != 0;
        const std::string result = fx.file("probe");
        check("PIT-14",
              "QEMU inherits none of jnext's other handles: an inheritable pipe holding a magic string "
              "that jnext has open cannot be read in the child",
              started && result == "magic=0\n",
              fmt("started=%d (%s) pipe=%ld probe file '%s' (want magic=0)", started ? 1 : 0, error.c_str(),
                  pipe.number(), result.c_str()));
    }

    // ── PIT-15 — THE CHILD'S LOCALE (PI-18 for the portable launcher). Whatever
    // jnext's locale is, QEMU runs with LANG=C and LC_ALL=C each exactly once, and
    // jnext's own environment is left alone. On Windows variable names are
    // case-insensitive, so the parent's are spelt `Lang` / `Lc_All` and must go too.
    {
        Fixture fx("locale");
#ifdef _WIN32
        const char* lang_name = "Lang";
        const char* lc_name   = "Lc_All";
        for (const char* n : {"LANG", "LC_ALL"}) jtp::unset_env(n);
#else
        const char* lang_name = "LANG";
        const char* lc_name   = "LC_ALL";
#endif
        EnvVar lang(lang_name, "xx_XX.UTF-8");
        EnvVar lc(lc_name, "yy_YY.UTF-8");
        PiQemu::Spec spec;
        spec.dir         = fx.dir();
        spec.qemu_binary = fx.bin("qemu-system-arm");
        spec.audio       = "none";
        spec.transport   = PiQemu::Transport::Socket;
        PiQemu qemu;
        std::string error;
        const bool started = fx.ok() && qemu.start(spec, error) && fx.child_pid() != 0;
        const bool parent_kept = std::getenv(lang_name) && std::string(std::getenv(lang_name)) == "xx_XX.UTF-8" &&
                                 std::getenv(lc_name) && std::string(std::getenv(lc_name)) == "yy_YY.UTF-8";
        std::multiset<std::string> seen;
        {
            const std::string all = fx.file("env");
            std::string cur;
            for (char c : all) {
                if (c == '\n') { if (!cur.empty()) seen.insert(cur); cur.clear(); }
                else if (c != '\r') cur.push_back(c);
            }
        }
        const bool env_c = seen == std::multiset<std::string>({"LANG=C", "LC_ALL=C"});
        check("PIT-15",
              "QEMU runs with LANG=C and LC_ALL=C, each exactly once whatever the case of the parent's "
              "variable names, and jnext's own environment is left alone",
              started && env_c && parent_kept,
              fmt("started=%d (%s) env_c=%d (%zu entries) parent_kept=%d", started ? 1 : 0, error.c_str(),
                  env_c ? 1 : 0, seen.size(), parent_kept ? 1 : 0));
    }

    // ── PIT-16 — WHAT AN EMPTY `audio` MEANS (owner decision 2026-10-12, completed
    // by GH #331). Empty means the Next's mixer on every OS, and QEMU gets the
    // `wav` back-end; `host` is the host output (dsound on Windows, coreaudio on
    // macOS, pa elsewhere) and never the mixer. Pure.
    {
        auto audiodev = [](const std::vector<std::string>& a) {
            for (std::size_t i = 0; i + 1 < a.size(); ++i)
                if (a[i] == "-audiodev") return a[i + 1];
            return std::string();
        };
        PiQemu::Spec spec;
        spec.dir = "n";
        const std::string empty = audiodev(PiQemu::build_args(spec, std::uint16_t{40123}, "n/uart"));
        const bool empty_mixer  = PiQemu::uses_mixer(spec);
        spec.audio = "host";
        const std::string host  = audiodev(PiQemu::build_args(spec, std::uint16_t{40123}, "n/uart"));
        const bool host_mixer   = PiQemu::uses_mixer(spec);
        const bool empty_ok = empty == PiQemu::mixer_audiodev_arg("n/uart") && empty_mixer;
#ifdef _WIN32
        const bool host_ok  = host == "dsound,id=snd0";
#elif defined(__APPLE__)
        const bool host_ok  = host.rfind("coreaudio,id=snd0,", 0) == 0;
#else
        const bool host_ok  = host == "pa,id=snd0";
#endif
        check("PIT-16",
              "an empty Pi audio setting is the Next's mixer (QEMU's wav back-end into the audio "
              "FIFO, or the named pipe on Windows) on every OS; `host` is the host output "
              "(dsound, coreaudio, pa) and never the mixer (jnext-only, no VHDL counterpart)",
              empty_ok && host_ok && !host_mixer,
              fmt("empty='%s' mixer=%d host='%s' host_mixer=%d", empty.c_str(), empty_mixer ? 1 : 0,
                  host.c_str(), host_mixer ? 1 : 0));
    }

    // ── PIT-17 — the same through a real start() on the socket transport: the
    // stand-in gets the mixer's -audiodev, PiQemu makes the audio reader, and
    // link_config hands it to the Emulator. On POSIX this is the socket transport
    // WITH the mixer: the audio FIFO needs a runtime directory of its own, so the
    // wav path must be absolute, inside a fresh `jnext-pi-*` directory that
    // exists while QEMU runs and is gone once PiQemu is destroyed, and nothing
    // may appear in the current directory. On Windows the path is the named pipe
    // `\\.\pipe\jnext-pi-<this pid>-<n>.audio`, which must exist when "QEMU"
    // starts (the stand-in records `opened` or `failed <errno>` for its fopen,
    // as QEMU's wav back-end does) and is gone once stop() has returned.
    {
        Fixture fx("emptyaudio");
        PiQemu::Spec spec;
        spec.dir         = fx.dir();
        spec.qemu_binary = fx.bin("qemu-system-arm");
        spec.transport   = PiQemu::Transport::Socket;     // spec.audio left empty
        std::string error, dev, wav_path;
        bool started = false, per_os = false, cwd_clean = true, dir_ok = true, gone = true;
        const fs::path cwd_fifo = fs::current_path() / ".audio";
        {
            PiQemu qemu;
            started = fx.ok() && qemu.start(spec, error) && fx.child_pid() != 0;
            for (const std::string& l : fx.arg_lines())
                if (l.rfind("wav,id=snd0,path=", 0) == 0 || l.rfind("dsound,id=snd0", 0) == 0) dev = l;
            EmulatorConfig cfg;
            qemu.link_config(cfg);
#ifdef _WIN32
            per_os = dev.rfind("wav,id=snd0,path=", 0) == 0 && qemu.audio() != nullptr &&
                     cfg.pi_audio == qemu.audio();
            if (per_os) {
                bool named = false;
                for (unsigned n = 0; n < 4096 && !named; ++n)
                    named = dev == PiQemu::mixer_audiodev_arg(PiQemu::audio_pipe_base(nxs::self_pid(), n));
                wav_path = dev.substr(std::strlen("wav,id=snd0,path="));
                wav_path = wav_path.substr(0, wav_path.find(".audio,out.") + 6);
                dir_ok = named && fx.file("audio") == "opened\n";
                qemu.stop();     // the pipe must be gone once stop() returns
                if (std::FILE* leaked = std::fopen(wav_path.c_str(), "wb")) {
                    std::fclose(leaked);
                    gone = false;
                }
                gone = gone && !wav_path.empty();
            }
#else
            per_os = dev.rfind("wav,id=snd0,path=", 0) == 0 &&
                     dev.find(".audio,out.frequency=44100,out.channels=2,out.format=s16") != std::string::npos &&
                     qemu.audio() != nullptr && cfg.pi_audio == qemu.audio();
            if (per_os) {
                wav_path = dev.substr(std::strlen("wav,id=snd0,path="));
                wav_path = wav_path.substr(0, wav_path.find(".audio,out.") + 6);
                const fs::path p(wav_path);
                dir_ok = p.is_absolute() && p.parent_path().filename().string().rfind("jnext-pi-", 0) == 0 &&
                         fs::is_directory(p.parent_path()) && fs::exists(p);
            }
            std::error_code ec;
            cwd_clean = !fs::exists(cwd_fifo, ec);
#endif
        }
#ifndef _WIN32
        std::error_code ec2;
        gone = !wav_path.empty() && !fs::exists(fs::path(wav_path).parent_path(), ec2) &&
               !fs::exists(cwd_fifo, ec2);
        cwd_clean = cwd_clean && !fs::exists(cwd_fifo, ec2);
#endif
        check("PIT-17",
              "starting NextPi with the audio setting empty gives QEMU the mixer's wav FIFO and makes "
              "the audio reader (also on the socket transport, in a runtime directory of its own "
              "that stop() removes, never in the current directory); on Windows the FIFO is a named "
              "pipe that exists when QEMU starts and is gone after stop(); link_config carries the "
              "reader (jnext-only, no VHDL counterpart)",
              started && per_os && dir_ok && cwd_clean && gone,
              fmt("started=%d (%s) audiodev='%s' dir_ok=%d cwd_clean=%d gone=%d", started ? 1 : 0,
                  error.c_str(), dev.c_str(), dir_ok ? 1 : 0, cwd_clean ? 1 : 0, gone ? 1 : 0));
    }

    // ── PIT-24 — THE GRACE IS A TIME, NOT A COUNT OF SLEEPS (GH #355): on a host
    // that stretches short sleeps (macOS timer coalescing) the grace must still end
    // on the clock. The launcher's poll sleeps are replaced by one that takes 50 ms
    // per 10 ms asked, five times over; a grace counted in sleeps (30 of them) then
    // runs ~1500 ms, a grace measured on steady_clock ends at ~300 ms.
    {
        Fixture fx("stubborn");
        EnvVar mode("JNEXT_NEXTPI_STUB_MODE", "stubborn");
        PiQemu::Spec spec;
        spec.dir           = fx.dir();
        spec.qemu_binary   = fx.bin("qemu-system-arm");
        spec.audio         = "none";
        spec.transport     = PiQemu::Transport::Socket;
        spec.stop_grace_ms = 300;
        spec.poll_sleep    = stretched_sleep;
        auto qemu = std::make_unique<PiQemu>();
        std::string error;
        const bool started = fx.ok() && qemu->start(spec, error);
        const unsigned long pid = started ? fx.child_pid() : 0;
        const int sleeps0 = g_stretched_sleeps;
        const auto t0 = std::chrono::steady_clock::now();
        std::atomic<bool> stopped_flag{false};
        std::thread stopper([&] { qemu.reset(); stopped_flag = true; });
        const bool returned_in_time = [&] {
            while (ms_since(t0) < 8000) {
                if (stopped_flag) return true;
                sleep_ms(10);
            }
            return false;
        }();
        const long ms = ms_since(t0);
        if (!returned_in_time && pid != 0) nxs::kill_hard(pid);
        if (stopper.joinable()) stopper.join();
        const int sleeps = g_stretched_sleeps - sleeps0;
        const bool killed = pid != 0 && nxs::gone_within(pid, 1500);
        if (!killed && pid != 0) nxs::kill_hard(pid);
        check("PIT-24",
              "the stop grace period is measured on the clock, not counted in sleeps: with every launcher "
              "poll sleep stretched five-fold (and seen to be, during stop()), a stubborn QEMU is still "
              "killed at the grace, not after it (jnext-only, no VHDL counterpart)",
              started && returned_in_time && killed && sleeps >= 1 && ms >= 300 && ms < 600,
              fmt("started=%d (%s) returned=%d killed=%d stretched sleeps in stop()=%d (want >=1) stop took "
                  "%ld ms (want 300..600)", started ? 1 : 0, error.c_str(), returned_in_time ? 1 : 0,
                  killed ? 1 : 0, sleeps, ms));
    }

    // ── PIT-25 — THE STARTUP WINDOW IS A TIME TOO (GH #355): start() watches for a
    // QEMU that dies at once for 300 ms (POSIX) / 1 s (Windows), in 50 ms polls. With
    // every sleep stretched five-fold, a window counted in polls would take 1.5 s /
    // 5 s; measured on the clock it takes about 0.5 s / 1.25 s (it ends at the first
    // poll past the deadline). The healthy stand-in runs the whole window.
    {
        Fixture fx("startwin");
        PiQemu::Spec spec;
        spec.dir         = fx.dir();
        spec.qemu_binary = fx.bin("qemu-system-arm");
        spec.audio       = "none";
        spec.transport   = PiQemu::Transport::Socket;
        spec.poll_sleep  = stretched_sleep;
#ifdef _WIN32
        const long min_ms = 1000, max_ms = 2500;
        // The 1000 ms window is exactly four stretched 250 ms polls, so a fifth
        // (a poll that returns a little short on the clock) is not a fault: one
        // honest wine run under load made 5 (1522 ms). Unverified guess: a sleep
        // can return slightly short under MinGW/wine. The doubled window (8) and
        // the counted loop (20) are still far outside.
        const int  max_sleeps = 5;
#else
        const long min_ms = 300, max_ms = 1000;
        const int  max_sleeps = 2;   // polls at 0, 250 ms stretched
#endif
        std::string error;
        const int sleeps0 = g_stretched_sleeps;
        const auto t0     = std::chrono::steady_clock::now();
        auto qemu         = std::make_unique<PiQemu>();
        const bool started = fx.ok() && qemu->start(spec, error);
        const long ms      = ms_since(t0);
        const int sleeps   = g_stretched_sleeps - sleeps0;
        qemu.reset();
        check("PIT-25",
              "the startup window for a QEMU that dies at once is measured on the clock, not counted in "
              "polls: with every launcher poll sleep stretched five-fold (and seen to be, during start()), "
              "start() of a healthy QEMU returns in about a window, not five times that (jnext-only, no "
              "VHDL counterpart)",
              started && sleeps >= 1 && sleeps <= max_sleeps && ms >= min_ms && ms < max_ms,
              fmt("started=%d (%s) stretched sleeps in start()=%d (want 1..%d) start took %ld ms (want %ld..%ld)",
                  started ? 1 : 0, error.c_str(), sleeps, max_sleeps, ms, min_ms, max_ms));
    }

    // ── PIT-18 — THE FEATURE ON EVERY OS: the Pi's sound reaches the Next's mixer
    // end to end (PI-48 for the socket transport; on Windows the named pipe,
    // written by another process's fopen). The stand-in opens the audio path as
    // QEMU's wav back-end does and writes a tone. NR 0xA2 gates it exactly as on
    // the board (zxnext.vhd:2358-2359): 0x00 silent, 0xC0 both channels. The
    // levels are exact: left +-16384 is to_i2s 768/256, +-256 from the 0x200 rest,
    // which the mixer outputs as +1024/-1024 (MX-06/07's x4); right +-8192 gives
    // +512/-512 (i2s.vhd:177-180).
    {
        Fixture fx("tone");
        write_tone((fs::path(fx.bin_dir()) / "tone.wav").string(), 11025, 16384, 50, 8192);   // 250 ms
        PiQemu::Spec spec;
        spec.dir         = fx.dir();
        spec.qemu_binary = fx.bin("qemu-system-arm");
        spec.transport   = PiQemu::Transport::Socket;
        PiQemu qemu;
        std::string error;
        const bool started = fx.ok() && qemu.start(spec, error) && qemu.audio() != nullptr;
        bool arrived = false;
        for (int i = 0; i < 500 && started && !arrived; ++i) {
            arrived = qemu.audio()->available() >= 11025;
            if (!arrived) sleep_ms(10);
        }
        const std::vector<std::string> args = fx.arg_lines();
        const std::string audio_rec = fx.file("audio");
        bool argv_ok = false;
        for (const std::string& a : args) argv_ok = argv_ok || a.rfind("wav,id=snd0,path=", 0) == 0;
        std::pair<int, int> closed{-1, -1};
        MixerLevels open;
        if (started && arrived) {
            Emulator emu;
            emu.init(launcher_config(qemu));
            closed = mixer_swing(emu, 0x00, 3);
            open   = mixer_levels(emu, 0xC0, 5);
        }
        check("PIT-18",
              "NextPi's sound reaches the Next's mixer over I2S on every OS: QEMU's wav back-end writes "
              "it to the FIFO (a named pipe on Windows) PiQemu serves, and NR 0xA2 gates it (0x00 "
              "silent, 0xC0 the square wave, left +-1024 and right +-512) (i2s.vhd:177-180, "
              "zxnext.vhd:2358-2359)",
              started && arrived && argv_ok && audio_rec == "opened\n" && closed.first <= 16 &&
                  closed.second <= 16 && open.n > 0 && open.lo_l == -1024 && open.hi_l == 1024 &&
                  open.lo_r == -512 && open.hi_r == 512,
              fmt("started=%d (%s) arrived=%d argv=%d audio='%s' closed L=%d R=%d (want <=16) open n=%d "
                  "L %d..%d R %d..%d", started ? 1 : 0, error.c_str(), arrived ? 1 : 0, argv_ok ? 1 : 0,
                  audio_rec.c_str(), closed.first, closed.second, open.n, open.lo_l, open.hi_l, open.lo_r,
                  open.hi_r));
    }

    // ── PIT-23 — AN AUDIO PIPE THAT CANNOT BE MADE FAILS THE START (PI-51 for the
    // portable launcher): no QEMU is spawned and the error says which pipe.
    // Windows: the row holds the one-instance servers on every name the launcher
    // could pick, so its own CreateNamedPipe is refused. POSIX: the runtime
    // directory sits so deep in $TMPDIR that "<base>.audio" is one byte past
    // PATH_MAX, as PI-51 builds it.
    {
        Fixture fx("noaudio");
        PiQemu::Spec spec;
        spec.dir         = fx.dir();
        spec.qemu_binary = fx.bin("qemu-system-arm");
        spec.transport   = PiQemu::Transport::Socket;
        bool refused = false, setup = true;
        std::string error, want_text;
#ifdef _WIN32
        std::vector<std::string> names;
        for (unsigned n = 0; n < 256; ++n) names.push_back(PiQemu::audio_pipe_base(nxs::self_pid(), n) + ".audio");
        nxs::PipeSquat squat(names);
        setup = squat.held() == names.size();
        want_text = "jnext-pi-" + std::to_string(nxs::self_pid()) + "-";
        {
            PiQemu q;
            refused = !q.start(spec, error) && !q.running() && q.audio() == nullptr;
        }
#else
        std::error_code ec;
        const fs::path deep_root = fs::canonical(fs::temp_directory_path(), ec) /
                                   ("jnext-pit23-" + std::to_string(nxs::self_pid()));
        // strlen(<tmp>/jnext-pi-XXXXXX/uart) + ".audio" == PATH_MAX + 1.
        const std::size_t want = static_cast<std::size_t>(PATH_MAX) + 1 - std::strlen(".audio") -
                                 std::strlen("/jnext-pi-XXXXXX/uart");
        std::string deep = deep_root.string();
        while (deep.size() + 1 < want) {               // each step adds "/" + a name
            const std::size_t rest = want - deep.size() - 1;
            const std::size_t take = rest <= 200 ? rest : (rest == 201 ? 199 : 200);
            deep += "/" + std::string(take, 'd');
        }
        fs::create_directories(deep, ec);
        setup = !ec && deep.size() == want;
        want_text = "uart.audio";
        {
            EnvVar tmp("TMPDIR", deep);
            PiQemu q;
            refused = !q.start(spec, error) && !q.running() && q.audio() == nullptr;
        }
        fs::remove_all(deep_root, ec);
#endif
        const bool spawned = fs::exists(fs::path(fx.bin_dir()) / "pid");
        check("PIT-23",
              "when the Pi's audio pipe cannot be made, starting NextPi fails with that error and "
              "starts no QEMU (PI-51 for the portable launcher; jnext-only, no VHDL counterpart)",
              fx.ok() && setup && refused && !spawned && error.find(want_text) != std::string::npos,
              fmt("setup=%d refused=%d spawned=%d error='%s' (want '%s')", setup ? 1 : 0, refused ? 1 : 0,
                  spawned ? 1 : 0, error.c_str(), want_text.c_str()));
    }

    // ── PIT-26 — THE PROGRAMS ARE LOOKED FOR BEFORE ANYTHING IS DOWNLOADED (GH #357):
    // PiQemu::find_programs is what main.cpp asks first and what start() resolves
    // through. Missing QEMU is refused naming qemu-system-arm; QEMU without qemu-img
    // is refused naming qemu-img, but only while the directory has no overlay.qcow2
    // (start() creates the overlay with it, once); with both, both are the stand-ins'.
    // A QEMU given by path needs no PATH. The rules are start()'s, moved (PIT-10).
    {
        Fixture fx("programs");
        const fs::path root = fs::path(fx.dir()).parent_path();
        std::error_code ec;
        const fs::path qonly = root / "qonly";
        fs::create_directories(qonly, ec);
        fs::copy_file(JNEXT_NEXTPI_STUB, qonly / (std::string("qemu-system-arm") + kExe),
                      fs::copy_options::overwrite_existing, ec);
        const bool setup = fx.ok() && !ec;
        PiQemu::Spec spec;
        spec.dir = fx.dir();
        std::string q, qi, e_none, e_img, e_over, e_both, e_path;
        bool none_refused = false, img_refused = false, over_ok = false, both_ok = false, path_ok = false;
        {
            EnvVar path("PATH", (root / "empty").string());
            none_refused = !PiQemu::find_programs(spec, q, qi, e_none) &&
                           e_none.find("qemu-system-arm not found") == 0;
        }
        {
            EnvVar path("PATH", qonly.string());
            img_refused = !PiQemu::find_programs(spec, q, qi, e_img) && e_img.find("qemu-img not found") == 0;
            std::ofstream(fs::path(fx.dir()) / "overlay.qcow2").put('\0');
            over_ok = PiQemu::find_programs(spec, q, qi, e_over) && qi.empty() && !q.empty() &&
                      fs::equivalent(q, qonly / (std::string("qemu-system-arm") + kExe), ec);
            fs::remove(fs::path(fx.dir()) / "overlay.qcow2", ec);
        }
        {
            EnvVar path("PATH", fx.bin_dir());
            both_ok = PiQemu::find_programs(spec, q, qi, e_both) && !q.empty() && !qi.empty() &&
                      fs::equivalent(q, fx.bin("qemu-system-arm"), ec) &&
                      fs::equivalent(qi, fx.bin("qemu-img"), ec);
        }
        {
            EnvVar path("PATH", (root / "empty").string());
            PiQemu::Spec by_path = spec;
            by_path.qemu_binary  = fx.bin("qemu-system-arm");
            path_ok = PiQemu::find_programs(by_path, q, qi, e_path) &&
                      fs::equivalent(q, fx.bin("qemu-system-arm"), ec) &&
                      fs::equivalent(qi, fx.bin("qemu-img"), ec);
        }
        check("PIT-26",
              "PiQemu::find_programs, asked before NextPi is offered or downloaded, refuses a missing "
              "QEMU naming qemu-system-arm, refuses QEMU without qemu-img naming qemu-img only while "
              "the directory has no overlay, and otherwise returns the programs start() will run, a "
              "QEMU given by path included (GH #357; jnext-only, no VHDL counterpart)",
              setup && none_refused && img_refused && over_ok && both_ok && path_ok,
              fmt("setup=%d none='%s' img='%s' overlay=%d both=%d by_path=%d", setup ? 1 : 0, e_none.c_str(),
                  e_img.c_str(), over_ok ? 1 : 0, both_ok ? 1 : 0, path_ok ? 1 : 0));
    }

    test_nextpi_provisioner();
    test_nextpi_local_archive_rows();
    test_nextpi_review_rows();
    test_nextpi_review2_rows();
    test_pi_audio_pipe();

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
