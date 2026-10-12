// Stand-in `qemu-system-arm` and `qemu-img` for nextpi_test and the
// nextpi-link-func regression row (GH #316).
//
// A real program rather than a shell script, because the Windows launcher
// starts QEMU with CreateProcess, which runs an .exe and nothing else (#319
// fact 6), so the same stub has to exist on every OS. The suite copies it as
// `qemu-system-arm[.exe]` and `qemu-img[.exe]` into a fixture bin/ directory;
// it acts as qemu-img when its own file name says so (the ffmpeg_stub idiom).
//
// qemu-img   `create ... <overlay> <size>`: creates an empty <overlay>, exit 0.
//
// qemu-system-arm  records, in the directory it was started from (the records
// are what the rows assert about the child jnext made):
//   args      one QEMU argument per line, argv[0] excluded
//   env       the environment entries named LANG or LC_ALL, in any letter case
//   probe     `magic=1` when the pipe whose number is in JNEXT_NEXTPI_STUB_PROBE
//             could be read in this process and held JNEXTMAG, else `magic=0`
//   pid       its pid, written LAST: its presence means the others are complete
//             and the console chardev is already listening
//   received  every byte the Next sent to the Pi, appended as it arrives
//   graceful  written when it was asked to stop the orderly way
// and then plays the Pi on the console chardev QEMU was given, as QEMU does:
//   socket,host=H,port=P,server=on,...  listens, sends `SUP> ` to each client
//                                       that connects, and accepts again after
//                                       one disconnects (char-socket.c:467-470)
//   pipe,path=B  (POSIX)                opens B.in / B.out read-write, so neither
//                                       side ever sees EOF, and sends `SUP> `
// The orderly stop is SIGTERM on POSIX and a `quit` line on stdin when
// `-monitor stdio` was given (Windows).
//
// JNEXT_NEXTPI_STUB_MODE: (unset) normal; `fails` exit 3 at once; `exit7` exit 7
// after one second; `stubborn` ignores the orderly stop. Every mode is bounded:
// whatever happens, the process ends by itself after two minutes.

#include "esp01/esp_socket.h"
#include "esp01/esp_socket_platform.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <cwchar>
#else
#include <csignal>
#include <fcntl.h>
#include <unistd.h>
extern char** environ;
#endif

namespace {

std::atomic<bool> g_stop{false};
std::string       g_dir;
bool              g_stubborn = false;

std::string join(const std::string& dir, const char* name) {
#ifdef _WIN32
    return dir + "\\" + name;
#else
    return dir + "/" + name;
#endif
}

std::string dir_of(const char* argv0) {
    const std::string p = argv0 ? argv0 : "";
    const std::size_t at = p.find_last_of("/\\");
    return at == std::string::npos ? "." : p.substr(0, at);
}

std::string base_name_lower(const char* argv0) {
    std::string p = argv0 ? argv0 : "";
    const std::size_t at = p.find_last_of("/\\");
    if (at != std::string::npos) p = p.substr(at + 1);
    for (char& c : p) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (p.size() > 4 && p.compare(p.size() - 4, 4, ".exe") == 0) p.resize(p.size() - 4);
    return p;
}

void write_text(const char* name, const std::string& text, const char* mode = "wb") {
    if (std::FILE* f = std::fopen(join(g_dir, name).c_str(), mode)) {
        std::fwrite(text.data(), 1, text.size(), f);
        std::fclose(f);
    }
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool ieq(const std::string& a, const char* b) {
    if (a.size() != std::strlen(b)) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

void record_environment() {
    std::string out;
#ifdef _WIN32
    if (wchar_t* block = GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p; p += wcslen(p) + 1) {
            std::string var;
            for (const wchar_t* c = p; *c; ++c) var.push_back(static_cast<char>(*c));
            const std::size_t eq = var.find('=', var.empty() ? 0 : 1);
            const std::string name = var.substr(0, eq);
            if (ieq(name, "LANG") || ieq(name, "LC_ALL")) out += var + "\n";
        }
        FreeEnvironmentStringsW(block);
    }
#else
    for (char** e = environ; e && *e; ++e) {
        const std::string var = *e;
        const std::string name = var.substr(0, var.find('='));
        if (ieq(name, "LANG") || ieq(name, "LC_ALL")) out += var + "\n";
    }
#endif
    write_text("env", out);
}

/// Can the pipe numbered in JNEXT_NEXTPI_STUB_PROBE be read here, and does it
/// hold JNEXTMAG? Done first thing, before this process opens anything that
/// could reuse the number.
void probe_inherited_pipe() {
    const char* num = std::getenv("JNEXT_NEXTPI_STUB_PROBE");
    if (!num || !*num) return;
    const long n = std::strtol(num, nullptr, 10);
    char buf[8] = {};
    bool magic = false;
#ifdef _WIN32
    const HANDLE h = reinterpret_cast<HANDLE>(static_cast<intptr_t>(n));
    DWORD avail = 0, got = 0;
    if (PeekNamedPipe(h, nullptr, 0, nullptr, &avail, nullptr) && avail >= 8 &&
        ReadFile(h, buf, 8, &got, nullptr) && got == 8)
        magic = std::memcmp(buf, "JNEXTMAG", 8) == 0;
#else
    const ssize_t got = ::read(static_cast<int>(n), buf, 8);
    magic = got == 8 && std::memcmp(buf, "JNEXTMAG", 8) == 0;
#endif
    write_text("probe", magic ? "magic=1\n" : "magic=0\n");
}

void write_pid() {
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = static_cast<unsigned long>(::getpid());
#endif
    write_text("pid", std::to_string(pid) + "\n");
}

void orderly_stop() {
    if (g_stubborn) return;
    write_text("graceful", "1\n");
    g_stop = true;
}

#ifndef _WIN32
void on_term(int) {
    // Async-signal-safe only: the marker is written by the main loop.
    g_stop = true;
}
#endif

void watch_stdin() {
    // `-monitor stdio`: HMP commands arrive as lines on stdin.
    std::string line;
    int c;
    while ((c = std::fgetc(stdin)) != EOF) {
        if (c == '\n') {
            if (line == "quit") orderly_stop();
            line.clear();
        } else if (c != '\r') {
            line.push_back(static_cast<char>(c));
        }
    }
}

void append_received(const std::uint8_t* data, std::size_t n) {
    if (std::FILE* f = std::fopen(join(g_dir, "received").c_str(), "ab")) {
        std::fwrite(data, 1, n, f);
        std::fclose(f);
    }
}

// -chardev socket,...,port=P,server=on: listen on 127.0.0.1:P and play the Pi.
int serve_socket(std::uint16_t port) {
    std::string err, warn;
    if (!esp::net::init(err)) return 4;
    std::uint16_t bound = 0;
    const esp::net::NativeSocket listener =
        esp::net::open_listener(esp::ipv4(127, 0, 0, 1), port, bound, warn, err);
    if (listener == esp::net::kInvalidSocket) return 5;   // e.g. the port was taken
    write_pid();

    esp::net::NativeSocket client = esp::net::kInvalidSocket;
    while (!g_stop) {
        if (client == esp::net::kInvalidSocket) {
            esp::IpAddress peer;
            std::uint16_t peer_port = 0;
            bool failed = false;
            client = esp::net::accept_nonblocking(listener, peer, peer_port, failed, err);
            if (client != esp::net::kInvalidSocket) {
                bool send_failed = false;
                esp::net::send(client, reinterpret_cast<const std::uint8_t*>("SUP> "), 5,
                               send_failed, err);
            }
        } else {
            std::uint8_t buf[256];
            bool eof = false, failed = false, reset = false;
            const std::size_t n = esp::net::recv(client, buf, sizeof(buf), true, eof, failed, reset, err);
            if (n > 0) append_received(buf, n);
            else if (eof || failed) {      // the client went away: listen for the next
                esp::net::close(client);
                client = esp::net::kInvalidSocket;
            }
        }
        sleep_ms(2);
    }
    return 0;
}

#ifndef _WIN32
// -chardev pipe,path=B: QEMU opens B.in and B.out read-write.
int serve_pipe(const std::string& base) {
    const int in  = ::open((base + ".in").c_str(), O_RDWR | O_NONBLOCK);
    const int out = ::open((base + ".out").c_str(), O_RDWR | O_NONBLOCK);
    if (in < 0 || out < 0) return 6;
    write_pid();
    if (::write(out, "SUP> ", 5) != 5) return 6;
    while (!g_stop) {
        std::uint8_t buf[256];
        const ssize_t n = ::read(in, buf, sizeof(buf));
        if (n > 0) append_received(buf, static_cast<std::size_t>(n));
        sleep_ms(2);
    }
    return 0;
}
#endif

int qemu_main(int argc, char** argv) {
    probe_inherited_pipe();
    record_environment();
    std::string args;
    for (int i = 1; i < argc; ++i) args += std::string(argv[i]) + "\n";
    write_text("args", args);

    const char* mode_env = std::getenv("JNEXT_NEXTPI_STUB_MODE");
    const std::string mode = mode_env ? mode_env : "";
    if (mode == "fails") return 3;
    g_stubborn = mode == "stubborn";

    std::string chardev;
    bool monitor_stdio = false;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "-chardev") == 0) chardev = argv[i + 1];
        if (std::strcmp(argv[i], "-monitor") == 0 && std::strcmp(argv[i + 1], "stdio") == 0)
            monitor_stdio = true;
    }

    // Bounded whatever a row does or fails to do.
    esp::net::start_detached([] { sleep_ms(120000); std::exit(9); });   // not std::thread::detach (GH #328 lint)
    if (monitor_stdio) esp::net::start_detached(watch_stdin);
#ifndef _WIN32
    ::signal(SIGTERM, g_stubborn ? SIG_IGN : on_term);
#endif

    if (mode == "exit7") {
        write_pid();
        sleep_ms(1000);
        return 7;
    }

    int rc = 6;
    if (chardev.rfind("socket,", 0) == 0) {
        const std::size_t at = chardev.find("port=");
        if (at == std::string::npos) return 6;
        rc = serve_socket(static_cast<std::uint16_t>(std::strtoul(chardev.c_str() + at + 5, nullptr, 10)));
#ifndef _WIN32
    } else if (chardev.rfind("pipe,", 0) == 0) {
        const std::size_t at = chardev.find("path=");
        if (at == std::string::npos) return 6;
        rc = serve_pipe(chardev.substr(at + 5));
#endif
    }
    // SIGTERM sets g_stop without writing the marker; write it here.
    if (rc == 0 && !g_stubborn) {
        std::FILE* f = std::fopen(join(g_dir, "graceful").c_str(), "rb");
        if (f) std::fclose(f);
        else write_text("graceful", "1\n");
    }
    return rc;
}

int img_main(int argc, char** argv) {
    // `create -q -f qcow2 -b nextpi.img -F raw <overlay> 16G`
    if (argc < 3) return 2;
    std::FILE* f = std::fopen(argv[argc - 2], "wb");
    if (!f) return 1;
    std::fclose(f);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    g_dir = dir_of(argv[0]);
    if (base_name_lower(argv[0]) == "qemu-img") return img_main(argc, argv);
    return qemu_main(argc, argv);
}
