// NextPi under QEMU on every OS (GH #316): the Windows launch and the socket
// transport, tested natively on Linux and macOS and under wine for Windows.
//
// `# os: all` in test/unit-tests.conf. The FIFO transport and its shell-script
// QEMU stand-in stay in uart_posix_test (PI-*, `# os: posix`); this suite is the
// part of the feature that exists on Windows. It runs the SOCKET transport, which
// POSIX can select too (PiQemu::Spec::transport), so Linux and macOS exercise the
// same code path the Windows build ships.
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
#include "core/pi_qemu.h"
#include "esp01/esp_socket.h"
#include "esp01/esp_socket_platform.h"
#include "peripheral/joy_uart_link.h"
#include "peripheral/pi_uart_device.h"

#include "nextpi_support.h"

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
#include <memory>
#include <set>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifndef _WIN32
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

    // ── PIT-03 — the audio default is the host's own back-end: qemu(1) lists
    // dsound as the Windows one, coreaudio on macOS, pa elsewhere.
    {
        const std::string dflt = PiQemu::audiodev_arg("");
#ifdef _WIN32
        const bool ok = dflt == "dsound,id=snd0";
#elif defined(__APPLE__)
        const bool ok = dflt.rfind("coreaudio,id=snd0", 0) == 0;
#else
        const bool ok = dflt == "pa,id=snd0";
#endif
        check("PIT-03",
              "the default Pi audio back-end is the host's own: dsound on Windows, coreaudio on "
              "macOS, pa elsewhere",
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

    // ── PIT-16 — WHAT AN EMPTY `audio` MEANS, PER OS (owner decision 2026-10-12).
    // Where the Pi's sound can reach the Next's mixer (the FIFO path: Linux,
    // macOS) empty means the mixer, and QEMU gets the `wav` back-end; on Windows,
    // which has no such path until GH #331, empty means the host output
    // (dsound). `host` is the host output on every OS and never the mixer. Pure.
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
#ifdef _WIN32
        const bool empty_ok = empty == "dsound,id=snd0" && !empty_mixer;
        const bool host_ok  = host == "dsound,id=snd0";
#else
        const bool empty_ok = empty == PiQemu::mixer_audiodev_arg("n/uart") && empty_mixer;
#ifdef __APPLE__
        const bool host_ok  = host.rfind("coreaudio,id=snd0,", 0) == 0;
#else
        const bool host_ok  = host == "pa,id=snd0";
#endif
#endif
        check("PIT-16",
              "an empty Pi audio setting is the Next's mixer (the wav FIFO) on Linux and macOS and the "
              "host output (dsound) on Windows, where there is no mixer path yet; `host` is the host "
              "output on every OS (jnext-only, no VHDL counterpart)",
              empty_ok && host_ok && !host_mixer,
              fmt("empty='%s' mixer=%d host='%s' host_mixer=%d", empty.c_str(), empty_mixer ? 1 : 0,
                  host.c_str(), host_mixer ? 1 : 0));
    }

    // ── PIT-17 — the same through a real start() on the socket transport: the
    // stand-in gets the per-OS -audiodev, PiQemu makes the audio reader exactly
    // where the mixer path exists, and link_config hands that reader (or null)
    // to the Emulator. On POSIX this is the socket transport WITH the mixer: the
    // audio FIFO needs a runtime directory of its own, so the wav path must be
    // absolute, inside a fresh `jnext-pi-*` directory that exists while QEMU
    // runs and is gone once PiQemu is destroyed, and nothing may appear in the
    // current directory.
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
            per_os = dev == "dsound,id=snd0" && qemu.audio() == nullptr && cfg.pi_audio == nullptr;
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
              "the audio reader on Linux and macOS (also on the socket transport, in a runtime "
              "directory of its own that stop() removes, never in the current directory), and gives "
              "QEMU dsound with no reader on Windows; link_config carries the reader (jnext-only, no "
              "VHDL counterpart)",
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
        spec.poll_sleep    = [](int ms) { sleep_ms(ms * 5); };
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
        if (!returned_in_time && pid != 0) nxs::kill_hard(pid);
        if (stopper.joinable()) stopper.join();
        const bool killed = pid != 0 && nxs::gone_within(pid, 1500);
        if (!killed && pid != 0) nxs::kill_hard(pid);
        check("PIT-24",
              "the stop grace period is measured on the clock, not counted in sleeps: with every launcher "
              "poll sleep stretched five-fold, a stubborn QEMU is still killed at the grace, not after it "
              "(jnext-only, no VHDL counterpart)",
              started && returned_in_time && killed && ms >= 300 && ms < 900,
              fmt("started=%d (%s) returned=%d killed=%d stop took %ld ms (want 300..900)", started ? 1 : 0,
                  error.c_str(), returned_in_time ? 1 : 0, killed ? 1 : 0, ms));
    }

    std::printf("\n====================================\n");
    std::printf("Total: %4d  Passed: %4d  Failed: %4d  Skipped: %4d\n", g_total, g_pass, g_fail, 0);
    return g_fail ? 1 : 0;
}
